/**The MIT License (MIT)
  Copyright (c) 2015 by Daniel Eichhorn
  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:
  The above copyright notice and this permission notice shall be included in all
  copies or substantial portions of the Software.
  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYBR_DATUM HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
  SOFTWARE.
  See more at http://blog.squix.ch

  Adapted by Bodmer to use the faster TFT_eSPI library:
  https://github.com/Bodmer/TFT_eSPI

  Plus:
  Minor changes to text placement and auto-blanking out old text with background colour padding
  Moon phase text added
  Forecast text lines are automatically split onto two lines at a central space (some are long!)
  Time is printed with colons aligned to tidy display
  Min and max forecast temperatures spaced out
  The ` character has been changed to a degree symbol in the 36 point font
  New smart WU jpeg splash startup screen and updated progress messages
  Display does not need to be blanked between updates
  Icons nudged about slightly to add wind direction + speed
*/

#include <Arduino.h>

#include <SPI.h>
#include <SD.h>
#include <TFT_eSPI.h> // Hardware-specific library

// Additional UI functions
#include "GfxUi.h"

#include <Fonts/GFXFF/gfxfont.h>
// Fonts created by http://oleddisplay.squix.ch/
#if defined(ILI9341_DRIVER)
#include "ArialRoundedMTBold_14.h"
#include "ArialRoundedMTBold_36.h"
#elif defined(ST7735_DRIVER)
#include "DejaVu_Sans_Condensed_Bold_8.h"
#include "DejaVu_Sans_Condensed_Bold_10.h"
#endif

// Download helper
#include "WebResource.h"

#include <ESP8266WiFi.h>
#include <Ticker.h>
#include <ArduinoOTA.h>
#include <ESP8266mDNS.h>
#include <DNSServer.h>

#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <SPIFFSEditor.h>
const char* http_username = "admin";
const char* http_password = "admin";

// Helps with connecting to internet
#include <ESPAsyncWiFiManager.h>

// check settings.h for adapting to your needs
#include "settings.h"
#include <JsonListener.h>
#include <WundergroundClient.h>
#include "TimeClient.h"

#include <FS.h>

//ONEWIRE
#include <OneWire.h>
#include <DallasTemperature.h>
#define ONE_WIRE_BUS D3
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);
DeviceAddress insideThermometer, outsideThermometer;
String curr_inhouse, curr_outhouse = "--";

//ONEWIRE

// HOSTNAME for OTA update
#define HOSTNAME "ESP8266-OTA-"

/*****************************
   Important: see settings.h to configure your settings!!!
 * ***************************/

TFT_eSPI tft = TFT_eSPI();       // Invoke custom library

boolean booted = true;

GfxUi ui = GfxUi(&tft);

WebResource webResource;
TimeClient timeClient(UTC_OFFSET);

// Set to false, if you prefere imperial/inches, Fahrenheit
WundergroundClient wunderground(IS_METRIC);

// flag changed in the ticker function every 10 minutes
bool readyForWeatherUpdate = false;
// flag changed in the ticker function every 1 minute
bool readyForOWUpdate = true;

Ticker ticker;

//declaring prototypes
void configModeCallback (AsyncWiFiManager *myWiFiManager);
void downloadCallback(String filename, int16_t bytesDownloaded, int16_t bytesTotal);
ProgressCallback _downloadCallback = downloadCallback;
void downloadResources();
void updateData();
void drawProgress(uint8_t percentage, String text);
void drawTime();
void drawCurrentWeather();
void drawCurrentTemp();
void drawForecast();
void drawForecastDetail(uint16_t x, uint16_t y, uint8_t dayIndex);
String getMeteoconIcon(String iconText);
void drawAstronomy();
void drawSeparator(uint16_t y);
void setReadyForWeatherUpdate();
void setReadyForOWUpdate();
int rightOffset(String text, String sub);
void fillSegment(int x, int y, int start_angle, int sub_angle, int r, unsigned int colour);
int splitIndex(String text);

long lastDownloadUpdate = millis();

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
AsyncEventSource events("/events");
DNSServer dns;

