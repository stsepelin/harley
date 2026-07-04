#!/usr/bin/env python3
"""Capture J1850 sniffer output into a clean, timestamped log.

Plain pyserial instead of `idf.py monitor`: no ANSI colour codes in the
file, host-side timestamps on every line (the analyzer uses them for
message periodicity), and a Ctrl-C summary instead of a scrollback dig.

    pip install pyserial
    ./j1850_capture.py [-p PORT] [-o FILE] [--reset]

Defaults: the cluster's known-good port, and a log name like
2026-07-04-1432.log in firmware/docs/captures/. Everything is also
teed to stdout so you can watch frames live at the bike.
"""

import argparse
import datetime
import pathlib
import re
import sys
import time

import serial

DEFAULT_PORT = "/dev/cu.usbmodem5B5F0299541"
CAPTURES_DIR = pathlib.Path(__file__).resolve().parent.parent / "firmware/docs/captures"

FRAME_RE = re.compile(r"j1850: ((?:[0-9A-F]{2} )+)\| CRC (OK|BAD)")
STATS_RE = re.compile(r"j1850: stats: (\d+) frames, (\d+) bad CRC, (\d+) overruns")
ANSI_RE  = re.compile(r"\x1b\[[0-9;]*[a-zA-Z]")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("-p", "--port", default=DEFAULT_PORT)
    ap.add_argument("-o", "--out", help="log file (default: captures/YYYY-MM-DD-HHMM.log)")
    ap.add_argument("--reset", action="store_true",
                    help="pulse RTS to reboot the cluster before capturing")
    args = ap.parse_args()

    out_path = pathlib.Path(args.out) if args.out else \
        CAPTURES_DIR / (datetime.datetime.now().strftime("%Y-%m-%d-%H%M") + ".log")
    out_path.parent.mkdir(parents=True, exist_ok=True)

    def open_port():
        # Garage reality: USB gets bumped. Wait for the port instead of
        # dying — Ctrl-C still exits cleanly with the summary.
        announced = False
        while True:
            try:
                return serial.Serial(args.port, 115200, timeout=1)
            except serial.SerialException:
                if not announced:
                    print(f"waiting for {args.port} (plug the board in; Ctrl-C to stop)...")
                    announced = True
                time.sleep(2)

    ser = open_port()
    if args.reset:
        ser.setDTR(False)
        ser.setRTS(True)
        time.sleep(0.15)
        ser.setRTS(False)

    t0 = time.monotonic()
    frames = crc_bad = 0
    last_stats = None

    print(f"capturing {args.port} -> {out_path}  (Ctrl-C to stop)")
    try:
        with open(out_path, "w", encoding="utf-8") as out:
            while True:
                try:
                    raw = ser.readline()
                except serial.SerialException:
                    print("-- USB link lost; waiting for the board to come back --")
                    try:
                        ser.close()
                    except Exception:
                        pass
                    ser = open_port()
                    print("-- reconnected, capture continues --")
                    continue
                if not raw:
                    continue
                line = ANSI_RE.sub("", raw.decode("utf-8", "replace")).rstrip()
                if not line:
                    continue
                stamped = f"{time.monotonic() - t0:10.3f}  {line}"
                out.write(stamped + "\n")
                out.flush()
                print(stamped)
                m = FRAME_RE.search(line)
                if m:
                    frames += 1
                    if m.group(2) == "BAD":
                        crc_bad += 1
                s = STATS_RE.search(line)
                if s:
                    last_stats = s.groups()
    except KeyboardInterrupt:
        pass
    finally:
        ser.close()

    dur = time.monotonic() - t0
    print(f"\n--- {out_path}")
    print(f"    {dur:.0f}s, {frames} frames captured ({crc_bad} bad CRC)", end="")
    print(f", {frames / dur:.1f}/s" if dur > 0 and frames else "")
    if last_stats:
        print(f"    device totals at last stats line: {last_stats[0]} frames, "
              f"{last_stats[1]} bad CRC, {last_stats[2]} overruns")
    if frames and crc_bad / frames > 0.02:
        print("    WARNING: >2% bad CRC — check wiring/ground before trusting data")
    return 0


if __name__ == "__main__":
    sys.exit(main())
