/*
* There is no specific copyright on this code as it has been
* copied from other generous developers.
* This is code that I have implemented on my lora receive 
* module and is presented as a curiosity for review.
*/

#include <SPI.h>               // Built in library
#include <LoRa.h>              // installed from Platformio
#include <Wire.h>              // Built in library
#include <SSD1306.h>           // installed from Platformio
#include <ArduinoOTA.h>        // Built in library
#include "Security.h"          // text file with Wifi username and password
#include <NTP.h>               // by Stefan Staub, installed from Platformio but also available at https://github.com/sstaub/NTP
#include <SPIFFS.h>            // Built in library
#include <ESPAsyncWebServer.h> // installed from Platformio but also available at https://github.com/me-no-dev/ESPAsyncWebServer

const String Version = "20190517-001";

// pins
const byte BuiltInLED = 2;
// Lora
#define SCK 5                         // GPIO5  -- SX1278's SCK
#define MISO 19                       // GPIO19 -- SX1278's MISO
#define MOSI 27                       // GPIO27 -- SX1278's MOSI
#define SS 18                         // GPIO18 -- SX1278's CS
#define RST 14                        // GPIO14 -- SX1278's RESET
#define DIO0 26                       // GPIO26 -- SX1278's IRQ(Interrupt Request)
const unsigned long LoraBand = 915E6; // 915E6, 868E6, 433E6
// OLED display
const byte OLEDResetPin = 16; // reset pin for OLED display
const byte OLEDSDA = 4;
const byte OLEDSCL = 15;
// wifi
const byte WiFiMaxCycle = 10;  // number of cycles before disconnect and reconnect
const int WiFiLoopWait = 2000; // wait for wifi to settle, probably not needed
const byte WiFiLoopCycle = 10; // number of cycles before reboot
const IPAddress WiFiIP(192, 168, 0, 22);
const IPAddress WiFiGateway(192, 168, 0, 1);
const IPAddress WiFiSubnet(255, 255, 0, 0);
const IPAddress WiFiPrimaryDNS(192, 168, 0, 1); //must have if not using DHCP
const IPAddress WiFiSecondaryDNS(8, 8, 4, 4);   //optional
//
const byte LEDOn = HIGH;
const byte LEDOff = LOW;
// delays
const int SerialStartDelay = 200; //delay after starting the serial interface to let things settle
// Main Loop delays
const int MainLoopCycleTime = 100;   // have a leisurely romp through the main loop
const int OTALoopCycleTime = 50;     // stop OTA being in a tight loop
const int XStartDisplayDelay = 5000; // delay the restart to give time for the web page to be displayed

// NTP refresh time
const unsigned long NTPRefresh = 60000 * 60 * 24; // refresh time in milliseconds, i.e. once per day
// delay to display message on OLED display
const int OLEDDisplayDelay = 400; // pause after displaying a message while starting up so that they can be read
// Lora packet
const String PacketPreAmble = "A1A"; // PacketPreAmble - received packet must start with this
const int MaxPacketSize = 10;        // Sanity check, anything longer than this is a bad packet

// Web Server
AsyncWebServer WebServer(80);
// NTP Server
WiFiUDP NTPUDP;
NTP NTPTime(NTPUDP);
String FormattedDate = "";
String FormattedTime = "";
// SSD1306
SSD1306 OLEDDisplay(0x3c, OLEDSDA, OLEDSCL);
// Lora packets
String RSSI = "";
String SNR = "";
String PackSize = "";
String Packet = "";
String LastGoodPacket = "";
String LastGoodPacketSize = "";
String WaterLevel = "";
String VoltageLevel = "";
float Volts = 0.0;
bool PacketFlag = false; // indicates if a packet has been received by the lora receive callback
int PacketSize = 0;      // holds the current packet size
// WiFi info
String LocalIP = "";
String LocalMac = "";
String LocalSubNet = "";
String LocalGateway = "";
String LocalDNS = "";
String WebStatus = "";
String WebRSSI = "";
String RxDate = "";
String RxTime = "";

// sub routines
//-----------------------------------------------
// common oled display setup
void OLEDMessage(String OLEDText) // must be first as used immediately after serial setup to display setup progress
{
  OLEDDisplay.clear();
  OLEDDisplay.drawString(0, 0, OLEDText);
  OLEDDisplay.display();
}