void onWsEvent(AsyncWebSocket * server, AsyncWebSocketClient * client, AwsEventType type, void * arg, uint8_t *data, size_t len){
  if(type == WS_EVT_CONNECT){
    Serial.printf("ws[%s][%u] connect\n", server->url(), client->id());
    client->printf("Hello Client %u :)", client->id());
    client->ping();
  } else if(type == WS_EVT_DISCONNECT){
    Serial.printf("ws[%s][%u] disconnect: %u\n", server->url(), client->id());
  } else if(type == WS_EVT_ERROR){
    Serial.printf("ws[%s][%u] error(%u): %s\n", server->url(), client->id(), *((uint16_t*)arg), (char*)data);
  } else if(type == WS_EVT_PONG){
    Serial.printf("ws[%s][%u] pong[%u]: %s\n", server->url(), client->id(), len, (len)?(char*)data:"");
  } else if(type == WS_EVT_DATA){
    AwsFrameInfo * info = (AwsFrameInfo*)arg;
    String msg = "";
    if(info->final && info->index == 0 && info->len == len){
      //the whole message is in a single frame and we got all of it's data
      Serial.printf("ws[%s][%u] %s-message[%llu]: ", server->url(), client->id(), (info->opcode == WS_TEXT)?"text":"binary", info->len);

      if(info->opcode == WS_TEXT){
        for(size_t i=0; i < info->len; i++) {
          msg += (char) data[i];
        }
      } else {
        char buff[3];
        for(size_t i=0; i < info->len; i++) {
          sprintf(buff, "%02x ", (uint8_t) data[i]);
          msg += buff ;
        }
      }
      Serial.printf("%s\n",msg.c_str());

      if(info->opcode == WS_TEXT)
        client->text("I got your text message");
      else
        client->binary("I got your binary message");
    } else {
      //message is comprised of multiple frames or the frame is split into multiple packets
      if(info->index == 0){
        if(info->num == 0)
          Serial.printf("ws[%s][%u] %s-message start\n", server->url(), client->id(), (info->message_opcode == WS_TEXT)?"text":"binary");
        Serial.printf("ws[%s][%u] frame[%u] start[%llu]\n", server->url(), client->id(), info->num, info->len);
      }

      Serial.printf("ws[%s][%u] frame[%u] %s[%llu - %llu]: ", server->url(), client->id(), info->num, (info->message_opcode == WS_TEXT)?"text":"binary", info->index, info->index + len);

      if(info->opcode == WS_TEXT){
        for(size_t i=0; i < info->len; i++) {
          msg += (char) data[i];
        }
      } else {
        char buff[3];
        for(size_t i=0; i < info->len; i++) {
          sprintf(buff, "%02x ", (uint8_t) data[i]);
          msg += buff ;
        }
      }
      Serial.printf("%s\n",msg.c_str());

      if((info->index + len) == info->len){
        Serial.printf("ws[%s][%u] frame[%u] end[%llu]\n", server->url(), client->id(), info->num, info->len);
        if(info->final){
          Serial.printf("ws[%s][%u] %s-message end\n", server->url(), client->id(), (info->message_opcode == WS_TEXT)?"text":"binary");
          if(info->message_opcode == WS_TEXT)
            client->text("I got your text message");
          else
            client->binary("I got your binary message");
        }
      }
    }
  }
}

