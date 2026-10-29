# LoRa HAB Link-Budget Test

Two **Heltec WiFi LoRa 32 V3** firmwares (PlatformIO):

- **Payload** — 1 Hz SD log + 1 Hz LoRa TELEM; ACKs ground PINGs
- **Ground** — receives TELEM, OLED status, USB CSV; PING via PRG or serial `PING`

Default PHY (edit [`include/radio_config.h`](include/radio_config.h), reflash **both**): **915 MHz, SF10, 125 kHz, CR 4/5, 22 dBm**.

## Quick start

```bash
pio run -e payload -t upload
pio run -e ground  -t upload

# Ground laptop logger ([uv](https://docs.astral.sh/uv/)); --lat/--lon = host ground position
uv run tools/serial_logger.py /dev/ttyUSB0 --lat 45.5048 --lon -73.5772 --alt 40
```

Or `pio device monitor -e ground -b 115200` and type `PING`.

## Radio knobs

In `radio_config.h`: `LORA_FREQUENCY_HZ`, `LORA_SF`, `LORA_BW_KHZ`, `LORA_CR`, `LORA_TX_POWER_DBM`  
(or presets `LORA_PRESET_SF7_22` / `SF10_22` / `SF12_14`).

## Pins (Heltec WiFi LoRa 32 V3)

Onboard LoRa / OLED use Heltec defaults (do not rewire). See [`include/pins.h`](include/pins.h).

| Function | Module pin | Heltec GPIO | Notes |
|----------|------------|-------------|-------|
| LoRa NSS / SCK / MOSI / MISO | — | 8 / 9 / 10 / 11 | Onboard SX1262 |
| LoRa RST / BUSY / DIO1 | — | 12 / 13 / 14 | |
| I2C SDA | MPU6050 SDA, QMC5883P SDA, OLED | **17** | Shared bus |
| I2C SCL | MPU6050 SCL, QMC5883P SCL, OLED | **18** | |
| OLED RST | — | 21 | Onboard |
| 3V3 / GND | VCC / GND | **3V3 / GND** | |
| SD CS / SCK / MOSI / MISO | CS / SCK / MOSI / MISO | **4 / 7 / 6 / 5** | Second SPI (not LoRa) |
| GPS TX / RX | Neo-6M TX / RX | **47 / 48** | ESP RX←TX, ESP TX→RX; 9600 |
| Ping | — | **0** (PRG) | Ground only |
| Vext (optional) | — | **36** | Active LOW |
| Battery | 1S LiPo | **BAT** | Or buck **5V** into 5V pin |

I2C: OLED `0x3C`, MPU6050 `0x68`, QMC5883P `0x2C`. Sensors on 3V3. Ground node needs no SD.

Missing IMU/mag/SD → warn and continue; only radio init is fatal.

## Logging & matching

OTA `ver=3`: TELEM/ACK carry sample + `session_id`; PING is header-only.

| Where | Folder | Files |
|-------|--------|-------|
| Payload SD | `/<utc>_<phy>_<session_id>/` | `flight.csv`, `ping.csv` |
| Laptop | `logs/<utc>_<phy>_<session_id>/` | `flight.csv`, `ping.csv`, `ground.log`, `meta.txt` |

Match runs by the shared **`session_id`** suffix (also a CSV column). Join rows on `session_id` + `seq`.

- Payload `flight.csv`: sensors (+ `seq` on TELEM TX)
- Payload `ping.csv`: uplink RSSI/SNR per received PING
- Ground `flight.csv`: TELEM + downlink RSSI/SNR + `gnd_lat/lon/alt`
- Ground `ping.csv`: `acked=1|0` (timeout ⇒ no ACK), RTT, downlink RF

## Bench check

1. Same `radio_config.h` + antennas on both; flash both.
2. Logger (or serial) on ground → TELEM ~1 Hz with RSSI/SNR.
3. PRG / `PING` → ACK + RTT (or `acked=0`).
4. Payload SD folder grows; same `session_id` as laptop logs folder.

```
include/pins.h  radio_config.h
lib/hab_*   src/payload/  src/ground/  tools/serial_logger.py
```
