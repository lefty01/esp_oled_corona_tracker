/**
 * The MIT License (MIT)
 *
 * Copyright (c) 2020 by Andreas Loeffler
 *
 */

// data sources:
// https://coronavirus-19-api.herokuapp.com/countries/GERMANY
// fingerprint: 2F:0E:48:24:F8:BA:05:3E:42:40:77:76:55:61:50:F0:2A:DA:58:D2:05:FB:16:90:B8:1D:A6:6D:DD:76:C1:E4
//


#define DEBUG_ESP_SSL 1
#define DEBUG 1

#include "debug_print.h"
#include "wifi_mqtt_creds.h"

#ifdef ESP32
#include <WiFi.h>
#else
#include <ESP8266WiFi.h>
#endif

#include <PubSubClient.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <NTPClient.h>
#include <WiFiUdp.h>

#include <ArduinoJson.h>

#include "server_mqtt.crt.h"
#include "client.crt.h"
#include "client.key.h"
#include "herokuapp_com_pem.h"

#include "SSD1306Wire.h"
#include "OLEDDisplayUi.h"
#include "mqtt_img.h"

#define EVERY_SECOND 1000
#define EVERY_MINUTE 60 * 1000
#define EVERY_HOUR   60 * 60 * 1000
#define EVERY_5_MINUTE 5 * 60 * 1000

#define GER_TZ_OFFSET 2*60*60
#define COVID19_DATA_URL "https://coronavirus-19-api.herokuapp.com/countries/GERMANY"

unsigned long time_1 = 0;
time_t now;
bool readyForNewData = false;

String ipAddr;
String dnsAddr;
String rssi;

const unsigned max_wifi_wait_seconds = 60;
const char* mqtt_device_id = "wemosEsp32_oled";
const int maxMqttRetry = 5;
bool mqttConnected = false;

// global object pointing to received json data
struct covid19Data_t {
  String cases;
  String time;
  String todayCases;
  String deaths;
  String todayDeaths;
  String recovered;
  String active;
  String critical;
  String casesPerOneMillion;
  String deathsPerOneMillion;
  bool valid;
} covid19Data;


// Initialize the OLED display using Wire library
// ESP8266 | ESP32 | pin
// --------+-------+----
//  D3     | 5     | SDA
//  D5     | 4     | SCL
#ifdef ESP32
WiFiClientSecure net;
SSD1306Wire display(0x3c, 5, 4);
#else
BearSSL::X509List   server_cert(server_crt_str);
BearSSL::X509List   client_crt(client_crt_str);
BearSSL::PrivateKey client_key(client_key_str);
BearSSL::WiFiClientSecure net;
SSD1306Wire display(0x3c, D3, D5);
#endif

PubSubClient mqttClient(net);

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "europe.pool.ntp.org", GER_TZ_OFFSET, 600000); // 2h offset, 10min update intervall

const int screenW = 128;
const int screenH = 64;

OLEDDisplayUi ui(&display);


// ******  PROGRAM VERSION ******
const char* VERSION = "0.4";


void drawProgress(OLEDDisplay *display, int percentage, String label) {
  display->clear();
  display->setTextAlignment(TEXT_ALIGN_CENTER);
  display->setFont(ArialMT_Plain_10);
  display->drawString(64, 10, label);
  display->drawProgressBar(2, 28, 124, 10, percentage);
  display->display();
}

void rssiOverlay(OLEDDisplay *display, OLEDDisplayUiState* state) {
  display->setTextAlignment(TEXT_ALIGN_RIGHT);
  display->setFont(ArialMT_Plain_10);
  display->drawString(128, 0, String(WiFi.RSSI()));
}

void drawLogo(OLEDDisplay *display) { // mqtt logo
  display->clear();
  display->drawXbm(40, 2, mqtt_width, mqtt_height, mqtt_bits);
  display->display();
}

void drawInfo(OLEDDisplay *display) {
  display->setFont(ArialMT_Plain_10);
  display->setTextAlignment(TEXT_ALIGN_LEFT);
  display->drawString(1, 50, "v" + String(VERSION) + " ip: " + ipAddr);
  display->display();
}