void setup() {
  Serial.begin(115200);
  tft.begin();
  tft.fillScreen(TFT_BLACK);

#if defined(ILI9341_DRIVER)
  tft.setFreeFont(&ArialRoundedMTBold_14);
#elif defined(ST7735_DRIVER)
  tft.setFreeFont(&TomThumb);
#endif

  tft.setTextDatum(BC_DATUM);
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.drawString("Original by: blog.squix.org", TFT_WIDTH / 2, TFT_HEIGHT / 4 * 3); // 240
  tft.drawString("Adapted by: Bodmer", TFT_WIDTH / 2, TFT_HEIGHT / 16 * 13 ); //260
  tft.setTextColor(TFT_ORANGE, TFT_BLACK);
  
  SPIFFS.begin();
  listFiles();
  //Uncomment next line if you want to erase SPIFFS and update all internet resources, this takes some time!
  //tft.drawString("Formatting SPIFFS, so wait!", TFT_WIDTH / 2, TFT_HEIGHT / 8 * 5); SPIFFS.format(); //200

  if (SPIFFS.exists("/WU.jpg") == true) ui.drawJpeg("/WU.jpg", 0, 10);
#if defined(ILI9341_DRIVER)
  if (SPIFFS.exists("/Earth.jpg") == true) ui.drawJpeg("/Earth.jpg", 0, TFT_HEIGHT - 56); // Image is 56 pixels high
#elif defined(ST7735_DRIVER)
  if (SPIFFS.exists("/Earth.jpg") == true) ui.drawJpeg("/Earth.jpg", 0, TFT_HEIGHT - 30); // Image is 56 pixels high
#endif
  delay(1000);
  tft.drawString("Connecting to WiFi", TFT_WIDTH / 2, TFT_HEIGHT / 8 * 5);
  tft.setTextPadding(TFT_WIDTH); // Pad next drawString() text to full width to over-write old text

  //WiFiManager
  //Local intialization. Once its business is done, there is no need to keep it around
  AsyncWiFiManager wifiManager(&server, &dns);
  // Uncomment for testing wifi manager
  //wifiManager.resetSettings();
  wifiManager.setRemoveDuplicateAPs(false);

  wifiManager.setAPCallback(configModeCallback);

  //or use this for auto generated name ESP + ChipID
  wifiManager.autoConnect();

  //Manual Wifi
  //WiFi.begin(WIFI_SSID, WIFI_PWD);

  // OTA Setup
  String hostname(HOSTNAME);
  hostname += String(ESP.getChipId(), HEX);
  WiFi.hostname(hostname);
  ArduinoOTA.setHostname((const char *)hostname.c_str());
  ArduinoOTA.begin();

  MDNS.addService("http","tcp",80);

  ws.onEvent(onWsEvent);
  server.addHandler(&ws);

  events.onConnect([](AsyncEventSourceClient *client){
    client->send("hello!",NULL,millis(),1000);
  });
  server.addHandler(&events);

  server.addHandler(new SPIFFSEditor(http_username,http_password));

  server.on("/heap", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(200, "text/plain", String(ESP.getFreeHeap()));
  });

  server.serveStatic("/", SPIFFS, "/").setDefaultFile("index.htm");

  server.onNotFound([](AsyncWebServerRequest *request){
    Serial.printf("NOT_FOUND: ");
    if(request->method() == HTTP_GET)
      Serial.printf("GET");
    else if(request->method() == HTTP_POST)
      Serial.printf("POST");
    else if(request->method() == HTTP_DELETE)
      Serial.printf("DELETE");
    else if(request->method() == HTTP_PUT)
      Serial.printf("PUT");
    else if(request->method() == HTTP_PATCH)
      Serial.printf("PATCH");
    else if(request->method() == HTTP_HEAD)
      Serial.printf("HEAD");
    else if(request->method() == HTTP_OPTIONS)
      Serial.printf("OPTIONS");
    else
      Serial.printf("UNKNOWN");
    Serial.printf(" http://%s%s\n", request->host().c_str(), request->url().c_str());

    if(request->contentLength()){
      Serial.printf("_CONTENT_TYPE: %s\n", request->contentType().c_str());
      Serial.printf("_CONTENT_LENGTH: %u\n", request->contentLength());
    }

    int headers = request->headers();
    int i;
    for(i=0;i<headers;i++){
      AsyncWebHeader* h = request->getHeader(i);
      Serial.printf("_HEADER[%s]: %s\n", h->name().c_str(), h->value().c_str());
    }

    int params = request->params();
    for(i=0;i<params;i++){
      AsyncWebParameter* p = request->getParam(i);
      if(p->isFile()){
        Serial.printf("_FILE[%s]: %s, size: %u\n", p->name().c_str(), p->value().c_str(), p->size());
      } else if(p->isPost()){
        Serial.printf("_POST[%s]: %s\n", p->name().c_str(), p->value().c_str());
      } else {
        Serial.printf("_GET[%s]: %s\n", p->name().c_str(), p->value().c_str());
      }
    }

    request->send(404);
  });
  server.onFileUpload([](AsyncWebServerRequest *request, const String& filename, size_t index, uint8_t *data, size_t len, bool final){
    if(!index)
      Serial.printf("UploadStart: %s\n", filename.c_str());
    Serial.printf("%s", (const char*)data);
    if(final)
      Serial.printf("UploadEnd: %s (%u)\n", filename.c_str(), index+len);
  });
  server.onRequestBody([](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total){
    if(!index)
      Serial.printf("BodyStart: %u\n", total);
    Serial.printf("%s", (const char*)data);
    if(index + len == total)
      Serial.printf("BodyEnd: %u\n", total);
  });
  server.begin();

  // download images from the net. If images already exist don't download
  tft.drawString("Downloading to SPIFFS...", TFT_WIDTH / 2, TFT_HEIGHT / 8 * 5);
  tft.drawString(" ", TFT_WIDTH / 2, TFT_HEIGHT / 4 * 3);  // Clear line
  tft.drawString(" ", TFT_WIDTH / 2, TFT_HEIGHT / 16 * 13);  // Clear line
  //downloadResources();
  listFiles();
  tft.drawString(" ", TFT_WIDTH / 2, TFT_HEIGHT / 8 * 5);  // Clear line above using set padding width
  tft.drawString("Fetching weather data...", TFT_WIDTH / 2, TFT_HEIGHT / 16 * 11);
  //delay(500);
  
  // Start up the library
  sensors.begin();

  // locate devices on the bus
  Serial.print("Locating devices...");
  Serial.print("Found ");
  Serial.print(sensors.getDeviceCount(), DEC);
  Serial.println(" devices.");

  // report parasite power requirements
  Serial.print("Parasite power is: ");
  if (sensors.isParasitePowerMode()) Serial.println("ON");
  else Serial.println("OFF");

  if (!sensors.getAddress(insideThermometer, 0)) Serial.println("Unable to find address for Device 0");
  if (!sensors.getAddress(outsideThermometer, 1)) Serial.println("Unable to find address for Device 1");

  // show the addresses we found on the bus
  Serial.print("Device 0 Address: ");
  printAddress(insideThermometer);


  Serial.print("Device 1 Address: ");
  printAddress(outsideThermometer);

  sensors.setResolution(insideThermometer,10);
  sensors.setResolution(outsideThermometer,10);

  // load the weather information
  updateData();

  ticker.attach(UPDATE_INTERVAL_SECS, setReadyForWeatherUpdate);
  ticker.attach(60, setReadyForOWUpdate);
}

long lastDrew = 0;
void loop() {
  // Handle OTA update requests
  ArduinoOTA.handle();

  // Check if we should update the clock
  //if (millis() - lastDrew > 30000 && wunderground.getSeconds() == "00") {
  if (millis() - lastDrew >= 1000) {
    drawTime();
    lastDrew = millis();
  }

  // Check if we should update weather information
  if (readyForWeatherUpdate /*&& ui.getUiState()->frameState == FIXED */) {
    updateData();
    //lastDownloadUpdate = millis();
  }

  if (readyForOWUpdate /* && ui.getUiState()->frameState == FIXED */) {
	updateOW();
	drawCurrentTemp();
  }

}

