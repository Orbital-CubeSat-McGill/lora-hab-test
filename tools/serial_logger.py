#!/usr/bin/env -S uv run
# /// script
# requires-python = ">=3.10"
# dependencies = [
#     "pyserial>=3.5",
# ]
# ///
"""Capture ground-node USB serial for a laptop bench / range test.

Echoes pretty text to the console, writes a session folder:
  - flight.csv / ping.csv / ground.log / meta.txt

Also serves a live Leaflet map at http://127.0.0.1:8765/ (disable with --no-map).

Folder naming (match payload SD by shared session_id suffix):
  logs/<host_utc>_<phy>_<session_id>/
e.g. logs/20261029_234512_sf10_bw125_cr5_p22_915_A1B2C3D4/

Opens as *_pending, then renames when the first non-zero session_id arrives
in a FLIGHT/PING row (payload TELEM/ACK over LoRa).

PHY slug is read from include/radio_config.h (does not need the boot PHY line).

Usage:
  uv run tools/serial_logger.py /dev/ttyUSB0
  uv run tools/serial_logger.py /dev/ttyUSB0 --lat 45.5048 --lon -73.5772 --alt 40
  uv run tools/serial_logger.py /dev/ttyUSB0 --ping-interval 10
"""

from __future__ import annotations

import argparse
import json
import os
import re
import select
import sys
import threading
import time
import webbrowser
from datetime import datetime, timezone
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any, TextIO

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
RADIO_CONFIG = REPO_ROOT / "include" / "radio_config.h"
MAP_HTML = Path(__file__).resolve().parent / "track_map.html"
TRAIL_MAX = 2000

FLIGHT_HEADER = (
    "rx_utc,session_id,ts_ms,gps_time,seq,lat,lon,alt_m,tilt_deg,pitch_deg,"
    "roll_deg,heading_deg,sats,gps_ok,imu_ok,mag_ok,rssi_dbm,snr_db,"
    "gnd_lat,gnd_lon,gnd_alt_m\n"
)
PING_HEADER = (
    "rx_utc,event,acked,session_id,seq,rtt_ms,rssi_dbm,snr_db,ts_ms,gps_time,"
    "lat,lon,alt_m,tilt_deg,pitch_deg,roll_deg,heading_deg,sats,gps_ok,imu_ok,"
    "mag_ok,gnd_lat,gnd_lon,gnd_alt_m\n"
)


def pick_port(preferred: str | None) -> str:
    """Return preferred port, or the first USB/UART/ACM serial device found."""
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
    """Replace the board rx_utc field with host UTC ISO-8601."""
    now = datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%S.%fZ")
    parts = body.split(",", 1)
    if len(parts) == 2:
        return f"{now},{parts[1]}"
    return f"{now},{body}"


def parse_phy_slug_line(line: str) -> str | None:
    """Extract slug= from a board PHY boot line, if present."""
    if not line.startswith(PHY_PREFIX):
        return None
    for part in line[len(PHY_PREFIX) :].split(","):
        part = part.strip()
        if part.startswith("slug="):
            slug = part[5:].strip()
            if slug:
                return slug
    return None


def phy_slug_from_radio_config(path: Path = RADIO_CONFIG) -> str:
    """Build the PHY folder slug from include/radio_config.h."""
    text = path.read_text(encoding="utf-8", errors="replace")
    if re.search(r"^#define\s+LORA_PRESET_SF7_22\b", text, re.M):
        return "sf7_bw125_cr5_p22_915"
    if re.search(r"^#define\s+LORA_PRESET_SF12_14\b", text, re.M):
        return "sf12_bw125_cr5_p14_915"

    def last_define(name: str, default: str) -> str:
        """Return the last #define value for name, or default."""
        matches = re.findall(rf"^#define\s+{name}\s+(\S+)", text, re.M)
        return matches[-1] if matches else default

    sf = int(float(last_define("LORA_SF", "10")))
    bw = float(last_define("LORA_BW_KHZ", "125.0f").rstrip("fF"))
    cr = int(float(last_define("LORA_CR", "5")))
    pwr = int(float(last_define("LORA_TX_POWER_DBM", "22")))
    freq = last_define("LORA_FREQUENCY_HZ", "915000000UL")
    freq = re.sub(r"[Uu][Ll]?$", "", freq)
    mhz = int(float(freq)) // 1_000_000
    return f"sf{sf}_bw{bw:.0f}_cr{cr}_p{pwr}_{mhz}"


