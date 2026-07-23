# J1850 signal board (Phase 3 Stage 4 -> bike): perfboard layout of the J1850
# transceiver (RX bare divider + high-side TX) + the 6 discrete 12V input
# dividers (10k/2.7k + 3V3 clamp; turn L/R, high-beam, neutral, oil, ignition).
# Bike build (Rpd omitted). SINGLE central ground bus (row 15); dividers are
# flipped (GND up to the central bus, harness inputs at the bottom edge).
# Harness = physical screw terminals: one PWR 3-pin (+12V/GND/BUS, top-left,
# orthogonally routed) + two signal 3-pin blocks sitting directly on the divider
# inputs (5 mm divider pitch -> a 3-pin block lands on 3 divider inputs, no
# routing). P4 = TX (gate) / RX (NODE_B) + 6 GPIO pads on the divider nodes.
# Companion to schemdraw j1850_tx.py / j1850_rx.py / discrete_divider.py (the
# electrical source of truth). Power chain is a SEPARATE board.
# matplotlib, NOT schemdraw. Regenerate: python3 j1850_signal_board.py (matplotlib).
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import FancyBboxPatch, Rectangle, Circle
import matplotlib.patheffects as pe

def X(c): return c
def Y(r): return -r
LBL=[pe.withStroke(linewidth=2.5,foreground="white")]

fig, ax = plt.subplots(figsize=(11,15))
fig.patch.set_facecolor("white")
ax.set_facecolor("white")
ax.set_xlim(-1.2, 20.5)
ax.set_ylim(-27.6, 1.8)
ax.axis("off")

# board outline (portrait 5x7cm ~ 19 x 27 holes)
ax.add_patch(Rectangle((0.4,-27.3),19.2,28.0, facecolor="#fbfaf6",
             edgecolor="#cfc9b4", lw=1.2, zorder=0))
NC, NR = 19, 27
for r in range(1,NR+1):
    for c in range(1,NC+1):
        ax.add_patch(Circle((X(c),Y(r)),0.11,facecolor="white",edgecolor="#c9c2ac",lw=0.6,zorder=1))

def busline(row,x0,x1,color,label,lx,ly=0.0):
    ax.plot([x0,x1],[Y(row),Y(row)],color=color,lw=5,solid_capstyle="round",zorder=2)
    if label:
        t=ax.text(lx,Y(row)+ly,label,ha="center",va="center",fontsize=9,fontweight="bold",color=color,zorder=6)
        t.set_path_effects(LBL)

def res(c1,r1,c2,r2,label,color,loff=(0.3,0),fs=8):
    x1,y1,x2,y2=X(c1),Y(r1),X(c2),Y(r2)
    ax.plot([x1,x2],[y1,y2],color=color,lw=5,solid_capstyle="round",zorder=3,alpha=0.9)
    t=ax.text((x1+x2)/2+loff[0],(y1+y2)/2+loff[1],label,ha="left",va="center",fontsize=fs,fontweight="bold",color="#222",zorder=6)
    t.set_path_effects(LBL)

def jumper(c1,r1,c2,r2):
    ax.plot([X(c1),X(c2)],[Y(r1),Y(r2)],color="#b9b090",lw=1.6,zorder=4,solid_capstyle="round")
    for (c,r) in [(c1,r1),(c2,r2)]:
        ax.add_patch(Circle((X(c),Y(r)),0.12,facecolor="#e7dfbf",edgecolor="#9a9068",lw=0.5,zorder=5))

def node(c,r,label,dx=0.26,dy=0.24,col="#ffd33d"):
    ax.add_patch(Circle((X(c),Y(r)),0.15,facecolor=col,edgecolor="#555",lw=0.6,zorder=6))
    if label:
        t=ax.text(X(c)+dx,Y(r)+dy,label,ha="left",va="bottom",fontsize=7.5,fontweight="bold",zorder=7)
        t.set_path_effects(LBL)

def transistor(cells,name,namexy):
    xs=[X(c) for c,r,_ in cells]; ys=[Y(r) for c,r,_ in cells]
    ax.add_patch(FancyBboxPatch((min(xs)-0.42,min(ys)-0.42),max(xs)-min(xs)+0.84,max(ys)-min(ys)+0.84,
        boxstyle="round,pad=0.02,rounding_size=0.1",facecolor="#eceae2",edgecolor="#555",lw=1.1,zorder=3))
    for c,r,pl in cells:
        ax.add_patch(Circle((X(c),Y(r)),0.14,facecolor="#ffd33d",edgecolor="#555",lw=0.6,zorder=6))
        ax.text(X(c)-0.3,Y(r),pl,ha="right",va="center",fontsize=7.5,fontweight="bold",color="#333",zorder=7)
    t=ax.text(namexy[0],namexy[1],name,ha="center",va="center",fontsize=8,fontweight="bold",color="#333",zorder=7)
    t.set_path_effects(LBL)