// Called if WiFi has not been configured yet
void configModeCallback (AsyncWiFiManager *myWiFiManager) {
  tft.setTextDatum(BC_DATUM);

#if defined(ILI9341_DRIVER)
  tft.setFreeFont(&ArialRoundedMTBold_14);
#elif defined(ST7735_DRIVER)
  tft.setFreeFont(&TomThumb);
#endif

  tft.setTextColor(TFT_ORANGE);
  tft.drawString("Wifi Manager", TFT_WIDTH / 2, 28);
  tft.drawString("Please connect to AP", TFT_WIDTH / 2, 42);
  tft.setTextColor(TFT_WHITE);
  tft.drawString(myWiFiManager->getConfigPortalSSID(), TFT_WIDTH / 2, 56);
  tft.setTextColor(TFT_ORANGE);
  tft.drawString("To setup Wifi Configuration", TFT_WIDTH / 2, 70);
}

// callback called during download of files. Updates progress bar
void downloadCallback(String filename, int16_t bytesDownloaded, int16_t bytesTotal) {
  Serial.println(String(bytesDownloaded) + " / " + String(bytesTotal));

  tft.setTextDatum(BC_DATUM);
  tft.setTextColor(TFT_ORANGE, TFT_BLACK);
  tft.setTextPadding(240);

  int percentage = 100 * bytesDownloaded / bytesTotal;
  if (percentage == 0) {
    tft.drawString(filename, TFT_WIDTH / 2, TFT_HEIGHT / 16 * 11);
  }
  if (percentage % 5 == 0) {
    tft.setTextDatum(TC_DATUM);
    tft.setTextPadding(tft.textWidth(" 888% "));
    tft.drawString(String(percentage) + "%", TFT_WIDTH / 2, TFT_HEIGHT / 64 * 49);
    ui.drawProgressBar(10, 225, 240 - 20, 15, percentage, TFT_WHITE, TFT_BLUE);
  }

}

// Download the bitmaps
void downloadResources() {
  // tft.fillScreen(TFT_BLACK);

#if defined(ILI9341_DRIVER)
  tft.setFreeFont(&ArialRoundedMTBold_14);
#elif defined(ST7735_DRIVER)
  tft.setFreeFont(&TomThumb);
#endif

  char id[5];

  // Download WU graphic jpeg first and display it, then the Earth view
  webResource.downloadFile((String)"http://i.imgur.com/njl1pMj.jpg", (String)"/WU.jpg", _downloadCallback);
  if (SPIFFS.exists("/WU.jpg") == true) ui.drawJpeg("/WU.jpg", 0, 10);

  webResource.downloadFile((String)"http://i.imgur.com/v4eTLCC.jpg", (String)"/Earth.jpg", _downloadCallback);
  if (SPIFFS.exists("/Earth.jpg") == true) ui.drawJpeg("/Earth.jpg", 0, 320-56);
  
  //webResource.downloadFile((String)"http://i.imgur.com/IY57GSv.jpg", (String)"/Horizon.jpg", _downloadCallback);
  //if (SPIFFS.exists("/Horizon.jpg") == true) ui.drawJpeg("/Horizon.jpg", 0, 320-160);

  //webResource.downloadFile((String)"http://i.imgur.com/jZptbtY.jpg", (String)"/Rainbow.jpg", _downloadCallback);
  //if (SPIFFS.exists("/Rainbow.jpg") == true) ui.drawJpeg("/Rainbow.jpg", 0, 0);

  for (int i = 0; i < 19; i++) {
    sprintf(id, "%02d", i);
    webResource.downloadFile("http://www.squix.org/blog/wunderground/" + wundergroundIcons[i] + ".bmp", "/" + wundergroundIcons[i] + ".bmp", _downloadCallback);
  }
  for (int i = 0; i < 19; i++) {
    sprintf(id, "%02d", i);
    webResource.downloadFile("http://www.squix.org/blog/wunderground/mini/" + wundergroundIcons[i] + ".bmp", "/mini/" + wundergroundIcons[i] + ".bmp", _downloadCallback);
  }
  for (int i = 0; i < 24; i++) {
    webResource.downloadFile("http://www.squix.org/blog/moonphase_L" + String(i) + ".bmp", "/moon" + String(i) + ".bmp", _downloadCallback);
  }
}

