# BOM — protected bike-power chain (12V → 5V)

Bill of materials for the `bike-power-chain.py/.svg` drawing. Derived from the
parts table in [`../../firmware/docs/bike-power-injection.md`](../../firmware/docs/bike-power-injection.md);
keep the three in sync (drawn part = parts-table part = BOM row). Order is
bike-side to board.

| Ref | Part | Value / Rating | Package | Qty | Notes (purpose) |
|---|---|---|---|---|---|
| F1 | Automotive blade fuse + inline holder | 2 A, 32 V | ATM / mini blade | 1 | Protects the feed wiring against a short at the tap; blade fuse is slightly slow so it tolerates the mini560 inrush. Bump to 3 A only if inrush nuisance-blows. |
| D2 | Schottky, series reverse-polarity — SB560 (60 V/5 A) or SS54 (40 V/5 A) | ≥40 V, ≥3 A | DO-201AD / SMC | 1 | Blocks a swapped +12V/GND at the mini560 input. Low-loss alternative: a P-channel MOSFET. |
| TVS1 | TVS, unidirectional — P6KE16A | 16 V standoff, ~26 V clamp @ I_PP, 600 W | axial (DO-15) | 1 | Load-dump / spike clamp across the mini560 12V input; ~26 V clamp sits ~2 V under the mini560 ~28 V abs-max. Axial/leaded for the potted flying module. Higher-energy alternative: 1.5KE16A (1500 W). |
| mini560 | MP1584-class 12V→5V buck module | Vin 4.5–28 V, Iout ~3 A, **set 5.0 V** | module | 1 | Steps 12V down to 5.0 V. Trim to exactly 5.0 V for the XL74610 ideal-diode path (no drop compensation). |
| D4 | XL74610 ideal-diode module (LM74610-based) | ~1.5–36 V, 30 A, ~tens-of-mV drop | module | 1 | Output reverse-block the header lacks (blocks `VCC_5V` → mini560); primary/drawn. Fallback: SS34 Schottky (40 V/3 A, SMA/DO-214AC) → then set the mini560 to 5.35 V to compensate. |

Notes:
- **5 V-rail bulk cap:** *unspecified* — `bike-power-injection.md` does not list a
  bulk/electrolytic cap on the 5 V rail, so none is included here. Add one per
  board/module guidance if bench testing shows it's needed.
- **On-board parts are not in this BOM.** The board's own USB reverse protection
  (AO3401 + MMDT3906DW) and its ESD clamp (D3 SMF5.0CA) are already fitted on the
  Waveshare board; only the injection-chain parts above are to be sourced.
