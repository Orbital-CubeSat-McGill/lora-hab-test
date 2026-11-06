#include "hab_sensors.h"

#include <Adafruit_MPU6050.h>
#include <SD.h>
#include <SPI.h>
#include <TinyGPSPlus.h>
#include <Wire.h>
#include <esp_system.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pins.h"
#include "qmc5883p.h"
#include "radio_config.h"

static Adafruit_MPU6050 mpu;
static Qmc5883P mag;
static TinyGPSPlus gps;
static HardwareSerial GpsSerial(1);
// Must NOT use FSPI — that is Arduino's default SPI bus used by the SX1262.
static SPIClass SdSpi(HSPI);

static HabSensorStatus g_status = {};
static File g_logFile;
static File g_pingFile;
// e.g. "/20261028_063015_sf10_bw125_cr5_p22_915_a1b2c3d4"
static char g_sessionDir[72] = {};
static uint32_t g_sessionId = 0;

static float rad2deg(float r) { return r * 180.0f / PI; }

static void pollGpsBytes() {
  while (GpsSerial.available() > 0) {
    gps.encode(GpsSerial.read());
  }
}

// UBX binary frame → Neo-6M (needs Heltec TX → GPS RX).
static void gpsSendUbx(uint8_t cls, uint8_t id, const uint8_t* payload,
                       uint16_t len) {
  uint8_t ckA = 0;
  uint8_t ckB = 0;

  GpsSerial.write((uint8_t)0xB5);
  GpsSerial.write((uint8_t)0x62);

  const uint8_t hdr[] = {cls, id, (uint8_t)(len & 0xFF), (uint8_t)(len >> 8)};
  for (uint8_t b : hdr) {
    GpsSerial.write(b);
    ckA = (uint8_t)(ckA + b);
    ckB = (uint8_t)(ckB + ckA);
  }
  for (uint16_t i = 0; i < len; ++i) {
    GpsSerial.write(payload[i]);
    ckA = (uint8_t)(ckA + payload[i]);
    ckB = (uint8_t)(ckB + ckA);
  }
  GpsSerial.write(ckA);
  GpsSerial.write(ckB);
}

// CFG-NAV5: dynModel=6 (Airborne <1g). Re-sent every boot (no flash save).
static void gpsSetAirborneMode() {
  uint8_t nav5[36] = {};
  nav5[0] = 0x01;  // mask: apply dynModel only
  nav5[1] = 0x00;
  nav5[2] = 6;     // Airborne <1g
  gpsSendUbx(0x06, 0x24, nav5, sizeof(nav5));
  GpsSerial.flush();
  Serial.println(F("[sensors] GPS dynModel=Airborne<1g"));
}

// Wait briefly for a GPS wall-clock; HAB modules often get time before a fix.
static bool waitForGpsTimestamp(uint32_t timeout_ms) {
  const uint32_t start = millis();
  while (millis() - start < timeout_ms) {
    pollGpsBytes();
    if (gps.date.isValid() && gps.time.isValid() && gps.date.year() >= 2020) {
      return true;
    }
    delay(10);
  }
  return false;
}

// Next unused "/boot_N" by scanning root for existing boot_* folders.
static unsigned nextBootIndex() {
  unsigned maxIdx = 0;
  File root = SD.open("/");
  if (!root) {
    return 1;
  }
  for (;;) {
    File entry = root.openNextFile();
    if (!entry) {
      break;
    }
    if (entry.isDirectory()) {
      const char* name = entry.name();
      // FatFS may return "boot_3" or "/boot_3"
      if (name[0] == '/') {
        ++name;
      }
      if (strncmp(name, "boot_", 5) == 0) {
        char* end = nullptr;
        unsigned long n = strtoul(name + 5, &end, 10);
        // Accept "boot_3" or "boot_3_sf10_..." (PHY suffix).
        if (end != name + 5 && (*end == '\0' || *end == '_') && n > maxIdx) {
          maxIdx = (unsigned)n;
        }
      }
    }
    entry.close();
  }
  root.close();
  return maxIdx + 1;
}