// Update the internet based information and update screen
void updateData() {
  // booted = true;  // Test only
  // booted = false; // Test only

  if (booted) ui.drawJpeg("/WU.jpg", 0, 10); // May have already drawn this but it does not take long
  else tft.drawCircle(22, 22, 16, TFT_DARKGREY); // Outer ring - optional

  if (booted) drawProgress(20, "Updating time...");
  else fillSegment(22, 22, 0, (int) (20 * 3.6), 16, TFT_NAVY);
  configTime(UTC_OFFSET * 3600, 0, NTP_SERVERS);

  timeClient.updateTime();
  if (booted) drawProgress(50, "Updating conditions...");
  else fillSegment(22, 22, 0, (int) (50 * 3.6), 16, TFT_NAVY);

  wunderground.updateConditions(WUNDERGRROUND_API_KEY, WUNDERGRROUND_LANGUAGE, WUNDERGROUND_COUNTRY, WUNDERGROUND_CITY);
  if (booted) drawProgress(70, "Updating forecasts...");
  else fillSegment(22, 22, 0, (int) (70 * 3.6), 16, TFT_NAVY);

  wunderground.updateForecast(WUNDERGRROUND_API_KEY, WUNDERGRROUND_LANGUAGE, WUNDERGROUND_COUNTRY, WUNDERGROUND_CITY);
  if (booted) drawProgress(90, "Updating astronomy...");
  else fillSegment(22, 22, 0, (int) (90 * 3.6), 16, TFT_NAVY);

  wunderground.updateAstronomy(WUNDERGRROUND_API_KEY, WUNDERGRROUND_LANGUAGE, WUNDERGROUND_COUNTRY, WUNDERGROUND_CITY);
  // lastUpdate = timeClient.getFormattedTime();
  readyForWeatherUpdate = false;
  if (booted) drawProgress(100, "Done...");
  else fillSegment(22, 22, 0, 360, 16, TFT_NAVY);

  if (booted) delay(2000);

  if (booted) tft.fillScreen(TFT_BLACK);
  else   fillSegment(22, 22, 0, 360, 22, TFT_BLACK);

  //tft.fillScreen(TFT_CYAN); // For text padding and update graphics over-write checking only
  drawTime();
  drawCurrentWeather();
  drawCurrentTemp();
  drawForecast();
  drawAstronomy();

  //if (booted) screenshotToConsole(); // No supporting function in this sketch, documentation support only!
  booted = false;
}

// Progress bar helper
void drawProgress(uint8_t percentage, String text) {
#if defined(ILI9341_DRIVER)
  tft.setFreeFont(&ArialRoundedMTBold_14);
#elif defined(ST7735_DRIVER)
  tft.setFreeFont(&TomThumb);
#endif

  tft.setTextDatum(BC_DATUM);
  tft.setTextColor(TFT_ORANGE, TFT_BLACK);
  tft.setTextPadding(TFT_WIDTH);
  tft.drawString(text, TFT_WIDTH / 2, TFT_HEIGHT / 16 * 11);

  ui.drawProgressBar(10, 225, 240 - 20, 15, percentage, TFT_WHITE, TFT_BLUE);

  tft.setTextPadding(0);
}

