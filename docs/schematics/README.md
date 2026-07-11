# Schematics

Source of truth for the analog/wiring drawings. Each `.py` is a
[schemdraw](https://schemdraw.readthedocs.io/) script that renders the
`.svg` next to it; the SVGs are committed so the docs render on GitHub
without any toolchain.

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
| `discrete_divider.py/.svg` | Phase 6 | 12V discrete-signal divider (10k/2.7k + optional 3.3V zener), ×6 for turns/beam/neutral/oil/ignition. Sized for 14.4V charging voltage. |
| `bike-power-chain.py/.svg` | Phase 6 (power) | Protected 12V→5V bike-power chain: fuse + reverse-polarity + load-dump TVS (TVS1 P6KE16A) → mini560 → output reverse-block (D4 XL74610 ideal-diode @ 5.0V) → board header 5V, with USB-C data coexisting via the board's own AO3401. Full parts list + bench test in `../../firmware/docs/bike-power-injection.md`; BOM in `bike-power-chain.bom.md`. |
| `gps_module.py/.svg` | Map (optional) | NEO-6M / GY-NEO6MV2 map-position module: 5V/GND + module TX → GPIO 21 (3.3V TTL, no level shift), RX-only. Wiring + bring-up in `../../firmware/docs/gps-module.md`. |

## Regenerate

```sh
python3 -m venv .venv && .venv/bin/pip install schemdraw
for f in *.py; do .venv/bin/python "$f"; done
```

Edit the `.py`, re-run, commit both files. Don't hand-edit the SVGs.

## Conventions

- Component designators (R1, Q2, D1…) are stable across drawings and
  match the prose in `../00-MASTER-PROJECT-PLAN.md`.
- Values that came out of analysis carry their reasoning as a caption
  inside the drawing (e.g. the 14.4V charging-voltage math), so a
  printout taken to the bench is self-contained.
