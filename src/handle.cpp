
#include "general.h"
#include <Crypto.h>
#include <Hash.h>
#include <SHA256.h>

const char *espSecretKey =
    "RRbp5ChK6CQH4Nkwo0PdfglrjJDOdBzbC5wn5IfCRlA2XtXPtePItOVm2q5y61y6Q4HaNn5uG2"
    "5gys1Zywd753wLddYPmm6ChHyrZZCZEru7Bpu3fI9aHxCWyuMGqwNy";
const char *ssid = "Orange-066C";
const char *password = "GMA6ABLMG87";
const char *fileUrl = "http://192.168.1.106:8000/fackroun_project.bin";

static bool humidifierFansOn = false;
static bool humidifierOn = false;

static volatile bool sendDataFlag = false;
volatile int bitIndex = 0;

String timeString;
int timeStringLen = 0;

static String commandString = "";
int commandLen = 0;

static bool sendingCommand = false;
static unsigned long clockLowStartTime = 0;
static bool wasClockLow = false;
static bool lastTimeoutOccurred = false;
static String lastStatusMsg = "";

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
    Serial.println("sending time data!!");
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

void handleClient() {
  WiFiClient client = server.available();
  if (!client)
    return;

  String currentLine = "";
  bool isPost = false;
  String postData = "";
  unsigned long timeout = millis() + 2000;

  while (client.connected() && millis() < timeout) {
    if (client.available()) {
      char c = client.read();
      currentLine += c;

      if (currentLine.endsWith("\r\n\r\n")) {
        if (isPost)
          while (client.available())
            postData += (char)client.read();
        break;
      }

      if (currentLine.startsWith("POST"))
        isPost = true;
    }
  }

  if (isPost) {
    String cmdBase = "";
    String cmdToSTM = "";
    bool performDownload = false;

    if (postData.indexOf("command=download_firmware") >= 0) {
      cmdBase = "Download Firmware";
      performDownload = true;
    } else if (postData.indexOf("command=toggle_fans") >= 0) {
      cmdBase = humidifierFansOn ? "Humidfier Fans OFF" : "Humidfier Fans ON";
      cmdToSTM = cmdBase + "\n";
    } else if (postData.indexOf("command=toggle_humidifier") >= 0) {
      cmdBase = humidifierOn ? "Humidfier OFF" : "Humidfier ON";
      cmdToSTM = cmdBase + "\n";
    } else if (postData.indexOf("command=automated_mode") >= 0) {
      cmdBase = "End esp Task";
      cmdToSTM = cmdBase + "\n";
    }

    String receivedHmac = getPostValue(postData, "hmac");

    char localHmac[65] = {0};
    if (!computeHMAC(espSecretKey, cmdBase.c_str(), localHmac,
                     sizeof(localHmac))) {
      Serial.println("Failed to compute HMAC");
    }

    if (receivedHmac.equalsIgnoreCase(localHmac)) {
      Serial.println("✅ HMAC verified!");

      if (performDownload) {
        if (downloadFirmware()) {
          lastStatusMsg = "<p style='color:green;'>✅ Firmware downloaded "
                          "successfully.</p>";

          // Prepare command string to STM32 via GPIO
          digitalWrite(DATA_PIN, HIGH);
          delay(1200);
          digitalWrite(DATA_PIN, LOW);
          delay(1);

          commandString = "Download Firmware\n";
          commandLen = commandString.length();
          sendingCommand = true;
          bitIndex = 0;

          Serial.println(
              "Start condition done, ready to send 'Download Firmware' bits");
          handle_command_send(); // Force handle it now

          Serial.println(
              "✅ GPIO command finished, now uploading binary over UART");

          // After GPIO command is finished, do UART upload
          if (uploadToSTM32("/fackroun_project.bin")) {
            lastStatusMsg +=
                "<p style='color:green;'>✅ Upload to STM32 completed.</p>";
          } else {
            lastStatusMsg +=
                "<p style='color:red;'>❌ Upload to STM32 failed.</p>";
          }
        } else {
          lastStatusMsg =
              "<p style='color:red;'>❌ Firmware download failed.</p>";
        }
      } else {
        if (postData.indexOf("command=toggle_fans") >= 0)
          humidifierFansOn = !humidifierFansOn;
        if (postData.indexOf("command=toggle_humidifier") >= 0)
          humidifierOn = !humidifierOn;

        digitalWrite(DATA_PIN, HIGH);
        delay(1200);
        digitalWrite(DATA_PIN, LOW);
        delay(1);

        commandString = cmdToSTM;
        commandLen = commandString.length();
        sendingCommand = true;
        bitIndex = 0; 

        Serial.println("Start condition done, ready to send command bits");
        lastStatusMsg = "<p style='color:green;'>✅ Command sent.</p>";
      }
    } else {
      Serial.println("❌ HMAC verification failed, ignoring command.");
      lastStatusMsg = "<p style='color:red;'>❌ HMAC verification failed.</p>";
    }
  } 

  String html = "<!DOCTYPE html><html><head><meta "
                "charset=\"UTF-8\"><title>ESP8266 Control</title>"
                "<style>button {padding: 20px; font-size: 18px; border: none; "
                "border-radius: 8px;}"
                ".on {background-color: green; color: white;}.off "
                "{background-color: red; color: white;}"
                "</style><script "
                "src='https://cdnjs.cloudflare.com/ajax/libs/crypto-js/4.1.1/"
                "crypto-js.min.js'></script>"
                "<script>function generateHMAC(command, key) {return "
                "CryptoJS.HmacSHA256(command, key).toString();}"
                "function prepareForm(formId, command) {var key = "
                "document.getElementById('key').value;"
                "var hmac = generateHMAC(command, key);var form = "
                "document.getElementById(formId);"
                "form.querySelector('input[name=\"hmac\"]').value = "
                "hmac;}</script></head><body>";

  handle_command_send();

  html += "<h1>ESP8266 Control Page</h1>";
  html += lastStatusMsg;
  html += "Secret Key: <input type='text' id='key' name='key'><br><br>";

  html +=
      "<form id='fansForm' method='POST' onsubmit=\"prepareForm('fansForm', '" +
      String(humidifierFansOn ? "Humidfier Fans OFF" : "Humidfier Fans ON") +
      "')\">";
  html += "<input type='hidden' name='hmac' id='fans_hmac'>";
  html += humidifierFansOn ? "<button class='on' type='submit' name='command' "
                             "value='toggle_fans'>Turn Fans OFF</button>"
                           : "<button class='off' type='submit' name='command' "
                             "value='toggle_fans'>Turn Fans ON</button>";
  html += "</form>";

  html += "<form id='humidifierForm' method='POST' "
          "onsubmit=\"prepareForm('humidifierForm', '" +
          String(humidifierOn ? "Humidfier OFF" : "Humidfier ON") + "')\">";
  html += "<input type='hidden' name='hmac' id='humidifier_hmac'>";
  html += humidifierOn
              ? "<button class='on' type='submit' name='command' "
                "value='toggle_humidifier'>Turn Humidifier OFF</button>"
              : "<button class='off' type='submit' name='command' "
                "value='toggle_humidifier'>Turn Humidifier ON</button>";
  html += "</form>";

  html += "<form id='autoForm' method='POST' "
          "onsubmit=\"prepareForm('autoForm', 'End esp Task')\">";
  html += "<input type='hidden' name='hmac' id='auto_hmac'>";
  html += "<button style='background-color: blue; color: white;' type='submit' "
          "name='command' value='automated_mode'>Automated Mode</button>";
  html += "</form>";

  html += "<form id='downloadForm' method='POST' "
          "onsubmit=\"prepareForm('downloadForm', 'Download Firmware')\">";
  html += "<input type='hidden' name='hmac' id='download_hmac'>";
  html +=
      "<button style='background-color: orange; color: white;' type='submit' "
      "name='command' value='download_firmware'>Download Firmware</button>";
  html += "</form>";

  html += "</body></html>";

  client.print("HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nConnection: "
               "close\r\n\r\n");
  client.print(html);

  delay(1);
  client.stop();
}
