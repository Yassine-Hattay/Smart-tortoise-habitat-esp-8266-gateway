#include "web_page.h"

static bool humidifierFansOn = false;
static bool humidifierOn = false;


static const char *espSecretKey =
    "RRbp5ChK6CQH4Nkwo0PdfglrjJDOdBzbC5wn5IfCRlA2XtXPtePItOVm2q5y61y6Q4HaNn5uG2"
    "5gys1Zywd753wLddYPmm6ChHyrZZCZEru7Bpu3fI9aHxCWyuMGqwNy";

static String lastStatusMsg = "";

String commandString = "";

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
