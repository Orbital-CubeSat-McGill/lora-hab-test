#pragma once

#include <Arduino.h>
#include <stdint.h>

static const uint8_t HAB_PKT_MAGIC   = 0xA5;
// v3: header adds session_id (payload-owned run id for folder matching).
static const uint8_t HAB_PKT_VERSION = 3;

enum HabPktType : uint8_t {
  HAB_PKT_TELEM = 1,
  HAB_PKT_PING  = 2,
  HAB_PKT_ACK   = 3,
  // Log-only (never on the air): ground ping CSV when PING gets no ACK.
  HAB_EVT_PING_TIMEOUT = 4,
};

enum HabFlags : uint8_t {
  HAB_FLAG_GPS_OK = 1 << 0,
  HAB_FLAG_IMU_OK = 1 << 1,
  HAB_FLAG_MAG_OK = 1 << 2,
};

// Packed sample carried in TELEM and ACK bodies.
struct HabSample {
  uint32_t ts_ms;       // payload millis at sample / ping-receive
  int32_t  lat_e7;      // degrees * 1e7
  int32_t  lon_e7;
  int32_t  alt_mm;      // altitude millimetres
  int16_t  tilt_cdeg;   // centi-degrees (0.01°)
  int16_t  pitch_cdeg;
  int16_t  roll_cdeg;
  int16_t  heading_cdeg;
  uint8_t  sats;
  uint8_t  flags;       // HabFlags
  uint8_t  gps_h;       // 0–23, or 0xFF if GPS time invalid
  uint8_t  gps_m;
  uint8_t  gps_s;
};

struct HabPacket {
  uint8_t   type;
  uint16_t  seq;
  uint32_t  session_id;  // payload run id; 0 on ground→payload PING
  HabSample sample;      // meaningful for TELEM/ACK; zeroed for PING
};

static const size_t HAB_PKT_HEADER_LEN =
    1 + 1 + 1 + 2 + 4;  // magic,ver,type,seq,session_id
static const size_t HAB_SAMPLE_LEN =
    4 + 4 + 4 + 4 + 2 + 2 + 2 + 2 + 1 + 1 + 1 + 1 + 1;  // 29
static const size_t HAB_PKT_TELEM_LEN = HAB_PKT_HEADER_LEN + HAB_SAMPLE_LEN;
static const size_t HAB_PKT_PING_LEN  = HAB_PKT_HEADER_LEN;
static const size_t HAB_PKT_MAX_LEN   = HAB_PKT_TELEM_LEN;

size_t habPacketEncode(const HabPacket& pkt, uint8_t* out, size_t outCap);
bool   habPacketDecode(const uint8_t* data, size_t len, HabPacket& out);

void habSampleClear(HabSample& s);
void habSampleSetGpsTime(HabSample& s, uint8_t h, uint8_t m, uint8_t sec);
void habSampleClearGpsTime(HabSample& s);
bool habSampleGpsTimeValid(const HabSample& s);
// Writes "HH:MM:SS" or "" if invalid.
void habSampleFormatGpsTime(const HabSample& s, char* buf, size_t cap);

// USB / SD formatters
void habPrettyPrintTelem(Print& out, const HabPacket& pkt, float rssi, float snr);
void habPrettyPrintAck(Print& out, const HabPacket& pkt, float rssi, float snr,
                       int32_t rtt_ms);
void habPrettyPrintPingTimeout(Print& out, uint16_t seq, uint32_t waited_ms);

// Payload SD flight.csv (no RX RF columns).
void habCsvHeaderPayload(Print& out);
// seq < 0 → leave seq column blank (1 Hz samples). TELEM rows pass the TX seq.
void habCsvLinePayload(Print& out, uint32_t session_id, const HabSample& s,
                       const char* gps_time, int32_t seq = -1);

// Ground → host: FLIGHT,<rx_utc>,...  (TELEM only; full OTA sample + RSSI/SNR)
void habCsvHeaderGroundFlight(Print& out);
void habCsvLineGroundFlight(Print& out, const HabPacket& pkt, float rssi,
                            float snr, const char* rx_utc);

// Ground → host: PING,<rx_utc>,...  (ACK or timeout)
void habCsvHeaderGroundPing(Print& out);
void habCsvLineGroundPingAck(Print& out, const HabPacket& pkt, float rssi,
                             float snr, const char* rx_utc, int32_t rtt_ms);
void habCsvLineGroundPingTimeout(Print& out, uint16_t seq, uint32_t session_id,
                                 const char* rx_utc, uint32_t waited_ms);
