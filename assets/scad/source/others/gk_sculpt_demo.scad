// gk_sculpt_demo.scad —— 雕塑几何与法线原语对照台（NextDayz 视觉升级 V1-1）
// 每排左边是旧写法，右边是新原语，同色同光对比：
//   前排  倒角盒：三立方体 hull 假倒角 | gk_round_box segs=1 / 3 / 5 | power=4 方圆角
//   中排  八棱柱：默认 35° 折痕 | gk_crease(50) | gk_crease(0) 纯硬面球
//   后排  树：多球拼树冠 + 圆锥干 | gk_sweep 弯干 + gk_puff_normals 蓬松树冠
// gnb shot --scene assets/scad/source/others/gk_sculpt_demo.scad
use <../../lib/gk_camera.scad>

$fn = 8;

module old_chamfer_box(s, b)
{
    hull()
    {
        cube([s[0] - 2 * b, s[1], s[2] - 2 * b], center = true);
        cube([s[0], s[1] - 2 * b, s[2] - 2 * b], center = true);
        cube([s[0] - 2 * b, s[1] - 2 * b, s[2]], center = true);
    }
}

CLAY = [0.62, 0.42, 0.30];
BARK = [0.30, 0.22, 0.16];
LEAF = [0.22, 0.34, 0.13];
LEAF_TOP = [0.30, 0.42, 0.16];

// 展台
color([0.46, 0.46, 0.44]) translate([2.4, 2.2, -0.05]) cube([12, 9, 0.1], center = true);

// ---- 前排：倒角盒 ----
color(CLAY)
{
    translate([-1.5, 0, 0.45]) old_chamfer_box([0.9, 0.9, 0.9], 0.12);
    translate([0.5, 0, 0.45]) gk_round_box([0.9, 0.9, 0.9], r = 0.12, segs = 1);
    translate([2.5, 0, 0.45]) gk_round_box([0.9, 0.9, 0.9], r = 0.12, segs = 3);
    translate([4.5, 0, 0.45]) gk_round_box([0.9, 0.9, 0.9], r = 0.25, segs = 5);
    translate([6.5, 0, 0.45]) gk_round_box([0.9, 0.9, 0.9], r = 0.40, segs = 5, power = 4);
}

// ---- 中排：八棱柱与球 ----
color([0.36, 0.42, 0.52])
{
    translate([-1.5, 2.4, 0]) { cylinder(r = 0.35, h = 0.9); translate([0, 0, 1.25]) sphere(r = 0.35); }
    translate([0.9, 2.4, 0]) gk_crease(50) { cylinder(r = 0.35, h = 0.9); translate([0, 0, 1.25]) sphere(r = 0.35); }
    translate([3.3, 2.4, 0]) gk_crease(0) { cylinder(r = 0.35, h = 0.9); translate([0, 0, 1.25]) sphere(r = 0.35); }
}
// 一根 S 形扫掠管：平行传输标架，拐点不翻转
color([0.55, 0.50, 0.36]) translate([5.2, 2.4, 0.3])
    gk_sweep([for (i = [0 : 8]) [i * 0.25, 0.35 * sin(i * 60), 0.35 + 0.2 * sin(i * 40)]], 0.09, fn = 12, cap = "round");

// ---- 后排：树 ----
module old_tree()
{
    color(BARK) cylinder(h = 2.2, r1 = 0.16, r2 = 0.1, $fn = 7);
    for (i = [0 : 5]) let(a = i * 60, d = i == 0 ? 0 : 0.6)
        color(i % 2 == 0 ? LEAF : LEAF_TOP)
            translate([d * cos(a), d * sin(a), 2.4 + 0.25 * (i % 3)]) scale([1, 1, 0.8]) sphere(r = 0.8);
}

module new_tree()
{
    color(BARK) gk_flatten()
    {
        gk_sweep([[0, 0, 0], [0.05, 0, 0.8], [0.18, 0.05, 1.7], [0.1, 0.1, 2.5]], [0.24, 0.15, 0.12, 0.08], fn = 12, cap = "flat");
        gk_sweep([[0.15, 0.04, 1.5], [0.55, 0.1, 2.1], [0.75, 0.15, 2.4]], [0.07, 0.05, 0.03], fn = 8);
        gk_sweep([[0.1, 0.06, 1.8], [-0.35, -0.1, 2.3], [-0.5, -0.2, 2.6]], [0.06, 0.045, 0.03], fn = 8);
    }
    gk_puff_normals(0.7, 0.3) gk_crease(60)
        for (i = [0 : 5]) let(a = i * 60, d = i == 0 ? 0 : 0.6)
            color(i % 2 == 0 ? LEAF : LEAF_TOP)
                translate([d * cos(a), d * sin(a), 2.4 + 0.25 * (i % 3)]) scale([1, 1, 0.8]) sphere(r = 0.8);
}

translate([-0.5, 5.6, 0]) old_tree();
translate([4.5, 5.6, 0]) new_tree();

gk_camera_lookat([2.4, -6.5, 4.2], [2.4, 2.6, 1.0], "overview", 50);
gk_camera_lookat([2.6, -2.6, 1.6], [2.6, 0.6, 0.45], "boxes", 46);
gk_camera_lookat([2.0, -1.2, 2.6], [2.0, 5.6, 2.2], "trees", 55);
