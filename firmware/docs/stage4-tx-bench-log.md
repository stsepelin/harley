# Stage 4 — J1850 TX high-side bench bring-up log

Running log for the DC bench bring-up of the TX high-side stage on breadboard.
**BENCH ONLY — no bike.** This is the canonical record of the Stage 4 TX
transceiver bench ladder (Phase 3, `../../docs/03-PHASE3-J1850-PLAN.md`).

## Board under test

RX front end + TX cascade on breadboard:

- **RX front end** (`../../docs/schematics/j1850_rx.svg`): R1 10k / R2 4.7k
  divider, D1 7.5V zener with the band (cathode) to the BUS node (clamp).
- **TX cascade** (`../../docs/schematics/j1850_tx.svg`): R3 1k, R4 10k,
  R5 100Ω, R6 10k, Q1 IRLZ44N (logic-level N-FET), Q2 2N2907A (PNP high-side).
- **Bench-only additions** (not on the bike, stand in for the vehicle):
  - **Rg 10k** gate -> GND pull-down. Holds Q1 off while the P4 TX pad is
    high-Z (boot/reset). Now also drawn in `j1850_tx.py` as the watchdog
    "layer 3" hardware backstop.
  - **Rpd 10k** bus -> GND pull-down. Defines the recessive (LOW) rest level
    on the bench, where no vehicle is present to hold it. **Bench-only — never
    added to any schematic** (a pull-up here was the old inverted-bus mistake).
- TX pin = **GPIO 24**.
- P4 powered by **USB**. Only **GND** shared with the transceiver.
  **+12V never reaches the P4.**

## Test ladder

| Step | Test | Status |
|------|------|--------|
| Ring-out | DMM pre-power: shorts / jumpers / resistor values | PASS |
| Test A | Quiescent smoke test, TX gate undriven | PASS |
| Test B HIGH | Manual 3V3 -> gate | PASS |
| Test B LOW | Manual gate low (expect BUS ~0V, I ~0) | PASS |
| Test C | Firmware pin-wiggle on GPIO 24 | PASS |
| Step 2 | TX firmware watchdog self-test + loopback self-sniff | PASS (fix verified; fix + diagnostics uncommitted) |

### Ring-out (DMM, pre-power) — PASS
No shorts, jumpers good, resistor values in place.

### Test A — quiescent smoke test (TX gate undriven) — PASS
+12V rail present, BUS ~0V, current ~0. No exact numbers recorded.

### Test B HIGH — manual 3V3 -> gate (breadboard row 2) — PASS
Manual 3V3 onto row 2 (= Q1 gate). Measured: **BUS 8.0V**, **node_B 2.53V**,
**gate 3.02V**, PSU **12V / 0.042A / CV**.

Interpretation:
- Confirms Q1 switches, Q2 emitter/collector orientation correct, zener
  polarity correct, RX divider correct.
- **BUS ~8.0V** is the normal zener rise at ~40 mA (D1 7.5V zener, not a fault).
- **node_B 2.53V < 3.3V** — the RX divider node the P4 sees is P4-safe.

Note: PSU current limit was first misset to ~10 mA -> supply went into **CC**
and the rail sagged to **8.47V**; raised to 100 mA (-> CV) and the numbers
above are the corrected reading.

### Test B LOW — PASS
Gate driven low: BUS ~0V in the LOW phase (Q1 off -> R6 holds Q2 off -> bus
released). Q2 confirmed switching cleanly HIGH<->LOW with the gate.

### Test C — firmware pin-wiggle (GPIO 24) — PASS (2026-07-17)
Goal: firmware toggles GPIO 24 between 0V and 3.3V every 2.5s, logging each
transition, to confirm the P4 drives the TX gate line under firmware control.

Build config (TEMPORARY, local sdkconfig only — not committed):
- `CONFIG_VROD_PIN_WIGGLE_GPIO=24`
- Sniffer/J1850 stack disabled for a clean wiggle-only test (see restore note).

**Firmware half — PASS.** Built clean (42% app partition free), flashed +
verified on `/dev/cu.usbmodem5B5F0299541`. Serial capture shows 8 consecutive
transitions, alternating 3.3V/0V at exactly 2500 ms spacing:

```
I (27855) wiggle: GPIO24 -> 3.3V
I (30355) wiggle: GPIO24 -> 0V
I (32855) wiggle: GPIO24 -> 3.3V
I (35355) wiggle: GPIO24 -> 0V
I (37855) wiggle: GPIO24 -> 3.3V
I (40355) wiggle: GPIO24 -> 0V
I (42855) wiggle: GPIO24 -> 3.3V
I (45355) wiggle: GPIO24 -> 0V
```

**Bench-probe half — PASS.** With the wiggle running, DMM confirmed all three
nodes swinging in step with the 2.5s toggle:
- gate (row 3): 0 <-> **3.0V**
- BUS (row 13): 0 <-> **7.9V**
- node_B (row 15): 0 <-> **2.49V** (< 3.3V, P4-safe)

