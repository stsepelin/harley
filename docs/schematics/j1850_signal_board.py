import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import FancyBboxPatch, Rectangle, Circle
import matplotlib.patheffects as pe

def X(c): return c
def Y(r): return -r
LBL=[pe.withStroke(linewidth=2.0,foreground="white")]

fig, ax = plt.subplots(figsize=(12,15.5))
fig.patch.set_facecolor("white"); ax.set_facecolor("white")
ax.set_xlim(-0.6,19.4); ax.set_ylim(-25.2,1.7); ax.axis("off")

NC,NR=18,24
ax.add_patch(Rectangle((0.4,-24.4),18.2,25.0,facecolor="#fbfaf6",edgecolor="#cfc9b4",lw=1.2,zorder=0))
for r in range(1,NR+1):
    for c in range(1,NC+1):
        ax.add_patch(Circle((X(c),Y(r)),0.095,facecolor="white",edgecolor="#c9c2ac",lw=0.5,zorder=1))

def used(c,r,col="#e7dfbf"):
    ax.add_patch(Circle((X(c),Y(r)),0.11,facecolor=col,edgecolor="#9a9068",lw=0.4,zorder=5))
def bus(row,c0,c1,color,label,lx,ly=0.0):
    ax.plot([X(c0),X(c1)],[Y(row),Y(row)],color=color,lw=4.2,solid_capstyle="round",zorder=2)
    if label:
        t=ax.text(lx,Y(row)+ly,label,ha="center",fontsize=8,fontweight="bold",color=color,zorder=6); t.set_path_effects(LBL)
def res(c1,r1,c2,r2,label,color,loff=(0.24,0),fs=6.2):
    ax.plot([X(c1),X(c2)],[Y(r1),Y(r2)],color=color,lw=4.0,solid_capstyle="round",zorder=3,alpha=0.9); used(c1,r1);used(c2,r2)
    t=ax.text((X(c1)+X(c2))/2+loff[0],(Y(r1)+Y(r2))/2+loff[1],label,ha="left",va="center",fontsize=fs,fontweight="bold",color="#222",zorder=6); t.set_path_effects(LBL)
def zen(c1,r1,c2,r2,label,loff=(0.2,0),fs=5.5):
    ax.plot([X(c1),X(c2)],[Y(r1),Y(r2)],color="#e3a008",lw=4.0,solid_capstyle="round",zorder=3); used(c1,r1);used(c2,r2)
    ax.add_patch(Rectangle((X(c1)-0.14,Y(r1)-0.02),0.28,0.09,facecolor="#7a5c00",zorder=4))
    t=ax.text((X(c1)+X(c2))/2+loff[0],(Y(r1)+Y(r2))/2,label,ha="left",va="center",fontsize=fs,fontweight="bold",color="#9a6700",zorder=6); t.set_path_effects(LBL)
def jump(c1,r1,c2,r2,color="#b9b090",lw=1.4):
    ax.plot([X(c1),X(c2)],[Y(r1),Y(r2)],color=color,lw=lw,zorder=2.5,solid_capstyle="round"); used(c1,r1);used(c2,r2)
def node(c,r,label="",dx=0.22,dy=0.2,col="#ffd33d"):
    ax.add_patch(Circle((X(c),Y(r)),0.14,facecolor=col,edgecolor="#555",lw=0.6,zorder=6))
    if label:
        t=ax.text(X(c)+dx,Y(r)+dy,label,ha="left",va="bottom",fontsize=6.5,fontweight="bold",zorder=7); t.set_path_effects(LBL)
def tr(cells,name,nxy,fs=6):
    xs=[X(c) for c,r,_ in cells]; ys=[Y(r) for c,r,_ in cells]
    ax.add_patch(FancyBboxPatch((min(xs)-0.38,min(ys)-0.38),max(xs)-min(xs)+0.76,max(ys)-min(ys)+0.76,
        boxstyle="round,pad=0.02,rounding_size=0.1",facecolor="#eceae2",edgecolor="#555",lw=1.0,zorder=3))
    for c,r,pl in cells:
        ax.add_patch(Circle((X(c),Y(r)),0.12,facecolor="#ffd33d",edgecolor="#555",lw=0.5,zorder=6))
        ax.text(X(c),Y(r)-0.32,pl,ha="center",va="top",fontsize=5.5,fontweight="bold",color="#333",zorder=7)
    t=ax.text(nxy[0],nxy[1],name,ha="center",va="center",fontsize=fs,fontweight="bold",color="#333",zorder=7); t.set_path_effects(LBL)
