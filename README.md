Pocket WiFi Storage (8MB SPI Flash)
This project transforms a standard 8MB SPI NOR Flash IC (eFeon EN25T80) into a portable, wireless storage device using a Wemos D1 R1 (ESP8266) board. The most unique part? The entire project was coded and deployed using only an Android phone, without any laptop!

✨ Key Features
No Libraries Used: Built using pure Arduino C++ with Raw SPI Commands for direct hardware communication.

Standalone WiFi: Operates as a WiFi Access Point; no external router or internet required.

Web Interface: Access, upload, view, and download files directly via a mobile browser at 192.168.4.1.

OTA Updates: Supports Over-The-Air (Wireless) code updates for future improvements.

Ultra Low Cost: Built with a cheap ₹8 Flash IC and a budget ESP8266 board.

🛠️ Hardware Requirements
Flash IC: eFeon EN25T80 (8MB SPI Flash)

Controller: Wemos D1 R1 (or any ESP8266 based board)

Power: Micro USB cable via Mobile Charger or Power Bank

🔌 Connection Details (Wiring)
Connect the Flash IC to the ESP8266 as follows:

VCC connects to 3.3V

GND connects to GND

CS connects to D8 (GPIO15)

DO (MISO) connects to D7 (GPIO12)

DI (MOSI) connects to D6 (GPIO13)

CLK (SCK) connects to D5 (GPIO14)

🚀 How to Get Started
Flash the Code: Upload the .ino file to your Wemos D1 board.

Connect to WiFi: On your phone, look for the WiFi network named "Pocket_Storage".

Open Browser: Navigate to http://192.168.4.1

Manage Files: Start uploading images, text, or audio files directly to the 8MB chip!

📌 Note
This is my second hardware hacking project, and I am actively working on updates. If you have suggestions for optimization or new features, feel free to contribute or open an issue!
