# LoRa High-Altitude Balloon (HAB) Link-Budget Test

Two **Heltec WiFi LoRa 32 V3** firmwares managed using PlatformIO:

- **Payload** — 1 Hz SD log + 1 Hz LoRa telemetry (TELEM); acknowledges (ACKs) ground PINGs
- **Ground** — receives TELEM, logs to csv. Ping the payload via PRG button or serial `PING`.

Default physical layer settings (PHY): **915 MHz, SF10, 125 kHz, CR 4/5, 22 dBm**. To adjust radio parameters, edit `include/radio_config.h`, and **reflash both boards**.

## Quick start

```bash
pio run -e payload -t upload
pio run -e ground -t upload

# Run while ground node is plugged into laptop. Logs received data and opens a live Leaflet map at http://127.0.0.1:8765/ (use --no-map to disable)
uv run tools/serial_logger.py <port>
```

Or `pio device monitor -e ground -b 115200` and type `PING`.

## Radio knobs

In `radio_config.h`: `LORA_FREQUENCY_HZ`, `LORA_SF`, `LORA_BW_KHZ`, `LORA_CR`, `LORA_TX_POWER_DBM`  
(or presets `LORA_PRESET_SF7_22` / `SF10_22` / `SF12_14`).

## Wiring (payload ↔ modules)

Onboard LoRa / OLED are already connected on the Heltec. External wiring only (`[include/pins.h](include/pins.h)`):


| Module   | Module pin             | Heltec                                          |
| -------- | ---------------------- | ----------------------------------------------- |
| MPU6050  | VCC / GND              | 3V3 / GND                                       |
| MPU6050  | SDA / SCL              | **GPIO 41 / 42**                                |
| QMC5883P | VCC / GND              | 3V3 / GND                                       |
| QMC5883P | SDA / SCL              | **GPIO 41 / 42**                                |
| microSD  | VCC / GND              | 3V3 / GND                                       |
| microSD  | CS / SCK / MOSI / MISO | GPIO 4 / 7 / 6 / 5                              |
| Neo-6M   | VCC / GND              | 3V3 / GND                                       |
| Neo-6M   | TX / RX                | GPIO 47 / 48 (ESP RX ← GPS TX, ESP TX → GPS RX) |


External I2C is on the pin headers (41/42). Onboard OLED stays on 17/18 (not broken out).  
I2C addresses: MPU6050 `0x98`, QMC5883P `0x2C`, OLED `0x3C`. Ground node needs no external modules.

Missing IMU/mag/SD → warn and continue; only radio init is fatal.

## Libraries

Firmware (`[platformio.ini](platformio.ini)`):

- **RadioLib** — LoRa TX/RX
- **TinyGPSPlus** — Neo-6M NMEA
- **Adafruit SSD1306** (+ GFX) — OLED
- **Adafruit MPU6050** — IMU

Host: **pyserial** (USB logger via [uv](https://docs.astral.sh/uv/)); **Leaflet** (live map to visualize gps coordinates).

## Logging & matching

OTA `ver=3`: TELEM/ACK carry sample + `session_id`; PING is header-only. A new payload folder is created each flight (power cycle). A new `logs/` folder is created every time `tools/serial_logger.py` is run. 


| Where      | Folder                           | Files                                              |
| ---------- | -------------------------------- | -------------------------------------------------- |
| Payload SD | `/<utc>_<phy>_<session_id>/`     | `flight.csv`, `ping.csv`                           |
| Laptop     | `logs/<utc>_<phy>_<session_id>/` | `flight.csv`, `ping.csv`, `ground.log`, `meta.txt` |


Match runs by the shared `session_id` suffix (also a CSV column). Join rows on `session_id` + `seq`.

- Payload `flight.csv`: sensors (+ `seq` on TELEM TX)
- Payload `ping.csv`: uplink RSSI/SNR per received PING
- Ground `flight.csv`: TELEM + downlink RSSI/SNR + `gnd_lat/lon/alt`
- Ground `ping.csv`: `acked=1|0` (5s timeout ⇒ no ACK), Round-Trip Time (RTT), downlink RF

### Serial logger script & live map

Logs ground USB to a new `logs/` folder and (by default) opens a Leaflet map at `http://127.0.0.1:8765/`. Type `PING` + Enter in the logger terminal, or press **PRG** on the ground board, for a manual ping anytime.

```bash
uv run tools/serial_logger.py [port] [log_dir] [flags…]
```


| Parameter                                                          | Flag / arg            | Default       |
| ------------------------------------------------------------------ | --------------------- | ------------- |
| Serial device (e.g. `/dev/ttyUSB0`)                                | `port`                | auto USB/UART |
| Parent dir for session folders                                     | `log_dir`             | `logs/`       |
| Serial baud                                                        | `-b` / `--baud`       | `115200`      |
| Host ground WGS84 (pass both or neither)                           | `--lat` `--lon`       | unset         |
| Host ground altitude (m)                                           | `--alt`               | unset         |
| Disable live map                                                   | `--no-map`            | off           |
| Map HTTP port                                                      | `--map-port`          | `8765`        |
| Auto-send `PING` every `SEC` seconds (recommended for flight test) | `--ping-interval SEC` | off           |


Example: `uv run tools/serial_logger.py /dev/ttyUSB0 --lat 45.5048 --lon -73.5772 --alt 40 --ping-interval 10`

## Bench check

1. Same `radio_config.h` + antennas on both; flash both.
2. Logger (or serial) on ground → TELEM ~1 Hz with RSSI/SNR.
3. PRG / `PING` → ACK + RTT (or `acked=0`).
4. Payload SD folder grows; same `session_id` as laptop logs folder.

```
include/pins.h  radio_config.h
lib/hab_*   src/payload/  src/ground/  tools/serial_logger.py
```

