#pragma once

#include <Arduino.h>
#include <Wire.h>

// Minimal QMC5883P driver (I2C 0x2C). Soft-fail friendly.
class Qmc5883P {
 public:
  bool begin(TwoWire& wire = Wire, uint8_t addr = 0x2C);
  bool readGauss(float& x, float& y, float& z);
  // Heading degrees [0,360) from X/Y, no tilt compensation.
  bool readHeadingDeg(float& heading_deg);

 private:
  TwoWire* _wire = nullptr;
  uint8_t _addr = 0x2C;
  bool writeReg(uint8_t reg, uint8_t val);
  bool readRegs(uint8_t reg, uint8_t* buf, size_t n);
};
