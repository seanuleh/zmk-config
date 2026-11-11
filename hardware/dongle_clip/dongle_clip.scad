// nRFMicro + 0.91" SSD1306 OLED dongle case that clips onto the monitor's
// bottom edge. The case hangs just below the bezel with the OLED facing you.
//
// Side view (you sit on the left):
//
//          glass |
//        tooth __|      <- hooks over the 2 mm lip that sticks out
//             |##| lip  ##################  monitor
//             |##############################|
//             |<---------- depth ----------->|| <- back jaw
//             |=============plate=============|
//      OLED ->|[o|  |nRF|parts]|lid
//      window |________________|
//
// Inside, front -> back: OLED (glass toward the window), wire gap,
// nRFMicro (parts facing the back lid, reset button reachable via the lid tab).
//
// Coordinates: X = along the monitor width, Y = front -> back, Z = up.
// Origin = front-bottom corner of the monitor lip (z = 0 is the underside).
//
// part = "assembly" | "clip" | "lid" | "test" | "section" | "assembled"
part = "assembly";

/* ---------- monitor (measured) ---------- */
depth        = 21.12; // underside, front of lip -> back face
lip_h        = 2.06;  // lip thickness (vertical)
lip_proud    = 2.0;   // how far the lip sticks out past the glass
grip_interf  = 0.25;  // jaw squeeze; raise if loose, lower if it won't snap on

/* ---------- nRFMicro (verify with calipers) ---------- */
board_l      = 33.0;
board_w      = 18.0;
board_t      = 1.0;
top_clear    = 6.8;   // tallest top-side part (reset button) + margin
button_h     = 6.0;   // reset button height above the PCB (measured)
button_from_tip = 11.0; // button centre distance from the USB-C tip (9 measured; print showed it 2 mm too close to the USB end)
usb_over     = 2.0;   // USB-C overhang past the PCB end (measured 1-2)
usb_h        = 3.2;   // USB-C receptacle height above the PCB

/* ---------- 0.91" 128x32 OLED module (verify) ---------- */
oled_l       = 38.0;  // PCB length (pins on one short end)
oled_w       = 12.0;  // PCB height
oled_t       = 3.0;   // glass + PCB thickness
glass_l      = 30.0;
glass_off    = 1.5;   // glass distance from the non-pin end of the PCB
win_l        = 24.0;  // window over the 22.4 x 5.6 active area
win_h        = 7.5;
win_dz       = 0.0;   // nudge the window up/down if the active area is off-centre
win_dx       = -2.0;  // nudge along X (- = left when facing the screen); print showed it 2 mm right
wire_gap     = 2.0;   // OLED back -> nRFMicro back (wires, solder joints)

usb_end_right = true; // USB faces +X; false mirrors the whole case

/* ---------- print ---------- */
wall     = 1.6;
front_t  = 1.2;   // front wall with the window
usb_wall = 1.2;   // thin end wall so the plug seats fully
plate    = 2.0;
fit      = 0.3;
lid_t    = 1.6;
lid_clear = 0.15;
skirt_h  = 2.0;
skirt_t  = 1.2;
snap     = 0.4;

tooth_d  = 1.4;   // must stay < lip_proud so it never covers the screen
tooth_t  = 1.2;
tooth_clr = 0.15;
back_h   = 5.0;
back_t   = 1.6;

$fn = 48;

/* ---------- derived ---------- */
cav_l = max(board_l, oled_l) + 2*fit;
cav_h = max(board_w, oled_w) + 2*fit;           // Z
cav_d = oled_t + wire_gap + board_t + top_clear;  // Y
L     = wall + cav_l + usb_wall;
y0    = -wall;                  // front face of the case / front jaw
yf    = y0 + front_t;           // inner face of the front wall
yback = yf + cav_d;             // open back of the cavity (lid goes here)
y1    = depth - grip_interf;    // inner face of the back jaw
yb    = y1 + back_t;
zt    = -plate;                 // cavity top
zb    = zt - cav_h;             // cavity bottom
zc    = (zt + zb) / 2;
cx0   = wall;
cx1   = wall + cav_l;           // USB-end inner wall
yn    = yf + oled_t + wire_gap; // nRFMicro back face
nx1   = cx1 - fit - usb_over;   // nRFMicro PCB USB end (USB tip sits at cx1 - fit)
nx0   = nx1 - board_l;          // nRFMicro antenna end
ox1   = cx0 + fit + oled_l;     // OLED non-pin end (pins at the antenna end)
win_cx = ox1 - glass_off - glass_l/2 + win_dx;

module xmirror() { if (usb_end_right) children(); else translate([L,0,0]) mirror([1,0,0]) children(); }

module profile() { // YZ profile of plate + jaws, extruded along X
  polygon([
    [y0, -plate], [yb, -plate], [yb, 0],
    [yb, back_h], [y1 + 0.8, back_h], [y1, back_h - 0.8], [y1, 0],
    [0, 0], [0, lip_h + tooth_clr],
    [tooth_d, lip_h + tooth_clr],
    [tooth_d, lip_h + tooth_clr + tooth_t*0.4],
    [tooth_d - 0.6, lip_h + tooth_clr + tooth_t],
    [y0, lip_h + tooth_clr + tooth_t],
  ]);
}

