// nextdayz_heritage_showroom.scad —— NextDayz "有史可考"武器与服装验收展台
// 上排：手持坐标系（nd_wpn_*_held，右侧朝向镜头），与游戏里挂到手上的模型完全一致；
// 下排：地面 loot 版本（平躺）；右侧：服装的地面 loot 版本（nextdayz_loot_items.scad 的 bone_item_*）。
// 改 FOCUS 切换默认机位（gnb shot 截第一个 gk_camera）：
//   "all" / "aks74u" / "bizon" / "aps" / "svt40" / "toz34" / "loot" / "kit"。
use <../../lib/kit_nd_arsenal.scad>
use <../../lib/gk_camera.scad>
use <../../characters/nextdayz_loot_items.scad>

FOCUS = "all";

color([0.58, 0.58, 0.56]) translate([0.32, 0.2, 1.2]) cube([0.04, 3.4, 2.6], center = true);
color([0.20, 0.19, 0.18]) translate([-0.9, 0.2, -0.03]) cube([3.0, 3.4, 0.06], center = true);

// 手持版：枪口朝 -Y，右侧（-X）朝镜头
translate([0, 0, 2.05]) nd_wpn_aks74u_held();
translate([0, 0, 1.70]) nd_wpn_bizon_held();
translate([0, 0, 1.33]) nd_wpn_svt40_held();
translate([0, 0, 0.95]) nd_wpn_toz34_held();
translate([0, 0.95, 0.62]) nd_wpn_aps_held();

// loot 版：平躺在台面上
translate([-0.75, -0.6, 0]) rotate([0, 0, 90]) nd_wpn_aks74u();
translate([-1.00, -0.6, 0]) rotate([0, 0, 90]) nd_wpn_bizon();
translate([-1.25, -0.6, 0]) rotate([0, 0, 90]) nd_wpn_svt40();
translate([-0.75, 0.75, 0]) rotate([0, 0, 90]) nd_wpn_toz34();
translate([-1.05, 0.75, 0]) rotate([0, 0, 90]) nd_wpn_aps();

// 服装 loot：三列（阿富汗 / 戈尔卡 + 海魂衫 / 东德）
translate([-1.65, -1.05, 0]) bone_item_hat_panama();
translate([-1.65, -0.60, 0]) bone_item_helmet_ssh68();
translate([-1.65, -0.10, 0]) bone_item_jacket_afganka();
translate([-1.65, 0.45, 0]) bone_item_pants_afganka();
translate([-1.65, 0.95, 0]) bone_item_vest_type56();
translate([-1.65, 1.45, 0]) bone_item_vest_6b3();
translate([-2.15, -1.05, 0]) bone_item_backpack_rd54();
translate([-2.15, -0.45, 0]) bone_item_shirt_telnyashka();
translate([-2.15, 0.05, 0]) bone_item_jacket_gorka();
translate([-2.15, 0.55, 0]) bone_item_pants_gorka();
translate([-2.15, 1.20, 0]) bone_item_backpack_abalakov();
translate([-2.60, -0.60, 0]) bone_item_helmet_m56();
translate([-2.60, -0.05, 0]) bone_item_jacket_strichtarn();
translate([-2.60, 0.50, 0]) bone_item_pants_strichtarn();

module cam_for(name)
{
    if (name == "aks74u") gk_camera_lookat([-0.80, -0.047, 2.00], [0, -0.047, 1.95], "aks74u", 45);
    if (name == "bizon")  gk_camera_lookat([-0.80, -0.039, 1.65], [0, -0.039, 1.60], "bizon", 45);
    if (name == "svt40")  gk_camera_lookat([-1.15, -0.178, 1.29], [0, -0.178, 1.25], "svt40", 45);
    if (name == "toz34")  gk_camera_lookat([-1.10, -0.183, 0.91], [0, -0.183, 0.87], "toz34", 45);
    if (name == "aps")    gk_camera_lookat([-0.42, 0.855, 0.62], [0, 0.855, 0.60], "aps", 45);
    if (name == "loot")   gk_camera_lookat([-2.0, 0.10, 1.10], [-1.0, 0.10, 0.0], "loot", 55);
    if (name == "kit")    gk_camera_lookat([-0.75, 0.20, 1.35], [-2.15, 0.20, 0.05], "kit", 62);
    if (name == "all")    gk_camera_lookat([-3.4, 0.25, 1.35], [0, 0.25, 1.25], "all", 50);
}
cam_for(FOCUS);
for (n = ["all", "aks74u", "bizon", "aps", "svt40", "toz34", "loot", "kit"]) if (n != FOCUS) cam_for(n);
