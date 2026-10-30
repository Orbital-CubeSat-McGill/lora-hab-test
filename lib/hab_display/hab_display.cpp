#include "hab_display.h"

#include "pins.h"
#include "radio_config.h"

bool habDisplayBegin(Adafruit_SSD1306& display) {
  // Heltec V3 OLED (and external 3V3 rail) is gated by Vext — active LOW.
  // Without this the panel powers from I2C leakage and shows snow/noise.
  pinMode(PIN_VEXT, OUTPUT);
  digitalWrite(PIN_VEXT, LOW);
  delay(50);

  pinMode(PIN_OLED_RST, OUTPUT);
  digitalWrite(PIN_OLED_RST, LOW);
  delay(20);
  digitalWrite(PIN_OLED_RST, HIGH);
  delay(20);

  // Onboard OLED bus only (GPIO 17/18). External sensors use Wire1 on 41/42.
  Wire.begin(PIN_OLED_SDA, PIN_OLED_SCL);

  if (!display.begin(SSD1306_SWITCHCAPVCC, I2C_ADDR_OLED)) {
    return false;
  }
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.cp437(true);
  display.display();
  return true;
}

void habDisplaySplash(Adafruit_SSD1306& display, const char* role, bool radioOk) {
  display.clearDisplay();
  habOledRow(display, 0);
  display.print(F("OCS-3"));
  habOledRow(display, 1);
  display.print(F("Role: "));
  display.print(role);
  habOledRow(display, 2);
  display.printf("SF%d %.0fMHz %ddBm", LORA_SF, LORA_FREQUENCY_HZ / 1e6f,
                 LORA_TX_POWER_DBM);
  habOledRow(display, 3);
  display.print(F("Radio: "));
  display.print(radioOk ? F("OK") : F("FAIL"));
  display.display();
  delay(3000);
}
