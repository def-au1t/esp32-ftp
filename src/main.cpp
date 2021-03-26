#include <Arduino.h>

#include <SPI.h>
#include <MFRC522.h>
#include <RFIDReader.h>

#include <WiFi.h>
#include <WiFiClient.h>
#include <FTPServer.h>

#include "credentials.h"

#define RFID_RST_PIN 34 // Configurable, see typical pin layout above
#define RFID_SS_PIN 21  // Configurable, see typical pin layout above

bool isLEDOn = false;

TaskHandle_t FTPTask;
TaskHandle_t RFIDTask;

void switchLamp()
{
  isLEDOn = !isLEDOn;
  digitalWrite(2, isLEDOn);
}

void launchWiFi()
{
  WiFi.begin(ssid, password);
  Serial.println("");

  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.print("Connected to ");
  Serial.println(ssid);
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}

void SecondThread(void *params)
{
  RFIDReader rf = RFIDReader(RFID_SS_PIN, RFID_RST_PIN);

  while (1)
  {
    delay(300);
    if (rf.verifyLoop())
    {
      switchLamp();
    }
  }
}

void FTPThread(void *params)
{
  FTPServer ftpServer = FTPServer();
  if (SD.begin())
  {
    Serial.println("SD opened!");
    ftpServer.begin("esp32", "esp32", 50009);

    while (1)
    {
      delay(30);
      ftpServer.listenCommands();
    }
  }
  vTaskDelete(NULL);
}

void setup()
{

  Serial.begin(115200);
  launchWiFi();
  while (!Serial)
  {
  }
  SPI.begin(); // Init SPI bus

  pinMode(2, OUTPUT);

  xTaskCreatePinnedToCore(
      FTPThread, /* Task function. */
      "FTP",     /* name of task. */
      100000,     /* Stack size of task */
      NULL,      /* parameter of the task */
      1,         /* priority of the task */
      &FTPTask,  /* Task handle to keep track of created task */
      0);        /* pin task to core 0 */
  xTaskCreatePinnedToCore(
      SecondThread,
      "RFID",
      10000,
      NULL,
      1,
      &RFIDTask,
      1);
}

void loop()
{
}
