// Geographic reference only; gameplay lives in layout.scad.
// 50.18600, 12.75400; 1000m square. DEM relief scale 0.40.
// © OpenStreetMap contributors (ODbL); SRTM / NASA-USGS.
use <../../../lib/gk_camera.scad>
use <../../../lib/kit_abandoned.scad>
use <../../../lib/nextdayz_pcg.scad>
use <../../../lib/nextdayz_coldwar.scad>
$fn=12;
include <layout.scad>
GEO_TERR = ["gkterr1", [1000, 1000], [256, 256], 817,
    [10, 0, 0, 160], 10, "continental",
    [["hmap", "assets/scad/proc/nextdayz/cz_loket_riverbend/terrain.hmap", "set", 1, 0],
     ["road", [[378.04, -143.23], [392.2, -96.28]], 6.8],
     ["road", [[378.04, -143.23], [321.96, -146.47], [238.01, -179.63], [215.59, -200.23], [210.19, -245.5]], 6.8],
     ["road", [[143.82, 219.83], [137.52, 267.31], [111.89, 326.21], [65.25, 376.23], [23.04, 402.9]], 6.8],
     ["road", [[299.39, 333.74], [245.14, 299.83], [201.93, 244.41], [151.7, 202.49]], 11],
     ["road", [[321.77, 341.22], [438.19, 418.12], [491.3, 443.11], [500, 458.23]], 3.4],
     ["road", [[399.1, 287.3], [441.35, 311.1], [500, 322.44]], 7],
     ["road", [[-91.81, 116.08], [-92.41, 117.35]], 7],
     ["road", [[-92.41, 117.35], [-140.99, 219.19], [-152.36, 244.19]], 7],
     ["road", [[103.51, -101.43], [140.67, -82.35], [168.2, -55.42], [193.36, 46.25], [192.48, 82.5], [181.43, 125.89], [186.15, 159.08], [193.14, 177.86], [233.57, 229.05]], 7],
     ["road", [[-56.99, 36.6], [-40.75, 69.62], [29.74, 61.46]], 7],
     ["road", [[-178.53, 26.34], [-208.46, 12.08], [-220.15, -3.98], [-215.54, -36.13], [-194.91, -65.74], [-174.38, -84.84], [-75.33, -138.23], [-34.83, -124.17]], 7],
     ["road", [[-178.53, 26.34], [-157.21, 20.17], [-133.95, -18.76], [-75.7, -66.68]], 7],
     ["road", [[-75.7, -66.68], [-57.01, -43.18], [-56.01, -19.96], [-34.54, 15.27]], 7],
     ["road", [[-204.91, 83.27], [-202.61, 76.33], [-190.09, 53.7], [-178.53, 26.34]], 7],
     ["road", [[103.51, -101.43], [120.22, -133.99], [127.49, -145.49]], 7],
     ["road", [[59.58, 1.1], [59.6, 8.01], [58.89, 41.91]], 7],
     ["road", [[397.3, 293.38], [380.64, 310.41], [331.7, 339.36]], 6.8],
     ["road", [[331.7, 339.36], [325.43, 340.78], [321.77, 341.22]], 6.8],
     ["road", [[392.2, -96.28], [396.5, -81.93]], 6.8],
     ["road", [[396.5, -81.93], [400.4, -65.66], [395.77, -42.55], [340.82, -0.62], [330.29, 39.73], [350.79, 169.16], [399.1, 287.3]], 6.8],
     ["road", [[500, -193.27], [479.33, -186.66], [439.45, -172.25], [411.86, -161.29], [399.89, -154.59], [389.49, -148.83], [382.53, -145.13]], 6.8],
     ["road", [[101.52, -302.7], [-1.36, -283.87], [-101.01, -249.51], [-138.87, -244.37], [-205.86, -206.76], [-226.76, -160.01], [-265.43, -133.99], [-289.32, -108.12], [-310.89, -68.85], [-328.66, -13.64], [-307.56, 91.08], [-234.7, 227.11], [-228.63, 252.15], [-232.1, 272.56], [-256.94, 299.66], [-328.7, 323.98], [-411.51, 343.05], [-454.89, 336.38], [-500, 288.74]], 6.8],
     ["road", [[382.53, -145.13], [378.04, -143.23]], 6.8],
     ["road", [[23.04, 402.9], [-28.3, 451.54], [-100.49, 473.34]], 11],
     ["road", [[399.1, 287.3], [397.3, 293.38]], 6.8],
     ["road", [[321.77, 341.22], [317.33, 340.48], [309.44, 337.84], [299.39, 333.74]], 6.8],
     ["road", [[156.34, 224.1], [122.83, 372.16], [58.11, 467.49], [59.64, 500]], 9],
     ["road", [[205.92, -252.59], [139.25, -290.31], [101.52, -302.7]], 6.8],
     ["road", [[210.19, -245.5], [205.92, -252.59]], 6.8],
     ["road", [[151.7, 202.49], [144.65, 207.35], [143.82, 219.83]], 6.8],
     ["road", [[159.48, 206.98], [156.34, 224.1]], 9],
     ["road", [[-100.49, 473.34], [-104.06, 475.12], [-148.29, 500]], 11]]];

