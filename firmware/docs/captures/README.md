# J1850 capture corpus

Raw sniffer logs off the bike's bus (Phase 3 Stage 2). These become
the test fixtures for the Stage 3 message parser, so capture liberally
— disk is cheap, bench time on the bike is not.

## Capturing

```sh
cd firmware
idf.py menuconfig        # V-Rod cluster -> J1850 passive sniffer
idf.py -p /dev/cu.usbmodem5B5F0299541 build flash

# Preferred: clean, host-timestamped capture + live tee + summary
../tools/j1850_capture.py -o firmware/docs/captures/2026-07-XX-ignition-on.log
# (fallback: script -q <name>.log idf.py -p PORT monitor — works, but
#  leaves ANSI codes in the file and no host timestamps)
```

One line per frame; the leading number is seconds since capture start
(that's what gives the analyzer message periodicity):

```
    12.345  I (12345) j1850: 28 1B 10 02 0A F0 | CRC OK
    22.345  I (22345) j1850: stats: 412 frames, 0 bad CRC, 0 overruns
```

## Analyzing

```sh
tools/j1850_report.py firmware/docs/captures/*.log
```

Groups traffic by header, shows counts / share / median repeat interval
/ payload variability, annotates rows the decode table already knows,
and lists unknown headers — the steady-interval unknowns are the IM
keep-alive candidates for Stage 4 replay.

## Naming

`YYYY-MM-DD-<condition>.log` — one file per bus condition:

- `ignition-on.log` — key on, engine off, 5+ min
- `idle.log` — engine running, stationary, 5+ min
- `riding.log` — a short ride if practical
- plus targeted ones: `turn-left.log`, `high-beam.log`, ... (flip one
  input at a time; the frames that change are the decode-table proof)

## Road captures (Stage 3.5 ride log)

`2026-07-08-ride-1.log` is the first real road capture — pulled off the
microSD (not the sniffer console), so it's in the **ride-log** line format
(`<sec.ms> j1850: HH .. | CRC OK | <decoded>`), still `j1850_report.py`-
parseable. It calibrated speed/temp and mapped the odometer / neutral / fuel
frames; full analysis in `../ride-1-findings.md`.

## What Stage 2/3 needed from these — resolved by ride 1

- The **IM keep-alive set**: the steady-interval unknowns (`C8 88 10`,
  `C8 89 60`, `E8 89 60`, `68 FF 10/40/60`, `29 FE 40/60`) — constant
  payloads = inter-module housekeeping; Stage 4 replay candidates.
- Decode-table confirmation: **speed is km/h-native** (`counts/~195` → mph,
  not `/128`); **engine temp is `raw − 40 = °C`** (not raw °C). See
  `../ride-1-findings.md`.
- CRC health: ride 1 ran ~2.5% bad — RX noise, tolerable for calibration;
  a hysteresis front end (Phase 6) tightens it.