// format a numeric string with commas, mainly used to display the system information values, i.e. 240,000,000MHz clock speed
// Format a numeric string with commas, mainly used to display the system information values and file sizes, i.e. 240,000,000MHz clock speed
// Do not use for numbers with decimal places or non-mumerics
String FormatString(String RawNumber)
{
  RawNumber.trim();
  // do some sanity checks
  if (RawNumber.length() < 1)
    return "Not a valid number length";
  if (RawNumber.length() < 4)
    return RawNumber; // no need to add commas
  for (int i = 0; i < RawNumber.length(); i++)
  {
    if (!isDigit(RawNumber.charAt(i)))
      return "Not a valid whole number";
  }
  String FinalNumber = ""; // working strings
  String WorkingNumber = "";
  int j = 0;
  for (j = RawNumber.length(); j > 3; j = j - 3) // work from right hand side to left in multiples of 3, i.e. thousands
  {
    WorkingNumber = RawNumber.substring(j - 3, j); // get a block
    if ((FinalNumber == "") ? FinalNumber = WorkingNumber : FinalNumber = WorkingNumber + FinalNumber)
      ; // if it is the first then start the string else add it to the existing string
    if (j > 1)
      FinalNumber = "," + FinalNumber; // if it is a multiple of 3 and and there is more than 1 character left then add a comma
  }
  if (j <= 3)
    FinalNumber = RawNumber.substring(0, j) + FinalNumber; // add the last digits (less than 3) to the front of the number
  return FinalNumber;
}

void SerialConnect()
{
  Serial.begin(115200);
  vTaskDelay(pdMS_TO_TICKS(SerialStartDelay)); // probably unnecessary
}

void WiFiConnect()
{
  byte TempLoopCount = 0;               // used to keep track of the number of times through the outer "wifi.disconnect" loop, reboot if exceeded
  while (WiFi.status() != WL_CONNECTED) // Infinate loop - if wifi is not available then no point carrying on
  {
    TempLoopCount++;
    if (TempLoopCount > WiFiLoopCycle) // restart if we have cycled too much
      ESP.restart();
    WiFi.disconnect();      // disconnect if we have tried begin too many times
    byte TempWiFiCount = 0; // used to keep track of the number of times "wifi.begin" is called, wifi disconnect if exceeded
    Serial.print("Starting WiFi");
    while ((TempWiFiCount <= WiFiMaxCycle) && (WiFi.status() != WL_CONNECTED)) // try to connect x times
    {
      Serial.print("@");
      TempWiFiCount++;
      WiFi.begin(WiFiSSID, WiFiPassword);
      WiFi.config(WiFiIP, WiFiGateway, WiFiSubnet, WiFiPrimaryDNS, WiFiSecondaryDNS); // must be after begin else it won't connect
      vTaskDelay(pdMS_TO_TICKS(WiFiLoopWait));                                        // may not be required
      // print the status for diagnosis and keep for the web page (which will always be WL_CONNECTED otherwise you won't have web access)
      switch (WiFi.status())
      {
      case WL_IDLE_STATUS:
        WebStatus = "WL_IDLE_STATUS"; // 0
        break;
      case WL_NO_SSID_AVAIL:
        WebStatus = "WL_NO_SSID_AVAIL"; // 1
        WiFi.persistent(false);
        break;
      case WL_SCAN_COMPLETED:
        WebStatus = "WL_SCAN_COMPLETED"; // 2
        break;
      case WL_CONNECTED:
        WebStatus = "WL_CONNECTED"; // 3
        WiFi.persistent(true);
        break;
      case WL_CONNECT_FAILED:
        WebStatus = "WL_CONNECT_FAILED"; // 4
        WiFi.persistent(false);
        break;
      case WL_CONNECTION_LOST:
        WebStatus = "WL_CONNECTION_LOST"; // 5
        WiFi.persistent(false);
        break;
      case WL_DISCONNECTED:
        WebStatus = "WL_DISCONNECTED"; // 6
        break;
      case WL_NO_SHIELD:
        WebStatus = "WL_NO_SHIELD"; // 255
        break;
      default:
        WebStatus = "Undefined WiFi status";
        break;
      }
      Serial.println();
      Serial.println("WiFi Status: " + WebStatus); // print the status after each try
    }
  }
  // we must be connected, keep the values for the network web page
  LocalIP = (WiFi.localIP().toString());
  LocalSubNet = (WiFi.subnetMask().toString());
  LocalGateway = (WiFi.gatewayIP().toString());
  LocalMac = WiFi.macAddress();
  LocalDNS = (WiFi.dnsIP().toString());
  WebRSSI = String((int(WiFi.RSSI())));
  // print the info for diagnosis
  Serial.print("SSID: ");
  Serial.println(WiFiSSID);
  Serial.println("IP address: " + LocalIP);
  Serial.println("Mac Address: " + WiFi.macAddress());
  Serial.println("Subnet Mask: " + LocalSubNet);
  Serial.println("Gateway IP: " + LocalGateway);
  Serial.println("DNS: " + LocalDNS);
  Serial.println("WiFi RSSI: " + WebRSSI);
}

