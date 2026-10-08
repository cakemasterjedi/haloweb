// =====================================================================================
//  Round trunk-emblem display housing  —  BMW G30 5 Series (fits the 74 mm trunk roundel spot)
//  For: Waveshare ESP32-S3-Touch-LCD-2.8C (73.0 mm round glass, 70.13 mm image, 480x480)
//  Lens: 3" (76.2 mm) x 1/16" (1.5 mm) clear acrylic disc, glued flush into the rim
//
//  Three printed parts:
//    HOUSING    - perfect circle; display drops in from the front, lens covers the whole glass
//    TAB CAP    - curved rim segment that hides the display's cable tab at 6 o'clock
//    BACK PLATE - 0.6 mm plate that closes in the back and sits on the paint under the board.
//                 Three lugs reach out under the rim through gaps cut in the VHB ring and are glued
//                 to the rim. Two hollow 4 mm pegs go into the factory emblem grommets and carry the wires.
//
//  Default = SLIM version: rim on a 0.64 mm VHB ring (3M 5925), board on the 0.6 mm back plate.
//  10.8 mm off the paint including the tape. No drilling.
//  Set open_back = false for the old solid-back version (thicker).
//
//  Works with the OpenSCAD Customizer, MakerWorld Parametric Model Maker and Thingiverse Customizer.
//  Pick a PART, adjust measurements, render (F6), export STL/3MF.
//  Units: mm. z = 0 is the face that sits on the paint.
// =====================================================================================

/* [Output] */
// What to generate
PART = "both"; // [both:All parts (print plate), housing:Housing only, tab_cap:Tab cap only, back_plate:Back plate only, assembly:Assembled preview]
// Smoothness (higher = smoother, slower)
resolution = 160; // [64:16:360]

/* [Display panel] */
// Diameter of the round display glass
glass_d = 73.0;
// Clearance around the glass (radial)
glass_clear = 0.3;
// Width of the flat cable tab at the bottom of the glass
tab_w = 33.5;
// How far the rigid tab reaches below the round glass edge
tab_reach = 3.65;
// Clearance around the tab
tab_clear = 0.2;
// Front of glass to the deepest part on the back of the board (studs / ports)
module_depth = 8.23;
// Extra depth on top of module_depth
depth_margin = 0.15;
// What the board rests on: the back plate thickness when open_back (or foam tape on the back wall)
foam_gap = 0.6;

/* [Lens] */
// Lens diameter (3 in = 76.2)
lens_d = 76.2;
// Lens thickness
lens_t = 1.55;
// Clearance around the lens (radial) for sealant + heat expansion
lens_clear = 0.3;
// Adhesive layer under the lens
lens_bond = 0.25;

/* [Housing] */
// Slim open back (board rests on felt pads on the paint, needs a 16 mm hole in the lid)
open_back = true;
// Back wall thickness (only used when open_back = false)
back_t = 1.6;
// Thickness of the VHB tape under the rim (open back: 3M 5925 = 0.64 mm, the rim sits this far off the paint)
vhb_t = 0.64;
// Minimum wall left outside the cable-tab corners (sets the outside diameter)
corner_wall = 0.8;
// Radius of the rounded outer front edge
edge_r = 2.0;
// How much the rim face drops from lens edge to outer edge (convex crown)
dome = 0.5;
// Small 45-degree chamfer at the lens edge
inner_chamfer = 0.4;
// Small chamfer where the housing meets the paint (kept small so the first layer has full bed contact, no brim)
base_chamfer = 0.4;

/* [Tab cap] */
// Extra coverage each side of the tab
cap_margin = 2.0;
// Gap between cap and housing at the seams
cap_clear = 0.3;
// Pocket under the cap for the ribbon wrapped around the cable tab
cap_recess = 1.0;

/* [Trunk mounting] */
// Centre-to-centre spacing of the factory emblem pegs (56 mm on G30)
peg_spacing = 56.0;
// Solid-back version only: holes for 4 mm OD brass tubes that act as the pegs and carry the wires
cable_hole_d = 4.2;

/* [Back plate (open back)] */
// Plate style: hollow pegs into the factory grommets (no drilling), or a center hole for a drilled 16 mm hole
plate_style = "pegs"; // [pegs:Pegs in factory holes, center_hole:Center hole (drilled lid)]
// Lug width (3 lugs reach out under the rim through gaps in the tape)
lug_w = 6.0;
// How far the lugs reach (radius)
lug_r = 43.4;
// Lug positions (degrees, 0 = 3 o'clock)
lug_angles = [0, 90, 180];
// Peg outside diameter (factory pegs are 4 mm)
peg_od = 4.0;
// Wire hole through each peg
peg_bore = 2.7;
// Peg length below the plate (factory pegs are 10 mm)
peg_len = 10.0;
// Center hole for the drilled option
center_hole_d = 12.0;

/* [Hidden] */
$fn = resolution;
R_CAV  = glass_d / 2 + glass_clear;
R_LP   = lens_d / 2 + lens_clear;
HW     = tab_w / 2 + tab_clear;
BOT    = glass_d / 2 + tab_reach + tab_clear;
R_OUT  = max(R_LP + 2, sqrt(HW * HW + BOT * BOT) + corner_wall);
BT     = open_back ? 0 : back_t;
// open back: housing z=0 is the bottom of the rim (on the tape); the board's pads sit vhb_t lower, on the paint
H      = BT + (open_back ? foam_gap - vhb_t : foam_gap) + module_depth + depth_margin;  // display glass face
TOP    = H + lens_bond + lens_t;                           // rim face = lens front
THETA  = asin(min(1, (HW + cap_margin) / R_LP));           // half-angle of tab-cap sector (deg)
THETA_CAP = THETA - (cap_clear / R_LP) * 180 / PI;
N_DOME = 24;

