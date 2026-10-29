# LoRa HAB Link-Budget Test

Dual-firmware PlatformIO project for two **Heltec WiFi LoRa 32 V3** nodes:

- **Payload** — logs IMU tilt, magnetometer heading, and GPS to microSD at 1 Hz; broadcasts telemetry every 1 s over LoRa; answers ground PINGs with ACK.
- **Ground** — receives telemetry, shows last packet on the OLED, pretty-prints + CSV over USB; can PING the payload (PRG button or serial `PING`).

Starting PHY (edit before reflashing both boards): **915.0 MHz, SF10, 125 kHz, CR 4/5, 22 dBm**.

## Quick start

```bash
# Build
pio run -e payload
pio run -e ground

# Flash (select port if needed: --upload-port /dev/ttyUSB0)
pio run -e payload -t upload
pio run -e ground  -t upload

# Serial monitor
pio device monitor -e ground -b 115200
```

Laptop CSV logger (ground USB; needs [uv](https://docs.astral.sh/uv/)):

```bash
uv run tools/serial_logger.py /dev/ttyUSB0 logs
# → logs/<utc>/{flight.csv,ping.csv,ground.log}
```

On the ground serial console, type `PING` + Enter, or press the **PRG** button.

## Changing LoRa parameters

Edit [`include/radio_config.h`](include/radio_config.h) — the knobs at the top:

| Define | Default | Meaning |
|--------|---------|---------|
| `LORA_FREQUENCY_HZ` | `915000000` | Center frequency |
| `LORA_SF` | `10` | Spreading factor 7–12 |
| `LORA_BW_KHZ` | `125.0` | Bandwidth kHz |
| `LORA_CR` | `5` | Coding rate 4/5 |
| `LORA_TX_POWER_DBM` | `22` | TX power dBm |

Or uncomment a preset (`LORA_PRESET_SF7_22`, `LORA_PRESET_SF10_22`, `LORA_PRESET_SF12_14`).

**Reflash both** `payload` and `ground` with the same config.

## Pin connections (Heltec WiFi LoRa 32 V3)

Onboard LoRa / OLED use Heltec defaults (do not rewire).

| Function | Module pin | Heltec GPIO / net | Notes |
|----------|------------|-------------------|-------|
| I2C SDA | MPU6050 SDA, QMC5883P SDA | **GPIO 17** | Shared with OLED |
| I2C SCL | MPU6050 SCL, QMC5883P SCL | **GPIO 18** | |
| 3V3 | VCC | **3V3** | |
| GND | GND | **GND** | |
| SD CS | CS | **GPIO 4** | |
| SD SCK | SCK | **GPIO 7** | Second SPI (not LoRa) |
| SD MOSI | MOSI | **GPIO 6** | |
| SD MISO | MISO | **GPIO 5** | |
| GPS TX | Neo-6M TX | **GPIO 47** (ESP RX) | 9600 baud |
| GPS RX | Neo-6M RX | **GPIO 48** (ESP TX) | |
| Ping | — | **GPIO 0** (PRG) | Ground only |
| Vext (optional) | — | **GPIO 36** (active LOW) | External power switch |
| Battery | 1S LiPo | **BAT** | Or buck **5V** into 5V pin |

I2C addresses: OLED `0x3C`, MPU6050 `0x68`, QMC5883P `0x2C`.

**Power:** 1S 3.7 V → BAT, or 3S → buck to 5 V → Heltec 5V. Sensors on 3V3. Do not feed 11.1 V into the Heltec.

Ground node needs **no SD** — log on the laptop over USB.

## Soft-fail sensors

Missing **magnetometer**, **IMU**, or **SD** prints a one-time warning and continues. Telemetry flags mark fields as `n/a`. Only **radio** init failure is fatal. You can bench without the QMC5883P.

## Data formats

OTA packet `ver=2`: TELEM/ACK carry the full sample (incl. GPS time `HH:MM:SS`). PING is header-only.

**Payload SD** — `/YYYYMMDD_HHMMSS/` (or `/boot_N/`):

- `flight.csv` — `ts_ms,gps_time,seq,lat,lon,alt_m,tilt_deg,pitch_deg,roll_deg,heading_deg,sats,gps_ok,imu_ok,mag_ok`  
  (`seq` blank on 1 Hz rows; set on TELEM TX — join ground `flight.csv` on `seq` / `ts_ms`)
- `ping.csv` — `ts_ms,gps_time,seq,rssi_dbm,snr_db` (uplink RF at PING receive)

**Ground** (`serial_logger.py` → `logs/<utc>/`):

- `flight.csv` — TELEM: same sample fields + `rx_utc,rssi_dbm,snr_db`
- `ping.csv` — `rx_utc,event,seq,rtt_ms,rssi_dbm,snr_db` + ACK sample fields  
  (`event`=`ack`|`timeout`; timeout blanks sample, `rtt_ms=-<timeout_ms>`)

## End-to-end bench checklist

1. Same `radio_config.h` on both boards; antennas attached.
2. Flash payload + ground; open ground serial (or `serial_logger.py`).
3. Confirm TELEM every ~1 s with RSSI/SNR.
4. Send `PING` / press PRG → ACK with RTT (or TIMEOUT if payload off).
5. Confirm payload `<session>/flight.csv` grows at ~1 Hz when SD present (new folder each reset).
6. Unplug magnetometer, reboot payload → warning, no hang, heading `n/a`.

## Repo layout

```
include/pins.h radio_config.h
lib/hab_radio/ hab_packet/ hab_display/ hab_sensors/
src/payload/main.cpp
src/ground/main.cpp
tools/serial_logger.py
```

## Out of scope (for now)

- Live Leaflet map (CSV is ready for a later host UI)
- Runtime SF/power changes from ground
- LoRaWAN