def extpad(c,r,label,tx,ty,fs=8.5):
    ax.add_patch(Circle((X(c),Y(r)),0.17,facecolor="#c7f0cf",edgecolor="#2da44e",lw=1.0,zorder=6))
    ax.annotate(label, xy=(X(c),Y(r)), xytext=(tx,ty), fontsize=fs, fontweight="bold",
                ha="center", va="center", zorder=8, color="#1a5c30",
                arrowprops=dict(arrowstyle="->",color="#2da44e",lw=1.4))

# ================= J1850 transceiver (rows 2-15) =================
busline(2,4,18,"#d1242f","+12V",11,0.35)
busline(15,2,19,"#1f6feb","GND  (single central bus)",10,-0.35)

res(8,2,8,5,"R6 10k","#bf3989")
res(8,5,8,8,"R4 10k","#0969da")
res(4,9,6,9,"R3 1k","#8250df",loff=(0,0.4))
res(7,9,7,12,"Rg 10k","#8250df")
res(11,7,11,10,"R5 100Ω","#cf222e")
res(13,10,13,13,"D1 7.5V","#e3a008",loff=(-1.85,0))
ax.text(12.5,Y(11)+0.5,"band=top",ha="right",va="center",fontsize=7,color="#9a6700",path_effects=LBL)
res(15,10,15,12,"R1 10k","#1a7f37")
res(15,12,15,15,"R2 4.7k","#1a7f37")

transistor([(7,9,"G"),(8,9,"D"),(9,9,"S")],"",(8,Y(9)))
ax.text(8,Y(9)-0.95,"Q1 IRLZ44N",ha="center",va="center",fontsize=7.5,fontweight="bold",color="#333",path_effects=LBL)
transistor([(11,4,"E"),(11,5,"B"),(11,6,"C")],"",(11,Y(5)))
ax.text(12.1,Y(5),"Q2\n2N2907A",ha="left",va="center",fontsize=7.5,fontweight="bold",color="#333",path_effects=LBL)

node(8,5,"NODE_A")
node(11,10,"BUS")
node(15,12,"NODE_B")

jumper(6,9,7,9); jumper(8,8,8,9); jumper(8,5,11,5); jumper(11,4,11,2)
jumper(11,6,11,7); jumper(11,10,13,10); jumper(13,10,15,10)
jumper(9,9,9,15); jumper(7,12,7,15); jumper(13,13,13,15)
# NOTE: removed jumper(15,12,17,12) — it extended NODE_B (RX) rightward to the
# old RX pad column (col 17), which sat one hole from the BUS-terminal GND pin
# (col 18) — a bridge hazard that shorts NODE_B to GND (bypasses R2 -> BUS-GND
# reads R1=10k). The P4 RX pad now sits directly on NODE_B (col 15), so this
# jumper is vestigial. Do not solder it.
jumper(15,10,18,10)


ax.text(10,-16.7,"— J1850 transceiver (Phase 3) —",ha="center",fontsize=9,style="italic",color="#777")

# ===== 6 discrete 12V dividers - FLIPPED, single central GND (row 15) =====
# terminal(bottom) -> Ra -> node -> Rb/Z -> GND(top, central bus)
DIV=[("turn-L",3),("turn-R",5),("beam",7),("neutral",9),("oil",11),("ign",13)]
for name,c in DIV:
    res(c,16,c,19,"2.7k","#0a7ea4",loff=(0.22,0),fs=7)     # Rb: central GND(top)->node
    jumper(c,15,c,16)                                      # -> central GND bus
    res(c,19,c,22,"10k","#0a7ea4",loff=(0.22,0),fs=7)      # Ra: node->terminal(bottom)
    # 3V3 clamp zener node->GND in adjacent column (band/cathode at node side)
    jumper(c,19,c+1,19)
    res(c+1,16,c+1,19,"Z\n3V3","#e3a008",loff=(0.18,0),fs=6.5)
    jumper(c+1,15,c+1,16)
    # harness screw terminal at the BOTTOM edge (sits on the divider input)
    ax.add_patch(Rectangle((X(c)-0.32,Y(22)-0.32),0.64,0.64,facecolor="#e8e2d0",edgecolor="#444",lw=1.1,zorder=6))
    ax.add_patch(Circle((X(c),Y(22)),0.15,facecolor="#2da44e",edgecolor="#222",lw=0.8,zorder=7))
    t=ax.text(X(c),Y(22)-0.5,name,ha="center",va="top",fontsize=7.5,fontweight="bold",color="#1a5c30",zorder=8); t.set_path_effects(LBL)
    # P4 GPIO dupont pad at the node
    ax.add_patch(Circle((X(c),Y(19)),0.16,facecolor="#dcd0ff",edgecolor="#6f42c1",lw=1.0,zorder=6))
    ax.text(X(c)-0.28,Y(19),"G",ha="right",va="center",fontsize=6,fontweight="bold",color="#5a32a3",zorder=8)



