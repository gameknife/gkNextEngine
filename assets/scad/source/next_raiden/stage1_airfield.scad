// ============================================================================
// stage1_airfield.scad - NextRaiden 关卡 1「机场突入」。
//
// 关卡沿 +Y 生长；y=0 是开场，玩家出生在 y≈-20 附近朝 +Y 飞。滚动 12 m/s，
// 所以 (y-22)/12 就是它出现的关卡秒数——摆地面目标时按这个换算对波次。
//
// 这里放两类东西：
//   * 景观带（nr_band_*），一行一个 Node，纯背景；
//   * 地面目标（nr_veh_tank / nr_prop_turret / nr_veh_launcher / nr_bldg_*），
//     运行时由 StageIndex 按模块名索引，能开火、能被打、按 chunk 分级掉块。
// 建筑里只有 bunker 和 fueldepot 是目标，hangar / controltower / radar 是布景。
//
// 空中波次不在这里：那是时间线，写在 assets/configs/nextraiden/stage1_waves.json。
// ============================================================================

use <../../lib/kit_raiden.scad>
use <../../lib/raiden_parts.scad>
use <../../lib/gk_camera.scad>
use <nr_stage_common.scad>

$fn = 8;

// ---- 景观带 ----------------------------------------------------------------
// 覆盖到 endZ 再往前一个可见范围（约 44 m）为止，否则关卡末尾会露出背景色。
// 现在覆盖到 SCAD y=2016 = 引擎 z=-2016；stages.json 的 endZ 是 -1900。
// 行末的秒数按滚动 12 m/s 算：地面目标摆在哪一秒，直接看这个。
nr_band_grass(y0 = -80, rows = 5, seed = 3, props = 1);   // 出生点身后，防止开场露白
nr_band_runway(y0 = 0, rows = 13, seed = 11);   //    -2s -    16s
nr_band_grass(y0 = 208, rows = 6, seed = 53, props = 1);   //    16s -    24s
nr_band_road(y0 = 304, rows = 8, seed = 95);   //    24s -    34s
nr_band_farm(y0 = 432, rows = 6, seed = 137);   //    34s -    42s
nr_band_runway(y0 = 528, rows = 10, seed = 179);   //    42s -    56s
nr_band_grass(y0 = 688, rows = 8, seed = 221, props = 1);   //    56s -    66s
nr_band_road(y0 = 816, rows = 8, seed = 263);   //    66s -    77s
nr_band_runway(y0 = 944, rows = 16, seed = 305);   //    77s -    98s
nr_band_grass(y0 = 1200, rows = 6, seed = 347, props = 1);   //    98s -   106s
nr_band_runway(y0 = 1296, rows = 13, seed = 389);   //   106s -   124s
nr_band_farm(y0 = 1504, rows = 8, seed = 431);   //   124s -   134s
nr_band_runway(y0 = 1632, rows = 16, seed = 473);   //   134s -   156s
nr_band_grass(y0 = 1888, rows = 8, seed = 515, props = 1);   //   156s -   166s

// ---- 取景校准物 ------------------------------------------------------------
// 塔台在 +X、机库在 -X：截图里塔台必须出现在**右**边。SCAD +X = 屏幕右方是整套
// 朝向契约的地基（设计 5.1），所以关卡里留一对不对称的参照物，改相机时一眼能看出翻没翻。
translate([11, 67, 0.02]) nr_bldg_controltower();
translate([-11, 76, 0.02]) nr_bldg_hangar();

// ---- 地面目标 --------------------------------------------------------------
// 0:17 起：坦克纵队沿跑道边缘，教玩家"地面也要打"
translate([-7, 227, 0.02]) nr_veh_tank();
translate([7, 256, 0.02]) nr_veh_tank();
translate([-6, 284, 0.02]) nr_veh_tank();
for (y = [212, 244, 276]) translate([-11, y, 0.02]) nr_prop_barrier();

// 0:25 公路段：炮塔夹道
translate([-9, 316, 0.02]) nr_prop_turret();
translate([9, 345, 0.02]) nr_prop_turret();
translate([0, 377, 0.02]) nr_veh_tank();

// 0:35 农地：第一门发射车
translate([8, 444, 0.02]) nr_veh_launcher();
translate([-9, 467, 0.02]) nr_veh_tank();
for (s = [-1, 1], i = [0:2]) translate([s * (30 + i), 424 + i * 22, 0.02]) nr_nature_tree(seed = i * 7 + 3);

