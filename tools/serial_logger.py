#!/usr/bin/env -S uv run
# /// script
# requires-python = ">=3.10"
# dependencies = [
#     "pyserial>=3.5",
# ]
# ///
"""Capture ground-node USB serial for a laptop bench / range test.

Echoes pretty text to the console, writes a session folder:
  - flight.csv   — TELEM rows (full OTA sample + downlink RSSI/SNR + optional ground GPS)
  - ping.csv     — PING ACK / no-ACK rows (acked=1|0)
  - ground.log   — full serial transcript
  - meta.txt     — PHY + ground GPS used for this session

Session folder name includes the PHY slug from the board boot line, e.g.
  logs/20261029_231500_sf10_bw125_cr5_p22_915/

Ground GPS comes from the host (this script), not the Heltec — pass --lat/--lon
(and optional --alt) so range can be computed vs payload lat/lon.

Usage:
  uv run tools/serial_logger.py /dev/ttyUSB0
  uv run tools/serial_logger.py /dev/ttyUSB0 --lat 45.5048 --lon -73.5772 --alt 40
"""

from __future__ import annotations

import argparse
import sys
import time
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
PHY_PREFIX = "PHY,"
REPO_ROOT = Path(__file__).resolve().parent.parent
DEFAULT_LOG_DIR = REPO_ROOT / "logs"

