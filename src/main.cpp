#include <Arduino.h>

#include <SPI.h>
#include <MFRC522.h>
#include <RFIDReader.h>

#include <WiFi.h>
#include <WiFiClient.h>
#include <FTPServer.h>
#include <MPU6050.h>

#include "credentials.h"
// #include "soc/timer_group_struct.h"
// #include "soc/timer_group_reg.h"

#define RFID_RST_PIN 34
#define RFID_SS_PIN 32
#define LIGHT_PIN 35


bool isLEDOn = false;

TaskHandle_t FTPTask;

TaskHandle_t RFIDTask;
TaskHandle_t AccTask;
TaskHandle_t LightTask;

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

void LightThread(void *params)
{

  while (1)
  {
    delay(500);
    int light = analogRead(LIGHT_PIN);
    Serial.println("Light: "+String(light));
  }
  vTaskDelete(NULL);
}

void RFIDThread(void *params)
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
  vTaskDelete(NULL);
}

void AccThread(void *params)
{
  delay(100);
  MPU6050 mpu;
  mpu.init();

  while (1)
  {
    delay(1000);
    mpu.printSensorData();
  }
  vTaskDelete(NULL);
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
      // delay(1);
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
      100000,    /* Stack size of task */
      NULL,      /* parameter of the task */
      1,         /* priority of the task */
      &FTPTask,  /* Task handle to keep track of created task */
      1);        /* pin task to core */

  xTaskCreatePinnedToCore(
      RFIDThread,
      "RFID",
      10000,
      NULL,
      1,
      &RFIDTask,
      0);
    xTaskCreatePinnedToCore(
      LightThread,
      "Light",
      10000,
      NULL,
      1,
      &LightTask,
      0);
  xTaskCreatePinnedToCore(
      AccThread,
      "Acc",
      10000,
      NULL,
      1,
      &AccTask,
      0);
}

void loop()
{
}
