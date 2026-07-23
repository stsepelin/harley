# V-Rod Instrument Module (IM) 12-pin connector — physical face map.
#
# Connector: DEUTSCH DTM06-12S (molded on the housing) — 12-way socket,
# 2 rows of 6. Bottom row numbering is INVERTED: left-to-right it runs
# 12,11,10,9,8,7 (pin 12 sits under pin 1, pin 7 under pin 6). This was
# verified against the physical connector on the bike (2026-07); it is NOT
# the generic serpentine assumption.
#
# View is the WIRE-ENTRY side (where terminals are inserted). The mating
# face mirrors left-right relative to this drawing.
#
# Pin -> wire color -> signal mapping is from the project connector table
# (00-MASTER-PROJECT-PLAN.md / PROJECT-BRIEF.md). Cavity ring colour encodes
# the two-board destination: green = signal-board divider, purple = J1850 BUS,
# red = power board, blue = common GND, tan = Phase 6, grey = unused.
#
# CAVEAT (measure, don't guess): verify wire colours against the HD service
# manual before crimping — colours can vary by year/market. Function follows
# the WIRE COLOUR, so build by colour, not by cavity number.
#
# Regenerate: python3 im_connector_face.py  (writes .png + .svg)
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import FancyBboxPatch, Rectangle, Circle
import matplotlib.patheffects as pe

LBL = [pe.withStroke(linewidth=2.5, foreground="white")]
DK  = [pe.withStroke(linewidth=2.5, foreground="#222")]

fig, ax = plt.subplots(figsize=(15, 9.5))
fig.patch.set_facecolor("white"); ax.set_facecolor("white")
ax.set_xlim(0, 30); ax.set_ylim(0, 19); ax.axis("off")

ax.text(15, 18.3, "V-Rod IM 12-pin — 2-row housing (Deutsch DTM06-12S)",
        ha="center", fontsize=17, fontweight="bold")
ax.text(15, 17.55, "Wire-entry side.  Top 1-6 left-to-right;  bottom row inverted: 12-7 left-to-right "
        "(pin 12 under pin 1, pin 7 under pin 6).  Verified against the connector.",
        ha="center", fontsize=9.5, color="#1a7f37", style="italic")

# wire colour swatch: (code, base, tracer)
WIRE = {
    1:("R/O","#d1242f","#f08c00"), 2:("White","#ffffff",None), 3:("Violet","#7a4fd0",None),
    4:("Brown","#7a4a1e",None),   5:("BK/GN","#1a1a1a","#2da44e"), 6:("Grey","#9aa0a6",None),
    7:("LGN/V","#a6e07a","#7a4fd0"),8:("(sub)","#c9c2ac",None), 9:("GN/Y","#2da44e","#ffd33d"),
    10:("TN","#d8c39a",None),      11:("Y/W","#ffd33d","#ffffff"),12:("O/W","#f08c00","#ffffff"),
}
SIG = {
    1:"+12V const\nDO NOT USE", 2:"High\nbeam", 3:"Left\nturn", 4:"Right\nturn",
    5:"Ground\n(GND)", 6:"+12V ign.\n-> POWER", 7:"J1850\nBUS", 8:"VSS\n(Phase 6)",
    9:"Oil\npress.", 10:"Neutral", 11:"Fuel\n(Phase 6)", 12:"Access.\n(opt.)",
}
RING = {
    1:"#999999", 2:"#2f8f4e", 3:"#2f8f4e", 4:"#2f8f4e", 5:"#3b6ea5", 6:"#cf4b2a",
    7:"#7a5cff", 8:"#b08d3a", 9:"#2f8f4e", 10:"#2f8f4e", 11:"#b08d3a", 12:"#999999",
}

# connector housing
ax.add_patch(FancyBboxPatch((1.5, 5.2), 27.0, 9.6,
    boxstyle="round,pad=0.1,rounding_size=0.5",
    facecolor="#3a3a3c", edgecolor="#1c1c1e", lw=2.5, zorder=1))
ax.add_patch(FancyBboxPatch((2.3, 6.0), 25.4, 8.0,
    boxstyle="round,pad=0.05,rounding_size=0.4",
    facecolor="#2b2b2d", edgecolor="#161617", lw=1.5, zorder=1.5))

# keyway (approximate)
ax.add_patch(Rectangle((13.8, 5.05), 2.4, 0.5, facecolor="#1c1c1e", edgecolor="#0d0d0e", lw=1, zorder=2))
ax.text(15, 4.55, "keyway (approx — verify on housing)", ha="center", fontsize=7.5, color="#b0392b")

x0, dx = 4.3, 3.85
y_top, y_bot = 11.6, 8.0
CELL = 1.35