FLIGHT_HEADER = (
    "rx_utc,ts_ms,gps_time,seq,lat,lon,alt_m,tilt_deg,pitch_deg,roll_deg,"
    "heading_deg,sats,gps_ok,imu_ok,mag_ok,rssi_dbm,snr_db,"
    "gnd_lat,gnd_lon,gnd_alt_m\n"
)
PING_HEADER = (
    "rx_utc,event,acked,seq,rtt_ms,rssi_dbm,snr_db,ts_ms,gps_time,lat,lon,alt_m,"
    "tilt_deg,pitch_deg,roll_deg,heading_deg,sats,gps_ok,imu_ok,mag_ok,"
    "gnd_lat,gnd_lon,gnd_alt_m\n"
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


def parse_phy_slug(line: str) -> str | None:
    """Extract slug=... from a PHY,... boot line."""
    if not line.startswith(PHY_PREFIX):
        return None
    for part in line[len(PHY_PREFIX) :].split(","):
        part = part.strip()
        if part.startswith("slug="):
            slug = part[5:].strip()
            if slug:
                return slug
    return None


def fmt_gnd(lat: float | None, lon: float | None, alt: float | None) -> str:
    lat_s = "" if lat is None else f"{lat:.7f}"
    lon_s = "" if lon is None else f"{lon:.7f}"
    alt_s = "" if alt is None else f"{alt:.3f}"
    return f"{lat_s},{lon_s},{alt_s}"


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
    ap.add_argument(
        "--lat",
        type=float,
        default=None,
        help="Ground station latitude (WGS84). Host GPS — not from the Heltec.",
    )
    ap.add_argument(
        "--lon",
        type=float,
        default=None,
        help="Ground station longitude (WGS84).",
    )
    ap.add_argument(
        "--alt",
        type=float,
        default=None,
        help="Ground station altitude in metres (optional).",
    )
    ap.add_argument(
        "--phy-timeout",
        type=float,
        default=8.0,
        help="Seconds to wait for board PHY, boot line before naming session (default: 8).",
    )
    args = ap.parse_args()

    if (args.lat is None) ^ (args.lon is None):
        raise SystemExit("Pass both --lat and --lon, or neither.")

    port = pick_port(args.port)
    parent = Path(args.log_dir).expanduser().resolve()
    parent.mkdir(parents=True, exist_ok=True)

    print(f"Opening {port} @ {args.baud}")
    if args.lat is not None:
        print(f"Ground GPS (host): lat={args.lat:.7f} lon={args.lon:.7f}"
              + (f" alt={args.alt:.1f} m" if args.alt is not None else ""))
    else:
        print("Ground GPS (host): not set — pass --lat/--lon for range analysis")
    print("Waiting for PHY boot line from ground node...")
    print("Ctrl+C to stop\n")

    gnd_csv = fmt_gnd(args.lat, args.lon, args.alt)
    n_flight = 0
    n_ping = 0
    n_noack = 0
    phy_slug: str | None = None
    session: Path | None = None
    flight_f = ping_f = log_f = None

    def open_session(slug: str | None) -> None:
        nonlocal session, flight_f, ping_f, log_f
        stamp = datetime.now(timezone.utc).strftime("%Y%m%d_%H%M%S")
        name = f"{stamp}_{slug}" if slug else stamp
        session = parent / name
        session.mkdir(parents=True, exist_ok=True)
        flight_f = (session / "flight.csv").open("w", encoding="utf-8", newline="")
        ping_f = (session / "ping.csv").open("w", encoding="utf-8", newline="")
        log_f = (session / "ground.log").open("w", encoding="utf-8")
        flight_f.write(FLIGHT_HEADER)
        flight_f.flush()
        ping_f.write(PING_HEADER)
        ping_f.flush()
        log_f.write(f"# HAB ground log started {stamp}Z port={port}\n")
        log_f.flush()
        meta = session / "meta.txt"
        with meta.open("w", encoding="utf-8") as mf:
            mf.write(f"started_utc={stamp}Z\n")
            mf.write(f"port={port}\n")
            mf.write(f"phy_slug={slug or ''}\n")
            if args.lat is not None:
                mf.write(f"gnd_lat={args.lat:.7f}\n")
                mf.write(f"gnd_lon={args.lon:.7f}\n")
            if args.alt is not None:
                mf.write(f"gnd_alt_m={args.alt:.3f}\n")
        print(f"Session → {session}")
        print("  flight.csv / ping.csv / ground.log / meta.txt")

    def ensure_session() -> None:
        if session is None:
            open_session(phy_slug)

    def close_session() -> None:
        for f in (flight_f, ping_f, log_f):
            if f is not None:
                f.close()

    try:
        with serial.Serial(port, args.baud, timeout=0.2) as ser:
            deadline = time.monotonic() + max(0.0, args.phy_timeout)
            while True:
                try:
                    raw = ser.readline()
                except KeyboardInterrupt:
                    print("\nStopped.")
                    break

                if not raw:
                    # Timed out waiting for a line — open session without PHY if needed.
                    if session is None and time.monotonic() >= deadline:
                        print("No PHY line yet — opening session without PHY slug")
                        ensure_session()
                    continue

                try:
                    line = raw.decode("utf-8", errors="replace").rstrip("\r\n")
                except Exception:
                    continue

                slug = parse_phy_slug(line)
                if slug and phy_slug is None:
                    phy_slug = slug
                    print(f"PHY slug: {phy_slug}")
                    if session is None:
                        open_session(phy_slug)
                    else:
                        # Session already opened; record in log/meta note.
                        assert log_f is not None and session is not None
                        log_f.write(f"# late PHY slug={phy_slug}\n")
                        log_f.flush()

                ensure_session()
                assert log_f is not None and flight_f is not None and ping_f is not None

                log_f.write(line + "\n")
                log_f.flush()

                if line.startswith(FLIGHT_PREFIX):
                    body = line[len(FLIGHT_PREFIX) :]
                    if body.startswith("rx_utc,"):
                        print(line)
                        continue
                    row = stamp_rx_utc(body)
                    flight_f.write(f"{row},{gnd_csv}\n")
                    flight_f.flush()
                    n_flight += 1
                    print(f"FLIGHT,{row},{gnd_csv}")
                elif line.startswith(PING_PREFIX):
                    body = line[len(PING_PREFIX) :]
                    if body.startswith("rx_utc,"):
                        print(line)
                        continue
                    row = stamp_rx_utc(body)
                    ping_f.write(f"{row},{gnd_csv}\n")
                    ping_f.flush()
                    n_ping += 1
                    # event is first field after rx_utc in stamped row: utc,event,acked,...
                    parts = row.split(",")
                    if len(parts) >= 3 and parts[1] == "timeout":
                        n_noack += 1
                    print(f"PING,{row},{gnd_csv}")
                else:
                    print(line)
    finally:
        close_session()

    print(f"Saved {n_flight} flight + {n_ping} ping row(s)"
          + (f" ({n_noack} no-ACK)" if n_noack else ""))
    if session is not None:
        print(f"  session: {session}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
