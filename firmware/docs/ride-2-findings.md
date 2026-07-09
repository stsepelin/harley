# Ride 2 findings — speed divisor lock + live-stack review

Second on-bike session (2026-07-09), ~2 rides bracketing a fuel stop:
`2026-07-09-ride-2a.log` (66,106 frames, ~4% bad CRC — the outbound leg) and
`2026-07-09-ride-2b.log` (12,167 frames, 0.17% bad CRC — the return leg). Both
pulled over serial (`CONFIG_VROD_RIDE_LOG_DUMP` + `tools/ride_log_pull.py`).

## Headline: the ride ran on the wrong divisor

The cluster ran Ride 2 at **speed divisor 130**, not the intended 195 — a
leftover written to NVS during bench validation of the config write-back path
(the last manual value sent was 130; it persisted, and a plain reflash does not
erase NVS). Speed therefore read **~45% high** (188/130), and because the gear
indicator is derived from `rpm / speed_mph`, that one error dragged every gear
1-3 positions too high.

## The divisor lock: 188 (counts/km-h = 117)

The ride data pins it independently of any GPS reference. Pairing each speed
frame with the latest RPM (17,006 speed frames) and fitting the exact overall
gear ratios (1st 10.969 … 5th 4.563, rear 240/40R18 ≈ 2.04 m circumference):

- `speed_raw / rpm` clusters at **1.93** and **2.42**, spaced 1.25 — exactly the
  2nd:3rd ratio (7.371/5.9 = 1.249).
- 2nd @1.93 → 116 counts/km-h; 3rd @2.42 → 117 counts/km-h. **Both agree at ~117
  counts per km/h**, matching Ride 1's independent gear-ratio fit.

`speed_mph = speed_raw / divisor`, and `speed_raw = 117 · km/h = 117 · 1.609 ·
mph`, so **divisor = 117 × 1.609 = 188**. Cross-check against the rider's
roadside-radar point (true ≈ 28 km/h): at divisor 188 a true 28 km/h reads
28 km/h. (At the divisor that actually ran, 130, it read ~40 km/h — consistent
with the ~35 km/h the rider saw.)

## Gear derivation confirmed — it was the divisor, not the logic

Replaying `gear_calc` over the time-aligned ride stream:

| divisor | shown gear == true gear | in true 1st, shows |
|---|---|---|
| 130 (what ran) | **2%** | 2nd 68% / 1st 13% / N 17% |
| 188 (correct)  | **91%** | 1st 68% / N 31% |

So "1st gear climbing to 4th, inconsistent" is fully explained by the low
divisor; the gear-band logic is sound at the correct divisor. No gear-code
change needed — it tracks the speed calibration by design.

## Every field-observation, mapped

| Observation | Cause |
|---|---|
| Speed ~45% high vs radar | NVS divisor 130 (bench leftover). Correct = **188**. |
| Gears wrong / inconsistent | Downstream of the divisor (2% → 91% correct at 188). |
| Neutral won't restore after moving | **Neutral is not decoded.** No `48 3B 40` case in `j1850_parse.c` (deferred). The "N" shown is only `gear_calc` returning UNKNOWN below 5 mph, not a real neutral signal. |
| Key-on: oil + immobiliser on stock, nothing on ours | **Not decoded.** The parser handles only RPM / temp / speed / turn / check-engine. Oil (pin 9) + immobiliser are discrete or IM-originated, not in the table. |
| Check-engine blinked early then synced | Correct (`68 88 10`, bit 0x80) — a boot-order blink before the first good frame. |
| Temp OK | Correct (`A8 49 10`, °C = raw − 40). |
| Fuel low-alert / consumption arrow | Stock cluster (as noted). Ours decodes only fuel consumption *ticks* (`A8 83 10`) for economy, not level / low-fuel. |
| App calibration "Finish" never enabled | GPS ran only ~3 s (appops). The wizard samples only while its Composable is foreground+awake, so on a moving bike it never reached the ≥5-sample floor. App-side flaw, not the bus. |

## Fuel calibration captured

The rider did complete the fill-up calibration: the phone stored
`ml_per_tick = 0.309` (`zeppl.fuel.xml`). That now drives the Fuel card's
mpg / L/100km / range-to-empty. (Sanity: ~0.31 mL/tick ≈ 10 L over ~32k ticks.)

## Actions

1. **Divisor → 188.** Reset the cluster's NVS `speed_div` (clear the bench 130)
   and move the compile-time default 195 → 188 (Ride 1 physics + Ride 2 physics
   + radar all agree). Done in the same change as this doc.
2. **Fix the GPS calibration wizard** so sampling survives screen-off — collect
   in the foreground BLE service instead of the Composable, lower the sample
   floor, and/or allow a manual finish. (Follow-up.)
3. **Decode neutral** (`48 3B 40`, bit5) — confirm the bit's polarity against
   stops in this log, then wire it in. (Follow-up.)
4. **Oil / fuel-level / immobiliser** — decide per signal whether it is on the
   bus (decode it) or a discrete-wire tap for Phase 6.