// Build "/YYYYMMDD_HHMMSS_<phy>_<sid>" or "/boot_N_<phy>_<sid>".
// On name collision, append _2, _3, ...
static bool createSessionDirectory() {
  char base[20];
  char phy[40];
  habRadioPhySlug(phy, sizeof(phy));

  if (gps.date.isValid() && gps.time.isValid() && gps.date.year() >= 2020) {
    snprintf(base, sizeof(base), "%04u%02u%02u_%02u%02u%02u", gps.date.year(),
             gps.date.month(), gps.date.day(), gps.time.hour(),
             gps.time.minute(), gps.time.second());
  } else {
    snprintf(base, sizeof(base), "boot_%u", nextBootIndex());
  }

  for (int attempt = 0; attempt < 100; ++attempt) {
    if (attempt == 0) {
      snprintf(g_sessionDir, sizeof(g_sessionDir), "/%s_%s_%08lX", base, phy,
               (unsigned long)g_sessionId);
    } else {
      snprintf(g_sessionDir, sizeof(g_sessionDir), "/%s_%s_%08lX_%d", base, phy,
               (unsigned long)g_sessionId, attempt + 1);
    }
    if (SD.exists(g_sessionDir)) {
      continue;
    }
    if (SD.mkdir(g_sessionDir)) {
      return true;
    }
    Serial.printf("[sensors] WARN: mkdir %s failed\n", g_sessionDir);
    return false;
  }
  Serial.println(F("[sensors] WARN: could not allocate unique session dir"));
  return false;
}

static bool openSessionLogs() {
  char path[80];

  snprintf(path, sizeof(path), "%s/flight.csv", g_sessionDir);
  g_logFile = SD.open(path, FILE_WRITE);
  if (!g_logFile) {
    Serial.printf("[sensors] WARN: cannot open %s\n", path);
    return false;
  }
  habCsvHeaderPayload(g_logFile);
  g_logFile.flush();

  snprintf(path, sizeof(path), "%s/ping.csv", g_sessionDir);
  g_pingFile = SD.open(path, FILE_WRITE);
  if (!g_pingFile) {
    Serial.printf("[sensors] WARN: cannot open %s\n", path);
    g_logFile.close();
    return false;
  }
  g_pingFile.println(F("ts_ms,gps_time,seq,rssi_dbm,snr_db"));
  g_pingFile.flush();
  return true;
}

