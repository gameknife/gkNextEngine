// Geographic reference only; gameplay lives in layout.scad.
// 43.20250, 141.00050; 1000m square. DEM relief scale 0.65.
// © OpenStreetMap contributors (ODbL); SRTM / NASA-USGS.
use <lib/gk_camera.scad>
use <lib/kit_abandoned.scad>
use <lib/nextdayz_pcg.scad>
use <lib/nextdayz_coldwar.scad>
$fn=12;
include <layout.scad>
GEO_TERR = ["gkterr1", [1000, 1000], [256, 256], 1947,
    [10, 0, 0, 160], 10, "temperate",
    [["hmap", "assets/scad/proc/nextdayz/jp_otaru_north_canal/published/v4/terrain.hmap", "set", 1, 0],
     ["road", [[-500, 24.01], [-436.95, 32.9], [-340.74, 46.75], [-253.29, 59.89]], 10.2],
     ["road", [[290.65, -500], [188.35, -357.96], [84.96, -130.64]], 6.8],
     ["road", [[-383.57, -152.76], [-378, -148.91], [-289.18, -122.18], [-225.63, -101.05], [-208.94, -95.73], [-208, -95.43], [-157.68, -78.66], [-143.36, -74.65]], 9],
     ["road", [[28.23, -500], [20.88, -487.14], [-44.81, -336.85], [-48.43, -327.7], [-65.42, -287.67], [-106.47, -199.64], [-114.57, -177.9], [-153.9, -91.14], [-157.68, -78.66]], 6.8],
     ["road", [[50.93, -283.72], [-41.29, -10.17], [-59.43, 27.72], [-85.05, 49.84], [-121.69, 61.8], [-251.46, 47.94]], 10.2],
     ["road", [[-253.29, 59.89], [-123.04, 74.51], [-62.91, 49.08], [-42.12, 23.02], [-27.57, -14.68], [63.2, -285.05]], 10.2],
     ["road", [[-46.24, -44.88], [-32.2, -39.51]], 6.8],
     ["road", [[63.2, -285.05], [71.58, -281.26], [75.84, -279.4]], 6.8],
     ["road", [[75.84, -279.4], [79.09, -278], [92.7, -272.25], [93.48, -271.91]], 6.8],
     ["road", [[93.48, -271.91], [128.84, -257.12], [140.44, -252.27]], 6.8],
     ["road", [[271.91, -480.73], [248.12, -500]], 6.8],
     ["road", [[-9.98, -33.88], [17.75, -26.82]], 6.8],
     ["road", [[17.75, -26.82], [44.72, -19.45]], 6.8],
     ["road", [[84.96, -130.64], [67.89, -90.15]], 6.8],
     ["road", [[67.89, -90.15], [38.51, 1.64], [34.12, 33.18], [37.78, 81.49]], 6.8],
     ["road", [[63.2, -285.05], [55.27, -294.34]], 6.8],
     ["road", [[-481.63, -498.44], [-128.51, -360.96], [50.93, -283.72]], 6.8],
     ["road", [[-106.47, -199.64], [-179.18, -227.71], [-182.46, -229], [-193.53, -233.34], [-246.3, -254.37], [-249.64, -255.62], [-254.54, -257.56], [-337.22, -287.77], [-343.07, -290.01]], 9],
     ["road", [[-46.24, -44.88], [-80.59, -54.69], [-143.36, -74.65]], 6.8],
     ["road", [[-32.2, -39.51], [-21.68, -36.88], [-15.06, -35.18], [-9.98, -33.88]], 6.8],
     ["road", [[-157.68, -78.66], [-173.55, 2.23], [-171.28, 215.77], [-154.95, 285], [-167.17, 353.75], [-159.92, 500]], 6.8],
     ["road", [[63.2, -285.05], [156.38, -462.73], [182.21, -500]], 10.2],
     ["road", [[166.97, -500], [124.03, -433.03], [55.27, -294.34]], 10.2],
     ["road", [[54, 157.14], [76.74, 298.88], [90.69, 468.48], [87.7, 500]], 6.8],
     ["road", [[-251.46, 47.94], [-337.22, 34.51], [-433.91, 21.74], [-500, 11.52]], 10.2],
     ["road", [[50.93, -283.72], [63.2, -285.05]], 6.8],
     ["road", [[55.27, -294.34], [50.93, -283.72]], 10.2],
     ["road", [[-246.48, 500], [-260.76, 314.71], [-251.46, 47.94]], 13.6]]];

