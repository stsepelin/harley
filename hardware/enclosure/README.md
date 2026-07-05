# Enclosure — Waveshare ESP32-P4-WIFI6-Touch-LCD-3.4C

Parametric round enclosure in `enclosure.scad`. **Rear-bolt board fixation** for a
motorcycle (vibration): the PCB is mounted the way it's designed to be — bolted at
its 4 mount holes (74.5 mm square) to internal standoffs, from the **rear**.

**Key dimensions:** Ø115 mm face (cover-glass OD, `glass_od`) · 74.5 mm board mount
square (`mount_square`) · ~50 mm electronics cavity depth (`internal_depth`). Build
tiers (`simple` / `compromise` / `robust`) tune wall / floor / standoff / gasket —
see the tier table below. (STL outputs are regenerated from `enclosure.scad`, not
committed — see the render commands under "Modes".)

> **This is a TEMP / bench-test build.** Wiring exits through ONE cable hole at the
> bottom of the wall (`cable_hole_dia` at `cable_exit_angle` = 270°); the per-connector
> cutouts (USB-C / microSD / 40-pin / buttons) are dropped and kept as commented
> params for the final enclosure. microSD access on this build = remove the rear cover.
> **Every screw boss is gusseted** (bezel ears, case perimeter bosses, board
> standoffs) via a shared `boss_gusset` module — no freestanding stalks anywhere.

```
[ bezel ring ]   frames + protects the glass edge (light duty)
[ glass + PCB ]  one bonded block, 7.6 mm (glass 6 + pcb 1.6), touching; in from front
[ standoffs ]    4 chunky wall-gusseted posts behind the PCB at the 74.5 square,
                 M3 heat-set inserts — the board's PRIMARY load path
[ deep cavity ]  ~50 mm for breadboard + mini560 + wiring
[ rear cover ]   REMOVABLE — reach the 4 board bolts + wiring, then close
```

## Why the rear cover is removable (not a closed cylinder)

The 4 board bolts are driven from the **rear**, so the back must be **open during
assembly** to reach them. A one-piece closed cylinder would make the board bolts
(and wiring, and the microSD) unreachable. So the rear cover is a separate,
removable panel — screwed on last.

## Why this works now (it didn't before)

The 4 M3 holes sit under the glass *from the front*, but the removable rear cover
means they're **reachable from behind**. The board seats rear-face on 4 chunky
standoffs at the 74.5 square; 4 rear M3 bolts thread up into heat-set inserts in
the standoffs, engaging the board's own mount holes. The block is trapped forward
by the front lip + bezel and rearward by the standoffs, so the board is held
captive at its 4 holes — its intended mount, stronger and more direct than the
edge-compression it replaces.

> Note on the load path: because the glass fully backs the PCB *front*, a bolt
> head can't sit on the glass side, so the rear bolts **locate + secure** the board
> at its holes while the front lip/bezel trap the block forward (the board can't
> lift off the posts). This is a direct 4-point board mount, not an edge clamp.

## Standoffs (the board's load path)

