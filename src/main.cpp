#include <Arduino.h>

#include <WiFi.h>
#include <WiFiClient.h>
#include <FTPServer.h>

const char* ssid = "Loading...";
const char* password = "tfys0681";

FTPServer ftpServer;
void setup(void){
  Serial.begin(115200);
  WiFi.begin(ssid, password);
  Serial.println("");

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.print("Connected to ");
  Serial.println(ssid);
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());


  if (SD.begin()) {
      Serial.println("SD opened!");
      ftpServer.begin("esp32","esp32", 50009);
  }
}

void loop(void){
  ftpServer.listenCommands();
}