Auto-range artifacts (ignore): the auto-ranging DMM showed transient "peaks"
during the edges (BUS ~12.26V, node_B ~4V). These are not real — BUS cannot
exceed the 12V rail (sourced through R5, zener-clamped ~8V), there is no
inductance so no overshoot, and the steady node_B (2.49V) is the P4-safe value.

The P4 drives the TX gate line and the whole high-side cascade follows:
GPIO HIGH -> gate 3.0V -> Q1/Q2 on -> BUS 7.9V; GPIO LOW -> BUS 0V.

### Step 2 — TX watchdog self-test + loopback self-sniff — PASS (breadboard 2026-07-17; fabricated PCB 2026-07-19)

Build config (TEMPORARY, local sdkconfig only — not committed):
- `CONFIG_VROD_J1850_TX=y`, `CONFIG_VROD_J1850_TX_GPIO=24`
- `CONFIG_VROD_J1850_SNIFFER=y`, `CONFIG_VROD_J1850_TX_SELFTEST=y`
- `CONFIG_VROD_J1850_GLITCH_NS=0`, `CONFIG_VROD_PIN_WIGGLE_GPIO=-1`
- `CONFIG_VROD_J1850=n`, `CONFIG_VROD_RIDE_LOG=n` (focused self-sniff build)

Built clean (41% app partition free), flashed + verified on
`/dev/cu.usbmodem5B5F0299541`, captured from a fresh hard-reset.

Serial (verbatim):
```
I (2781) j1850tx: TX ready on GPIO24 (high-side, HIGH=dominant); watchdog cutoff 300 us
I (3292) j1850tx: watchdog trigger test: PASS (faulted=1, line=LOW)
W (3324) j1850tx: self-sniff FAIL: sent=0 decoded=0 rx [(none)] CRC BAD [TX FAULT]
... (every self-sniff frame identical) ...
I (5414) j1850tx: self-sniff tally: 0 pass, 4 fail
I (7534) j1850tx: self-sniff tally: 0 pass, 8 fail
I (47814) j1850tx: self-sniff tally: 0 pass, 84 fail
```

Observed (not yet interpreted — awaiting bench confirmation):
- Watchdog trigger test line = **PASS** (layer-2 gptimer ISR detached the pad
  and drove it LOW; `faulted=1`).
- **Every** self-sniff frame = **FAIL**, `sent=0` / `[TX FAULT]`, 0 decoded.
  Tally never leaves **0 pass** (84 fail after 48s).
- `sent=0` + `[TX FAULT]` after a watchdog test that latched `faulted=1`
  suggests the TX fault latch set by `wd_selftest()` is not being cleared
  before the self-sniff loop, so no frame is ever transmitted. HYPOTHESIS ONLY
  — flagged for the bench operator; root cause not confirmed. This path
  (RMT/watchdog re-init glue in `j1850_tx.c`) is out of host-test scope.

Build left on the board; full J1850/ride-log config NOT restored (per instruction).

#### Diagnosis (2026-07-17) — root cause found: `j1850_tx_reset()` fails to clear the latch

Temporary diagnostic logging added to `j1850_tx.c` (local only, NOT committed):
post-wd_selftest fault state, send entry-guard hits, post-transmit err/fault,
and the `j1850_tx_reset()` `rmt_tx_switch_gpio` result. Serial from a fresh
hard-reset (verbatim, boot + first cycle):

```
I (3276) j1850tx: watchdog trigger test: PASS (faulted=1, line=LOW)
W (3278) j1850tx: reset: switch_gpio err=ESP_ERR_INVALID_STATE
I (3284) j1850tx: post-wd_selftest: faulted=1
W (3288) j1850tx: send guard hit: ready=1 faulted=1
W (3322) j1850tx: self-sniff FAIL: sent=0 decoded=0 rx [(none)] CRC BAD [TX FAULT]
W (3822) j1850tx: send guard hit: ready=1 faulted=1
W (3852) j1850tx: self-sniff FAIL: sent=0 decoded=0 rx [(none)] CRC BAD [TX FAULT]
```

**Hypothesis A CONFIRMED.** The chain:
1. `wd_selftest()` deliberately trips the watchdog -> `s_faulted=1`, line LOW (PASS).
2. It calls `j1850_tx_reset()` to recover, but `rmt_tx_switch_gpio(s_chan,
   TX_GPIO, false)` returns **`ESP_ERR_INVALID_STATE`**, so reset bails early
   and **never clears `s_faulted`**.
3. `post-wd_selftest: faulted=1` -> latch still set.
4. Every `j1850_tx_send()` hits the entry guard (`ready=1 faulted=1`) and
   returns false BEFORE any `rmt_transmit`. No frame is ever keyed
   (`sent=0`); the `send after tx` diagnostic never prints. This rules out
   Hypothesis B (RMT re-tripping the watchdog).

