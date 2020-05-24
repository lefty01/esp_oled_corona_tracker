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
#include <HTTPClient.h>
#else
// if ESP8266
#include <ESP8266WiFi.h>
#include <ESP8266WiFiMulti.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecureBearSSL.h>
ESP8266WiFiMulti WiFiMulti;
#endif

#include <PubSubClient.h>
//#include <WiFiClientSecure.h>
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

const String COVID19_DATA_URL = "https://coronavirus-19-api.herokuapp.com/countries/GERMANY";

#define _UNUSED_ __attribute__((unused))

unsigned long time_1 = 0;
time_t now;
//bool readyForNewData = true;
bool readyForNewData = false;

String ipAddr;
String dnsAddr;
String rssi;

const unsigned maxWifiWaitSeconds = 60;
const int maxMqttRetry = 5;
bool mqttConnected = false;

// global object pointing to received json data
struct covid19Data_t {
  String country;
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
  String totalTests;
  String testsPerOneMillion;
  bool valid;
} covid19Data;
covid19Data_t covid19Data_prev;

// Initialize the OLED display using Wire library
// ESP8266 | ESP32 | pin
// --------+-------+----
//  D3     | 5     | SDA
//  D5     | 4     | SCL
#ifdef ESP32
WiFiClientSecure net;
SSD1306Wire display(0x3c, 5, 4);

#define MQTTDEVICEID  "wemosEsp32_oled"
#else
// if ESP8266
BearSSL::X509List   server_cert(server_crt_str);
BearSSL::X509List   client_crt(client_crt_str);
BearSSL::PrivateKey client_key(client_key_str);
BearSSL::WiFiClientSecure net;

BearSSL::X509List   herokuapp_cert(herokuapp_com_pem_str);
//BearSSL::WiFiClientSecure https_client;

SSD1306Wire display(0x3c, D3, D5);

#define MQTTDEVICEID  "wemosD1_oled"
#endif

const char* mqttSet      = "/" MQTTDEVICEID "/set";
const char* mqttState    = "/" MQTTDEVICEID "/state";
const char* mqttDateTime = "/" MQTTDEVICEID "/datetime";

const char* mqtt    = "/" MQTTDEVICEID "/state";

const char* mqttCovidCountry             = "/" MQTTDEVICEID "/covid19/country";
const char* mqttCovidCases               = "/" MQTTDEVICEID "/covid19/cases";
const char* mqttCovidTodayCases          = "/" MQTTDEVICEID "/covid19/todayCases";
const char* mqttCovidDeaths              = "/" MQTTDEVICEID "/covid19/deaths";
const char* mqttCovidTodayDeaths         = "/" MQTTDEVICEID "/covid19/todayDeaths";
const char* mqttCovidRecovered           = "/" MQTTDEVICEID "/covid19/recovered";
const char* mqttCovidActive              = "/" MQTTDEVICEID "/covid19/active";
const char* mqttCovidCritical            = "/" MQTTDEVICEID "/covid19/critical";
const char* mqttCovidCasesPerOneMillion  = "/" MQTTDEVICEID "/covid19/casesPerOneMillion";
const char* mqttCovidDeathsPerOneMillion = "/" MQTTDEVICEID "/covid19/deathsPerOneMillion";
const char* mqttCovidTime                = "/" MQTTDEVICEID "/covid19/datetime";



PubSubClient mqttClient(net);

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "europe.pool.ntp.org", GER_TZ_OFFSET); // 2h offset

const int screenW = 128;
const int screenH = 64;

OLEDDisplayUi ui(&display);


// ******  PROGRAM VERSION ******
const char* VERSION = "0.5.5";


void drawProgress(OLEDDisplay *display, int percentage, String label) {
  display->clear();
  display->setTextAlignment(TEXT_ALIGN_CENTER);
  display->setFont(ArialMT_Plain_10);
  display->drawString(64, 10, label);
  display->drawProgressBar(2, 28, 124, 10, percentage);
  display->display();
}

void rssiOverlay(OLEDDisplay *display, _UNUSED_ OLEDDisplayUiState* state) {
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
  display->setTextAlignment(TEXT_ALIGN_CENTER);
  display->setFont(ArialMT_Plain_16);
  display->drawStringMaxWidth(display->getWidth()/2, 30, 128, text);
  display->display();
}