void FlashLED(int OnTime, int OffTime, int Repeat)
{
  if (OnTime < 0 || OnTime > 2000)
    OnTime = 0;
  if (OffTime < 0 || OffTime > 2000)
    OffTime = 0;
  if (Repeat < 0 || Repeat > 10)
    Repeat = 1;
  for (int i = 0; i < Repeat; i++)
  {
    digitalWrite(BuiltInLED, LEDOn);
    vTaskDelay(pdMS_TO_TICKS(OnTime));
    digitalWrite(BuiltInLED, LEDOff);
    vTaskDelay(pdMS_TO_TICKS(OffTime));
  }
}

void LoraProcessing() // process new packet, called from main loop based on packet flag
{
  Packet = ""; // global as also used in web server
               // read packet
  while (LoRa.available())
  {
    Packet += (char)LoRa.read();
  }
  RSSI = String(LoRa.packetRssi());  // global as also used in web server
  SNR = String(LoRa.packetSnr(), 2); // global as also used in web server
  PackSize = String(PacketSize);     // can't use raw PacketSize in OLED display text, must be string, packetsize set by onReceive routine
  OLEDDisplay.clear();
  OLEDDisplay.drawString(0, 0, "RSSI: " + RSSI + ", SNR: " + SNR);
  OLEDDisplay.drawString(0, 12, "Received " + PackSize + " bytes");

  // see if it is valid and for us
  if (PacketSize <= MaxPacketSize)
  {
    OLEDDisplay.drawString(0, 24, Packet);
    if (Packet.startsWith(PacketPreAmble)) // only process if it has the correct preamble otherwise ignore it as it's not for us
    {
      // unpack received packet
      WaterLevel = Packet.substring(3, 4); // global as also used in web server
      VoltageLevel = Packet.substring(4);
      Volts = VoltageLevel.toFloat() / 100.0; // global as also used in web server.  Sender has multiplied voltage by 100 to make it an integer so divide to get fraction back
      RxDate = FormattedDate;                 // keep the received date and time
      RxTime = FormattedTime;
      LastGoodPacket = Packet;
      LastGoodPacketSize = PackSize;
      OLEDDisplay.drawString(0, 36, "Water: " + WaterLevel + ", Voltage: " + Volts);
      OLEDDisplay.drawString(0, 48, RxDate + " " + RxTime);
      Serial.println("Packet received: " + RxDate + " " + RxTime + " - Packet:" + Packet + ", Size:" + PacketSize);
      Serial.println("Lora RSSI: " + RSSI + ", SNR: " + SNR);
      Serial.println("Water: " + WaterLevel + ", Voltage: " + Volts);
      Serial.println();
    }
    else
    { // packet doesn't match preamble, can't be for us or is corrupted
      OLEDDisplay.drawString(0, 36, "Packet not for us");
      OLEDDisplay.drawString(0, 48, FormattedDate + " " + FormattedTime);
    }
  }
  else
  { // packet is longer than MaxPacketSize
    OLEDDisplay.drawString(0, 24, Packet.substring(0, MaxPacketSize));
    OLEDDisplay.drawString(0, 36, "Packet too long");
    OLEDDisplay.drawString(0, 48, FormattedDate + " " + FormattedTime);
  }
  OLEDDisplay.display();
}

void LoraReceive(int packetSize) // a packet has been received, set flag for processing
{
  PacketFlag = true;       // semaphore for lora processing as can not put display commands in same callback as lora processing
  PacketSize = packetSize; // global as used in Lora and web server
}

