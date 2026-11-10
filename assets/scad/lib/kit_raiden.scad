// kit_raiden: NextRaiden low-poly brick arcade shooter asset kit.
// Pure module/function library, no top-level geometry, no textures.
// 1 unit = 1 m; Z-up; ALL aircraft / guns / boats face -Y, matching ScadRig.
// Ground objects bottom z=0. Tiles TOP z=0, bottom=-0.4 (intentional zMin).
// Aircraft bottom z=0; flight altitude belongs to the caller. No baked shadows.
// Player width 5.5m, scouts 3.3m, boss 14m. Gameplay camera: height 75m, FOV 50.
// damage=0 intact, 1 stripped outer panels, 2 core only, 3 empty/destroyed.
// explode=0 assembled; >0 exploded inspection pose (NOT a physics simulation).
// raiden_airframes.scad owns stable per-instance IDs; raiden_parts.scad owns meshes.
// Each nr_chunk keeps id/releaseStage/massKg/hitPoints/group/collider metadata.
// Suggested convex proxies should be recentered at bounds centre on physics handoff.
// No preattached PhysicsComponents: future application owns hit routing, detachment,
// world-pose preservation, velocities, pooling, lifetime and debris collision layers.
// For animated variants use characters/next_raiden_{rotor,gunship,fortress}.scad.
// Static SCAD previews show bind poses; clips are consumed only by ScadRig.

use <raiden_parts.scad>
use <raiden_airframes.scad>

// ================= Aircraft =================
module nr_veh_player(damage = 0, explode = 0) { nr_assemble(nr_interceptor(), 0, damage, explode); }
module nr_veh_wingman(damage = 0, explode = 0) { nr_assemble(nr_scout(), 0, damage, explode); }
module nr_veh_scout(paint = 1, damage = 0, explode = 0) { nr_assemble(nr_scout(), paint, damage, explode); }
module nr_veh_interceptor(paint = 1, damage = 0, explode = 0) { nr_assemble(nr_interceptor(), paint, damage, explode); }
module nr_veh_bomber(paint = 2, damage = 0, explode = 0) { nr_assemble(nr_bomber(), paint, damage, explode); }
module nr_veh_rotorcraft(paint = 3, damage = 0, explode = 0) { nr_assemble(nr_rotorcraft(), paint, damage, explode); }
module nr_veh_gunship(paint = 4, damage = 0, explode = 0) { nr_assemble(nr_gunship(), paint, damage, explode); }
module nr_veh_fortress(paint = 1, damage = 0, explode = 0) { nr_assemble(nr_fortress(), paint, damage, explode); }

// ================= Individual detachable pieces =================
module nr_part_armour(paint = 1) { nr_chunk(size = [1.1,1.2,0.28], paint = paint); }
module nr_part_wing(paint = 1) { nr_chunk(id = "wing", size = [1.1,1.9,0.23], shape = 1, paint = paint); }
module nr_part_canopy() { nr_chunk(id = "canopy", size = [0.8,1.4,0.6], shape = 2); }
module nr_part_engine() { nr_chunk(id = "engine", size = [0.7,1.5,0.8], shape = 3, tone = 3, releaseStage = 2); }
module nr_part_core() { nr_chunk(id = "core", size = [1.0,1.5,0.6], shape = 4, releaseStage = 3); }
module nr_part_gun() { nr_chunk(id = "gun", size = [0.4,1.6,0.4], shape = 5, releaseStage = 2); }
module nr_part_fin(paint = 1) { nr_chunk(id = "fin", size = [0.22,1.1,0.9], shape = 6, paint = paint); }
module nr_part_blade() { nr_chunk(id = "blade", size = [2.5,0.28,0.12], shape = 7, group = "rotor"); }
module nr_part_pod(paint = 1) { nr_chunk(id = "pod", size = [0.7,1.1,0.5], shape = 8, paint = paint); }

// Resting wreck is assembled from the SAME chunks, not a separate replacement mesh.
// Flat Z placement intentionally preserves reliable ground contact without physics.
module nr_prop_wreck(seed = 4, paint = 1, stage = 1)
{
    parts = nr_interceptor();
    for (i = [0:len(parts)-1])
        let (r = parts[i])
            if (r[5] <= stage)
                translate([(i%5-2)*1.65, (floor(i/5)-1.5)*2.35, 0])
                    rotate([0,0,nr_rnd(seed+i*17,360)]) nr_piece(r, paint);
}

// ================= Ground enemies and boats =================
module nr_veh_tank(paint = 2, damage = 0)
{
    if (damage < 3) nr_chunk(id = "chassis", size = [1.9,3.1,0.60], shape = 4, releaseStage = 3, paint = paint);
    if (damage < 2)
    {
        for (s = [-1,1]) translate([s*1.15,0,0])
            nr_chunk(id = str("track_",s), size = [0.50,3.35,0.70], tone = 2, releaseStage = 2);
        translate([0,0.24,0.68]) nr_chunk(id = "turret", size = [1.38,1.55,0.6], paint = paint, releaseStage = 2);
        translate([0,-1.0,0.93]) nr_chunk(id = "barrel", size = [0.35,2.5,0.35], shape = 5, releaseStage = 2);
    }
    if (damage < 1) for (s = [-1,1]) translate([s*0.82,0,0.68])
        nr_chunk(id = str("skirt_",s), size = [0.29,2.8,0.30], paint = paint);
}

module nr_prop_turret(paint = 1, damage = 0)
{
    if (damage < 3) nr_chunk(id = "base", size = [2.3,2.3,0.38], shape = 4, releaseStage = 3);
    if (damage < 2)
    {
        translate([0,0,0.59]) nr_chunk(id = "head", size = [1.65,1.6,0.7], paint = paint, releaseStage = 2);
        for (s = [-1,1]) translate([s*0.44,-0.70,1.0])
            nr_chunk(id = str("barrel_",s), size = [0.3,1.75,0.34], shape = 5, releaseStage = 2);
    }
    if (damage < 1) for (s = [-1,1]) translate([s*0.96,0,0.39])
        nr_chunk(id = str("shield_",s), size = [0.3,1.9,0.95], paint = paint);
}

