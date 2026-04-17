#include <SPI.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ArduinoOTA.h>

const char* AP_SSID = "FlashIC_Storage";
const char* AP_PASS = "12345678";

#define CS_PIN           D8
#define CMD_WREN         0x06
#define CMD_RDSR         0x05
#define CMD_READ         0x03
#define CMD_PAGE_PROG    0x02
#define CMD_SECTOR_ERASE 0x20
#define CMD_CHIP_ERASE   0xC7
#define SLOT_SIZE        32768
#define HEADER_SIZE      32
#define NAME_SIZE        16
#define MAX_SLOTS        256

int currentSlot = 0;
bool icReady = false;
bool uploadStarted = false;
int uploadSize = 0;
int uploadSlot = 0;
uint32_t uploadAddr = 0;
String uploadFilename = "";

ESP8266WebServer server(80);

void cs_low()  { digitalWrite(CS_PIN, LOW); }
void cs_high() { digitalWrite(CS_PIN, HIGH); }

void writeEnable() {
  cs_low();
  SPI.transfer(CMD_WREN);
  cs_high();
  delayMicroseconds(10);
}

bool isBusy() {
  cs_low();
  SPI.transfer(CMD_RDSR);
  byte s = SPI.transfer(0x00);
  cs_high();
  return (s & 0x01);
}

void waitReady() {
  while (isBusy()) delay(10);
}

void sectorErase(uint32_t addr) {
  writeEnable();
  waitReady();
  cs_low();
  SPI.transfer(CMD_SECTOR_ERASE);
  SPI.transfer((addr >> 16) & 0xFF);
  SPI.transfer((addr >> 8)  & 0xFF);
  SPI.transfer( addr        & 0xFF);
  cs_high();
  waitReady();
}

void chipErase() {
  writeEnable();
  waitReady();
  cs_low();
  SPI.transfer(CMD_CHIP_ERASE);
  cs_high();
  while (isBusy()) delay(500);
}

void writePage(uint32_t addr, byte* buf, int len) {
  writeEnable();
  waitReady();
  cs_low();
  SPI.transfer(CMD_PAGE_PROG);
  SPI.transfer((addr >> 16) & 0xFF);
  SPI.transfer((addr >> 8)  & 0xFF);
  SPI.transfer( addr        & 0xFF);
  for (int i = 0; i < len; i++) SPI.transfer(buf[i]);
  cs_high();
  waitReady();
}

void eraseSlot(int slot) {
  uint32_t startAddr = (uint32_t)slot * SLOT_SIZE;
  int sectors = SLOT_SIZE / 4096;
  for (int i = 0; i < sectors; i++) {
    sectorErase(startAddr + (uint32_t)i * 4096);
    yield();
  }
}

void writeChunk(uint32_t addr, byte* data, int len) {
  int written = 0;
  while (written < len) {
    int chunk = len - written;
    if (chunk > 256) chunk = 256;
    uint32_t pageEnd = (addr & 0xFFFFFF00) + 256;
    if (addr + chunk > pageEnd) chunk = (int)(pageEnd - addr);
    writePage(addr, data + written, chunk);
    written += chunk;
    addr += chunk;
  }
}

void readBytes(uint32_t addr, byte* buf, int len) {
  cs_low();
  SPI.transfer(CMD_READ);
  SPI.transfer((addr >> 16) & 0xFF);
  SPI.transfer((addr >> 8)  & 0xFF);
  SPI.transfer( addr        & 0xFF);
  for (int i = 0; i < len; i++) buf[i] = SPI.transfer(0x00);
  cs_high();
}

void writeHeader(int slot, String filename, int filesize) {
  uint32_t addr = (uint32_t)slot * SLOT_SIZE;
  byte header[HEADER_SIZE];
  memset(header, 0xFF, HEADER_SIZE);
  header[0] = 0xAB;
  header[1] = 0xCD;
  header[2] = (filesize >> 24) & 0xFF;
  header[3] = (filesize >> 16) & 0xFF;
  header[4] = (filesize >> 8)  & 0xFF;
  header[5] =  filesize        & 0xFF;
  int nlen = filename.length();
  if (nlen > NAME_SIZE - 1) nlen = NAME_SIZE - 1;
  for (int i = 0; i < nlen; i++) header[6 + i] = (byte)filename[i];
  writePage(addr, header, HEADER_SIZE);
}

