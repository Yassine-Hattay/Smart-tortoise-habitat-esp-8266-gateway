
#include "handle.h"

#define MAX_LOGS 8000

static unsigned long logCount = 0; // persists only during runtime

const char *fileUrl = "http://192.168.1.106:8000/fackroun_project.bin";
String secretKey = ""; // will hold the secret key entered by user

volatile bool sendDataFlag = false;
volatile int bitIndex = 0;

String timeString;
int timeStringLen = 0;

int commandLen = 0;

bool sendingCommand = false;
bool received_temp_humidity = false;

static unsigned long clockLowStartTime = 0;
static bool wasClockLow = false;
static bool lastTimeoutOccurred = false;

unsigned long lastLogTime = 0;

String formatTimestamp(unsigned long ts) {
  time_t rawTime = (time_t)ts;
  struct tm *timeinfo = localtime(&rawTime);
  if (timeinfo != nullptr) {
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", timeinfo);
    return String(buf);
  } else {
    return String("Invalid time");
  }
}

void appendLog(const String &entry) {
  if (logCount >= MAX_LOGS) {
    // Reset log file by opening with "w"
    File f = LittleFS.open("/log.txt", "w"); // truncate the file
    if (!f) {
      Serial.println("Failed to open log file for truncating");
      return;
    }
    Serial.println("Log file truncated after reaching max logs");
    logCount = 0; // reset count
    f.close();
  }

  // Append new log
  File f = LittleFS.open("/log.txt", "a");
  if (!f) {
    Serial.println("Failed to open log file for appending");
    return;
  }
  unsigned long ts = getUnixTime();
  f.print("[");
  f.print(ts);
  f.print("] ");
  f.println(entry);
  f.close();

  logCount++;
}

String sanitizeLine(const String &line) {
  String cleanLine = "";
  for (unsigned int i = 0; i < line.length(); i++) {
    char c = line.charAt(i);
    // Keep only printable ASCII + basic whitespace
    if ((c >= 32 && c <= 126) || c == '\t' || c == '\r' || c == '\n')
      cleanLine += c;
    else
      cleanLine += '?'; // or just skip by not adding
  }
  return cleanLine;
}

String readLogs() {
  String finalHtml = "";

  File f = LittleFS.open("/log.txt", "r");
  if (!f)
    return "No logs found.";

  // Store lines temporarily
  const int max_logs = 300;
  std::vector<String> lines;

  while (f.available()) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (line.length() > 0)
      lines.push_back(line);
  }
  f.close();

  // Keep only last 300 logs
  int startIndex = lines.size() > max_logs ? lines.size() - max_logs : 0;

  // Loop in reverse order
  for (int i = lines.size() - 1; i >= startIndex; i--) {
    String cleanLine = sanitizeLine(lines[i]);

    int start = cleanLine.indexOf('[');
    int end = cleanLine.indexOf(']');
    if (start != -1 && end != -1 && end > start + 1) {
      String tsStr = cleanLine.substring(start + 1, end);
      unsigned long ts = tsStr.toInt();
      String humanTime = formatTimestamp(ts);

      String content = cleanLine.substring(end + 1);
      finalHtml += "[" + humanTime + "] " + content + "<br>";
    } else {
      finalHtml += cleanLine + "<br>";
    }
  }

  return finalHtml;
}