module nr_veh_launcher(paint = 2, damage = 0)
{
    if (damage < 3) nr_chunk(id = "chassis", size = [1.8,3.7,0.56], shape = 4, releaseStage = 3);
    if (damage < 2)
    {
        translate([0,-1.22,0.58]) nr_chunk(id = "cab", size = [1.75,1.1,0.9], shape = 2);
        for (s = [-1,1]) translate([s*1.02,0,0])
            nr_chunk(id = str("track_",s), size = [0.4,3.6,0.58], tone = 2, releaseStage = 2);
    }
    if (damage < 1) for (s = [-1,1], y = [0,1]) translate([s*0.47,y*0.95,0.78])
        nr_chunk(id = str("rack_",s,"_",y), size = [0.84,0.86,0.75], shape = 8, paint = paint);
}

module nr_veh_patrolboat(paint = 2, damage = 0)
{
    if (damage < 3) nr_chunk(id = "keel", size = [1.3,5.4,0.45], shape = 4, releaseStage = 3);
    if (damage < 2)
    {
        for (s = [-1,1], j = [0:2]) translate([s*1.0,-1.8+j*1.8,0.08])
            nr_chunk(id = str("hull_",s,"_",j), size = [0.65,1.72,0.65], paint = paint, releaseStage = 2);
        translate([0,0.75,0.68]) nr_chunk(id = "bridge", size = [1.5,1.4,1.2], shape = 2);
        translate([0,-1.35,0.69]) nr_chunk(id = "deck_gun", size = [0.62,1.7,0.55], shape = 5, releaseStage = 2);
    }
    if (damage < 1) translate([0,1.8,0.56]) nr_chunk(id = "exhaust", size = [0.9,0.75,0.65], shape = 3);
}

// ================= Scrolling tiles: 16m pitch, top = 0 =================
module nr_ground_grass(seed = 0)
{
    gk_flatten()
    {
        color(nr_SOIL()) translate([0,0,-0.4]) nr_box([16,16,0.36]);
        color(nr_GRASS()) translate([0,0,-0.04]) nr_box([16,16,0.04]);
        for (i = [0:2]) color(i == 0 ? [0.18,0.30,0.15] : [0.23,0.35,0.17])
            translate([nr_rnd(seed+i*11,90)/10-4.5, nr_rnd(seed+i*29,90)/10-4.5, 0])
                rotate([0,0,nr_rnd(seed+i*7,180)])
                    scale([1.0+nr_rnd(seed+i,12)/10,0.8,1]) cylinder(h = 0.012, r = 1.45, $fn = 7);
    }
}
// Markings belong to a runway section, not to every 16m tile: 0 centreline,
// 1 threshold, 2 touchdown bars. Width changes do not change the 16m row pitch.
module nr_ground_runway(seed = 0, width = 16, marking = 0)
{
    gk_flatten()
    {
        color(nr_DARK()) translate([0,0,-0.4]) nr_box([width,16,0.4]);
        for (i = [0:1]) color([0.053,0.066,0.071])
            translate([(nr_rnd(seed+i*19,80)/100-0.4)*width,nr_rnd(seed+i*31,80)/10-4,0])
                nr_box([1.2+nr_rnd(seed+i*3,20)/10,3+nr_rnd(seed+i*13,35)/10,0.012]);
        for (s = [-1,1]) color([0.40,0.43,0.42]) translate([s*(width/2-1.1),0,0.02])
            nr_box([0.18,16,0.02]);
        for (y = [-4,4]) color(nr_WHITE()) translate([0,y,0.02]) nr_box([0.3,3.0,0.02]);
        if (marking == 1)
            for (s = [-1,1], i = [0:3]) color(nr_WHITE())
                translate([s*(2.0+i*(width/2-4)/4),0,0.02]) nr_box([0.55,5.0,0.02]);
        if (marking == 2)
            for (s = [-1,1]) color(nr_WHITE()) translate([s*width*0.27,0,0.02]) nr_box([1.3,4.6,0.02]);
        for (s = [-1,1]) color([0.40,0.32,0.12]) translate([s*(width/2-0.35),-5,0.02])
            nr_box([0.22,0.5,0.06]);
    }
}
module nr_ground_apron(seed = 0, width = 16, variant = 0)
{
    gk_flatten()
    {
        color([0.20,0.23,0.22]) translate([0,0,-0.4]) nr_box([width,16,0.4]);
        // Staggered joints and local repairs, never a full-tile colour swap.
        jointY = -4+nr_rnd(seed,7);
        color([0.16,0.19,0.18]) translate([0,jointY,0]) nr_box([width,0.055,0.012]);
        color([0.16,0.19,0.18]) translate([width*0.21,0,0]) nr_box([0.055,16,0.012]);
        if (nr_rnd(seed+5,3) == 0) color([0.18,0.21,0.20])
            translate([-width*0.2,3,0]) nr_box([width*0.32,4.5,0.014]);
        color([0.43,0.32,0.10]) translate([-width*0.35,0,0.02]) nr_box([0.13,16,0.02]);
        if (variant == 1)
            for (y = [-5,1,6]) color([0.38,0.40,0.37]) translate([width*0.08,y,0.02])
                nr_box([width*0.58,0.12,0.02]);
        if (variant == 2) color([0.43,0.32,0.10]) translate([0,2,0.02]) nr_box([width,0.16,0.02]);
    }
}
module nr_ground_road(seed = 0)
{
    gk_flatten()
    {
        color(nr_GRASS()) translate([0,0,-0.4]) nr_box([16,16,0.4]);
        color([0.12,0.15,0.15]) translate([0,0,0.02]) nr_box([6,16,0.04]);
        for (s = [-1,1]) color(nr_CONCRETE()) translate([s*3.6,0,0.02]) nr_box([1.0,16,0.08]);
        for (y = [-6,-2,2,6]) color(nr_YELLOW()) translate([0,y,0.065]) nr_box([0.16,1.8,0.015]);
        color([0.105,0.13,0.13]) translate([nr_rnd(seed,2)==0?-1.5:1.5,nr_rnd(seed+9,80)/10-4,0.062])
            nr_box([1.2,2+nr_rnd(seed+7,30)/10,0.012]);
    }
}
module nr_ground_water(seed = 0)
{
    gk_flatten()
    {
        gk_material(nr_WATER(), roughness = 0.28) translate([0,0,-0.4]) nr_box([16,16,0.4]);
        for (i = [0:2+nr_rnd(seed,3)]) color(i%2==0 ? [0.034,0.26,0.30] : [0.045,0.28,0.32])
            translate([nr_rnd(seed+i*7,90)/10-4.5,nr_rnd(seed+i*13,130)/10-6.5,0])
                linear_extrude(height = 0.012)
                    polygon([[-2.2,0],[-0.6,-0.10],[1.4+nr_rnd(seed+i*17,18)/10,0.08],
                             [0.8,0.17],[-1.3,0.08]]);
    }
}
// edge: -1/+1 waterfront side; 0 inland; 2 standalone tile with two edges.
module nr_ground_quay(seed = 0, edge = 2)
{
    gk_flatten()
    {
        color(nr_CONCRETE()) translate([0,0,-0.4]) nr_box([16,16,0.4]);
        for (s = [-1,1]) if (edge == s || edge == 2)
        {
            color([0.45,0.35,0.13]) translate([s*7.5,0,0]) nr_box([0.2,16,0.03]);
            color([0.20,0.23,0.22]) translate([s*7.85,0,0]) nr_box([0.3,16,0.10]);
        }
        color([0.24,0.27,0.25]) translate([0,-5+nr_rnd(seed,11),0]) nr_box([16,0.06,0.012]);
        if (nr_rnd(seed+5,3) == 0) color([0.25,0.28,0.26])
            translate([-2+nr_rnd(seed+11,5),2,0]) nr_box([4.0,5.6,0.014]);
    }
}
// Crop states and furrow directions persist over several rows in stage bands.
module nr_ground_farmland(seed = 0, variant = 0, direction = 0)
{
    gk_flatten()
    {
        color(variant == 0 ? [0.20,0.25,0.13] : variant == 1 ? [0.29,0.25,0.14] :
              variant == 2 ? nr_SOIL() : [0.19,0.26,0.14])
            translate([0,0,-0.4]) nr_box([16,16,0.4]);
        rotate([0,0,direction*90])
        {
            if (variant != 3)
                for (x = [-6.5:2.6:6.5])
                    color(variant == 0 ? [0.24,0.31,0.14] : variant == 1 ? [0.34,0.30,0.17] : [0.20,0.16,0.11])
                        translate([x,0,0]) nr_box([variant == 2 ? 0.45 : 1.3,16,variant == 0 ? 0.18 : 0.06]);
            else
                for (i = [0:2]) color([0.22,0.28,0.15])
                    translate([-4+i*4,-3+nr_rnd(seed+i*23,7),0])
                        scale([1.2,0.7,1]) cylinder(h = 0.02, r = 2.1, $fn = 7);
            if (variant == 1 && nr_rnd(seed+3,4) == 0) color([0.37,0.32,0.17])
                translate([-4+nr_rnd(seed+11,9),-3+nr_rnd(seed+23,7),0.07]) nr_box([1.4,2.8,0.03]);
        }
    }
}

