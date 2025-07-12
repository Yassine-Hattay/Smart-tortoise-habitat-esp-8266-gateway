#include "general.h"

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

  // ✅ Tunisia timezone: UTC+1 (3600 seconds), no daylight offset (0)
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
}

void loop() {
  handleClient();
  handle_date_time();
}
