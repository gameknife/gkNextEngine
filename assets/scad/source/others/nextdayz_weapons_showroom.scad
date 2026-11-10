// nextdayz_weapons_showroom.scad —— NextDayz 武器验收展台
// 上排：手持坐标系（cw_wpn_*_held，右侧朝向镜头），与游戏里挂到手上的模型完全一致；
// 下排：地面 loot 版本（cw_wpn_* / cw_item_* 平躺）。
// 改 FOCUS 切换默认机位（gnb shot 截第一个 gk_camera）："all" / "ak" / "svd" / "mosin" /
// "shotgun" / "pistol" / "melee" / "axe" / "rpg" / "loot"。
use <../../lib/kit_coldwar.scad>
use <../../lib/gk_camera.scad>

FOCUS = "all";

// 展台：浅灰背板 + 深色台面
color([0.58, 0.58, 0.56]) translate([0.32, 0.2, 1.2]) cube([0.04, 3.4, 2.6], center = true);
color([0.20, 0.19, 0.18]) translate([-0.5, 0.2, -0.03]) cube([2.2, 3.4, 0.06], center = true);

// 手持版：枪口朝 -Y，右侧（-X）朝镜头
translate([0, 0, 2.05]) cw_wpn_ak_held();
translate([0, 0, 1.68]) cw_wpn_svd_held();
translate([0, 0, 1.31]) cw_wpn_mosin_held();
translate([0, 0, 0.95]) cw_wpn_shotgun_held();
translate([0, 0.95, 0.62]) cw_wpn_pistol_held();
translate([0, 0.10, 0.62]) cw_wpn_knife_held();
translate([0, -0.55, 0.50]) cw_wpn_crowbar_held();
translate([0, 1.30, 0.34]) rotate([-90, 0, 0]) cw_wpn_axe_held();
translate([0, 0.0, 0.30]) cw_wpn_rpg_held();

// loot 版：平躺在台面上
translate([-0.75, -0.6, 0]) rotate([0, 0, 90]) cw_wpn_ak(seed = 0);
translate([-1.00, -0.6, 0]) rotate([0, 0, 90]) cw_wpn_svd(seed = 0);
translate([-1.25, -0.6, 0]) rotate([0, 0, 90]) cw_wpn_mosin(seed = 0);
translate([-0.75, 0.75, 0]) rotate([0, 0, 90]) cw_wpn_shotgun(seed = 0);
translate([-1.05, 0.75, 0]) rotate([0, 0, 90]) cw_wpn_pistol(seed = 0);
translate([-1.05, 1.15, 0]) rotate([0, 0, 90]) cw_item_knife();
translate([-1.30, 0.80, 0]) rotate([0, 0, 90]) cw_item_crowbar();
translate([-1.50, 0.80, 0]) rotate([0, 0, 90]) cw_item_axe();

// 机位：第一个 gk_camera 是默认入场视角
module cam_for(name)
{
    // 手持版的前向 f = -Y：机位目标的 y 取 -(武器长度中点)
    if (name == "ak")      gk_camera_lookat([-1.00, -0.147, 1.99], [0, -0.147, 1.93], "ak", 45);
    if (name == "svd")     gk_camera_lookat([-1.15, -0.212, 1.64], [0, -0.212, 1.59], "svd", 45);
    if (name == "mosin")   gk_camera_lookat([-1.15, -0.183, 1.25], [0, -0.183, 1.21], "mosin", 45);
    if (name == "shotgun") gk_camera_lookat([-1.10, -0.180, 0.87], [0, -0.180, 0.83], "shotgun", 45);
    if (name == "pistol")  gk_camera_lookat([-0.38, 0.900, 0.62], [0, 0.900, 0.60], "pistol", 45);
    if (name == "melee")   gk_camera_lookat([-0.95, -0.300, 0.52], [0, -0.300, 0.44], "melee", 45);
    if (name == "axe")     gk_camera_lookat([-0.85, 1.300, 0.88], [0, 1.300, 0.80], "axe", 45);
    if (name == "rpg")     gk_camera_lookat([-1.30, 0.070, 0.26], [0, 0.070, 0.18], "rpg", 45);
    if (name == "loot")    gk_camera_lookat([-1.0, 0.25, 1.25], [-1.05, 0.25, 0.0], "loot", 60);
    if (name == "all")     gk_camera_lookat([-3.4, 0.25, 1.25], [0, 0.25, 1.15], "all", 50);
}
cam_for(FOCUS);
for (n = ["all", "ak", "svd", "mosin", "shotgun", "pistol", "melee", "axe", "rpg", "loot"]) if (n != FOCUS) cam_for(n);
