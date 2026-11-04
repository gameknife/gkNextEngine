// Deterministic spring-arm fixture. SCAD [x,y,z] maps to engine [x,z,-y].
// Includes the default NextDayz spawn (-30, 177) and four isolated probe lanes.
color([0.24, 0.30, 0.22])
    translate([-50, -200, -0.5]) cube([100, 250, 0.5]);

// A wall closer than the minimum user zoom (1.5 m).
color([0.60, 0.38, 0.25])
    translate([-4, 1, 0]) cube([8, 0.3, 4]);

// Side wall: only the shoulder offset takes the camera through this wall.
color([0.40, 0.48, 0.60])
    translate([9.35, -1, 0]) cube([0.2, 6, 4]);

// The centre ray misses this thin post; the near-plane edge must catch it.
color([0.75, 0.64, 0.24])
    translate([19.78, 1.6, 0]) cube([0.12, 0.6, 4]);

// Just beyond the desired lens position: the clearance still needs to pull in.
color([0.50, 0.35, 0.55])
    translate([26, 3.92, 0]) cube([8, 0.2, 4]);