// ================= Buildings and scenery =================
module nr_bldg_hangar(paint = 2)
{
    gk_flatten()
    {
        color(nr_CONCRETE()) nr_box([7.0,7.2,0.20]);
        for (s = [-1,1]) nr_surface(nr_WHITE()) translate([s*3.12,0,0.2]) nr_box([0.48,6.8,2.65]);
        nr_surface(nr_WHITE()) translate([0,3.1,0.2]) nr_box([6.4,0.48,2.65]);
        nr_surface(nr_paint(paint)) translate([0,0,2.85]) nr_bevel([6.8,7.0,0.65]);
        for (x = [-2.6,0,2.6]) nr_surface(nr_YELLOW()) translate([x,-3.28,0.2]) nr_box([0.16,0.13,2.7]);
        for (y = [-2,0,2]) nr_surface(nr_STEEL()) translate([0,y,3.51]) nr_box([6.2,0.12,0.04]);
    }
}
module nr_bldg_controltower()
{
    gk_flatten()
    {
        color(nr_CONCRETE()) nr_bevel([3.4,3.4,0.35]);
        nr_surface(nr_WHITE()) translate([0,0,0.35]) nr_bevel([1.7,1.8,4.2]);
        nr_surface(nr_STEEL()) translate([0,0,4.55]) nr_bevel([3.1,2.8,0.28]);
        nr_surface(nr_GLASS()) translate([0,0,4.83]) nr_bevel([2.7,2.4,1.05]);
        nr_surface(nr_RED()) translate([0,0,5.88]) nr_bevel([3.25,2.96,0.32]);
        nr_surface(nr_WHITE()) translate([0,0,6.2]) cylinder(h = 1.3, r = 0.08, $fn = 6);
        nr_surface(nr_YELLOW()) translate([0,0,7.5]) cube([0.22,0.22,0.22], center = true);
    }
}
module nr_bldg_bunker(damage = 0)
{
    if (damage < 3) nr_chunk(id = "foundation", size = [4.8,4.2,0.45], shape = 4, releaseStage = 3);
    if (damage < 2) for (s = [-1,1]) translate([s*1.9,0,0.46])
        nr_chunk(id = str("wall_",s), size = [0.65,4.1,1.55], tone = 3, releaseStage = 2);
    if (damage < 1) for (i = [-1,0,1]) translate([i*1.5,0,2.05])
        nr_chunk(id = str("roof_",i), size = [1.4,4.3,0.48], paint = 2);
}
module nr_bldg_factory()
{
    gk_flatten()
    {
        nr_surface(nr_WHITE()) nr_bevel([6.0,7.0,2.8]);
        for (y = [-2.3,0,2.3]) nr_surface(nr_TEAL()) translate([0,y,2.8]) nr_bevel([6.4,2.18,0.65]);
        for (x = [-2,0,2]) nr_surface(nr_GLASS()) translate([x,-3.51,1.1]) nr_box([1.15,0.03,0.8]);
        for (x = [-1.8,1.8])
        {
            nr_surface(nr_STEEL()) translate([x,2.0,3.4]) nr_box([0.85,0.85,2.0]);
            nr_surface(nr_RED()) translate([x,2.0,5.4]) nr_box([0.88,0.88,0.35]);
        }
    }
}
module nr_bldg_fueldepot()
{
    gk_flatten()
    {
        color(nr_CONCRETE()) nr_box([6.5,5,0.25]);
        for (s = [-1,1])
        {
            nr_surface(nr_WHITE()) translate([s*1.65,0,0.25]) cylinder(h = 2.9, r = 1.23, $fn = 10);
            nr_surface(nr_RED()) translate([s*1.65,0,2.0]) cylinder(h = 0.40, r = 1.25, $fn = 10);
            nr_surface(nr_STEEL()) translate([s*1.65,0,3.15]) cylinder(h = 0.20, r1 = 1.23, r2 = 0.84, $fn = 10);
        }
    }
}
module nr_prop_radar()
{
    gk_flatten()
    {
        color(nr_CONCRETE()) nr_bevel([2.6,2.6,0.3]);
        nr_surface(nr_STEEL()) translate([0,0,0.3]) nr_box([0.42,0.42,2.0]);
        nr_surface(nr_TEAL()) translate([0,0,2.3]) rotate([15,0,0]) nr_bevel([3.0,0.22,1.45]);
        for (x = [-1,0,1]) nr_surface(nr_WHITE()) translate([x,-0.15,2.45]) nr_box([0.06,0.06,1.0]);
    }
}
module nr_prop_container(paint = 1, variant = 0, civilian = false)
{
    gk_flatten()
    {
        length = variant == 1 ? 3.6 : variant == 2 ? 6.0 : 5.0;
        nr_surface(civilian ? nr_scenery_roof(paint) : nr_paint(paint)) nr_bevel([2.4,length,2.3]);
        for (y = [-length/2+0.4:0.7:length/2-0.3]) nr_surface(nr_STEEL())
            translate([0,y,2.3]) nr_box([2.15,0.07,0.04]);
        for (x = [-0.65,0.65]) nr_surface(nr_WHITE()) translate([x,-length/2-0.01,0.2]) nr_box([0.08,0.04,1.8]);
        if (variant == 1) color([0.34,0.38,0.35]) translate([0,0,2.34]) nr_box([1.25,1.7,0.18]);
    }
}
module nr_prop_crane()
{
    gk_flatten()
    {
        color(nr_CONCRETE()) nr_box([4.2,4.2,0.3]);
        for (s = [-1,1]) nr_surface(nr_YELLOW()) translate([s*1.6,0,0.3]) nr_box([0.5,1.5,5.3]);
        nr_surface(nr_YELLOW()) translate([0,-1.4,5.6]) nr_box([4.0,6.0,0.65]);
        nr_surface(nr_DARK()) translate([0,-3.75,2.6]) nr_box([0.10,0.10,3.0]);
        nr_surface(nr_STEEL()) translate([0,-3.75,2.4]) nr_box([1.8,0.4,0.22]);
        nr_surface(nr_GLASS()) translate([1.05,-0.6,4.3]) nr_bevel([1.1,1.4,1.15]);
    }
}
module nr_prop_barrier()
{
    gk_flatten()
    {
        color(nr_CONCRETE()) nr_bevel([3.4,0.65,0.75]);
        for (i = [-3:3]) color(i%2==0 ? nr_YELLOW() : nr_DARK())
            translate([i*0.44,-0.331,0.28]) nr_box([0.42,0.025,0.25]);
    }
}
module nr_prop_crate()
{
    gk_flatten()
    {
        nr_surface(nr_TEAL()) nr_bevel([1.2,1.2,1.05]);
        for (s = [-1,1]) nr_surface(nr_WHITE()) translate([s*0.38,0,1.05]) nr_box([0.14,1.18,0.05]);
    }
}
module nr_nature_tree(seed = 0)
{
    gk_flatten()
    {
        color(nr_SOIL()) nr_box([0.38,0.38,1.1]);
        for (j = [0:2]) color([0.10+j*0.035,0.24+j*0.04,0.12]) translate([0,0,0.65+j*0.7])
            rotate([0,0,nr_rnd(seed,40)]) cylinder(h = 1.4, r1 = 1.2-j*0.25, r2 = 0.1, $fn = 5);
    }
}
module nr_nature_rock(seed = 0)
{
    gk_flatten() color(nr_CONCRETE()) rotate([0,0,nr_rnd(seed,90)])
        scale([1.3,1,1]) cylinder(h = 0.95, r1 = 1.0, r2 = 0.62, $fn = 5);
}

