// Synthetic ground (no real place); gameplay lives in layout.scad.
// 800m square, seed 707. Relief scale 1.00.
use <../../../lib/gk_camera.scad>
use <../../../lib/kit_abandoned.scad>
use <../../../lib/nextdayz_pcg.scad>
use <../../../lib/nextdayz_coldwar.scad>
$fn=12;
include <layout.scad>
GEO_TERR = ["gkterr1", [800, 800], [256, 256], 707,
    [10, 0, 0, 160], undef, "continental",
    [["hmap", "assets/scad/proc/nextdayz/training_ground/terrain.hmap", "set", 1, 0]]];

TERR=["gkterr1",GEO_TERR[1],GEO_TERR[2],GEO_TERR[3],GEO_TERR[4],GEO_TERR[5],GEO_TERR[6],concat(GEO_TERR[7],PCG_PATHS,nd_pcg_terrain(PCG_SITES,PCG_SPAWNS,PCG_EXTRACTIONS,707,PCG_LANDCOVER))];
gk_terrain(TERR,detail=true);
function nd_ground(x,y)=gk_terrain_height(TERR,x,y);
for(i=[0:len(PCG_SITES)-1]) nd_pcg_site(TERR,PCG_SITES[i],"training",707+i*131);
for(p=PCG_SPAWNS) translate([p[0],p[1],nd_ground(p[0],p[1])+0.15]) nd_spawn_player_safe();
translate([200,-150,nd_ground(200,-150)]) { gk_flatten() az_prop_helipad(seed=707); translate([0,0,0.15]) nd_extraction(id="training_ground_pad_0",name="Rescue zone ALPHA"); }
translate([-300,250,nd_ground(-300,250)]) { gk_flatten() az_prop_helipad(seed=708); translate([0,0,0.15]) nd_extraction(id="training_ground_pad_1",name="Rescue zone BRAVO"); }
nd_pcg_vegetation(TERR,PCG_VEGETATION,PCG_SITES,PCG_SPAWNS,PCG_EXTRACTIONS,PCG_CLEAR,707,"training");
nd_pcg_landcover(TERR,PCG_LANDCOVER,PCG_SITES,PCG_SPAWNS,PCG_EXTRACTIONS,PCG_CLEAR,707,"training");
nd_pcg_alleys(TERR,PCG_ALLEYS,PCG_SITES,PCG_SPAWNS,PCG_EXTRACTIONS,PCG_CLEAR,707,"training");
gk_camera_lookat([-500,-580,416],[0,0,25],"overview",48);
gk_camera_lookat([-150,-230,135],[20,85,25],"tactical_core",55);
