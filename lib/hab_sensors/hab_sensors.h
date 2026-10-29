#pragma once

#include <Arduino.h>

#include "hab_packet.h"

struct HabSensorStatus {
  bool imu_ok;
  bool mag_ok;
  bool gps_ok;   // hardware/UART up (fix may still be false)
  bool sd_ok;
};

// Init I2C peripherals + GPS UART + SD. Soft-fails individually.
// Call after Wire/display begin (shares I2C). Enables Vext.
HabSensorStatus habSensorsBegin();

// Poll GPS UART (call frequently).
void habSensorsPollGps();

// Fill sample from current sensor state. Always sets ts_ms = millis().
// Flags reflect validity of fields written.
void habSensorsReadSample(HabSample& out, char* gpsTimeBuf, size_t gpsTimeCap);

const HabSensorStatus& habSensorsStatus();

// Session directory created on this boot, e.g. "/20261028_063015_…_a1b2c3d4", or "".
const char* habSensorsSessionDir();

// Payload-owned run id (also in OTA TELEM/ACK and folder name). Never 0.
uint32_t habSensorsSessionId();

// Append one CSV data row to <session>/flight.csv.
// Pass telem_seq >= 0 on TELEM TX so ground logs can join on seq (+ ts_ms).
bool habSensorsLogSample(const HabSample& s, const char* gps_time,
                         int32_t telem_seq = -1);

// Append a PING RX event to <session>/ping.csv (payload receive time + uplink RF).
bool habSensorsLogPing(uint16_t seq, float rssi_dbm, float snr_db,
                       const char* gps_time);