// routines to process web page variables, called iteratively until all page variables have been processed
String Processor(const String &var) // process the index.html variables
{
  if (var == "FormattedDate")
    return FormattedDate;
  if (var == "FormattedTime")
    return FormattedTime;
  if (var == "Version")
    return Version;
  return String();
}

String ProcessNetwork(const String &var) // process the network.html variables
{
  if (var == "FormattedDate")
    return FormattedDate;
  if (var == "FormattedTime")
    return FormattedTime;
  if (var == "WIFISSID")
    return WiFiSSID;
  if (var == "LocalIP")
    return LocalIP;
  if (var == "LocalMac")
    return LocalMac;
  if (var == "LocalSubNet")
    return LocalSubNet;
  if (var == "LocalGateway")
    return LocalGateway;
  if (var == "LocalDNS")
    return LocalDNS;
  if (var == "WebStatus")
    return WebStatus;
  if (var == "WebRSSI")
    return WebRSSI;
  return String();
}

String ProcessWater(const String &var) // process the water.html variables
{
  if (var == "FormattedDate")
    return FormattedDate;
  if (var == "FormattedTime")
    return FormattedTime;
  if (var == "Version")
    return Version;
  if (var == "RxDate")
    return RxDate;
  if (var == "RxTime")
    return RxTime;
  if (var == "RSSI")
    return RSSI;
  if (var == "SNR")
    return SNR;
  if (var == "Packet")
    return LastGoodPacket;
  if (var == "PacketSize")
    return String(LastGoodPacketSize);
  if (var == "WaterLevel")
    return WaterLevel;
  if (var == "Volts")
    return String(Volts);
  return String();
}

String ProcessSystem(const String &var) // process the system.html variables
{
  if (var == "ChipID")
    return String((unsigned long)ESP.getEfuseMac());
  if (var == "ChipRevision")
    return String(ESP.getChipRevision());
  if (var == "ChipFrequency")
    return FormatString(String(ESP.getCpuFreqMHz())) + "MHz";
  if (var == "FlashSize")
    return FormatString(String(ESP.getFlashChipSize())) + "B";
  if (var == "FlashSpeed")
    return FormatString(String(ESP.getFlashChipSpeed())) + "Hz";
  if (var == "HeapSize")
    return FormatString(String(ESP.getHeapSize())) + "B";
  if (var == "FreeHeap")
    return FormatString(String(ESP.getFreeHeap())) + "B";
  if (var == "SketchSize")
    return FormatString(String(ESP.getSketchSize())) + "B";
  if (var == "SketchSpaceFree")
    return FormatString(String(ESP.getFreeSketchSpace())) + "B";
  if (var == "FormattedDate")
    return FormattedDate;
  if (var == "FreeHeap")
    return FormatString(String(ESP.getFreeHeap())) + "B";
  if (var == "FormattedTime")
    return FormattedTime;
  return String();
}

// set up processing on core 0
void OTACore0(void *p) // run OTA on core 0, makes it responsive
{
  // OTA callbacks
  ArduinoOTA.onStart([]() {
    OLEDMessage("OTA starting");
    Serial.println("Starting OTA");
    vTaskDelay(pdMS_TO_TICKS(400));
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH)
      type = "sketch";
    else // U_SPIFFS
    {
      SPIFFS.end();
      type = "filesystem";
      // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
    }
  });
  ArduinoOTA.onEnd([]() { Serial.println("\nEnd"); });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR)
      Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR)
      Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR)
      Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR)
      Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR)
      Serial.println("End Failed");
  });
  // start OTA      //
  ArduinoOTA.begin(); //
  OLEDMessage("OTA started");
  Serial.println("OTA started");
  vTaskDelay(pdMS_TO_TICKS(OLEDDisplayDelay));

  while (true)
  {
    // standing section for OTA updates
    ArduinoOTA.handle();
    vTaskDelay(pdMS_TO_TICKS(OTALoopCycleTime));
  }
}

