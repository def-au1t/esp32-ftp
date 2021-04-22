#include <Arduino.h>

#include <SPI.h>
#include <MFRC522.h>
#include <RFIDReader.h>

#include <WiFi.h>
#include <WiFiClient.h>
#include <FTPServer.h>
#include <MPU6050.h>

#include "credentials.h"

#define RFID_RST_PIN 34
#define RFID_SS_PIN 32
#define LIGHT_PIN 35

bool isLEDOn = false;
bool isFTPsuspended = false;

TaskHandle_t FTPTask;

TaskHandle_t RFIDTask;
TaskHandle_t AccTask;
TaskHandle_t LightTask;
TaskHandle_t SDCleanerTask;

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
    vTaskDelay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.print("Connected to ");
  Serial.println(ssid);
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}

String absolutePath(String path, String filename)
{
  if (path = "/")
  {
    return path + filename;
  }
  else
  {
    return path + "/" + filename;
  }
}

void SDCleaner(String path)
{
  File dir = SD.open(path);
  File file = dir.openNextFile();
  while (file)
  {
    String fileName = file.name();
    if (file.isDirectory())
    {
      SDCleaner(fileName);
      // Serial.println(fileName);
      SD.rmdir(absolutePath(path,fileName));
    }
    else
    {
      // Serial.println(fileName);
      SD.remove(absolutePath(path,fileName));
    }
    file = dir.openNextFile();
  }
}

void SDCleanerThread(void *params)
{
  if (SD.begin())
  {
    Serial.println("SD cleaner start!");
    SDCleaner("/");
    Serial.println("SD cleaner finished!");
  }
  vTaskDelete(NULL);
}

void LightThread(void *params)
{
  TickType_t xLastWakeTime;
  while (1)
  {
    xLastWakeTime = xTaskGetTickCount();
    int light = analogRead(LIGHT_PIN);
    Serial.println("Light: " + String(light));
    vTaskDelayUntil(&xLastWakeTime, 1000);
  }
  vTaskDelete(NULL);
}

void RFIDThread(void *params)
{
  RFIDReader rf = RFIDReader(RFID_SS_PIN, RFID_RST_PIN);

  TickType_t xLastWakeTime;
  while (1)
  {
    xLastWakeTime = xTaskGetTickCount();
    if (rf.verifyLoop())
    {
      switchLamp();
      vTaskSuspend(FTPTask);
      xTaskCreatePinnedToCore(
          SDCleanerThread,
          "SDCleaner",
          10000,
          NULL,
          1,
          &SDCleanerTask,
          1);
      vTaskResume(FTPTask);
    }
    vTaskDelayUntil(&xLastWakeTime, 300);
  }
  vTaskDelete(NULL);
}

void AccThread(void *params)
{
  TickType_t xLastWakeTime;
  vTaskDelay(100);
  MPU6050 mpu;
  mpu.init();

  while (1)
  {
    xLastWakeTime = xTaskGetTickCount();
    mpu.printSensorData();
    vTaskDelayUntil(&xLastWakeTime, 1000);
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
      vTaskDelay(1);
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
  vTaskDelay(1);
}