TERR=["gkterr1",GEO_TERR[1],GEO_TERR[2],GEO_TERR[3],GEO_TERR[4],GEO_TERR[5],GEO_TERR[6],concat(GEO_TERR[7],PCG_PATHS,nd_pcg_terrain(PCG_SITES,PCG_SPAWNS,PCG_EXTRACTIONS,1947,PCG_LANDCOVER))];
gk_terrain(TERR);
function nd_ground(x,y)=gk_terrain_height(TERR,x,y);
let(za=nd_ground(-84.6,280.96)+0.06,zb=nd_ground(-45.9,280.22)+0.06) translate([-65.25,280.59,(za+zb)/2]) rotate([0,0,-1.1]) gk_flatten() az_prop_bridge(seed=1948,L=41.7,W=8,rise=zb-za,pierDepth=max(10,(za+zb)/2-10+8));
let(za=nd_ground(75.84,-279.4)+0.06,zb=nd_ground(95.32,-271.13)+0.06) translate([85.58,-275.26,(za+zb)/2]) rotate([0,0,23]) gk_flatten() az_prop_bridge(seed=1949,L=24.16,W=6.8,rise=zb-za,pierDepth=max(10,(za+zb)/2-10+8));
let(za=nd_ground(-9.98,-33.88)+0.06,zb=nd_ground(17.75,-26.82)+0.06) translate([3.88,-30.35,(za+zb)/2]) rotate([0,0,14.29]) gk_flatten() az_prop_bridge(seed=1950,L=31.62,W=6.8,rise=zb-za,pierDepth=max(10,(za+zb)/2-10+8));
let(za=nd_ground(84.96,-130.64)+0.06,zb=nd_ground(67.89,-90.15)+0.06) translate([76.42,-110.4,(za+zb)/2]) rotate([0,0,112.87]) gk_flatten() az_prop_bridge(seed=1951,L=46.94,W=6.8,rise=zb-za,pierDepth=max(10,(za+zb)/2-10+8));
let(za=nd_ground(37.78,81.49)+0.06,zb=nd_ground(54,157.14)+0.06) translate([45.89,119.31,(za+zb)/2]) rotate([0,0,77.9]) gk_flatten() az_prop_bridge(seed=1952,L=80.37,W=6.8,rise=zb-za,pierDepth=max(10,(za+zb)/2-10+8));
for(i=[0:len(PCG_SITES)-1]) nd_pcg_site(TERR,PCG_SITES[i],"hokkaido",1947+i*131);
for(p=PCG_SPAWNS) translate([p[0],p[1],nd_ground(p[0],p[1])+0.15]) nd_spawn_player_safe();
translate([-460,250,nd_ground(-460,250)]) { gk_flatten() az_prop_helipad(seed=1947); translate([0,0,0.15]) nd_extraction(id="jp_otaru_north_canal_pad_0",name="Rescue zone ALPHA"); }
translate([430,-350,nd_ground(430,-350)]) { gk_flatten() az_prop_helipad(seed=1948); translate([0,0,0.15]) nd_extraction(id="jp_otaru_north_canal_pad_1",name="Rescue zone BRAVO"); }
nd_pcg_vegetation(TERR,PCG_VEGETATION,PCG_SITES,PCG_SPAWNS,PCG_EXTRACTIONS,PCG_CLEAR,1947,"hokkaido");
nd_pcg_landcover(TERR,PCG_LANDCOVER,PCG_SITES,PCG_SPAWNS,PCG_EXTRACTIONS,PCG_CLEAR,1947,"hokkaido");
nd_pcg_alleys(TERR,PCG_ALLEYS,PCG_SITES,PCG_SPAWNS,PCG_EXTRACTIONS,PCG_CLEAR,1947,"hokkaido");
gk_camera_lookat([-625,-725,520],[0,0,25],"overview",48);
gk_camera_lookat([-150,-230,135],[20,85,25],"tactical_core",55);
