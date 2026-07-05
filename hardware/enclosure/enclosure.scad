// ============================================================================
// Waveshare ESP32-P4-WIFI6-Touch-LCD-3.4C -- parametric round enclosure
// REAR-BOLT BOARD FIXATION REV (motorcycle / vibration). No threads, no snap-fits.
// ----------------------------------------------------------------------------
// The PCB is mounted the way it was designed to be: BOLTED at its 4 mount holes
// (74.5 mm square) to internal standoffs, with the bolts driven from the REAR
// through the open back (the rear cover is removable, so the holes -- though under
// the glass from the front -- ARE reachable from behind). Bolts land in METAL (M3
// brass heat-set inserts in chunky, wall-gusseted standoffs), never printed threads.
//
//   [ bezel ring ]        frames + protects the glass edge (light duty)
//   [ glass + PCB ]        one bonded block, 7.6 mm (glass 6 + pcb 1.6), touching;
//                          inserts from the front, glass to a front lip
//   [ standoffs ]          4 chunky gusseted posts behind the PCB at the 74.5 sq;
//                          M3 heat-set inserts -- the board's primary load path
//   [ deep cavity ]        ~50 mm for breadboard + mini560 + wiring
//   [ rear cover ]         REMOVABLE -- reach the 4 board bolts + wiring, then close
//
// Direct board bolting is the primary fixation; edge-compression is dropped as the
// main load path (the bezel now just frames/protects the glass). A gasket under the
// glass rim + between PCB and standoffs stays for vibration damping.
//
// mount_square = 74.5 is LOAD-BEARING again (the rear board bolts use it).
//
// Modes (-D 'part="..."'):
//   rear_case | bezel | rear_cover | calibration_base | assembly
// Tiers (-D 'tier="..."'):  simple | robust | compromise(default)
//
// Coordinate frame: z=0 at the front rim (glass front), +z runs REARWARD.
// ============================================================================

part = "rear_case";     // rear_case | bezel | rear_cover | calibration_base | assembly
tier = "compromise";    // simple | robust | compromise
fastener = "auto";      // auto (tier default) | heatset | captured_nut

$fn = 128;              // $fn = circle facet count -> smooth round shell

// ---- CONFIRMED / locked ----------------------------------------------------
glass_od           = 115;   // measured cover-glass OD (== overall display face)
glass_thickness    = 6;
display_active_dia  = 87.6; // KEEP OPEN -- never covered
pcb_w = 86; pcb_h = 65;     // PCB outline (smaller than the glass; fully backed by it)
pcb_thickness = 1.6;
mount_square = 74.5;        // LOAD-BEARING: the 4 rear board bolts mount here
internal_depth = 50;        // electronics cavity depth (breadboard + mini560 + wiring)

// ---- Bonded block: glass + PCB, faces touching (7.6 mm) ---------------------
block_gap = 0;              // glass-rear to PCB-front; 0 = touching (bonded)

// ---- Tier presets (tune per build tier; don't fork geometry) ----------------
function t_wall(t)    = t=="robust" ? 4.0 : 3.0;
function t_floor(t)   = t=="robust" ? 4.0 : t=="simple" ? 4.0 : 3.5;
function t_screws(t)  = t=="simple" ? 4   : 6;   // keep 6 perimeter screws (simple=4)
function t_gasket(t)  = t=="robust" ? 1.0 : t=="simple" ? 1.0 : 0.6;
function t_fastener(t)= t=="simple" ? "captured_nut" : "heatset";
function t_standoff(t)= t=="robust" ? 11 : t=="simple" ? 9 : 10;   // chunky board-post dia

wall            = t_wall(tier);
floor_thickness = t_floor(tier);        // rear_cover thickness (the removable "floor")
screw_count     = t_screws(tier);
gasket_gap      = t_gasket(tier);       // foam/silicone slot behind the glass rim
fastener_r      = fastener=="auto" ? t_fastener(tier) : fastener;
standoff_od     = t_standoff(tier);     // >= 8, chunky; wall-gusseted, never a thin stalk

// ---- Fits ------------------------------------------------------------------
glass_fit = 0.5;            // diametral clearance for the glass in its bore
block_fit = 0.5;

