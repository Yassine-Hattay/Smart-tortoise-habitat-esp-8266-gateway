#include "general.h"

const char *ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 3600; // Tunisia UTC+1
const int daylightOffset_sec = 0;

WiFiServer server(80);

void setup() {
  Serial.begin(115'200);
Serial1.begin(115200, SERIAL_8N1, SERIAL_FULL, 2);

  pinMode(CLOCK_PIN, INPUT);
  pinMode(DATA_PIN, OUTPUT);
  digitalWrite(DATA_PIN, LOW);

  if (!LittleFS.begin()) {
    Serial.println("Failed to mount LittleFS");
    return;
  }

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected.");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  struct tm timeinfo;
  if (getLocalTime(&timeinfo)) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%02d/%02d/%04d %02d:%02d:%02d\n",
             timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year + 1900,
             timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    timeString = String(buf);
  } else {
    timeString = "Failed to get time\n";
  }
  timeStringLen = timeString.length();
  bitIndex = 0;

  attachInterrupt(digitalPinToInterrupt(CLOCK_PIN), onClockRising, RISING);
  server.begin();
}

void loop() {
  handleClient();
  handle_date_time();
}
