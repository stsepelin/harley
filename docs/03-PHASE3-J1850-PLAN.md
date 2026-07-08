# Phase 3: J1850 Bus + IM Simulation

> **Status: ⏳ active** (kicked off July 2026 — parts arrived June 2026)
>
> First phase that touches the bike. The gauge UI (Phase 2), all
> off-bike features (Phase 2.5), and the loose ends from both are done;
> everything below runs on the bench transceiver + the bike's 12-pin
> harness.
>
> **GPS was dropped (July 2026):** speed comes off the J1850 bus, so an
> onboard GPS module added a large separate effort (module, antenna,
> NMEA parser, enclosure space, power) for little benefit on a
> gauge-cluster replacement. The NEO-6M path and the speed-camera / POI
> feature that depended on GPS position were removed; a phone GPS over
> the BLE link could refine the speed calibration later if ever wanted.

## Goal

Replace the synthetic driving cycle with real bike data and keep the ECM
happy without the stock instrument module. End state: the cluster shows
real speed / RPM / gear / temp / indicators from the J1850 bus and sends
the IM keep-alive messages itself (no U1255, no TSSM lockout).

## Order of attack

Each stage is independently verifiable and the bike stays rideable
throughout (stock cluster keeps working until Stage 4 testing).

### Stage 1 — Bench transceiver build (hardware) — ✅ RX half complete (July 2026)

> Built on breadboard per `schematics/j1850_rx.svg`: R1=10k, R2=4.7k,
> D1=1N4737A (7.5V 1W), cathode to the bus node. No Q1/Q2 populated.
> Bench-verified (KUAIQU PSU in CV @ 50 mA limit, UNI-T UT125C DMM,
> common ground): 7.00V in → **2.285V** at the GPIO node (expect ~2.2V,
> PASS); 12V through a 330Ω series protection resistor → **7.63V** at
> the bus node (expect ~7.5V, PASS).
>
> TX half (Stage 4) waits on the 2N2907A on order — the kit's 2N2222
> is NPN and cannot serve as Q2. RX stays on the breadboard until the
> full transceiver is validated; permanent perfboard build is deferred
> to Phase 6.

Breadboard the corrected transceiver — rendered schematics in
[`schematics/`](schematics/): [RX front end](schematics/j1850_rx.svg)
(this stage) and [TX stage](schematics/j1850_tx.svg) (Stage 4). The
master plan's "J1850 BIDIRECTIONAL TRANSCEIVER CIRCUIT" section has
the same drawings plus the design notes; the schematic was fixed at
Phase 3 kickoff — the old drawing would jam the bus:

- **Populate RX only first**: 10kΩ/4.7kΩ divider + 7.5V zener. No Q1/Q2
  — physically impossible to disturb the bus while sniffing.
- Bench-check with a PSU: 7V on the bus node must read ~2.2V at the
  GPIO node; 12V injected must clamp at ~7.5V.