Root cause is the `rmt_tx_switch_gpio` -> `ESP_ERR_INVALID_STATE` in the fault
recovery path — the watchdog ISR detached the pad via
`esp_rom_gpio_connect_out_signal` (not through RMT), and the switch-back call
rejects the current channel state. Diagnostic logging is temporary and must be
reverted before any firmware commit.

#### Fix + re-test (2026-07-17) — Step 2 GREEN

ESP-IDF 6.0.1 `driver/rmt_tx.h` documents the required state for the switch:
`ESP_ERR_INVALID_STATE: Switch GPIO failed because channel is not disabled`.
Our channel is left enabled after init, so the recovery hit exactly that.

Fix in `j1850_tx_reset()` (uncommitted, pending approval): bring the channel
into the required state around the switch --
`rmt_disable(s_chan)` -> `rmt_tx_switch_gpio(s_chan, TX_GPIO, false)` ->
`rmt_enable(s_chan)` -- with graceful error returns (no `ESP_ERROR_CHECK`
panic), the pad held LOW (recessive) throughout, and `s_faulted` cleared only
on full success.

Re-test, same Step-2 config, fresh hard-reset, verbatim:
```
I (3291) j1850tx: watchdog trigger test: PASS (faulted=1, line=LOW)
I (3291) j1850tx: reset: cleared
I (3291) j1850tx: post-wd_selftest: faulted=0
I (3436) j1850tx: send after tx: err=ESP_OK faulted=0
I (3474) j1850tx: self-sniff PASS: rx [68 FF 40 03 D8 ] CRC OK
I (4069) j1850tx: self-sniff PASS: rx [68 FF 60 03 AD ] CRC OK
I (4604) j1850tx: self-sniff PASS: rx [29 FE 40 01 64 ] CRC OK
I (5139) j1850tx: self-sniff PASS: rx [29 FE 60 01 11 ] CRC OK
I (5639) j1850tx: self-sniff tally: 4 pass, 0 fail
```

- `reset: cleared` (was `switch_gpio err=ESP_ERR_INVALID_STATE`).
- `post-wd_selftest: faulted=0` (latch now cleared).
- Every send: `send after tx: err=ESP_OK faulted=0` (transmit path reached; no
  guard-hit, no re-trip -> also positively confirms Hypothesis B was NOT in play).
- All four IM keep-alive frames round-trip with CRC OK; tally 4/0, 8/0, 12/0,
  16/0, 20/0 (100% pass). Bench RX jumper confirmed connected.

**Step 2 acceptance ladder items (1) watchdog trigger test and (2) bench
self-sniff both PASS.** Remaining before the bike: on-bike with stock cluster
attached (no DTCs), then the stock-cluster-removal / U1255 / TSSM checks.

The temporary diagnostics have been reverted; the `j1850_tx_reset()` fix is
committed on its own (see the `fix(j1850)` commit on this branch).

#### PCB re-test (2026-07-19) — Step 2 GREEN on the fabricated board

Re-ran Step 2 on the **fabricated PCB** (replacing the breadboard) — same
selftest firmware (`j1850_tx_reset()` fix in), fresh hard-reset, verbatim:
```
I (3273) j1850tx: watchdog trigger test: PASS (faulted=1, line=LOW)
I (3441) j1850tx: self-sniff PASS: rx [68 FF 40 03 D8 ] CRC OK
I (4028) j1850tx: self-sniff PASS: rx [68 FF 60 03 AD ] CRC OK
I (4562) j1850tx: self-sniff PASS: rx [29 FE 40 01 64 ] CRC OK
I (5096) j1850tx: self-sniff PASS: rx [29 FE 60 01 11 ] CRC OK
I (5596) j1850tx: self-sniff tally: 4 pass, 0 fail
   ... all-pass ... tally 32 pass, 0 fail
```

- Watchdog trigger test PASS; all four IM keep-alive frames round-trip CRC OK;
  100% pass (tally 4/0 -> 32/0). The PCB reproduces the breadboard result.
- Because decode is clean, the **recessive level is defined on the PCB** — the
  no-Rpd loopback caveat was handled (bench pull-down tacked on, or the board
  driven with the recessive reference present).

## Config restore note (on command, after Test C)

The wiggle build stays flashed and `sdkconfig` stays as-is until explicitly
told to revert. When told, restore these local sdkconfig values and rebuild:
- `CONFIG_VROD_PIN_WIGGLE_GPIO=-1`
- `CONFIG_VROD_J1850_SNIFFER=y`
- `CONFIG_VROD_J1850=y`
- `CONFIG_VROD_RIDE_LOG=y`
- `CONFIG_VROD_J1850_RX_GPIO=20`

(`sdkconfig.defaults` is tracked and was NOT modified.)

## Notes

- **Discrepancy #1 (closed):** the Stage 4 plan calls the gate pull-down the
  watchdog "layer 3" hardware backstop, but it was missing from
  `j1850_tx.py`. Rg 10k gate -> GND is now drawn there. Rpd (bench bus
  pull-down) stays bench-only and is documented here only.
