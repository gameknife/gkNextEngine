// Deterministic traversal fixture (nextdayz-3c-traversal.agentscript.json).
// The teleport cvar takes SCAD "x,y" and the engine maps it to z = -y;
// lookYaw 0 walks toward scad -y, so every lane starts at a y GREATER than
// its obstacle's near face. Lanes run along x; the near faces of the first
// row sit at y = -4, the second row at y = -11.
color([0.24, 0.30, 0.22])
    translate([-22, -24, -0.5]) cube([50, 34, 0.5]);

// --- row 1: near faces at y = -4 ------------------------------------------

// x = 0: thin 1.0 m wall with open ground behind it -> vault.
color([0.55, 0.45, 0.25])
    translate([-2, -4.25, 0]) cube([4, 0.25, 1.0]);

// x = 4.75: chest-high deep platform -> mantle.
color([0.35, 0.45, 0.55])
    translate([3.5, -6.5, 0]) cube([2.5, 2.5, 1.25]);

// x = 9.5: 2.2 m platform, above the head -> climb (jump, catch, haul up).
color([0.40, 0.42, 0.48])
    translate([8.0, -7.0, 0]) cube([3.0, 3.0, 2.2]);

// x = 16: 2.0 m concrete fence, 14 cm thick -> climb over and drop behind.
color([0.52, 0.52, 0.50])
    translate([14.0, -4.14, 0]) cube([4.0, 0.14, 2.0]);

// x = 21: 1.4 m garden wall, too high to vault -> hop, press and swing over.
color([0.58, 0.40, 0.30])
    translate([19.5, -4.2, 0]) cube([3.0, 0.2, 1.4]);

// x = -7: rural house, 2.5 m walls under a pitched roof with a 0.5 m eave
// (kit_coldwar cw_part_roof). The eave lip sits 0.5 m in front of the wall
// -> climb onto the roof from under the eave.
color([0.44, 0.34, 0.22])
    translate([-9.0, -9.0, 0]) cube([4.0, 5.0, 2.5]);
color([0.34, 0.25, 0.17])
    translate([-7.0, -6.5, 2.5])
        polyhedron(points = [[-2.5, -3.0, 0], [2.5, -3.0, 0], [2.5, 3.0, 0], [-2.5, 3.0, 0], [-2.0, 0, 1.6], [2.0, 0, 1.6]],
                   faces = [[0, 1, 2, 3], [4, 5, 1, 0], [5, 4, 3, 2], [5, 2, 1], [4, 0, 3]]);

// --- row 2: near faces at y = -11 -----------------------------------------

// x = -12: 3.6 m wall, out of reach even from a jump -> plain jump.
color([0.60, 0.38, 0.25])
    translate([-14.0, -11.4, 0]) cube([4.0, 0.4, 3.6]);

// x = 9.5: 1.9 m ledge started from too far out -> the jump catches it.
color([0.42, 0.46, 0.40])
    translate([8.0, -14.0, 0]) cube([3.0, 3.0, 1.9]);

// x = 15.5: 1.2 m platform under a 1.5 m crawl space -> mantle, end crouched.
color([0.46, 0.40, 0.46])
    translate([14.0, -14.0, 0]) cube([3.0, 3.0, 1.2]);
color([0.36, 0.32, 0.36])
    translate([14.0, -14.0, 2.7]) cube([3.0, 2.7, 0.2]);
