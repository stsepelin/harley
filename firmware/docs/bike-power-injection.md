# Bike power injection — protected 12V → 5V chain

How to power the Waveshare ESP32-P4-WIFI6-Touch-LCD-3.4C from a **mini560 buck
(12V→5V)** off a 2009 Harley V-Rod, safely, with a USB-C data cable still
connected. This is the first vehicle power tap for this project — the design is
deliberately conservative and every part is explained.

Schematic: [`../../docs/schematics/bike-power-chain.svg`](../../docs/schematics/bike-power-chain.svg)
(regenerate from `bike-power-chain.py`; see that dir's README).

Prerequisite for the **ride-log** (`live-gauge-bench-test.md` needs USB, but a
real ride needs bike power) and any permanent install.

## Injection point (settled) — and why it needs my own reverse block

Power injects at the board's **40-pin header 5V / GND pins** (soldered feed —
the USB-C connector is fragile under vibration and is left free for data).

Verified from the board schematic (`../waveshare-reference/hardware/schematics/`):
the header 5V (J8 pin 40, both "5V" pads) sits on the board's common **`VCC_5V`**
rail **after** the board's on-board USB-C reverse protection (the AO3401 +
MMDT3906DW ideal-diode stages on each Type-C port). The header itself has only a
TVS ESD clamp (D3 SMF5.0CA on the board) — **no series reverse block**. So the
mini560, fed at the header, gets no reverse protection from the board. **I add my
own** (D4 below).

That also makes USB-data + mini560-power coexist safely: **D4 blocks back-current
into the mini560**, and the **board's own AO3401 blocks back-current into the
laptop**. Two 5V sources simply OR at `VCC_5V`; the higher one supplies, neither
back-feeds the other.

## Current budget (everything sizes off this)

At 5V, board side. Not all peaks coincide (WiFi TX burst + audio chirp + full
backlight at once is the rare worst case):

| Load | Typical | Peak |
|---|---|---|
| ESP32-P4 (dual RISC-V + peripherals) | ~250 mA | ~400 mA |
| ESP32-C6 (WiFi6 / BLE coprocessor, SDIO) | ~120 mA | ~350 mA (TX burst) |
| 3.4" round MIPI-DSI panel + backlight | ~150 mA | ~250 mA (full bright) |
| Audio codec + speaker PA | ~20 mA | ~400 mA (chirp transient) |
| GT911 touch + RTC + LEDs + misc | ~30 mA | ~50 mA |
| J1850 RX front end (TX later, brief) | ~5 mA | ~50 mA |
| **Board @ 5V** | **~0.6 A** | **~1.5 A realistic** |

**Design figures (rounded up for margin): ~1.0 A continuous, ~2.0 A peak @ 5V.**
Through the mini560 (~88% efficient) that's **~0.5 A continuous / ~1.0 A peak on
the 12V side** (less at 14.4V charging). Everything below is rated with ≥2×
margin on these.

## Parts list

| Ref | Part | Rating | Package | Why this rating |
|---|---|---|---|---|
| **F1** | Automotive **blade fuse 2 A** + inline holder | 2 A, 32 V | ATM/mini | ~1 A peak on 12V → 2 A = 2× margin. Protects the **feed wiring** against a short (bike battery sources huge fault current), not just the load. Place at the tap. Blade fuses are slightly slow → tolerate the mini560 inrush. Bump to 3 A only if inrush nuisance-blows. |
| **D2** | **Schottky, series reverse-polarity** — SB560 (60 V/5 A) or SS54 (40 V/5 A) | ≥40 V, ≥3 A | DO-201AD / SMC | Blocks a swapped +12V/GND at the mini560 input. ≥3 A ≫ 1 A load; 60 V (SB560) adds load-dump headroom on this part. ~0.4 V drop / ~0.4 W @ 1 A on the 12V side = negligible. **Alt (low-loss): a P-channel MOSFET** (see below). |
| **D3** | **TVS, unidirectional — SMBJ16A** (across the mini560 12V input) | 16 V standoff, ~26 V clamp @ I_PP, **600 W** | SMB (DO-214AA) | Clamps load-dump / regulator spikes **below the mini560's ~28 V max**. 16 V standoff stays off through the ~15 V charging spike; ~26 V clamp keeps a safe margin under 28 V. For more energy margin (severe load-dump) use **1.5KE16A** (1500 W). |
| **mini560** | MP1584-class **12V→5V buck module**, output trimmed | Vin 4.5–28 V, Iout ~3 A | module | Set the output so the board sees ~5.0 V **after D4** (below). |
| **D4** | **Ideal-diode module (recommended)** or **SS34 Schottky** (fallback) | module / 40 V, 3 A | — / SMA (DO-214AC) | Gives the mini560 the reverse block the header lacks (blocks `VCC_5V` → mini560). **Ideal-diode module:** ~20–50 mV drop → set the mini560 to exactly **5.0 V**, no compensation math. **SS34:** cheaper/simpler but ~0.35 V drop @ 1–2 A → set the mini560 to **5.35 V** to compensate (see setpoint). |

### Reverse-polarity: Schottky vs P-FET (D2)
- **Series Schottky (recommended for a first build):** one part, foolproof, no
  gate network. Costs ~0.4 V / ~0.4 W on the 12V side — irrelevant here.
