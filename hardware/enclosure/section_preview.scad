// Cross-section inspector. Half-cut (remove y<0), rotated -45deg so the cut
// passes through two opposite board standoffs (the 74.5 square corners).
include <enclosure.scad>
$fn = 96;

module half() { translate([-200,-200,-80]) cube([400,200,600]); }
module cut(col) { color(col) difference() { rotate([0,0,-45]) children(); half(); } }

cut("gold")        rear_case();
cut("limegreen")   bezel();
cut("dimgray")     translate([0,0,rear_plane_z]) rotate([180,0,0]) rear_cover();
cut("red")         translate([0,0,glass_front_z]) cylinder(d=glass_od, h=glass_thickness);       // glass
cut("deepskyblue") translate([0,0,pcb_front_z]) linear_extrude(pcb_thickness) square([pcb_w,pcb_h],center=true); // PCB
cut("orange")      translate([0,0,glass_rear_z]) tube(glass_od, ledge_id, gasket_gap);           // glass-rim gasket
cut("orangered") for (p=mount_pts())                                                             // under-PCB damper pads
    translate([p[0],p[1],pcb_rear_z]) cylinder(d=standoff_od+2, h=gasket_under_pcb);
// rear board bolts (silver) driven from the back into the standoff inserts
cut("silver") for (p=mount_pts())
    translate([p[0],p[1],standoff_front_z+insert_depth]) rotate([180,0,0]) cylinder(d=screw_dia, h=standoff_len);

module lbl(x, z, s)
    color("black") translate([x, -0.7, z]) rotate([90,0,0]) linear_extrude(0.6)
        text(s, size=2.3, halign="center", valign="center");

lbl( 35, -2.0, "BEZEL");
lbl( 18,  3.0, "GLASS");
lbl( 18,  6.8, "PCB");
lbl( 44,  8.0, "PCB GASKET");
lbl( 40, 14.0, "GUSSET");
lbl( 53, 20.0, "REAR BOLT");
