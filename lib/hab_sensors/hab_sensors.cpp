#include "hab_sensors.h"

#include <Adafruit_MPU6050.h>
#include <SD.h>
#include <SPI.h>
#include <TinyGPSPlus.h>
#include <Wire.h>
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
// e.g. "/20261028_063015_sf10_bw125_cr5_p22_915" — one dir per boot/reset
static char g_sessionDir[64] = {};

static float rad2deg(float r) { return r * 180.0f / PI; }

static void pollGpsBytes() {
  while (GpsSerial.available() > 0) {
    gps.encode(GpsSerial.read());
  }
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

// Build "/YYYYMMDD_HHMMSS_<phy>" or "/boot_N_<phy>" if GPS time unavailable.
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
      snprintf(g_sessionDir, sizeof(g_sessionDir), "/%s_%s", base, phy);
    } else {
      snprintf(g_sessionDir, sizeof(g_sessionDir), "/%s_%s_%d", base, phy,
               attempt + 1);
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

  // Power external rails via Vext (active LOW) if used
  pinMode(PIN_VEXT, OUTPUT);
  digitalWrite(PIN_VEXT, LOW);
  delay(50);

  // --- IMU ---
  if (mpu.begin(I2C_ADDR_MPU6050, &Wire)) {
    mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
    mpu.setGyroRange(MPU6050_RANGE_500_DEG);
    mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);
    g_status.imu_ok = true;
    Serial.println(F("[sensors] MPU6050 OK"));
  } else {
    Serial.println(F("[sensors] WARN: MPU6050 missing — tilt disabled"));
  }

  // --- Magnetometer (optional) ---
  if (mag.begin(Wire, I2C_ADDR_QMC5883P)) {
    g_status.mag_ok = true;
    Serial.println(F("[sensors] QMC5883P OK"));
  } else {
    Serial.println(F("[sensors] WARN: QMC5883P missing — heading disabled"));
  }

  // --- GPS UART ---
  GpsSerial.begin(9600, SERIAL_8N1, PIN_GPS_RX, PIN_GPS_TX);
  g_status.gps_ok = true;  // UART opened; fix_ok tracked per sample
  Serial.println(F("[sensors] GPS UART started (9600)"));

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
      out.heading_cdeg = (int16_t)lroundf(heading * 100.0f);
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
  habCsvLinePayload(g_logFile, s, gps_time, telem_seq);
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