void drawText(OLEDDisplay *display, const char *text) {
  // Align text vertical/horizontal center
  display->clear();
  //display->setTextAlignment(TEXT_ALIGN_LEFT);
  display->setTextAlignment(TEXT_ALIGN_CENTER);
  display->setFont(ArialMT_Plain_10);
  //display->drawString(display->getWidth()/2, display->getHeight()/2, text);
  //display->drawString(1, display->getHeight()/2, text);
  display->drawStringMaxWidth(display->getWidth()/2, 0, 128, text);
  display->display();
}


// Frames ...
void startFrame(OLEDDisplay *display, OLEDDisplayUiState* state, int16_t x, int16_t y)
{
  display->setFont(ArialMT_Plain_24);
  display->setTextAlignment(TEXT_ALIGN_CENTER);

  display->drawString(64 + x,  5 + y, "COVID-19");
  display->setFont(ArialMT_Plain_16);
  display->drawString(64 + x, 30 + y, "Tracker");

  //display->drawXbm(x + 40,   y +  2, mqtt_width, mqtt_height, mqtt_bits);
  //display->drawString(x + 0, y + 50, "v" + String(VERSION) + ", ip: " + ipAddr);
}

void Frame1(OLEDDisplay *display, OLEDDisplayUiState* state, int16_t x, int16_t y)
{
  if (!covid19Data.valid)
    return;
  display->setTextAlignment(TEXT_ALIGN_CENTER);
  display->setFont(ArialMT_Plain_16);
  display->drawString(64 + x,  5 + y, "CASES:");
  display->drawString(64 + x, 30 + y, covid19Data.cases);
}
void Frame2(OLEDDisplay *display, OLEDDisplayUiState* state, int16_t x, int16_t y)
{
  if (!covid19Data.valid)
    return;
  display->setTextAlignment(TEXT_ALIGN_CENTER);
  display->setFont(ArialMT_Plain_16);
  display->drawString(64 + x,  5 + y, "TODAY CASES:");
  display->drawString(64 + x, 30 + y, covid19Data.todayCases);
}
void Frame3(OLEDDisplay *display, OLEDDisplayUiState* state, int16_t x, int16_t y)
{
  if (!covid19Data.valid)
    return;
  display->setTextAlignment(TEXT_ALIGN_CENTER);
  display->setFont(ArialMT_Plain_16);
  display->drawString(64 + x,  5 + y, "DEATHS:");
  display->drawString(64 + x, 30 + y, covid19Data.deaths);
}
void Frame4(OLEDDisplay *display, OLEDDisplayUiState* state, int16_t x, int16_t y)
{
  if (!covid19Data.valid)
    return;
  display->setTextAlignment(TEXT_ALIGN_CENTER);
  display->setFont(ArialMT_Plain_16);
  display->drawString(64 + x,  5 + y, "TODAY DEATHS:");
  display->drawString(64 + x, 30 + y, covid19Data.todayDeaths);
}
void Frame5(OLEDDisplay *display, OLEDDisplayUiState* state, int16_t x, int16_t y)
{
  if (!covid19Data.valid)
    return;
  display->setTextAlignment(TEXT_ALIGN_CENTER);
  display->setFont(ArialMT_Plain_16);
  display->drawString(64 + x,  5 + y, "ACTIVE:");
  display->drawString(64 + x, 30 + y, covid19Data.active);
}
void Frame6(OLEDDisplay *display, OLEDDisplayUiState* state, int16_t x, int16_t y)
{
  if (!covid19Data.valid)
    return;
  display->setTextAlignment(TEXT_ALIGN_CENTER);
  display->setFont(ArialMT_Plain_16);
  display->drawString(64 + x,  5 + y, "CRITICAL:");
  display->drawString(64 + x, 30 + y, covid19Data.critical);
}

void drawFooDetails(OLEDDisplay *display, int x, int y, int dayIndex) {

}