// ---- Fastening -------------------------------------------------------------
screw_dia         = 3.0;    // M3
screw_clearance   = 0.6;    // -> 3.6 shaft clearance
insert_hole_dia   = 4.0;    // M3 brass heat-set insert seat (melt-in) -- METAL, not printed
insert_depth      = 6.0;
nut_af            = 5.5;    // M3 hex nut across-flats
nut_thick         = 2.4;
nut_pocket_extra  = 0.3;    // clearance on the hex pocket
csk_dia = 6.2; csk_depth = 2.0;   // countersink for the M3 screw head in the bezel

// ---- Bezel / screw ears ----------------------------------------------------
screw_boss_gap = 5.0;       // radial gap: glass edge -> screw axis (screws outside Ø115)
screw_boss_od  = 9.0;
bezel_th       = 4.0;
bezel_open_dia = display_active_dia;    // 87.6 open -- never covers the active area
bezel_relief   = 0.6;       // underside relief: bezel presses only the OUTER glass rim
ear_depth      = 12.0;      // screw-boss column depth (front)
ledge_overlap  = 2.5;       // how far the block-seat ledge reaches in over the glass rim
ledge_th       = 3.0;

// ---- Board standoffs (PRIMARY board fixation, at the 74.5 square) -----------
standoff_len       = 14.0;  // post depth behind the PCB (insert + solid base)
gasket_under_pcb   = 0.8;   // foam/silicone pad between PCB rear and standoff tops (0.5-1)
gusset_span        = 15;    // deg each side: fan gusset arc from post to wall
gusset_h           = 10.0;  // gusset taper height (full fan at the base -> post at the top)

// ---- Rear cover fixation ---------------------------------------------------
rear_boss_len = 10.0;       // rear screw-boss depth (rear_cover -> rear_case)
rc_lip        = 4.0;        // rear_cover locating lip into the bore

// ---- Cable exit -- ONE bottom hole (TEMP test enclosure) -------------------
// This is a bench/test build: all wiring (power, J1850 tap, USB if used) exits
// through a single grommet hole at the BOTTOM of the wall. microSD access on this
// temp build is by removing the rear cover (a dedicated slot comes on the final one).
cable_hole_dia   = 12;      // size for the wire bundle + grommet
cable_exit_angle = 270;     // deg CCW from +X; 270 = bottom (wires drop toward the bike)
cable_exit_z     = 35;      // z-centre in the cavity (behind the standoffs, ahead of rear bosses)
// Per-connector cutouts dropped for the temp build -- kept here for the FINAL enclosure:
// usb_c_angle = 0;   usb_c_w = 12;  usb_c_h = 7;
// microsd_angle = 40; microsd_w = 14; microsd_h = 5;   // MUST stay reachable (ride-log card)
// power_boot_angle = 90; power_boot_w = 16; power_boot_h = 6;
// harness_angle = 200; harness_w = 34; harness_h = 12;  // 40-pin harness exit

// ---- Handlebar mount = OUT of scope (metal load path, separate task) --------
// A future aluminium strap to the triple-tree riser bolts (M10x1.5, ~89 mm
// spacing) would attach near the rear-case wall here. Metal, not printed.
// mount_strap_angle = 270; mount_strap_boss_w = 24; mount_strap_boss_h = 16;

// ============================================================================
// Derived
// ============================================================================
glass_bore_dia = glass_od + glass_fit;              // 115.5
wall_inner_r   = glass_bore_dia/2;                  // 57.75
main_outer_dia = glass_bore_dia + 2*wall;
R_screw        = glass_od/2 + screw_boss_gap;       // screw axis radius (outside Ø115)
ear_outer_r    = R_screw + screw_boss_od/2;
overall_dia    = 2*ear_outer_r;                     // OD at the screw ears
screw_clear_dia = screw_dia + screw_clearance;      // 3.6
ledge_in_r     = glass_od/2 - ledge_overlap;        // block-seat inner radius
ledge_id       = 2*ledge_in_r;