bool readSlotHeader(int slot, String &filename, int &filesize) {
  uint32_t addr = (uint32_t)slot * SLOT_SIZE;
  byte header[HEADER_SIZE];
  readBytes(addr, header, HEADER_SIZE);
  if (header[0] != 0xAB || header[1] != 0xCD) return false;
  filesize = ((int)header[2] << 24) |
             ((int)header[3] << 16) |
             ((int)header[4] << 8)  |
              (int)header[5];
  char namebuf[NAME_SIZE + 1];
  memset(namebuf, 0, NAME_SIZE + 1);
  for (int i = 0; i < NAME_SIZE; i++) {
    if (header[6+i] == 0xFF || header[6+i] == 0x00) break;
    namebuf[i] = (char)header[6+i];
  }
  filename = String(namebuf);
  return true;
}

int findNextSlot() {
  for (int i = 0; i < MAX_SLOTS; i++) {
    byte magic[2];
    readBytes((uint32_t)i * SLOT_SIZE, magic, 2);
    if (magic[0] != 0xAB || magic[1] != 0xCD) return i;
  }
  return MAX_SLOTS;
}

String getContentType(String fname) {
  fname.toLowerCase();
  if (fname.endsWith(".jpg"))  return "image/jpeg";
  if (fname.endsWith(".jpeg")) return "image/jpeg";
  if (fname.endsWith(".png"))  return "image/png";
  if (fname.endsWith(".gif"))  return "image/gif";
  if (fname.endsWith(".mp3"))  return "audio/mpeg";
  if (fname.endsWith(".wav"))  return "audio/wav";
  if (fname.endsWith(".txt"))  return "text/plain";
  if (fname.endsWith(".pdf"))  return "application/pdf";
  return "application/octet-stream";
}

bool isImage(String fname) {
  fname.toLowerCase();
  return fname.endsWith(".jpg") || fname.endsWith(".jpeg") ||
         fname.endsWith(".png") || fname.endsWith(".gif");
}

bool isAudio(String fname) {
  fname.toLowerCase();
  return fname.endsWith(".mp3") || fname.endsWith(".wav");
}

bool isText(String fname) {
  fname.toLowerCase();
  return fname.endsWith(".txt") || fname.endsWith(".html") ||
         fname.endsWith(".csv") || fname.endsWith(".json");
}

const char CSS[] PROGMEM =
  "body{font-family:Arial;background:#1a1a2e;color:#eee;margin:0;padding:12px}"
  "h1{color:#00d4ff;text-align:center;font-size:20px;margin:8px 0}"
  "h2{color:#00d4ff;font-size:16px;word-break:break-all;margin:8px 0}"
  ".card{background:#16213e;border-radius:10px;padding:14px;margin:10px 0}"
  ".info{background:#0f3460;padding:10px;border-radius:8px;font-size:13px}"
  ".btn{background:#00d4ff;color:#000;border:none;padding:8px 14px;"
  "border-radius:6px;font-size:13px;cursor:pointer;margin:2px}"
  ".btnr{background:#ff4444;color:#fff;border:none;padding:10px;"
  "border-radius:6px;font-size:14px;cursor:pointer;width:100%}"
  ".btng{background:#44ff88;color:#000;border:none;padding:12px;"
  "border-radius:6px;font-size:15px;cursor:pointer;width:100%;margin:6px 0}"
  ".btnv{background:#aa44ff;color:#fff;border:none;padding:8px 14px;"
  "border-radius:6px;font-size:13px;cursor:pointer;margin:2px}"
  ".btno{background:#ff8800;color:#fff;border:none;padding:10px;"
  "border-radius:6px;font-size:14px;cursor:pointer;width:100%;margin:4px 0}"
  "input[type=file]{width:100%;padding:10px;margin:8px 0;box-sizing:border-box;"
  "background:#0f3460;color:#eee;border:2px solid #00d4ff;border-radius:8px;font-size:14px}"
  ".frow{padding:10px 0;border-bottom:1px solid #2a2a4e}"
  ".fname{font-size:14px;word-break:break-all;margin-bottom:4px;color:#fff}"
  ".fmeta{color:#888;font-size:11px;margin-bottom:6px}"
  ".bar{background:#333;border-radius:4px;height:10px;margin:6px 0}"
  ".barb{background:#00aaff;border-radius:4px;height:10px}"
  ".warn{color:#ffaa00;font-size:12px;margin-top:4px}"
  ".otabox{background:#1a2a1a;border:1px solid #44ff88;"
  "border-radius:8px;padding:10px;margin:6px 0;font-size:12px;color:#aaa}"
  "pre{background:#0f3460;padding:12px;border-radius:8px;"
  "white-space:pre-wrap;word-break:break-all;font-size:12px;margin:8px 0}"
  "img{max-width:100%;border-radius:8px;display:block;margin:8px auto}"
  "audio{width:100%;margin:8px 0}";

