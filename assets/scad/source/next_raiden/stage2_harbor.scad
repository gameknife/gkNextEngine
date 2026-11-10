// ============================================================================
// stage2_harbor.scad - NextRaiden 关卡 2「军港破袭」。
//
// 和关卡 1 同一套契约：关卡沿 +Y 生长，滚动 12 m/s，(y-22)/12 就是它出现的关卡秒数。
// 滚动速度与关卡 1 相同是有意的——它同时是地面目标的节奏和 y 与秒的换算，两关用两套
// 心算只会把摆件摆错位。第二关的"更难"来自密度、弹速和 BOSS，不来自表速。
//
// 比关卡 1 长约 18%：景观覆盖到 y=2256（3:06），关底 BOSS 在 3:00 触发。
//
// 取景校准：第一条景观带是 nr_band_quay(side = -1)，码头在 SCAD -X。截图里码头必须出现在
// **屏幕左侧**、水面在右。这比关卡 1 的塔台/机库还好认——整整一条岸线翻没翻，一眼就知道。
//
// 地面目标的摆位规则（由设计 5.2 的播放列宽度决定，不是随手定的）：
//   * 岸上目标 |x| = 10~18，再往外就被 HUD 立柱盖住，看不见也打不着；
//   * 船只放在水面那一侧，|x| <= 16；
//   * nr_band_channel 的两岸在 |x| >= 24，整个在立柱后面，所以水道段只放船。
//
// 空中波次在 assets/configs/nextraiden/stage2_waves.json。
// ============================================================================

use <../../lib/kit_raiden.scad>
use <../../lib/raiden_parts.scad>
use <../../lib/gk_camera.scad>
use <nr_stage_common.scad>

$fn = 8;

// ---- 景观带 ----------------------------------------------------------------
// 覆盖到 stages.json 的 endZ(-2200) 再往前一个可见范围（约 44 m）为止，否则关卡末尾露背景色。
nr_band_quay(y0 = -80, rows = 5, seed = 3, side = -1);  // 出生点身后，防止开场露白
nr_band_quay(y0 = 0, rows = 7, seed = 13, side = -1);   // -0:01 -  0:07  入港：左岸码头
nr_band_channel(y0 = 112, rows = 7, seed = 50);         //  0:07 -  0:16  出港水道
nr_band_quay(y0 = 224, rows = 6, seed = 87, side = 1);  //  0:16 -  0:24  右岸栈桥
nr_band_water(y0 = 320, rows = 9, seed = 124);          //  0:24 -  0:36  外海
nr_band_channel(y0 = 464, rows = 7, seed = 161);        //  0:36 -  0:46  防波堤水道
nr_band_quay(y0 = 576, rows = 8, seed = 198, side = -1);//  0:46 -  0:56  集装箱码头
nr_band_water(y0 = 704, rows = 7, seed = 235);          //  0:56 -  1:06  外锚地
nr_band_channel(y0 = 816, rows = 9, seed = 272);        //  1:06 -  1:18  船坞水道（中BOSS）
nr_band_quay(y0 = 960, rows = 8, seed = 309, side = 1); //  1:18 -  1:28  修船厂
nr_band_water(y0 = 1088, rows = 9, seed = 346);         //  1:28 -  1:40  内锚地
nr_band_channel(y0 = 1232, rows = 9, seed = 383);       //  1:40 -  1:52  军港核心水道
nr_band_quay(y0 = 1376, rows = 9, seed = 420, side = -1);//  1:52 -  2:04  弹药库码头
nr_band_water(y0 = 1520, rows = 9, seed = 457);         //  2:04 -  2:16  开阔水面
nr_band_channel(y0 = 1664, rows = 9, seed = 494);       //  2:16 -  2:28  干船坞
nr_band_quay(y0 = 1808, rows = 8, seed = 531, side = 1);//  2:28 -  2:39  最后阻拦
nr_band_water(y0 = 1936, rows = 10, seed = 568);        //  2:39 -  2:52  出港
nr_band_channel(y0 = 2096, rows = 4, seed = 605);       //  2:52 -  2:58  防波堤口
nr_band_water(y0 = 2160, rows = 6, seed = 642);         //  2:58 -  3:06  BOSS 空域

