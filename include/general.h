#ifndef GENERAL_HPP
#define GENERAL_HPP

#include <ESP8266WiFi.h>
#include <time.h>
#include <ESP8266HTTPClient.h>
#include <LittleFS.h>


#define WIFI_SSID        "Orange-066C"
#define WIFI_PASSWORD    "GMA6ABLMG87"

#define NTP_SERVER       "pool.ntp.org"
#define GMT_OFFSET_SEC   3600     // Tunisia UTC+1
#define DAYLIGHT_OFFSET  0

#define CLOCK_PIN        4        // GPIO4 (D2)
#define DATA_PIN         5        // GPIO5 (D1)

void handleClient();
void IRAM_ATTR onClockRising() ;
void handle_command_send() ;
void handle_date_time();

extern WiFiServer server;
extern volatile int bitIndex;
extern int timeStringLen ;
extern String timeString;
extern const char* ssid;
extern const char* password ;

#endif // GENERAL_HPP