// direction = 0 -> DOWN
// direction = 1 -> UP
void drawArrow(OLEDDisplay *display, int direction) {
  display->drawXbm(118, 30, 8, 8, direction == 1 ? upArrow : downArrow);
}
void clearArrow(OLEDDisplay *display) {
  display->drawXbm(118, 30, 8, 8, noArrow);
}



// Frames ...
void startFrame(OLEDDisplay *display, _UNUSED_ OLEDDisplayUiState* state, int16_t x, int16_t y)
{
  display->setFont(ArialMT_Plain_10);
  display->setTextAlignment(TEXT_ALIGN_CENTER);
  display->drawString(64 + x,  5 + y, "COVID-19");

  display->setFont(ArialMT_Plain_10);
  display->setTextAlignment(TEXT_ALIGN_LEFT);
  display->drawString(10 + x, 15 + y, "Tracker");
  display->drawString(70 + x, 25 + y, "v" + String(VERSION));

  display->setFont(ArialMT_Plain_24);
  display->setTextAlignment(TEXT_ALIGN_CENTER);
  display->drawString(64 + x,  33 + y, covid19Data.country);

  clearArrow(display);
  //display->drawXbm(x + 40,   y +  2, mqtt_width, mqtt_height, mqtt_bits);
  //display->drawString(x + 0, y + 50, "v" + String(VERSION) + ", ip: " + ipAddr);
}

void Frame1(OLEDDisplay *display, _UNUSED_ OLEDDisplayUiState* state, int16_t x, int16_t y)
{
  if (!covid19Data.valid)
    return;
  display->setTextAlignment(TEXT_ALIGN_CENTER);
  display->setFont(ArialMT_Plain_16);
  display->drawString(64 + x,  5 + y, "CASES:");
  display->drawString(64 + x, 30 + y, covid19Data.cases);
}
void Frame2(OLEDDisplay *display, _UNUSED_ OLEDDisplayUiState* state, int16_t x, int16_t y)
{
  if (!covid19Data.valid)
    return;
  display->setTextAlignment(TEXT_ALIGN_CENTER);
  display->setFont(ArialMT_Plain_16);
  display->drawString(64 + x,  5 + y, "TODAY CASES:");
  display->drawString(64 + x, 30 + y, covid19Data.todayCases);
}
void Frame3(OLEDDisplay *display, _UNUSED_ OLEDDisplayUiState* state, int16_t x, int16_t y)
{
  if (!covid19Data.valid)
    return;
  display->setTextAlignment(TEXT_ALIGN_CENTER);
  display->setFont(ArialMT_Plain_16);
  display->drawString(64 + x,  5 + y, "DEATHS:");
  display->drawString(64 + x, 30 + y, covid19Data.deaths);
}
void Frame4(OLEDDisplay *display, _UNUSED_ OLEDDisplayUiState* state, int16_t x, int16_t y)
{
  if (!covid19Data.valid)
    return;
  display->setTextAlignment(TEXT_ALIGN_CENTER);
  display->setFont(ArialMT_Plain_16);
  display->drawString(64 + x,  5 + y, "TODAY DEATHS:");
  display->drawString(64 + x, 30 + y, covid19Data.todayDeaths);

  clearArrow(display);
}
void Frame5(OLEDDisplay *display, _UNUSED_ OLEDDisplayUiState* state, int16_t x, int16_t y)
{
  if (!covid19Data.valid)
    return;
  display->setTextAlignment(TEXT_ALIGN_CENTER);
  display->setFont(ArialMT_Plain_16);
  display->drawString(64 + x,  5 + y, "ACTIVE:");
  display->drawString(64 + x, 30 + y, covid19Data.active);
  if (covid19Data.active > covid19Data_prev.active)
    drawArrow(display, 1);
  if (covid19Data.active < covid19Data_prev.active)
    drawArrow(display, 0);
}
void Frame6(OLEDDisplay *display, _UNUSED_ OLEDDisplayUiState* state, int16_t x, int16_t y)
{
  if (!covid19Data.valid)
    return;
  display->setTextAlignment(TEXT_ALIGN_CENTER);
  display->setFont(ArialMT_Plain_16);
  display->drawString(64 + x,  5 + y, "CRITICAL:");
  display->drawString(64 + x, 30 + y, covid19Data.critical);
  if (covid19Data.critical > covid19Data_prev.critical)
    drawArrow(display, 1);
  if (covid19Data.critical < covid19Data_prev.critical)
    drawArrow(display, 0);
}