module clip() {
  xmirror() {
    difference() {
      union() {
        rotate([90,0,90]) linear_extrude(L) profile();
        translate([0, y0, zb - wall]) cube([L, yback - y0, cav_h + wall + 0.01]);
      }
      // cavity, open at the back
      translate([cx0, yf, zb]) cube([cav_l, cav_d + 1, cav_h]);
      // OLED window, chamfered outward for viewing angle
      translate([win_cx, y0 - 0.01, zc + win_dz]) rotate([-90,0,0])
        linear_extrude(front_t + 0.02, scale = [win_l/(win_l + 2*front_t), win_h/(win_h + 2*front_t)])
          square([win_l + 2*front_t, win_h + 2*front_t], center = true);
      // USB-C opening, sized for a typical plug overmold
      translate([cx1 - 1, yn + board_t + usb_h/2 - 3.75, zc - 6.25]) cube([usb_wall + 2, 7.5, 12.5]);
      // snap grooves (top + bottom inner walls) for the lid skirt
      // (centred on the lid nubs, which sit at yback-1.5 .. yback-0.7)
      for (z = [zb - snap - 0.1, zt - 0.01])
        translate([L/2 - 8, yback - 0.6 - 1.0, z]) cube([16, 1.0, snap + 0.11]);
    }
    // corner posts: hold the OLED against the window and space the nRFMicro off it
    for (x = [nx0, nx1 - 1.2], z = [zb, zt - 2])
      translate([x, yf + oled_t, z]) cube([1.2, wire_gap, 2]);
  }
}

module lid() { // modelled lying flat: outside face on z = 0, skirt + nubs up
  gap = top_clear - button_h;
  // lid-local x matches the case x; lid-local y = case z measured up from the box bottom
  zl = function(z) z - (zb - wall);
  bx = cx1 - fit - button_from_tip;
  by = zl(zc);
  hgt = cav_h + wall - lid_clear; // covers the box bottom up to the plate underside; the plate runs on
                                 // past the back of the box, so the lid tucks under it, not over it
  xmirror() {
    difference() {
      cube([L, hgt, lid_t]);
      // U-slot forming a flexible press tab over the reset button (root toward the antenna end)
      translate([bx, by, -1]) difference() {
        translate([-10, -4, 0]) cube([14, 8, lid_t + 2]);
        translate([-10.8, -3.2, -1]) cube([14, 6.4, lid_t + 4]);
      }
      translate([bx - 10, by - 3.2, lid_t - 0.6]) cube([13.2, 6.4, 1]);
    }
    translate([bx, by, lid_t - 0.6]) cylinder(d = 3, h = gap - 0.3 + 0.6);
    // rib that presses on the USB-C shell, keeping the board seated on the posts
    translate([cx1 - fit - 4.5, by - 3, lid_t]) cube([3, 6, top_clear - usb_h - 0.2]); // clear of the tab slot
    // skirt (open on the USB end so the plug isn't blocked)
    translate([cx0 + lid_clear, zl(zb) + lid_clear, lid_t]) difference() {
      cube([cav_l - 2*lid_clear, cav_h - 2*lid_clear, skirt_h]);
      translate([skirt_t, skirt_t, -1]) cube([cav_l, cav_h - 2*lid_clear - 2*skirt_t, skirt_h + 2]);
    }
    for (s = [0, 1])
      translate([L/2 - 7, s ? zl(zt) - lid_clear : zl(zb) + lid_clear - snap, lid_t + 0.7])
        cube([14, snap, 0.8]);
  }
}

module lid_placed() { // lid in its closed position on the back of the case
  translate([0, yback + lid_t, zb - wall]) rotate([90, 0, 0]) lid();
}

module test() { linear_extrude(10) profile(); } // prints flat, profile on the bed

module monitor() {
  color("dimgray", 0.5) translate([-10, 0, 0]) {
    cube([L + 20, depth, 30]);
    translate([0, lip_proud, lip_h]) cube([L + 20, 1, 30]);
  }
}

module boards() { // ghost boards for checking fit
  xmirror() {
    color("green") translate([nx0, yn, zc - board_w/2]) cube([board_l, board_t, board_w]);
    color("silver") translate([nx1 + usb_over - 7.5, yn + board_t, zc - 4.5]) cube([7.5, usb_h, 9]);
    color("black") translate([cx0 + fit, yf, zc - oled_w/2]) cube([oled_l, oled_t, oled_w]);
  }
}

if (part == "clip") translate([0, 0, L]) rotate([0, 90, 0]) clip(); // stands on its USB end (flat face on the bed), no supports
else if (part == "plate") { translate([0, 0, L]) rotate([0, 90, 0]) clip(); translate([-zb + wall + 8, y0, 0]) lid(); } // both parts, print layout
else if (part == "lid") lid();
else if (part == "test") test();
else if (part == "assembled") { clip(); lid_placed(); }
else if (part == "section")
  intersection() {
    translate([(usb_end_right ? cx1 - fit - button_from_tip : L - cx1 + fit + button_from_tip) - 0.25, -50, -50]) cube([0.5, 100, 100]);
    union() { clip(); lid_placed(); boards(); }
  }
else { monitor(); color("steelblue") clip(); color("lightsteelblue") lid_placed(); boards(); }
