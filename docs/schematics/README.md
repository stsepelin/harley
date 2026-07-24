# Schematics

Source of truth for the analog/wiring drawings. Each `.py` is a
[schemdraw](https://schemdraw.readthedocs.io/) script that renders the
`.svg` next to it; the SVGs are committed so the docs render on GitHub
without any toolchain. (Exceptions: the matplotlib drawings —
`j1850_perfboard.py`, `j1850_signal_board.py`, `bike_power_perfboard.py`,
`im_connector_face.py` — are matplotlib, not schemdraw; see Regenerate.)

> **TX polarity RESOLVED (2026-07-04): standard VPW → high-side TX.**
> The bus idles LOW / dominant HIGH (bare-bus DMM + invert-off raw dump +
> clean filter-off decode all agree — see `SESSION-2026-07-04.md`), so
> `j1850_tx.svg` (high-side PNP) is the canonical TX. The low-side
> `j1850_transceiver` drawing was the inverted-bus hypothesis; it was
> wrong and has been deleted (its "wrong turn" is recorded in prose in
> the master plan + session notes, not kept as a buildable drawing). TX
> build itself is still Stage 4, gated on the 2N2907A + bench self-sniff.

| Drawing | Built in | What it is |
|---|---|---|
| `j1850_rx.py/.svg` | Phase 3 Stage 1–2 | J1850 RX front end alone: 7.5V zener clamp + 10k/4.7k divider → sniffer GPIO. Correct and unchanged. Build this first — it can't disturb the bus. (The temporary GPIO 22 ADC amplitude probe is a second wire off node B — firmware `CONFIG_VROD_J1850_ADC_GPIO`; not drawn.) **Phase 6:** the permanent harness needs a **comparator / Schmitt** stage here for noise immunity — not the P4 glitch filter (it desyncs decode; see the master plan Phase 6 note + `../../firmware/docs/j1850-toggling-isr-candidate.md`). This bare-divider drawing is the bench build. |
| `j1850_tx.py/.svg` | Phase 3 Stage 4 — **canonical TX (high-side)** | High-side PNP source: dominant = drive bus HIGH. Correct for standard VPW (idle LOW / dominant HIGH), now confirmed on the bike. 2N2907A needed. |
| `j1850_perfboard.py/.svg` | Phase 3 Stage 4 — perfboard build | Physical pad-per-hole layout (5x7 cm perfboard) of the validated bench transceiver (RX bare divider + high-side TX) for the **bike build** — **Rpd omitted** (the bench bus pull-down is bench-only; the vehicle holds the recessive state). The schemdraw `j1850_rx`/`j1850_tx` drawings stay the electrical source of truth; this only fixes where parts and jumpers physically go. Net list + solder/ring-out procedure in `j1850_perfboard.md`. **matplotlib, not schemdraw.** |
| `j1850_signal_board.py/.svg` | Phase 3 + 6 — full bike signal board | Physical pad-per-hole layout of the whole signal board: the J1850 transceiver (as `j1850_perfboard`) **plus the 6 discrete 12V dividers** (`discrete_divider`, ×6: turn L/R, beam, neutral, oil, ignition). Bike build (no Rpd), superset of `j1850_perfboard`. **v4 (18×24 perfboard): single GND rail down the right edge, one P4 comb (RX·GP20 / TX / GND + the 6 divider outputs), a PWR/BUS 3-pin input terminal, 2×3p signal-input terminals, and a +12V/GND transit output to the power board; each 3V3 clamp joins its comb pin through a jumper so the pin hole stays free.** The power chain lives on a **separate** board (switcher noise / current). Electrical source of truth stays `j1850_rx`/`j1850_tx`/`discrete_divider`. **matplotlib, not schemdraw.** |
| `discrete_divider.py/.svg` | Phase 6 | 12V discrete-signal divider (10k/2.7k + optional 3.3V zener), ×6 for turns/beam/neutral/oil/ignition. Sized for 14.4V charging voltage. |
| `bike-power-chain.py/.svg` | Phase 6 (power) | Protected 12V→5V bike-power chain: fuse + reverse-polarity + load-dump TVS (TVS1 P6KE16A) → mini560 → output reverse-block (D4 XL74610 ideal-diode @ 5.0V) → board header 5V, with USB-C data coexisting via the board's own AO3401. Full parts list + bench test in `../../firmware/docs/bike-power-injection.md`; BOM in `bike-power-chain.bom.md`. |
| `bike_power_perfboard.py/.svg` | Phase 6 (power) — perfboard build | Physical pad-per-hole layout of the `bike-power-chain` circuit: F1 2A → D2 SB560 → TVS1 P6KE16A → mini560 12V→5V → D4 XL74610 ideal-diode → P4 header 5V, with the protected-12V tap feeding the signal board. Separate board from the signal board. Electrical source of truth stays `bike-power-chain.py`. **matplotlib, not schemdraw.** |
| `im_connector_face.py/.svg` | Phase 6 (harness) | Physical face map of the V-Rod Instrument Module 12-pin connector (**Deutsch DTM06-12S** socket, from the molded housing marking) for building quick-connect mating connectors. 2-row / 6-wide; **bottom row numbering is inverted** — 12,11,10,9,8,7 left→right (pin 12 under pin 1, pin 7 under pin 6), **verified on the bike (2026-07)**, not assumed. Wire-entry side (the mating face mirrors L↔R). Each cavity shows wire colour + signal; the ring colour encodes the destination (signal-board divider / J1850 BUS / power board / GND / Phase 6 / unused). Pin→colour→signal from the connector table in `../00-MASTER-PROJECT-PLAN.md` / `../PROJECT-BRIEF.md`. **matplotlib, not schemdraw.** |
| `gps_module.py/.svg` | Map (optional) | NEO-6M / GY-NEO6MV2 map-position module: 5V/GND + module TX → GPIO 21 (3.3V TTL, no level shift), RX-only. Wiring + bring-up in `../../firmware/docs/gps-module.md`. |

## Regenerate

```sh
python3 -m venv .venv && .venv/bin/pip install schemdraw matplotlib
for f in *.py; do .venv/bin/python "$f"; done
```

Edit the `.py`, re-run, commit both files. Don't hand-edit the SVGs.

The matplotlib drawings — `j1850_perfboard.py`, `j1850_signal_board.py`,
`bike_power_perfboard.py`, `im_connector_face.py` — are **matplotlib, not
schemdraw** (hence the `matplotlib` install above). Each writes both a `.svg`
and a `.png`; only the `.py` + `.svg` are committed, the `.png` is gitignored
(`docs/schematics/*.png`).

## Conventions

- Component designators (R1, Q2, D1…) are stable across drawings and
  match the prose in `../00-MASTER-PROJECT-PLAN.md`.
- Values that came out of analysis carry their reasoning as a caption
  inside the drawing (e.g. the 14.4V charging-voltage math), so a
  printout taken to the bench is self-contained.
