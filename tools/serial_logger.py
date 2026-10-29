#!/usr/bin/env -S uv run
# /// script
# requires-python = ">=3.10"
# dependencies = [
#     "pyserial>=3.5",
# ]
# ///
"""Capture ground-node USB serial for a laptop bench test.

Echoes pretty text to the console, writes a session folder:
  - flight.csv   — TELEM rows (full OTA sample + downlink RSSI/SNR)
  - ping.csv     — PING ACK / timeout rows
  - ground.log   — full serial transcript

Usage:
  uv run tools/serial_logger.py [/dev/ttyUSB0] [logs]
"""

from __future__ import annotations

import argparse
import sys
from datetime import datetime, timezone
from pathlib import Path

try:
    import serial
    from serial.tools import list_ports
except ImportError:
    print("Install deps with uv:  uv run tools/serial_logger.py", file=sys.stderr)
    sys.exit(1)


FLIGHT_PREFIX = "FLIGHT,"
PING_PREFIX = "PING,"
REPO_ROOT = Path(__file__).resolve().parent.parent
DEFAULT_LOG_DIR = REPO_ROOT / "logs"

FLIGHT_HEADER = (
    "rx_utc,ts_ms,gps_time,seq,lat,lon,alt_m,tilt_deg,pitch_deg,roll_deg,"
    "heading_deg,sats,gps_ok,imu_ok,mag_ok,rssi_dbm,snr_db\n"
)
PING_HEADER = (
    "rx_utc,event,seq,rtt_ms,rssi_dbm,snr_db,ts_ms,gps_time,lat,lon,alt_m,"
    "tilt_deg,pitch_deg,roll_deg,heading_deg,sats,gps_ok,imu_ok,mag_ok\n"
)


def pick_port(preferred: str | None) -> str:
    if preferred:
        return preferred
    ports = list(list_ports.comports())
    if not ports:
        raise SystemExit("No serial ports found. Pass the device path explicitly.")
    for p in ports:
        desc = (p.description or "").lower()
        if "usb" in desc or "uart" in desc or "serial" in desc or "acm" in p.device.lower():
            return p.device
    return ports[0].device


def stamp_rx_utc(body: str) -> str:
    """Replace firmware rx_utc placeholder with host UTC; body is after prefix."""
    now = datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%S.%fZ")
    parts = body.split(",", 1)
    if len(parts) == 2:
        return f"{now},{parts[1]}"
    return f"{now},{body}"


def main() -> int:
    ap = argparse.ArgumentParser(description="HAB ground serial logger")
    ap.add_argument("port", nargs="?", help="Serial device (default: auto)")
    ap.add_argument(
        "log_dir",
        nargs="?",
        default=str(DEFAULT_LOG_DIR),
        help=f"Parent directory for session folders (default: {DEFAULT_LOG_DIR})",
    )
    ap.add_argument("-b", "--baud", type=int, default=115200)
    args = ap.parse_args()

    port = pick_port(args.port)
    parent = Path(args.log_dir).expanduser().resolve()
    parent.mkdir(parents=True, exist_ok=True)
    stamp = datetime.now(timezone.utc).strftime("%Y%m%d_%H%M%S")
    session = parent / stamp
    session.mkdir(parents=True, exist_ok=True)

    flight_path = session / "flight.csv"
    ping_path = session / "ping.csv"
    log_path = session / "ground.log"

    print(f"Opening {port} @ {args.baud}")
    print(f"Session → {session}")
    print(f"  flight.csv / ping.csv / ground.log")
    print("Ctrl+C to stop\n")

    n_flight = 0
    n_ping = 0
    with (
        serial.Serial(port, args.baud, timeout=1) as ser,
        flight_path.open("w", encoding="utf-8", newline="") as flight_f,
        ping_path.open("w", encoding="utf-8", newline="") as ping_f,
        log_path.open("w", encoding="utf-8") as log_f,
    ):
        flight_f.write(FLIGHT_HEADER)
        flight_f.flush()
        ping_f.write(PING_HEADER)
        ping_f.flush()
        log_f.write(f"# HAB ground log started {stamp}Z port={port}\n")
        log_f.flush()

        while True:
            try:
                raw = ser.readline()
            except KeyboardInterrupt:
                print("\nStopped.")
                break
            if not raw:
                continue
            try:
                line = raw.decode("utf-8", errors="replace").rstrip("\r\n")
            except Exception:
                continue

            log_f.write(line + "\n")
            log_f.flush()

            if line.startswith(FLIGHT_PREFIX):
                body = line[len(FLIGHT_PREFIX) :]
                if body.startswith("rx_utc,"):
                    print(line)
                    continue
                row = stamp_rx_utc(body)
                flight_f.write(row + "\n")
                flight_f.flush()
                n_flight += 1
                print(f"FLIGHT,{row}")
            elif line.startswith(PING_PREFIX):
                body = line[len(PING_PREFIX) :]
                if body.startswith("rx_utc,"):
                    print(line)
                    continue
                row = stamp_rx_utc(body)
                ping_f.write(row + "\n")
                ping_f.flush()
                n_ping += 1
                print(f"PING,{row}")
            else:
                print(line)

    print(f"Saved {n_flight} flight + {n_ping} ping row(s)")
    print(f"  session: {session}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
