#ifndef HANDLE_H
#define HANDLE_H

#include "general.h"
#include <Crypto.h>
#include <Hash.h>
#include <SHA256.h>
#include <time.h>

#define WIFI_SSID "Orange-066C"
#define WIFI_PASSWORD "GMA6ABLMG87"

#define NTP_SERVER "pool.ntp.org"
#define GMT_OFFSET_SEC 3600 // Tunisia UTC+1
#define DAYLIGHT_OFFSET 0

void IRAM_ATTR onClockRising();
void handle_command_send();
void handle_date_time_sending();
bool uploadToSTM32(const char *filePath);
void handle_receive();
void appendLog(const String &entry);
unsigned long getUnixTime();
String readLogs();
uint8_t read_byte();

extern volatile int bitIndex;
extern const char *ssid;
extern const char *password;
extern bool received_temp_humidity;
extern unsigned long lastLogTime;
extern String lastStatusMsg ;

#endif // HANDLE_H
