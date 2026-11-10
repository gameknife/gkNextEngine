// NextDayz forward-operations cabin. Metres, Z-up, front toward -Y.
// A deliberately small set: no terrain, geography, foliage or world simulation.
// The live survivor is inserted by PlayerRigVisual, seated at [0, .25, 0] on the
// middle jump seat and facing -Y.
use <../lib/gk_camera.scad>
use <../lib/rig_forms.scad>
$fn = 10;

// The lobby shot (NextDayz binds the camera named "Cabin"): seated eye height on
// the survivor's left, looking across the cabin so he sits three-quarter in the
// right of the frame while the closed port door and its window fill the left,
// behind the menu. The set is dressed for this angle; the starboard door behind
// the camera stays open and lets the sky in as fill.
gk_camera_lookat([1.20, -1.50, 1.00], [-0.42, -0.28, 0.84], "Cabin", 40);

function cab_skin() = [0.150, 0.180, 0.175];     // painted airframe
function cab_frame() = [0.235, 0.265, 0.255];    // ribs and stringers
function cab_pad() = [0.170, 0.205, 0.185];      // quilted soundproofing
function cab_canvas() = [0.215, 0.255, 0.205];
function cab_dark() = [0.050, 0.060, 0.060];
function cab_stencil() = [0.630, 0.480, 0.190];

module cab_paint(c = cab_skin(), r = 0.72, m = 0.35) { gk_material(c, roughness = r, metalness = m) children(); }

module cabin_shell(collision = "none")
{
    gk_flatten()
    {
        // Deck, rear bulkhead and overhead.
        cab_paint([0.115, 0.14, 0.14], 0.82)
        {
            translate([0, -0.8, -0.065]) cube([3.3, 4.6, 0.13], center = true);
            translate([0, 0.86, 1.14]) cube([3.3, 0.12, 2.35], center = true);
            translate([0, -0.45, 2.29]) cube([3.3, 3.4, 0.10], center = true);
        }
        // Quilted panels on the bulkhead, one behind each seat.
        color(cab_pad())
            for (x = [-1.1, -0.55, 0, 0.55, 1.1], z = [0.72, 1.28, 1.84])
                translate([x, 0.786, z]) rf_box([0.50, 0.045, 0.52], 0.016);
        cab_paint(cab_frame(), 0.7, 0.55)
        {
            // Deck tie-down rails and overhead frames.
            for (x = [-1.3, -0.65, 0, 0.65, 1.3])
                translate([x, -0.85, 0.008]) cube([0.023, 4.2, 0.014], center = true);
            for (y = [-2.4, -1.65, -0.9, -0.15, 0.6])
                translate([0, y, 2.20]) cube([3.15, 0.045, 0.07], center = true);
            // Starboard side: open door, only the frame posts and the sill.
            translate([1.56, -0.64, 0.30]) cube([0.12, 3.9, 0.60], center = true);
            for (y = [-2.38, 0.68])
                translate([1.56, y, 1.3]) cube([0.10, 0.12, 1.9], center = true);
            translate([1.56, -0.85, 2.16]) rotate([90, 0, 0]) cylinder(h = 3.8, r = 0.035, center = true);
        }
        // Chamfered shoulder ribs read as an aircraft pressure shell.
        cab_paint(cab_frame(), 0.75, 0.4)
            for (s = [-1, 1], y = [-2.1, -1.1, -0.1, 0.69])
                translate([s * 1.46, y, 2.05]) rotate([0, s * 35, 0]) cube([0.065, 0.075, 0.49], center = true);
        // Stencilled floor bands and tie-down rings.
        color(cab_stencil())
            for (x = [-1.34, 1.34]) translate([x, -0.8, 0.013]) cube([0.075, 3.9, 0.012], center = true);
        color(cab_dark())
            for (x = [-1.1, 1.1], y = [-2.0, -1.1, -0.2, 0.5])
                translate([x, y, 0.018]) cylinder(h = 0.012, r = 0.04);
    }
}

