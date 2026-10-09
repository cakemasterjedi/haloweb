// =====================================================================================
//  No-drill trunk-lid tray for the emblem display electronics
//  Holds: YRDZXG 12/24V->5V 5A 25W buck converter (body about 46 x 32 x 18 mm, 61 mm over the
//  mounting ears) + INA219 (GY-219, 25 x 22 mm)
//  Mounts with 3M VHB on the flat back, and/or zip ties around the existing trunk-lid wiring harness.
//  Units: mm. Print flat, back down, no supports.
// =====================================================================================

/* [Tray] */
plate_l = 118;
plate_w = 46;
plate_t = 3;
corner_r = 5;

/* [Buck converter] */
// Footprint of the converter including its mounting ears (length x width)
conv_l = 61;
conv_w = 32;
// Clearance around the converter footprint
conv_clear = 0.8;
// Depth of the locating pocket for the converter's base and ears
conv_pocket = 1.2;
// Centre of the converter along the tray
conv_x = -20;
// Distance between the two zip-tie straps over the converter
strap_spacing = 24;

/* [INA219 board] */
ina_l = 25;
ina_w = 22;
ina_clear = 0.6;
// Height of the low wall around the INA219
ina_wall_h = 1.6;
ina_wall_t = 1.6;
// Width of the wire gaps in the short walls
ina_gap = 10;
ina_x = 32;

/* [Zip ties] */
// Zip-tie width + clearance
tie_w = 5.0;
// Zip-tie thickness + clearance
tie_t = 2.4;
// Distance from tray end to the end slots (for strapping to a harness)
end_slot_inset = 4;
// Spacing of the two end slots across the tray
end_slot_spacing = 18;

/* [Hidden] */
$fn = 64;
CPL = conv_l + 2 * conv_clear;
CPW = conv_w + 2 * conv_clear;
IPL = ina_l + 2 * ina_clear;
IPW = ina_w + 2 * ina_clear;

module rounded_rect(l, w, r, h) {
    linear_extrude(height = h)
        offset(r = r) square([l - 2 * r, w - 2 * r], center = true);
}

module slot_x(cx, cy) { translate([cx - tie_w / 2, cy - tie_t / 2, -1]) cube([tie_w, tie_t, plate_t + 2]); }  // tie runs across Y
module slot_y(cx, cy) { translate([cx - tie_t / 2, cy - tie_w / 2, -1]) cube([tie_t, tie_w, plate_t + 2]); }  // tie runs across X

module tray() {
    difference() {
        union() {
            rounded_rect(plate_l, plate_w, corner_r, plate_t);
            // low wall around the INA219 with wire gaps in the short sides
            translate([ina_x, 0, plate_t]) difference() {
                translate([-(IPL / 2 + ina_wall_t), -(IPW / 2 + ina_wall_t), 0])
                    cube([IPL + 2 * ina_wall_t, IPW + 2 * ina_wall_t, ina_wall_h]);
                translate([-IPL / 2, -IPW / 2, -1]) cube([IPL, IPW, ina_wall_h + 2]);
                translate([-(IPL / 2 + ina_wall_t + 1), -ina_gap / 2, -1]) cube([IPL + 2 * ina_wall_t + 2, ina_gap, ina_wall_h + 2]);
            }
        }
        // locating pocket for the converter's base and ears
        translate([conv_x - CPL / 2, -CPW / 2, plate_t - conv_pocket]) cube([CPL, CPW, conv_pocket + 1]);
        // two straps over the converter body
        for (dx = [-strap_spacing / 2, strap_spacing / 2]) for (sy = [-1, 1])
            slot_x(conv_x + dx, sy * (CPW / 2 + tie_t / 2 + 1.5));
        // end slots: zip-tie the tray to a harness or bracket behind it
        for (sx = [-1, 1]) for (sy = [-1, 1])
            slot_y(sx * (plate_l / 2 - end_slot_inset - tie_t / 2), sy * end_slot_spacing / 2);
    }
}

tray();