// ================= Airfield, rural and dock scenery =================
// Muted roof colours keep unarmed facilities distinct from red combat targets.
// All variants are grounded at z=0, flattened, and below the 7m flight plane.
function nr_scenery_roof(variant = 0) = [[0.12,0.24,0.23],[0.29,0.17,0.12],
                                        [0.22,0.26,0.30]][variant%3];

module nr_bldg_shelter(variant = 0)
{
    gk_flatten()
    {
        color(nr_CONCRETE()) nr_box([8.2,9.0,0.2]);
        // Open barrel vault, extruded along Y; no scene-scale boolean operation.
        color(nr_scenery_roof(variant)) translate([0,4,0.2]) rotate([90,0,0])
            linear_extrude(height = 8)
                polygon([[-3.9,0],[-3.9,1.3],[-2.8,3.0],[-1.3,3.7],[1.3,3.7],
                         [2.8,3.0],[3.9,1.3],[3.9,0],[3.35,0],[3.35,1.2],
                         [2.4,2.55],[1.1,3.15],[-1.1,3.15],[-2.4,2.55],[-3.35,1.2],[-3.35,0]]);
        color([0.21,0.25,0.21]) translate([0,3.75,0.2]) nr_box([6.7,0.4,2.8]);
        for (y = [-2.6,0,2.6]) color(nr_STEEL()) translate([0,y,3.92]) nr_box([2.4,0.18,0.07]);
        if (variant == 1) color([0.26,0.30,0.27]) translate([0,-3.85,0.2]) nr_box([6.6,0.12,2.7]);
        if (variant == 2) color([0.17,0.25,0.26]) translate([0,0,3.92]) nr_box([1.7,4.8,0.15]);
    }
}

