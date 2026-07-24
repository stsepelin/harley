# Bike power board: perfboard layout of the protected 12V->5V chain that
# bike-power-chain.py draws schematically (fuse -> reverse-polarity Schottky
# -> load-dump TVS -> mini560 buck @5.0V -> XL74610 ideal-diode -> 5V to the
# board). Separate board from the signal board (switcher noise/current). Parts
# + ratings in bike-power-chain.bom.md / firmware/docs/bike-power-injection.md.
# matplotlib, NOT schemdraw. Regenerate: python3 bike_power_perfboard.py (needs matplotlib).
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import FancyBboxPatch, Rectangle, Circle
import matplotlib.patheffects as pe

def X(c): return c
def Y(r): return -r
LBL=[pe.withStroke(linewidth=2.5,foreground="white")]

fig, ax = plt.subplots(figsize=(14,8.5))
fig.patch.set_facecolor("white"); ax.set_facecolor("white")
ax.set_xlim(-1.0, 25.5); ax.set_ylim(-13.6, 2.2); ax.axis("off")

NC, NR = 24, 12
ax.add_patch(Rectangle((0.4,-12.4),24.2,13.0, facecolor="#fbfaf6",edgecolor="#cfc9b4",lw=1.2,zorder=0))
for r in range(1,NR+1):
    for c in range(1,NC+1):
        ax.add_patch(Circle((X(c),Y(r)),0.11,facecolor="white",edgecolor="#c9c2ac",lw=0.6,zorder=1))

def busline(row,x0,x1,color,label,lx,ly=0.0):
    ax.plot([x0,x1],[Y(row),Y(row)],color=color,lw=5,solid_capstyle="round",zorder=2)
    t=ax.text(lx,Y(row)+ly,label,ha="center",va="center",fontsize=9,fontweight="bold",color=color,zorder=6)
    t.set_path_effects(LBL)

def part(c1,r1,c2,r2,label,color,loff=(0.3,0),fs=8):
    x1,y1,x2,y2=X(c1),Y(r1),X(c2),Y(r2)
    ax.plot([x1,x2],[y1,y2],color=color,lw=6,solid_capstyle="round",zorder=3,alpha=0.9)
    t=ax.text((x1+x2)/2+loff[0],(y1+y2)/2+loff[1],label,ha="left",va="center",fontsize=fs,fontweight="bold",color="#222",zorder=6)
    t.set_path_effects(LBL)

def jumper(c1,r1,c2,r2,color="#b9b090"):
    ax.plot([X(c1),X(c2)],[Y(r1),Y(r2)],color=color,lw=1.8,zorder=4,solid_capstyle="round")
    for (c,r) in [(c1,r1),(c2,r2)]:
        ax.add_patch(Circle((X(c),Y(r)),0.12,facecolor="#e7dfbf",edgecolor="#9a9068",lw=0.5,zorder=5))

def flow(c1,c2,r,color="#d1242f"):
    ax.plot([X(c1),X(c2)],[Y(r),Y(r)],color=color,lw=3,zorder=2,solid_capstyle="round")

def module(c1,r1,c2,r2,name,pins):
    ax.add_patch(FancyBboxPatch((X(c1)-0.4,Y(r2)-0.4),X(c2)-X(c1)+0.8,Y(r1)-Y(r2)+0.8,
        boxstyle="round,pad=0.03,rounding_size=0.15",facecolor="#e7eef7",edgecolor="#3b6ea5",lw=1.3,zorder=3))
    t=ax.text((X(c1)+X(c2))/2,(Y(r1)+Y(r2))/2,name,ha="center",va="center",fontsize=8,fontweight="bold",color="#26456e",zorder=5)
    t.set_path_effects(LBL)
    for c,r,pl in pins:
        ax.add_patch(Circle((X(c),Y(r)),0.15,facecolor="#ffd33d",edgecolor="#555",lw=0.6,zorder=6))
        ax.text(X(c),Y(r)-0.42,pl,ha="center",va="top",fontsize=6.5,fontweight="bold",color="#444",zorder=7)

def terminal(c1,r1,c2,r2,name,pins):
    ax.add_patch(FancyBboxPatch((X(c1)-0.4,Y(r2)-0.4),X(c2)-X(c1)+0.8,Y(r1)-Y(r2)+0.8,
        boxstyle="round,pad=0.03,rounding_size=0.15",facecolor="#e8e2d0",edgecolor="#444",lw=1.4,zorder=3))
    t=ax.text((X(c1)+X(c2))/2,Y(r1)+0.75,name,ha="center",va="center",fontsize=8,fontweight="bold",zorder=5)
    for c,r,pl,co in pins:
        ax.add_patch(Circle((X(c),Y(r)),0.22,facecolor=co,edgecolor="#333",lw=1.0,zorder=6))
        ax.text(X(c),Y(r),pl,ha="center",va="center",fontsize=6.5,fontweight="bold",color="white",zorder=7)

