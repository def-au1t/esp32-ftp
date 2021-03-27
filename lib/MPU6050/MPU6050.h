#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <Wire.h>

class MPU6050
{
public:
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
    this->printSensorData();
  }

  void calibrate(){
    //TODO: Implement calibration and anomaly detections
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
    mpu.getEvent(&this->acc, &this->gyro, &this->temp);

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
