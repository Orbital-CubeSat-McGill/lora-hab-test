# LoRa HAB Link-Budget Test

Two **Heltec WiFi LoRa 32 V3** firmwares (PlatformIO):

- **Payload** — 1 Hz SD log + 1 Hz LoRa TELEM (TX / standby only; no RX)
- **Ground** — receives TELEM, OLED status, USB CSV

Default PHY (edit [`include/radio_config.h`](include/radio_config.h), reflash **both**): **925 MHz, SF10, 125 kHz, CR 4/5, 22 dBm**.

## Quick start

```bash
pio run -e payload -t upload
pio run -e ground  -t upload

# Ground laptop logger ([uv](https://docs.astral.sh/uv/)); --lat/--lon = host ground position
uv run tools/serial_logger.py /dev/ttyUSB0 --lat 45.5048 --lon -73.5772 --alt 40
```

Or `pio device monitor -e ground -b 115200`.

## Radio knobs

In `radio_config.h`: `LORA_FREQUENCY_HZ`, `LORA_SF`, `LORA_BW_KHZ`, `LORA_CR`, `LORA_TX_POWER_DBM`  
(or presets `LORA_PRESET_SF7_22` / `SF10_22` / `SF12_14`).

## Wiring (payload ↔ modules)

Onboard LoRa / OLED are already connected. External wiring only ([`include/pins.h`](include/pins.h)):

| Module | Module pin | Heltec |
|--------|------------|--------|
| MPU6050 | VCC / GND | 3V3 / GND |
| MPU6050 | SDA / SCL | **GPIO 41 / 42** |
| QMC5883P | VCC / GND | 3V3 / GND |
| QMC5883P | SDA / SCL | **GPIO 41 / 42** |
| microSD | VCC / GND | 3V3 / GND |
| microSD | CS / SCK / MOSI / MISO | GPIO 4 / 7 / 6 / 5 |
| Neo-6M | VCC / GND | 3V3 / GND |
| Neo-6M | TX / RX | GPIO 47 / 48 (ESP RX ← GPS TX, ESP TX → GPS RX) |

External I2C is on the pin headers (41/42). Onboard OLED stays on 17/18 (not broken out).  
I2C addresses: MPU6050 `0x68`, QMC5883P `0x2C`, OLED `0x3C`. Ground node needs no external modules.

Missing IMU/mag/SD → warn and continue; only radio init is fatal.

## Logging & matching

OTA `ver=4`: every packet header carries callsign `VA2ZMC`; TELEM also carries `seq` + `session_id` + sample (uplink only).

| Where | Folder | Files |
|-------|--------|-------|
| Payload SD | `/<utc>_<phy>_<session_id>/` | `flight.csv` |
| Laptop | `logs/<utc>_<phy>_<session_id>/` | `flight.csv`, `ground.log`, `meta.txt` |

Match runs by the shared **`session_id`** suffix (also a CSV column). Join rows on `session_id` + `seq`.

- Payload `flight.csv`: sensors (+ `seq` on TELEM TX)
- Ground `flight.csv`: TELEM + `callsign` + downlink RSSI/SNR + `gnd_lat/lon/alt`

## Bench check

1. Same `radio_config.h` + antennas on both; flash both.
2. Logger (or serial) on ground → TELEM ~1 Hz with RSSI/SNR.
3. Payload SD folder grows; same `session_id` as laptop logs folder.

```
include/pins.h  radio_config.h
lib/hab_*   src/payload/  src/ground/  tools/serial_logger.py
```