def fmt_gnd(lat: float | None, lon: float | None, alt: float | None) -> str:
    """Format host ground lat,lon,alt as a CSV triple (empty if unset)."""
    lat_s = "" if lat is None else f"{lat:.7f}"
    lon_s = "" if lon is None else f"{lon:.7f}"
    alt_s = "" if alt is None else f"{alt:.3f}"
    return f"{lat_s},{lon_s},{alt_s}"


def extract_session_id_flight(stamped_row: str) -> str | None:
    """Return non-zero session_id from a stamped FLIGHT row, else None."""
    parts = stamped_row.split(",")
    if len(parts) >= 2 and re.fullmatch(r"[0-9A-Fa-f]{8}", parts[1]):
        sid = parts[1].upper()
        return None if sid == "00000000" else sid
    return None


def extract_session_id_ping(stamped_row: str) -> str | None:
    """Return non-zero session_id from a stamped PING row, else None."""
    parts = stamped_row.split(",")
    if len(parts) >= 4 and re.fullmatch(r"[0-9A-Fa-f]{8}", parts[3]):
        sid = parts[3].upper()
        return None if sid == "00000000" else sid
    return None


def _f(parts: list[str], i: int) -> float | None:
    """Parse parts[i] as float, or None if missing/empty/invalid."""
    if i >= len(parts):
        return None
    s = parts[i].strip()
    if not s:
        return None
    try:
        return float(s)
    except ValueError:
        return None


def _i(parts: list[str], i: int) -> int | None:
    """Parse parts[i] as int via float, or None if missing/empty/invalid."""
    v = _f(parts, i)
    return None if v is None else int(v)


def parse_flight_telem(stamped_row: str) -> dict[str, Any]:
    """Parse a stamped FLIGHT row into a telem dict for the map."""
    p = stamped_row.split(",")
    return {
        "rx_utc": p[0] if p else "",
        "session_id": p[1] if len(p) > 1 else "",
        "ts_ms": _i(p, 2),
        "gps_time": p[3] if len(p) > 3 else "",
        "seq": _i(p, 4),
        "lat": _f(p, 5),
        "lon": _f(p, 6),
        "alt_m": _f(p, 7),
        "tilt_deg": _f(p, 8),
        "pitch_deg": _f(p, 9),
        "roll_deg": _f(p, 10),
        "heading_deg": _f(p, 11),
        "sats": _i(p, 12),
        "gps_ok": _i(p, 13),
        "imu_ok": _i(p, 14),
        "mag_ok": _i(p, 15),
        "rssi_dbm": _f(p, 16),
        "snr_db": _f(p, 17),
    }


class TrackState:
    """Thread-safe latest telem, trail, and ground position for the map API."""

    def __init__(self, gnd_lat: float | None, gnd_lon: float | None, gnd_alt: float | None):
        """Store optional host ground WGS84 position."""
        self._lock = threading.Lock()
        self.telem: dict[str, Any] | None = None
        self.trail: list[list[float]] = []
        self.gnd = {
            "lat": gnd_lat,
            "lon": gnd_lon,
            "alt_m": gnd_alt,
        }

    def update_flight(self, telem: dict[str, Any]) -> None:
        """Set latest telem; append lat/lon to trail when gps_ok."""
        with self._lock:
            self.telem = telem
            lat, lon = telem.get("lat"), telem.get("lon")
            if lat is not None and lon is not None and telem.get("gps_ok") == 1:
                self.trail.append([lat, lon])
                if len(self.trail) > TRAIL_MAX:
                    self.trail = self.trail[-TRAIL_MAX:]

    def snapshot(self) -> dict[str, Any]:
        """Return a JSON-ready copy of telem, trail, and ground position."""
        with self._lock:
            return {
                "telem": self.telem,
                "trail": list(self.trail),
                "gnd": self.gnd,
            }


def start_map_server(state: TrackState, port: int) -> ThreadingHTTPServer:
    """Serve track_map.html and /api/state on 127.0.0.1 in a daemon thread."""
    html = MAP_HTML.read_text(encoding="utf-8")

    class Handler(BaseHTTPRequestHandler):
        def log_message(self, fmt: str, *args: Any) -> None:
            """Silence per-request access logs."""
            return

        def do_GET(self) -> None:
            """Serve the map page or JSON state snapshot."""
            if self.path in ("/", "/index.html"):
                body = html.encode("utf-8")
                self.send_response(200)
                self.send_header("Content-Type", "text/html; charset=utf-8")
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)
            elif self.path.startswith("/api/state"):
                body = json.dumps(state.snapshot()).encode("utf-8")
                self.send_response(200)
                self.send_header("Content-Type", "application/json")
                self.send_header("Cache-Control", "no-store")
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)
            else:
                self.send_error(404)

    httpd = ThreadingHTTPServer(("127.0.0.1", port), Handler)
    threading.Thread(target=httpd.serve_forever, daemon=True).start()
    return httpd