// ---- 地面目标 --------------------------------------------------------------
// 0:03 入港：左岸岸炮，先教『岸上也要打』
translate([-12, 64, 0.02]) nr_prop_turret();
translate([-13, 88, 0.02]) nr_prop_turret();
translate([6, 96, 0.02]) nr_veh_patrolboat();

// 0:08 出港水道：巡逻艇编队，水道段只有船
translate([-4, 128, 0.02]) nr_veh_patrolboat();
translate([8, 152, 0.02]) nr_veh_patrolboat();
translate([-10, 176, 0.02]) nr_veh_patrolboat();
translate([2, 208, 0.02]) nr_veh_patrolboat();

// 0:18 右岸栈桥：换边，岸在右
translate([12, 240, 0.02]) nr_prop_turret();
translate([-6, 256, 0.02]) nr_veh_patrolboat();
translate([14, 264, 0.02]) nr_bldg_bunker();
translate([11, 288, 0.02]) nr_veh_launcher();
translate([-10, 304, 0.02]) nr_veh_patrolboat();

// 0:26 外海：没有岸，只有船
translate([-8, 336, 0.02]) nr_veh_patrolboat();
translate([9, 360, 0.02]) nr_veh_patrolboat();
translate([0, 384, 0.02]) nr_veh_patrolboat();
translate([-13, 416, 0.02]) nr_veh_patrolboat();
translate([13, 440, 0.02]) nr_veh_patrolboat();

// 0:38 防波堤水道
translate([-6, 480, 0.02]) nr_veh_patrolboat();
translate([7, 504, 0.02]) nr_veh_patrolboat();
translate([0, 536, 0.02]) nr_veh_patrolboat();

// 0:47 集装箱码头：油库连锁的第一处
translate([-12, 592, 0.02]) nr_prop_turret();
translate([6, 608, 0.02]) nr_veh_patrolboat();
translate([-14, 616, 0.02]) nr_bldg_fueldepot();
translate([-14, 632, 0.02]) nr_bldg_fueldepot();
translate([-11, 656, 0.02]) nr_veh_launcher();
translate([9, 672, 0.02]) nr_veh_patrolboat();
translate([-13, 688, 0.02]) nr_bldg_bunker();

// 0:58 外锚地
translate([-9, 720, 0.02]) nr_veh_patrolboat();
translate([8, 744, 0.02]) nr_veh_patrolboat();
translate([0, 776, 0.02]) nr_veh_patrolboat();

// 1:07 船坞水道：中BOSS 前的巡逻网
translate([-11, 832, 0.02]) nr_veh_patrolboat();
translate([11, 856, 0.02]) nr_veh_patrolboat();
translate([-5, 888, 0.02]) nr_veh_patrolboat();
translate([5, 920, 0.02]) nr_veh_patrolboat();

// 1:19 修船厂：中BOSS 之后的补给区
translate([13, 976, 0.02]) nr_prop_turret();
translate([-8, 1024, 0.02]) nr_veh_patrolboat();
translate([12, 1040, 0.02]) nr_veh_launcher();
translate([14, 1064, 0.02]) nr_bldg_bunker();
translate([-12, 1072, 0.02]) nr_veh_patrolboat();

// 1:30 内锚地
translate([-10, 1104, 0.02]) nr_veh_patrolboat();
translate([10, 1128, 0.02]) nr_veh_patrolboat();
translate([0, 1160, 0.02]) nr_veh_patrolboat();
translate([-14, 1192, 0.02]) nr_veh_patrolboat();
translate([14, 1216, 0.02]) nr_veh_patrolboat();