void drawFoo(OLEDDisplay *display, OLEDDisplayUiState* state, int16_t x, int16_t y) {
  drawFooDetails(display, x, y, 0);
  drawFooDetails(display, x + 44, y, 1);
  drawFooDetails(display, x + 88, y, 2);
}

FrameCallback frames[] = { startFrame,
			   Frame1,
			   Frame2,
			   Frame3,
			   Frame4,
			   Frame5,
			   Frame6,
};
OverlayCallback overlays[] = { rssiOverlay };

int numberOfFrames   = 7;
int numberOfOverlays = 1;


int setupWifi() {
  DEBUG_PRINTLN();
  DEBUG_PRINTLN("Connecting to wifi");

  WiFi.begin(wifi_ssid, wifi_pass);

  unsigned retry_counter = 0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    DEBUG_PRINT(".");

    display.clear();
    display.drawString(64, 10, "Connecting to WiFi");
    display.drawXbm(46, 30, 8, 8, retry_counter % 3 == 0 ? activeSymbole : inactiveSymbole);
    display.drawXbm(60, 30, 8, 8, retry_counter % 3 == 1 ? activeSymbole : inactiveSymbole);
    display.drawXbm(74, 30, 8, 8, retry_counter % 3 == 2 ? activeSymbole : inactiveSymbole);
    display.display();

    retry_counter++;
    if (retry_counter > max_wifi_wait_seconds) {
      DEBUG_PRINTLN(" TIMEOUT!");
      display.clear();
      display.drawString(64, 10, "Wifi TIMEOUT");
      display.display();
      return 1;
    }
  }
  ipAddr  = WiFi.localIP().toString();
  dnsAddr = WiFi.dnsIP().toString();

  DEBUG_PRINTLN("");
  DEBUG_PRINTLN("WiFi connected");
  DEBUG_PRINTLN("IP address: ");
  DEBUG_PRINTLN(ipAddr);
  DEBUG_PRINTLN("DNS address: ");
  DEBUG_PRINTLN(dnsAddr);

  display.clear();
  display.drawString(64, 10, "Wifi CONNECTED");
  display.display();

  return 0;
}

void mqttConnect()
{
  DEBUG_PRINT("Attempting MQTT connection...");
  String connect_msg = "CONNECTED ";
  connect_msg += VERSION;

  // Attempt to connect
  if (mqttClient.connect(mqtt_device_id, mqtt_user, mqtt_pass, "/esp32oled/state", 1, 1, "OFFLINE")) {
    DEBUG_PRINTLN("connected");
    // Once connected, publish an announcement...
    mqttClient.publish("/esp32oled/state", connect_msg.c_str(), true);
  }
  else {
    DEBUG_PRINT("failed, mqttClient.state = ");
    DEBUG_PRINTLN(mqttClient.state());
    DEBUG_PRINTLN(state2str(mqttClient.state()));
  }
}


void mqttCallback(char* topic, byte* payload, unsigned int length)
{
  DEBUG_PRINT("Message arrived [");
  DEBUG_PRINT(topic);
  DEBUG_PRINT("] ");

  char value[length+1];
  memcpy(value, payload, length);
  value[length] = '\0';

  DEBUG_PRINTLN(value);

  if (0 == strcmp("/esp32oled/set", topic)) {
    DEBUG_PRINTLN("wemos esp32oled set");
    if (0 == memcmp("logo", payload, 4)) {
      DEBUG_PRINTLN("wemos set logo");
      //drawLogo(&display);
      //drawInfo(&display);
    }
    else {
      //drawText(&display, value);
    }
  }
}


/* get info about covid-19 infections/death
   {"country":"Germany",
   "cases":63079,
   "todayCases":644,
   "deaths":545,
   "todayDeaths":4,
   "recovered":9211,
   "active":53323,
   "critical":1979,
   "casesPerOneMillion":753,
   "deathsPerOneMillion":7,
   "firstCase":"\nJan 26 "}
 */
