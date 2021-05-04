#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <Wire.h>

#define MPU_HISTORY_COUNT 10

class MPU6050
{
public:
  float AccAverage[6];
  float AccHistory[MPU_HISTORY_COUNT][6];
  int historyIdx = 0;
  int anomalyHistory = 0;

  MPU6050()
  {
  }

  void init()
  {
    if (!this->mpu.begin())
    {
      Serial.println("Failed to find MPU6050 chip");
      while (1)
      {
        delay(10);
      }
    }
    Serial.println("MPU6050 Found!");
    this->mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
    this->mpu.setGyroRange(MPU6050_RANGE_500_DEG);
    this->mpu.setFilterBandwidth(MPU6050_BAND_5_HZ);
    this->calibrate();
  }
  void clearHistory(){
    for(int i=0; i<6; i++){
      for(int j=0; j< MPU_HISTORY_COUNT; j++){
        this->AccHistory[j][i] = -1;
      }
      this->AccAverage[i] = -1;
    }
    this->historyIdx = 0;
    this->anomalyHistory = 0;
  }

  void calibrate()
  {
    Serial.println("MPU - Calibration started");
    for(int i=0; i<MPU_HISTORY_COUNT; i++){
      do{
        Serial.println("MPU - Calibrating");
        mpu.getEvent(&this->acc, &this->gyro, &this->temp);
        this->printSensorData();
        vTaskDelay(50);
      }
      while((abs(this->acc.acceleration.x) < 7 && abs(this->acc.acceleration.y < 7) && abs(this->acc.acceleration.z < 7)) ||
        (abs(this->acc.acceleration.x) > 11) ||
        (abs(this->acc.acceleration.y) > 11) ||
        (abs(this->acc.acceleration.z) > 11) ||
        (abs(this->gyro.gyro.x) > 1)  ||
        (abs(this->gyro.gyro.y) > 1)  ||
        (abs(this->gyro.gyro.z) > 1)
        );
      this->saveToHistory();
      vTaskDelay(150);
    }
    Serial.println("MPU - Calibration finished");
    this->calculateAverage();
    Serial.println(String(this->AccAverage[5]));
  }

  void saveToHistory()
  {
    AccHistory[historyIdx][0] = this->gyro.gyro.x;
    AccHistory[historyIdx][1] = this->gyro.gyro.y;
    AccHistory[historyIdx][2] = this->gyro.gyro.z;
    AccHistory[historyIdx][3] = this->acc.acceleration.x;
    AccHistory[historyIdx][4] = this->acc.acceleration.y;
    AccHistory[historyIdx][5] = this->acc.acceleration.z;
    historyIdx = (historyIdx+1)%MPU_HISTORY_COUNT;
  }

  void calculateAverage(){
    for (int i=0; i<6; i++){
      float sum = 0;
      for(int j=0; j<MPU_HISTORY_COUNT; j++){
        sum+=AccHistory[j][i];
      }
      this->AccAverage[i] = sum/MPU_HISTORY_COUNT;
    }
  }

  boolean checkForAnomalies(){
    // Serial.println("---");
    // for (int i=0; i<MPU_HISTORY_COUNT; i++){
    //   for(int j=0; j<6; j++){
    //     Serial.print(String(AccHistory[i][j]));
    //     Serial.print(",");
    //   }
    //   Serial.println("");
    // }

    // Serial.println("---");
    if(this->AccHistory[MPU_HISTORY_COUNT-1][0] + 1 < 1e-3){
      Serial.println("MPU - not enought data for anomaly detection");
      return false;
    }
    mpu.getEvent(&this->acc, &this->gyro, &this->temp);

  this->printSensorData();

    this->calculateAverage();
    Serial.print("MPU - AVG: ");
    for(int i=0; i<6; i++){
      Serial.print(String(AccAverage[i]));
      Serial.print(",");
    }
    Serial.println("");
    this->anomalyHistory <<= 1;
    if((abs(this->acc.acceleration.x) < 0.5 && abs(this->acc.acceleration.y < 0.5) && abs(this->acc.acceleration.z < 0.5)) ||
        (abs(this->acc.acceleration.x) > 15) ||
        (abs(this->acc.acceleration.y) > 15) ||
        (abs(this->acc.acceleration.z) > 15) ){  //Error
      return false;
    }
    if((abs(this->acc.acceleration.x - AccAverage[3]) > 0.5) ||
      (abs(this->acc.acceleration.y - AccAverage[4]) > 0.5) ||
      (abs(this->acc.acceleration.z - AccAverage[5]) > 0.5)){  //Detect anomaly
        Serial.println("MPU - sensor data change detected");
        this->anomalyHistory |= 1;
        if(((this->anomalyHistory) & 0b11111) == 0b11111){
          this->clearHistory();
          Serial.println("MPU - Intrusion!!!");
          return true;
        }
      }
    else{
      this->saveToHistory();
    }
    return false;
  }

  sensors_vec_t getGyroscope()
  {
    mpu.getEvent(&this->acc, &this->gyro, &this->temp);
    return this->gyro.gyro;
  }

  float getTemperature()
  {
    mpu.getEvent(&this->acc, &this->gyro, &this->temp);
    return this->temp.temperature;
  }

  sensors_vec_t getAcceleration()
  {
    mpu.getEvent(&this->acc, &this->gyro, &this->temp);
    return this->acc.acceleration;
  }
  void printSensorData()
  {
    Serial.print("Acceleration X: ");
    Serial.print(this->acc.acceleration.x);
    Serial.print(", Y: ");
    Serial.print(this->acc.acceleration.y);
    Serial.print(", Z: ");
    Serial.print(this->acc.acceleration.z);
    Serial.print(" m/s^2 ");

    Serial.print(" Rotation X: ");
    Serial.print(this->gyro.gyro.x);
    Serial.print(", Y: ");
    Serial.print(this->gyro.gyro.y);
    Serial.print(", Z: ");
    Serial.print(this->gyro.gyro.z);
    Serial.print(" rad/s ");

    Serial.print(" Temperature: ");
    Serial.print(this->temp.temperature);
    Serial.print(" degC ");
    Serial.println("");
  }

private:
  Adafruit_MPU6050 mpu;
  sensors_event_t acc, gyro, temp;
  sensors_vec_t defaultAcc;
};
