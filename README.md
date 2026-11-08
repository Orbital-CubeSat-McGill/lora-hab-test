# LoRa High-Altitude Balloon (HAB) Link-Budget Test

Two **Heltec WiFi LoRa 32 V3** firmwares managed using PlatformIO:

- **Payload** — 1 Hz SD log + 1 Hz LoRa telemetry (TELEM); TX / standby only (no RX)
- **Ground** — receives TELEM, logs to csv

Default physical layer settings (PHY): **925 MHz, SF10, 125 kHz, CR 4/5, 14 dBm**. To adjust radio parameters, edit `include/radio_config.h`, and **reflash both boards**.

## Quick start

```bash
pio run -e payload -t upload
pio run -e ground -t upload

# Run while ground node is plugged into laptop. Logs received data.
uv run tools/serial_logger.py <port>
```

Or `pio device monitor -e ground -b 115200`, but this will not log.

## Radio knobs

In `radio_config.h`: `LORA_FREQUENCY_HZ`, `LORA_SF`, `LORA_BW_KHZ`, `LORA_CR`, `LORA_TX_POWER_DBM.` Or select a preset by uncommenting it (eg `LORA_PRESET_SF10_22`)

## Wiring for external modules

Onboard LoRa / OLED are already connected on the Heltec. External wiring only, defined in [include/pins.h.](include/pins.h) Reference [Heltec V3 pinout](media/heltecv3-pinout.png)for pinout.


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


External I2C is on the pin headers (41/42). Onboard OLED stays on 17/18, which are not broken out.  
I2C addresses: MPU6050 `0x68`, QMC5883P `0x2C`, OLED `0x3C`. Ground node needs no external modules.

Missing IMU/magnetometer/SD/GPS modules are handled at run-time. A warning will be displayed and csv fields will be empty. Only radio initialization is fatal. Always have an antenna connected before transmitting.

## Libraries

Firmware (`[platformio.ini](platformio.ini)`):

- **RadioLib** — LoRa TX/RX
- **TinyGPSPlus** — Neo-6M NMEA
- **Adafruit SSD1306** (+ GFX) — OLED
- **Adafruit MPU6050** — IMU



## Logging & matching

OTA `ver=4`: every packet header carries callsign `VA2ZMC`; TELEM also carries `seq` + `session_id` + sample. A new payload folder is created each flight (power cycle). A new `logs/` folder is created every time `tools/serial_logger.py` is run.


| Where      | Folder                           | Files                                |
| ---------- | -------------------------------- | ------------------------------------ |
| Payload SD | `/<utc>_<phy>_<session_id>/`     | `flight.csv`                         |
| Laptop     | `logs/<utc>_<phy>_<session_id>/` | `flight.csv`, `ground.log`, `meta.txt` |


Match runs by the shared `session_id` suffix (also a CSV column). Join rows on `session_id` + `seq`.

- Payload `flight.csv`: sensors (+ `seq` on TELEM TX)
- Ground `flight.csv`: TELEM + `callsign` + downlink RSSI/SNR + `gnd_lat/lon/alt`



### Serial logger script

Logs ground USB to a new `logs/` folder. Pass host ground position with `--lat` / `--lon` / `--alt` when known.

```bash
uv run tools/serial_logger.py [port] [log_dir] [flags…]
```


| Parameter                           | Flag / arg  | Default       |
| ----------------------------------- | ----------- | ------------- |
| Serial device (e.g. `/dev/ttyUSB0`) | `port`      | auto USB/UART |
| Parent dir for session folders      | `log_dir`   | `logs/`       |
| Serial baud                         | `-b` / `--baud` | `115200`  |
| Host ground WGS84 (pass both or neither) | `--lat` `--lon` | unset |
| Host ground altitude (m)            | `--alt`     | unset         |


Example: `uv run tools/serial_logger.py /dev/ttyUSB0 --lat 45.5048 --lon -73.5772 --alt 40`