// main setup
//-----------------------------
void setup()
{
  // set up led
  pinMode(BuiltInLED, OUTPUT);

  //Signal progress
  FlashLED(250, 0, 1);

  // setup oled display, must be done first as is used to display during the setup progress
  pinMode(OLEDResetPin, OUTPUT);
  digitalWrite(OLEDResetPin, LEDOff);
  digitalWrite(OLEDResetPin, LEDOn);
  OLEDDisplay.init();
  OLEDDisplay.flipScreenVertically();
  OLEDDisplay.setFont(ArialMT_Plain_10);
  OLEDDisplay.setTextAlignment(TEXT_ALIGN_LEFT);
  OLEDMessage("OLED started");
  vTaskDelay(pdMS_TO_TICKS(OLEDDisplayDelay));

  // Start serial
  SerialConnect();
  OLEDMessage("Serial started");
  Serial.println("Serial started");
  vTaskDelay(pdMS_TO_TICKS(OLEDDisplayDelay));

  // Start WiFi
  OLEDMessage("Wifi starting");
  WiFiConnect();
  OLEDMessage("Wifi started");
  Serial.println("Wifi started");
  vTaskDelay(pdMS_TO_TICKS(OLEDDisplayDelay));

  // Start NTP client
  NTPTime.ntpServer("msltime.irl.cri.nz");
  NTPTime.updateInterval(NTPRefresh);
  NTPTime.ruleSTD("NZST", First, Sun, Apr, 3, 12 * 60);     // first sunday in April at 3:00, timezone 12 hours
  NTPTime.ruleDST("NZDT", Last, Sun, Sep, 3, 12 * 60 + 60); // last sunday in September at 3:00, timezone 13 hours
  NTPTime.begin();
  NTPTime.update();
  OLEDMessage("NTP started");
  Serial.println("NTP started");
  vTaskDelay(pdMS_TO_TICKS(OLEDDisplayDelay));

  // Start SPIFFS
  if (!SPIFFS.begin(true)) // if there is an error ignore it
  {
    Serial.println("An Error has occurred while mounting SPIFFS");
  }
  else
  {
    Serial.println("SPIFFS started.");
  }

  // callbacks to respond to web request
  WebServer.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(SPIFFS, "/index.html", String(), false, ProcessWater);
  });
  WebServer.on("/style.css", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(SPIFFS, "/style.css", "text/css");
  });
  WebServer.on("/network", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(SPIFFS, "/network.html", String(), false, ProcessNetwork);
    Serial.println("Network status: " + WebStatus);
  });
  WebServer.on("/system", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(SPIFFS, "/system.html", String(), false, ProcessSystem);
  });
  WebServer.on("/restart", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(SPIFFS, "/restart.html", String(), false, Processor);
  });
  // Restart the esP32
  WebServer.on("/xstart", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/html", "<h1>ESP being restarted</h1>");
    vTaskDelay(pdMS_TO_TICKS(XStartDisplayDelay)); // make sure everything is sent and displayed
    ESP.restart();
  });

  // start the async web server
  WebServer.begin();
  OLEDMessage("Web server started");
  Serial.println("HTTP server started");
  vTaskDelay(pdMS_TO_TICKS(OLEDDisplayDelay));

  // start lora
  SPI.begin(SCK, MISO, MOSI, SS);
  LoRa.setPins(SS, RST, DIO0);
  if (!LoRa.begin(LoraBand)) // if it doesn't start there is no point carrying on
  {
    OLEDMessage("LoRa failed to start");
    Serial.println("LoRa failed to start");
    while (true)
    {
    }
  }
  LoRa.setSyncWord(0xA1);      // ranges from 0-0xFF, default 0x34, see API docs - doesn't seem to work reliably
  LoRa.onReceive(LoraReceive); // setup callback
  LoRa.receive();              // put into receive mode
  OLEDMessage("Lora started");
  Serial.println("Lora started");
  vTaskDelay(pdMS_TO_TICKS(OLEDDisplayDelay));

  // start OTA monitoring task on core 0
  xTaskCreatePinnedToCore(OTACore0, "OTACore0", 4096, NULL, 0, NULL, 0);

  // finished setup
  OLEDMessage("Setup finished");
  Serial.println("Setup finished");
  FlashLED(200, 200, 3);
}

void loop()
{
  // date and time
  NTPTime.update(); // setup the date/time strings used for the OLED display, serial out, and web
  FormattedDate = NTPTime.formattedTime("%d %B %Y");
  FormattedTime = NTPTime.formattedTime("%T");
  if (PacketFlag) // if we have a packet then process it, OLED display commands can't be in the receive callback so need to process independantly
  {
    PacketFlag = false;
    FlashLED(100, 100, 2);
    LoraProcessing();
  }
  vTaskDelay(pdMS_TO_TICKS(MainLoopCycleTime)); // probably not needed
}