// draws the clock
void drawTime() {
  char *dstAbbrev;
  char time_str[11];
  time_t now = dstAdjusted.time(&dstAbbrev);
  struct tm * timeinfo = localtime (&now);

#if defined(ILI9341_DRIVER)
	  tft.setFreeFont(&ArialRoundedMTBold_14);
#elif defined(ST7735_DRIVER)
	  tft.setFreeFont(&DejaVu_Sans_Condensed_Bold_8);
#endif

  String date = wunderground.getDate();
  //String date = ctime(&now);
  //date = date.substring(0,11) + String(1900+timeinfo->tm_year);

  tft.setTextDatum(BC_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextPadding(tft.textWidth(" Ddd, 44 Mmm 4444 "));  // String width + margin

#if defined(ILI9341_DRIVER)
  tft.drawString(date, 120, 14);

  tft.setFreeFont(&ArialRoundedMTBold_36);

#elif defined(ST7735_DRIVER)
  tft.drawString(date, 64, 9);

  tft.setFreeFont(&DejaVu_Sans_Condensed_Bold_10);
#endif

  //String timeNow = timeClient.getHours() + ":" + timeClient.getMinutes();

  tft.setTextDatum(BC_DATUM);
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);

#ifdef STYLE_24HR
  tft.setTextPadding(tft.textWidth(" 44:44:44 "));  // String width + margin
  sprintf(time_str, "%02d:%02d:%02d",timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
#else
  tft.setTextPadding(tft.textWidth(" 44:44:44 "));  // String width + margin
  int hour = (timeinfo->tm_hour+11)%12+1;  // take care of noon and midnight
  sprintf(time_str, "%2d:%02d:%02d",hour, timeinfo->tm_min, timeinfo->tm_sec);
#endif
  String timeNow = String(time_str);


#if defined(ILI9341_DRIVER)
  tft.drawString(timeNow, 120, 50);

  drawSeparator(52);

#elif defined(ST7735_DRIVER)
  tft.drawString(timeNow, 64, 24);

  drawSeparator(25);
#endif

  tft.setTextPadding(0);
}

void drawCurrentTemp() {
	  tft.setTextDatum(TC_DATUM);
	  tft.setTextColor(TFT_ORANGE, TFT_BLACK);

	#if defined(ILI9341_DRIVER)
		  tft.setFreeFont(&ArialRoundedMTBold_ArialRoundedMTBold_36);
		  tft.drawString(curr_inhouse, 120, 59);
	#elif defined(ST7735_DRIVER)
		  tft.setFreeFont(&DejaVu_Sans_Condensed_Bold_8);
		  tft.drawString(curr_inhouse, 64, 26);
	#endif
}

// draws current weather information
void drawCurrentWeather() {
  // Weather Icon
  String weatherIcon = getMeteoconIcon(wunderground.getTodayIcon());
  //uint32_t dt = millis();
#if defined(ILI9341_DRIVER)
  ui.drawBmp("/" + weatherIcon + ".bmp", 0, 59);
#elif defined(ST7735_DRIVER)
  ui.drawBmp("/" + weatherIcon + ".bmp", 0, 26);
#endif
  //Serial.print("Icon draw time = "); Serial.println(millis()-dt);

  // Weather Text

  String weatherText = wunderground.getWeatherText();
  //weatherText = "Heavy Thunderstorms with Small Hail"; // Test line splitting with longest(?) string

#if defined(ILI9341_DRIVER)
	  tft.setFreeFont(&ArialRoundedMTBold_14);
#elif defined(ST7735_DRIVER)
	  tft.setFreeFont(&TomThumb);
#endif

  tft.setTextDatum(BR_DATUM);
  tft.setTextColor(TFT_ORANGE, TFT_BLACK);

  int splitPoint = 0;
  int xpos = TFT_WIDTH - 10;
  splitPoint =  splitIndex(weatherText);
  if (splitPoint > 16) xpos = TFT_WIDTH - 5;

  tft.setTextPadding(tft.textWidth("Heavy Thunderstorms"));  // Max anticipated string width
#if defined(ILI9341_DRIVER)
  if (splitPoint) tft.drawString(weatherText.substring(0, splitPoint), xpos, 72);
#elif defined(ST7735_DRIVER)
  if (splitPoint) tft.drawString(weatherText.substring(0, splitPoint), xpos, 33);
#endif

  tft.setTextPadding(tft.textWidth(" with Small Hail"));  // Max anticipated string width + margin
#if defined(ILI9341_DRIVER)
  tft.drawString(weatherText.substring(splitPoint), xpos, 87);
#elif defined(ST7735_DRIVER)
  tft.drawString(weatherText.substring(splitPoint), xpos, 46);
#endif

#if defined(ILI9341_DRIVER)
  tft.setFreeFont(&ArialRoundedMTBold_36);
#elif defined(ST7735_DRIVER)
  tft.setFreeFont(&DejaVu_Sans_Condensed_Bold_10);
#endif

  tft.setTextDatum(TR_DATUM);
  tft.setTextColor(TFT_ORANGE, TFT_BLACK);

  // Font ASCII code 96 (0x60) modified to make "`" a degree symbol
  tft.setTextPadding(tft.textWidth("-88`")); // Max width of vales

  weatherText = wunderground.getCurrentTemp();
  if (weatherText.indexOf(".")) weatherText = weatherText.substring(0, weatherText.indexOf(".")); // Make it integer temperature
  if (weatherText == "") weatherText = "?";  // Handle null return
#if defined(ILI9341_DRIVER)
  tft.drawString(weatherText + "`", 221, 100);

  tft.setFreeFont(&ArialRoundedMTBold_14);

#elif defined(ST7735_DRIVER)
  tft.drawString(weatherText + "`", 118, 53);

  tft.setFreeFont(&TomThumb);
#endif

  tft.setTextDatum(TL_DATUM);
  tft.setTextPadding(0);
  if (IS_METRIC) tft.drawString("C ", 221, 100);
  else  tft.drawString("F ", 221, 100);

  weatherText = wunderground.getWindDir() + " ";
  weatherText += String((int)(wunderground.getWindSpeed().toInt() * WIND_SPEED_SCALING)) + WIND_SPEED_UNITS;

  tft.setTextPadding(tft.textWidth("Variable 888 mph ")); // Max string length?

#if defined(ILI9341_DRIVER)
	  tft.drawString(weatherText, 114, 136);
#elif defined(ST7735_DRIVER)
  tft.drawString(weatherText, 60, 68);
#endif

  weatherText = wunderground.getWindDir();

  int windAngle = 0;
  String compassCardinal = "";
  switch (weatherText.length()) {
    case 1:
      compassCardinal = "N E S W "; // Not used, see default below
      windAngle = 90 * compassCardinal.indexOf(weatherText) / 2;
      break;
    case 2:
      compassCardinal = "NE SE SW NW";
      windAngle = 45 + 90 * compassCardinal.indexOf(weatherText) / 3;
      break;
    case 3:
      compassCardinal = "NNE ENE ESE SSE SSW WSW WNW NNW";
      windAngle = 22 + 45 * compassCardinal.indexOf(weatherText) / 4; // 22 should be 22.5 but accuracy is not needed!
      break;
    default:
      if (weatherText == "Variable") windAngle = -1;
      else {
        //                 v23456v23456v23456v23456 character ruler
        compassCardinal = "North East  South West"; // Possible strings
        windAngle = 90 * compassCardinal.indexOf(weatherText) / 6;
      }
      break;
  }
#if defined(ILI9341_DRIVER)
  tft.fillCircle(128, 110, 23, TFT_BLACK); // Erase old plot, radius + 1 to delete stray pixels
  tft.drawCircle(128, 110, 22, TFT_DARKGREY);    // Outer ring - optional
  if ( windAngle >= 0 ) fillSegment(128, 110, windAngle - 15, 30, 22, TFT_GREEN); // Might replace this with a bigger rotating arrow
  tft.drawCircle(128, 110, 6, TFT_RED);

  drawSeparator(153);
#elif defined(ST7735_DRIVER)
  tft.fillCircle(65, 50, 11, TFT_BLACK); // Erase old plot, radius + 1 to delete stray pixels
  tft.drawCircle(65, 50, 10, TFT_DARKGREY);    // Outer ring - optional
  if ( windAngle >= 0 ) fillSegment(65, 50, windAngle - 7, 15, 11, TFT_GREEN); // Might replace this with a bigger rotating arrow
  tft.drawCircle(65, 50, 6, TFT_RED);

  drawSeparator(74);
#endif

  tft.setTextPadding(0); // Reset padding width to none
}

// draws the three forecast columns
void drawForecast() {

#if defined(ILI9341_DRIVER)
  drawForecastDetail(10, 171, 0);
  drawForecastDetail(95, 171, 2);
  drawForecastDetail(180, 171, 4);
  drawSeparator(171 + 69);
#elif defined(ST7735_DRIVER)
  drawForecastDetail(10, 84, 0);
  drawForecastDetail(46, 84, 2);
  drawForecastDetail(88, 84, 4);
  drawSeparator(84 + 34);
#endif
}

// helper for the forecast columns
void drawForecastDetail(uint16_t x, uint16_t y, uint8_t dayIndex) {
#if defined(ILI9341_DRIVER)
  tft.setFreeFont(&ArialRoundedMTBold_14);
#elif defined(ST7735_DRIVER)
  tft.setFreeFont(&TomThumb);
#endif

  String day = wunderground.getForecastTitle(dayIndex).substring(0, 3);
  day.toUpperCase();

  tft.setTextDatum(BC_DATUM);

  tft.setTextColor(TFT_ORANGE, TFT_BLACK);
  tft.setTextPadding(tft.textWidth("WWW"));
#if defined(ILI9341_DRIVER)
  tft.drawString(day, x + 25, y);
#elif defined(ST7735_DRIVER)
  tft.drawString(day, x + 12, y);
#endif

  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextPadding(tft.textWidth("-88   -88"));
#if defined(ILI9341_DRIVER)
  tft.drawString(wunderground.getForecastHighTemp(dayIndex) + "   " + wunderground.getForecastLowTemp(dayIndex), x + 25, y + 14);
#elif defined(ST7735_DRIVER)
  tft.drawString(wunderground.getForecastHighTemp(dayIndex) + "   " + wunderground.getForecastLowTemp(dayIndex), x + 12, y + 8);
#endif

  String weatherIcon = getMeteoconIcon(wunderground.getForecastIcon(dayIndex));
#if defined(ILI9341_DRIVER)
  ui.drawBmp("/mini/" + weatherIcon + ".bmp", x, y + 15);
#elif defined(ST7735_DRIVER)
  ui.drawBmp("/mini/" + weatherIcon + ".bmp", x, y + 8);
#endif

  tft.setTextPadding(0); // Reset padding width to none
}

// draw moonphase and sunrise/set and moonrise/set
void drawAstronomy() {
#if defined(ILI9341_DRIVER)
  tft.setFreeFont(&ArialRoundedMTBold_14);
#elif defined(ST7735_DRIVER)
  tft.setFreeFont(&TomThumb);
#endif

  tft.setTextDatum(BC_DATUM);
  tft.setTextColor(TFT_ORANGE, TFT_BLACK);
  tft.setTextPadding(tft.textWidth(" Waxing Crescent "));
  tft.drawString(wunderground.getMoonPhase(), TFT_WIDTH / 2, TFT_HEIGHT * 0.8125 - 2);

  int moonAgeImage = 24 * wunderground.getMoonAge().toInt() / 30.0;
  ui.drawBmp("/moon" + String(moonAgeImage) + ".bmp", (TFT_WIDTH - 30 )/ 2 , TFT_HEIGHT * 0.8125);

  tft.setTextDatum(BC_DATUM);
  tft.setTextColor(TFT_ORANGE, TFT_BLACK);
  tft.setTextPadding(0); // Reset padding width to none
  tft.drawString("Sun", TFT_WIDTH * 4 / 25 , TFT_HEIGHT * 0.875);

  tft.setTextDatum(BR_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextPadding(tft.textWidth(" 88:88 "));
  int dt = rightOffset(wunderground.getSunriseTime(), ":"); // Draw relative to colon to them aligned
  tft.drawString(wunderground.getSunriseTime(), TFT_WIDTH * 4 / 25 + dt, TFT_HEIGHT * 0.88);

  dt = rightOffset(wunderground.getSunsetTime(), ":");
  tft.drawString(wunderground.getSunsetTime(), TFT_WIDTH * 4 / 25 + dt, TFT_HEIGHT * 0.92);

  tft.setTextDatum(BC_DATUM);
  tft.setTextColor(TFT_ORANGE, TFT_BLACK);
  tft.setTextPadding(0); // Reset padding width to none
  tft.drawString("Moon", TFT_WIDTH * 0.83 , TFT_HEIGHT * 0.875);

  tft.setTextDatum(BR_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextPadding(tft.textWidth(" 88:88 "));
  dt = rightOffset(wunderground.getMoonriseTime(), ":"); // Draw relative to colon to them aligned
  tft.drawString(wunderground.getMoonriseTime(), TFT_WIDTH * 0.83  + dt, TFT_HEIGHT * 0.88 );

  dt = rightOffset(wunderground.getMoonsetTime(), ":");
  tft.drawString(wunderground.getMoonsetTime(), TFT_WIDTH * 0.83 + dt, TFT_HEIGHT * 0.92);

  tft.setTextPadding(0); // Reset padding width to none
}

// Helper function, should be part of the weather station library and should disappear soon
String getMeteoconIcon(String iconText) {
  if (iconText == "F") return "chanceflurries";
  if (iconText == "Q") return "chancerain";
  if (iconText == "W") return "chancesleet";
  if (iconText == "V") return "chancesnow";
  if (iconText == "S") return "chancetstorms";
  if (iconText == "B") return "clear";
  if (iconText == "Y") return "cloudy";
  if (iconText == "F") return "flurries";
  if (iconText == "M") return "fog";
  if (iconText == "E") return "hazy";
  if (iconText == "Y") return "mostlycloudy";
  if (iconText == "H") return "mostlysunny";
  if (iconText == "H") return "partlycloudy";
  if (iconText == "J") return "partlysunny";
  if (iconText == "W") return "sleet";
  if (iconText == "R") return "rain";
  if (iconText == "W") return "snow";
  if (iconText == "B") return "sunny";
  if (iconText == "0") return "tstorms";


  return "unknown";
}

// if you want separators, uncomment the tft-line
void drawSeparator(uint16_t y) {
  tft.drawFastHLine(10, y, TFT_WIDTH - 2 * 10, 0x4228);
}

// determine the "space" split point in a long string
int splitIndex(String text)
{
  int index = 0;
  while ( (text.indexOf(' ', index) >= 0) && ( index <= text.length() / 2 ) ) {
    index = text.indexOf(' ', index) + 1;
  }
  if (index) index--;
  return index;
}

// Calculate coord delta from start of text String to start of sub String contained within that text
// Can be used to vertically right align text so for example a colon ":" in the time value is always
// plotted at same point on the screen irrespective of different proportional character widths,
// could also be used to align decimal points for neat formatting
int rightOffset(String text, String sub)
{
  int index = text.indexOf(sub);
  return tft.textWidth(text.substring(index));
}

// Calculate coord delta from start of text String to start of sub String contained within that text
// Can be used to vertically left align text so for example a colon ":" in the time value is always
// plotted at same point on the screen irrespective of different proportional character widths,
// could also be used to align decimal points for neat formatting
int leftOffset(String text, String sub)
{
  int index = text.indexOf(sub);
  return tft.textWidth(text.substring(0, index));
}

// Draw a segment of a circle, centred on x,y with defined start_angle and subtended sub_angle
// Angles are defined in a clockwise direction with 0 at top
// Segment has radius r and it is plotted in defined colour
// Can be used for pie charts etc, in this sketch it is used for wind direction
#define DEG2RAD 0.0174532925 // Degrees to Radians conversion factor
#define INC 2 // Minimum segment subtended angle and plotting angle increment (in degrees)
void fillSegment(int x, int y, int start_angle, int sub_angle, int r, unsigned int colour)
{
  // Calculate first pair of coordinates for segment start
  float sx = cos((start_angle - 90) * DEG2RAD);
  float sy = sin((start_angle - 90) * DEG2RAD);
  uint16_t x1 = sx * r + x;
  uint16_t y1 = sy * r + y;

  // Draw colour blocks every INC degrees
  for (int i = start_angle; i < start_angle + sub_angle; i += INC) {

    // Calculate pair of coordinates for segment end
    int x2 = cos((i + 1 - 90) * DEG2RAD) * r + x;
    int y2 = sin((i + 1 - 90) * DEG2RAD) * r + y;

    tft.fillTriangle(x1, y1, x2, y2, x, y, colour);

    // Copy segment end to sgement start for next segment
    x1 = x2;
    y1 = y2;
  }
}

// Called every 1 minute
void updateOW() {
  sensors.requestTemperatures();
  Serial.print("Inhouse: ");
  curr_inhouse = sensors.getTempC(insideThermometer);
  Serial.println(curr_inhouse);
  readyForOWUpdate = false;
}

void setReadyForWeatherUpdate() {
  Serial.println("Setting readyForUpdate to true");
  readyForWeatherUpdate = true;
}

void setReadyForOWUpdate() {
  Serial.println("Setting readyForOWUpdate to true");
  readyForOWUpdate = true;
}

// function to print a device address
void printAddress(DeviceAddress deviceAddress)
{
  for (uint8_t i = 0; i < 8; i++)
  {
    // zero pad the address if necessary
    if (deviceAddress[i] < 16) { Serial.print("0"); tft.print("0");}
    Serial.print(deviceAddress[i], HEX);
  }
  Serial.println();
}
