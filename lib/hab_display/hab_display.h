#pragma once

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Wire.h>

bool habDisplayBegin(Adafruit_SSD1306& display);

// Shows boot splash (blocks ~3 s) before the node starts acting.
void habDisplaySplash(Adafruit_SSD1306& display, const char* role, bool radioOk);

// Heltec V3 LiPo sense (GPIO1 + ADC_Ctrl on GPIO37). Call after display begin.
void habBatteryBegin();
// Battery volts, or <0 if reading implausible (e.g. USB-only / no cell).
float habBatteryVolts();
// 0–100, or -1 if unknown.
int habBatteryPercent();
// Draw "NN%" (or "--%") at top-right of the current frame (before display()).
void habOledBatteryTopRight(Adafruit_SSD1306& display);

// Default 5x7 font is 8 px tall; +1 px gap between rows.
static const int16_t HAB_OLED_LINE_H = 9;

inline void habOledRow(Adafruit_SSD1306& display, uint8_t row) {
  display.setCursor(0, (int16_t)row * HAB_OLED_LINE_H);
}
