// gk_texture_patterns.scad —— gk_texture() 过程纹理样板
// 每块面板一种图案（默认色板），下方小方块验证盒投影在各个面上的连续性。
// gk_texture(pattern, palette = [[r,g,b], ...], scale = 0.5, seed = 0)：
//   最终反照率 = 外层 color()/gk_material() 颜色 × 烘焙图案，所以迷彩用白色底材质，
//   canvas / twill / grain 这类质感纹理则直接乘在布料、皮革本色上。
use <../../lib/gk_camera.scad>

PATTERNS = ["woodland", "digital", "desert", "flora", "rain", "canvas", "twill", "grain"];
BASE = [[1, 1, 1], [1, 1, 1], [1, 1, 1], [1, 1, 1], [1, 1, 1],
        [0.36, 0.35, 0.22], [0.50, 0.42, 0.27], [0.30, 0.18, 0.09]];

color([0.20, 0.19, 0.18]) translate([0, 0, -0.03]) cube([6.4, 3.0, 0.06], center = true);
color([0.58, 0.58, 0.56]) translate([0, 0.9, 1.2]) cube([6.4, 0.04, 2.4], center = true);

for (i = [0 : len(PATTERNS) - 1])
{
    x = -2.8 + i * 0.8;
    // 竖直面板（侧面投影：v 沿 +Z，竖条纹应保持竖直）
    gk_material(BASE[i], roughness = 1) gk_texture(PATTERNS[i], scale = 0.45)
        translate([x, 0.85, 1.35]) cube([0.7, 0.02, 1.4], center = true);
    // 小方块：三个面三种投影
    gk_material(BASE[i], roughness = 1) gk_texture(PATTERNS[i], scale = 0.45)
        translate([x, 0.2, 0.2]) rotate([0, 0, 25]) cube(0.4, center = true);
    color([0.85, 0.85, 0.82]) translate([x - 0.3, 0.84, 0.48]) rotate([90, 0, 0])
        linear_extrude(height = 0.004) text(PATTERNS[i], size = 0.07);
}

gk_camera_lookat([0, -4.2, 1.5], [0, 0.6, 1.0], "board", 45);
gk_camera_lookat([-2.4, -1.4, 1.0], [-2.4, 0.6, 1.2], "camo_close", 40);
