#include "general.h"
#include "handle.h"
#include "web_page.h"
#include <ArduinoOTA.h>

WiFiServer server(80);

const char *ssid = "Orange-066C";
const char *password = "GMA6ABLMG87";

void setup() {
  Serial.begin(115'200);
  Serial1.begin(115'200, SERIAL_8N1, SERIAL_FULL, 2);

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

  configTime(3600, 0, "pool.ntp.org", "time.nist.gov");

  struct tm timeinfo;
  if (getLocalTime(&timeinfo, 5000)) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%02d/%02d/%04d %02d:%02d:%02d",
             timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year + 1900,
             timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    timeString = String(buf);
    Serial.printf("✅ Setup completed at: %s\n", buf);
  } else {
    timeString = "Failed to get time\n";
    Serial.println("⚠️ Failed to get time in setup.");
  }

  timeStringLen = timeString.length();
  bitIndex = 0;

  attachInterrupt(digitalPinToInterrupt(CLOCK_PIN), onClockRising, RISING);
  server.begin();

  // ===========================
  // OTA setup
  // ===========================
  ArduinoOTA.setHostname("esp8266-ota");

  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH)
      type = "sketch";
    else
      type = "filesystem";
    Serial.println("Start updating " + type);
  });

  ArduinoOTA.onEnd([]() { Serial.println("\nEnd"); });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });

  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR)
      Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR)
      Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR)
      Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR)
      Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR)
      Serial.println("End Failed");
  });

  ArduinoOTA.begin();
  Serial.println("OTA ready V 69");
}

void loop() {
  ArduinoOTA.handle();
  handleClient();
  handle_date_time();
}
