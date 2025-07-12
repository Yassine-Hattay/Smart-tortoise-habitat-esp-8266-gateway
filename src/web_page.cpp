#include "web_page.h"

static bool humidifierFansOn = false;
static bool humidifierOn = false;

static const char *espSecretKey =
    "RRbp5ChK6CQH4Nkwo0PdfglrjJDOdBzbC5wn5IfCRlA2XtXPtePItOVm2q5y61y6Q4HaNn5uG2"
    "5gys1Zywd753wLddYPmm6ChHyrZZCZEru7Bpu3fI9aHxCWyuMGqwNy";

static String lastStatusMsg = "";

String commandString = "";

// Helper to get UNIX timestamp on ESP8266 (assuming time is synchronized)
unsigned long getUnixTime() {
  return (unsigned long)time(nullptr); // requires time synchronization setup
}

// Helper to check timestamp freshness (maxAgeSeconds = 10 seconds)
bool isTimestampFresh(unsigned long ts) {
  unsigned long now = getUnixTime();
  if (ts > now)
    return false; // future timestamp not allowed
  if ((now - ts) > 10)
    return false; // older than 10 seconds -> stale
  return true;
}

unsigned long extractTimestamp(const String &cmd) {
  int tsIndex = cmd.indexOf(";ts=");
  if (tsIndex == -1)
    return 0;
  String tsStr = cmd.substring(tsIndex + 4);
  tsStr.trim();
  Serial.println("Timestamp substring extracted: '" + tsStr + "'");
  return tsStr.toInt();
}

// Remove the ";ts=TIMESTAMP" part from command string
String stripTimestamp(const String &cmd) {
  int tsIndex = cmd.indexOf(";ts=");
  if (tsIndex == -1)
    return cmd;
  return cmd.substring(0, tsIndex);
}