// z-levels (z=0 = front rim = glass front, +z rearward)
glass_front_z = 0;
glass_rear_z  = glass_thickness;                    // 6
pcb_front_z   = glass_rear_z + block_gap;           // 6 (touching)
pcb_rear_z    = pcb_front_z + pcb_thickness;        // 7.6
ledge_front_z = glass_rear_z + gasket_gap;          // gasket sits glass-rim-rear .. ledge
ledge_rear_z  = ledge_front_z + ledge_th;
standoff_front_z = pcb_rear_z + gasket_under_pcb;   // post top sits a gasket behind the PCB rear
standoff_rear_z  = standoff_front_z + standoff_len;
rear_plane_z  = ledge_rear_z + internal_depth;      // rear opening
calib_depth   = max(standoff_rear_z, ear_depth) + 3;

// ============================================================================
// Helpers
// ============================================================================
module tube(od, id, h) { linear_extrude(h) difference(){ circle(d=od); circle(d=id); } }
module hexprism(af, h) { cylinder(d=af/cos(30), h=h, $fn=6); }   // af = across-flats

screw_offset = 0;   // deg; 0 -> a clean gap at the bottom (270) for the cable hole (6 screws)
function screw_pts(n) = [ for (i=[0:n-1]) let(a=360/n*i + screw_offset)
                          [R_screw*cos(a), R_screw*sin(a)] ];
function mount_pts() = [ for (sx=[-1,1], sy=[-1,1]) [sx*mount_square/2, sy*mount_square/2] ];

// Shared gusset: a fan web from a boss to a ~2*gusset_span arc of its structural
// body (wall/ring/floor) at radius anchor_r, from z0 over height h. taper=true ->
// tapers to the boss at the top (for z-posts); taper=false -> full-height web
// (for flat tabs). Every screw boss uses this so none is a freestanding stalk.
module boss_gusset(cx, cy, boss_dia, anchor_r, z0, h, taper=true) {
    ang = atan2(cy, cx);
    a1  = [anchor_r*cos(ang-gusset_span), anchor_r*sin(ang-gusset_span)];
    a2  = [anchor_r*cos(ang+gusset_span), anchor_r*sin(ang+gusset_span)];
    hull() {
        translate([cx,cy,z0])   cylinder(d=boss_dia, h=0.1);
        translate([a1[0],a1[1],z0]) cylinder(d=2, h=0.1);
        translate([a2[0],a2[1],z0]) cylinder(d=2, h=0.1);
        translate([cx,cy,z0+h]) cylinder(d=boss_dia, h=0.1);
        if (!taper) {
            translate([a1[0],a1[1],z0+h]) cylinder(d=2, h=0.1);
            translate([a2[0],a2[1],z0+h]) cylinder(d=2, h=0.1);
        }
    }
}

// 4 chunky board standoffs at the 74.5 square, behind the PCB. Each post's TOP
// (front) face meets the PCB rear (via the under-PCB gasket). Each is tied to the
// wall by a TAPERED FAN GUSSET -- a solid web fanning from the post base out to a
// ~30deg arc of the wall, tapering up to the post at the top, so the post can't
// crack off at the base. M3 heat-set insert opens toward the PCB; the rear bolt
// reaches it up a clearance bore, driven from the open back through the board hole.
module standoffs() {
    for (p=mount_pts()) {
        difference() {
            union() {
                translate([p[0],p[1],standoff_front_z]) cylinder(d=standoff_od, h=standoff_len);  // post
                boss_gusset(p[0],p[1], standoff_od, wall_inner_r-0.3, standoff_front_z, gusset_h);// fan to wall
            }
            translate([p[0],p[1],standoff_front_z-0.01]) cylinder(d=insert_hole_dia, h=insert_depth);   // insert (PCB side)
            translate([p[0],p[1],standoff_front_z+insert_depth-1]) cylinder(d=screw_clear_dia, h=standoff_len); // rear bolt
        }
    }
}

// Metal-fastener feature inside a front screw ear, bored along z from the front.
module ear_fastener() {
    if (fastener_r == "heatset") {
        translate([0,0,-0.01]) cylinder(d=insert_hole_dia, h=insert_depth);   // heat-set seat
    } else {                                                                   // captured_nut
        translate([0,0,-0.01]) cylinder(d=screw_clear_dia, h=ear_depth+0.02);  // screw clearance
        translate([0,0,ear_depth-nut_thick]) hexprism(nut_af+nut_pocket_extra, nut_thick+0.1);
    }
}