def screw(pins,title,tx,ty,vert=False,flip=False):
    # each screw position is 2 holes wide: vert -> spans cols c..c+1; horizontal -> spans rows r..r+1
    # flip (vert only): single solder hole sits on the RIGHT edge, body extends LEFT (for terminals hugging the right board edge)
    xs=[X(c) for c,r,_,_ in pins]; ys=[Y(r) for c,r,_,_ in pins]
    if vert:
        bx = min(xs)-1.55 if flip else min(xs)-0.55
        ax.add_patch(FancyBboxPatch((bx,min(ys)-0.7),(max(xs)-min(xs))+2.1,(max(ys)-min(ys))+1.4,
            boxstyle="round,pad=0.03,rounding_size=0.12",facecolor="#e8e2d0",edgecolor="#444",lw=1.3,zorder=5.5))
    else:
        ax.add_patch(FancyBboxPatch((min(xs)-0.7,min(ys)-0.7),(max(xs)-min(xs))+1.4,(max(ys)-min(ys))+2.0,
            boxstyle="round,pad=0.03,rounding_size=0.12",facecolor="#e8e2d0",edgecolor="#444",lw=1.3,zorder=5.5))
    for c,r,lab,co in pins:
        if vert:
            bodyx = X(c)-1.32 if flip else X(c)-0.32
            labx  = X(c)-0.62 if flip else X(c)+0.62
            ax.add_patch(FancyBboxPatch((bodyx,Y(r)-0.3),1.64,0.6,boxstyle="round,pad=0.02,rounding_size=0.16",facecolor=co,edgecolor="#222",lw=1,zorder=7))
            ax.add_patch(Circle((X(c),Y(r)),0.09,facecolor="#fff",edgecolor="#333",lw=0.6,zorder=8))   # single solder hole (left, or right if flip)
            ax.text(labx,Y(r),lab,ha="center",va="center",fontsize=4.6,fontweight="bold",color="white",zorder=9)
        else:
            ax.add_patch(FancyBboxPatch((X(c)-0.3,Y(r)-0.32),0.6,1.64,boxstyle="round,pad=0.02,rounding_size=0.16",facecolor=co,edgecolor="#222",lw=1,zorder=7))
            ax.add_patch(Circle((X(c),Y(r)),0.09,facecolor="#fff",edgecolor="#333",lw=0.6,zorder=8))   # single solder hole
            ax.text(X(c),Y(r)+0.62,lab,ha="center",va="center",fontsize=4.4,fontweight="bold",color="white",zorder=9)
    ax.text(tx,ty,title,ha="center",fontsize=6.0,fontweight="bold",color="#3d3524",zorder=8)

RED="#d1242f"; BLU="#1f6feb"; PUR="#7a5cff"; GRN="#1a7f37"; TEAL="#0a7ea4"; VIO="#6f42c1"; GPIO="#2f8f4e"

# ===== rails =====
bus(1,1,18,RED,"+12V",9,0.3)
# GND now runs down the RIGHT edge (col18), fed from the transceiver rail at row11 —
# no staircase and no central rail needed (see comb + divider block below).

# ===== left: ONE 3-pin terminal (+12V / BUS / GND), pins 2 holes wide =====
screw([(1,3,"12V",RED),(1,5,"BUS",PUR),(1,7,"GND",BLU)],"PWR/BUS 3p",2.4,-1.7,vert=True)
jump(1,3,1,1,RED,1.8)                                             # +12V -> rail (from the single left hole)
jump(1,5,3,5,PUR,1.6); jump(3,5,3,9,PUR,1.6); jump(3,9,4,9,PUR,1.6)  # BUS -> BUS node
jump(1,7,1,11,BLU,1.6)                                            # GND -> rail

