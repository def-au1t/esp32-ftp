#include <SPI.h>
#include <MFRC522.h>
#include <Arduino.h>
#include "../../src/credentials.h"

class RFIDReader
{
public:
  RFIDReader(int SS_PIN, int RST_PIN)
  {
    this->rfid = MFRC522(SS_PIN, RST_PIN);
    this->rfid.PCD_Init();
    delay(4);
    this->rfid.PCD_DumpVersionToSerial();
    Serial.println("RFID Init");
  }

  bool isValidID(byte uid[10])
  {
    // Serial.println("Read ID: ");
    for (uint8_t i = 0; i < 4; i++)
    {
      Serial.print(uid[i]);
      Serial.print(",");
    }
    Serial.println("");
    bool valid = true;
    for (int id = 0; id < VALID_ID_COUNT; id++)
    {
      valid = true;
      for (int i = 0; i < 4; i++)
      {
        if (VALID_IDS[id][i] != uid[i])
        {
          valid = false;
          break;
        }
      }
      if (valid == true)
      {
        return true;
      }
    }
    return false;
  }

  bool verifyLoop()
  {
    int actualCardStatus = (int)rfid.PICC_IsNewCardPresent();
    this->history <<= 1;
    this->history |= actualCardStatus;

    if ((this->history & 0b111) == 0b001)
    {
      if (!rfid.PICC_ReadCardSerial())
      {
        Serial.println("Cannot read card info!");
        return false;
      }

      if (isValidID(rfid.uid.uidByte))
      {
        Serial.println("Valid");
        return true;
      }
      else
      {
        Serial.println("Invalid");
      }
    }
    return false;
  }

private:
  MFRC522 rfid;
  byte VALID_IDS[]
  int history = 0;
};
