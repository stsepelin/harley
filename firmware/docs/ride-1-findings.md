# Ride 1 findings — J1850 decode calibration

First real capture on the 2009 VRSCF: ~10.4 min, laptop-free, powerbank-
powered, RX-only tap, logged to microSD and pulled over serial
(`CONFIG_VROD_RIDE_LOG_DUMP` + `tools/ride_log_pull.py`). 16,896 frames,
~2.5% bad CRC (RX noise, tolerable; a hysteresis front end is Phase 6).

Bike spec sheet (`2016 V-Rod Muscle`, same drivetrain): overall gear ratios
1st 10.969 / 2nd 7.371 / 3rd 5.9 / 4th 5.095 / 5th 4.563; rear 240/40R18
(~2.04 m rolling circumference); 5-speed. These are exact and drive the gear
calculator + the speed cross-check below.

## Confirmed

### Speed `48 29 10 02` — km/h-native, divisor provisional
The frame is **km/h-native**, ~117-128 raw counts per km/h — NOT mph-native as
first guessed. Evidence:
- Peak frame `48 29 10 02 22 0C` = 8716 counts; /128 = 68 ≈ the ~70 km/h the
  rider held.
- Fitting the logged RPM/speed pairs to the exact gear ratios gives ~117
  counts/km-h (explains 82% of moving samples within 6%; gear clusters land
  where 1st/2nd predict).

`vehicle_data.speed_mph` is mph-canonical, so the parser divides counts → mph:
- gear-ratio physics → ~188  ·  stock-speedo peak → ~200  ·  naive 68≈70 → 206.
- **Set to 195 provisionally** (was 128, which read ~1.5x high — matched the
  ride: 30→40, 70→100+). Still ±~5%.
- **To lock:** one GPS-referenced point (companion app auto-correlates phone
  GPS speed with the logged `speed_raw` counts). The ride log stores RAW counts
  (`speed_raw=`) so the divisor is re-derivable from any capture.

### Temp `A8 49 10 10` — `°C = raw − 40`
OBD-style offset, confirmed by the cold-start correlation: raw `0x3F` (63) at
~20-25°C ambient → 23°C; warmed to `0x81` (129) → 89°C. Both correct. Stored
int8_t; realistic bus range maps well within range.

### Gear — no sensor; compute from RPM/speed ratio
The stock cluster shows only an **N** indicator, no gear number — the V-Rod has
no gear-position sensor. `gear = argmin |rpm/speed − ratio_g|` over the exact
overall ratios is deterministic (how Healtech GIpro-type indicators work, no
sensor needed). Ride 1 confirms the 1st/2nd clusters. To build as pure
`gear_calc.c`.

### Odometer / trip on the bus `A8 69 10`
A rising 16-bit counter (`…06 00 64 → 06 00 C8 → 06 01 2C …`, +100 each frame)
sent every ~40 m — i.e. **0.4 m per tick** (frame spacing tracks speed: ~4 s at
pace, ~6.5 s slow). So odo/trip come straight from the bus — no speed
integration. Accumulate ticks × 0.4 m; trips are deltas. (Exact tick size to be
confirmed against GPS distance.)

## Not on the bus (discrete wires — Phase 6)
Confirmed by comparing our cluster to the stock one during the ride:
- **Low oil pressure** lamp — stock lit it, we never saw it → discrete.
- **Battery/charge** lamp — same → discrete (~70% sure).
- **Low / high beam** — low not present; high via voltage rise → discrete/analog.
- No ABS on this bike (ABS became standard on later V-Rods); ignore the lamp.

## Corrected / reinterpreted
- **`A8 3B 10` is NOT gear.** We had it decoding a gear ladder; the payloads are
  a two-byte parameter (`03 00 00` idle, `03 02 3A`, `03 04 74`, `03 08 E8`…)
  dominant at zero when stopped and stepping up under power — looks like an
  **engine-load / throttle-position** readout. The ECM's `28 3B 10` variant is a
  monotonically rising counter. PID `3B` identity TBD (throttle-sweep capture).