echo(str("Outside diameter = ", 2 * R_OUT, " mm, housing height = ", TOP, " mm, off the paint incl. tape = ",
          TOP + (open_back ? vhb_t : 0), open_back ? "" : " + your tape", " mm, rim width = ", R_OUT - R_LP, " mm"));

function dome_z(r) = TOP - dome * pow((r - R_LP) / (R_OUT - R_LP), 2);

// Revolved profile of the full housing (before the tab pocket, holes and cap split)
function profile() =
    let(cx = R_OUT - edge_r, ze = dome_z(cx), cz = ze - edge_r, r_in = R_LP + inner_chamfer)
    concat(
        [[open_back ? R_CAV : 0, 0], [R_OUT - base_chamfer, 0], [R_OUT, base_chamfer], [R_OUT, cz]],
        [for (a = [10 : 10 : 90]) [cx + edge_r * cos(a), cz + edge_r * sin(a)]],
        [for (i = [1 : N_DOME]) let(r = cx - i * (cx - r_in) / N_DOME) [r, dome_z(r)]],
        [[R_LP, dome_z(r_in) - inner_chamfer], [R_LP, H], [R_CAV, H]],
        open_back ? [] : [[R_CAV, BT], [0, BT]]          // open back closes straight down to [R_CAV, 0]
    );

module body() { rotate_extrude() polygon(profile()); }

// Wedge around 6 o'clock with half-angle `ang`, from z0 to z1
module wedge(ang, z0, z1) {
    L = 3 * R_OUT;
    translate([0, 0, z0]) linear_extrude(height = z1 - z0)
        polygon([[0, 0], [L * sin(ang), -L * cos(ang)], [0, -L], [-L * sin(ang), -L * cos(ang)]]);
}

module housing() {
    difference() {
        body();
        // cable-tab pocket (open to the front)
        translate([-HW, -BOT, open_back ? -1 : BT]) cube([2 * HW, BOT - 20, TOP + 2]);
        // rim removed over the tab; the tab cap fills it
        wedge(THETA, H, TOP + 1);
        // solid-back version only: both pegs are 4 mm cable tubes (right: 5 V + GND, left: 3V3 + SDA + SCL)
        if (!open_back)
            for (s = [-1, 1]) translate([s * peg_spacing / 2, 0, -1]) cylinder(d = cable_hole_d, h = BT + 2);
    }
}

// Back plate, modelled in print orientation: board side at z = 0, pegs pointing up (+z)
module plate_outline() {
    circle(r = R_CAV + 0.3);
    translate([-(HW + 0.3), -(BOT + 0.3)]) square([2 * (HW + 0.3), BOT + 0.3]);
    for (a = lug_angles) rotate(a) {
        translate([0, -lug_w / 2]) square([lug_r - lug_w / 2, lug_w]);
        translate([lug_r - lug_w / 2, 0]) circle(d = lug_w);
    }
}
module back_plate() {
    L = peg_spacing / 2;
    difference() {
        union() {
            linear_extrude(height = foam_gap) plate_outline();
            if (plate_style == "pegs") for (s = [-1, 1]) translate([s * L, 0, foam_gap * 0.5]) {
                cylinder(d = peg_od, h = foam_gap * 0.5 + peg_len - 0.5);
                translate([0, 0, foam_gap * 0.5 + peg_len - 0.5]) cylinder(d1 = peg_od, d2 = peg_od - 1, h = 0.5);  // lead-in
            }
        }
        if (plate_style == "pegs") for (s = [-1, 1]) translate([s * L, 0, -1]) cylinder(d = peg_bore, h = foam_gap + peg_len + 2);
        else translate([0, 0, -1]) cylinder(d = center_hole_d, h = foam_gap + 2);
    }
}

module tab_cap() {
    difference() {
        intersection() {
            body();
            wedge(THETA_CAP, H, TOP + 1);
        }
        // ribbon clearance pocket on the underside
        translate([-HW - 0.3, -BOT - 0.5, H - 1]) cube([2 * HW + 0.6, BOT + 0.5 - (R_LP - 1), 1 + cap_recess]);
    }
}

if (PART == "housing") {
    housing();
} else if (PART == "tab_cap") {
    translate([0, 0, -H]) tab_cap();                     // print face-up
} else if (PART == "assembly") {
    color("DimGray") housing();
    color("Peru") tab_cap();
    %translate([0, 0, H + lens_bond]) cylinder(d = lens_d, h = lens_t);              // lens (ghost)
    %translate([0, 0, H - module_depth]) cylinder(d = glass_d, h = module_depth);   // display (ghost)
    if (open_back) color("DimGray") translate([0, 0, -vhb_t + foam_gap]) mirror([0, 0, 1]) back_plate();
    if (open_back) %translate([0, 0, -vhb_t]) difference() {                         // VHB ring (ghost)
        cylinder(r = R_OUT - base_chamfer, h = vhb_t); translate([0, 0, -1]) cylinder(r = R_CAV + 0.5, h = vhb_t + 2); }
} else if (PART == "back_plate") {
    back_plate();
} else {                                                  // "both": print plate
    housing();
    translate([0, -R_OUT - 8, -H]) tab_cap();
    if (open_back) translate([2 * R_OUT + 6, 0, 0]) back_plate();
}