# ---- orthogonal grid router ----
def orout(pts,color,lw=2.0):
    xs=[X(c) for c,r in pts]; ys=[Y(r) for c,r in pts]
    ax.plot(xs,ys,color=color,lw=lw,zorder=1.7,solid_capstyle="round",solid_joinstyle="round",alpha=0.9)
    for c,r in [pts[0],pts[-1]]:
        ax.add_patch(Circle((X(c),Y(r)),0.12,facecolor=color,edgecolor="#333",lw=0.4,zorder=5))
# ---- PWR 2-pin screw terminal (top-left): +12V / GND ----
ax.add_patch(FancyBboxPatch((0.3,Y(4)-0.55),1.5,3.1,boxstyle="round,pad=0.03,rounding_size=0.1",facecolor="#d8cdb0",edgecolor="#444",lw=1.5,zorder=5.5))
ax.text(1.05,Y(1)+0.15,"PWR 2-pin",ha="center",fontsize=7,fontweight="bold")
for r,lab,co in [(2,"12V","#d1242f"),(4,"GND","#1f6feb")]:
    ax.add_patch(Circle((1.05,Y(r)),0.21,facecolor=co,edgecolor="#222",lw=1,zorder=7))
    ax.text(1.05,Y(r),lab,ha="center",va="center",fontsize=5.4,fontweight="bold",color="white",zorder=8)
orout([(1,2),(4,2)],"#d1242f")             # +12V -> +12V bus
orout([(1,4),(1,15),(2,15)],"#1f6feb")     # GND  -> central bus
# ---- BUS 2-pin screw terminal sits ON the BUS node (right side): no long routing ----
ax.add_patch(FancyBboxPatch((X(18)-0.62,Y(12)-0.62),1.24,3.0,boxstyle="round,pad=0.03,rounding_size=0.1",facecolor="#d8cdb0",edgecolor="#444",lw=1.5,zorder=5.4))
ax.text(X(18),Y(9)+0.15,"BUS 2-pin (pin7)",ha="center",fontsize=6.2,fontweight="bold")
ax.add_patch(Circle((X(18),Y(10)),0.21,facecolor="#7a5cff",edgecolor="#222",lw=1,zorder=7))
ax.text(X(18),Y(10),"BUS",ha="center",va="center",fontsize=4.8,fontweight="bold",color="white",zorder=8)
ax.add_patch(Circle((X(18),Y(12)),0.21,facecolor="#1f6feb",edgecolor="#222",lw=1,zorder=7))
ax.text(X(18),Y(12),"GND",ha="center",va="center",fontsize=4.8,fontweight="bold",color="white",zorder=8)
orout([(18,12),(18,15)],"#1f6feb")         # BUS-terminal GND pin -> central bus (short)

# ---- two 3-pin harness screw terminals sit DIRECTLY on the divider inputs (row 22) ----
for cols,lab in [([3,5,7],"turn-L / turn-R / beam"),([9,11,13],"neutral / oil / ign")]:
    x0,x1=X(cols[0]),X(cols[-1])
    ax.add_patch(FancyBboxPatch((x0-0.62,Y(23)-0.15),x1-x0+1.24,1.5,boxstyle="round,pad=0.03,rounding_size=0.1",
        facecolor="#d8cdb0",edgecolor="#444",lw=1.5,zorder=5.4))
    ax.text((x0+x1)/2,Y(24.55),"3-pin screw terminal ("+lab+")",ha="center",fontsize=6.0,fontweight="bold",color="#3d3524",zorder=8)

# ---- P4 dupont pads: TX at gate, RX at NODE_B (6x GPIO already marked on the divider nodes) ----
for c,r,lab in [(4,9,"TX"),(15,12,"RX")]:
    ax.add_patch(Circle((X(c),Y(r)),0.19,facecolor="#c9b6ff",edgecolor="#6f42c1",lw=1.2,zorder=7))
    ax.text(X(c)-0.32 if lab=="TX" else X(c)+0.32,Y(r),lab,ha="right" if lab=="TX" else "left",va="center",fontsize=6.5,fontweight="bold",color="#42287a",zorder=8)
ax.text(10,1.05,"Physical screw terminals: PWR 2-pin (top-left) + BUS 2-pin (on its node, right) + 2x signal 3-pin (bottom, on divider inputs). "
        "Single central ground. P4 = violet (6 GPIO + TX/RX).",ha="center",fontsize=7.0,color="#555")

# title + minimal note
ax.text(10,1.35,"Zeppl signal board (5×7cm) — J1850 transceiver + 6 discrete dividers",
        ha="center",fontsize=12.5,fontweight="bold")
ax.text(10,-26.6,
 "Power chain (12V→5V, mini560) stays on a SEPARATE board (switcher noise / current).  "
 "Verify transistor pinouts + D1 band before solder; ring out every net before power.",
 ha="center",fontsize=8,color="#666")

plt.tight_layout()
fig.savefig("j1850_signal_board.png", dpi=140, facecolor="white", bbox_inches="tight")
fig.savefig("j1850_signal_board.svg", facecolor="white", bbox_inches="tight")
print("ok")
