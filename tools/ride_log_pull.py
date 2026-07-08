#!/usr/bin/env python3
"""Pull ride logs off the cluster over serial, no card removal.

Pairs with CONFIG_VROD_RIDE_LOG_DUMP: that build prints every
/sdcard/ride_NNN.log to the console at boot, framed by markers:

    ===RIDELOG BEGIN <name> <bytes>===
    <exact file bytes>
    ===RIDELOG END <name>===
    ...
    ===RIDELOG DONE===

This script resets the board, captures the stream, and writes each file to
the output directory verbatim (verifying the byte count from the BEGIN
marker). It can also parse a previously saved capture with --from-file.

    python tools/ride_log_pull.py -p /dev/cu.usbmodem5B5F0299541 -o rides/
    python tools/ride_log_pull.py --from-file capture.bin -o rides/
"""
import argparse
import os
import re
import sys
import time

BEGIN = re.compile(rb"===RIDELOG BEGIN (ride_\d+\.log) (\d+)===\n")
DONE = b"===RIDELOG DONE==="


def capture_serial(port, baud, timeout_s):
    import serial  # pyserial

    p = serial.Serial(port, baud, timeout=0.2)
    # Pulse EN (RTS) to reset into run mode; keep GPIO0 (DTR) high = normal boot.
    p.setDTR(False)
    p.setRTS(True)
    time.sleep(0.15)
    p.setRTS(False)
    buf = bytearray()
    t0 = time.time()
    while time.time() - t0 < timeout_s:
        buf += p.read(8192)
        if DONE in buf:
            # give the tail a moment to drain, then stop
            buf += p.read(8192)
            break
    p.close()
    return bytes(buf)


def extract(stream, out_dir):
    os.makedirs(out_dir, exist_ok=True)
    pulled = []
    for m in BEGIN.finditer(stream):
        name = m.group(1).decode()
        size = int(m.group(2))
        start = m.end()
        data = stream[start:start + size]
        path = os.path.join(out_dir, name)
        with open(path, "wb") as f:
            f.write(data)
        ok = len(data) == size
        pulled.append((name, size, len(data), ok, path))
    return pulled


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("-p", "--port", help="serial port (e.g. /dev/cu.usbmodemXXXX)")
    ap.add_argument("-b", "--baud", type=int, default=115200)
    ap.add_argument("-o", "--out", default="rides", help="output directory")
    ap.add_argument("-t", "--timeout", type=float, default=180.0,
                    help="max seconds to wait for the DONE marker")
    ap.add_argument("--from-file", help="parse a saved capture instead of serial")
    ap.add_argument("--save-raw", help="also write the raw capture to this path")
    args = ap.parse_args()

    os.makedirs(args.out, exist_ok=True)
    if args.from_file:
        with open(args.from_file, "rb") as f:
            stream = f.read()
    elif args.port:
        stream = capture_serial(args.port, args.baud, args.timeout)
        if args.save_raw:
            with open(args.save_raw, "wb") as f:
                f.write(stream)
    else:
        ap.error("give --port or --from-file")

    if DONE not in stream:
        print("WARNING: no ===RIDELOG DONE=== marker seen - capture may be "
              "truncated (raise --timeout).", file=sys.stderr)

    pulled = extract(stream, args.out)
    if not pulled:
        print("No ride logs found in the stream. Is CONFIG_VROD_RIDE_LOG_DUMP "
              "built in, and is a card mounted?", file=sys.stderr)
        return 1

    print(f"Pulled {len(pulled)} file(s) into {args.out}/:")
    all_ok = True
    for name, size, got, ok, path in pulled:
        tag = "ok" if ok else f"SHORT ({got}/{size})"
        all_ok &= ok
        print(f"  {name}: {got} bytes [{tag}]")
    return 0 if all_ok else 2


if __name__ == "__main__":
    raise SystemExit(main())
