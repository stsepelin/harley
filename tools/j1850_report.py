#!/usr/bin/env python3
"""Digest J1850 capture logs into the Stage 3 worklist.

    ./j1850_report.py firmware/docs/captures/*.log

For every distinct message (grouped by its first three bytes — header +
mode, which is how the decode table is keyed) prints: count, share of
traffic, mean interval between repeats (the number Stage 4 IM replay
needs), payload variability, and the decode-table row it matches, if
any. Messages that repeat at a steady interval with the cluster
connected are the IM keep-alive candidates.

Reads the timestamped format j1850_capture.py writes; plain
`idf.py monitor` logs work too (no intervals without timestamps).
"""

import re
import statistics
import sys

# First-bytes prefixes from the master plan's decode table
# (docs/00-MASTER-PROJECT-PLAN.md, HarleyDroid-derived).
KNOWN = {
    "28 1B 10": "RPM = (HH<<8|LL)/4",
    "48 29 10": "speed = km/h-native counts (HH<<8|LL); /~195 -> mph (ride-1)",
    "48 3B 40": "neutral/clutch bits (bit5=N, bit7=clutch; ride-1 tentative)",
    "48 DA 40": "turn signals (bit1=L, bit0=R)",
    "68 88 10": "check engine / MIL (d3 & 0x80)",
    "A8 3B 10": "engine load / throttle (NOT gear on the V-Rod: no gear sensor)",
    "A8 49 10": "engine temp: C = raw - 40 (ride-1 confirmed)",
    "A8 69 10": "odometer ticks (~0.4 m each)",
    "A8 83 10": "fuel consumption ticks",
    "A8 83 61": "fuel gauge 0-6",
}

LINE_RE = re.compile(
    r"(?:^\s*(?P<ts>\d+\.\d+)\s+)?.*j1850: (?P<bytes>(?:[0-9A-F]{2} )+)\| CRC (?P<crc>OK|BAD)")


def main(paths: list[str]) -> int:
    if not paths:
        print(__doc__)
        return 2

    # key -> {count, bad, times[], payloads set, example}
    groups: dict[str, dict] = {}
    total = bad_total = 0

    for path in paths:
        with open(path, encoding="utf-8", errors="replace") as f:
            for line in f:
                m = LINE_RE.search(line)
                if not m:
                    continue
                total += 1
                data = m.group("bytes").split()
                key = " ".join(data[:3])
                g = groups.setdefault(key, {
                    "count": 0, "bad": 0, "times": [], "payloads": set(),
                    "example": m.group("bytes").strip(),
                })
                g["count"] += 1
                if m.group("crc") == "BAD":
                    g["bad"] += 1
                    bad_total += 1
                if m.group("ts"):
                    g["times"].append(float(m.group("ts")))
                g["payloads"].add(" ".join(data[3:]))

    if not total:
        print("no frames found in the given logs")
        return 1

    print(f"{total} frames, {bad_total} bad CRC, {len(groups)} distinct headers\n")
    hdr = f"{'header':<10} {'count':>6} {'share':>6} {'interval':>9} {'payloads':>8}  meaning"
    print(hdr)
    print("-" * len(hdr))
    for key, g in sorted(groups.items(), key=lambda kv: -kv[1]["count"]):
        ts = sorted(g["times"])
        deltas = [b - a for a, b in zip(ts, ts[1:])]
        interval = f"{statistics.median(deltas) * 1000:7.0f}ms" if deltas else "      —"
        meaning = KNOWN.get(key, "?")
        if g["bad"]:
            meaning += f"  [{g['bad']} bad CRC]"
        print(f"{key:<10} {g['count']:>6} {g['count'] / total:>5.0%} "
              f"{interval:>9} {len(g['payloads']):>8}  {meaning}")

    unknown = [k for k in groups if k not in KNOWN]
    if unknown:
        print("\nunknown headers (Stage 3 decode candidates / IM keep-alive suspects):")
        for k in sorted(unknown, key=lambda k: -groups[k]["count"]):
            print(f"  {k}   e.g. {groups[k]['example']}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
