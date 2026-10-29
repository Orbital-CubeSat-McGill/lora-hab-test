#include <Arduino.h>
#include <Adafruit_SSD1306.h>
#include <RadioLib.h>
#include <Wire.h>

#include "hab_display.h"
#include "hab_packet.h"
#include "hab_radio.h"
#include "hab_sensors.h"
#include "pins.h"
#include "radio_config.h"

static SX1262 radio = new Module(PIN_LORA_NSS, PIN_LORA_DIO1, PIN_LORA_RST,
                                 PIN_LORA_BUSY);
static Adafruit_SSD1306 display(128, 64, &Wire, -1);

static HabSample g_latest;
static char g_gpsTime[16];
static uint16_t g_telemSeq = 0;
static uint32_t g_lastSampleMs = 0;
static uint32_t g_lastTelemMs  = 0;

static volatile bool g_rxFlag = false;

static void IRAM_ATTR onRadioRx() { g_rxFlag = true; }

static void armReceive() {
  g_rxFlag = false;
  radio.startReceive();
}

static void updateOled() {
  display.clearDisplay();
  uint8_t row = 0;
  habOledRow(display, row++);
  display.print(F("PAYLOAD"));
  habOledRow(display, row++);
  display.printf("SF%d %ddBm", LORA_SF, LORA_TX_POWER_DBM);
  habOledRow(display, row++);
  display.printf("seq %u", g_telemSeq);
  habOledRow(display, row++);
  if (g_latest.flags & HAB_FLAG_GPS_OK) {
    display.printf("alt %.0fm", g_latest.alt_mm / 1000.0);
  } else {
    display.print(F("gps n/a"));
  }
  habOledRow(display, row++);
  if (g_latest.flags & HAB_FLAG_IMU_OK) {
    display.printf("tilt %.1f", g_latest.tilt_cdeg / 100.0);
  } else {
    display.print(F("imu n/a"));
  }
  habOledRow(display, row++);
  if (g_latest.flags & HAB_FLAG_MAG_OK) {
    display.printf("hdg %.0f", g_latest.heading_cdeg / 100.0);
  } else {
    display.print(F("mag n/a"));
  }
  display.display();
}

static bool transmitPacket(const HabPacket& pkt) {
  uint8_t buf[HAB_PKT_MAX_LEN];
  size_t n = habPacketEncode(pkt, buf, sizeof(buf));
  if (n == 0) {
    return false;
  }
  int state = radio.transmit(buf, n);
  armReceive();
  if (state != RADIOLIB_ERR_NONE) {
    Serial.printf("[payload] TX err %d\n", state);
  }
  return state == RADIOLIB_ERR_NONE;
}

static void sendTelem() {
  HabPacket pkt = {};
  pkt.type   = HAB_PKT_TELEM;
  pkt.seq    = ++g_telemSeq;
  pkt.sample = g_latest;
  // SD row with seq + same ts_ms as the over-the-air sample (join key vs ground).
  habSensorsLogSample(g_latest, g_gpsTime, (int32_t)pkt.seq);
  if (transmitPacket(pkt)) {
    Serial.printf("[payload] TELEM seq=%u flags=0x%02X\n", pkt.seq,
                  pkt.sample.flags);
  } else {
    Serial.println(F("[payload] TELEM TX failed"));
  }
  updateOled();
}

static void handlePing(uint16_t seq, float rssi, float snr) {
  // Fresh sample at receive time (ts_ms + gps_time go OTA in the ACK).
  habSensorsReadSample(g_latest, g_gpsTime, sizeof(g_gpsTime));

  HabPacket pkt = {};
  pkt.type   = HAB_PKT_ACK;
  pkt.seq    = seq;
  pkt.sample = g_latest;

  // Log uplink quality of the received PING (before TX clears radio stats).
  Serial.printf("[payload] PING seq=%u  RSSI=%.1f dBm  SNR=%.1f dB  gps=%s\n",
                seq, rssi, snr, g_gpsTime[0] ? g_gpsTime : "n/a");
  habSensorsLogPing(seq, rssi, snr, g_gpsTime);

  if (transmitPacket(pkt)) {
    Serial.printf("[payload] ACK seq=%u (reply to RSSI=%.1f SNR=%.1f)\n", seq,
                  rssi, snr);
  } else {
    Serial.println(F("[payload] ACK TX failed"));
  }
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
    float snr  = radio.getSNR();
    HabPacket pkt;
    if (habPacketDecode(buf, len, pkt) && pkt.type == HAB_PKT_PING) {
      handlePing(pkt.seq, rssi, snr);
      return;  // handlePing re-arms RX
    }
  }
  armReceive();
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println(F("\n=== HAB Payload ==="));
  habRadioPrintConfig(Serial);

  bool dispOk = habDisplayBegin(display);
  if (!dispOk) {
    Serial.println(F("[payload] WARN: OLED init failed"));
  }

  int st = habRadioBegin(radio);
  bool radioOk = (st == RADIOLIB_ERR_NONE);
  if (!radioOk) {
    Serial.printf("[payload] FATAL: radio init failed (%d)\n", st);
    if (dispOk) {
      habDisplaySplash(display, "PAYLOAD", false);
    }
    while (true) {
      delay(1000);
    }
  }
  Serial.println(F("[payload] Radio OK"));

  radio.setPacketReceivedAction(onRadioRx);

  if (dispOk) {
    habDisplaySplash(display, "PAYLOAD", true);
  }

  HabSensorStatus sens = habSensorsBegin();
  if (sens.sd_ok) {
    Serial.printf("[payload] Logging session %s\n", habSensorsSessionDir());
  }
  habSampleClear(g_latest);

  armReceive();
  g_lastSampleMs = millis();
  g_lastTelemMs  = millis();
  Serial.println(F("[payload] Running (1 Hz sample, 5 s TELEM)"));
}

void loop() {
  habSensorsPollGps();
  serviceRx();

  const uint32_t now = millis();

  if (now - g_lastSampleMs >= HAB_SAMPLE_INTERVAL_MS) {
    g_lastSampleMs = now;
    habSensorsReadSample(g_latest, g_gpsTime, sizeof(g_gpsTime));
    habSensorsLogSample(g_latest, g_gpsTime);
    updateOled();
  }

  if (now - g_lastTelemMs >= HAB_TELEM_INTERVAL_MS) {
    g_lastTelemMs = now;
    sendTelem();
  }
}