// Single bottom cable-exit hole through the wall (temp test build).
module cable_hole() {
    rotate([0,0,cable_exit_angle]) translate([main_outer_dia/2 - wall - 1, 0, cable_exit_z])
        rotate([0,90,0]) cylinder(d=cable_hole_dia, h=wall + 4, center=true);
}

// ============================================================================
// Parts
// ============================================================================

module rear_case() {
    difference() {
        union() {
            tube(main_outer_dia, glass_bore_dia, rear_plane_z);          // outer wall
            translate([0,0,ledge_front_z])                               // block-seat ledge
                tube(main_outer_dia, ledge_id, ledge_th);
            for (p=screw_pts(screw_count)) {                             // front screw ears + gussets
                translate([p[0],p[1],0]) cylinder(d=screw_boss_od, h=ear_depth);
                boss_gusset(p[0],p[1], screw_boss_od, main_outer_dia/2-0.3, 0, gusset_h);
            }
            for (p=screw_pts(screw_count)) {                             // rear cover bosses + gussets
                translate([p[0],p[1],rear_plane_z-rear_boss_len]) cylinder(d=screw_boss_od, h=rear_boss_len);
                boss_gusset(p[0],p[1], screw_boss_od, main_outer_dia/2-0.3, rear_plane_z-rear_boss_len, gusset_h);
            }
            standoffs();                                                 // 4 board standoffs (74.5 sq)
        }
        for (p=screw_pts(screw_count)) translate([p[0],p[1],0]) ear_fastener();      // front metal fastener
        for (p=screw_pts(screw_count))                                               // rear-cover inserts (from rear)
            translate([p[0],p[1],rear_plane_z-insert_depth]) cylinder(d=insert_hole_dia, h=insert_depth+0.1);
        cable_hole();                                                                // single bottom cable exit
    }
}

module bezel() {
    difference() {
        union() {
            translate([0,0,-bezel_th]) tube(main_outer_dia, bezel_open_dia, bezel_th);   // frame ring
            for (p=screw_pts(screw_count)) {                                             // screw ears + gussets
                translate([p[0],p[1],-bezel_th]) cylinder(d=screw_boss_od, h=bezel_th);
                boss_gusset(p[0],p[1], screw_boss_od, main_outer_dia/2-0.3, -bezel_th, bezel_th, taper=false);
            }
        }
        // underside relief: press ONLY the outer glass rim (over the ledge), spare the
        // active/module area. Contact stays at r >= ledge_in_r.
        translate([0,0,-bezel_relief]) tube(2*(ledge_in_r-1), bezel_open_dia, bezel_relief+0.02);
        // screw clearance + countersink through each ear
        for (p=screw_pts(screw_count)) translate([p[0],p[1],0]) {
            translate([0,0,-bezel_th-1]) cylinder(d=screw_clear_dia, h=bezel_th+2);
            translate([0,0,-bezel_th-0.01]) cylinder(d1=csk_dia, d2=screw_clear_dia, h=csk_depth);
        }
    }
}

module rear_cover() {
    difference() {
        union() {
            cylinder(d=main_outer_dia, h=floor_thickness);                       // back panel
            for (p=screw_pts(screw_count)) {                                     // screw ears + gussets
                translate([p[0],p[1],0]) cylinder(d=screw_boss_od, h=floor_thickness);
                boss_gusset(p[0],p[1], screw_boss_od, main_outer_dia/2-0.3, 0, floor_thickness, taper=false);
            }
            translate([0,0,-rc_lip]) tube(glass_bore_dia-0.6, glass_bore_dia-0.6-2*2, rc_lip); // locating lip
        }
        for (p=screw_pts(screw_count)) translate([p[0],p[1],-rc_lip-1]) {        // screw clearance + csk
            cylinder(d=screw_clear_dia, h=floor_thickness+rc_lip+2);
            translate([0,0,rc_lip+floor_thickness+1-csk_depth]) cylinder(d1=screw_clear_dia, d2=csk_dia, h=csk_depth+0.1);
        }
    }
}

