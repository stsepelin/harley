# Protected bike-power chain: 12V (V-Rod, ignition-switched) -> mini560 buck
# -> board 40-pin header 5V. Power injects at the HEADER (soldered feed, USB-C
# left free for data). The header 5V sits on the board's common VCC_5V rail
# AFTER the board's on-board USB reverse protection, so it gives the mini560
# NO reverse block -- D4 adds that. USB-C data can stay plugged in: the board's
# own AO3401 ideal-diode blocks back-feed into the laptop, D4 blocks back-feed
# into the mini560, so the two 5V sources OR safely at VCC_5V.
# Regenerate: see README.md in this directory.
import schemdraw
import schemdraw.elements as elm

with schemdraw.Drawing(file="bike-power-chain.svg", show=False) as d:
    d.config(unit=2.0, fontsize=10)
    GNDY = -3.2

    # ===== +12V input protection (top rail, left -> right) =====
    start = d.add(elm.Dot(open=True)
                  .label("+12V bike\n(IM pin 6 Grey,\nignition-switched)", loc="left"))
    d.add(elm.Fuse().right().label("F1  2A\nblade fuse"))
    d.add(elm.Schottky().right().label("D2  SB560\nreverse-polarity"))
    nA = d.add(elm.Dot())

    # load-dump clamp: unidirectional TVS (drawn as a zener, cathode to the
    # +rail) from the mini560 input node down to the ground rail.
    d.add(elm.Zener().down().reverse().at(nA.center).toy(GNDY)
          .label("TVS1\nP6KE16A\n(axial)\nload-dump clamp", loc="bottom", ofst=0.15))
    gA = d.add(elm.Dot())

    # ===== mini560 buck =====
    d.add(elm.Line().right().at(nA.center).length(2.0))
    mini = d.add(elm.Ic(
        pins=[elm.IcPin(name="VIN", side="left", slot="2/2"),
              elm.IcPin(name="GI", side="left", slot="1/2"),
              elm.IcPin(name="VOUT", side="right", slot="2/2"),
              elm.IcPin(name="GO", side="right", slot="1/2")],
        size=(2.6, 2.6),
        label="mini560\n12V->5V buck\n(MP1584)\nset 5.0V").anchor("VIN"))
    d.add(elm.Line().down().at(mini.GI).toy(GNDY))
    gB = d.add(elm.Dot())

    # ===== output reverse-block + board header =====
    d.add(elm.Line().right().at(mini.VOUT).length(0.5))
    d.add(elm.Diode().right().label("D4  XL74610\nideal-diode module\n(LM74610, ~0V drop)"))
    vcc = d.add(elm.Dot().label("VCC_5V", loc="bottom", ofst=(0, -0.15)))
    d.add(elm.Line().right().length(1.1))
    board = d.add(elm.Ic(
        pins=[elm.IcPin(name="P5V", side="left", slot="2/2", anchorname="P5V"),
              elm.IcPin(name="GND", side="left", slot="1/2", anchorname="GNDP")],
        size=(3.0, 2.6),
        label="Waveshare board\nJ8 pin 40 = 5V\n(= VCC_5V,\nboard-protected)").anchor("P5V"))
    d.add(elm.Line().down().at(mini.GO).toy(GNDY))
    gC = d.add(elm.Dot())
    d.add(elm.Line().down().at(board.GNDP).toy(GNDY))
    gD = d.add(elm.Dot())

    # ===== coexisting USB-C source into VCC_5V via the board's own AO3401 =====
    d.add(elm.Line().up().at(vcc.center).length(1.6))
    top = d.add(elm.Dot())
    d.add(elm.Schottky().left().at(top.center).length(2.4)
          .label("on-board AO3401\nideal-diode\n(blocks back-feed\ninto laptop)", loc="top"))
    d.add(elm.Dot(open=True)
          .label("USB-C 5V\n(laptop, data port)", loc="left"))

    # ===== common ground rail =====
    d.add(elm.Line().at(start.center).toy(GNDY))
    gS = d.add(elm.Dot())
    d.add(elm.Line().at(gS.center).tox(gD.center))
    d.add(elm.Ground().at(gS.center))
    d.add(elm.Label().at((gS.center[0] + 0.1, GNDY - 0.5))
          .label("common ground: bike chassis / IM pin 5", loc="right", fontsize=9))

    # ===== captions (self-contained bench printout) =====
    d.add(elm.Label().at((1.5, -5.6)).label(
        "Order matters: fuse first (protects the whole run), then reverse-polarity, "
        "then the load-dump TVS on the mini560 input.\n"
        "Board draws ~1.0 A continuous / ~2.0 A peak at 5V (~1 A at 12V). "
        "Everything rated with >=2x margin.\n"
        "TVS1 P6KE16A (axial): 16V standoff (stays off below the 15V charging spike), ~26V clamp "
        "(safely under the mini560's ~28V max).\n"
        "D4 gives the mini560 the reverse block the header lacks. Drawn: XL74610 ideal-diode "
        "module (LM74610, ~0V drop -> set mini560 to 5.0V, no math). Fallback: SS34 Schottky -> "
        "set 5.35V, verify ~5.0V at the board UNDER LOAD.\n"
        "USB-C 5V and mini560 5V OR at VCC_5V: D4 blocks back-feed into the buck, the "
        "on-board AO3401 blocks back-feed into the laptop.",
        fontsize=8.5, halign="left"))