// Port side, the backdrop of the shot: fixed wall aft, the closed sliding door
// with its window (the cabin fill lamp sits just outside it), wall forward.
module cabin_port_wall(collision = "none")
{
    gk_flatten()
    {
        // Skin, in pieces around the window opening (y -1.06..-0.38, z 1.04..1.66).
        cab_paint()
        {
            translate([-1.60, 0.42, 1.14]) cube([0.06, 0.88, 2.30], center = true);     // aft wall
            translate([-1.60, -2.05, 1.14]) cube([0.06, 1.34, 2.30], center = true);    // forward wall
            translate([-1.58, -0.70, 0.52]) cube([0.05, 1.36, 1.04], center = true);    // door, below glass
            translate([-1.58, -0.70, 1.97]) cube([0.05, 1.36, 0.62], center = true);    // door, above glass
            translate([-1.58, -1.22, 1.35]) cube([0.05, 0.32, 0.62], center = true);
            translate([-1.58, -0.20, 1.35]) cube([0.05, 0.36, 0.62], center = true);
        }
        cab_paint(cab_frame(), 0.7, 0.55)
        {
            // Door frame, top rail and the stiffeners pressed into the door skin.
            for (y = [-1.40, 0.00])
                translate([-1.545, y, 1.14]) cube([0.06, 0.07, 2.26], center = true);
            translate([-1.53, -0.70, 2.19]) cube([0.07, 1.60, 0.06], center = true);
            for (z = [0.18, 0.56, 0.90])
                translate([-1.548, -0.70, z]) rf_box([0.022, 1.22, 0.07], 0.008);
            translate([-1.548, -0.70, 1.86]) rf_box([0.022, 1.22, 0.07], 0.008);
            // Window gasket.
            for (z = [1.025, 1.675]) translate([-1.548, -0.72, z]) cube([0.03, 0.76, 0.035], center = true);
            for (y = [-1.075, -0.365]) translate([-1.548, y, 1.35]) cube([0.03, 0.035, 0.66], center = true);
            // Wall stringers aft of the door.
            for (z = [0.62, 1.20, 1.78])
                translate([-1.555, 0.44, z]) cube([0.03, 0.78, 0.05], center = true);
            translate([-1.555, 0.83, 1.14]) cube([0.05, 0.06, 2.26], center = true);
        }
        // Rivet lines along the door edges.
        cab_paint([0.30, 0.33, 0.32], 0.5, 0.8)
            for (z = [0.10 : 0.16 : 2.12], y = [-1.33, -0.07])
                translate([-1.552, y, z]) rotate([0, 90, 0]) cylinder(h = 0.012, r = 0.011, center = true, $fn = 6);
        // Door handle and its guard.
        cab_paint([0.62, 0.20, 0.10], 0.5, 0.2) translate([-1.525, -0.18, 1.02]) rf_box([0.03, 0.05, 0.16], 0.008);
        cab_paint(cab_dark(), 0.6, 0.6) translate([-1.54, -0.18, 1.02]) rf_box([0.02, 0.10, 0.24], 0.008);
        // Stencil band across the door.
        color(cab_stencil()) translate([-1.552, -0.70, 0.74]) cube([0.006, 1.22, 0.035], center = true);
        // Soundproofing between the stringers aft of the door.
        color(cab_pad())
            for (z = [0.90, 1.49])
                translate([-1.562, 0.42, z]) rf_box([0.03, 0.70, 0.48], 0.012);
        // Fire extinguisher in its bracket, intercom box with a coiled lead.
        cab_paint([0.55, 0.09, 0.06], 0.45, 0.1)
        {
            translate([-1.47, 0.62, 0.52]) cylinder(h = 0.34, r = 0.055, $fn = 10);
            translate([-1.47, 0.62, 0.86]) sphere(r = 0.055, $fn = 10);
        }
        cab_paint(cab_dark(), 0.5, 0.7)
        {
            translate([-1.47, 0.62, 0.90]) cylinder(h = 0.07, r = 0.016, $fn = 8);
            translate([-1.50, 0.62, 0.66]) cube([0.06, 0.13, 0.03], center = true);
            translate([-1.52, 0.25, 1.50]) rf_box([0.07, 0.16, 0.22], 0.012);
        }
        cab_paint([0.42, 0.44, 0.40], 0.6, 0.3)
        {
            translate([-1.482, 0.25, 1.545]) cube([0.006, 0.10, 0.06], center = true);
            for (i = [0 : 5]) translate([-1.50, 0.20 + (i % 2) * 0.012, 1.36 - i * 0.035]) sphere(r = 0.014, $fn = 6);
        }
    }
}

