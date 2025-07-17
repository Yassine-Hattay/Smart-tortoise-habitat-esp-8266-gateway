#ifndef WEB_PAGE_H
#define WEB_PAGE_H

#include "handle.h"

extern WiFiServer server;

void handleClient();
bool computeHMAC(const char *key, const char *message, char *hmacOutput,
                 size_t hmacOutputLen);
String getPostValue(const String &postData, const String &key);
bool downloadFirmware();

extern int commandLen;
extern bool sendingCommand;
extern String latestReceivedData;

#endif // WEB_PAGE_H
