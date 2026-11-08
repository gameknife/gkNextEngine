// gk_texture_patterns.scad —— gk_texture() 过程纹理样板
// 每块面板一种图案（默认色板），下方小方块验证盒投影在各个面上的连续性。
// gk_texture(pattern, palette = [[r,g,b], ...], scale = 0.5, seed = 0, relief = 1)：
//   最终反照率 = 外层 color()/gk_material() 颜色 × 烘焙图案。迷彩用白色底材质；
//   织纹、地表、建筑类图案是调制纹理，直接乘在布料、地面、墙面本色上。
//   每个图案还带法线起伏；第四排的旧金属 / 木纹再用 MRA 调粗糙度与金属度（材质给上限），
//   chipped / rust / worn 的色板是绝对颜色，所以外层材质是白色。
// 第一排：布料 / 皮革；第二排：地表与路面；第三排：建筑；第四排：旧金属与木。
// 每项：[图案, 颜色, scale, roughness, metalness]
// 右侧 2×3：完全用 SCAD 写的节点图印花（gk_texgraph.scad），不改 C++。
use <../../lib/gk_camera.scad>
use <../../lib/gk_texgraph.scad>

ROWS = [
    [["woodland", [1, 1, 1], 0.45], ["digital", [1, 1, 1], 0.45], ["desert", [1, 1, 1], 0.45],
     ["flora", [1, 1, 1], 0.45], ["rain", [1, 1, 1], 0.45], ["canvas", [0.36, 0.35, 0.22], 0.45],
     ["twill", [0.50, 0.42, 0.27], 0.45], ["grain", [0.30, 0.18, 0.09], 0.45, 0.6]],
    [["meadow", [0.22, 0.29, 0.16], 3.0], ["dirt", [0.29, 0.24, 0.18], 2.0], ["gravel", [0.33, 0.31, 0.26], 1.5],
     ["asphalt", [0.17, 0.17, 0.165], 2.0], ["rock", [0.32, 0.30, 0.28], 2.0]],
    [["plaster", [0.54, 0.46, 0.32], 1.6], ["brick", [0.40, 0.20, 0.13], 1.0], ["tiles", [0.42, 0.20, 0.13], 1.6],
     ["planks", [0.30, 0.24, 0.16], 1.2], ["stone", [0.42, 0.40, 0.36], 1.4], ["concrete", [0.46, 0.45, 0.42], 1.6]],
    [["chipped", [1, 1, 1], 0.5, 0.72, 0.9], ["rust", [1, 1, 1], 0.8, 0.85, 0.8], ["worn", [1, 1, 1], 0.4, 0.32, 0.85],
     ["woodgrain", [0.45, 0.27, 0.13], 0.5, 0.4]],
];

color([0.20, 0.19, 0.18]) translate([0, -0.6, -0.03]) cube([7.0, 4.4, 0.06], center = true);
color([0.58, 0.58, 0.56]) translate([0, 0.9, 2.2]) cube([7.0, 0.04, 4.4], center = true);

for (r = [0 : len(ROWS) - 1], i = [0 : len(ROWS[r]) - 1])
{
    p = ROWS[r][i];
    x = -3.0 + i * 0.82;
    z = 3.95 - r * 1.1;
    // 竖直面板（侧面投影：v 沿 +Z，竖条纹 / 砖缝 / 瓦行保持水平竖直）
    gk_material(p[1], roughness = len(p) > 3 ? p[3] : 1, metalness = len(p) > 4 ? p[4] : 0)
        gk_texture(p[0], scale = p[2])
        translate([x, 0.85, z]) cube([0.72, 0.02, 0.95], center = true);
    color([0.85, 0.85, 0.82]) translate([x - 0.34, 0.835, z - 0.56]) rotate([90, 0, 0])
        linear_extrude(height = 0.004) text(p[0], size = 0.08);
}
// SCAD 节点图印花：[名字, 节点图, 颜色, scale, roughness, metalness]
GRAPHS = [
    ["corrugated", tex_corrugated(), [1, 1, 1], 0.9, 0.8, 0.9],
    ["flagstone", tex_flagstone(), [0.45, 0.43, 0.38], 1.2, 1, 0],
    ["bark", tex_bark(), [0.36, 0.27, 0.19], 0.5, 1, 0],
    ["brickwall", tex_brickwall(rows = 12, columns = 4, joint = 0.1), [0.36, 0.22, 0.16], 1.0, 1, 0],
    ["tigerstripe", tex_tigerstripe(), [1, 1, 1], 0.45, 1, 0],
    ["splinter", tex_splinter(), [1, 1, 1], 0.45, 1, 0],
];
for (k = [0 : len(GRAPHS) - 1])
{
    g = GRAPHS[k];
    x = 2.0 + (k % 2) * 0.82;
    z = 3.95 - (1 + floor(k / 2)) * 1.1;
    gk_material(g[2], roughness = g[4], metalness = g[5]) gk_texture(g[1], scale = g[3])
        translate([x, 0.85, z]) cube([0.72, 0.02, 0.95], center = true);
    color([0.85, 0.85, 0.82]) translate([x - 0.34, 0.835, z - 0.56]) rotate([90, 0, 0])
        linear_extrude(height = 0.004) text(g[0], size = 0.07);
}

// 地面平铺：地表图案的俯视投影
for (i = [0 : len(ROWS[1]) - 1])
{
    p = ROWS[1][i];
    gk_material(p[1], roughness = 1) gk_texture(p[0], scale = p[2])
        translate([-3.0 + i * 1.3, -1.6, 0.01]) cube([1.25, 1.6, 0.02], center = true);
}

gk_camera_lookat([0, -6.2, 2.6], [0, 0.6, 1.8], "board", 45);
gk_camera_lookat([-1.4, -3.4, 1.2], [-1.4, -1.6, 0.0], "ground", 50);
