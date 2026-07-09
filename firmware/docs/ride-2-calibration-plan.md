# Ride 2 plan — GPS speed calibration + live-stack validation

Ride 1 (`ride-1-findings.md`) proved the speed frame `48 29 10 02` is
km/h-native and pinned the raw->mph divisor at a **provisional 195** (physics
fit ~188, stock-speedo peak ~200, naive ~206 — all within ~5 %). Ride 2 locks
that number with a GPS reference and validates the full live stack end to end
on the Zeppl companion:

```
J1850 bus -> j1850_driver decode -> vehicle_data -> gauge
                                              \-> BLE telemetry -> phone
phone GPS speed + speed_raw -> SpeedCalibrator -> divisor -> cluster -> NVS
```

This is the payoff of Stage 5 Bricks 1-3. Brick 4 (fuel economy) needs a
fill-up and is a separate follow-up (Part D).

## Preconditions

**Firmware — production build** (the currently-flashed config; confirm before
riding):

| Option | Set | Why |
|---|---|---|
| `VROD_J1850_SNIFFER` | y | RX capture + VPW decode |
| `VROD_J1850` | y | live producer -> vehicle_data (suppresses the sim) |
| `VROD_J1850_BENCH_SPEED` | **n** | **must be off** — it fabricates speed_raw=19500 |
| `VROD_J1850_RX_GPIO` | 20 | divider node |
| `VROD_J1850_TX` | n | RX-only; cannot disturb the stock cluster / bus |
| `VROD_RIDE_LOG` | y | log raw counts to microSD for an offline cross-check |
| `VROD_RIDE_LOG_DUMP` | **n** | leave off — dumping a full log blocks boot ~1 min |

Sanity-check on serial before leaving: **no `j1850_bench` lines**, and
`j1850: stats` climbing with frames once the RX tap is on the bus.

**Rig:**
- RX front end tapped to J1850 **pin 7 (signal) + pin 5 (ground)** into the
  GPIO 20 divider node (same as the sniffer).
- Bike 5 V power (`bike-power-injection.md`) or a powerbank on the header 5 V.
- microSD inserted (ride log).
- Phone: **Zeppl** app installed + bonded to the cluster; a clear-sky start so
  GPS locks quickly.
- **A pillion or an assistant to operate the phone** — the wizard needs taps
  while moving. Never interact handheld while riding; do it stopped or hand it
  to a passenger.

## Part A — stationary at the bike (gate before moving)

Reuse `live-gauge-bench-test.md`. Key-on / engine running, **stationary**, and
confirm the same values on the **gauge and the phone** (Zeppl Ride screen +
Dev > Live telemetry):

- Rev -> RPM tracks on the tach and phone `rpm`.
- Warm up -> temperature climbs (`raw - 40` °C).
- Turn signals -> arrows. Neutral -> **N**.
- **Speed 0** (stationary) — correct. `speed_raw` should read 0.

If the phone shows live RPM/temp, the decode -> display -> BLE telemetry chain
(Brick 1) is proven and it's safe to ride.

## Part B — GPS calibration ride (the divisor lock)

1. **Unlock dev mode:** Cluster tab -> tap your cluster -> **Firmware** -> tap
   *Version* seven times.
2. **Dev tab -> your connected cluster -> Speed calibration -> Start.** Grant
   location when prompted.
3. **Ride and vary speed** across roughly **15-55 mph (25-90 km/h)**. Hold a
   few steady speeds for several seconds each; a spread of points fits a better
   slope than one cruise speed. The wizard samples `(GPS mph, speed_raw)` once a
   second, drops sub-10 mph noise, and needs >=5 usable samples.
4. Watch the card: **Solved divisor** + **Fit error (RMS mph)** appear once
   enough samples land. Aim for **RMS < ~2 mph** and **>=8-10 samples** across
   the range.
5. **Finish -> Apply to cluster.** This writes the divisor over BLE; the cluster
   applies it live (`speed_mph = speed_raw / divisor`) and persists it to NVS.
6. **Sanity:** the solved divisor should land ~**180-200** (physics ~188, stock
   ~200). If it's wildly outside that, the samples were noisy — Redo with
   steadier holds and a clearer GPS view.

## Part C — cross-checks (confidence, not required to ride)

- **Reboot persistence (Brick 3):** power-cycle the cluster, reconnect, confirm
  `speed_mph` still tracks GPS at the new divisor (not back to 195). Validated
  on the bench; re-confirm on the real value.
- **Offline re-derivation:** the ride log stores raw counts (`speed_raw=`), so
  the divisor is independently re-derivable from the capture
  (`tools/ride_log_pull.py` + `tools/j1850_report.py`). Compare it to the
  wizard's number — they should agree within a couple of percent.
- **Stock speedo** (if the proxy box keeps it alive): eyeball-compare at a
  couple of held speeds.

## Part D — the rest of the live stack

- **Gear derivation:** N at a stop; 1-6 should settle sensibly under way
  (`gear = argmin |rpm/speed - ratio|` over the exact overall ratios). No
  sensor on this bike, so it's inferred — spot-check it feels right per gear.
- **Fuel economy (Brick 4) — start the baseline at the fuel stop.** The
  mL/tick calibration needs *two* fills bracketing a known burned amount, so a
  single stop can only **start** it. At this ride's fuel stop: **fill the tank
  and reset Trip 1** (cluster Trip settings) at the pump — that zeroes the fuel
  tick counter for the tank you're about to burn. Ride the tank down; at the
  **next** fill-up, open the Zeppl Ride screen's **Fuel** card, enter the litres
  added, and Save — that solves mL/tick (litres / Trip-1 ticks) and unlocks
  trip economy (mpg / L/100km) + range-to-empty. Until then the Fuel card shows
  the level bar and prompts to calibrate.

## Safety rails

- Phone mounted or pillion-operated; **no handheld interaction underway**.
- Firmware is **RX-only** (no TX compiled) — it physically cannot put anything
  on the bus or upset the stock cluster.
- **BLE drops** (occasional `status=19` peer-terminate / `status=8` supervision
  timeout) auto-recover in <11 s. A drop mid-ride does **not** lose the divisor
  once Applied — it's in NVS. If the link is down when you hit Apply, reconnect
  and re-send.

## Success criteria

- Solved divisor Applied and **persisted** (survives a reboot).
- `speed_mph` within a few percent of GPS across the 15-55 mph range.
- RPM, temperature, turn signals, N, and gear all live and sane on **both** the
  gauge and the phone.

## Post-ride

1. Record results in **`firmware/docs/ride-2-findings.md`**: solved divisor,
   RMS, sample count, cross-check deltas (wizard vs offline re-derivation vs
   stock).
2. **Promote the locked divisor** for fresh flashes (this unit's NVS already
   holds it): update `SETTINGS_SPEED_DIVISOR_DEFAULT` in
   `main/settings/settings.h` and `J1850_SPEED_DIVISOR` in
   `main/j1850/j1850_parse.h`, and replace the "provisional 195" notes in
   `ride-1-findings.md` and the Phase 3 plan with the locked value.
3. Turn `VROD_RIDE_LOG_DUMP` on only to pull the capture, then back **off**
   (it blocks boot ~1 min).