int isValidNumber(int num)
{
  // if (num.length() > 10) {
  //   return 0;
  // }
  // for (char& c : num) {
  //   if (!isDigit(c)) {
  //     return 0;
  //   }
  // }
  if ((num >= 0) && (10000000000))
    return 1;
  return 0;
}

int isValidData(JsonObject data)
{
  if (isValidNumber(data["cases"])
      && isValidNumber(data["todayCases"])
      && isValidNumber(data["deaths"])
      && isValidNumber(data["todayDeaths"])
      && isValidNumber(data["recovered"])
      && isValidNumber(data["active"])
      && isValidNumber(data["critical"])
      && isValidNumber(data["casesPerOneMillion"])
      && isValidNumber(data["deathsPerOneMillion"])) {
    return 1;
  }
  return 0;
}

int getCovid19Data(OLEDDisplay *display)
{
  HTTPClient http;
  int rc = 0;
  //JsonObject root = {};

  http.begin(COVID19_DATA_URL, herokuapp_com_pem_str);

  int httpCode = http.GET();

  // drawProgress();


  if (httpCode > 0) { //Check for the returning code
    String payload = http.getString();
    //DEBUG_PRINTLN("httpcode and payload:");
    //DEBUG_PRINTLN(httpCode);
    DEBUG_PRINTLN(payload);

    // https://arduinojson.org/v6/assistant/
    const size_t capacity = JSON_OBJECT_SIZE(11) + 160;
    DynamicJsonDocument jsonBuffer(capacity);

    auto error = deserializeJson(jsonBuffer, payload);
    if (error) {
      DEBUG_PRINTLN(F("Error with response: deserializeJson() failed:"));
      DEBUG_PRINTLN(error.c_str());
      covid19Data.valid = false;
      rc = 1;
    }
    else {
      if (isValidData(jsonBuffer.as<JsonObject>())) {
	//covid19Data = jsonBuffer.as<JsonObject>();
	//covid19Data = jsonBuffer;
	covid19Data.valid = true;
	covid19Data.cases		= jsonBuffer["cases"].as<String>();
	covid19Data.todayCases		= jsonBuffer["todayCases"].as<String>();
	covid19Data.deaths		= jsonBuffer["deaths"].as<String>();
	covid19Data.todayDeaths		= jsonBuffer["todayDeaths"].as<String>();
	covid19Data.recovered		= jsonBuffer["recovered"].as<String>();
	covid19Data.active		= jsonBuffer["active"].as<String>();
	covid19Data.critical		= jsonBuffer["critical"].as<String>();
	covid19Data.casesPerOneMillion	= jsonBuffer["casesPerOneMillion"].as<String>();
	covid19Data.deathsPerOneMillion = jsonBuffer["deathsPerOneMillion"].as<String>();

	mqttClient.publish("/esp32oled/covid19/cases",		     covid19Data.cases.c_str());
	mqttClient.publish("/esp32oled/covid19/todayCases",	     covid19Data.todayCases.c_str());
	mqttClient.publish("/esp32oled/covid19/deaths",		     covid19Data.deaths.c_str());
	mqttClient.publish("/esp32oled/covid19/todayDeaths",	     covid19Data.todayDeaths.c_str());
	mqttClient.publish("/esp32oled/covid19/recovered",	     covid19Data.recovered.c_str());
	mqttClient.publish("/esp32oled/covid19/active",		     covid19Data.active.c_str());
	mqttClient.publish("/esp32oled/covid19/critical",	     covid19Data.critical.c_str());
	mqttClient.publish("/esp32oled/covid19/casesPerOneMillion",  covid19Data.casesPerOneMillion.c_str());
	mqttClient.publish("/esp32oled/covid19/deathsPerOneMillion", covid19Data.deathsPerOneMillion.c_str());
	mqttClient.publish("/esp32oled/covid19/datetime",	     timeClient.getFormattedTime().c_str());
      }
    }
  }
  else {
    DEBUG_PRINTLN("Error on HTTP request");
    covid19Data.valid = false;
    rc = 1;
  }

  http.end(); // Free the resources
  return rc;
}


