// gk_material_demo.scad —— 触感材质对照台（NextDayz 视觉升级 V1-2）
// 每组左边是旧写法，右边是新参数，同色同光对比：
//   前排  皮肤：Lambertian | subsurface 0.55（暖色明暗交界）| ior 1.26 + subsurface 微光泽
//   中排  布料：Lambertian | sheen 0.45（轮廓绒光）；陶土：Mixture 1.45 | ior 1.2 温润微光泽
//   后排  树冠：普通 color | gk_foliage 逆光透射（+ gk_puff_normals）
// gnb shot --scene assets/scad/source/others/gk_material_demo.scad
use <../../lib/gk_camera.scad>

$fn = 24;

SKIN = [0.80, 0.58, 0.46];
CLOTH = [0.22, 0.28, 0.36];
CLAY = [0.62, 0.40, 0.28];
LEAF = [0.24, 0.38, 0.14];

module head() { sphere(r = 0.42); translate([0, -0.36, 0.02]) sphere(r = 0.09); }
module coat() gk_round_box([0.8, 0.5, 1.0], r = 0.18, segs = 4);
module crown() gk_puff_normals(0.7, 0.3) gk_crease(60)
    for (i = [0 : 5]) let(a = i * 60, d = i == 0 ? 0 : 0.55)
        translate([d * cos(a), d * sin(a), 0.25 * (i % 3)]) sphere(r = 0.7, $fn = 12);

color([0.46, 0.46, 0.44]) translate([2.5, 3.5, -0.05]) cube([12, 11, 0.1], center = true);

// ---- 前排：皮肤 ----
translate([0, 0, 0.45]) color(SKIN) head();
translate([1.4, 0, 0.45]) gk_material(SKIN, roughness = 1, subsurface = 0.55) head();
translate([2.8, 0, 0.45]) gk_material(SKIN, roughness = 0.78, ior = 1.26, subsurface = 0.55) head();

// ---- 中排：布料与陶土 ----
translate([0, 2.4, 0.5]) color(CLOTH) coat();
translate([1.4, 2.4, 0.5]) gk_material(CLOTH, sheen = 0.45) coat();
translate([3.4, 2.4, 0.45]) gk_material(CLAY, roughness = 0.6) sphere(r = 0.45);
translate([4.8, 2.4, 0.45]) gk_material(CLAY, roughness = 0.6, ior = 1.2) sphere(r = 0.45);

// ---- 后排：树冠 ----
translate([0.5, 6.0, 1.0]) color(LEAF) crown();
translate([3.5, 6.0, 1.0]) gk_foliage(LEAF, transmission = 0.65) crown();

gk_camera_lookat([2.2, -4.2, 2.4], [2.2, 2.6, 0.7], "overview", 52);
gk_camera_lookat([1.4, -2.0, 0.8], [1.4, 0, 0.45], "skin", 40);
gk_camera_lookat([2.0, 2.4 - 3.0, 1.2], [2.4, 2.4, 0.5], "cloth", 50);
gk_camera_lookat([2.0, 11.5, 2.0], [2.0, 6.0, 1.3], "backlit", 50);
