#ifndef GENERAL_H
#define GENERAL_H

#include <ESP8266HTTPClient.h>
#include "stdbool.h"
#include <ESP8266WiFi.h>
#include <LittleFS.h>

#define CLOCK_PIN        4        // GPIO4 (D2)
#define DATA_PIN         5        // GPIO5 (D1)

extern String timeString;
extern int timeStringLen ;
extern volatile int bitIndex;
extern String commandString ;

#endif // GENERAL_H