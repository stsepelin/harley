# Ride 3 plan — controlled signal-mapping + GPS-map verification

> **Status: planned.** Ride 1 calibrated the core decode; Ride 2 locked the speed
> divisor (188). Ride 3 has two jobs the earlier rides couldn't do: (A) a
> **stationary, one-input-at-a-time capture** to nail the remaining discrete/status
> signals (a moving ride can't isolate them — see `signal-mapping-capture.md`), and
> (B) the first **on-bike verification of the GPS-driven moving map** (task #58).
> One session, ~45 min, powerbank-powered, RX-only tap — nothing here needs TX.

## What Ride 3 must resolve

Carried over from `ride-1-findings.md` / `ride-2-findings.md`, still open:

| Item | Question | How Ride 3 answers it |
|---|---|---|
| **Brake vs clutch** (`48 3B 40` bit5) | Ride 2 saw bit5 on braking, but brake+clutch co-occur when stopping | Part A tests 1-2: brake only, then clutch only |
| **Neutral** | Confirmed NOT on the bus (`48 3B 40` disproven); is it truly discrete? | Part A test 3: hold N vs 1st, look for any *steady* frame |
| **Oil pressure lamp** | On the bus or discrete (pin 9)? Untestable so far (engine-off capture was empty) | Part A test 6: key-on engine-OFF (lamp on) → start → diff |
| **Immobiliser / security** | The key-on handshake frames | Part A test 7: key off→on, capture the settle |
| **Fuel level + low-fuel** | Level not seen on bus; low-fuel bit never flipped (never ran low) | Part A test 8 + Part C: capture while the low-fuel lamp is ON |
| **Lean / bank angle** | Any TSSM frame track lean? | Part A test 4 (off the stand, rock L/R) |
| **Turn signals** | Re-confirm `48 DA 40 39` bit1=L / bit0=R | Part A test 5 |
| **GPS map on bike** (#58) | Does the module drive the map at speed? | Part B: ride with the map view up |
| **Speed 188 + gear** | Locked at bench; confirm against GPS/radar on the road | Part B: cross-check `speed_raw` vs phone-GPS speed |
| **Odometer tick (0.4 m)** | Confirm tick size against GPS distance | Part B: compare `A8 69 10` ticks × 0.4 m to the GPS track length |

## Prep (before leaving)

1. **Firmware build** — the capture + map build:
   - `CONFIG_VROD_J1850=y`, `CONFIG_VROD_J1850_SNIFFER=y` (raw frame log).
   - `CONFIG_VROD_RIDE_LOG=y` (SD capture, laptop-free) + `CONFIG_VROD_RIDE_LOG_DUMP=y`
     (pull over serial afterwards with `tools/ride_log_pull.py`).
   - `CONFIG_VROD_MAP_SD=y` + `CONFIG_VROD_GPS_UART=y` for the map half (Part B).
   - Flash via `/dev/cu.usbmodem5B5F0299541`; verify boot (`streaming NNN tiles`,
     `NMEA reader on UART1`).
2. **microSD** in the cluster (Estonia `map.zmta` already on it) — the ride log and
   the map **share the mount** now (`storage/sd_mount.c`), so both work together.
3. **GPS**: external **active antenna** with clear sky view (the bare patch is
   desensed next to the board — see `gps-module.md`). Confirm a solid fix (badge
   goes green `SAT 6+`) before riding.
4. **RX tap**: pin 7 (LGN/V data) + pin 5 (BK/GN ground) into GPIO 20, per the
   proxy-box T-taps. Bike keeps its stock cluster.
5. **Power**: powerbank on the board's USB (no laptop needed on the road).
6. **Phone**: companion app connected (so the `SAT ⇄ BT` handover + phone fallback
   can be exercised), fine-location granted.
7. **A stopwatch / notes**: call each action out loud and note the clock, so the log
   can be sliced by time afterwards (the whole method in `signal-mapping-capture.md`).

## Part A — stationary controlled capture (engine idling, on the stand)

The point: **one input at a time, 5+ reps, a few seconds apart**, so a real signal
shows a repeatable pattern. Start the ride log (bench UI REC, or it auto-records),
announce each test, do it, announce done. Full test table + analysis in
`signal-mapping-capture.md`; ride-day order:

1. **Turn signals** (warm-up + sanity): L only ×5, R only ×5, hazard. Confirms the
   tap is live and re-confirms `48 DA 40 39` bit1=L / bit0=R.
2. **Brake**: front brake only, 5×, **no clutch, no shift**. Watch `48 3B 40` bit5.
3. **Clutch**: pull clutch only, 5×, in gear, stopped, **no brake**. Splits brake
   vs clutch on the same bit5.
4. **Neutral**: N 30 s → 1st 30 s → N 30 s (clutch as needed). Looking for any frame
   that holds a *steady* state across each block (expected: none → confirms the
   discrete pin-10 tap for Phase 6).
5. **Lean**: off the stand, rock the bike L/R past ~10-20°, a few times each side.
   Any `…40` (TSSM) byte that tracks lean?
6. **Oil**: key-on **engine-OFF** ~30 s (oil lamp lit), then **start** the engine.
   Diff the frames for the bit that clears when oil pressure comes up. (This is the
   capture Ride 1 never got — the earlier engine-off log was empty.)
7. **Immobiliser**: key OFF → ON, capture the brief security/fob handshake before
   it settles.
8. **Fuel level** (best-effort): hard stationary, note candidates `A8 83 61 12` /
   anything level-shaped; may only confirm by watching over a tank (Part C).

## Part B — the ride (GPS map + speed/gear)

Keep the **map view up** (double-tap from the gauge). Ride a normal ~15-20 min loop
with a GPS-referenced stretch (steady speed on a known road, or the phone's speedo /
a radar sign).

- **GPS module**: badge should read green `SAT n` and the marker should track your
  real position, **heading-up rotation following your course**. Confirm the rotation
  is smooth at road speed (the fixed-point rotate + 30 fps + PPA work).
- **Source handover**: if the module drops (tunnel, tree cover), the badge should flip
  to `BT` (phone) and the map keep going; back to `SAT n` when the module re-locks.
  Note where/when it switches and how fast.
- **Speed**: hold ~50 and ~90 km/h steady past a GPS/radar reference; the ride log
  stores `speed_raw` so the 188 divisor is re-checkable (expect the readout to match
  the reference within a few %).
- **Gear**: watch the gear digit through the range; at divisor 188 it should read
  correctly ~91% of the time (Ride 2). Note any gear that's consistently wrong.
- **Ride log** runs the whole time → `firmware/docs/captures/2026-…-ride-3.log`.

## Part C — targeted, opportunistic captures

- **Low-fuel lamp**: if the tank is low (or ride it down), capture a stretch with the
  **low-fuel lamp ON** and note the clock. The low-fuel bit never flipped in Rides
  1-2; this is the only way to locate it. (Also watch whether the fuel-gauge frame
  `A8 83 61 12` even appears — it's likely IM-originated and may vanish with the
  stock IM later.)
- **Cold-start check**: the very first key-on of the day doubles as the oil +
  immobiliser capture (Part A tests 6-7) if you'd rather do them first.

## Post-ride analysis

1. Pull the log: `tools/ride_log_pull.py` over serial → `firmware/docs/captures/`.
2. Slice by the announced timestamps and diff each toggle window against a quiet
   baseline: `tools/j1850_report.py`, or the awk one-liner in
   `signal-mapping-capture.md` (`$1>=T0 && $1<=T1 | sort | uniq -c | sort -rn`).
3. For each confirmed mapping, the frame/bit whose payload flips **in lockstep with
   one toggle and only then** is the signal.

## What gets written up after (the deliverables)

- **`ride-3-findings.md`** — the results, with the evidence per signal.
- **`j1850_parse.c`** — decode any signal proven to be on the bus (brake?, fuel
  level?), with the ride evidence in the commit.
- **`PINS.md` + master-plan Phase 6 table** — anything proven **off** the bus
  (neutral, likely oil) becomes a discrete-wire tap (pins 2/9/10/11 dividers).
- **GPS/map**: fold the on-bike result into task #58; note any rotation/handover
  tuning (`GPS_MODULE_STALE_MS`, frame rate) the road exposed.
- **Speed/gear**: confirm 188 holds on the road, or open a re-calibration if not.