// 0:44 侧跑道：掩体群 + 雷达
translate([-10, 547, 0.02]) nr_bldg_bunker();
translate([10, 582, 0.02]) nr_bldg_bunker();
translate([13, 614, 0.02]) nr_prop_radar();
translate([-12, 633, 0.02]) nr_prop_turret();

// 1:07 补给公路：油库——打爆会连锁
translate([-10, 822, 0.02]) nr_bldg_fueldepot();
translate([-10, 844, 0.02]) nr_bldg_fueldepot();
translate([9, 864, 0.02]) nr_veh_launcher();
translate([0, 886, 0.02]) nr_veh_tank();

// 1:17 机场核心：机库、工厂、集装箱与密集防空
translate([-12, 950, 0.02]) nr_bldg_hangar(paint = 1);
translate([12, 976, 0.02]) nr_bldg_factory();
translate([-9, 1001, 0.02]) nr_prop_turret();
translate([9, 1027, 0.02]) nr_prop_turret();
translate([-11, 1052, 0.02]) nr_veh_launcher();
for (i = [0:2]) translate([13, 1062 + i * 10, 0.02]) nr_prop_container(paint = i);
translate([0, 1091, 0.02]) nr_veh_tank();
translate([-13, 1113, 0.02]) nr_bldg_bunker();
translate([13, 1126, 0.02]) nr_bldg_controltower();
translate([-8, 1148, 0.02]) nr_prop_turret();

// 1:48 主跑道：发射车阵地
translate([9, 1315, 0.02]) nr_veh_launcher();
translate([-9, 1340, 0.02]) nr_veh_launcher();
translate([0, 1366, 0.02]) nr_prop_turret();
translate([-11, 1392, 0.02]) nr_veh_tank();
translate([11, 1417, 0.02]) nr_veh_tank();
for (y = [1300, 1348, 1396, 1444]) translate([-14, y, 0.02]) nr_prop_barrier();

// 2:00 油库群：最后的送分点，也是最后一次补给机会
translate([-10, 1459, 0.02]) nr_bldg_fueldepot();
translate([-10, 1481, 0.02]) nr_bldg_fueldepot();
translate([10, 1475, 0.02]) nr_bldg_fueldepot();
translate([0, 1510, 0.02]) nr_prop_turret();

// 2:15 BOSS 空域前的最后阻拦
translate([-12, 1638, 0.02]) nr_prop_turret();
translate([12, 1657, 0.02]) nr_prop_turret();
translate([0, 1683, 0.02]) nr_veh_launcher();
translate([-13, 1708, 0.02]) nr_bldg_bunker();
translate([13, 1728, 0.02]) nr_bldg_bunker();

// ---- Non-combat districts --------------------------------------------------
// Keep the centre and every existing target clear. Asymmetric silhouettes live
// inside the visible shoulders, not only behind the HUD at |x| > 24.
// Arrival: maintenance apron, helicopter pad, utilities and crew quarters.
translate([-18, 2, 0.02]) nr_bldg_substation();
translate([18, 16, 0.02]) nr_bldg_workshop();
translate([-17, 30, 0.02]) nr_bldg_shelter();
translate([18, 43, 0.02]) nr_prop_helipad();
translate([-19, 58, 0.02]) rotate([0,0,25]) nr_prop_servicecart();
translate([18, 97, 0.02]) nr_bldg_pumpstation();
translate([-18, 113, 0.02]) nr_bldg_shelter(variant = 1);
translate([17, 137, 0.02]) nr_bldg_barracks(variant = 1);
translate([-18, 152, 0.02]) rotate([0,0,90]) nr_prop_servicecart(variant = 2);
translate([-17, 178, 0.02]) nr_prop_supplyyard();
translate([18, 190, 0.02]) nr_bldg_watertower();

// Perimeter villages and agricultural service buildings.
translate([17, 215, 0.02]) nr_bldg_greenhouse();
translate([-18, 251, 0.02]) nr_bldg_barracks(variant = 2);
translate([18, 283, 0.02]) nr_bldg_watertower(variant = 1);
translate([-18, 330, 0.02]) nr_bldg_warehouse(variant = 1);
translate([18, 367, 0.02]) nr_bldg_pumpstation(variant = 1);
translate([-18, 401, 0.02]) nr_bldg_silos();
translate([-18, 443, 0.02]) nr_bldg_silos(variant = 1);
translate([18, 484, 0.02]) nr_bldg_workshop(variant = 2);
translate([-22, 490, 0.02]) nr_nature_grove(seed = 59, variant = 1);