def write_meta(
    session: Path,
    *,
    stamp: str,
    port: str,
    phy: str,
    session_id: str | None,
    lat: float | None,
    lon: float | None,
    alt: float | None,
) -> None:
    """Write session meta.txt (port, phy, session_id, optional ground GPS)."""
    with (session / "meta.txt").open("w", encoding="utf-8") as mf:
        mf.write(f"started_utc={stamp}Z\n")
        mf.write(f"port={port}\n")
        mf.write(f"phy_slug={phy}\n")
        mf.write(f"session_id={session_id or ''}\n")
        if lat is not None:
            mf.write(f"gnd_lat={lat:.7f}\n")
            mf.write(f"gnd_lon={lon:.7f}\n")
        if alt is not None:
            mf.write(f"gnd_alt_m={alt:.3f}\n")


def send_ping(ser: serial.Serial) -> None:
    """Write a PING command to the ground node over USB serial."""
    ser.write(b"PING\n")
    ser.flush()
    print("→ PING")


def stdin_wants_ping() -> bool:
    """True if the user typed PING (+ Enter) on stdin (non-blocking)."""
    if not sys.stdin.isatty():
        return False
    ready, _, _ = select.select([sys.stdin], [], [], 0)
    if not ready:
        return False
    return sys.stdin.readline().strip().upper() == "PING"


