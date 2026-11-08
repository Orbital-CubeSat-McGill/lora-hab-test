#include <Adafruit_SSD1306.h>
#include <Arduino.h>
#include <RadioLib.h>
#include <Wire.h>

#include "hab_display.h"
#include "hab_packet.h"
#include "hab_radio.h"
#include "pins.h"
#include "radio_config.h"

static SX1262 radio =
    new Module(PIN_LORA_NSS, PIN_LORA_DIO1, PIN_LORA_RST, PIN_LORA_BUSY);
static Adafruit_SSD1306 display(128, 64, &Wire, -1);

static volatile bool g_rxFlag = false;

static float g_lastRssi = 0;
static float g_lastSnr = 0;
static uint16_t g_lastSeq = 0;
static int32_t g_lastAltMm = 0;
static uint32_t g_lastRxMs = 0;
static bool g_havePacket = false;

static void IRAM_ATTR onRadioRx() { g_rxFlag = true; }

static void armReceive() {
  g_rxFlag = false;
  radio.startReceive();
}

static void utcNow(char *buf, size_t cap) {
  // No RTC: use millis-based placeholder; host logger stamps wall clock.
  snprintf(buf, cap, "t+%lu", (unsigned long)millis());
}

static void updateOled() {
  display.clearDisplay();
  uint8_t row = 0;
  habOledRow(display, row++);
  display.print(F("GROUND"));
  habOledRow(display, row++);
  display.printf("SF%d %ddBm", LORA_SF, LORA_TX_POWER_DBM);
  if (!g_havePacket) {
    habOledRow(display, row++);
    display.print(F("waiting..."));
  } else {
    habOledRow(display, row++);
    display.printf("RSSI %.0f SNR %.1f", g_lastRssi, g_lastSnr);
    habOledRow(display, row++);
    display.printf("seq %u", g_lastSeq);
    habOledRow(display, row++);
    display.printf("alt %.0fm", g_lastAltMm / 1000.0);
    const uint32_t age = (millis() - g_lastRxMs) / 1000UL;
    habOledRow(display, row++);
    display.printf("age %lus", (unsigned long)age);
  }
  display.display();
}

static void onDecoded(const HabPacket &pkt, float rssi, float snr) {
  if (pkt.type != HAB_PKT_TELEM) {
    return;
  }

  g_lastRssi = rssi;
  g_lastSnr = snr;
  g_lastSeq = pkt.seq;
  g_lastAltMm = pkt.sample.alt_mm;
  g_lastRxMs = millis();
  g_havePacket = true;

  char utc[24];
  utcNow(utc, sizeof(utc));

  habPrettyPrintTelem(Serial, pkt, rssi, snr);
  habCsvLineGroundFlight(Serial, pkt, rssi, snr, utc);

  updateOled();
}

static void serviceRx() {
  if (!g_rxFlag) {
    return;
  }
  g_rxFlag = false;

  uint8_t buf[HAB_PKT_MAX_LEN];
  int state = radio.readData(buf, sizeof(buf));
  if (state == RADIOLIB_ERR_NONE) {
    size_t len = radio.getPacketLength();
    float rssi = radio.getRSSI();
    float snr = radio.getSNR();
    HabPacket pkt;
    if (habPacketDecode(buf, len, pkt)) {
      onDecoded(pkt, rssi, snr);
    } else {
      Serial.printf("[ground] ignore bad pkt len=%u\n", (unsigned)len);
    }
  }
  armReceive();
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println(F("\n=== HAB Ground ==="));
  Serial.println(F("Laptop test: log USB serial with:"));
  Serial.println(
      F("  uv run tools/serial_logger.py /dev/ttyUSB0 --lat LAT --lon LON"));
  Serial.println(F("Files: logs/<utc>_<phy>_<session_id>/ …"));
  habRadioPrintConfig(Serial);
  habCsvHeaderGroundFlight(Serial);

  bool dispOk = habDisplayBegin(display);
  if (!dispOk) {
    Serial.println(F("[ground] WARN: OLED init failed"));
  }

  int st = habRadioBegin(radio);
  bool radioOk = (st == RADIOLIB_ERR_NONE);
  if (!radioOk) {
    Serial.printf("[ground] FATAL: radio init failed (%d)\n", st);
    if (dispOk) {
      habDisplaySplash(display, "GROUND", false);
    }
    while (true) {
      delay(1000);
    }
  }
  Serial.println(F("[ground] Radio OK"));

  radio.setPacketReceivedAction(onRadioRx);

  if (dispOk) {
    habDisplaySplash(display, "GROUND", true);
    updateOled();
  }

  armReceive();
  Serial.println(F("[ground] Listening (TELEM only)"));
}

void loop() {
  serviceRx();

  static uint32_t lastOled = 0;
  if (millis() - lastOled > 500) {
    lastOled = millis();
    updateOled();
  }
}