String pageHead(String title) {
  String h = "<!DOCTYPE html><html><head>";
  h += "<meta charset=UTF-8>";
  h += "<meta name=viewport content='width=device-width,initial-scale=1'>";
  h += "<title>";
  h += title;
  h += "</title><style>";
  h += FPSTR(CSS);
  h += "</style></head><body>";
  return h;
}

void handleRoot() {
  int usedKB = (currentSlot * SLOT_SIZE) / 1024;
  int totalKB = 8192;
  int pct = (usedKB * 100) / totalKB;

  String h = pageHead("Flash Storage");
  h += "<h1>Flash IC Storage</h1>";

  h += "<div class=card><div class=info>";
  h += "Files: <b>";
  h += String(currentSlot);
  h += "</b> | Used: <b>";
  h += String(usedKB);
  h += "KB</b> | Free: <b>";
  h += String(totalKB - usedKB);
  h += "KB</b></div>";
  h += "<div class=bar><div class=barb style='width:";
  h += String(pct);
  h += "%'></div></div></div>";

  h += "<div class=card><b>File Upload:</b><br><br>";
  h += "<form method=POST action=/upload enctype=multipart/form-data>";
  h += "<input type=file name=file>";
  h += "<button class=btng type=submit>Upload karo</button>";
  h += "</form>";
  h += "<div class=warn>Max: 30KB per file</div></div>";

  h += "<div class=card><b>Code Update (OTA):</b>";
  h += "<div class=otabox>ArduinoDroid: Tools - Port - Network - 192.168.4.1<br>";
  h += "Ya browser se .bin upload karo:</div>";
  h += "<form method=POST action=/otaupdate enctype=multipart/form-data>";
  h += "<input type=file name=firmware>";
  h += "<button class=btno type=submit>Firmware Update</button>";
  h += "</form></div>";

  h += "<div class=card><b>Files:</b><br>";
  if (currentSlot == 0) {
    h += "<br><center><i style='color:#666'>Koi file nahi.</i></center>";
  } else {
    for (int i = 0; i < currentSlot; i++) {
      String fname;
      int fsize;
      if (readSlotHeader(i, fname, fsize)) {
        h += "<div class=frow><div class=fname>";
        h += fname;
        h += "</div><div class=fmeta>";
        h += String(fsize);
        h += " bytes</div>";
        h += "<a href='/view?slot=";
        h += String(i);
        h += "'><button class=btnv>View</button></a> ";
        h += "<a href='/download?slot=";
        h += String(i);
        h += "'><button class=btn>Save</button></a></div>";
      }
    }
  }
  h += "</div>";

  h += "<div class=card>";
  h += "<form method=POST action=/erase>";
  h += "<button class=btnr type=submit>Sab Data Mita Do</button>";
  h += "</form></div></body></html>";
  server.send(200, "text/html", h);
}

