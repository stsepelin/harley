# J1850 RX: toggling-ISR candidate (module merged behind a flag; trial pending)

> **Status: pure tracker merged to main behind a compile flag; the sniffer
> ISR is NOT wired to it yet.** `main/j1850/j1850_edge.c` is the toggle +
> idle-anchor level reconstructor, host-tested in `test_j1850_edge.c`
> (clean-stream reconstruction through the decoder, startup drop, idle-gap
> boundary, and injected missed/spurious-edge re-sync) at 100% line+branch.
> The shipping RX path is UNCHANGED: the sniffer ISR still reads the pin,
> the glitch filter is still OFF (`CONFIG_VROD_J1850_GLITCH_NS=0`),
> `RX_INVERT` is gone, and `j1850_vpw.c` is untouched.
>
> **Two gates, both default off (see `main/Kconfig.projbuild`):**
> - **(a) `CONFIG_VROD_J1850_TOGGLE_ISR`** — compiles `j1850_edge.c` into
>   the firmware so it is available. HARMLESS: nothing calls it, so RX
>   behaviour is byte-for-behaviour identical to the flag being off. This
>   is what merged now.
> - **(b) `CONFIG_VROD_J1850_TOGGLE_ISR_LIVE`** — would switch the live ISR
>   to the tracker. **NOT WIRED** in this merge; enabling it today does
>   nothing. Reserved so the two-step opt-in is explicit.
>
> **Phase 6 follow-up (opt-in, deliberate) — the actual gate-(b) work:**
> wire `j1850_edge` into the sniffer under `TOGGLE_ISR_LIVE` — the ISR
> stops reading the pin and only timestamps edges; the task reconstructs
> the level via `j1850_edge_level()`; the idle-flush keeps an honest pin
> read (allowed — the line is static there). Then a bench trial (toggle ISR
> + a conservative filter, re-enabled to confirm toggling lets it coexist)
> measured against the current filter-off baseline (546 frames / 0 bad
> CRC), one variable at a time.

## Why this exists

Phase 3 settled the bus: **standard VPW, idle LOW, dominant HIGH**
(bare-bus DMM ~0.3 V idle + invert-off raw dump + 546 frames / 0 bad CRC
with the filter off — see `captures/SESSION-2026-07-04.md`).

It also proved the **P4 hardware glitch filter is unusable** on this RX
path. The on-bike sweep decoded **zero frames at every nonzero window**
(400 / 800 / 1200 / 1600 ns alike) while 0 ns gave 546 frames — a hard
cliff, not a gradient. Root cause: the flex filter delays the *filtered*
edge relative to the sniffer ISR's `gpio_get_level()` read, so the level
recorded for the just-ended pulse is systematically wrong and frames
never assemble. The delay magnitude barely matters, so no window works.

That leaves the bench build with **no debounce at all**. Fine on short
clean leads; risky on the Phase 6 permanent harness (longer runs,
ignition/coil noise, vibration). The primary fix is a hardware RX
**comparator / Schmitt** front end (see the master plan Phase 6 note).
This note describes a *software* change that would additionally let a
conservative glitch filter coexist — useful defence-in-depth, not a
substitute for the comparator.

## The current ISR (what races the filter)

`edge_isr()` in `main/j1850/j1850_sniffer.c`, on every edge:

```c
int level = gpio_get_level(SNIFF_GPIO);  // <-- the pin read that races
evt.active = (uint8_t)s_level;           // level HELD during the pulse that just ended
evt.dur_us = now - s_last_edge_us;
s_level = level;                         // remember for next edge
```

The pulse that just ended is labelled with `s_level`; the new level comes
from reading the pin. With a filter in front, the interrupt fires on the
*delayed* filtered edge but `gpio_get_level()` returns the *instantaneous*
pin — the two disagree, and `s_level` gets corrupted from then on.

## The idea: infer the level by toggling

VPW is a two-level line and edges strictly alternate, so after any real
edge the new level is simply the inverse of the old one:

```c
// candidate: no pin read on the hot path
evt.active = (uint8_t)s_level;
evt.dur_us = now - s_last_edge_us;
s_level = !s_level;   // toggle instead of gpio_get_level()
```

No pin read on the edge → no filter-vs-read race → a conservative filter
could be re-enabled purely for chatter rejection.

## The failure mode this MUST design around

Toggling assumes **every edge is observed**. A single missed edge
(swallowed by a filter, lost to a full queue/overrun, or a genuine
dropout) **inverts the stream polarity from that point on**, and the
error **propagates** — unlike pin-reading, where every read is
independent and self-correcting. Toggling is only safe with **absolute
re-sync anchors** so a miss corrupts at most one frame (which CRC already
rejects) instead of the whole stream.