// Cheap shallow print: front rim + block-seat ledge + screw ears + the 4 board
// standoffs. Verifies the block seats, the bezel screws line up, and -- most
// important now -- the board bolts onto the 74.5 standoffs from the rear.
module calibration_base() {
    difference() {
        union() {
            tube(main_outer_dia, glass_bore_dia, calib_depth);
            translate([0,0,ledge_front_z]) tube(main_outer_dia, ledge_id, ledge_th);
            for (p=screw_pts(screw_count)) {
                translate([p[0],p[1],0]) cylinder(d=screw_boss_od, h=ear_depth);
                boss_gusset(p[0],p[1], screw_boss_od, main_outer_dia/2-0.3, 0, gusset_h);
            }
            standoffs();
        }
        for (p=screw_pts(screw_count)) translate([p[0],p[1],0]) ear_fastener();
    }
}

module assembly() {
    rear_case();
    color("green")    translate([0,0,0]) bezel();
    color("dimgray")  translate([0,0,rear_plane_z]) rotate([180,0,0]) rear_cover();
    // bonded block: glass + rectangular PCB (reaches the 74.5 standoffs) + gasket
    %translate([0,0,glass_front_z]) cylinder(d=glass_od, h=glass_thickness);          // glass
    %color("navy") translate([0,0,pcb_front_z]) linear_extrude(pcb_thickness) square([pcb_w,pcb_h],center=true); // PCB
    %color("orange") translate([0,0,glass_rear_z]) tube(glass_od, ledge_id, gasket_gap);       // glass-rim gasket
    // under-PCB damper pads: foam/silicone between PCB rear and each standoff top
    %color("orange") for (p=mount_pts())
        translate([p[0],p[1],pcb_rear_z]) cylinder(d=standoff_od+2, h=gasket_under_pcb);
    // rear board bolts, driven from the open back into the standoff inserts
    color("silver") for (p=mount_pts())
        translate([p[0],p[1],standoff_front_z+insert_depth]) rotate([180,0,0]) cylinder(d=screw_dia, h=standoff_len);
}

// ============================================================================
// Dispatch
// ============================================================================
if      (part=="rear_case")        rear_case();
else if (part=="bezel")            bezel();
else if (part=="rear_cover")       rear_cover();
else if (part=="calibration_base") calibration_base();
else if (part=="assembly")         assembly();
else echo(str("Unknown part: ", part,
              " -- rear_case | bezel | rear_cover | calibration_base | assembly"));

echo(str("tier=", tier, " fastener=", fastener_r, " screws=", screw_count,
         " wall=", wall, " gasket_gap=", gasket_gap));
echo(str("overall_dia (at ears) = ", overall_dia, " mm | R_screw = ", R_screw,
         " | rear_plane_z = ", rear_plane_z));
echo(str("standoffs: dia=", standoff_od, " | post z=", standoff_front_z, "->", standoff_rear_z,
         " | gusset span=+/-", gusset_span, "deg h=", gusset_h));
echo(str("standoff centers (mm): ", mount_pts()));
echo(str("74.5 CHECK -- edge spacing X: ", mount_pts()[2][0]-mount_pts()[0][0],
         "  Y: ", mount_pts()[1][1]-mount_pts()[0][1], "  (both must = ", mount_square, ")"));
echo(str("cable hole: dia=", cable_hole_dia, " at ", cable_exit_angle, "deg z=", cable_exit_z,
         " | standoffs at 45/135/225/315 deg (z ", standoff_front_z, "-", standoff_rear_z,
         "), screws at ", [for(i=[0:screw_count-1]) 360/screw_count*i+screw_offset], " deg"));
echo(str("BOLT PATH -- PCB hole z ", pcb_front_z, "..", pcb_rear_z,
         " | under-PCB gasket ", pcb_rear_z, "..", standoff_front_z,
         " | insert ", standoff_front_z, "..", standoff_front_z+insert_depth,
         " | rear bolt clearance to z=", standoff_rear_z, " (open back)"));
