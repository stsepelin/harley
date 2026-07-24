# J1850 transceiver perfboard layout (Phase 3 Stage 4 bench -> bike board).
# Physical pad-per-hole placement of the validated bench circuit (RX bare
# divider + high-side TX) on a 5x7 cm perfboard. Companion to the schemdraw
# schematics j1850_tx.py / j1850_rx.py, which stay the electrical source of
# truth; this only fixes where parts and jumpers physically go. Net list +
# solder/ring-out procedure in j1850_perfboard.md.
#
# NOTE: this drawing is matplotlib, NOT schemdraw. Regenerate with:
#   python3 -m venv .venv && .venv/bin/pip install matplotlib
#   .venv/bin/python j1850_perfboard.py
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import FancyBboxPatch, Rectangle, Circle
import matplotlib.patheffects as pe

def X(c): return c
def Y(r): return -r

fig, ax = plt.subplots(figsize=(14, 11))
ax.set_xlim(-1.5, 21.5)
ax.set_ylim(-17.2, 1.6)
ax.axis("off")

# board body (5x7cm ~ 19x27 holes; we use a corner of it)
ax.add_patch(Rectangle((0.4,-16.4),20.2,16.8, facecolor="#12100c",
             edgecolor="#3a3320", lw=1.5, zorder=0))
# hole grid
NC, NR = 21, 16
for r in range(1,NR+1):
    for c in range(1,NC+1):
        ax.add_patch(Circle((X(c),Y(r)),0.12,facecolor="#2a2618",edgecolor="#7a6f45",lw=0.4,zorder=1))

# power / ground bus bare wires
def busline(row,x0,x1,color,label,lx):
    ax.plot([x0,x1],[Y(row),Y(row)],color=color,lw=6,solid_capstyle="round",zorder=2)
    t=ax.text(lx,Y(row),label,ha="center",va="center",fontsize=10,fontweight="bold",color="white",zorder=6)
    t.set_path_effects([pe.withStroke(linewidth=3,foreground=color)])
busline(2,4,19,"#d1242f","+12V bus (bare wire, row 2)",11.5)
busline(15,4,19,"#1f6feb","GND bus (bare wire, row 15)",11.5)

def res(c1,r1,c2,r2,label,color,loff=(0.35,0)):
    x1,y1,x2,y2=X(c1),Y(r1),X(c2),Y(r2)
    ax.plot([x1,x2],[y1,y2],color=color,lw=7,solid_capstyle="round",zorder=3,alpha=0.92)
    t=ax.text((x1+x2)/2+loff[0],(y1+y2)/2+loff[1],label,ha="left",va="center",
              fontsize=9,fontweight="bold",color="black",zorder=6)
    t.set_path_effects([pe.withStroke(linewidth=3,foreground="white")])

def jumper(c1,r1,c2,r2):
    ax.plot([X(c1),X(c2)],[Y(r1),Y(r2)],color="#e8dfa0",lw=2.2,zorder=4,
            solid_capstyle="round")
    for (c,r) in [(c1,r1),(c2,r2)]:
        ax.add_patch(Circle((X(c),Y(r)),0.14,facecolor="#e8dfa0",edgecolor="black",lw=0.5,zorder=5))

def node(c,r,label,col="#ffd33d",dx=0.28,dy=0.28):
    ax.add_patch(Circle((X(c),Y(r)),0.17,facecolor=col,edgecolor="black",lw=0.7,zorder=6))
    if label:
        t=ax.text(X(c)+dx,Y(r)+dy,label,ha="left",va="bottom",fontsize=8,fontweight="bold",zorder=7)
        t.set_path_effects([pe.withStroke(linewidth=2.5,foreground="white")])

def transistor(cells,name,namexy):
    xs=[X(c) for c,r,_ in cells]; ys=[Y(r) for c,r,_ in cells]
    ax.add_patch(FancyBboxPatch((min(xs)-0.45,min(ys)-0.45),max(xs)-min(xs)+0.9,max(ys)-min(ys)+0.9,
        boxstyle="round,pad=0.02,rounding_size=0.12",facecolor="#333",edgecolor="black",zorder=3))
    for c,r,pl in cells:
        ax.add_patch(Circle((X(c),Y(r)),0.16,facecolor="#ffd33d",edgecolor="black",lw=0.7,zorder=6))
        ax.text(X(c)-0.34,Y(r),pl,ha="right",va="center",fontsize=8,fontweight="bold",color="white",zorder=7)
    ax.text(namexy[0],namexy[1],name,ha="center",va="center",fontsize=8.5,fontweight="bold",color="white",zorder=7)

def extpad(c,r,label,tx,ty):
    ax.add_patch(Circle((X(c),Y(r)),0.19,facecolor="#7ee787",edgecolor="black",lw=0.8,zorder=6))
    ax.annotate(label, xy=(X(c),Y(r)), xytext=(tx,ty), fontsize=9.5, fontweight="bold",
                ha="center", va="center", zorder=8,
                arrowprops=dict(arrowstyle="->",color="#2da44e",lw=1.8),
                bbox=dict(boxstyle="round,pad=0.25",facecolor="#dafbe1",edgecolor="#2da44e"))