void handleView() {
  if (!server.hasArg("slot")) { server.send(400, "text/plain", "Error"); return; }
  int slot = server.arg("slot").toInt();
  String fname;
  int fsize;
  if (!readSlotHeader(slot, fname, fsize)) { server.send(404, "text/plain", "Nahi mila"); return; }

  String h = pageHead(fname);
  h += "<h2>";
  h += fname;
  h += "</h2>";
  h += "<div class=info style='font-size:12px;color:#aaa'>Size: ";
  h += String(fsize);
  h += " bytes | Slot: ";
  h += String(slot);
  h += "</div><br>";
  h += "<a href='/'><button class=btn>Back</button></a> ";
  h += "<a href='/download?slot=";
  h += String(slot);
  h += "'><button class=btn>Download</button></a><br><br>";

  if (isImage(fname)) {
    h += "<img src='/download?slot=";
    h += String(slot);
    h += "' alt='img'>";
  } else if (isAudio(fname)) {
    h += "<audio controls><source src='/download?slot=";
    h += String(slot);
    h += "' type='";
    h += getContentType(fname);
    h += "'></audio>";
  } else if (isText(fname)) {
    h += "<pre>";
    uint32_t addr = (uint32_t)slot * SLOT_SIZE + HEADER_SIZE;
    int rem = fsize;
    byte buf[64];
    while (rem > 0) {
      int chunk = rem > 64 ? 64 : rem;
      readBytes(addr, buf, chunk);
      for (int i = 0; i < chunk; i++) {
        char c = (char)buf[i];
        if (c == '<') h += "&lt;";
        else if (c == '>') h += "&gt;";
        else if (c == '&') h += "&amp;";
        else if (c >= 0x20 || c == '\n' || c == '\r') h += c;
      }
      addr += chunk;
      rem -= chunk;
    }
    h += "</pre>";
  } else {
    h += "<pre>Binary file - Size: ";
    h += String(fsize);
    h += " bytes\n\nHEX:\n";
    uint32_t addr = (uint32_t)slot * SLOT_SIZE + HEADER_SIZE;
    int show = fsize > 64 ? 64 : fsize;
    byte buf[64];
    readBytes(addr, buf, show);
    for (int i = 0; i < show; i++) {
      if (buf[i] < 0x10) h += "0";
      h += String(buf[i], HEX);
      h += " ";
      if ((i + 1) % 16 == 0) h += "\n";
    }
    h += "</pre>";
  }
  h += "</body></html>";
  server.send(200, "text/html", h);
}

void handleDownload() {
  if (!server.hasArg("slot")) { server.send(400, "text/plain", "Error"); return; }
  int slot = server.arg("slot").toInt();
  String fname;
  int fsize;
  if (!readSlotHeader(slot, fname, fsize)) { server.send(404, "text/plain", "Nahi mila"); return; }
  String ct = getContentType(fname);
  server.sendHeader("Content-Disposition", "attachment; filename=" + fname);
  server.setContentLength(fsize);
  server.send(200, ct, "");
  uint32_t addr = (uint32_t)slot * SLOT_SIZE + HEADER_SIZE;
  int rem = fsize;
  byte buf[128];
  while (rem > 0) {
    int chunk = rem > 128 ? 128 : rem;
    readBytes(addr, buf, chunk);
    server.client().write(buf, chunk);
    addr += chunk;
    rem -= chunk;
    yield();
  }
}

void handleUploadPage() {
  String h = pageHead("Upload Done");
  h += "<div class=card><b>Upload ho gaya!</b><br><br>";
  h += "<a href='/'><button class=btn>Home</button></a>";
  h += "</div></body></html>";
  server.send(200, "text/html", h);
}

