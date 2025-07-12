
#include "handle.h"

const char *fileUrl = "http://192.168.1.106:8000/fackroun_project.bin";
String secretKey = "";  // will hold the secret key entered by user

static volatile bool sendDataFlag = false;
volatile int bitIndex = 0;

String timeString;
int timeStringLen = 0;

int commandLen = 0;

bool sendingCommand = false;
static unsigned long clockLowStartTime = 0;
static bool wasClockLow = false;
static bool lastTimeoutOccurred = false;

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

void handle_date_time() {
  int clockState = digitalRead(CLOCK_PIN);
  if (clockState == LOW) {
    if (!wasClockLow) {
      clockLowStartTime = millis();
      wasClockLow = true;
    } else if (millis() - clockLowStartTime >= 10) {
      bitIndex = 0;
    }
  } else {
    wasClockLow = false;
  }

  if (sendDataFlag && !sendingCommand) {
    sendDataFlag = false;
    if (bitIndex < timeStringLen * 8) {
      int byteIndex = bitIndex / 8;
      int bitInByte = bitIndex % 8;
      char c = timeString.charAt(byteIndex);
      int bitVal = (c >> bitInByte) & 0x01;
      digitalWrite(DATA_PIN, bitVal);
      bitIndex++;
    } else {
      bitIndex = 0;
    }
  }
}

void handle_command_send() {
  unsigned long startTime = millis();
  lastTimeoutOccurred = false;

  while (sendingCommand) {
    yield();
    if (millis() - startTime > 5000) {
      Serial.println("❌ Timeout in handle_command_send, aborting!");
      digitalWrite(DATA_PIN, LOW);
      bitIndex = 0;
      lastTimeoutOccurred = true;
      sendingCommand = false;
      break;
    }

    if (sendDataFlag) {
      sendDataFlag = false;
      if (bitIndex + 1 < (commandLen * 8)) {
        int byteIndex = bitIndex / 8;
        int bitInByte = bitIndex % 8;
        char c = commandString.charAt(byteIndex);
        int bitVal = (c >> bitInByte) & 0x01;
        digitalWrite(DATA_PIN, bitVal); 
        bitIndex++;
      } else {
        bitIndex = 0;
        digitalWrite(DATA_PIN, LOW);
        Serial.println("Command sent, switching back to time sending.");
        sendingCommand = false;
      }
    }
  }
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

