// Quiet range for comparing the sight line with an actual target at eye height.
$fn = 64;
color([0.24, 0.30, 0.22])
    translate([-50, -220, -0.5]) cube([100, 270, 0.5]);
color([0.75, 0.74, 0.68])
    translate([-2, -25.1, 0]) cube([4, 0.2, 3.3]);
color([0.08, 0.09, 0.10])
    translate([0, -24.87, 1.65]) rotate([90, 0, 0]) cylinder(h = 0.02, r = 0.48);
color([0.8, 0.3, 0.08])
    translate([0, -24.85, 1.65]) rotate([90, 0, 0]) cylinder(h = 0.01, r = 0.10);