module nr_bldg_workshop(variant = 0)
{
    gk_flatten()
    {
        width = variant == 1 ? 8.8 : 6.8;
        color(nr_CONCRETE()) nr_box([width+0.6,7.2,0.2]);
        color([0.35,0.36,0.30]) translate([0,0,0.2]) nr_box([width,6.5,2.45]);
        for (s = [-1,1])
        {
            color(nr_scenery_roof(variant)) translate([s*width/4,0,2.65]) nr_bevel([width/2-0.05,6.8,0.6]);
            color([0.12,0.20,0.21]) translate([s*width/4,0.4,3.25]) nr_box([1.0,3.9,0.04]);
            color(nr_STEEL()) translate([s*width/4,-3.27,0.2]) nr_box([width*0.32,0.04,1.85]);
        }
        if (variant == 2) color(nr_STEEL()) translate([width*0.35,2.1,3.25]) nr_box([0.6,0.7,1.4]);
    }
}

module nr_bldg_barracks(variant = 0)
{
    gk_flatten()
    {
        color(nr_CONCRETE()) nr_box([7.0,8.0,0.18]);
        color([0.31,0.34,0.28]) translate([-1.4,0,0.18]) nr_box([3.5,7.2,1.9]);
        color(nr_scenery_roof(variant)) translate([-1.4,0,2.08]) nr_bevel([3.9,7.6,0.5]);
        color([0.31,0.34,0.28]) translate([1.8,2,0.18]) nr_box([2.9,3.2,1.9]);
        color(nr_scenery_roof(variant)) translate([1.8,2,2.08]) nr_bevel([3.2,3.6,0.5]);
        for (y = [-2.6,-0.7,1.2]) color([0.10,0.19,0.20]) translate([0.37,y,0.95]) nr_box([0.04,0.8,0.7]);
        if (variant == 1) color([0.18,0.24,0.19]) translate([1.8,-1.8,0.18]) nr_box([2.2,2.6,0.65]);
        if (variant == 2) color(nr_STEEL()) translate([-1.4,1.5,2.58]) nr_box([1.5,1.7,0.45]);
    }
}

module nr_bldg_warehouse(variant = 0)
{
    gk_flatten()
    {
        color(nr_CONCRETE()) nr_box([9.6,10.6,0.22]);
        color([0.32,0.34,0.30]) translate([0,0,0.22]) nr_box([8.8,9.4,2.5]);
        color(nr_scenery_roof(variant)) translate([0,0,2.72]) hull()
        {
            nr_box([9.3,9.9,0.10]);
            translate([0,0,1.05]) nr_box([0.25,9.9,0.10]);
        }
        for (y = [-3,-1,1,3]) color([0.33,0.36,0.33]) translate([0,y,3.86]) nr_box([0.9,0.9,0.15]);
        for (x = [-2.6,2.6]) color(nr_STEEL()) translate([x,-4.73,0.22]) nr_box([2.3,0.06,1.85]);
        if (variant == 1) color([0.20,0.26,0.23]) translate([0,-4.9,2.3]) nr_box([8.4,1.1,0.15]);
        if (variant == 2) color([0.17,0.22,0.22]) translate([2.9,0,3.16]) rotate([0,13,0]) nr_box([2.3,7.6,0.10]);
    }
}

module nr_bldg_substation(variant = 0)
{
    gk_flatten()
    {
        color([0.24,0.26,0.23]) nr_box([7.0,6.6,0.18]);
        for (s = [-1,1])
        {
            color(nr_STEEL()) translate([s*1.8,0,0.18]) nr_bevel([1.9,3.4,1.35]);
            for (y = [-1,0,1]) color([0.35,0.32,0.22]) translate([s*1.8,y,1.53]) cylinder(h = 0.7, r = 0.24, $fn = 6);
            for (y = [-1.1,0,1.1]) color([0.12,0.17,0.17]) translate([s*2.8,y,0.4]) nr_box([0.18,0.55,0.9]);
        }
        color([0.30,0.32,0.29]) translate([0,2.3,0.18]) nr_box([6.2,0.9,1.5]);
        if (variant > 0)
        {
            for (x = [-2.7,2.7]) color(nr_STEEL()) translate([x,-2.4,0.18]) nr_box([0.2,0.2,2.7]);
            color(nr_STEEL()) translate([0,-2.4,2.88]) nr_box([5.6,0.22,0.18]);
        }
    }
}

module nr_bldg_pumpstation(variant = 0)
{
    gk_flatten()
    {
        color(nr_CONCRETE()) nr_box([7.4,6.6,0.2]);
        for (x = [-2,0.4])
        {
            for (y = [-1.4,1.4]) color(nr_STEEL()) translate([x,y,0.2]) nr_box([1.8,0.4,0.6]);
            color([0.36,0.39,0.35]) translate([x,2.2,1.55]) rotate([90,0,0]) cylinder(h = 4.4, r = 0.95, $fn = 10);
            color(nr_scenery_roof(variant)) translate([x,0,2.35]) nr_box([0.5,0.6,0.35]);
        }
        color([0.25,0.31,0.26]) translate([2.7,0.7,0.2]) nr_box([1.3,2.4,1.5]);
        color(nr_STEEL()) translate([0,-2.65,0.55]) nr_box([5.7,0.25,0.25]);
        for (x = [-2,0.4]) color(nr_STEEL()) translate([x,-2.65,0.6]) nr_box([0.25,0.25,0.8]);
    }
}