- **P-channel MOSFET** (source→+12V, drain→load, gate→GND via 10 kΩ, gate-source
  clamped by a 12–15 V zener; pick a ≥40–60 V, low-R_DS(on) P-FET): near-zero
  drop, no heat, but more parts and easier to wire wrong. The low-loss upgrade.

### Output reverse-block: ideal-diode vs Schottky (D4)
- **Ideal-diode module (recommended for a first build):** an LM74700-Q1
  controller + N-FET, or a prebuilt "ideal diode" board. ~20–50 mV drop, so the
  mini560 stays at exactly **5.0 V** — **no setpoint-compensation math, nothing
  to trim to hit 5.0 V at the board.** The safest first-timer choice.
- **SS34 Schottky (simpler-parts fallback):** one cheap part, no module — but
  the ~0.35 V drop means you must set the mini560 to **5.35 V** to compensate,
  and verify the result at the board *under load* (the drop grows with current).

### mini560 output setpoint
- **Ideal-diode module (recommended):** set the mini560 to exactly **5.0 V** —
  the module's drop is negligible, so that is what the board sees. No math.
- **SS34 Schottky:** set the mini560 to **5.35 V**, then **verify the voltage AT
  THE BOARD (after D4) UNDER REAL LOAD and re-trim if needed** — the Schottky
  drop grows with current (~0.35 V @ 1.5 A, a bit more near 2 A), so the setpoint
  must be dialed so it lands at ~5.0 V with the board actually running. Expect
  ~5.05 V light / ~5.0 V @ 1.5 A / ~4.9 V @ 2 A — all inside 4.75–5.25 V.
- **Set and verify with a DMM BEFORE connecting the board** (an over-voltage on
  first connect can kill it).

### Buy as a kit vs single
- Schottkys (SB560/SS34) and the TVS (SMBJ16A) are pennies — buy a small
  **Schottky assortment** + **TVS assortment** so you have the exact values and
  spares. Blade fuses + holder come as an automotive kit. The mini560 is a single
  ~€1 module. If you go the ideal-diode route, buy one prebuilt module.

## Bench-test procedure (before touching the bike)

Do this on a **bench PSU**, never the bike, with the board **disconnected** until
the very end.

1. **Build the chain minus the board.** Bench PSU → 12.0 V, current-limit ~1.5 A,
   into F1.
2. **Trim the mini560.** Measure the mini560 output (before D4) with a DMM;
   adjust the trimpot to **5.35 V** (or 5.0 V for an ideal-diode module).
3. **Check after D4** (open-circuit at the header wire): ≈ setpoint (~0 V drop at
   ~0 A). It drops under load — that's expected.
4. **Load test — verify ~5.0 V AT THE BOARD, UNDER LOAD.** Add a realistic load
   (a 5 Ω / 5 W resistor ≈ 1 A, or a USB load tester) at the header end. Measure
   **after D4, under load**: with the SS34 fallback the Schottky drop grows with
   current, so confirm the 5.35 V setpoint actually lands at **~5.0 V at the
   board** at ~1–1.5 A and **re-trim if it's off**; ramp briefly to ~2 A and
   re-check. (An ideal-diode module should already read ~5.0 V.) Check
   D2 / D4 / mini560 for excess heat.
5. **Reverse-polarity test.** Swap the PSU leads at F1 — output must stay **0 V**
   (D2 blocks), no smoke, fuse intact. Restore polarity.
6. **Load-dump test (optional, careful).** Briefly step the input to ~20 V — the
   mini560 input node must clamp (D3) and stay < 28 V; the 5 V output holds. Skip
   if unsure; D3 is a static safety net regardless.
7. **Common-ground check.** DMM continuity: bike-GND / mini560 GND-in / GND-out /
   board GND (and later the J1850 pin-5 ground) are all one continuous node — a
   single reference, no loops.
8. **Only after 1–7 pass:** connect the board (setpoint already verified) →
   confirm it boots on bench 12 V. Then move to the bike.

## Wiring notes

- **Tap ignition-SWITCHED +12V — IM connector pin 6 (Grey) — NOT constant (pin
  1).** The cluster then powers with the key; no parasitic battery drain. Verify
  with a DMM first: pin 6 should read ~0 V key-off, ~12–14 V key-on.
- **Ground** to the bike chassis / IM connector ground, shared with the J1850 RX
  ground (**pin 5**) — one common reference for the buck, the board, and the bus
  tap.
- **Physical placement:** F1 within a few cm of the 12V tap (protects the whole
  run); D2 + D3 + mini560 + D4 together in a small heatshrunk/potted module near
  the cluster; **D4 right at the mini560 output**; short, strain-relieved,
  soldered + heatshrunk leads; secure against vibration; conformal-coat on the
  permanent build (Phase 6).
- **USB-C stays free for data.** When a laptop is plugged in (bench / flashing),
  the two 5 V sources OR safely at `VCC_5V` (D4 + the board's on-board AO3401).
- **Verify the V-Rod pinout** against the service manual / the master-plan
  connector table before cutting the harness — pin colors can vary by year and
  market.
