#include "general.h"
#include "handle.h"
#include "web_page.h"
#include <ArduinoOTA.h>

WiFiServer server(80);

const char *ssid = "Orange-066C";
const char *password = "GMA6ABLMG87";

const unsigned long logInterval = 900'000; // 15 minutes in milliseconds

void log_data() {
  if (millis() - lastLogTime >= logInterval) {
    lastLogTime = millis();

    String logEntry = latestReceivedData;
    appendLog(logEntry);
    Serial.println("Logged: " + logEntry);
  }
}

void handle_receive() {
  static String buffer = "";
  unsigned long startTime = millis();
  const unsigned long timeout = 10'000; // 10 seconds
  bool received_temp_humidity = false;

  
  while (!received_temp_humidity) {
    yield(); // allow other tasks to run

    // Check if data available on UART0 (Serial)
    while (Serial.available()) {
      char c = Serial.read();
      buffer += c;

      if (c == '\n') { // end of line
        Serial.print("Received line: ");
        Serial.print(buffer);
        latestReceivedData = buffer; // update global display buffer

        // Check format
        if (buffer.startsWith("T1:") && buffer.indexOf("H1:") != -1 &&
            buffer.indexOf("T2:") != -1 && buffer.indexOf("H2:") != -1) {
          received_temp_humidity = true; // valid data flag
          log_data();                    // ✅ only log if format matches
        } else {
          Serial.println("⚠️ Invalid format, skipping log.");
        }

        buffer = ""; // clear buffer
        break;
      }
    }

    // Check if timeout has passed
    if (millis() - startTime > timeout) {
      Serial.println("❌ Timeout waiting for data!");
      buffer = ""; // reset buffer on timeout
      break;
    }
  }
}

void setup() {
  Serial.begin(115'200);
  Serial1.begin(115'200, SERIAL_8N1, SERIAL_FULL, 2);

  pinMode(DATA_PIN, INPUT);
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
    char buf[40];
    snprintf(buf, sizeof(buf), "%02d/%02d/%04d %02d:%02d:%02d",
             timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year + 1900,
             timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    timeString = String(buf);
    Serial.printf("✅ Setup completed at: %s\n", buf);
  } else {
    timeString = "Failed to get time\n";
    Serial.println("⚠️ Failed to get time in setup.");

    // Restart ESP8266 after a short delay to avoid watchdog reset chaos
    delay(1000);
    ESP.restart();
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

  /*   if (LittleFS.format())
      Serial.println("LittleFS formatted successfully.");
    else
      Serial.println("LittleFS format failed."); */
}

void loop() {
  ArduinoOTA.handle();
  handleClient();
  handle_date_time_sending();
  handle_receive();
}