module nr_bldg_silos(variant = 0)
{
    gk_flatten()
    {
        color(nr_CONCRETE()) nr_box([7.5,6.0,0.2]);
        for (i = [0:2])
        {
            h = 2.4+(variant == 1 ? i*0.4 : 0.5);
            color([0.40,0.41,0.35]) translate([-2.5+i*2.5,0,0.2]) cylinder(h = h, r = 1.05, $fn = 10);
            color(nr_scenery_roof(variant)) translate([-2.5+i*2.5,0,h+0.2]) cylinder(h = 0.75, r1 = 1.12, r2 = 0.16, $fn = 10);
            for (z = [0.8,1.8]) color(nr_STEEL()) translate([-2.5+i*2.5,0,z]) cylinder(h = 0.1, r = 1.07, $fn = 10);
        }
        color(nr_STEEL()) translate([0,2,0.2]) nr_box([6.4,0.6,0.6]);
    }
}

module nr_bldg_greenhouse(variant = 0)
{
    gk_flatten()
    {
        color([0.25,0.26,0.19]) nr_box([7.6,8.6,0.12]);
        for (x = [-1.9,1.9])
        {
            color(variant == 1 ? [0.27,0.34,0.28] : [0.18,0.28,0.27])
                translate([x,4,0.12]) rotate([90,0,0]) linear_extrude(height = 8)
                    polygon([[-1.6,0],[1.6,0],[1.6,1.0],[0.9,1.8],[-0.9,1.8],[-1.6,1.0]]);
            for (y = [-3,-1,1,3]) color([0.39,0.42,0.36]) translate([x,y,1.92]) nr_box([1.8,0.14,0.06]);
            color([0.12,0.20,0.19]) translate([x,-4.02,0.12]) nr_box([1.0,0.04,1.2]);
        }
    }
}

module nr_bldg_watertower(variant = 0)
{
    gk_flatten()
    {
        color(nr_CONCRETE()) nr_box([4.6,4.6,0.2]);
        for (x = [-1.5,1.5], y = [-1.5,1.5]) color(nr_STEEL()) translate([x,y,0.2]) nr_box([0.27,0.27,3.4]);
        for (z = [1.2,2.8], s = [-1,1]) color(nr_STEEL())
        {
            translate([s*1.5,0,z]) nr_box([0.18,3.2,0.18]);
            translate([0,s*1.5,z]) nr_box([3.2,0.18,0.18]);
        }
        color([0.34,0.38,0.34]) translate([0,0,3.6]) cylinder(h = 1.35, r = 2.0, $fn = 10);
        color(nr_scenery_roof(variant)) translate([0,0,4.95]) cylinder(h = 0.45, r1 = 2.1, r2 = 0.8, $fn = 10);
    }
}

module nr_prop_helipad(variant = 0)
{
    gk_flatten()
    {
        color([0.15,0.19,0.18]) cylinder(h = 0.12, r = 4.3, $fn = 12);
        color([0.42,0.40,0.29]) translate([0,0,0.12]) difference()
        {
            cylinder(h = 0.025, r = 3.5, $fn = 12);
            translate([0,0,-0.01]) cylinder(h = 0.05, r = 3.3, $fn = 12);
        }
        color([0.43,0.44,0.37]) translate([0,0,0.12])
        {
            for (x = [-1.1,1.1]) translate([x,0,0]) nr_box([0.32,3.0,0.03]);
            nr_box([2.4,0.32,0.03]);
        }
        if (variant > 0) for (x = [-1,1]) color([0.37,0.33,0.17]) translate([x*3.9,0,0.12]) nr_box([0.25,0.6,0.12]);
    }
}

module nr_prop_servicecart(variant = 0)
{
    gk_flatten()
    {
        for (s = [-1,1], y = [-1.45,1.45]) color(nr_DARK())
            translate([s*1.0,y,0.42]) rotate([0,90,0]) cylinder(h = 0.28, r = 0.42, center = true, $fn = 8);
        color(nr_STEEL()) translate([0,0,0.48]) nr_box([1.8,4.2,0.35]);
        color(nr_scenery_roof(variant)) translate([0,-1.35,0.83]) nr_bevel([1.85,1.3,1.15]);
        color([0.10,0.20,0.23]) translate([0,-2.01,1.25]) nr_box([1.5,0.03,0.55]);
        if (variant == 0) color([0.36,0.38,0.32]) translate([0,1.65,1.5]) rotate([90,0,0]) cylinder(h = 2.65, r = 0.7, $fn = 8);
        else if (variant == 1) color([0.27,0.32,0.22]) translate([0,0.75,0.83]) nr_bevel([1.85,2.7,1.05]);
        else for (y = [0,1.25]) color([0.33,0.28,0.19]) translate([0,y,0.83]) nr_box([1.6,1.0,0.65]);
    }
}

module nr_prop_supplyyard(variant = 0)
{
    gk_flatten()
    {
        color([0.22,0.24,0.21]) nr_box([6.6,6.2,0.14]);
        for (x = [-1.7,1.7], y = [-1.7,1.7])
        {
            color([0.28,0.23,0.16]) translate([x,y,0.14]) nr_box([2.5,2.2,0.18]);
            if (variant == 1) for (s = [-0.5,0.5]) color([0.23,0.29,0.24])
                translate([x+s,y,0.32]) cylinder(h = 1.2, r = 0.43, $fn = 8);
            else
            {
                color(nr_scenery_roof(variant)) translate([x,y,0.32]) nr_bevel([2.15,1.8,1.0+(x>0?0.55:0)]);
                for (s = [-0.55,0.55]) color([0.40,0.39,0.30]) translate([x+s,y,1.32+(x>0?0.55:0)]) nr_box([0.14,1.8,0.04]);
            }
        }
    }
}