Chunky and **wall-gusseted** — no thin tall stalks. Diameter is per-tier (`simple`
9, `compromise` 10, `robust` 11 mm). Each post is tied to the wall by a **tapered
fan gusset** (`gusset_span` ±15°, `gusset_h` 10 mm): a solid web fanning from the
post base out to a ~30° arc of the wall and tapering up to the post, so the post
can't crack off at the base under vibration. M3 heat-set insert (`insert_hole_dia
= 4.0`, `insert_depth = 6.0`) opens toward the PCB; the rear bolt reaches it up a
clearance bore. A **`gasket_under_pcb`** pad (0.8 mm default) sits between the PCB
rear and the standoff tops — the whole board is damped, not just the glass rim.
Print `rear_case` open-end up so the standoff/gusset layers resist the bolt load
across, not along, the layer lines.

Verified in the render echo: standoff centers at (±37.25, ±37.25) → **74.5 mm
center-to-center in X and Y**, and the bolt path PCB-hole (z 6–7.6) → under-PCB
gasket (7.6–8.4) → insert (8.4–14.4) → rear clearance to the open back.

## Modes / render

```sh
openscad -o rear_case.stl        -D 'part="rear_case"'        enclosure.scad
openscad -o bezel.stl            -D 'part="bezel"'            enclosure.scad
openscad -o rear_cover.stl       -D 'part="rear_cover"'       enclosure.scad
openscad -o calibration_base.stl -D 'part="calibration_base"' enclosure.scad
# tier / fastener overrides:
openscad -o rear_case.stl -D 'part="rear_case"' -D 'tier="robust"' enclosure.scad
```
All four parts render manifold (`Simple: yes`) at every tier / fastener setting.

## Build tiers (`-D 'tier="..."'`)

| tier | fastener | wall | floor | perimeter screws | standoff Ø | gasket_gap |
|---|---|---|---|---|---|---|
| `simple` | captured nuts | 3.0 | 4.0 | 4 | 9 | 1.0 |
| `compromise` (default) | heat-set | 3.0 | 3.5 | 6 | 10 | 0.6 |
| `robust` | heat-set | 4.0 | 4.0 | 6 | 11 | 1.0 |

Every screw boss (bezel ears, case perimeter bosses, board standoffs) is reinforced
by the shared `boss_gusset` module — a fan web to its wall/ring/floor, sized by
`gusset_span` / `gusset_h`. `screw_offset` (0°) keeps a clean gap at the bottom
(270°) for the cable hole with 6 screws.

`fastener` (for the perimeter bezel/cover screws) is independently overridable:
`-D 'fastener="heatset"'` / `"captured_nut"`. The **board** standoffs always use
M3 heat-set inserts (metal — the board is the load path).

## Print order

1. **`calibration_base`** — shallow front section with the block-seat ledge, the
   bezel screw ears, **and the 4 board standoffs**. Verify the block seats, the
   bezel screws line up, and — most important — the **board bolts onto the 74.5
   standoffs from the rear**. Cheap.
2. **`rear_case` + `bezel` + `rear_cover`** once the fit checks out.

## Assembly order

1. **Heat-set the inserts** into the 4 board standoffs (and the perimeter bezel /
   rear-cover bosses). Metal only — printed threads strip under vibration.
2. **Glass+PCB block in from the FRONT** — glass to the front lip; the PCB rear
   lands on the 4 standoff tops. Put the `gasket_under_pcb` foam/silicone pad (with
   4 clearance holes for the bolts) between the PCB rear and the standoff tops, and
   a gasket under the glass rim — the whole board is damped, not just the glass.
3. **4 rear board bolts** — reaching through the **open back**, drive short M3 bolts
   into the standoff inserts, engaging the board's 4 mount holes. **Thread-locker**
   (medium, e.g. Loctite 243) on every one.
4. **Bezel on the front** — light-duty frame/edge protection over the glass rim,
   its perimeter screws into the rear-case ears.
5. **Wiring** in the cavity (breadboard + mini560) exits the **single bottom cable
   hole**; then **rear cover** on last (screws into the rear bosses; thread-locker too).

## Material (print settings)

- **PETG** to start (tough). **ASA** best for a bike — UV / heat / sun resistant.
  **Not PLA** — heat-warps in a sun-parked cockpit.
- **Layer orientation:** print `rear_case` open-end up so the standoffs + gussets
  aren't a cleavage plane under the bolt load; 4+ perimeter walls. Print `bezel` and
  `rear_cover` flat. Thread-locker on all metal fasteners.

## Params to tune after test-fit

- **`standoff_len` / `standoff_od`** — post depth and chunk.
- **`gusset_span` / `gusset_h`** — fan-gusset arc and taper height (post-to-wall tie).
- **`gasket_under_pcb`** — foam/silicone pad between the PCB rear and standoff tops.
- **`gasket_gap`** — damping pad under the glass rim.
- **`mount_square` (74.5)** — the board bolt circle; re-measure if the bolts miss.
- **`fastener`** — perimeter screw style (`heatset` / `captured_nut`).
- **`glass_fit` / `block_fit`** — bore clearance if the block is tight/loose.

## Cable exit — single bottom hole (TEMP build)

All wiring exits one grommet hole at the **bottom of the wall** (`cable_hole_dia`
12 mm at `cable_exit_angle` 270°, `cable_exit_z` 35). Verified clear of the
standoffs (45/135/225/315°, z 8.4–22.4) and the screw gussets (270° sits in the
255–285° gap between the 240°/300° screws) — echoed at render time. **microSD is
accessed by removing the rear cover** on this temp build; a dedicated SD slot (and
the per-connector cutouts, kept commented in-file) comes on the final enclosure.
Move the hole with `cable_exit_angle` / `cable_exit_z`, or widen `cable_hole_dia`,
if your bundle needs it.

## Out of scope — handlebar mount

Metal load path, separate task: aluminium straps to the triple-tree riser bolts
(M10×1.5, ~89 mm). A commented `mount_strap_*` location is left in `enclosure.scad`.
