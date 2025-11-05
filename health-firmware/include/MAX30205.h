/**
 * MAX30205 Digital Temperature Sensor Driver
 * I2C interface, ±0.1°C accuracy
 */

#ifndef MAX30205_H
#define MAX30205_H

#include <Arduino.h>
#include <Wire.h>

#define MAX30205_ADDRESS 0x48
#define MAX30205_TEMP_REG 0x00

class MAX30205 {
public:
  MAX30205() {}
  
  bool begin() {
    Wire.beginTransmission(MAX30205_ADDRESS);
    if (Wire.endTransmission() == 0) {
      return true;
    }
    return false;
  }
  
  float getTemperature() {
    Wire.beginTransmission(MAX30205_ADDRESS);
    Wire.write(MAX30205_TEMP_REG);
    Wire.endTransmission(false);
    Wire.requestFrom(MAX30205_ADDRESS, 2);
    
    if (Wire.available() >= 2) {
      uint8_t msb = Wire.read();
      uint8_t lsb = Wire.read();
      
      // Temperature is 16-bit, MSB first
      int16_t rawTemp = (msb << 8) | lsb;
      // MAX30205: 16-bit, 0.00390625°C per LSB
      float temperature = rawTemp * 0.00390625;
      return temperature;
    }
    
    return 0.0;
  }
};

#endif