// URL decode function for decoding command string from POST data
String urlDecode(const String &input) {
  String decoded = "";
  char temp[] = "0x00";
  unsigned int len = input.length();
  unsigned int i = 0;

  while (i < len) {
    char c = input.charAt(i);
    if (c == '+') {
      decoded += ' ';
    } else if (c == '%') {
      if (i + 2 < len) {
        temp[2] = input.charAt(i + 1);
        temp[3] = input.charAt(i + 2);
        decoded += (char)strtol(temp, NULL, 16);
        i += 2;
      }
    } else {
      decoded += c;
    }
    i++;
  }
  return decoded;
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
    String cmdBase = "";  // will contain command WITH timestamp (for HMAC)
    String cmdToSTM = ""; // command without timestamp, to send to STM32
    bool performDownload = false;

    String receivedCmdEncoded = getPostValue(postData, "command");
    Serial.println("Received command (encoded): " + receivedCmdEncoded);

    String onceDecoded = urlDecode(receivedCmdEncoded);
    Serial.println("Once decoded: " + onceDecoded);

    String receivedCmd = urlDecode(onceDecoded);
    Serial.println("Fully decoded: " + receivedCmd);

    unsigned long ts = extractTimestamp(receivedCmd);
    Serial.printf("Extracted timestamp: %lu\n", ts);

    if (ts == 0 || !isTimestampFresh(ts)) {
      Serial.println("❌ Timestamp missing or not fresh, rejecting command");
      lastStatusMsg =
          "<p style='color:red;'>❌ Command timestamp missing or expired.</p>";
      goto SEND_RESPONSE;
    }

    // For comparison, we compute HMAC over the full command WITH timestamp
    // (decoded)
    cmdBase = receivedCmd;

    // For STM32, strip off the timestamp part before sending the command
    cmdToSTM = stripTimestamp(receivedCmd) + "\n";

    if (cmdToSTM.startsWith("Download Firmware")) {
      performDownload = true;
    } else if (cmdToSTM == "Humidfier Fans ON" ||
               cmdToSTM == "Humidfier Fans OFF") {
      // Toggle fan state after verifying command
    } else if (cmdToSTM == "Humidfier ON" || cmdToSTM == "Humidfier OFF") {
      // Toggle humidifier state after verifying command
    } else if (cmdToSTM == "End esp Task") {
      // Automated mode command
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
          handle_command_send();

          Serial.println(
              "✅ GPIO command finished, now uploading binary over UART");

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
        // Toggle states accordingly
        if (cmdToSTM == "Humidfier Fans ON")
          humidifierFansOn = true;
        else if (cmdToSTM == "Humidfier Fans OFF")
          humidifierFansOn = false;
        else if (cmdToSTM == "Humidfier ON")
          humidifierOn = true;
        else if (cmdToSTM == "Humidfier OFF")
          humidifierOn = false;

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

SEND_RESPONSE:

  String html =
      "<!DOCTYPE html><html><head><meta charset=\"UTF-8\"><title>ESP8266 "
      "Control</title>"
      "<style>button {padding: 20px; font-size: 18px; border: none; "
      "border-radius: 8px;} "
      ".on {background-color: green; color: white;}.off {background-color: "
      "red; color: white;}</style>"
      "<script "
      "src='https://cdnjs.cloudflare.com/ajax/libs/crypto-js/4.1.1/"
      "crypto-js.min.js'></script>"
      "<script>"
      "function setKey() {"
      "  var inputKey = prompt('Enter your secret key:');"
      "  if (inputKey) {"
      "    localStorage.setItem('espKey', inputKey);"
      "    alert('Key saved locally! You can now use the buttons without "
      "entering it again.');"
      "  }"
      "}"
      "function generateHMAC(command, key) {"
      "  return CryptoJS.HmacSHA256(command, key).toString();"
      "}"
      "function prepareForm(formId, command) {"
      "  var key = localStorage.getItem('espKey');"
      "  if (!key) {"
      "    alert('Please set the key first using the Set Key button.');"
      "    return false;"
      "  }"
      "  var timestamp = Math.floor(Date.now()/1000);"
      "  var cmdWithTs = command + ';ts=' + timestamp;"
      "  var hmac = generateHMAC(cmdWithTs, key);"
      "  var form = document.getElementById(formId);"
      "  form.querySelector('input[name=\"command\"]').value = "
      "encodeURIComponent(cmdWithTs);"
      "  form.querySelector('input[name=\"hmac\"]').value = hmac;"
      "  return true;"
      "}"
      "function clearKey() {"
      "  localStorage.removeItem('espKey');"
      "  alert('Key cleared. Please set it again.');"
      "}"
      "</script></head><body>";

  handle_command_send();

  html += "<h1>ESP8266 Control Page</h1>";
  html += lastStatusMsg;

  html += "<button onclick=\"setKey()\" style='background-color: purple; "
          "color: white; padding: 10px; border-radius: 8px;'>Set Key</button> ";
  html +=
      "<button onclick=\"clearKey()\" style='background-color: gray; color: "
      "white; padding: 10px; border-radius: 8px;'>Clear Key</button><br><br>";

  html +=
      "<form id='fansForm' method='POST' onsubmit=\"return "
      "prepareForm('fansForm', '" +
      String(humidifierFansOn ? "Humidfier Fans OFF" : "Humidfier Fans ON") +
      "')\">";
  html += "<input type='hidden' name='command' value=''>";
  html += "<input type='hidden' name='hmac' id='fans_hmac'>";
  html += humidifierFansOn ? "<button class='on' type='submit' name='command' "
                             "value='toggle_fans'>Turn Fans OFF</button>"
                           : "<button class='off' type='submit' name='command' "
                             "value='toggle_fans'>Turn Fans ON</button>";
  html += "</form>";

  html += "<form id='humidifierForm' method='POST' onsubmit=\"return "
          "prepareForm('humidifierForm', '" +
          String(humidifierOn ? "Humidfier OFF" : "Humidfier ON") + "')\">";
  html += "<input type='hidden' name='command' value=''>";
  html += "<input type='hidden' name='hmac' id='humidifier_hmac'>";
  html += humidifierOn
              ? "<button class='on' type='submit' name='command' "
                "value='toggle_humidifier'>Turn Humidifier OFF</button>"
              : "<button class='off' type='submit' name='command' "
                "value='toggle_humidifier'>Turn Humidifier ON</button>";
  html += "</form>";

  html += "<form id='autoForm' method='POST' onsubmit=\"return "
          "prepareForm('autoForm', 'End esp Task')\">";
  html += "<input type='hidden' name='command' value=''>";
  html += "<input type='hidden' name='hmac' id='auto_hmac'>";
  html += "<button style='background-color: blue; color: white;' type='submit' "
          "name='command' value='automated_mode'>Automated Mode</button>";
  html += "</form>";

  html += "<form id='downloadForm' method='POST' onsubmit=\"return "
          "prepareForm('downloadForm', 'Download Firmware')\">";
  html += "<input type='hidden' name='command' value=''>";
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