// 1:42 军港核心水道
translate([-7, 1248, 0.02]) nr_veh_patrolboat();
translate([7, 1272, 0.02]) nr_veh_patrolboat();
translate([-13, 1304, 0.02]) nr_veh_patrolboat();
translate([13, 1328, 0.02]) nr_veh_patrolboat();
translate([0, 1360, 0.02]) nr_veh_patrolboat();

// 1:54 弹药库码头：三座油库挨着，一发连锁能清掉半条岸
translate([-12, 1392, 0.02]) nr_prop_turret();
translate([7, 1408, 0.02]) nr_veh_patrolboat();
translate([-14, 1416, 0.02]) nr_bldg_fueldepot();
translate([-14, 1432, 0.02]) nr_bldg_fueldepot();
translate([-10, 1448, 0.02]) nr_bldg_fueldepot();
translate([10, 1464, 0.02]) nr_veh_patrolboat();
translate([-12, 1472, 0.02]) nr_veh_launcher();
translate([4, 1496, 0.02]) nr_veh_patrolboat();
translate([-13, 1504, 0.02]) nr_bldg_bunker();

// 2:06 开阔水面
translate([-11, 1536, 0.02]) nr_veh_patrolboat();
translate([11, 1568, 0.02]) nr_veh_patrolboat();
translate([0, 1600, 0.02]) nr_veh_patrolboat();
translate([-14, 1632, 0.02]) nr_veh_patrolboat();
translate([14, 1648, 0.02]) nr_veh_patrolboat();

// 2:18 干船坞
translate([-8, 1680, 0.02]) nr_veh_patrolboat();
translate([8, 1712, 0.02]) nr_veh_patrolboat();
translate([0, 1744, 0.02]) nr_veh_patrolboat();
translate([-12, 1784, 0.02]) nr_veh_patrolboat();

// 2:30 最后阻拦：右岸防空阵地
translate([12, 1824, 0.02]) nr_prop_turret();
translate([-9, 1840, 0.02]) nr_veh_patrolboat();
translate([14, 1848, 0.02]) nr_prop_turret();
translate([11, 1872, 0.02]) nr_veh_launcher();
translate([-13, 1888, 0.02]) nr_veh_patrolboat();
translate([13, 1896, 0.02]) nr_bldg_bunker();
translate([12, 1920, 0.02]) nr_veh_launcher();

// 2:42 出港：BOSS 空域前的最后两条船
translate([-10, 1968, 0.02]) nr_veh_patrolboat();
translate([10, 2000, 0.02]) nr_veh_patrolboat();

// ---- 岸上布景 --------------------------------------------------------------
// Unarmed facilities occupy the visible outer quay. Keep target footprints and
// firing lanes intact; every wharf has a different working profile.
// Arrival: customs warehouse, unloading apron and mixed short/tall stacks.
translate([-18, -24, 0.02]) nr_bldg_barracks();
translate([-18, 7, 0.02]) nr_bldg_warehouse(variant = 1);
translate([-17, 29, 0.02]) nr_prop_loadingdock();
translate([-19, 49, 0.02]) nr_prop_containerstack(seed = 17);
translate([-20, 76, 0.02]) nr_prop_gantry(variant = 2);
translate([-19, 100, 0.02]) nr_prop_servicecart(variant = 2);

// Opposite shore: a utility wharf, not another identical container row.
translate([19, 229, 0.02]) nr_bldg_substation(variant = 1);
translate([19, 251, 0.02]) nr_prop_pipeyard(variant = 2);
translate([20, 278, 0.02]) nr_bldg_pumpstation();
translate([19, 306, 0.02]) nr_bldg_watertower();

// Container terminal: two-row stacks, empty loading bays and a gantry.
translate([-19, 580, 0.02]) nr_prop_containerstack(seed = 31, variant = 2);
translate([-19, 606, 0.02]) nr_prop_loadingdock(variant = 2);
translate([-21, 640, 0.02]) nr_prop_gantry();
translate([-19, 666, 0.02]) nr_prop_containerstack(seed = 47, variant = 1);
translate([-20, 692, 0.02]) nr_bldg_workshop();