def main() -> int:
    """Open serial, log FLIGHT/PING CSVs, optionally serve the live map."""
    ap = argparse.ArgumentParser(description="HAB ground serial logger")
    ap.add_argument("port", nargs="?", help="Serial device (default: auto)")
    ap.add_argument(
        "log_dir",
        nargs="?",
        default=str(DEFAULT_LOG_DIR),
        help=f"Parent directory for session folders (default: {DEFAULT_LOG_DIR})",
    )
    ap.add_argument("-b", "--baud", type=int, default=115200)
    ap.add_argument("--lat", type=float, default=None, help="Ground WGS84 latitude")
    ap.add_argument("--lon", type=float, default=None, help="Ground WGS84 longitude")
    ap.add_argument("--alt", type=float, default=None, help="Ground altitude m")
    ap.add_argument("--no-map", action="store_true", help="Disable live Leaflet map")
    ap.add_argument("--map-port", type=int, default=8765, help="Map HTTP port (default 8765)")
    ap.add_argument(
        "--ping-interval",
        type=float,
        default=None,
        metavar="SEC",
        help="Auto-send PING every SEC seconds (manual: type PING or press PRG)",
    )
    args = ap.parse_args()

    if (args.lat is None) ^ (args.lon is None):
        raise SystemExit("Pass both --lat and --lon, or neither.")
    if args.ping_interval is not None and args.ping_interval <= 0:
        raise SystemExit("--ping-interval must be > 0.")

    port = pick_port(args.port)
    parent = Path(args.log_dir).expanduser().resolve()
    parent.mkdir(parents=True, exist_ok=True)

    phy_slug = phy_slug_from_radio_config()
    stamp = datetime.now(timezone.utc).strftime("%Y%m%d_%H%M%S")
    session = parent / f"{stamp}_{phy_slug}_pending"
    session.mkdir(parents=True, exist_ok=True)

    print(f"Opening {port} @ {args.baud}")
    print(f"PHY slug (from radio_config.h): {phy_slug}")
    if args.lat is not None:
        print(
            f"Ground GPS (host): lat={args.lat:.7f} lon={args.lon:.7f}"
            + (f" alt={args.alt:.1f} m" if args.alt is not None else "")
        )
    else:
        print("Ground GPS (host): not set — pass --lat/--lon for range analysis")
    print(f"Session (renames on first sid) → {session}")

    track: TrackState | None = None
    httpd: ThreadingHTTPServer | None = None
    if not args.no_map:
        track = TrackState(args.lat, args.lon, args.alt)
        httpd = start_map_server(track, args.map_port)
        map_url = f"http://127.0.0.1:{args.map_port}/"
        print(f"Live map → {map_url}")
        if os.environ.get("DISPLAY") or os.environ.get("WAYLAND_DISPLAY"):
            webbrowser.open(map_url)

    if args.ping_interval is not None:
        print(f"Auto-PING every {args.ping_interval:g} s (type PING or press PRG for manual)")
    else:
        print("PING: type PING + Enter, or press PRG on the ground board")
    print("Ctrl+C to stop\n")

    gnd_csv = fmt_gnd(args.lat, args.lon, args.alt)
    session_id: str | None = None
    n_flight = 0
    n_ping = 0
    n_noack = 0

    write_meta(
        session,
        stamp=stamp,
        port=port,
        phy=phy_slug,
        session_id=None,
        lat=args.lat,
        lon=args.lon,
        alt=args.alt,
    )

    flight_f: TextIO = (session / "flight.csv").open("w", encoding="utf-8", newline="")
    ping_f: TextIO = (session / "ping.csv").open("w", encoding="utf-8", newline="")
    log_f: TextIO = (session / "ground.log").open("w", encoding="utf-8")
    flight_f.write(FLIGHT_HEADER)
    flight_f.flush()
    ping_f.write(PING_HEADER)
    ping_f.flush()
    log_f.write(f"# HAB ground log started {stamp}Z port={port} phy={phy_slug}\n")
    log_f.flush()

    def attach_sid(sid: str) -> None:
        """Rename *_pending session folder once the first real session_id arrives."""
        nonlocal session, session_id, flight_f, ping_f, log_f
        if session_id is not None or not sid or sid == "00000000":
            return
        session_id = sid
        new_path = parent / f"{stamp}_{phy_slug}_{session_id}"
        flight_f.close()
        ping_f.close()
        log_f.close()
        if new_path.exists():
            print(f"WARN: {new_path.name} exists — keeping {session.name}")
        else:
            session.rename(new_path)
            session = new_path
        flight_f = (session / "flight.csv").open("a", encoding="utf-8", newline="")
        ping_f = (session / "ping.csv").open("a", encoding="utf-8", newline="")
        log_f = (session / "ground.log").open("a", encoding="utf-8")
        write_meta(
            session,
            stamp=stamp,
            port=port,
            phy=phy_slug,
            session_id=session_id,
            lat=args.lat,
            lon=args.lon,
            alt=args.alt,
        )
        log_f.write(f"# session_id={session_id} folder={session.name}\n")
        log_f.flush()
        print(f"Matched payload session_id={session_id}")
        print(f"Session → {session}")

    try:
        with serial.Serial(port, args.baud, timeout=1) as ser:
            last_auto_ping = time.monotonic()
            while True:
                try:
                    raw = ser.readline()
                except KeyboardInterrupt:
                    print("\nStopped.")
                    break

                now = time.monotonic()
                if (
                    args.ping_interval is not None
                    and (now - last_auto_ping) >= args.ping_interval
                ):
                    send_ping(ser)
                    last_auto_ping = now
                if stdin_wants_ping():
                    send_ping(ser)

                if not raw:
                    continue
                try:
                    line = raw.decode("utf-8", errors="replace").rstrip("\r\n")
                except Exception:
                    continue

                boot_phy = parse_phy_slug_line(line)
                if boot_phy and boot_phy != phy_slug:
                    msg = (
                        f"# WARN board PHY slug={boot_phy} != "
                        f"radio_config {phy_slug}\n"
                    )
                    log_f.write(msg)
                    log_f.flush()
                    print(f"WARN: board PHY {boot_phy} != radio_config.h {phy_slug}")

                log_f.write(line + "\n")
                log_f.flush()

                if line.startswith(FLIGHT_PREFIX):
                    body = line[len(FLIGHT_PREFIX) :]
                    if body.startswith("rx_utc,"):
                        print(line)
                        continue
                    row = stamp_rx_utc(body)
                    sid = extract_session_id_flight(row)
                    if sid:
                        attach_sid(sid)
                    flight_f.write(f"{row},{gnd_csv}\n")
                    flight_f.flush()
                    n_flight += 1
                    if track is not None:
                        track.update_flight(parse_flight_telem(row))
                    print(f"FLIGHT,{row},{gnd_csv}")
                elif line.startswith(PING_PREFIX):
                    body = line[len(PING_PREFIX) :]
                    if body.startswith("rx_utc,"):
                        print(line)
                        continue
                    row = stamp_rx_utc(body)
                    sid = extract_session_id_ping(row)
                    if sid:
                        attach_sid(sid)
                    ping_f.write(f"{row},{gnd_csv}\n")
                    ping_f.flush()
                    n_ping += 1
                    parts = row.split(",")
                    if len(parts) >= 2 and parts[1] == "timeout":
                        n_noack += 1
                    print(f"PING,{row},{gnd_csv}")
                else:
                    print(line)
    finally:
        if httpd is not None:
            httpd.shutdown()
        for f in (flight_f, ping_f, log_f):
            try:
                f.close()
            except Exception:
                pass

    print(
        f"Saved {n_flight} flight + {n_ping} ping row(s)"
        + (f" ({n_noack} no-ACK)" if n_noack else "")
    )
    print(f"  session: {session}")
    if session_id is None:
        print("  NOTE: no session_id yet — folder still ends with _pending")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