void Frame7(OLEDDisplay *display, _UNUSED_ OLEDDisplayUiState* state, int16_t x, int16_t y)
{
  if (!covid19Data.valid)
    return;
  display->setTextAlignment(TEXT_ALIGN_CENTER);
  display->setFont(ArialMT_Plain_16);
  display->drawString(64 + x,  5 + y, "RECOVERED:");
  display->drawString(64 + x, 30 + y, covid19Data.recovered);
  if (covid19Data.recovered > covid19Data_prev.recovered)
    drawArrow(display, 1);
  if (covid19Data.recovered < covid19Data_prev.recovered)
    drawArrow(display, 0);
}

FrameCallback frames[] = { startFrame,
			   Frame1,
			   Frame2,
			   Frame3,
			   Frame4,
			   Frame5,
			   Frame6,
			   Frame7
};
OverlayCallback overlays[] = { rssiOverlay };

int numberOfFrames   = 8;
int numberOfOverlays = 1;


int setupWifi() {
  DEBUG_PRINTLN();
  DEBUG_PRINTLN("Connecting to wifi");

  unsigned retry_counter = 0;
#ifdef ESP32
  WiFi.begin(wifi_ssid, wifi_pass);

  while (WiFi.status() != WL_CONNECTED) {
#else
  WiFi.mode(WIFI_STA);
  WiFiMulti.addAP(wifi_ssid, wifi_pass);

  while ((WiFiMulti.run() != WL_CONNECTED)) {
#endif

    delay(500);
    DEBUG_PRINT(".");

    display.clear();
    display.drawString(64, 10, "Connecting to WiFi");
    display.drawXbm(46, 30, 8, 8, retry_counter % 3 == 0 ? activeSymbole : inactiveSymbole);
    display.drawXbm(60, 30, 8, 8, retry_counter % 3 == 1 ? activeSymbole : inactiveSymbole);
    display.drawXbm(74, 30, 8, 8, retry_counter % 3 == 2 ? activeSymbole : inactiveSymbole);
    display.display();

    retry_counter++;
    if (retry_counter > maxWifiWaitSeconds) {
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
  if (mqttClient.connect(MQTTDEVICEID, mqtt_user, mqtt_pass, mqttState, 1, 1, "OFFLINE")) {
    DEBUG_PRINTLN("connected");
    // Once connected, publish an announcement...
    mqttClient.publish(mqttState, connect_msg.c_str(), true);
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

  if (0 == strcmp(mqttSet, topic)) {
    DEBUG_PRINTLN("wemos esp oled set topic");
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
    "totalTests":918460,
    "testsPerOneMillion":10962
    }
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
  int rc = 0;
  covid19Data_prev = covid19Data;

  DEBUG_PRINTLN("getCovid19Data start");
  drawProgress(display, 10, "Updating time...");
  timeClient.forceUpdate();

  drawProgress(display, 40, "Fetching data...");

  //XXXXXXXXXXXXXXXXXXXXXX
  //std::unique_ptr<BearSSL::WiFiClientSecure>client(new BearSSL::WiFiClientSecure);
  //https_client.setTrustAnchors
  
  //HTTPClient https;
#ifdef ESP32
  HTTPClient https;
  https.begin(COVID19_DATA_URL, herokuapp_com_pem_str);
#else
  std::unique_ptr<BearSSL::WiFiClientSecure>https_client(new BearSSL::WiFiClientSecure);
  https_client->setFingerprint(herokuapp_com_fingerprint);

  HTTPClient https;
  https.begin(*https_client, COVID19_DATA_URL);
#endif
  
  int httpCode = https.GET();

  if (httpCode > 0) { //Check for the returning code
    String payload = https.getString();
    //DEBUG_PRINTLN("httpcode and payload:");
    //DEBUG_PRINTLN(httpCode);
    DEBUG_PRINTLN(payload);

    // https://arduinojson.org/v6/assistant/
    const size_t capacity = JSON_OBJECT_SIZE(12) + 170;
    DynamicJsonDocument jsonBuffer(capacity);

    DEBUG_PRINTLN("deserializeJson...");

    auto error = deserializeJson(jsonBuffer, payload);
    if (error) {
      drawText(display, "Error parsing GET response");
      DEBUG_PRINTLN(F("Error with response: deserializeJson() failed:"));
      DEBUG_PRINTLN(error.c_str());
      covid19Data.valid = false;
      rc = 1;
    }
    else {
      if (isValidData(jsonBuffer.as<JsonObject>())) {

	covid19Data.valid = true;
	covid19Data.country		= jsonBuffer["country"].as<String>();
	covid19Data.cases		= jsonBuffer["cases"].as<String>();
	covid19Data.todayCases		= jsonBuffer["todayCases"].as<String>();
	covid19Data.deaths		= jsonBuffer["deaths"].as<String>();
	covid19Data.todayDeaths		= jsonBuffer["todayDeaths"].as<String>();
	covid19Data.recovered		= jsonBuffer["recovered"].as<String>();
	covid19Data.active		= jsonBuffer["active"].as<String>();
	covid19Data.critical		= jsonBuffer["critical"].as<String>();
	covid19Data.casesPerOneMillion	= jsonBuffer["casesPerOneMillion"].as<String>();
	covid19Data.deathsPerOneMillion = jsonBuffer["deathsPerOneMillion"].as<String>();

	// if (publish via mqtt ... ) maybe enable/disable via mqtt SET topic
	mqttClient.publish(mqttCovidCountry,             covid19Data.country.c_str());
	mqttClient.publish(mqttCovidCases,               covid19Data.cases.c_str());
	mqttClient.publish(mqttCovidTodayCases,          covid19Data.todayCases.c_str());
	mqttClient.publish(mqttCovidDeaths,              covid19Data.deaths.c_str());
	mqttClient.publish(mqttCovidTodayDeaths,         covid19Data.todayDeaths.c_str());
	mqttClient.publish(mqttCovidRecovered,           covid19Data.recovered.c_str());
	mqttClient.publish(mqttCovidActive,              covid19Data.active.c_str());
	mqttClient.publish(mqttCovidCritical,            covid19Data.critical.c_str());
	mqttClient.publish(mqttCovidCasesPerOneMillion,  covid19Data.casesPerOneMillion.c_str());
	mqttClient.publish(mqttCovidDeathsPerOneMillion, covid19Data.deathsPerOneMillion.c_str());
	mqttClient.publish(mqttCovidTime,	         timeClient.getFormattedTime().c_str());
      }
    }
  }
  else {
    //drawText(display, "Error: GET failed");
    DEBUG_PRINTLN("Error on HTTP request");
    covid19Data.valid = false;
    drawProgress(display, 100, "... FAILED");
    delay(2000);
    rc = 1;
  }
  delay(1000);
  if (1 != rc) {
    drawProgress(display, 80, "Fetching data...");
    delay(1000);
    drawProgress(display, 100, "... Done");
  }
  https.end(); // Free the resources
  DEBUG_PRINTLN("getCovid19Data DONE");
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

  //https_client.setTrustAnchors(&herokuapp_cert);
#endif

  DEBUG_PRINTLN("set mqtt host/port and callback");
  mqttClient.setServer(mqtt_host, mqtt_port);
  //mqttClient.setCallback(mqttCallback);

  DEBUG_PRINTLN("calling mqttConnect()");
  mqttConnect();

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

  timeClient.begin();
  timeClient.forceUpdate();
  delay(1000);
  DEBUG_PRINTLN(timeClient.getFormattedTime());
  mqttClient.publish(mqttDateTime, timeClient.getFormattedTime().c_str());

  if (getCovid19Data(&display)) {
    DEBUG_PRINTLN("Error getting data");
    mqttClient.publish(mqttState, "ERROR: getCovid19Data");
  }
}


void loop() {
  // get data every X minute(s) / hour(s) ...
  if (millis() > (time_1 + EVERY_HOUR)) {
    readyForNewData = true;
    time_1 = millis();
  }

  if (readyForNewData && ui.getUiState()->frameState == FIXED) {
    if (0 == getCovid19Data(&display))
      readyForNewData = false;
  }

  int remainingTimeBudget = ui.update();

  if (remainingTimeBudget > 0) {

    mqttClient.loop();

    delay(remainingTimeBudget);
  }
}