HabSensorStatus habSensorsBegin() {
  g_status = {};

  // Shared with ground logs via OTA + folder suffix. Never 0 (0 = unknown).
  do {
    g_sessionId = esp_random();
  } while (g_sessionId == 0);

  // Power external rails via Vext (active LOW) if used
  pinMode(PIN_VEXT, OUTPUT);
  digitalWrite(PIN_VEXT, LOW);
  delay(50);

  // External sensors on header I2C (not the OLED's 17/18 bus).
  Wire1.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  Wire1.setClock(100000);
  Serial.printf("[sensors] Ext I2C Wire1 SDA=%d SCL=%d\n", PIN_I2C_SDA,
                PIN_I2C_SCL);

  // Scan so a wiring mistake is obvious on the serial console.
  Serial.print(F("[sensors] Wire1 scan:"));
  int found = 0;
  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire1.beginTransmission(addr);
    if (Wire1.endTransmission() == 0) {
      Serial.printf(" 0x%02X", addr);
      found++;
    }
  }
  if (found == 0) {
    Serial.print(F(" (none)"));
  }
  Serial.println();

  // --- IMU WHO_AM_I (reg 0x75): genuine 0x68, common clones 0x98 ---
  auto readWhoAmI = [](uint8_t addr) -> int {
    Wire1.beginTransmission(addr);
    Wire1.write(0x75);
    if (Wire1.endTransmission(false) != 0) {
      return -1;
    }
    if (Wire1.requestFrom((int)addr, 1) != 1) {
      return -2;
    }
    return (int)Wire1.read();
  };
  const int who68 = readWhoAmI(0x68);
  const int who69 = readWhoAmI(0x69);
  if (who68 >= 0) {
    Serial.printf("[sensors] MPU WHO_AM_I @0x68 = 0x%02X\n", who68);
  } else {
    Serial.printf("[sensors] MPU WHO_AM_I @0x68 fail (%d)\n", who68);
  }
  if (who69 >= 0) {
    Serial.printf("[sensors] MPU WHO_AM_I @0x69 = 0x%02X\n", who69);
  } else {
    Serial.printf("[sensors] MPU WHO_AM_I @0x69 fail (%d)\n", who69);
  }

  // --- IMU (AD0 low → 0x68, AD0 high → 0x69) ---
  bool imu = mpu.begin(I2C_ADDR_MPU6050, &Wire1);
  if (!imu) {
    imu = mpu.begin(0x69, &Wire1);
    if (imu) {
      Serial.println(F("[sensors] MPU6050 at 0x69 (AD0 high)"));
    }
  }
  if (imu) {
    // Clones (WHO_AM_I=0x98) often remain in sleep after Adafruit begin/reset
    // (PWR_MGMT_1 bit6). Accel then reads 0 → OLED "imu n/a".
    mpu.enableSleep(false);
    mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
    mpu.setGyroRange(MPU6050_RANGE_500_DEG);
    mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);

    // Wake again after range writes; verify PWR_MGMT_1 sleep bit cleared.
    for (int i = 0; i < 3; i++) {
      Wire1.beginTransmission(0x68);
      Wire1.write(0x6B);
      Wire1.write(0x00);  // clear sleep
      Wire1.endTransmission();
      delay(10);
      Wire1.beginTransmission(0x68);
      Wire1.write(0x6B);
      Wire1.endTransmission(false);
      Wire1.requestFrom(0x68, 1);
      const int pwr = Wire1.available() ? Wire1.read() : -1;
      Serial.printf("[sensors] MPU PWR_MGMT_1 after wake=%d -> 0x%02X\n", i,
                    pwr < 0 ? 0xff : pwr);
      if (pwr >= 0 && (pwr & 0x40) == 0) {
        break;
      }
    }
    delay(50);

    g_status.imu_ok = true;
    Serial.println(F("[sensors] MPU6050 OK"));
  } else {
    Serial.println(
        F("[sensors] WARN: MPU6050 missing — check SDA=41 SCL=42 GND; "
          "swap SDA/SCL if scan empty; WHO_AM_I must be 0x68 or 0x98"));
  }

  // --- Magnetometer (optional) ---
  if (mag.begin(Wire1, I2C_ADDR_QMC5883P)) {
    g_status.mag_ok = true;
    Serial.println(F("[sensors] QMC5883P OK"));
  } else {
    Serial.println(F("[sensors] WARN: QMC5883P missing — heading disabled"));
  }

  // --- GPS UART ---
  GpsSerial.begin(9600, SERIAL_8N1, PIN_GPS_RX, PIN_GPS_TX);
  g_status.gps_ok = true;  // UART opened; fix_ok tracked per sample
  Serial.println(F("[sensors] GPS UART started (9600)"));
  gpsSetAirborneMode();

  // --- SD (second SPI): new timestamped folder each boot ---
  SdSpi.begin(PIN_SD_SCK, PIN_SD_MISO, PIN_SD_MOSI, PIN_SD_CS);
  if (SD.begin(PIN_SD_CS, SdSpi)) {
    // Prefer GPS wall-clock for the folder name (time often arrives before fix).
    if (!waitForGpsTimestamp(2000)) {
      Serial.println(
          F("[sensors] GPS time not ready — using next boot_N session id"));
    }
    if (createSessionDirectory() && openSessionLogs()) {
      g_status.sd_ok = true;
      Serial.printf("[sensors] SD OK -> %s/flight.csv, %s/ping.csv\n",
                    g_sessionDir, g_sessionDir);
    } else {
      Serial.println(F("[sensors] WARN: SD mount OK but session logs failed"));
    }
  } else {
    Serial.println(F("[sensors] WARN: SD missing — onboard log disabled"));
  }

  return g_status;
}

void habSensorsPollGps() { pollGpsBytes(); }

const char* habSensorsSessionDir() { return g_sessionDir; }

uint32_t habSensorsSessionId() { return g_sessionId; }

const HabSensorStatus& habSensorsStatus() { return g_status; }