module nr_prop_containerstack(seed = 0, variant = 0)
{
    gk_flatten()
    {
        for (s = [-1,1]) translate([s*1.4,s*0.55,0])
            nr_prop_container(paint = nr_rnd(seed+s*17,3), variant = variant%3, civilian = true);
        if (variant != 1) translate([1.4,0.55,2.34])
            nr_prop_container(paint = nr_rnd(seed+47,3), variant = variant%3, civilian = true);
    }
}

module nr_prop_gantry(variant = 0)
{
    gk_flatten()
    {
        for (s = [-1,1])
        {
            color(nr_STEEL()) translate([s*3.0,0,0]) nr_box([0.8,7.0,0.25]);
            for (y = [-2.6,2.6]) color([0.36,0.29,0.13]) translate([s*3.0,y,0.25]) nr_box([0.38,0.45,4.35]);
            color([0.36,0.29,0.13]) translate([s*3.0,0,4.6]) nr_box([0.5,6.2,0.55]);
        }
        color([0.36,0.29,0.13]) translate([0,variant==1?1.6:-0.8,4.8]) nr_box([6.5,0.8,0.55]);
        color(nr_STEEL()) translate([0,variant==1?1.6:-0.8,2.8]) nr_box([0.10,0.10,2.0]);
        color(nr_STEEL()) translate([0,variant==1?1.6:-0.8,2.6]) nr_box([2.4,0.4,0.2]);
        if (variant == 2) color(nr_scenery_roof(0)) translate([-0.6,0,0.25]) nr_box([2.0,4.3,1.6]);
    }
}

module nr_prop_pipeyard(variant = 0)
{
    gk_flatten()
    {
        color([0.23,0.25,0.22]) nr_box([6.8,7.2,0.12]);
        for (y = [-2.5,2.5]) color([0.29,0.24,0.17]) translate([0,y,0.12]) nr_box([6.0,0.5,0.3]);
        for (x = [-2,-0.7,0.7,2])
        {
            color(variant == 1 ? [0.30,0.22,0.16] : nr_STEEL()) translate([x,3.2,0.94]) rotate([90,0,0]) cylinder(h = 6.4, r = 0.53, $fn = 8);
            color(nr_DARK()) translate([x,-3.205,0.94]) rotate([90,0,0]) cylinder(h = 0.012, r = 0.39, $fn = 8);
        }
        if (variant == 2) for (x = [-1.35,1.35]) color(nr_STEEL()) translate([x,3.0,1.84]) rotate([90,0,0]) cylinder(h = 6.0, r = 0.53, $fn = 8);
    }
}

module nr_prop_loadingdock(variant = 0)
{
    gk_flatten()
    {
        color(nr_CONCRETE()) nr_box([7.8,5.8,0.65]);
        for (x = [-3.3,3.3]) color(nr_STEEL()) translate([x,1.5,0.65]) nr_box([0.22,0.22,2.7]);
        color(nr_scenery_roof(variant)) translate([0,0.7,3.35]) nr_box([7.7,3.8,0.2]);
        for (x = [-2.6,0,2.6]) color([0.31,0.28,0.20]) translate([x,1,0.65]) nr_box([1.65,1.5,0.9]);
        for (i = [0:2]) color(nr_CONCRETE()) translate([0,-3.1-i*0.35,0]) nr_box([1.8,0.4,0.48-i*0.16]);
    }
}

module nr_prop_buoy(variant = 0)
{
    gk_flatten()
    {
        color(nr_STEEL()) cylinder(h = 0.25, r = 0.9, $fn = 8);
        color(variant == 0 ? [0.28,0.21,0.13] : [0.12,0.29,0.23]) translate([0,0,0.25]) cylinder(h = 0.65, r1 = 0.72, r2 = 0.35, $fn = 8);
        color(nr_STEEL()) translate([0,0,0.9]) cylinder(h = 0.85, r = 0.1, $fn = 6);
        color([0.44,0.43,0.33]) translate([0,0,1.75]) cylinder(h = 0.2, r = 0.22, $fn = 6);
    }
}

module nr_prop_dockfender(variant = 0)
{
    gk_flatten()
    {
        color(nr_STEEL()) nr_box([0.7,3.6,0.18]);
        for (y = [-1.25,1.25])
        {
            color(nr_DARK()) translate([0,y,0.18]) nr_box([0.8,0.6,0.45]);
            color([0.35,0.32,0.21]) translate([0,y,0.63]) cylinder(h = 0.18, r = 0.27, $fn = 8);
        }
        if (variant == 1) color(nr_STEEL()) translate([0,0,0.18]) nr_box([0.8,1.2,0.18]);
    }
}

module nr_nature_grove(seed = 0, variant = 0)
{
    gk_flatten()
    {
        for (i = [0:3])
        {
            x = (i%2==0?-1.8:1.8)+(nr_rnd(seed+i*17,10)-5)*0.13;
            y = (i<2?-2.5:2.5)+(nr_rnd(seed+i*31,10)-5)*0.13;
            if (variant == 0) translate([x,y,0]) scale([1.15,1.15,1.1+nr_rnd(seed+i,5)/10]) nr_nature_tree(seed = seed+i);
            else
            {
                color(nr_SOIL()) translate([x,y,0]) nr_box([0.35,0.35,1.6]);
                color([0.12,0.26+nr_rnd(seed+i,4)*0.015,0.12]) translate([x,y,2.0])
                    scale([1.0,0.9,0.8]) sphere(r = 1.65, $fn = 7);
            }
        }
    }
}

