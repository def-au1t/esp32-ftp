#include <Arduino.h>

#include <SPI.h>
#include <MFRC522.h>

#define RST_PIN 22 // Configurable, see typical pin layout above
#define SS_PIN 21  // Configurable, see typical pin layout above

byte validID[] = {201, 44, 11, 179};

MFRC522 mfrc522(SS_PIN, RST_PIN); // Create MFRC522 instance

int history = 0;
bool isLEDOn = false;


TaskHandle_t FTP;
TaskHandle_t RFID;

void switchLamp()
{
  isLEDOn = !isLEDOn;
  digitalWrite(2, isLEDOn);
}

bool isValidID(byte uid[10])
{
  Serial.println("Read ID");
  for (uint8_t i = 0; i < 4; i++)
  {
    Serial.print(uid[i]);
  }
  Serial.println("");
  for (int i = 0; i < 4; i++)
  {
    if (validID[i] != uid[i])
    {
      return false;
    }
  }
  return true;
}

void SecondThread(void* params)
{
  for (;;)
  {
    delay(300);
    int actualCardStatus = (int)mfrc522.PICC_IsNewCardPresent();
    history <<= 1;
    history |= actualCardStatus;

    if ((history & 0b111) == 0b001)
    {
      if (!mfrc522.PICC_ReadCardSerial())
      {
        Serial.println("Cannot read card info!");
        return;
      }

      if (isValidID(mfrc522.uid.uidByte))
      {
        Serial.println("Valid");
        switchLamp();

        Serial.print("RFID - ");
        Serial.println(xPortGetCoreID());
      }
      else
      {
        Serial.println("Invalid - blocked");
      }
    }
  }
}

void FirstThread(void* params)
{
  for (;;)
  {
    delay(1000);
    Serial.println("First FTP Thread");
    Serial.print("FTP - ");
    Serial.println(xPortGetCoreID());
  }
}


void setup()
{
  Serial.begin(115200); // Initialize serial communications with the PC
  while (!Serial)
    ;                                // Do nothing if no serial port is opened (added for Arduinos based on ATMEGA32U4)
  SPI.begin();                       // Init SPI bus
  mfrc522.PCD_Init();                // Init MFRC522
  delay(4);                          // Optional delay. Some board do need more time after init to be ready, see Readme
  mfrc522.PCD_DumpVersionToSerial(); // Show details of PCD - MFRC522 Card Reader details
  Serial.println(F("Scan PICC to see UID, SAK, type, and data blocks..."));

  pinMode(2, OUTPUT);

  xTaskCreatePinnedToCore(
      FirstThread, /* Task function. */
      "FTP",   /* name of task. */
      10000,     /* Stack size of task */
      NULL,      /* parameter of the task */
      1,         /* priority of the task */
      &FTP,    /* Task handle to keep track of created task */
      0);        /* pin task to core 0 */
  xTaskCreatePinnedToCore(
    SecondThread, /* Task function. */
    "RFID",   /* name of task. */
    10000,     /* Stack size of task */
    NULL,      /* parameter of the task */
    1,         /* priority of the task */
    &RFID,    /* Task handle to keep track of created task */
    1);        /* pin task to core 0 */
}

void loop()
{
  delay(3000);
  Serial.print("Loop - ");
  Serial.println(xPortGetCoreID());
}