void habSensorsReadSample(HabSample& out, char* gpsTimeBuf, size_t gpsTimeCap) {
  habSampleClear(out);
  out.ts_ms = millis();
  if (gpsTimeBuf && gpsTimeCap) {
    gpsTimeBuf[0] = '\0';
  }

  // IMU → tilt from vertical, pitch, roll
  if (g_status.imu_ok) {
    sensors_event_t a, g, temp;
    if (mpu.getEvent(&a, &g, &temp)) {
      const float ax = a.acceleration.x;
      const float ay = a.acceleration.y;
      const float az = a.acceleration.z;
      const float norm = sqrtf(ax * ax + ay * ay + az * az);
      static uint8_t imu_dbg = 0;
      if (imu_dbg < 5) {
        imu_dbg++;
        Wire1.beginTransmission(0x68);
        Wire1.write(0x6B);
        Wire1.endTransmission(false);
        Wire1.requestFrom(0x68, 1);
        const int pwr = Wire1.available() ? Wire1.read() : -1;
        Wire1.beginTransmission(0x68);
        Wire1.write(0x3B);
        Wire1.endTransmission(false);
        Wire1.requestFrom(0x68, 6);
        int16_t rx = 0, ry = 0, rz = 0;
        if (Wire1.available() >= 6) {
          rx = (int16_t)((Wire1.read() << 8) | Wire1.read());
          ry = (int16_t)((Wire1.read() << 8) | Wire1.read());
          rz = (int16_t)((Wire1.read() << 8) | Wire1.read());
        }
        Serial.printf(
            "[sensors] IMU sample ax=%.3f ay=%.3f az=%.3f norm=%.3f "
            "PWR=0x%02X raw=%d,%d,%d\n",
            ax, ay, az, norm, pwr < 0 ? 0xff : pwr, (int)rx, (int)ry,
            (int)rz);
      }
      if (norm > 0.1f) {
        float tilt = rad2deg(acosf(constrain(fabsf(az) / norm, -1.0f, 1.0f)));
        float pitch = rad2deg(atan2f(-ax, sqrtf(ay * ay + az * az)));
        float roll  = rad2deg(atan2f(ay, az));
        out.tilt_cdeg  = (int16_t)lroundf(tilt * 100.0f);
        out.pitch_cdeg = (int16_t)lroundf(pitch * 100.0f);
        out.roll_cdeg  = (int16_t)lroundf(roll * 100.0f);
        out.flags |= HAB_FLAG_IMU_OK;
      }
    }
  }

  // Magnetometer heading
  if (g_status.mag_ok) {
    float heading = 0;
    if (mag.readHeadingDeg(heading)) {
      out.heading_cdeg = (uint16_t)lroundf(heading * 100.0f);
      out.flags |= HAB_FLAG_MAG_OK;
    }
  }

  // GPS
  out.sats = (uint8_t)gps.satellites.value();
  if (gps.location.isValid()) {
    out.lat_e7 = (int32_t)lround(gps.location.lat() * 1e7);
    out.lon_e7 = (int32_t)lround(gps.location.lng() * 1e7);
    if (gps.altitude.isValid()) {
      out.alt_mm = (int32_t)lround(gps.altitude.meters() * 1000.0);
    }
    out.flags |= HAB_FLAG_GPS_OK;
  }
  if (gps.time.isValid()) {
    habSampleSetGpsTime(out, (uint8_t)gps.time.hour(),
                        (uint8_t)gps.time.minute(),
                        (uint8_t)gps.time.second());
    if (gpsTimeBuf && gpsTimeCap) {
      habSampleFormatGpsTime(out, gpsTimeBuf, gpsTimeCap);
    }
  }
}

bool habSensorsLogSample(const HabSample& s, const char* gps_time,
                         int32_t telem_seq) {
  if (!g_status.sd_ok || !g_logFile) {
    return false;
  }
  habCsvLinePayload(g_logFile, g_sessionId, s, gps_time, telem_seq);
  g_logFile.flush();
  return true;
}

bool habSensorsLogPing(uint16_t seq, float rssi_dbm, float snr_db,
                       const char* gps_time) {
  if (!g_status.sd_ok || !g_pingFile) {
    return false;
  }
  g_pingFile.printf("%lu,", (unsigned long)millis());
  g_pingFile.print(gps_time ? gps_time : "");
  g_pingFile.printf(",%u,%.1f,%.1f\n", seq, rssi_dbm, snr_db);
  g_pingFile.flush();
  return true;
}