// Repair yard: pipe racks, machinery sheds, spare parts and an open crane bay.
translate([19, 961, 0.02]) nr_bldg_workshop(variant = 1);
translate([18, 993, 0.02]) nr_prop_pipeyard();
translate([20, 1017, 0.02]) nr_prop_gantry(variant = 1);
translate([19, 1049, 0.02]) nr_prop_supplyyard(variant = 1);
translate([19, 1075, 0.02]) nr_prop_servicecart(variant = 1);

// Magazine wharf: low covered stores interspersed with the existing fuel targets.
translate([-19, 1380, 0.02]) nr_bldg_warehouse(variant = 2);
translate([-19, 1405, 0.02]) nr_prop_supplyyard(variant = 2);
translate([-20, 1461, 0.02]) nr_prop_loadingdock(variant = 1);
translate([-20, 1488, 0.02]) nr_bldg_substation();

// Last service quay. All pads end on land before the water band at y=1928.
translate([19, 1810, 0.02]) nr_bldg_workshop(variant = 2);
translate([20, 1835, 0.02]) nr_prop_containerstack(seed = 89, variant = 1);
translate([20, 1860, 0.02]) nr_bldg_pumpstation(variant = 2);
translate([20, 1883, 0.02]) nr_prop_gantry();
translate([19, 1907, 0.02]) nr_prop_supplyyard();

// Channel banks remain at |x| >= 24: tall silhouettes peek into the wide view
// without moving the shore under patrol boats or changing collision targets.
translate([-29, 147, 0.02]) nr_bldg_watertower(variant = 1);
translate([29, 188, 0.02]) nr_bldg_warehouse();
translate([-29, 491, 0.02]) nr_bldg_pumpstation(variant = 1);
translate([29, 542, 0.02]) nr_bldg_silos(variant = 2);
translate([29, 845, 0.02]) nr_bldg_warehouse(variant = 1);
translate([-28, 903, 0.02]) nr_prop_gantry(variant = 2);
translate([29, 1260, 0.02]) nr_bldg_silos();
translate([-29, 1305, 0.02]) nr_bldg_substation(variant = 1);
translate([29, 1347, 0.02]) nr_bldg_watertower(variant = 2);
translate([-28, 1684, 0.02]) nr_prop_gantry(variant = 1);
translate([29, 1740, 0.02]) nr_bldg_workshop(variant = 1);
translate([-29, 1788, 0.02]) nr_prop_pipeyard(variant = 1);

// 水面残骸与礁石：空旷的水段没有参照物就看不出在滚动。
for (i = [0:5]) translate([-15 + i * 6, 330 + i * 23, 0.02]) nr_nature_rock(seed = i * 5);
for (i = [0:3]) translate([13 - i * 8, 1108 + i * 27, 0.02]) nr_prop_wreck(seed = i, stage = 2);
for (i = [0:3]) translate([-12 + i * 9, 1544 + i * 25, 0.02]) nr_nature_rock(seed = 40 + i * 7);
for (i = [0:2]) translate([-9 + i * 9, 1972 + i * 30, 0.02]) nr_prop_wreck(seed = 9 + i, stage = 1);

// ---- 料架 ------------------------------------------------------------------
nr_rack();

// ---- 取景 ------------------------------------------------------------------
// 与游戏内相机同一套参数，见 gameplay.json 的 screen._comment。
gk_camera_lookat([0, 19.9, 75], [0, 40, 0], name = "01_Gameplay_framing", fov = 50);
gk_camera_lookat([0, 619.9, 75], [0, 640, 0], name = "02_Container_wharf", fov = 50);
gk_camera_lookat([0, 1419.9, 75], [0, 1440, 0], name = "03_Magazine_wharf", fov = 50);