void setup() {
  int wifiSetupTimeout = 1;

  delay(1000);
  DEBUG_BEGIN(115200);
  DEBUG_PRINTLN("");
  DEBUG_PRINTLN("setup begin...");

#ifdef ESP32
  DEBUG_PRINTLN(ESP.getChipRevision());
#else
  DEBUG_PRINTLN(ESP.getChipId());
#endif

  // oled setup
  display.init();
  display.clear();
  display.display();

  display.flipScreenVertically();
  display.setFont(ArialMT_Plain_10);
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  display.setContrast(250);


  DEBUG_PRINTLN("starting setupWifi()");
  while (wifiSetupTimeout) {
    wifiSetupTimeout = setupWifi();
  }
  DEBUG_PRINTLN("setupWifi() done");

  // Use WiFiClientSecure class to create TLS connection ****
  DEBUG_PRINTLN("setTrustAnchors()");
#ifdef ESP32
  net.setCACert(server_crt_str);
  net.setCertificate(client_crt_str);
  net.setPrivateKey(client_key_str);

#else
  net.setTrustAnchors(&server_cert);
  //  DEBUG_PRINTLN("allowSelfSignedCerts()");
  //  net.allowSelfSignedCerts();
  DEBUG_PRINTLN("setClientRSACert() client cert and key");
  net.setClientRSACert(&client_crt, &client_key);
#endif

  DEBUG_PRINTLN("set mqtt host/port and callback");
  mqttClient.setServer(mqtt_host, mqtt_port);
  //mqttClient.setCallback(mqttCallback);

  DEBUG_PRINTLN("calling mqttConnect()");
  mqttConnect();


  timeClient.begin();
  //configTime(TZ_SEC, DST_SEC, "pool.ntp.org");
  //delay(1000);


  // display gui setup
  ui.setTargetFPS(30);
  ui.setActiveSymbol(activeSymbole);
  ui.setInactiveSymbol(inactiveSymbole);
  // You can change this to
  // TOP, LEFT, BOTTOM, RIGHT
  ui.setIndicatorPosition(BOTTOM);
  // Defines where the first frame is located in the bar.
  ui.setIndicatorDirection(LEFT_RIGHT);
  // You can change the transition that is used
  // SLIDE_LEFT, SLIDE_RIGHT, SLIDE_TOP, SLIDE_DOWN
  ui.setFrameAnimation(SLIDE_LEFT);
  ui.setFrames(frames, numberOfFrames);
  ui.setOverlays(overlays, numberOfOverlays);
  // this init takes care of initalising the display too
  ui.init();

  display.flipScreenVertically(); // is reset by init?!
  drawLogo(&display);
  drawInfo(&display);

  timeClient.forceUpdate();
  DEBUG_PRINTLN(timeClient.getFormattedTime());
  mqttClient.publish("/esp32oled/datetime", timeClient.getFormattedTime().c_str());

  if (getCovid19Data(&display)) {
    DEBUG_PRINTLN("Error getting data");
    mqttClient.publish("/esp32oled/state", "ERROR: getCovid19Data");
  }
}


void loop() {
  // // still connected ...?
  // if (WiFi.status() != WL_CONNECTED) {
  //   setupWifi();
  // }

  // get data every X minute(s) / hour(s) ...
  if (millis() > (time_1 + EVERY_HOUR)) {
    readyForNewData = true;
    time_1 = millis();
  }

  if (readyForNewData && ui.getUiState()->frameState == FIXED) {
    getCovid19Data(&display);
    readyForNewData = false;
  }

  int remainingTimeBudget = ui.update();

  if (remainingTimeBudget > 0) {

    mqttClient.loop();

    delay(remainingTimeBudget);
  }

  // if ((WiFi.status() == WL_CONNECTED) && (!mqttClient.connected())) {
  //   DEBUG_PRINTLN("no mqtt connection!!!");
  //   mqttConnect();
  // }
  // else {
  //   mqttClient.loop();
  // }

}