# ---- components (verified netlist) ----
# R6: +12V(8,2) -> NODE_A(8,5)
res(8,2,8,5,"R6 10k","#bf3989")
# R4: NODE_A(8,5) -> DRAIN(8,8)
res(8,5,8,8,"R4 10k","#0969da")
# R3: TXIN(4,9) -> GATE(6,9)
res(4,9,6,9,"R3 1k","#8250df",loff=(0,0.4))
# Rg: GATE(7,9) -> (7,12)->GND
res(7,9,7,12,"Rg 10k","#8250df")
# R5: COLLECTOR(11,7) -> BUS(11,10)
res(11,7,11,10,"R5 100Ω","#cf222e")
# D1 zener: BUS(13,10) -> (13,13) anode->GND ; band(cathode) at TOP=BUS
res(13,10,13,13,"D1 7.5V","#e3a008",loff=(-1.9,0))
ax.text(12.6,Y(11)+0.5,"band=top",ha="right",va="center",fontsize=7.5,color="#9a6700",
        path_effects=[pe.withStroke(linewidth=2,foreground="white")])
# R1: BUS(15,10) -> NODE_B(15,12)
res(15,10,15,12,"R1 10k","#1a7f37")
# R2: NODE_B(15,12) -> GND(15,15)
res(15,12,15,15,"R2 4.7k","#1a7f37")

# ---- transistors ----
transistor([(7,9,"G"),(8,9,"D"),(9,9,"S")],"",(8,Y(9)))
ax.text(8,Y(9)-1.0,"Q1 IRLZ44N",ha="center",va="center",fontsize=8.5,fontweight="bold",
        path_effects=[pe.withStroke(linewidth=2.5,foreground="white")])
transistor([(11,4,"E"),(11,5,"B"),(11,6,"C")],"",(11,Y(5)))
ax.text(12.15,Y(5),"Q2\n2N2907A",ha="left",va="center",fontsize=8.5,fontweight="bold",
        path_effects=[pe.withStroke(linewidth=2.5,foreground="white")])

# ---- nodes ----
node(8,5,"NODE_A (base Q2)")
node(11,10,"BUS")
node(15,12,"NODE_B")

# ---- jumpers ----
jumper(6,9,7,9)     # R3 -> Q1 gate (GATE)
jumper(8,8,8,9)     # R4 -> Q1 drain (DRAIN)
jumper(8,5,11,5)    # NODE_A across to Q2 base
jumper(11,4,11,2)   # Q2 emitter -> +12V bus
jumper(11,6,11,7)   # Q2 collector -> R5 (COLLECTOR)
jumper(11,10,13,10) # BUS
jumper(13,10,15,10) # BUS
jumper(9,9,9,15)    # Q1 source -> GND bus
jumper(7,12,7,15)   # Rg -> GND bus
jumper(13,13,13,15) # D1 anode -> GND bus
jumper(15,12,17,12) # NODE_B -> RX out

# ---- external pads ----
extpad(4,2,"+12V IN\n(PSU/bike)",2.0,Y(1)+0.2)
extpad(4,15,"GND IN\n(P4 GND / PSU-)",2.0,Y(16)-0.2)
extpad(4,9,"TX GPIO24",1.7,Y(7)+0.2)
extpad(18,10,"to J1850 BUS\n(pin 7)",19.4,Y(9))
extpad(17,12,"RX GPIO20",19.4,Y(13))
# wire BUS node to external bus pad
jumper(15,10,18,10)

ax.text(10.5,1.25,"Zeppl J1850 transceiver — perfboard layout (5x7cm, pad-per-hole, TOP/component-side view). Bike build: no Rpd.",
        ha="center",fontsize=12,fontweight="bold")
ax.text(10.5,0.6,"Yellow lines = jumper wires (solder side). Thick bars = parts. Red/blue = bus wires. Green = external leads.",
        ha="center",fontsize=9,color="#444")

ax.text(0.6,-16.9,
 "VERIFY BEFORE SOLDER: Q1 IRLZ44N legs G-D-S left->right (label toward you). "
 "Q2 2N2907A: middle leg = Base; DMM diode-test to set E/C, EMITTER to +12V (top), COLLECTOR down to R5. "
 "D1 zener band (cathode) faces BUS (top). After soldering, ring out every net vs the table before powering.",
 fontsize=8.5, va="top", ha="left",
 bbox=dict(boxstyle="round,pad=0.5",facecolor="#fff8c5",edgecolor="#d4a72c"))

plt.tight_layout()
fig.savefig("j1850_perfboard.png", dpi=140, bbox_inches="tight")
fig.savefig("j1850_perfboard.svg", bbox_inches="tight")
print("ok")
