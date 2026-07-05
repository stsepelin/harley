# Live-gauge bench test (real J1850 on the display, stationary)

Verify the full chain **bus -> decode -> display** at the bike before any
ride: key-on / engine running but **stationary**, ESP on **laptop USB power**
(no bike power, no ride-log, no microSD). RX front end tapped to the bike's
J1850 pin **7 (signal)** + pin **5 (ground)**, same as the sniffer, into the
divider node on **GPIO 20**.

## The one flag that matters

**`CONFIG_VROD_J1850=y`** makes the J1850 decode the live `vehicle_data`
producer instead of the synthetic simulator. `main.c` starts `sim_engine`
only `#if !CONFIG_VROD_J1850`, so turning this on suppresses the sim; the
sniffer's decoded frames flow `j1850_driver -> vehicle_data_set`, and the
gauge UI (which always renders from `vehicle_data`) shows real bus values.
It depends on `CONFIG_VROD_J1850_SNIFFER=y` (the RX capture/decode).

## Build config (menuconfig -> "V-Rod cluster")

| Option | Set | Why |
|---|---|---|
| `VROD_J1850_SNIFFER` | **y** | RX capture + VPW decode |
| `VROD_J1850` | **y** | **the live producer** -> vehicle_data (suppresses the sim) |
| `VROD_J1850_RX_GPIO` | 20 | divider node (default) |
| `VROD_INCLUDE_SIM_ENGINE` | n | sim not needed (it is gated off anyway; n keeps the build clean) |
| `VROD_RIDE_LOG` | n | no SD logging / no card for this test |
| `VROD_J1850_TOGGLE_ISR` | n | keep the shipping pin-read RX ISR |
| `VROD_J1850_TX` | n | RX-only, cannot disturb the bus / stock cluster |
| `VROD_J1850_GLITCH_NS` | 0 | filter off (the tuned default; see the capture notes) |
| `VROD_J1850_ADC_GPIO` | -1 | no amplitude probe |

Build + flash + watch the serial log alongside the gauge:

```sh
cd firmware && . $IDF_PATH/export.sh
idf.py build
idf.py -p /dev/cu.usbmodem5B5F0299541 flash monitor
```

The **ride/gauge screen is the boot screen**; the BENCH diagnostics screen is
still reachable via Settings (sniffer is on) if you want raw frame counts.

## What is LIVE vs not (be honest about the bus)

Decoded from the bus and driven onto the gauge:

| Field | Frame | On screen |
|---|---|---|
| **RPM** | `28 1B 10 02` | tach arc — moves with throttle |
| **Temperature** | `A8 49 10 10` | temp dial — climbs as the engine warms (**raw / provisional scaling**; the point is it *moves*, calibration is later) |
| **Turn L / R / hazard** | `48 DA 40 39` | turn arrows |
| **Neutral** | gear frame `A8 3B 10 03`, `00`=N | gear reads **"N"** (orange) |
| **Check-engine** | `68 88 10` | engine lamp (off unless a fault is set) |

Expected NOT to move in this test:
- **Speed = 0** and **gears 1-6 absent** — stationary, no motion. Correct.
- **High/low beam — NOT on the J1850 bus.** They are discrete 12 V wires
  read locally by the IM (pin 2 White = high beam), confirmed off-bus in the
  2026-07-04 capture; `j1850_parse` does not decode them, so the beam lamp
  will **not** follow the switch here. That is a Phase 6 discrete-line tap,
  not a bug in this build.
- **Fuel gauge, oil, ABS, battery, immobiliser** — also not bus-decoded in
  Stage 3; they sit at their boot/zero state.

## Stationary checklist

1. **Key on, engine off.** Gauge shows **N** (neutral), RPM 0, speed 0. In
   `idf.py monitor` you should see the sniffer frame log + periodic
   `speed:` / `temp:` / `gear:` hint lines — confirms the bus is alive and
   frames decode with good CRC.
2. **Rev the engine.** The **RPM** needle tracks the throttle on the tach.
3. **Let it warm.** The **temperature** reading climbs (raw/provisional
   number — just confirm it rises, don't trust the absolute value yet).
4. **Turn signals.** Left blinker -> left arrow; right -> right; hazards ->
   both.
5. **High beam (expected no-op).** The on-screen beam lamp will **not**
   change — off-bus. Confirm via serial that no beam-related frame appears;
   that is the expected result, and the reason the discrete tap is Phase 6.

Success = RPM, temperature, turn signals, and the neutral "N" all follow the
bike live off the J1850 bus. That proves bus -> decode -> display end to end
before committing to a ride.

## Notes

- **RX-only, safe:** no TX is compiled in, so this build cannot put anything
  on the bus or upset the stock cluster.
- The **sniffer capture build** (same `VROD_J1850_SNIFFER=y` but without the
  producer / with the ride log) stays available separately — this is the
  gauge/live variant, driven by the single `VROD_J1850` producer flag.
- `j1850_vpw.c` and the RX capture path are untouched by this build.
