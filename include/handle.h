#ifndef HANDLE_H
#define HANDLE_H

#include <time.h>
#include <Crypto.h>
#include <Hash.h>
#include <SHA256.h>
#include "general.h"

#define WIFI_SSID        "Orange-066C"
#define WIFI_PASSWORD    "GMA6ABLMG87"

#define NTP_SERVER       "pool.ntp.org"
#define GMT_OFFSET_SEC   3600     // Tunisia UTC+1
#define DAYLIGHT_OFFSET  0



void IRAM_ATTR onClockRising() ;
void handle_command_send() ;
void handle_date_time();
bool uploadToSTM32(const char *filePath) ;

extern volatile int bitIndex;
extern const char* ssid;
extern const char* password ;

#endif // HANDLE_H