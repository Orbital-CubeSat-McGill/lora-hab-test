#pragma once

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Wire.h>

bool habDisplayBegin(Adafruit_SSD1306& display);

// Shows boot splash (blocks ~3 s) before the node starts acting.
void habDisplaySplash(Adafruit_SSD1306& display, const char* role, bool radioOk);

// Default 5x7 font is 8 px tall; +1 px gap between rows.
static const int16_t HAB_OLED_LINE_H = 9;

inline void habOledRow(Adafruit_SSD1306& display, uint8_t row) {
  display.setCursor(0, (int16_t)row * HAB_OLED_LINE_H);
}
