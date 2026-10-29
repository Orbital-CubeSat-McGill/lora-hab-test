#include "qmc5883p.h"

#include <math.h>

// QMC5883P register map (DFRobot / QST)
static const uint8_t REG_ID     = 0x00;
static const uint8_t REG_DATA   = 0x01;  // XL,XH,YL,YH,ZL,ZH
static const uint8_t REG_CTRL1  = 0x0A;
static const uint8_t REG_CTRL2  = 0x0B;

// CTRL1: mode continuous, ODR 50Hz, OSR1, range ±8G (typical demo config)
static const uint8_t CTRL1_CFG = 0xC3;
static const uint8_t CTRL2_CFG = 0x00;

bool Qmc5883P::writeReg(uint8_t reg, uint8_t val) {
  _wire->beginTransmission(_addr);
  _wire->write(reg);
  _wire->write(val);
  return _wire->endTransmission() == 0;
}

bool Qmc5883P::readRegs(uint8_t reg, uint8_t* buf, size_t n) {
  _wire->beginTransmission(_addr);
  _wire->write(reg);
  if (_wire->endTransmission(false) != 0) {
    return false;
  }
  size_t got = _wire->requestFrom((int)_addr, (int)n);
  if (got != n) {
    return false;
  }
  for (size_t i = 0; i < n; i++) {
    buf[i] = _wire->read();
  }
  return true;
}

bool Qmc5883P::begin(TwoWire& wire, uint8_t addr) {
  _wire = &wire;
  _addr = addr;

  // Probe: chip should ACK
  _wire->beginTransmission(_addr);
  if (_wire->endTransmission() != 0) {
    return false;
  }

  // Soft reset bit in CTRL2 if supported, then configure
  writeReg(REG_CTRL2, 0x80);
  delay(10);
  if (!writeReg(REG_CTRL2, CTRL2_CFG)) {
    return false;
  }
  if (!writeReg(REG_CTRL1, CTRL1_CFG)) {
    return false;
  }
  delay(20);

  float x, y, z;
  return readGauss(x, y, z);
}

bool Qmc5883P::readGauss(float& x, float& y, float& z) {
  uint8_t raw[6];
  if (!readRegs(REG_DATA, raw, 6)) {
    return false;
  }
  int16_t xi = (int16_t)((uint16_t)raw[0] | ((uint16_t)raw[1] << 8));
  int16_t yi = (int16_t)((uint16_t)raw[2] | ((uint16_t)raw[3] << 8));
  int16_t zi = (int16_t)((uint16_t)raw[4] | ((uint16_t)raw[5] << 8));

  // ±8G range ≈ 3000 LSB/G (datasheet-dependent; scale cancels for heading)
  const float lsb_per_g = 3000.0f;
  x = xi / lsb_per_g;
  y = yi / lsb_per_g;
  z = zi / lsb_per_g;
  return true;
}

bool Qmc5883P::readHeadingDeg(float& heading_deg) {
  float x, y, z;
  if (!readGauss(x, y, z)) {
    return false;
  }
  float h = atan2f(y, x) * 180.0f / PI;
  if (h < 0) {
    h += 360.0f;
  }
  heading_deg = h;
  return true;
}