Symmetric truth to hold onto:

- Toggling + a conservative filter defends against **extra** edges
  (chatter → the filter removes them; without the pin-read race they no
  longer corrupt the level).
- The **idle anchor** (below) defends against **missing** edges (the
  thing a filter can still cause by swallowing a slow edge).

You need both halves. One without the other is worse than today.

## Re-sync design (the core of this note)

### 1. Idle anchor — primary, most robust

Any passive gap longer than a valid VPW symbol means the bus is
**recessive = LOW**, known absolutely with zero history. Concretely: no
edge for `> J1850_VPW_SOF_MAX_US` (239 µs), i.e. an EOF-length idle
(≥280 µs nominal). At that moment, **force the toggled level to LOW.**

The sniffer already detects this gap — it's the same idle-flush path that
closes a pending frame (`level == RX_PASSIVE_PHYS_LEVEL && idle_us >
J1850_VPW_SOF_MAX_US + 60`). Every inter-frame gap becomes a free
re-sync, so a missed edge is **self-limited to a single frame**. This is
the workhorse; it must be mandatory, not optional.

### 2. SOF anchor — secondary

SOF is a ~200 µs **active** pulse, classified by **width** (robust, not
by level) and by definition **HIGH**. On SOF detection, assert
`level = HIGH`. If that disagreed with the toggled value, correct it and
mark the frame suspect (let CRC be the final arbiter). This tightens
re-sync to the start of each frame rather than only the gap before it.

### 3. Occasional honest pin read — OFF the hot path only

A `gpio_get_level()` is allowed **only in static moments** — e.g. at the
instant we enter the idle anchor, where the line is not transitioning, so
the filter delay is irrelevant. Use it as a second opinion to confirm the
idle anchor's LOW. **Never read the pin on an edge.** That's the whole
point.

### Startup

The toggled level has no history at boot. Seed it with **one** honest pin
read at init, or simply **do not trust the stream until the first idle
anchor** fires (→ LOW). Prefer the latter: it needs no assumption about
the power-on bus state.

### Filter interaction

Re-enabling a filter is optional and, if done, must be **conservative**
(smallest useful window). A filter still *can* swallow a slow edge
entirely (the passive recessive fall is the known offender) → a missed
toggle. The idle anchor is the mandatory backstop for exactly that, plus
for overruns and genuine dropouts. Do not re-enable a filter without the
idle anchor in place.

### Overrun / queue loss

A full pulse queue drops edges — same class of failure as a swallowed
edge. No special handling beyond the idle anchor, which re-syncs at the
next gap. (Overrun counting stays as-is for health visibility.)

## Acceptance gate (must pass BEFORE any implementation)

The alternation + re-sync is **pure logic**, so it lands in `vrod_pure`
with host tests, same policy as `j1850_vpw.c` / `nmea.c` (100 % line +
branch on the tested scope). The state machine (level toggle + idle/SOF
anchors) should be a free-function module separate from the LVGL/ISR glue
so it is host-testable in isolation.

Required tests feed synthetic pulse streams and assert re-sync:

- **Injected missed edge** mid-frame → that frame is corrupt (CRC
  rejects), and the stream is correct again by the next idle anchor.
- **Injected extra edge** (chatter) → at most one corrupt frame, re-sync
  at the next anchor.
- **Varying idle-gap lengths** around the `J1850_VPW_SOF_MAX_US`
  threshold → the anchor fires when (and only when) the gap qualifies.
- **SOF anchor disagreement** → level is corrected to HIGH and the frame
  is flagged suspect.
- **Startup** with an unknown seed → no valid frame is emitted until the
  first idle anchor establishes LOW.
- **Back-to-back frames** with short inter-frame gaps still at least
  EOF-length → each still re-syncs.

Only after these pass green does an on-hardware trial (toggle ISR +
conservative filter, invert nothing) make sense — measured against the
current filter-off baseline (546 frames / 0 bad CRC), one variable at a
time.

## Explicitly out of scope

- No code changes land from this note. The current filter-off build is
  the shipping RX path.
- `j1850_vpw.c` does not change — the codec was never the problem; the
  bug was in the ISR/glue edge capture.
- This does not replace the Phase 6 comparator front end; it complements
  it.

## Cross-links

- Sweep data + resolution: `captures/SESSION-2026-07-04.md`
- Phase 6 hardware requirement: `../../docs/00-MASTER-PROJECT-PLAN.md`
  (Phase 6 — "J1850 RX front end — add hysteresis")
- Filter default rationale: `CONFIG_VROD_J1850_GLITCH_NS` help in
  `../main/Kconfig.projbuild`
