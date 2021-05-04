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
#define LED_PIN 2

bool isLEDOn = false;
bool isFTPsuspended = false;
bool unsecureMode = false;
bool accessDetected = false;

#define LIGHT_VALUES_COUNT 10
int lightValues[LIGHT_VALUES_COUNT] = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
int lightIdx = 0;

TaskHandle_t FTPTask;

TaskHandle_t RFIDTask;
TaskHandle_t AccTask;
TaskHandle_t LightTask;
TaskHandle_t SDCleanerTask;

void switchMode()
{
  if (!unsecureMode)
  {
    Serial.println("Unsecure mode ON");
    unsecureMode = true;
    digitalWrite(2, true);
  }
  else
  {
    Serial.println("Unsecure mode OFF");
    unsecureMode = false;
    digitalWrite(2, false);
  }
  isLEDOn = !isLEDOn;
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
      // Serial.println(path);
      Serial.println(fileName);
      // Serial.println(absolutePath(path, fileName));
      SD.rmdir(fileName);
    }
    else
    {
      // Serial.println(path);
      Serial.println(fileName);
      // Serial.println(absolutePath(path, fileName));
      SD.remove(fileName);
    }
    file = dir.openNextFile();
  }
}

void SDCleanerThread(void *params)
{
  Serial.println("Access detected!");
  if (accessDetected == true || unsecureMode == true)
  {
    vTaskDelete(NULL);
  }
  accessDetected = true;
  if (SD.begin())
  {
    for (int i = 0; i < 5; i++)
    {
      digitalWrite(2, true);
      vTaskDelay(100);
      digitalWrite(2, false);
      vTaskDelay(100);
    }
    Serial.println("SD cleaner start!");
    SDCleaner("/");
    Serial.println("SD cleaner finished!");
  }
  accessDetected = false;
  vTaskDelete(NULL);
}

void LightThread(void *params)
{

  TickType_t xLastWakeTime;
  int mean = -1;
  int anomalyHistory = 0;
  while (1)
  {
    xLastWakeTime = xTaskGetTickCount();
    int light = analogRead(LIGHT_PIN);
    Serial.println("Light: " + String(light));
    if (lightValues[LIGHT_VALUES_COUNT - 1] != -1)
    {
      int sum = 0;
      for (int i = 0; i < LIGHT_VALUES_COUNT; i++)
      {
        sum += lightValues[i];
      }
      mean = sum / (LIGHT_VALUES_COUNT);
      Serial.println("Light - AVG: " + String(mean));
      if (abs(light - mean) > 20 && abs(light - mean) > mean * 0.1)
      {
        anomalyHistory |= 1;
      }
      if ((anomalyHistory & 0b111) == 0b111)
      {
        Serial.println("Light anomaly detected!");
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
        anomalyHistory = 0;
        for (int i = 0; i < LIGHT_VALUES_COUNT; i++)
        {
          lightValues[i] = -1;
        }
        lightIdx = 0;
      }
      else
      {
        lightValues[lightIdx] = light;
        lightIdx = (lightIdx + 1) % LIGHT_VALUES_COUNT;
      }
    }
    else
    { //Calibrating sensor
      lightValues[lightIdx] = light;
      lightIdx = (lightIdx + 1) % LIGHT_VALUES_COUNT;
    }

    anomalyHistory <<= 1;
    vTaskDelayUntil(&xLastWakeTime, 500);
  }
  vTaskDelete(NULL);
}

void RFIDThread(void *params)
{
  RFIDReader rf = RFIDReader(RFID_SS_PIN, RFID_RST_PIN);

  digitalWrite(2, false);
  TickType_t xLastWakeTime;
  while (1)
  {
    xLastWakeTime = xTaskGetTickCount();
    if (rf.verifyLoop())
    {

      switchMode();
    }
    vTaskDelayUntil(&xLastWakeTime, 400);
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
    if (mpu.checkForAnomalies())
    {
      Serial.print("MPU - Intrusion detected");
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
      mpu.calibrate();
    }
    vTaskDelayUntil(&xLastWakeTime, 500);
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
      ftpServer.mainFTPLoop();
    }
  }
  else
  {
    Serial.println("SD Card error!");
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
