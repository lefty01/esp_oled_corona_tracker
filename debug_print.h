#ifndef _debug_print_h_
#define _debug_print_h_


#ifdef DEBUG
#include <PubSubClient.h>
#include <map>

#define DEBUG_BEGIN(x)    Serial.begin(x)
#define DEBUG_PRINT(x)    Serial.print(x)
#define DEBUG_PRINTLN(x)  Serial.println(x)
#define DEBUG_PRINTDEC(x) Serial.print(x, DEC)
#define DEBUG_PRINTHEX(x) Serial.print(x, HEX)

std::map<int, std::string> mqtt_state_map = {
  {MQTT_CONNECTION_TIMEOUT,      "MQTT_CONNECTION_TIMEOUT"},
  {MQTT_CONNECTION_LOST,         "MQTT_CONNECTION_LOST"},
  {MQTT_CONNECT_FAILED,          "MQTT_CONNECT_FAILED"},
  {MQTT_DISCONNECTED,            "MQTT_DISCONNECTED"},
  {MQTT_CONNECT_BAD_PROTOCOL,    "MQTT_CONNECT_BAD_PROTOCOL"},
  {MQTT_CONNECT_BAD_CLIENT_ID,   "MQTT_CONNECT_BAD_CLIENT_ID"},
  {MQTT_CONNECT_UNAVAILABLE,     "MQTT_CONNECT_UNAVAILABLE"},
  {MQTT_CONNECT_BAD_CREDENTIALS, "MQTT_CONNECT_BAD_CREDENTIALS"},
  {MQTT_CONNECT_UNAUTHORIZED,    "MQTT_CONNECT_UNAUTHORIZED"}
};

const char* state2str(int state) {
  return std::string(mqtt_state_map[state]).c_str();
}

#else

#define DEBUG_BEGIN(x)
#define DEBUG_PRINT(x)
#define DEBUG_PRINTLN(x)
#define DEBUG_PRINTDEC(x)
#define DEBUG_PRINTHEX(x)
#endif

#endif
