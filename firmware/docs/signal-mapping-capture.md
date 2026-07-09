# Controlled signal-mapping capture

How to decode an **unknown discrete/status signal** on the J1850 bus (brake,
clutch, neutral, lean, oil, fuel level, immobiliser). A moving ride log is the
wrong tool for these: many things change at once, so you can't tell which input
drove a byte. Instead, hold everything still and **toggle one input at a time**
while logging — the frame whose bytes track *that* toggle is the mapping.

This is what Ride 2 could not do: `48 3B 40` bit5 correlated with braking, but
brake and clutch always co-occur when stopping, so the ride couldn't separate
them. A 10-minute stationary session settles it.

## Setup

- **RX-only sniffer build** (safe, cannot touch the bus):
  `CONFIG_VROD_J1850_SNIFFER=y`, `CONFIG_VROD_J1850=y` off is fine — you want the
  raw frame log, not the gauge. RX tapped to pin 7 / pin 5 into GPIO 20.
- **Capture** either way:
  - Laptop: `idf.py monitor` (each line is timestamped `t=… j1850: …`), or
  - Laptop-free: `CONFIG_VROD_RIDE_LOG=y`, log to microSD, pull later with
    `tools/ride_log_pull.py` + read with `tools/j1850_report.py`.
- **Call out each action out loud / note the clock** so you can slice the log by
  time afterwards. Engine idling for anything that needs the ECM awake; key-on
  engine-off for pure switch/lamp states.
- Do each toggle **5+ times** with a few seconds between, so a real signal shows
  a repeatable pattern (not a one-off coincidence). The bus frames are only
  ~0.4-2.6 s apart, so hold each state a few seconds.

## The tests (one input at a time)

| # | Input | How | What to watch |
|---|---|---|---|
| 1 | **Brake** | front brake only, 5×, no clutch/no shift | does `48 3B 40` bit5 fire on each squeeze? |
| 2 | **Clutch** | pull clutch only, 5×, no brake/no shift, bike in gear stopped | does `48 3B 40` bit5 fire? (splits brake vs clutch) |
| 3 | **Neutral** | N 30 s → 1st 30 s → N 30 s (clutch as needed) | any frame that holds a **steady** state across each 30 s block? (expected: none on the bus → discrete pin-10) |
| 4 | **Lean / bank angle** | off the stand, rock the bike L/R past ~10-20° | any TSSM frame (`… 40`) byte that tracks lean? |
| 5 | **Turn signals** | L only, R only, hazard, each held ~5 s | confirm `48 DA 40 39 XX`: `XX` bit1=L, bit0=R (swapped vs HarleyDroid) |
| 6 | **Oil** | key-on engine-OFF (oil lamp on) vs running (off) | which frame/bit clears when the engine starts? |
| 7 | **Immobiliser** | key off → on, watch the brief security handshake | early frames that settle after the fob/PIN authenticates |
| 8 | **Fuel level** | hard stationary; note candidates `A8 83 61 12` / any `…level…` | can only confirm by decode + watching over a tank; may die with the stock IM |

## Analysis

Slice the log to each toggle's timestamp window and diff the frame bytes against
a quiet baseline window. The header whose payload byte flips in lockstep with the
toggle — and only then — is the signal. A quick filter:

```sh
# frames in a time window, grouped by header, showing distinct payloads
awk '$1>=T0 && $1<=T1' ride_00N.log | sort | uniq -c | sort -rn
```

Confirmed mappings go into `j1850_parse.c` (with the ride/bench evidence in the
commit), and anything that proves to be **off the bus** (neutral looks likely)
becomes a discrete-wire tap in Phase 6 — `docs/PINS.md` + the master-plan Phase 6
discrete table already reserve the dividers for pins 2/9/10/11.

## Known so far (this bike)

- `28 1B 10` RPM, `48 29 10` speed (÷188), `A8 49 10` temp (raw−40), `48 DA 40`
  turns (bit1=L/bit0=R), `68 88 10` MIL, `A8 83 10` fuel ticks, `A8 69 10` odo —
  all decoded.
- `48 3B 40` = TSSM status: bit7 heartbeat toggle, **bit5 a deceleration/brake
  event** (NOT neutral — see `ride-2-findings.md`). Tests 1-2 above split
  brake vs clutch.
- `A8 3B 10` = ECM engine-load/throttle (same function `3B`, different source).
- Neutral, oil, fuel-level, immobiliser, high/low beam: **not confirmed on the
  bus**; likely discrete taps (pins 2/9/10/11) per the Phase 6 plan.
