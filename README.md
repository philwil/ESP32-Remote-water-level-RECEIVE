# ESP32 Remote water level
Monitor tank level remotely using esp32, oled display, lora, OTA, and Async Web server.

I need to monitor the level of a water tank that is outside of wifi coverage.  The level is either full or not full.  I'm using two TTGO esp32 modules with integrated OLED display and Lora.  I use OTA to update the esp32 module firmware.  I use deep sleep on the sending esp32 to conserve battery but this can be overridden by rebooting the esp32 then pressing the user button within 2 seconds.  This puts the esp32 into local mode that displays status and allows OTA.  The receiver is within my wifi network.  On the receiver I have an async web server so that I can check the water level status.  I am a noob esp32 programmer and use PlatformIO from within Microsoft Visual Studio Code as my development platform.

I spent quite a bit of time searching the internet to arrive at the included send and receive programs so I am publishing these to help other noobs get up to speed more quickly.  They are not the best or only ways to do this but they work for me.  None of the code is original it has all been cobbled together using code from the generous developers on the internet.

In platformio both send and receive are developed as seperate projects and each has a platformio.ini file.  To get the Over The Air updates to work you need to initially download the firmware with the OTA code using a usb cable then put "upload_port = 192.168.n.nn" (using the IP address used within each of the programs) in the associated platformio.ini file as it uses this to wirelessly connect to the esp32.  Once the initial code is downloaded and the platformio.ini files changed then a normal rebuild (the arrow icon in the bottom left of the VS Code screen) will connect to the esp32 over wifi using the platformio.ini IP address.  I don't know how this is done using the Arduino development environment.

The html and css files are created in a directory called Data that sits at the SAME LEVEL as the src directory.  The files in Data are uploaded from Platformio to the esp32 using: click on Terminal/Run Task/PlatformIO: Uplaod File System image.  This will be over usb or wifi depending on how you've set it up.

The security.h file goes in the src directory and contains your wifi SSID and password.

All the libraries were installed using the platformio library system.  Lora is ID1167, SSD1306 is ESP8266 SSD1306 ID562, web server is ESP Async Webserver ID306 also available at https://github.com/me-no-dev/ESPAsyncWebServer, NTP by Stefan Staub, installed from Platformio but also available at https://github.com/sstaub/NTP, ArduinoOTA is built in.

I had problems with the lora receive buffer losing its place however the suggestion on the Lora github site was to comment out line 583 (version 0.5.0) this solved the problem for me - https://github.com/sandeepmistry/arduino-LoRa/issues/218.  You will need to go to your lora library directory and comment out what should be line 583 in lora.cpp, i.e.  582: // reset FIFO address, 583: writeRegister(REG_FIFO_ADDR_PTR, 0); Double check that you have the right line, if in doubt don't do it.

Site that I have found useful are:
https://github.com/G6EJD, 
https://github.com/LilyGO, 
http://arduino.esp8266.com/Arduino/versions/2.0.0/doc/ota_updates/ota_updates.html, 
https://github.com/YogoGit, 
https://techtutorialsx.com/2017/12/01/esp32-arduino-asynchronous-http-webserver/, 
http://www.sensorsiot.org/, 
https://github.com/lucadentella, 
pcbreflux, 
educ8s.tv, 
https://techtutorialsx.com/category/esp32/page/5/, 
and many others, thankyou.
