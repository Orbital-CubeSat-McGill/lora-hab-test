#include "hab_display.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "pins.h"
#include "radio_config.h"

// V3 / V3.1: ADC_Ctrl LOW enables divider. V3.2: HIGH. Detected in habBatteryBegin.
static bool g_adcCtrlActiveHigh = true;
// Sticky: once we decide "no cell", stay there until a few stable good reads.
static uint8_t g_batGoodStreak = 0;
static bool    g_batPresent    = false;

// Divider 100k / (100k+390k) → VBAT = Vadc * 4.9
static float adcMvToVbat(float adc_mv) { return (adc_mv / 1000.0f) * 4.9f; }

// mean_v / spread_v (max-min) over n samples. Returns false if ADC path unusable.
static bool sampleVbat(bool ctrlHigh, float& mean_v, float& spread_v) {
  digitalWrite(PIN_ADC_CTRL, ctrlHigh ? HIGH : LOW);
  delay(12);
  const int n = 16;
  float vmin = 1e9f;
  float vmax = -1e9f;
  float sum  = 0;
  for (int i = 0; i < n; i++) {
    const float v = adcMvToVbat((float)analogReadMilliVolts(PIN_VBAT));
    sum += v;
    if (v < vmin) {
      vmin = v;
    }
    if (v > vmax) {
      vmax = v;
    }
  }
  mean_v   = sum / (float)n;
  spread_v = vmax - vmin;
  return true;
}

static float readVbatWithCtrl(bool ctrlHigh) {
  float mean = 0;
  float spread = 0;
  sampleVbat(ctrlHigh, mean, spread);
  return mean;
}

// Real 1S LiPo under load sits here; floating/USB garbage usually doesn't stay
// both in-range AND quiet.
static bool vbatPlausible(float v) { return v >= 3.20f && v <= 4.30f; }
static bool vbatStable(float spread) { return spread <= 0.08f; }  // 80 mV p-p

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
  habBatteryBegin();
  return true;
}

void habBatteryBegin() {
  analogReadResolution(12);
  pinMode(PIN_VBAT, INPUT);
  pinMode(PIN_ADC_CTRL, OUTPUT);

  const float vHigh = readVbatWithCtrl(true);
  const float vLow  = readVbatWithCtrl(false);
  const bool okH = vbatPlausible(vHigh);
  const bool okL = vbatPlausible(vLow);

  if (okH && !okL) {
    g_adcCtrlActiveHigh = true;
  } else if (okL && !okH) {
    g_adcCtrlActiveHigh = false;
  } else if (okH && okL) {
    // Prefer the reading closer to a typical mid/full LiPo.
    g_adcCtrlActiveHigh =
        (fabsf(vHigh - 3.85f) <= fabsf(vLow - 3.85f));
  } else {
    g_adcCtrlActiveHigh = true;  // Heltec V3.2 default
  }

  digitalWrite(PIN_ADC_CTRL, g_adcCtrlActiveHigh ? HIGH : LOW);

  float mean = 0;
  float spread = 0;
  sampleVbat(g_adcCtrlActiveHigh, mean, spread);
  g_batPresent    = vbatPlausible(mean) && vbatStable(spread);
  g_batGoodStreak = g_batPresent ? 3 : 0;
  Serial.printf(
      "[bat] ADC_Ctrl %s  V=%.2fV±%.0fmV present=%d (probe H=%.2f L=%.2f)\n",
      g_adcCtrlActiveHigh ? "HIGH" : "LOW", mean, spread * 1000.0f,
      (int)g_batPresent, vHigh, vLow);
}

float habBatteryVolts() {
  float mean = 0;
  float spread = 0;
  sampleVbat(g_adcCtrlActiveHigh, mean, spread);
  if (!vbatPlausible(mean) || !vbatStable(spread)) {
    return -1.0f;
  }
  return mean;
}

int habBatteryPercent() {
  float mean = 0;
  float spread = 0;
  sampleVbat(g_adcCtrlActiveHigh, mean, spread);

  const bool ok = vbatPlausible(mean) && vbatStable(spread);
  if (ok) {
    if (g_batGoodStreak < 3) {
      g_batGoodStreak++;
    }
    if (g_batGoodStreak >= 3) {
      g_batPresent = true;
    }
  } else {
    g_batGoodStreak = 0;
    g_batPresent    = false;
  }

  if (!g_batPresent) {
    return -1;
  }

  // Linear map empty→full for 1S LiPo display (good enough for OLED).
  const float empty = 3.30f;
  const float full  = 4.20f;
  int pct = (int)lroundf((mean - empty) / (full - empty) * 100.0f);
  if (pct < 0) {
    pct = 0;
  }
  if (pct > 100) {
    pct = 100;
  }
  return pct;
}

void habOledBatteryTopRight(Adafruit_SSD1306& display) {
  char buf[8];
  const int pct = habBatteryPercent();
  if (pct < 0) {
    snprintf(buf, sizeof(buf), "-1%%");
  } else {
    snprintf(buf, sizeof(buf), "%d%%", pct);
  }
  // Default 5x7 font: 6 px/char.
  const int16_t w = (int16_t)(strlen(buf) * 6);
  display.setCursor(128 - w, 0);
  display.print(buf);
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