TERR=["gkterr1",GEO_TERR[1],GEO_TERR[2],GEO_TERR[3],GEO_TERR[4],GEO_TERR[5],GEO_TERR[6],concat(GEO_TERR[7],PCG_PATHS,nd_pcg_terrain(PCG_SITES,PCG_SPAWNS,PCG_EXTRACTIONS,817,PCG_LANDCOVER))];
gk_terrain(TERR,water=[[0.07,0.1,0.09],0.04,0]);
function nd_ground(x,y)=gk_terrain_height(TERR,x,y);
let(za=nd_ground(-92.41,117.35)+0.06,zb=nd_ground(-152.36,244.19)+0.06) translate([-122.39,180.77,(za+zb)/2]) rotate([0,0,115.3]) gk_flatten() az_prop_bridge(seed=818,L=143.3,W=7,rise=zb-za,pierDepth=max(10,(za+zb)/2-10+8));
let(za=nd_ground(397.3,293.38)+0.06,zb=nd_ground(331.7,339.36)+0.06) translate([364.5,316.37,(za+zb)/2]) rotate([0,0,144.97]) gk_flatten() az_prop_bridge(seed=819,L=83.1,W=6.8,rise=zb-za,pierDepth=max(10,(za+zb)/2-10+8));
let(za=nd_ground(392.2,-96.28)+0.06,zb=nd_ground(396.5,-81.93)+0.06) translate([394.35,-89.11,(za+zb)/2]) rotate([0,0,73.35]) gk_flatten() az_prop_bridge(seed=820,L=17.98,W=6.8,rise=zb-za,pierDepth=max(10,(za+zb)/2-10+8));
let(za=nd_ground(244.84,248.55)+0.06,zb=nd_ground(263.99,237.95)+0.06) translate([254.42,243.25,(za+zb)/2]) rotate([0,0,-28.97]) gk_flatten() az_prop_bridge(seed=821,L=24.89,W=4.5,rise=zb-za,pierDepth=max(10,(za+zb)/2-10+8));
let(za=nd_ground(266,168.94)+0.06,zb=nd_ground(253.55,155.17)+0.06) translate([259.78,162.05,(za+zb)/2]) rotate([0,0,-132.13]) gk_flatten() az_prop_bridge(seed=822,L=21.56,W=3,rise=zb-za,pierDepth=max(10,(za+zb)/2-10+8));
let(za=nd_ground(151.26,-190.97)+0.06,zb=nd_ground(127.49,-145.49)+0.06) translate([139.37,-168.23,(za+zb)/2]) rotate([0,0,117.59]) gk_flatten() az_prop_bridge(seed=823,L=54.31,W=3,rise=zb-za,pierDepth=max(10,(za+zb)/2-10+8));
let(za=nd_ground(-138.5,222.65)+0.06,zb=nd_ground(-149.37,245.74)+0.06) translate([-143.93,234.19,(za+zb)/2]) rotate([0,0,115.23]) gk_flatten() az_prop_bridge(seed=824,L=28.52,W=3,rise=zb-za,pierDepth=max(10,(za+zb)/2-10+8));
for(i=[0:len(PCG_SITES)-1]) nd_pcg_site(TERR,PCG_SITES[i],"bohemia",817+i*131);
for(p=PCG_SPAWNS) translate([p[0],p[1],nd_ground(p[0],p[1])+0.15]) nd_spawn_player_safe();
translate([-265,360,nd_ground(-265,360)]) { gk_flatten() az_prop_helipad(seed=817); translate([0,0,0.15]) nd_extraction(id="cz_loket_riverbend_pad_0",name="Rescue zone ALPHA"); }
translate([240,-300,nd_ground(240,-300)]) { gk_flatten() az_prop_helipad(seed=818); translate([0,0,0.15]) nd_extraction(id="cz_loket_riverbend_pad_1",name="Rescue zone BRAVO"); }
nd_pcg_vegetation(TERR,PCG_VEGETATION,PCG_SITES,PCG_SPAWNS,PCG_EXTRACTIONS,PCG_CLEAR,817,"bohemia");
nd_pcg_landcover(TERR,PCG_LANDCOVER,PCG_SITES,PCG_SPAWNS,PCG_EXTRACTIONS,PCG_CLEAR,817,"bohemia");
nd_pcg_alleys(TERR,PCG_ALLEYS,PCG_SITES,PCG_SPAWNS,PCG_EXTRACTIONS,PCG_CLEAR,817,"bohemia");
gk_camera_lookat([-625,-725,520],[0,0,25],"overview",48);
gk_camera_lookat([-150,-230,135],[20,85,25],"tactical_core",55);