// ================= Solid-colour projectile meshes (no VFX logic) =================
module nr_wpn_bolt(hostile = false)
{
    gk_flatten()
    {
        nr_surface(hostile ? nr_RED() : nr_GLASS()) nr_bevel([0.20,0.95,0.18]);
        nr_surface(nr_WHITE()) translate([0,-0.16,0.18]) nr_box([0.08,0.58,0.025]);
    }
}
module nr_wpn_orb()
{
    gk_flatten()
    {
        nr_surface(nr_RED()) translate([0,0,0.24]) sphere(r = 0.24, $fn = 8);
        nr_surface(nr_YELLOW()) translate([0,-0.08,0.32]) nr_bevel([0.20,0.22,0.12]);
    }
}
module nr_wpn_needle()
{
    gk_flatten() nr_surface(nr_YELLOW()) linear_extrude(height = 0.12)
        polygon([[0,-0.70],[0.16,0.36],[0,0.23],[-0.16,0.36]]);
}
module nr_wpn_missile(paint = 0)
{
    gk_flatten()
    {
        nr_surface(nr_WHITE()) nr_bevel([0.28,1.26,0.28]);
        nr_surface(nr_paint(paint)) translate([0,-0.68,0]) nr_bevel([0.27,0.27,0.26]);
        for (s = [-1,1]) nr_surface(nr_paint(paint)) translate([s*0.23,0.4,0.08]) nr_bevel([0.22,0.43,0.09]);
    }
}
module nr_wpn_bomb()
{
    gk_flatten()
    {
        nr_surface(nr_STEEL()) nr_bevel([0.62,1.1,0.50]);
        nr_surface(nr_YELLOW()) translate([0,-0.38,0.50]) nr_box([0.52,0.16,0.035]);
        nr_surface(nr_RED()) translate([0,0.70,0.12]) nr_box([0.86,0.34,0.22]);
    }
}
module nr_wpn_laser(length = 5)
{
    gk_flatten()
    {
        nr_surface(nr_GLASS()) nr_bevel([0.30,length,0.22]);
        nr_surface(nr_WHITE()) translate([0,0,0.22]) nr_box([0.10,length-0.08,0.035]);
    }
}

// Pickups: top-view geometry symbols, no font or texture dependencies.
module nr_item_power()
{
    gk_flatten()
    {
        nr_surface(nr_RED()) nr_bevel([0.95,0.95,0.24]);
        nr_surface(nr_WHITE()) translate([0,0,0.24]) linear_extrude(height = 0.05)
            polygon([[-0.12,-0.34],[0.24,-0.34],[0.02,-0.03],[0.22,-0.03],[-0.23,0.36],[-0.07,0.04],[-0.24,0.04]]);
    }
}
module nr_item_medal()
{
    gk_flatten()
    {
        nr_surface(nr_YELLOW()) cylinder(h = 0.22, r = 0.52, $fn = 8);
        nr_surface(nr_WHITE()) translate([0,0,0.22]) linear_extrude(height = 0.06)
            polygon([for(i=[0:9]) [(i%2==0?0.34:0.16)*sin(i*36), (i%2==0?0.34:0.16)*cos(i*36)]]);
    }
}
module nr_item_repair()
{
    gk_flatten()
    {
        nr_surface(nr_TEAL()) nr_bevel([0.95,0.95,0.24]);
        nr_surface(nr_WHITE()) translate([0,0,0.24])
        { nr_box([0.60,0.18,0.06]); nr_box([0.18,0.60,0.06]); }
    }
}
module nr_item_bomb()
{
    gk_flatten()
    {
        nr_surface(nr_YELLOW()) nr_bevel([1.1,1.1,0.2]);
        translate([0,0,0.2]) scale([0.65,0.65,0.65]) nr_wpn_bomb();
    }
}
module nr_item_option()
{
    gk_flatten()
    {
        nr_surface(nr_BLUE()) nr_bevel([1.1,1.1,0.2]);
        nr_surface(nr_WHITE()) translate([0,0,0.2]) linear_extrude(height = 0.06)
            polygon([[0,-0.40],[0.40,0.3],[0,0.16],[-0.40,0.3]]);
    }
}

// ================= Mesh-only effect building blocks =================
module nr_fx_exhaust()
{
    gk_flatten()
    {
        nr_surface(nr_YELLOW()) linear_extrude(height = 0.12) polygon([[-0.22,-0.35],[0.22,-0.35],[0,0.9]]);
        nr_surface(nr_WHITE()) translate([0,0,0.12]) linear_extrude(height = 0.04)
            polygon([[-0.11,-0.32],[0.11,-0.32],[0,0.4]]);
    }
}
module nr_fx_flash()
{
    gk_flatten() nr_surface(nr_YELLOW()) linear_extrude(height = 0.15)
        polygon([for(i=[0:11]) [(i%2==0?0.7:0.22)*sin(i*30), (i%2==0?0.7:0.22)*cos(i*30)]]);
}
module nr_fx_smoke(seed = 0)
{
    gk_flatten()
    {
        for (i = [0:3]) color([0.12+i*0.025,0.13+i*0.025,0.13+i*0.025])
            translate([nr_rnd(seed+i*7,10)/20-0.25,0,0.4+i*0.44])
                scale([1,0.85,1]) sphere(r = 0.4+i*0.10, $fn = 6);
    }
}
module nr_fx_blast()
{
    gk_flatten()
    {
        for (i = [0:5]) nr_surface(i%2==0?nr_YELLOW():nr_RED())
            translate([cos(i*60)*0.52,sin(i*60)*0.52,0.45]) sphere(r = 0.45, $fn = 6);
        nr_surface(nr_WHITE()) translate([0,0,0.65]) sphere(r = 0.38, $fn = 6);
    }
}
module nr_fx_crater()
{
    gk_flatten()
    {
        color(nr_DARK()) cylinder(h = 0.018, r = 1.05, $fn = 10);
        for (i = [0:7]) color(nr_SOIL()) translate([cos(i*45)*1.05,sin(i*45)*1.05,0])
            rotate([0,0,i*45]) nr_bevel([0.48,0.38,0.17]);
    }
}