- The 6× discrete-signal dividers are 10kΩ/**2.7kΩ** (sized for 14.4V
  charging voltage, not nameplate 12V — see the master plan note).

### Stage 2 — Passive sniff (bike + proxy box, stock cluster in place)

> **✅ LIVE SNIFF DONE (2026-07-04).** Full on-bike capture session —
> ignition-on, discrete inputs, and engine running — all decoded. See
> `firmware/docs/captures/SESSION-2026-07-04.md` for the complete
> record. Headlines:
> - Bus polarity **RESOLVED (2026-07-04): standard VPW** (idle LOW,
>   dominant HIGH → **high-side TX**). The old "needs `RX_INVERT`"
>   reading was the 500 ns glitch filter dropping the recessive edge;
>   with the filter off, decode is clean with no invert flag (546
>   frames, 0 bad CRC). `RX_INVERT` removed, glitch filter defaults off.
>   See the master plan transceiver section + session notes.
> - Decoded live: **RPM** (tracked idle→4125), gear, check-engine,
>   fuel-consumption ticks, turn signals (L/R swapped vs the table),
>   **temp** (raw byte climbs — units PROVISIONAL, see below), **speed**
>   header (mph-native, DIV provisional). ~2% bad CRC even at 5k rpm.
> - **IM keep-alive set identified** (`68 FF 40/60`, `29 FE 40/60`,
>   steady ~2 s) — the Stage 4 replay targets.
> - Beam + neutral are **discrete wires** (pins 2/10), not on the bus.
>   Discrete-tap polarity (Phase 6): **neutral = active-LOW** (0V = N),
>   **turns = active-HIGH**; high beam / oil / ignition are **TBD by
>   measurement** — measure BOTH states, don't hard-code (see the master
>   plan Phase 6 discrete table).
>
> Still open in Stage 2 (all need the bike): a **riding capture** for road
> speed (`48 29 10 02`, native mph vs the stock speedo to fix DIV) + gears 1-6, and a
> **two-point temperature capture** (`A8 49 10 10`, raw byte at cold ~ambient
> and fully warm — the stock cluster shows no temp, so no dial reference).

- T-tap pin 7 (LGN/V, J1850 data) through the proxy box; RX-only
  transceiver → P4 GPIO.
- **VPW symbol codec — ✅ landed** (`firmware/main/j1850/j1850_vpw.c`):
  pure-logic pulse-width decoder (SOF/bit/EOD/EOF classification, IFR
  sections, CRC-8/SAE-J1850) plus the matching encoder for Stage 4 TX;
  encode → decode round-trip tested at 100% line/branch.
- **Sniffer build — ✅ landed** (`CONFIG_VROD_J1850_SNIFFER` +
  `CONFIG_VROD_J1850_RX_GPIO`, default 20): GPIO edge ISR times pulses
  into a queue; a task decodes and logs one line per frame (hex bytes,
  CRC verdict, IFR if present) plus 10 s stats (frames / bad CRC /
  overruns). Read-only build — no TX path compiled in at all.
- **Bench screen — ✅ landed**: sniffer builds add a BENCH sub-page in
  settings (long-press → SETTINGS → BENCH) showing the calibrated RX
  pin voltage + back-calculated bus voltage, live line level, edge
  count, frame/CRC/overrun counters, and the last decoded frame — the
  Stage 1/2 checks without a laptop or DMM attached. (The ADC readout
  briefly crash-looped the board; root cause was pre-scheduler heap
  margin, fixed in `sdkconfig.defaults` — see the addendum in
  `firmware/docs/ble-bringup-bisect.md`. Phase 6's fuel ADC is
  unblocked by the same fix.)
- **Pin choice resolved**: GPIO 20 (J1850 RX) is confirmed broken out on
  the 40-pin header and unclaimed (GPIO 21 was the GPS input and is now
  free) — full pin budget, header silkscreen map, and the GPIO 22 =
  fuel-ADC reservation live in `firmware/docs/PINS.md`. For physical
  confirmation, set `VROD_PIN_WIGGLE_GPIO` in menuconfig and DMM-probe
  the header (0V↔3.3V every 2.5 s).
- Remaining in this stage: two-wire link (divider node → GPIO 20, GND
  → GND), repeat the 7V→~2.2V check measured *at the ESP pin*, then
  T-tap and capture 5+ minutes each of ignition-on / idle / riding
  into `firmware/docs/captures/` (procedure + naming in its README).
- Deliverables: a capture corpus checked into `firmware/docs/captures/`
  (small text files), and the IM-originated message set identified —
  what the ECM expects to keep hearing. Specifically confirm whether
  the fuel-gauge broadcast (`A8 83 61 12`) is IM-originated: the 2009
  fuel sender is ultrasonic and wired to the IM, so that message
  likely dies with the stock cluster (see the master plan's Phase 6
  fuel-sender caveat for the fallback strategies).

### Stage 3 — Decode → vehicle_data producer

- Pure-logic message parser (`j1850_parse.c`): decode table from the
  master plan (HarleyDroid-derived). Host tests against real captured
  frames from Stage 2 — same fixture pattern as `phone_protocol`.
- **Two decode entries still blocked on the bike** (`j1850_parse.c` marks
  both provisional; the sniffer logs raw hints for the capture):
  - **Speed `48 29 10 02` — units SETTLED (mph-native), magnitude
    provisional.** US-market bike → stock cluster reads miles → the ECM
    value is mph-based; km/h is a display-layer conversion, NOT decode
    (don't bake it into the parser). `vehicle_data.speed_mph` is now
    mph-canonical throughout (the sim producer converts to mph, and
    `units_speed_display` does mph→km/h only when the user selects metric),
    so the parser stores the raw value with no conversion. Only the `/DIV`
    scale (128 guess) is open — see the **speed-calibration reference**
    below for the method.
  - **Temp `A8 49 10 10` — FULLY PROVISIONAL.** The stock cluster shows
    NO temperature, so there is no dial to check against; the raw byte's
    meaning is unconfirmed. Candidates: raw°C / raw°F / `raw-40=°C`
    (HarleyDroid's wiki documents `°C+40` = the offset form, but it's a
    HINT — its turn bits were wrong). Resolve with a **two-point capture**:
    raw byte cold (~ambient) + fully warm (~90-100°C) fixes scale+offset.
  - Gear ladder (1-6) is also ride-confirmed but tracked separately.
- `j1850_driver.c` glue: RMT RX → vpw decode → parse → `vehicle_data_set`.
  New Kconfig `VROD_J1850`, mutually exclusive producer with the sim
  (`sim_engine` is not started when `VROD_J1850` is on — both write
  `vehicle_data`).
- UI, tests, simulator all unchanged — that's what `vehicle_data_t`
  was for.

### Stage 3.5 — On-board ride log (laptop-free capture)

Speed DIV, the temperature formula, and gears 3-6 can only be resolved
while moving / warming up — where a laptop can't ride along. The ride log
persists every decoded frame to the board's microSD card so those captures
run headless.

- **Enable:** `CONFIG_VROD_RIDE_LOG=y` (depends on the sniffer). Off by
  default; it's part of the capture build alongside `VROD_J1850_SNIFFER`.
- **Storage:** microSD via 4-bit SDMMC (GPIO 39-44 + LDO power, see
  `firmware/docs/PINS.md`), FAT filesystem, mounted at `/sdcard`. A 7 MB
  `storage` SPIFFS partition also exists in `partitions.csv` as a card-free
  fallback, not currently wired.
- **Coexists with BLE.** The P4 has one SDMMC controller with two slots; the
  ESP32-C6 radio (esp_hosted) owns it on slot 1, and the microSD is on slot 0.
  The card is mounted on slot 0 with `host.init`/`host.deinit` stubbed so the
  driver reuses the controller esp_hosted already created instead of failing
  with "no available sd host controller" (IDF>=6.0 allows one creation only —
  esp-idf#16233; mirrors the `esp_hosted` `host_sdcard_with_hosted` example).
  So the ride log records with the phone link still up — no radio-off build.
  Verified on hardware: SD mounts + BLE advertises, and a session file
  survives an abrupt power-off (FAT intact on the next boot).
- **Control:** a REC/STOP toggle on the BENCH screen with a live indicator
  (state, frame count, dropped count, MB used/total). No laptop needed.
- **Format:** one line per frame, `<sec.ms> j1850: HH .. | CRC OK | <decoded>`
  — the same shape `tools/j1850_capture.py` / `j1850_report.py` parse, so
  the pulled file runs through the report tool unchanged. The decoded suffix
  carries the three capture fields: **native speed** (mph, for the stock-speedo
  compare), **raw temp byte** (units still provisional), and **gear raw+ladder**.
- **Fault-tolerant:** writes are buffered and flushed in a low-priority task
  (off the decode path, off the LVGL core). No card / card full / write
  error stop logging cleanly and show on the indicator; the gauge is never
  affected. A whole line is dropped (counted) rather than half-written when
  the buffer backs up.
- **Retrieval:** power off, pull the card, `tools/j1850_report.py
  /sdcard/ride_NNN.log` (or grep `speed=` / `temp=` / `gear=` directly).

> **Prerequisite for the ride (hardware, out of scope here):** laptop-free
> capture needs the ESP powered from the mini560 buck off switched +12V,
> sharing ground with the J1850 transceiver — NOT USB. The ride-log firmware
> is useless without bike power. Wiring is a Phase 6 / bench-harness step;
> flagged here so it isn't forgotten before a capture ride.

#### Speed DIV (/128) calibration reference

The reference is the **STOCK SPEEDOMETER**, which is mechanically driven by
the J1850 bus (there is **no onboard GPS**). Method: ride at a steady speed
and compare the logged native speed from `vehicle_data` (the `speed:` line /
ride-log `speed=` field) against the stock speedo read **in its native
MILES** — ignore the km/h sticker, the mechanism reads mph. Match across
~30 / 50 / 70 and correct `J1850_SPEED_DIVISOR` if off by a clean factor.
Note the stock speedo may read ~5-10% optimistic (typical), so this is a
**coarse** calibration; a phone GPS (over the BLE link) could refine it
later if ever desired.

### Stage 4 — IM simulation + TX

> **TX firmware landed (2026-07): driver + watchdog + self-sniff loop.**
> `CONFIG_VROD_J1850_TX` compiles `j1850_tx.c` (RMT-timed VPW from the
> host-tested `j1850_vpw.c` encoder) + `j1850_tx_logic.c` (pure: CRC
> frame build + the dominant-time guard, host tests at 100%). No TX
> hardware is built yet — build the transistor stage
> (`schematics/j1850_tx.svg`) only after the self-sniff loop below passes.

**Polarity (settled): standard VPW, high-side.** TX GPIO **HIGH = bus
dominant** (Q1 → Q2 sources ~7V); **LOW = recessive** (released). This is
the reverse of the old inversion-era note — the jam condition is
stuck-**HIGH**, not stuck-low.

**Watchdog (mandatory, implemented).** Three layers, none relying on the
TX task loop:
1. Pre-transmit pure guard (`j1850_tx_stream_within_limits`) refuses any
   stream whose active symbol exceeds `J1850_TX_DOMINANT_MAX_US` (300 µs,
   margin over the 200 µs SOF). The encoder never emits one; a failure
   means upstream corruption.
2. An independent gptimer samples the (input-enabled) TX pad every 50 µs;
   a dominant that outlasts 300 µs makes its ISR detach the pad from RMT
   and drive it LOW — ISR-safe register writes, fires even if the task
   hangs. Latches a fault; TX stays disabled until re-init.
3. RMT `init_level` / `eot_level` = LOW and a boot-time output-LOW keep
   the line recessive before, between, and after frames; the hardware
   gate pulldown (Q1 gate → GND) backs it up.

**TX GPIO: `CONFIG_VROD_J1850_TX_GPIO`, default 24** — the first free
header pin (see `firmware/docs/PINS.md`), clear of RX (20) and
fuel-ADC (22). Confirm the physical hole with `VROD_PIN_WIGGLE_GPIO`
before soldering.

**Bench self-sniff wiring (recessive = LOW).** With no vehicle, nothing
else defines the recessive state, so the bench must:
- Tie the **TX collector output (after R5) to the RX bus node** — the
  same node the RX divider taps (`schematics/j1850_rx.svg`).
- Add a **bus pull-down to GND** (≈10 kΩ) at that node so the line rests
  **LOW = recessive** when Q2 is off; Q2 turning on pulls it **HIGH =
  dominant**. (On the bike the other nodes hold recessive; the bench
  pull-down stands in for them. Do NOT add a pull-up — that was the
  inverted-bus mistake.)
- Common ground between the PSU, the transceiver, and the P4.
- Bench-check first with a PSU/DMM: TX GPIO HIGH → ~7 V at the bus node,
  idle → ~0 V. The 7.5 V zener (RX drawing) clamps the driven level.
- Optionally watch the **dominant→recessive fall** on the GPIO 22 ADC
  probe: the passive recessive fall is slow, so confirm it settles LOW
  between symbols — if not, the bench pull-down is too weak.

**Self-sniff validation (`CONFIG_VROD_J1850_TX_SELFTEST`).** The TX task
emits the IM keep-alive set; the unchanged RX sniffer decodes it off the
shared node; each frame logs one line:
```
j1850tx: self-sniff PASS: rx [68 FF 40 03 D8 ] CRC OK
j1850tx: self-sniff FAIL: sent=1 decoded=0 rx [(none)] CRC BAD [TX FAULT]
```
plus a periodic `self-sniff tally: N pass, M fail`. **Every frame must
PASS before the bike.**

- Replay the Stage 2 IM message set at captured intervals (the same
  keep-alive set the self-sniff emits).
- Test ladder: **(1)** watchdog trigger test — on boot the selftest build
  runs `wd_selftest()`, which drives the pad dominant BYPASSING RMT + the
  pre-transmit guard and confirms layer 2 (the gptimer ISR) detaches the
  pad and drives it LOW; must log `watchdog trigger test: PASS` (this is
  also the acceptance test for the P4 pad readback, so a FAIL means the
  watchdog is blind — stop) → **(2)** bench self-sniff — emit → own
  sniffer decodes, CRC-valid, every frame PASS → **(3)** bike with stock
  cluster still attached (no DTCs while both talk) → **(4)** later,
  separately: disconnect stock cluster → U1255 / TSSM lockout checks →
  full key fob unlock → start → ride → stop. One variable at a time; (1)
  and (2) gate the bike.
- Fallback if TSSM security fails without the stock IM: keep the stock
  IM wired in parallel under the airbox (master plan option C).

### Carried-over hardware verification (from Phase 2.5)

Run through once while the board is on the bench anyway:

- **Stage 8 E2E record** — the 4-step directed-advertising checklist
  in the Phase 2.5 plan.
- **Auto-reconnect** — power-cycle the cluster mid-connection; the
  companion must reach `Connected` again without taps (it arms a
  background `connectGatt` on link loss). Deliberate disconnects from
  either side must NOT auto-heal.
- **iOS scope decision** — still open; recommendation remains
  Option A (defer ANCS/AMS to Phase 4).

## Test policy

Same as everywhere: pure logic (`j1850_vpw.c`, `j1850_parse.c`,
`ride_log_format.c`) goes in `vrod_pure` at 100% line/branch, with the
lcov filters + policy table updated in the same commit. RMT/ISR/task glue
is validated on hardware.

## Safety rails

- RX-only hardware until the decoder is trusted; TX populated last.
- Never enable TX firmware and the stock cluster's removal in the same
  step — one variable at a time toward the U1255 test.
- The bike must remain fully revertible to stock at every stage
  (that's the proxy box's whole job).
