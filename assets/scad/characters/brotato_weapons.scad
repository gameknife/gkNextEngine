// ============================================================================
// brotato_weapons.scad - Brotato3D held weapons (a ScadRig used as a catalog of
// static models, the same trick as nextdayz_weapons.scad).
//
// Each bone_wpn_<weaponId> is one weapon; weaponId matches weapons.json. Model
// space is the survivor weapon frame: origin = firing-hand grip, muzzle -Y,
// top +Z, so Brotato3D hangs it on bone_weapon_socket unchanged.
// ============================================================================

use <../lib/kit_coldwar.scad>
use <../lib/kit_nd_arsenal.scad>
use <../lib/rig_forms.scad>

module bone_wpn_smg() { nd_wpn_bizon_held(); }
module bone_wpn_shotgun() { cw_wpn_shotgun_held(); }
module bone_wpn_sniper() { cw_wpn_svd_held(scope = true); }
module bone_wpn_rocket() { cw_wpn_rpg_held(); }

// Garage-built flamethrower: a fuel bottle slung under a scorched pipe.
module bone_wpn_flamethrower()
{
    color([0.20, 0.20, 0.22]) translate([0, -0.250, 0.060]) rotate([90, 0, 0]) cylinder(h = 0.620, r = 0.022, center = true, $fn = 8);
    color([0.12, 0.10, 0.09]) translate([0, -0.570, 0.060]) rotate([90, 0, 0]) cylinder(h = 0.090, r1 = 0.036, r2 = 0.024, center = true, $fn = 8);
    color([0.74, 0.20, 0.12]) translate([0, -0.180, -0.035]) rotate([90, 0, 0]) cylinder(h = 0.300, r = 0.052, center = true, $fn = 10);
    color([0.60, 0.58, 0.52]) translate([0, -0.345, -0.035]) rf_box([0.050, 0.040, 0.050], 0.010);
    color([0.16, 0.14, 0.12]) translate([0, 0.010, -0.040]) rotate([-14, 0, 0]) rf_box([0.036, 0.046, 0.110], 0.010);
    color([0.16, 0.14, 0.12]) translate([0, -0.330, -0.010]) rf_box([0.034, 0.040, 0.100], 0.010);
    color([0.30, 0.22, 0.16]) translate([0, 0.160, 0.040]) rf_box([0.040, 0.220, 0.090], 0.014);
    color([1.00, 0.55, 0.10]) translate([0, -0.612, 0.036]) sphere(r = 0.014, $fn = 6);
}

// Salvaged lab laser: a boxy receiver with a glowing coil.
module bone_wpn_laser()
{
    color([0.80, 0.82, 0.84]) translate([0, -0.140, 0.062]) rf_box([0.060, 0.460, 0.090], 0.014);
    color([0.22, 0.24, 0.28]) translate([0, -0.420, 0.062]) rf_box([0.044, 0.180, 0.060], 0.010);
    color([0.30, 0.95, 1.00]) for (y = [-0.300, -0.230, -0.160, -0.090]) translate([0, y, 0.062]) rf_box([0.070, 0.030, 0.100], 0.008);
    color([0.30, 0.95, 1.00]) translate([0, -0.516, 0.062]) sphere(r = 0.022, $fn = 6);
    color([0.16, 0.16, 0.18]) translate([0, 0.010, -0.040]) rotate([-14, 0, 0]) rf_box([0.036, 0.046, 0.110], 0.010);
    color([0.16, 0.16, 0.18]) translate([0, -0.300, -0.010]) rf_box([0.034, 0.040, 0.090], 0.010);
    color([0.22, 0.24, 0.28]) translate([0, 0.180, 0.050]) rf_box([0.044, 0.200, 0.110], 0.016);
    color([0.95, 0.80, 0.20]) translate([0.032, -0.060, 0.062]) cube([0.006, 0.060, 0.030], center = true);
}

module bone_root()
{
    bone_wpn_smg(); bone_wpn_shotgun(); bone_wpn_sniper(); bone_wpn_rocket();
    bone_wpn_flamethrower(); bone_wpn_laser();
}

bone_root();
