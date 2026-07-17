# J1850 transceiver — perfboard build (5x7 cm, pad-per-hole, bike build)

Soldering map for the validated bench circuit (bare-divider RX + high-side TX)
transferred to a 5x7 cm pad-per-hole perfboard. Top / component-side view.
Coordinates are **(column, row)**, row 1 at the top. Bare-wire buses:
**+12V on row 2**, **GND on row 15**. Rpd (the bench bus pull-down) is **omitted**
— on the bike the other bus nodes hold recessive.

Layout image: `j1850_perfboard.svg` (regenerate: `python3 j1850_perfboard.py`,
needs matplotlib — this drawing is NOT schemdraw). Circuit source of truth is
`j1850_tx.py` / `j1850_rx.py`; this file only fixes the physical placement.

> This is a soldered copy of the current bench circuit. The permanent Phase-6
> harness plan adds a comparator / Schmitt RX front end for noise immunity — a
> separate piece of work, not this board.

## Netlist (9 nets)

| Net | Holes (col,row) | Members |
|-----|-----------------|---------|
| **+12V** | bus row 2 | +12V IN, R6-top (8,2), Q2 E |
| **GND** | bus row 15 | GND IN, Q1 S, Rg-bottom, D1 anode, R2-bottom |
| **TXIN** | (4,9) | TX GPIO24, R3-left |
| **GATE** | (6,9)+(7,9) | R3-right, Q1 G, Rg-top |
| **DRAIN** | (8,8)+(8,9) | R4-bottom, Q1 D |
| **NODE_A** | (8,5)…(11,5) | R6-bottom, R4-top, Q2 B |
| **COLLECTOR** | (11,6)+(11,7) | Q2 C, R5-top |
| **BUS** | (11,10)…(18,10) | R5-bottom, D1 cathode, R1-top, → J1850 pin 7 |
| **NODE_B** | (15,12) | R1-bottom, R2-top, → RX GPIO20 |

## Component placement

| Part | Value | Pin A | Pin B | Net A → Net B |
|------|-------|-------|-------|----------------|
| R6 | 10k | (8,2) | (8,5) | +12V → NODE_A |
| R4 | 10k | (8,5) | (8,8) | NODE_A → DRAIN |
| R3 | 1k | (4,9) | (6,9) | TXIN → GATE |
| Rg | 10k | (7,9) | (7,12) | GATE → (jumper to GND) |
| R5 | 100R | (11,7) | (11,10) | COLLECTOR → BUS |
| D1 | 7.5V zener | (13,10) **cathode/band** | (13,13) anode | BUS → (jumper to GND) |
| R1 | 10k | (15,10) | (15,12) | BUS → NODE_B |
| R2 | 4.7k | (15,12) | (15,15) on GND bus | NODE_B → GND |
| Q1 | IRLZ44N (TO-220) | G(7,9) · D(8,9) · S(9,9) | | GATE / DRAIN / GND |
| Q2 | 2N2907A (TO-92) | E(11,4) · B(11,5) · C(11,6) | | +12V / NODE_A / COLLECTOR |

Splay each transistor's legs so every lead lands in its own pad (Q1: three
adjacent columns of one row; Q2: three adjacent rows of one column).

## Jumpers (wire / tinned bus wire)

| From | To | Purpose |
|------|----|---------|
| (6,9) | (7,9) | GATE: R3 → Q1 gate / Rg |
| (8,8) | (8,9) | DRAIN: R4 → Q1 drain |
| (8,5) | (11,5) | NODE_A: R6/R4 → Q2 base |
| (11,4) | (11,2) | Q2 emitter → +12V bus |
| (11,6) | (11,7) | COLLECTOR: Q2 → R5 |
| (11,10)→(13,10)→(15,10)→(18,10) | | BUS along row 10 |
| (9,9) | (9,15) | Q1 source → GND bus |
| (7,12) | (7,15) | Rg → GND bus |
| (13,13) | (13,15) | D1 anode → GND bus |
| (15,12) | (17,12) | NODE_B → RX GPIO20 pad |

## External leads

| Lead | Pad |
|------|-----|
| +12V (PSU/bike) | (4,2) on +12V bus |
| GND (P4 GND / PSU-) | (4,15) on GND bus |
| TX GPIO24 | (4,9) |
| J1850 BUS (pin 7) | (18,10) |
| RX GPIO20 | (17,12) |

## Solder order

1. Resistors (lowest profile): R6, R4, R3, Rg, R5, R1, R2.
2. D1 — **band (cathode) toward BUS**, i.e. the top lead (13,10).
3. Transistors — **verify pinout first** (Q1 G-D-S left→right; Q2 middle leg =
   Base by DMM diode test, emitter up to +12V).
4. Bare-wire buses: +12V (row 2) and GND (row 15).
5. Jumpers per the table.
6. External leads.

## Ring-out AFTER soldering, before applying 12V (mandatory)

1. Rails not shorted: **+12V row 2 ↔ GND row 15 = open**.
2. No transistor shorts: Q2 E(11,4)↔C(11,6), Q1 D(8,9)↔S(9,9) = open.
3. Resistor values in place (ohms): R5 (11,7↔11,10)=100R; Rg (7,9↔GND)=10k; etc.
4. **BUS (11,10) ↔ GND ≈ 14.7k** (only R1+R2 now, no Rpd) — not zero.
5. Only after a clean ring-out, re-run bench tests A/B/C on this board.

> Without Rpd the bus has no defined recessive-LOW on an isolated bench. To run
> loopback self-sniff on this board again, temporarily tack ~10k from BUS (11,10)
> to GND. Not needed on the bike (other nodes hold recessive).
