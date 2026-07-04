# J1850 TX stage (Phase 3 Stage 4 — populate only after the sniff works).
# High-side switched source: TX GPIO high -> Q1 pulls Q2's base low
# through R4 -> Q2 sources +12V through R5 onto the bus; D1 (in the RX
# drawing) clamps the driven level at ~7.5V. TX low -> Q1 off, R6 holds
# Q2's base at the emitter rail -> Q2 hard off -> bus released to 0V.
# Regenerate: see README.md in this directory.
import schemdraw
import schemdraw.elements as elm

with schemdraw.Drawing(file="j1850_tx.svg", show=False) as d:
    d.config(unit=2.2, fontsize=11)

    gpio = d.add(elm.Dot(open=True).label("ESP32-P4 TX GPIO", loc="left"))
    d.add(elm.Resistor().right().label("R3\n1k"))
    q1 = d.add(elm.NFet(bulk=False).right().anchor("gate").label("Q1\nIRLZ44N", loc="right"))
    d.add(elm.Ground().at(q1.source))

    d.add(elm.Line().up().at(q1.drain).length(0.4))
    d.add(elm.Resistor().up().label("R4\n10k"))
    node_a = d.add(elm.Dot())

    # Base pull-up: guarantees Q2 is fully off when Q1 isn't sinking.
    d.add(elm.Resistor().up().at(node_a.center).label("R6\n10k"))
    rail_l = d.add(elm.Dot())

    d.add(elm.Line().right().at(node_a.center).length(2.2))
    # .right() puts the emitter up (to +12V) and collector down (to the
    # bus) — a clean high-side source. Do NOT .flip(): that swaps E/C, so
    # the emitter's up-wire crosses back through the collector node and
    # the drawing reads as a C-E short.
    q2 = d.add(elm.BjtPnp(circle=True).right().anchor("base")
               .label("Q2  2N2907 / S8550\n(assortment)", loc="right", ofst=(0.4, 0.7)))

    d.add(elm.Line().up().at(q2.emitter).toy(rail_l.center))
    rail_r = d.add(elm.Dot())
    d.add(elm.Line().left().at(rail_r.center).tox(rail_l.center))
    d.add(elm.Vdd().at(rail_r.center).label("+12V (bike, switched)"))

    d.add(elm.Line().down().at(q2.collector).length(0.6))
    d.add(elm.Resistor().down().label("R5\n100Ω"))
    d.add(elm.Line().down().length(0.4))
    d.add(elm.Dot(open=True).label("to J1850 BUS node\n(the RX tap — D1 clamps it)", loc="right"))

    d.add(
        elm.Label()
        .at((4.0, -3.4))
        .label("Never hold TX high outside a VPW symbol - a stuck-high TX jams the whole bus.\n"
               "The firmware's TX watchdog is mandatory before this stage touches the bike.",
               fontsize=9)
    )