bool uploadToSTM32(const char *filePath) {
  File file = LittleFS.open(filePath, "r");
  if (!file) {
    Serial.println("❌ Failed to open file for STM32 upload");
    return false;
  }

  // Setup Serial1 (UART1 on GPIO2)
  Serial1.begin(115'200, SERIAL_8N1, SERIAL_FULL, 2);
  delay(100); // Give STM32 time to be ready

  uint8_t buffer[128];
  size_t totalSent = 0;

  while (file.available()) {
    size_t bytesRead = file.read(buffer, sizeof(buffer));

    Serial1.write(buffer, bytesRead);
    Serial1.flush(); // Ensure data is physically sent

    totalSent += bytesRead;
    delay(2); // Optional: let STM32 process, adjust as needed
  }

  file.close();
  Serial1.end();

  Serial.printf("✅ Upload to STM32 finished, total bytes sent: %d\n",
                totalSent);
  ESP.restart(); // Restart the ESP8266
  return true;
}

bool computeHMAC(const char *key, const char *message, char *hmacOutput,
                 size_t hmacOutputLen) {
  uint8_t result[32];
  hmac<SHA256>(result, sizeof(result), reinterpret_cast<const void *>(key),
               strlen(key), reinterpret_cast<const void *>(message),
               strlen(message));
  for (size_t i = 0; i < 32 && (i * 2 + 1) < hmacOutputLen; i++)
    sprintf(&hmacOutput[i * 2], "%02x", result[i]);
  return true;
}

String getPostValue(const String &postData, const String &key) {
  int start = postData.indexOf(key + "=");
  if (start == -1)
    return "";
  start += key.length() + 1;
  int end = postData.indexOf("&", start);
  if (end == -1)
    end = postData.length();
  return postData.substring(start, end);
}

void IRAM_ATTR onClockRising() { sendDataFlag = true; }

bool receive_requesting_time(unsigned long timeout_ms) {
  pinMode(DATA_PIN, INPUT);

  static char buffer[128];
  int index = 0;
  unsigned long startTime = millis();

  while (1) {
    yield();

    if (millis() - startTime > timeout_ms) {
      Serial.println("❌ Timeout waiting for data!");
      pinMode(DATA_PIN, OUTPUT);
      return false;
    }

    if (sendDataFlag) {
      sendDataFlag = false;
      uint8_t b = read_byte();

      if (index < sizeof(buffer) - 1)
        buffer[index++] = b;

      if (b == '\n') {
        buffer[index] = '\0';
        Serial.print("Received line in receive_requesting_time : ");
        Serial.print(buffer);

        if (strcmp(buffer, "requesting time\n") == 0) {
          pinMode(DATA_PIN, OUTPUT);
          return true;
        } else {
          index = 0; // reset buffer if not the expected command
        }
      }
    }
  }
}

void handle_date_time_sending() {
  if (digitalRead(5) == LOW)
    return;

  // Ensure it ends with '\n'
  String msg = timeString + "\n";
  const int maxRetries = 10;
  int attempts = 0;
  bool ackReceived = false;

  while (attempts < maxRetries) {
    if (digitalRead(5) == LOW)
      return;

    Serial1.print(msg); // Send string with '\n'
    Serial1.flush();

    Serial.println("Sent time string via UART1: " + msg);

    unsigned long startTime = millis();
    String ackBuffer = "";

    while (millis() - startTime < 2000) {
      if (digitalRead(5) == LOW)
        return;

      while (Serial.available()) {
        if (digitalRead(5) == LOW)
          return;

        char c = Serial.read();
        ackBuffer += c;

        if (ackBuffer.indexOf("ok") != -1) {
          ackReceived = true;
          break;
        }
      }

      if (ackReceived)
        break;

      delay(10);
    }

    if (ackReceived) {
      Serial.println("✅ Ack 'ok' received from STM32!");
      break;
    } else {
      Serial.println("⚠️ No ack received, retrying...");
      attempts++;
    }
  }

  if (!ackReceived)
    Serial.println("❌ Failed to get ack after 10 attempts.");
}

void handle_command_send() {
  unsigned long startTime = millis();
  lastTimeoutOccurred = false;

  if (!sendingCommand)
    return;

  // Convert String to C-style string
  const char *msg = commandString.c_str();
  int len = commandLen;

  Serial.println("📤 Sending command via UART...");
  Serial1.write(reinterpret_cast<const uint8_t *>(msg), len);
  Serial1.flush();
  Serial.println("✅ Command sent over UART, waiting for ACK...");

  // Buffer for incoming ACK
  String ackBuffer = "";
  bool ackReceived = false;

  unsigned long ackStartTime = millis();
  while (millis() - ackStartTime < 5000) { // Wait up to 2 sec
    while (Serial.available() > 0) {
      char c = Serial.read();
      ackBuffer += c;

      // Check if ACK is received
      if (ackBuffer.endsWith("ACK\n")) {
        ackReceived = true;
        break;
      }
    }
    if (ackReceived)
      break;

    yield(); // Let background tasks run if needed
  }

  if (ackReceived) {
    Serial.println("✅ ACK received from STM32!");
    lastStatusMsg =
        "<p style='color:green;'>✅ Command sent and ACK received.</p>";
  } else {
    Serial.println("❌ ACK timeout — no response from STM32!");
    lastTimeoutOccurred = true;
    lastStatusMsg = "<p style='color:red;'>❌ Command sent but ACK not "
                    "received (timeout).</p>";
  }
  // Mark as finished
  sendingCommand = false;
  bitIndex = 0;
}

bool downloadFirmware() {
  WiFiClient wifiClient;
  HTTPClient http;

  if (!http.begin(wifiClient, fileUrl)) {
    Serial.println("Failed to init HTTP");
    return false;
  }

  int httpCode = http.GET();
  if (httpCode == HTTP_CODE_OK) {
    File file = LittleFS.open("/fackroun_project.bin", "w");
    if (!file) {
      Serial.println("Failed to open file for writing");
      http.end();
      return false;
    }

    WiFiClient *stream = http.getStreamPtr();
    uint8_t buffer[128];
    int len = http.getSize();
    int written = 0;

    while (http.connected() && (len > 0 || len == -1)) {
      size_t size = stream->available();
      if (size) {
        int c = stream->readBytes(
            buffer, ((size > sizeof(buffer)) ? sizeof(buffer) : size));
        file.write(buffer, c);
        written += c;
        if (len > 0)
          len -= c;
      }
      delay(1);
    }

    file.close();
    Serial.printf("Download finished, %d bytes written\n", written);
    http.end();
    return true;
  } else {
    Serial.printf("Failed to download file, HTTP code: %d\n", httpCode);
    http.end();
    return false;
  }
}