- **Neutral is on `48 3B 40`**, not `A8 3B 10`. Payload d3 pairs up:
  `0x20/0xA0` (bit5) only at the start before pulling away, `0x02/0x82` (bit1)
  once rolling, bit7 toggling like a clutch switch. Tentative: **bit5 = Neutral,
  bit7 = clutch**. Needs one short confirm capture (neutral → clutch+1st →
  neutral) before wiring.

## Frame map (ride-1 frequencies)
| id | n | meaning |
|---|---|---|
| `28 1B 10` | 8476 | RPM = (HH<<8\|LL)/4 (confirmed) |
| `48 29 10` | 3041 | speed, km/h-native counts (÷~195 → mph) |
| `A8 3B 10` | 1136 | engine load / throttle (NOT gear) |
| `68 88 10` | 561 | check-engine/MIL bit (d3 & 0x80) |
| `A8 83 10` | 391 | fuel-consumption ticks |
| `48 DA 40` | 271 | turn signals (bit1=L, bit0=R) |
| `A8 49 10` | 116 | engine temp (°C = raw − 40) |
| `A8 69 10` | 73 | odometer ticks (0.4 m) |
| `48 3B 40` | 48 | neutral / clutch bits |
| `C8 88 10`, `C8 89 60`, `E8 89 60`, `68 FF ..`, `29 FE ..` | — | inter-module (IM/ABS/BCM) broadcasts; mine for status bits later |

## Undecoded-data audit
Of 16,895 frames, **83.6% decode** to gauge signals. The other 16.4% is not
lost data:
- **~2.5% CRC-noise** — the long tail of single-count headers (`2A 1B 10`,
  `28 1B 30`, `48 A9 10`, …) are bit-flipped copies of known frames, not real
  message types.
- **~14% static inter-module keep-alive/status** — `C8 88 10 0E`, `C8 89 60 03`,
  `E8 89 60 0E`, `68 FF 10/40/60 03`, `29 FE 40/60 01`: each a single constant
  payload for the whole ride → network housekeeping (IM/ABS/TSSM/BCM), no gauge
  signal. Stage 4 replay candidates.

**One real signal still unwired: FUEL (`A8 83 10`).** The 16-bit value climbs
strictly monotonically (480 → 8705, +20/frame, zero resets) — a cumulative
fuel-*consumption* counter (a "fuel odometer"), NOT a warning flag and NOT a
level. Three separate fuel things, only the first available now:
- **Consumption** (`A8 83 10`) — this counter; feeds economy/range and the
  spec's countdown feature.
- **Fuel level** (gauge) — not seen on the bus under the expected id; likely an
  analog sender (discrete/ADC) or an uncaptured frame.
- **Low-fuel warning** — a discrete flag that lights near ~1.9 L; the bit never
  flipped this ride (never ran low), so it's unlocated. Find it with a capture
  taken while the low-fuel lamp is ON, then diff the status frames (same method
  as the neutral hunt).

**Caveat:** the static keep-alive frames could hide an oil/battery/ABS status
bit that simply never changed during an all-engine-running ride. A capture that
includes ignition-on / cranking / a real fault would reveal it; until then the
discrete-wire assumption stands.

Reference capture committed at `firmware/docs/captures/2026-07-08-ride-1.log`.

## Fault codes (DTCs)
The V-Rod stores per-module DTCs (`P/C/B/U####`, SAE-standard + H-D-proprietary)
on this same bus. We already get the **MIL/CEL lamp bit** passively (`68 88 10`).
Reading the actual codes is **request/response** — send a "read DTCs" request to
each module and parse the reply, which needs **TX** (Stage 4). Target surfacing
codes (and clear-codes) on the **companion app**.

## Companion app (next)
- **Telemetry** GATT notify: stream decoded `vehicle_data` (+ optional raw
  frames) → phone logs live, no card needed.
- **GPS speed calibration**: correlate phone GPS speed with logged `speed_raw`
  counts → exact speed divisor, no eyeballing.
- **Config** GATT write: push calibration (speed divisor, temp offset, gear
  ratios, units) back to the cluster, persisted in NVS.
- **DTC readout**: request + display stored codes; clear-codes.