def cavity(cx, cy, pin):
    name, base, tracer = WIRE[pin]
    ring = RING[pin]
    ax.add_patch(FancyBboxPatch((cx-CELL/2-0.14, cy-CELL/2-0.14), CELL+0.28, CELL+0.28,
        boxstyle="round,pad=0.02,rounding_size=0.12",
        facecolor=ring, edgecolor="#111", lw=1.0, zorder=3))
    ax.add_patch(FancyBboxPatch((cx-CELL/2, cy-CELL/2), CELL, CELL,
        boxstyle="round,pad=0.02,rounding_size=0.1",
        facecolor=base, edgecolor="#333", lw=1.0, zorder=4))
    if tracer:
        ax.add_patch(Rectangle((cx-CELL/2+0.14, cy+0.28), CELL-0.28, 0.24,
            facecolor=tracer, edgecolor="none", zorder=5))
    t = ax.text(cx, cy-0.1, str(pin), ha="center", va="center",
                fontsize=15, fontweight="bold", color="#111", zorder=6)
    t.set_path_effects([pe.withStroke(linewidth=3, foreground=base if base!="#1a1a1a" else "#ddd")])
    ax.text(cx, cy+CELL/2+0.5, name, ha="center", va="bottom",
            fontsize=9, fontweight="bold", color="white", zorder=6, path_effects=DK)
    ax.text(cx, cy-CELL/2-0.42, SIG[pin], ha="center", va="top",
            fontsize=7.8, fontweight="bold", color="white", zorder=6, path_effects=DK)

# top row pins 1..6
for i in range(6):
    cavity(x0 + i*dx, y_top, i+1)
# bottom row INVERTED: left->right = 12,11,10,9,8,7 (confirmed on the DTM housing)
for i in range(6):
    cavity(x0 + i*dx, y_bot, 12 - i)

# corner pin markers to make the inverted bottom row obvious
ax.annotate("pin 1", xy=(x0-CELL/2-0.2, y_top), xytext=(x0-2.4, y_top+0.9),
            fontsize=9, fontweight="bold", color="#ffd33d", ha="center",
            arrowprops=dict(arrowstyle="->", color="#ffd33d", lw=1.6), zorder=7)
ax.annotate("pin 6", xy=(x0+5*dx+CELL/2+0.2, y_top), xytext=(x0+5*dx+2.4, y_top+0.9),
            fontsize=9, fontweight="bold", color="#ffd33d", ha="center",
            arrowprops=dict(arrowstyle="->", color="#ffd33d", lw=1.6), zorder=7)
ax.annotate("pin 12", xy=(x0-CELL/2-0.2, y_bot), xytext=(x0-2.4, y_bot-0.9),
            fontsize=9, fontweight="bold", color="#ff8c42", ha="center",
            arrowprops=dict(arrowstyle="->", color="#ff8c42", lw=1.6), zorder=7)
ax.annotate("pin 7", xy=(x0+5*dx+CELL/2+0.2, y_bot), xytext=(x0+5*dx+2.4, y_bot-0.9),
            fontsize=9, fontweight="bold", color="#ff8c42", ha="center",
            arrowprops=dict(arrowstyle="->", color="#ff8c42", lw=1.6), zorder=7)

# legend
leg = [("#2f8f4e","SIGNAL · divider"), ("#7a5cff","SIGNAL · BUS"),
       ("#cf4b2a","POWER board"), ("#3b6ea5","GND (common)"),
       ("#b08d3a","Phase 6 (later)"), ("#999999","not used")]
lx = 2.0
for i,(co,lab) in enumerate(leg):
    xx = lx + (i%2)*7.6
    yy = 3.3 - (i//2)*0.9
    ax.add_patch(FancyBboxPatch((xx, yy-0.28), 0.55, 0.55, boxstyle="round,pad=0.02,rounding_size=0.1",
        facecolor=co, edgecolor="#111", lw=0.8, zorder=3))
    ax.text(xx+0.8, yy, lab, ha="left", va="center", fontsize=9, color="#333", fontweight="bold", zorder=4)

# orientation note
ax.add_patch(FancyBboxPatch((17.6, 0.5), 12.0, 2.9,
    boxstyle="round,pad=0.05,rounding_size=0.2",
    facecolor="#fff6e5", edgecolor="#e3a008", lw=1.6, zorder=2))
ax.text(18.1, 3.05, "⚠  ORIENTATION", ha="left", va="top",
        fontsize=10, fontweight="bold", color="#9a6700", zorder=3)
ax.text(18.1, 2.45,
        "• Deutsch DTM06-12S (molded on housing).\n"
        "• Bottom row inverted: 12-7 left-to-right.\n"
        "• Wire-entry side; mating face mirrors\n"
        "  left-right.\n"
        "• Function follows WIRE COLOUR — go by it.",
        ha="left", va="top", fontsize=8.3, color="#7a5c1e", zorder=3)

plt.tight_layout()
fig.savefig("im_connector_face.png", dpi=150, facecolor="white", bbox_inches="tight")
fig.savefig("im_connector_face.svg", facecolor="white", bbox_inches="tight")
print("ok")