// Dispersal strip: closed shelter, radar utilities and unarmed support vehicles.
translate([18, 540, 0.02]) nr_prop_helipad(variant = 1);
translate([-19, 567, 0.02]) nr_bldg_shelter(variant = 2);
translate([-18, 601, 0.02]) nr_bldg_substation(variant = 1);
translate([18, 650, 0.02]) nr_prop_supplyyard(variant = 1);
translate([-17, 669, 0.02]) nr_prop_servicecart(variant = 1);
translate([-16, 710, 0.02]) nr_bldg_greenhouse(variant = 1);
translate([17, 738, 0.02]) nr_bldg_barracks();
translate([-17, 772, 0.02]) nr_nature_grove(seed = 83, variant = 1);
translate([18, 789, 0.02]) nr_bldg_watertower(variant = 2);

// Supply road and the main maintenance district.
translate([18, 827, 0.02]) nr_bldg_pumpstation(variant = 2);
translate([-18, 869, 0.02]) rotate([0,0,180]) nr_prop_servicecart();
translate([18, 906, 0.02]) nr_bldg_warehouse();
translate([-18, 919, 0.02]) nr_prop_loadingdock();
translate([18, 947, 0.02]) nr_prop_loadingdock(variant = 1);
translate([-18, 979, 0.02]) nr_prop_servicecart(variant = 2);
translate([18, 1007, 0.02]) nr_bldg_substation(variant = 1);
translate([-19, 1030, 0.02]) nr_bldg_shelter(variant = 1);
translate([-18, 1073, 0.02]) nr_prop_containerstack(seed = 109, variant = 1);
translate([18, 1102, 0.02]) nr_bldg_watertower();
translate([-18, 1153, 0.02]) nr_prop_supplyyard(variant = 2);
translate([18, 1177, 0.02]) nr_bldg_workshop(variant = 1);

// Green intervals separate the large paved districts at the pulled-back view.
translate([-17, 1214, 0.02]) nr_bldg_greenhouse();
translate([18, 1243, 0.02]) nr_nature_grove(seed = 131, variant = 1);
translate([-18, 1271, 0.02]) nr_bldg_silos(variant = 2);
translate([-18, 1305, 0.02]) nr_bldg_shelter();
translate([18, 1342, 0.02]) nr_bldg_pumpstation();
translate([-18, 1364, 0.02]) nr_bldg_workshop(variant = 2);
translate([18, 1391, 0.02]) nr_prop_helipad(variant = 1);
translate([-18, 1424, 0.02]) nr_prop_servicecart(variant = 1);
translate([18, 1451, 0.02]) nr_bldg_substation();
translate([-18, 1526, 0.02]) nr_bldg_silos(variant = 1);
translate([18, 1564, 0.02]) nr_bldg_warehouse(variant = 1);
translate([-22, 1582, 0.02]) nr_nature_grove(seed = 149);
translate([18, 1603, 0.02]) nr_bldg_workshop();

// Final airbase: alternate low yards and taller landmarks, never mirrored pairs.
translate([-19, 1660, 0.02]) nr_bldg_workshop(variant = 1);
translate([18, 1692, 0.02]) nr_bldg_substation(variant = 1);
translate([-18, 1748, 0.02]) nr_prop_pipeyard();
translate([18, 1788, 0.02]) nr_prop_helipad();
translate([-18, 1822, 0.02]) nr_bldg_barracks(variant = 2);
translate([18, 1855, 0.02]) nr_bldg_watertower(variant = 1);
translate([-18, 1910, 0.02]) nr_nature_grove(seed = 163, variant = 1);
translate([18, 1940, 0.02]) nr_bldg_greenhouse(variant = 1);
translate([-18, 1976, 0.02]) nr_bldg_silos();

// ---- 料架 ------------------------------------------------------------------
nr_rack();

// ---- 取景 ------------------------------------------------------------------
// 与游戏内相机同一套参数：目标在地面 (0, y, 0)，眼位 = 目标 + SCAD(0, -20.1, 75)，
// 即引擎空间的 (0, +75, +20.1)。俯角 15 度，竖直 FOV 50 度。
// 相机高度是整套尺度的旋钮，见 gameplay.json 的 screen._comment。
gk_camera_lookat([0, 19.9, 75], [0, 40, 0], name = "01_Gameplay_framing", fov = 50);
gk_camera_lookat([0, 979.9, 75], [0, 1000, 0], name = "02_Airfield_core", fov = 50);
gk_camera_lookat([0, 1479.9, 75], [0, 1500, 0], name = "03_Fuel_farm", fov = 50);
