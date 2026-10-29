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

Onboard LoRa/OLED: Heltec defaults. Sensors:

| Function | GPIO |
|----------|------|
| I2C SDA / SCL (MPU6050, QMC5883P, OLED) | 17 / 18 |
| SD CS / SCK / MOSI / MISO (HSPI) | 4 / 7 / 6 / 5 |
| GPS TX→ESP RX / GPS RX←ESP TX | 47 / 48 (9600) |
| PRG (ground PING) | 0 |
| Vext (optional, active LOW) | 36 |

I2C: OLED `0x3C`, MPU6050 `0x68`, QMC5883P `0x2C`.  
Power: 1S → BAT, or buck to 5V pin. Sensors on 3V3. Ground node needs no SD.

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