# ground bus
busline(11,2,23,"#1f6feb","GND bus (bare wire)",12,-0.35)

# IN screw terminal (2p): +12V + GND
terminal(1,3,2,7,"IN 2p (harness)",[(2,4,"12V","#d1242f"),(2,6,"GND","#1f6feb")])
ax.text(1.6,Y(8)+0.1,"+12V bike (IM p6)\nGND chassis (IM p5)",ha="left",va="top",fontsize=6.8,color="#666")
jumper(2,6,2,11,"#1f6feb")           # IN GND -> GND bus

# F1 fuse
flow(2,4,4); part(4,4,6,4,"F1 2A","#e3a008",loff=(-0.1,0.55),fs=7.5)
# D2 reverse-polarity schottky (band/cathode -> right/output)
flow(6,8,4); part(8,4,10,4,"D2 SB560","#cf4b2a",loff=(-0.4,0.55),fs=7.5)
ax.text(9.9,Y(4)-0.55,"band→",ha="center",fontsize=6,color="#8a2a12")
# protected node
flow(10,11,4)
ax.add_patch(Circle((X(11),Y(4)),0.17,facecolor="#d1242f",edgecolor="#333",lw=0.8,zorder=6))
ax.text(11,Y(4)+0.5,"protected 12V",ha="center",fontsize=7,color="#9a2233",fontweight="bold",path_effects=LBL)
# +12V tap up to signal board
jumper(11,4,11,1,"#d1242f")
ax.add_patch(Circle((X(11),Y(1)),0.2,facecolor="#f3c6c6",edgecolor="#cf222e",lw=1.1,zorder=6))
ax.text(11.3,Y(1),"→ +12V to SIGNAL board (transceiver)",ha="left",va="center",fontsize=7.5,fontweight="bold",color="#9a2233")
# TVS1 down to GND (band/cathode at top -> +rail)
part(11,6,11,9,"TVS1\nP6KE16A","#e3a008",loff=(0.25,0),fs=6.8)
jumper(11,4,11,6); jumper(11,9,11,11,"#1f6feb")

# mini560 buck
jumper(11,4,13,4)
module(13,3,17,7,"mini560\n12V→5V\nset 5.0V",[(13,4,"VIN"),(13,6,"GI"),(17,4,"VOUT"),(17,6,"GO")])
jumper(13,6,13,11,"#1f6feb")        # GI -> GND
jumper(17,6,17,11,"#1f6feb")        # GO -> GND

# D4 ideal-diode module
flow(17,19,4,"#2da44e")
module(19,3,21,6,"D4\nXL74610\nideal-diode",[(19,4,"IN"),(21,4,"OUT"),(19,6,"GND")])
jumper(19,6,19,11,"#1f6feb")
# VCC_5V node
flow(21,22,4,"#2da44e")
ax.add_patch(Circle((X(22),Y(4)),0.16,facecolor="#2da44e",edgecolor="#333",lw=0.8,zorder=6))
ax.text(22,Y(4)+0.5,"VCC_5V",ha="center",fontsize=6.8,color="#1a7f37",fontweight="bold",path_effects=LBL)

# OUT screw terminal (2p): 5V + GND -> P4
terminal(22,3,23,7,"OUT 2p (P4)",[(23,4,"5V","#2da44e"),(23,6,"GND","#1f6feb")])
jumper(22,4,23,4,"#2da44e"); jumper(23,6,23,11,"#1f6feb")
ax.text(23,Y(8)+0.1,"→ Waveshare\nJ8 pin40 = 5V\n(powers all of P4)",ha="center",va="top",fontsize=6.5,color="#666")

ax.text(12.5,1.75,"Zeppl power board — protected 12V→5V (separate board, same perfboard style)",
        ha="center",fontsize=12.5,fontweight="bold")
ax.text(12.5,-12.9,
 "Order: fuse → reverse-polarity → load-dump TVS → buck → output reverse-block.  ~1.0A cont/~2.0A peak @5V; parts ≥2×.  "
 "TVS P6KE16A 16V standoff/~26V clamp.  Set mini560 to 5.0V for XL74610 (~0V drop); fallback D4=SS34 → 5.35V, verify ~5.0V under load.",
 ha="center",fontsize=7.5,color="#666")

plt.tight_layout()
fig.savefig("bike_power_perfboard.png", dpi=140, facecolor="white", bbox_inches="tight")
fig.savefig("bike_power_perfboard.svg", facecolor="white", bbox_inches="tight")
print("ok")