# ===== transceiver (rows 2-12) — ORTHOGONAL (wires run under components) =====
bus(11,1,18,BLU,"",0)   # transceiver GND rail
# Q2 high-side PNP, placed so its base sits on the NODE_A row
tr([(4,3,"E"),(4,4,"B"),(4,5,"C")],"Q2 2N2907A",(5.6,Y(4)))
jump(4,3,4,1,RED,1.6)                                  # E -> +12V (vertical)
# R6 + R4 STACKED in col6: +12V - R6 - NODE_A - R4 - drain
res(6,1,6,4,"R6 10k",RED); node(6,4,"NODE_A")
jump(4,4,6,4,"#8a6d3b",1.6)                            # B -> NODE_A (horizontal, under nothing)
res(6,4,6,7,"R4 10k","#0969da")
# Q1 low-side NFET as D-S-G so the gate exits right with no crossings
tr([(6,8,"D"),(7,8,"S"),(8,8,"G")],"Q1 IRLZ44N",(7,Y(8)+0.9))
jump(6,7,6,8,"#0969da")                                # R4 bottom -> drain (vertical)
jump(7,8,7,11,BLU)                                     # source -> GND (vertical)
jump(8,8,9,8,VIO,1.5)                                  # IRL gate leg -> gate NODE (resistors don't share the FET hole)
node(9,8,"",col="#ffd33d")
res(9,8,9,11,"Rg 10k",VIO)                             # Rg: gate node -> GND
res(9,8,12,8,"R3 1k",VIO,loff=(0,0.42))                # R3: gate node -> TX
node(12,8,"TX",dx=-0.2,dy=-0.7,col="#c9b6ff")
# Q2 collector -> R5 -> BUS
jump(4,5,4,6,RED); res(4,6,4,9,"R5 100Ω",RED); node(4,9,"BUS")  # BUS fed from the 3-pin (see left)
zen(4,9,4,11,"D1 7.5V",loff=(0.2,0))                   # BUS -> GND clamp
# BUS span to the RX divider: up at col5, then along row6 UNDER R4 (keeps it off the TX row)
jump(4,9,5,9,PUR,1.6); jump(5,9,5,6,PUR,1.6); jump(5,6,10,6,PUR,1.6)
res(10,6,13,6,"R1 10k",GRN); node(13,6,"NODE_B")
res(13,6,13,9,"R2 4.7k",GRN); jump(13,9,13,11,BLU)     # R1 & R2 SHARE NODE_B (13,6) — shifted LEFT so RX goes straight into the comb
node(13,6,"RX",dx=0.25,dy=-0.55,col="#c9b6ff")

# ===== GND rail down the RIGHT edge (col18) + P4 comb at col15 (3-hole room for the 3V3 clamps) =====
ax.plot([X(18),X(18)],[Y(11),Y(24)],color=BLU,lw=4.2,solid_capstyle="round",zorder=2)   # right-edge GND rail (fed from transceiver rail @ row11)
# comb shell drawn as a light BACKGROUND (low zorder) so the RX/TX wires that meet the pins stay visible
ax.add_patch(FancyBboxPatch((X(15)-0.42,Y(24)-0.45),0.84,(Y(6)-Y(24))+0.9,boxstyle="round,pad=0.02,rounding_size=0.1",
    facecolor="#efeaff",edgecolor=VIO,lw=1.4,zorder=1.3))
comb_pins=[("RX·GP20",6,GRN),("TX",8,PUR),("GND",11,BLU),
           ("t-L",12,GPIO),("t-R",14,GPIO),("beam",16,GPIO),
           ("neu",20,GPIO),("oil",22,GPIO),("ign",24,GPIO)]