module cabin_seats(collision = "none")
{
    gk_flatten()
    {
        for (x = [-0.89, 0, 0.89])
        {
            // Canvas sling seat and back on a tube frame.
            color(cab_canvas())
            {
                translate([x, 0.26, 0.435]) rf_box([0.66, 0.54, 0.07], 0.022);
                translate([x, 0.60, 0.86]) rotate([8, 0, 0]) rf_box([0.66, 0.07, 0.72], 0.022);
            }
            color(cab_canvas() * 0.8)
                for (z = [0.66, 0.90, 1.14])
                    translate([x, 0.555, z + 0.01]) rotate([8, 0, 0]) cube([0.60, 0.012, 0.02], center = true);
            cab_paint([0.30, 0.32, 0.31], 0.45, 0.85)
                for (s = [-1, 1])
                {
                    translate([x + s * 0.335, 0.26, 0.43]) rotate([90, 0, 0]) cylinder(h = 0.58, r = 0.016, center = true, $fn = 8);
                    translate([x + s * 0.335, 0.62, 0.86]) rotate([8, 0, 0]) cylinder(h = 0.86, r = 0.016, center = true, $fn = 8);
                    translate([x + s * 0.335, 0.02, 0.22]) rotate([-12, 0, 0]) cylinder(h = 0.46, r = 0.014, center = true, $fn = 8);
                    translate([x + s * 0.335, 0.50, 0.22]) cylinder(h = 0.44, r = 0.014, center = true, $fn = 8);
                }
            // Lap belt ends hanging off the frame, buckle on one of them.
            color(cab_dark())
                for (s = [-1, 1])
                    translate([x + s * 0.30, 0.42, 0.33]) rotate([0, s * 6, 0]) cube([0.04, 0.012, 0.26], center = true);
            cab_paint([0.5, 0.52, 0.5], 0.4, 0.9) translate([x - 0.30, 0.42, 0.19]) rf_box([0.05, 0.02, 0.045], 0.006);
        }
        // Overhead: cable tray, a conduit run and the dome lamp housing above the seats.
        cab_paint(cab_dark(), 0.6, 0.5)
        {
            translate([-0.55, -0.45, 2.215]) cube([0.16, 3.2, 0.03], center = true);
            translate([0.55, -0.25, 2.19]) rf_box([0.36, 0.36, 0.06], 0.02);
        }
        cab_paint([0.32, 0.34, 0.33], 0.5, 0.8)
            for (x = [0.62, 0.69])
                translate([x, -0.45, 2.21]) rotate([90, 0, 0]) cylinder(h = 3.3, r = 0.016, center = true, $fn = 6);
        cab_paint([0.55, 0.16, 0.08], 0.5, 0.3)
            translate([-0.55, -0.45, 2.19]) rotate([90, 0, 0]) cylinder(h = 3.2, r = 0.012, center = true, $fn = 6);
        // First-aid pouch on the bulkhead and a secured radio case on the port seat.
        color([0.24, 0.30, 0.24]) translate([1.20, 0.65, 1.50]) rf_box([0.32, 0.17, 0.38], 0.03);
        color([0.72, 0.68, 0.56])
        {
            translate([1.20, 0.556, 1.50]) cube([0.18, 0.012, 0.05], center = true);
            translate([1.20, 0.554, 1.50]) cube([0.05, 0.012, 0.18], center = true);
        }
        cab_paint([0.105, 0.135, 0.125], 0.6, 0.3) translate([-0.90, 0.26, 0.63]) rf_box([0.42, 0.34, 0.32], 0.02);
        cab_paint([0.30, 0.34, 0.31], 0.5, 0.7)
        {
            translate([-0.90, 0.26, 0.805]) rf_box([0.26, 0.06, 0.03], 0.01);
            for (s = [-1, 1]) translate([-0.90 + s * 0.15, 0.085, 0.66]) rf_box([0.05, 0.016, 0.07], 0.005);
        }
        color(cab_stencil()) translate([-0.90, 0.088, 0.56]) cube([0.16, 0.006, 0.03], center = true);
        // A cargo strap across the bulkhead between the port seats.
        color(cab_dark()) translate([-0.45, 0.755, 1.52]) cube([1.6, 0.012, 0.045], center = true);
    }
}

cabin_shell();
cabin_port_wall();
cabin_seats();
