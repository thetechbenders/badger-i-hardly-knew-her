// Parametric front window + pocket for a Qwiic APDS-9960 breakout.
//
// UNTESTED DESIGN. Measure your breakout and enclosure wall, then set the
// MEASURE parameters below. Print in an opaque, matte, dark material:
// light-coloured or translucent plastic leaks the sensor's own IR into its
// photodiodes (crosstalk) and ruins gesture detection.
//
// Optical rules this model enforces:
//  * The APDS-9960 package (IR LED + photodiode array, 3.94 x 2.36 mm per
//    the datasheet) sits directly behind an OPEN aperture: no cover, no
//    window, no tape. (An IR-transparent window is possible but needs its
//    own crosstalk testing and is not modelled here.)
//  * The aperture widens outward (chamfer) so the sensor's field of view is
//    not clipped by the wall thickness.
//  * The package face sits as close to the outer surface as the wall allows
//    (`face_gap`); a deep tunnel reflects IR back into the sensor.

// ---- MEASURE (mm) ----
board_w = 20.0;      // breakout PCB width  (MEASURE)
board_h = 20.0;      // breakout PCB height (MEASURE)
board_t = 1.6;       // PCB thickness
sensor_dx = 0.0;     // sensor centre offset from PCB centre, x (MEASURE)
sensor_dy = 0.0;     // sensor centre offset from PCB centre, y (MEASURE)
wall = 1.6;          // enclosure wall thickness at the window
face_gap = 0.2;      // air gap between package top and wall inner face

// ---- Optical aperture ----
pkg_w = 3.94;        // APDS-9960 package (datasheet)
pkg_h = 2.36;
margin = 0.6;        // clearance around the package at the inner face
flare = 50;          // total outward chamfer angle in degrees (field-of-view relief)

// ---- Mount ----
clearance = 0.3;
pocket_depth = board_t + 1.0;
plate_margin = 3.0;

$fn = 48;

module aperture() {
  iw = pkg_w + 2 * margin;
  ih = pkg_h + 2 * margin;
  grow = 2 * wall * tan(flare / 2);
  // Truncated pyramid through the wall, small side inside.
  translate([sensor_dx, sensor_dy, -0.01])
    hull() {
      translate([-iw / 2, -ih / 2, 0]) cube([iw, ih, 0.01]);
      translate([-(iw + grow) / 2, -(ih + grow) / 2, wall + 0.01]) cube([iw + grow, ih + grow, 0.01]);
    }
}

module window_plate() {
  pw = board_w + 2 * plate_margin;
  ph = board_h + 2 * plate_margin;
  difference() {
    union() {
      // Wall section (outer surface at z = wall).
      translate([-pw / 2, -ph / 2, 0]) cube([pw, ph, wall]);
      // Pocket walls behind the wall to locate the PCB.
      translate([-pw / 2, -ph / 2, -pocket_depth - face_gap]) difference() {
        cube([pw, ph, pocket_depth + face_gap]);
        translate([plate_margin - clearance, plate_margin - clearance, -0.01])
          cube([board_w + 2 * clearance, board_h + 2 * clearance, pocket_depth + face_gap + 0.02]);
      }
    }
    aperture();
  }
}

window_plate();