jump(12,8,15,8,PUR,1.5)                             # TX node(12,8) -> comb(15,8) STRAIGHT; (13,8) passes UNDER R2 body
jump(13,6,15,6,GRN,1.5)                             # RX NODE_B(13,6) -> comb(15,6) STRAIGHT (no bend)
for lab,r,co in comb_pins:
    ax.add_patch(Circle((X(15),Y(r)),0.17,facecolor=co,edgecolor="#222",lw=0.9,zorder=7))
    ax.text(X(18)+0.32,Y(r),lab,ha="left",va="center",fontsize=5.4,fontweight="bold",color="#42287a",zorder=8)
# GND comb pin (15,11) sits on the transceiver GND rail (row11), which reaches the col18 rail — connected

# ===== +12V/GND transit OUTPUT (screw) -> power board — hugs the RIGHT edge, holes flipped to the right =====
screw([(18,2,"12V",RED),(18,4,"GND",BLU)],"+12V/GND → power board",16.6,-0.2,vert=True,flip=True)
jump(18,2,18,1,RED,1.6)      # +12V out -> +12V rail (extended to col18)
# GND out drops STRAIGHT down the right edge (col18) to the GND rail — no staircase needed
jump(18,4,18,11,BLU,1.5)

# ===== 6 dividers — GND on the RIGHT edge; middle carries only thin input wires =====
# Each lane: thin input wire -> Ra 10k -> node -> output to comb. Rb (2k7) lies HORIZONTAL in the
# empty gap row next to the lane and hops to the right GND rail; 3V3 is a clamp right at the connector.
# two 3-pin screw terminals on the LEFT — PITCH-2 (pins every other hole), lanes aligned
screw([(1,12,"t-L","#2da44e"),(1,14,"t-R","#2da44e"),(1,16,"beam","#2da44e")],"3p harness A",2.2,-10.6,vert=True)
screw([(1,20,"neu","#2da44e"),(1,22,"oil","#2da44e"),(1,24,"ign","#2da44e")],"3p harness B",2.2,-25.4,vert=True)

def lane(g,rr,gap):
    jump(1,rr,8,rr,"#2da44e",1.2)                      # thin input wire: screw -> Ra
    res(8,rr,11,rr,"10k",TEAL,loff=(0,-0.55),fs=4.2)   # Ra 10k, node at col11
    node(11,rr)
    jump(11,rr,15,rr,GPIO,1.4)                          # node -> comb pin (output, col15)
    jump(15,rr,16,rr,"#8a6d3b",1.1)                     # comb pin -> zener via a short jumper (like the Rb; keeps the pin hole free)
    zen(16,rr,18,rr,"",fs=4)                            # 3V3 clamp: -> GND rail(18)
    jump(11,rr,11,gap,TEAL,1.1)                         # node -> gap row
    res(11,gap,14,gap,"2k7",TEAL,loff=(0,0.42),fs=3.8) # Rb HORIZONTAL in the gap row (body clear of the comb col)
    jump(14,gap,18,gap,BLU,1.1)                         # Rb -> right GND rail (thin wire under the header)

DECKA=[("G1",12,13),("G2",14,15),("G3",16,17)]   # gap row BELOW each lane
DECKB=[("G4",20,19),("G5",22,21),("G6",24,23)]   # gap row ABOVE each lane
for g,rr,gap in DECKA+DECKB: lane(g,rr,gap)

ax.text(9,1.3,"Zeppl signal board v4 (18×24) — J1850 transceiver + 6× 12V dividers → single P4 comb",ha="center",fontsize=10.5,fontweight="bold")
ax.text(9,0.62,"GND rail down the right edge · Rb in the gaps · zener clamps via a jumper at the connector · inputs 2×3p on the left",
        ha="center",fontsize=6.0,color="#666")
ax.text(9,0.05,"P4: RX=GPIO20 · TX=J1850 drive · GND · 6 outputs = turn-L/R, beam, neu, oil, ign (free header GPIOs — assign and confirm via wiggle test, PINS.md) · BUS=IM pin7",
        ha="center",fontsize=5.4,color="#888")

plt.tight_layout()
fig.savefig("j1850_signal_board.png", dpi=150, facecolor="white", bbox_inches="tight")
fig.savefig("j1850_signal_board.svg", facecolor="white", bbox_inches="tight")
print("ok")