void handleFileUpload() {
  HTTPUpload& upload = server.upload();
  if (upload.status == UPLOAD_FILE_START) {
    uploadFilename = upload.filename;
    if (uploadFilename.length() == 0) uploadFilename = "file.bin";
    uploadSlot = currentSlot;
    eraseSlot(uploadSlot);
    uploadAddr = (uint32_t)uploadSlot * SLOT_SIZE + HEADER_SIZE;
    uploadStarted = true;
    uploadSize = 0;
    Serial.print("Upload: ");
    Serial.println(uploadFilename);
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (uploadStarted) {
      int maxData = SLOT_SIZE - HEADER_SIZE;
      int chunk = upload.currentSize;
      if (uploadSize + chunk > maxData) chunk = maxData - uploadSize;
      if (chunk > 0) {
        writeChunk(uploadAddr, upload.buf, chunk);
        uploadAddr += chunk;
        uploadSize += chunk;
        Serial.print(".");
      }
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (uploadStarted) {
      writeHeader(uploadSlot, uploadFilename, uploadSize);
      currentSlot++;
      uploadStarted = false;
      Serial.println("");
      Serial.print("Done: ");
      Serial.print(uploadSize);
      Serial.println(" bytes");
    }
  }
}

void handleOtaPage() {
  String h = pageHead("OTA Update");
  h += "<div class=card>";
  if (Update.hasError()) {
    h += "<b style='color:#ff4444'>Update Failed!</b>";
  } else {
    h += "<b style='color:#44ff88'>Success! Restarting...</b>";
  }
  h += "<br><br><a href='/'><button class=btn>Home</button></a>";
  h += "</div></body></html>";
  server.send(200, "text/html", h);
  delay(1000);
  ESP.restart();
}

void handleOtaUpload() {
  HTTPUpload& upload = server.upload();
  if (upload.status == UPLOAD_FILE_START) {
    Serial.println("OTA Start...");
    uint32_t maxSpace = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
    if (!Update.begin(maxSpace)) {
      Serial.println("OTA: No space!");
    }
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
      Serial.println("OTA Write Error!");
    }
    Serial.print(".");
  } else if (upload.status == UPLOAD_FILE_END) {
    if (Update.end(true)) {
      Serial.println("\nOTA Success!");
    } else {
      Serial.println("\nOTA Failed!");
    }
  }
}

void handleErase() {
  chipErase();
  currentSlot = 0;
  server.sendHeader("Location", "/");
  server.send(303);
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  pinMode(CS_PIN, OUTPUT);
  cs_high();
  SPI.begin();
  SPI.setFrequency(500000);
  SPI.setDataMode(SPI_MODE0);
  SPI.setBitOrder(MSBFIRST);
  delay(500);

  Serial.println("IC scan...");
  currentSlot = findNextSlot();
  Serial.print("Files found: ");
  Serial.println(currentSlot);

  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);
  IPAddress ip = WiFi.softAPIP();
  Serial.print("Hotspot: ");
  Serial.println(AP_SSID);
  Serial.print("Browser: http://");
  Serial.println(ip);

  ArduinoOTA.setHostname("FlashIC");
  ArduinoOTA.setPassword("ota12345");
  ArduinoOTA.onStart([]() { Serial.println("OTA Start..."); });
  ArduinoOTA.onEnd([]() { Serial.println("OTA Done!"); });
  ArduinoOTA.onProgress([](unsigned int p, unsigned int t) {
    Serial.print("OTA: ");
    Serial.print(p * 100 / t);
    Serial.println("%");
  });
  ArduinoOTA.onError([](ota_error_t e) {
    Serial.print("OTA Error: ");
    Serial.println(e);
  });
  ArduinoOTA.begin();

  server.on("/", handleRoot);
  server.on("/upload", HTTP_POST, handleUploadPage, handleFileUpload);
  server.on("/download", handleDownload);
  server.on("/view", handleView);
  server.on("/erase", HTTP_POST, handleErase);
  server.on("/otaupdate", HTTP_POST, handleOtaPage, handleOtaUpload);
  server.begin();

  Serial.println("Ready!");
  icReady = true;
}

void loop() {
  ArduinoOTA.handle();
  if (icReady) server.handleClient();
}
