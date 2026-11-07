// nextdayz_clothing_heritage.scad —— NextDayz 有史可考的服装（参考 innawoods.net 的手绘图鉴挑选）
// 苏军阿富汗战争：巴拿马帽 / SSh-68 钢盔 / 阿富汗卡上衣与长裤（基尔扎人造革靴）/ 56 式胸挂 /
//                  6B3 防弹衣 / RD-54 伞兵背包 / 海魂衫
// 山地部队与猎人：戈尔卡罩衫与长裤 / 阿巴拉科夫背包
// 东德国家人民军：M56 钢盔 / 雨点迷彩上衣与长裤（高筒行军靴）
//
// 只提供几何模块，不含 bone_*：nextdayz_clothing.scad（穿戴，按骨骼局部坐标）与
// nextdayz_loot_items.scad（地面 loot）各自用 bone_<item id>__<bone> / bone_item_<id> 调用这里。
// 坐标与 nextdayz_clothing.scad 完全一致（米，Z 向上，前方 = -Y），并沿用它的分层包络：
//   皮肤 < 外套（厚 .28）< 背心（后 .20 / 前 -.21）< 背包（前 .21）。
// 迷彩、条纹、加固片都用真实几何表达（不贴图）；材质全部走 gk_material（粗糙度 / 金属度），
// 每个部件不超过 16 种材质。本文件不 use 其他服装文件，避免循环引用。

// ---------------- 材质 ----------------
module ndh_cloth(c, r = 0.92) { gk_material(c, roughness = r, metalness = 0) children(); }      // 棉布 / 帆布
module ndh_leather(c, r = 0.55) { gk_material(c, roughness = r, metalness = 0) children(); }
module ndh_metal(c = [0.30, 0.30, 0.29], r = 0.42) { gk_material(c, roughness = r, metalness = 0.9) children(); }
module ndh_brass() { gk_material([0.74, 0.56, 0.26], roughness = 0.32, metalness = 1.0) children(); }
module ndh_paint(c, r = 0.55) { gk_material(c, roughness = r, metalness = 0.15) children(); }    // 钢盔上的军绿漆
module ndh_plastic(c, r = 0.45) { gk_material(c, roughness = r, metalness = 0) children(); }

function ndh_sand() = [0.50, 0.42, 0.27];          // 阿富汗卡：晒褪的沙卡其
function ndh_gorka() = [0.35, 0.35, 0.22];         // 戈尔卡：帐篷布卡其
function ndh_gorka_patch() = [0.27, 0.20, 0.12];   // 戈尔卡：棕色加固片
function ndh_stein() = [0.37, 0.38, 0.30];         // 雨点迷彩：灰绿底
function ndh_rain() = [0.25, 0.19, 0.13];          // 雨点迷彩：棕色雨痕
function ndh_tel_white() = [0.80, 0.80, 0.78];
function ndh_tel_blue() = [0.14, 0.27, 0.56];
function ndh_web() = [0.09, 0.10, 0.095];
function ndh_shade(c, k = 0.8) = [c[0] * k, c[1] * k, c[2] * k];
function ndh_rnd(s, m) = (((s * s + s * 601 + 37) % 65521) * 7 + 11) % m;
function ndh_rndf(s) = ndh_rnd(s, 1000) / 999;

// 与 nextdayz_clothing.scad 的 ndc_box 同形：倒角盒
module ndh_box(size, bevel = 0.012)
{
    hull()
    {
        cube([size[0] - 2 * bevel, size[1], size[2] - 2 * bevel], center = true);
        cube([size[0], size[1] - 2 * bevel, size[2] - 2 * bevel], center = true);
        cube([size[0] - 2 * bevel, size[1] - 2 * bevel, size[2]], center = true);
    }
}
// 只倒竖棱的环带（条纹、袖口、腰带）
module ndh_band(w, d, h, bevel = 0.012)
    hull() { cube([w - 2 * bevel, d, h], center = true); cube([w, d - 2 * bevel, h], center = true); }

// 外套躯干外壳（与 ndc_coat_torso 同一包络）在高度 z 处的宽 / 深
function ndh_tw(z) = z < 0.105 ? 0.37 : z > 0.245 ? 0.425 : 0.37 + (z - 0.105) / 0.14 * 0.055;
function ndh_td(z) = z < 0.105 ? 0.26 : z > 0.245 ? 0.28 : 0.26 + (z - 0.105) / 0.14 * 0.02;

module ndh_coat_shell(c)
{
    ndh_cloth(c) hull()
    {
        translate([0, 0, 0.04]) ndh_box([0.37, 0.26, 0.13]);
        translate([0, 0, 0.37]) ndh_box([0.425, 0.28, 0.25]);
    }
}

// 雨点迷彩：竖直短雨痕。贴在外套躯干前 / 后面（side = -1 前，+1 后）
module ndh_rain_torso(side, seed)
{
    ndh_cloth(ndh_rain(), 0.9)
        for (row = [0 : 15], col = [0 : 13])
            let(i = row * 14 + col + seed * 977,
                z = 0.0 + row * 0.031 + ndh_rndf(i) * 0.012,
                w = ndh_tw(z) - 0.05,
                x = -w / 2 + (col + 0.5) / 14 * w + (ndh_rndf(i + 3) - 0.5) * 0.012,
                len = 0.012 + ndh_rndf(i + 7) * 0.020)
                if (ndh_rnd(i + 11, 10) < 6 && z < 0.47)
                    translate([x, side * (ndh_td(z) / 2 + 0.0008), z]) cube([0.0045, 0.0030, len], center = true);
}
// 雨点迷彩：贴在一个竖直长方体的四个侧面（袖子、裤腿）。box 中心在 (0,0,-len/2)
module ndh_rain_box(width, depth, len, seed, rows = 9)
{
    ndh_cloth(ndh_rain(), 0.9)
        for (face = [0 : 3], row = [0 : rows - 1], col = [0 : 4])
            let(i = face * 131 + row * 7 + col + seed * 389,
                along = face % 2 == 0 ? width : depth,
                off = (face % 2 == 0 ? depth : width) / 2 + 0.0008,
                t = -along / 2 + 0.014 + (col + ndh_rndf(i)) / 5 * (along - 0.028),
                z = -0.022 - (row + ndh_rndf(i + 5) * 0.6) / rows * (len - 0.05),
                l = 0.012 + ndh_rndf(i + 9) * 0.018)
                if (ndh_rnd(i + 13, 10) < 6)
                    translate(face == 0 ? [t, -off, z] : face == 1 ? [off, t, z] : face == 2 ? [t, off, z] : [-off, t, z])
                        cube(face % 2 == 0 ? [0.0045, 0.003, l] : [0.003, 0.0045, l], center = true);
}

// 阿富汗卡 / 雨点迷彩共用：翻领 + 肩章 + 门襟纽扣
module ndh_collar_and_straps(c, buttons)
{
    $fn = 10;
    // 翻领：贴在肩上的两片斜领面 + 后领
    for (s = [-1, 1])
        ndh_cloth(ndh_shade(c, 0.9)) translate([s * 0.100, -0.035, 0.507]) rotate([0, s * 18, s * -14]) ndh_box([0.085, 0.13, 0.022], 0.006);
    ndh_cloth(ndh_shade(c, 0.9)) translate([0, 0.085, 0.507]) ndh_box([0.20, 0.05, 0.028], 0.006);
    // 肩章
    for (s = [-1, 1]) ndh_cloth(ndh_shade(c, 0.86)) translate([s * 0.150, 0.0, 0.497]) ndh_box([0.050, 0.15, 0.012], 0.004);
    for (s = [-1, 1]) ndh_plastic(buttons) translate([s * 0.150, -0.060, 0.505]) cylinder(h = 0.006, r = 0.008, center = true, $fn = 10);
    // 门襟 + 纽扣
    ndh_cloth(ndh_shade(c, 0.92)) translate([0, -0.142, 0.25]) ndh_box([0.040, 0.010, 0.47], 0.003);
    for (z = [0.07, 0.18, 0.29, 0.40]) ndh_plastic(buttons) translate([0, -0.149, z]) rotate([90, 0, 0]) cylinder(h = 0.006, r = 0.008, center = true, $fn = 10);
}

// ================= 头部 =================
// 巴拿马帽：圆顶 + 下斜宽檐 + 帽箍 + 两侧通风孔 + 正面红星
module ndh_panama()
{
    $fn = 20;
    c = [0.56, 0.48, 0.31];
    ndh_cloth(c, 0.94)
    {
        hull()
        {
            translate([0, 0.005, 0.182]) scale([1, 1.06, 1]) cylinder(h = 0.03, r = 0.140);
            translate([0, 0.005, 0.262]) scale([1, 1.06, 0.55]) sphere(r = 0.128);
        }
        translate([0, 0.005, 0.168]) scale([1, 1.06, 1]) difference()
        {
            cylinder(h = 0.022, r1 = 0.232, r2 = 0.150);
            translate([0, 0, -0.01]) cylinder(h = 0.05, r = 0.120);
        }
    }
    // 帽檐车缝线（同心圈）
    ndh_cloth(ndh_shade(c, 0.86), 0.9) for (r = [0.175, 0.205])
        translate([0, 0.005, 0.168 + 0.022 * (0.232 - r) / 0.082 + 0.0008]) scale([1, 1.06, 1]) difference()
        {
            cylinder(h = 0.0022, r = r + 0.003);
            translate([0, 0, -0.01]) cylinder(h = 0.03, r = r);
        }
    ndh_cloth(ndh_shade(c, 0.72), 0.9) translate([0, 0.005, 0.192]) scale([1, 1.06, 1]) difference()
    {
        cylinder(h = 0.020, r = 0.1435);
        translate([0, 0, -0.01]) cylinder(h = 0.04, r = 0.130);
    }
    ndh_metal([0.22, 0.22, 0.20]) for (s = [-1, 1], a = [-12, 12])
        translate([s * 0.134 * cos(a), 0.005 + 0.134 * 1.06 * sin(a), 0.25]) rotate([0, 90, 0]) cylinder(h = 0.012, r = 0.0055, center = true, $fn = 10);
    // 红星帽徽：五角星片 + 中心
    ndh_paint([0.62, 0.07, 0.04], 0.3) translate([0, -0.1445, 0.232]) rotate([90, 0, 0])
        linear_extrude(height = 0.004, center = true)
            polygon([for (i = [0 : 9]) let(a = 90 + i * 36, rr = i % 2 == 0 ? 0.018 : 0.0075) [rr * cos(a), rr * sin(a)]]);
    ndh_brass() translate([0, -0.1475, 0.232]) rotate([90, 0, 0]) cylinder(h = 0.002, r = 0.0045, center = true, $fn = 10);
}

// SSh-68：近似半球的冲压钢盔 + 一圈外翻短檐 + 两侧铆钉 + 棕色皮下颚带（loot 时不带）
module ndh_ssh68(strap = true)
{
    $fn = 24;
    olive = [0.24, 0.27, 0.15];
    ndh_paint(olive) difference()
    {
        translate([0, 0.005, 0.170]) scale([1, 1.07, 0.98]) sphere(r = 0.186);
        translate([0, 0, -0.03]) cube([0.6, 0.6, 0.40], center = true);
        translate([0, 0.005, 0.170]) scale([1, 1.07, 0.98]) sphere(r = 0.176);
    }
    // 外翻短檐：截锥环
    ndh_paint(ndh_shade(olive, 0.92)) translate([0, 0.005, 0.158]) scale([1, 1.07, 1]) difference()
    {
        cylinder(h = 0.016, r1 = 0.200, r2 = 0.187);
        translate([0, 0, -0.01]) cylinder(h = 0.04, r = 0.176);
    }
    ndh_metal([0.36, 0.36, 0.34], 0.35) translate([0, 0.005, 0.157]) scale([1, 1.07, 1]) difference()
    {
        cylinder(h = 0.0035, r = 0.2005);
        translate([0, 0, -0.01]) cylinder(h = 0.04, r = 0.190);
    }
    ndh_metal() for (s = [-1, 1], a = [-30, 30])
        translate([s * 0.174 * cos(a), 0.005 + 0.174 * 1.07 * sin(a), 0.235]) rotate([0, s * 90, 0]) cylinder(h = 0.006, r = 0.0065, center = true, $fn = 10);
    if (strap)
    {
        ndh_leather([0.30, 0.18, 0.09]) for (s = [-1, 1])
            translate([s * 0.125, 0.005, 0.10]) rotate([0, s * 8, 0]) cube([0.012, 0.022, 0.13], center = true);
        ndh_leather([0.30, 0.18, 0.09]) translate([0, -0.040, 0.038]) ndh_box([0.24, 0.026, 0.018], 0.004);
        ndh_metal() translate([0.07, -0.054, 0.038]) cube([0.016, 0.004, 0.020], center = true);
    }
}

// M56 钢盔：平缓圆顶 + 钟形外扩的侧裙（后部更长）+ 两侧各三颗内衬铆钉 + 黑色下颚带（loot 时不带）
module ndh_m56(strap = true)
{
    $fn = 28;
    grey = [0.27, 0.29, 0.27];
    ndh_paint(grey, 0.62) scale([1, 1.08, 1]) rotate_extrude()
        polygon([[0, 0.336], [0.070, 0.332], [0.128, 0.316], [0.164, 0.286], [0.180, 0.246], [0.188, 0.204],
                 [0.204, 0.166], [0.226, 0.134], [0.230, 0.124], [0.218, 0.124], [0.196, 0.150],
                 [0.176, 0.190], [0.170, 0.240], [0, 0.240]]);
    // 外扩裙边下缘略深的边线
    ndh_paint(ndh_shade(grey, 0.8), 0.55) scale([1, 1.08, 1]) difference()
    {
        translate([0, 0, 0.124]) cylinder(h = 0.004, r = 0.231);
        translate([0, 0, 0.11]) cylinder(h = 0.04, r = 0.217);
    }
    ndh_metal([0.22, 0.23, 0.22], 0.5) for (s = [-1, 1], a = [-28, 0, 28])
        translate([s * 0.191 * cos(a), 1.08 * 0.191 * sin(a), 0.196]) rotate([0, s * 70, 0]) cylinder(h = 0.008, r = 0.0072, center = true, $fn = 10);
    if (strap)
    {
        ndh_leather([0.06, 0.055, 0.05], 0.45) for (s = [-1, 1])
            translate([s * 0.128, 0.0, 0.09]) rotate([0, s * 8, 0]) cube([0.012, 0.022, 0.12], center = true);
        ndh_leather([0.06, 0.055, 0.05], 0.45) translate([0, -0.040, 0.034]) ndh_box([0.24, 0.026, 0.018], 0.004);
    }
}

// ================= 躯干：外套与海魂衫 =================
module ndh_coat_torso(kind)
{
    $fn = 10;
    c = kind == "afganka" ? ndh_sand() : kind == "gorka" ? ndh_gorka() : ndh_stein();
    p = ndh_gorka_patch();
    if (kind == "afganka")
    {
        ndh_coat_shell(c);
        ndh_collar_and_straps(c, [0.22, 0.18, 0.10]);
        // 斜开胸袋（袋口向门襟上斜）+ 袋盖 + 下摆斜插袋 + 腰部抽绳
        for (s = [-1, 1])
        {
            ndh_cloth(ndh_shade(c, 0.88)) translate([s * 0.105, -0.146, 0.34]) rotate([0, s * 12, 0]) ndh_box([0.115, 0.016, 0.115], 0.005);
            ndh_cloth(ndh_shade(c, 0.78)) translate([s * 0.105, -0.153, 0.385]) rotate([0, s * 12, 0]) ndh_box([0.122, 0.010, 0.036], 0.004);
            ndh_plastic([0.22, 0.18, 0.10]) translate([s * 0.108, -0.160, 0.378]) rotate([90, 0, 0]) cylinder(h = 0.005, r = 0.007, center = true, $fn = 10);
            ndh_cloth(ndh_shade(c, 0.88)) translate([s * 0.110, -0.137, 0.080]) rotate([0, s * -16, 0]) ndh_box([0.110, 0.014, 0.120], 0.005);
            ndh_cloth(ndh_shade(c, 0.78)) translate([s * 0.120, -0.144, 0.128]) rotate([0, s * -16, 0]) ndh_box([0.112, 0.008, 0.030], 0.004);
        }
        ndh_cloth(ndh_shade(c, 0.85)) translate([0, 0, 0.170]) ndh_band(ndh_tw(0.17) + 0.006, ndh_td(0.17) + 0.006, 0.014);
    }
    if (kind == "gorka")
    {
        ndh_coat_shell(c);
        // 防风门襟（拉链外压一道大挡片）+ 拉链头
        ndh_cloth(ndh_shade(c, 0.9)) translate([0.012, -0.143, 0.25]) ndh_box([0.050, 0.012, 0.48], 0.004);
        ndh_metal([0.12, 0.12, 0.12]) translate([0.012, -0.151, 0.46]) cube([0.010, 0.006, 0.026], center = true);
        // 双肩加固片（前后连成一片过肩）+ 下摆加固带 + 两个竖插胸袋
        ndh_cloth(p, 0.88)
        {
            for (s = [-1, 1]) translate([s * 0.120, 0, 0.455]) ndh_box([0.17, 0.288, 0.085], 0.010);
            translate([0, 0, 0.030]) ndh_band(0.378, 0.268, 0.040);
            for (s = [-1, 1]) translate([s * 0.105, -0.144, 0.30]) ndh_box([0.075, 0.012, 0.130], 0.004);
        }
        ndh_metal([0.12, 0.12, 0.12]) for (s = [-1, 1]) translate([s * 0.105, -0.151, 0.36]) cube([0.006, 0.004, 0.010], center = true);
        // 折叠在后领的大兜帽 + 帽檐抽绳
        ndh_cloth(c) translate([0, 0.075, 0.500]) ndh_box([0.235, 0.18, 0.085], 0.025);
        ndh_cloth(p, 0.88) translate([0, 0.075, 0.543]) ndh_box([0.21, 0.16, 0.008], 0.003);
        ndh_cloth([0.12, 0.12, 0.11], 0.8) for (s = [-1, 1]) translate([s * 0.05, -0.146, 0.47]) cube([0.004, 0.004, 0.05], center = true);
    }
    if (kind == "strichtarn")
    {
        ndh_coat_shell(c);
        ndh_collar_and_straps(c, [0.18, 0.19, 0.15]);
        // 带盖胸袋 + 下摆斜插袋
        for (s = [-1, 1])
        {
            ndh_cloth(ndh_shade(c, 0.9)) translate([s * 0.105, -0.146, 0.335]) ndh_box([0.110, 0.014, 0.110], 0.005);
            ndh_cloth(ndh_shade(c, 0.8)) translate([s * 0.105, -0.153, 0.385]) ndh_box([0.118, 0.010, 0.035], 0.004);
            ndh_plastic([0.18, 0.19, 0.15]) translate([s * 0.105, -0.160, 0.378]) rotate([90, 0, 0]) cylinder(h = 0.005, r = 0.007, center = true, $fn = 10);
            ndh_cloth(ndh_shade(c, 0.85)) translate([s * 0.115, -0.137, 0.120]) rotate([0, s * 30, 0]) ndh_box([0.012, 0.012, 0.11], 0.004);
        }
        ndh_rain_torso(-1, 1);
        ndh_rain_torso(1, 2);
    }
    if (kind == "telnyashka")
    {
        // 贴身衫：比外套薄一圈，蓝白横条纹 + 圆领
        ndh_cloth(ndh_tel_white(), 0.85) hull()
        {
            translate([0, 0, 0.04]) ndh_box([0.355, 0.245, 0.13]);
            translate([0, 0, 0.37]) ndh_box([0.405, 0.262, 0.25]);
        }
        ndh_cloth(ndh_tel_blue(), 0.85) for (z = [0.000 : 0.036 : 0.47])
            let(w = z < 0.105 ? 0.355 : z > 0.245 ? 0.405 : 0.355 + (z - 0.105) / 0.14 * 0.05,
                d = z < 0.105 ? 0.245 : z > 0.245 ? 0.262 : 0.245 + (z - 0.105) / 0.14 * 0.017)
                translate([0, 0, z]) ndh_band(w + 0.003, d + 0.003, 0.016);
        ndh_cloth(ndh_tel_blue(), 0.85) translate([0, 0.012, 0.497]) difference()
        {
            ndh_band(0.20, 0.17, 0.012, 0.03);
            cube([0.15, 0.12, 0.05], center = true);
        }
    }
}

module ndh_sleeve(kind, forearm = false)
{
    len = forearm ? 0.29 : 0.31;
    width = forearm ? 0.137 : 0.158;
    depth = forearm ? 0.147 : 0.168;
    w = width - 0.012;
    d = depth - 0.012;
    c = kind == "afganka" ? ndh_sand() : kind == "gorka" ? ndh_gorka() : ndh_stein();
    $fn = 10;
    if (kind == "telnyashka")
    {
        ndh_cloth(ndh_tel_white(), 0.85) translate([0, 0, -len / 2]) ndh_box([w, d, len + 0.018]);
        ndh_cloth(ndh_tel_blue(), 0.85) for (z = [-0.020 : -0.036 : -len + 0.01])
            translate([0, 0, z]) ndh_band(w + 0.003, d + 0.003, 0.016);
        if (forearm) ndh_cloth(ndh_tel_white(), 0.8) translate([0, 0, -len + 0.016]) ndh_box([w + 0.004, d + 0.004, 0.030], 0.006);
    }
    else
    {
        ndh_cloth(c) translate([0, 0, -len / 2]) ndh_box([width, depth, len + 0.018]);
        if (kind == "gorka")
        {
            // 肩头 / 肘部加固片，松紧袖口
            ndh_cloth(ndh_gorka_patch(), 0.88)
            {
                if (!forearm) translate([0, 0, -0.040]) ndh_box([width + 0.008, depth + 0.008, 0.070], 0.010);
                if (!forearm) translate([0, 0, -0.270]) ndh_box([width + 0.008, depth + 0.008, 0.075], 0.010);
                if (forearm) translate([0, 0, -0.050]) ndh_box([width + 0.008, depth + 0.008, 0.090], 0.010);
            }
            if (forearm) ndh_cloth(ndh_shade(ndh_gorka(), 0.75)) translate([0, 0, -len + 0.020]) ndh_box([width - 0.004, depth - 0.004, 0.036], 0.008);
        }
        if (kind == "afganka")
        {
            if (forearm)
            {
                ndh_cloth(ndh_shade(c, 0.85)) translate([0, 0, -len + 0.022]) ndh_box([width + 0.004, depth + 0.004, 0.036], 0.006);
                ndh_plastic([0.22, 0.18, 0.10]) translate([0.03, -depth / 2 - 0.004, -len + 0.022]) rotate([90, 0, 0]) cylinder(h = 0.004, r = 0.006, center = true, $fn = 10);
            }
            else
                ndh_cloth(ndh_shade(c, 0.88)) translate([width / 2 + 0.003, -0.01, -0.12]) ndh_box([0.010, 0.085, 0.095], 0.004);   // 袖袋
        }
        if (kind == "strichtarn")
        {
            ndh_rain_box(width, depth, len, forearm ? 3 : 4, forearm ? 8 : 9);
            if (forearm) ndh_cloth(ndh_shade(c, 0.85)) translate([0, 0, -len + 0.022]) ndh_box([width + 0.004, depth + 0.004, 0.036], 0.006);
        }
    }
}

// ================= 背心 =================
// 56 式胸挂：三个长弹匣袋 + 两侧各两个手榴弹袋 + 肩带 + 背后交叉带 + 腰侧系带
module ndh_type56()
{
    $fn = 10;
    k = [0.46, 0.43, 0.27];
    s2 = [0.36, 0.33, 0.20];
    ndh_cloth(k, 0.94)
    {
        translate([0, -0.190, 0.245]) ndh_box([0.36, 0.030, 0.150], 0.008);                 // 袋组底板
        for (x = [-0.058, 0, 0.058])
        {
            translate([x, -0.228, 0.235]) ndh_box([0.054, 0.050, 0.150], 0.008);            // 长弹匣袋
        }
        for (x = [-0.150, -0.105, 0.105, 0.150])
            translate([x, -0.222, 0.208]) ndh_box([0.042, 0.042, 0.090], 0.008);            // 手榴弹袋
    }
    ndh_cloth(ndh_shade(k, 0.82), 0.9)
    {
        for (x = [-0.058, 0, 0.058]) translate([x, -0.246, 0.302]) ndh_box([0.058, 0.020, 0.036], 0.006);   // 袋盖
        for (x = [-0.150, -0.105, 0.105, 0.150]) translate([x, -0.236, 0.245]) ndh_box([0.046, 0.016, 0.026], 0.005);
    }
    ndh_plastic([0.20, 0.16, 0.09], 0.5) for (x = [-0.058, 0, 0.058]) translate([x, -0.257, 0.296]) rotate([90, 0, 0]) cylinder(h = 0.005, r = 0.006, center = true, $fn = 10);
    ndh_cloth(s2, 0.9)
    {
        for (s = [-1, 1])
        {
            translate([s * 0.130, -0.182, 0.395]) ndh_box([0.036, 0.016, 0.170], 0.004);    // 前肩带
            translate([s * 0.133, 0, 0.482]) ndh_box([0.040, 0.360, 0.022], 0.005);        // 过肩
            translate([s * 0.222, 0, 0.240]) ndh_box([0.020, 0.300, 0.034], 0.004);        // 腰侧系带
            // 背后交叉带
            hull()
            {
                translate([s * 0.130, 0.168, 0.465]) cube([0.036, 0.012, 0.010], center = true);
                translate([-s * 0.150, 0.152, 0.215]) cube([0.036, 0.012, 0.010], center = true);
            }
        }
        translate([0, 0.150, 0.240]) ndh_box([0.34, 0.012, 0.034], 0.004);                 // 背后横带
    }
}

// 6B3TM-01：橄榄色外罩前后片 + 叠压钛板的横向鼓包 + 胸前四个 AK 弹匣袋 + 护领 + 侧面束带与扣具
module ndh_6b3()
{
    o = [0.27, 0.30, 0.18];
    ndh_cloth(o, 0.86)
    {
        translate([0, -0.184, 0.31]) ndh_box([0.36, 0.056, 0.310]);
        translate([0, 0.174, 0.31]) ndh_box([0.34, 0.052, 0.310]);
        for (s = [-1, 1])
        {
            translate([s * 0.212, 0, 0.25]) ndh_box([0.028, 0.34, 0.130]);
            translate([s * 0.133, 0, 0.480]) ndh_box([0.056, 0.360, 0.034]);
        }
        // 护领：后颈到两肩的一圈立领
        translate([0, 0.090, 0.512]) ndh_box([0.25, 0.060, 0.040], 0.012);
        for (s = [-1, 1]) translate([s * 0.105, 0.020, 0.506]) rotate([0, 0, s * 28]) ndh_box([0.050, 0.120, 0.032], 0.010);
    }
    // 钛板鼓包：前胸上部 + 背部，每排一条横向凸起
    ndh_cloth(ndh_shade(o, 1.08), 0.82)
    {
        for (z = [0.390, 0.430]) translate([0, -0.215, z]) ndh_box([0.33, 0.010, 0.034], 0.006);
        for (z = [0.200, 0.250, 0.300, 0.350, 0.400]) translate([0, 0.203, z]) ndh_box([0.31, 0.010, 0.040], 0.006);
    }
    // 四个弹匣袋 + 袋盖 + 盖上的扣
    ndh_cloth(ndh_shade(o, 0.85), 0.88)
        for (x = [-0.126, -0.042, 0.042, 0.126]) translate([x, -0.232, 0.250]) ndh_box([0.074, 0.040, 0.140], 0.008);
    ndh_cloth(ndh_shade(o, 0.72), 0.86)
        for (x = [-0.126, -0.042, 0.042, 0.126]) translate([x, -0.254, 0.308]) ndh_box([0.078, 0.010, 0.040], 0.004);
    ndh_metal([0.20, 0.21, 0.19], 0.5) for (x = [-0.126, -0.042, 0.042, 0.126])
        translate([x, -0.261, 0.298]) cube([0.016, 0.004, 0.010], center = true);
    // 侧面束带 + 快拆扣
    ndh_cloth(ndh_web(), 0.8) for (s = [-1, 1], z = [0.20, 0.30]) translate([s * 0.228, 0, z]) ndh_box([0.006, 0.300, 0.026], 0.002);
    ndh_metal([0.18, 0.18, 0.17], 0.45) for (s = [-1, 1], z = [0.20, 0.30]) translate([s * 0.233, -0.12, z]) cube([0.006, 0.030, 0.030], center = true);
}

// ================= 下装 =================
// kind: "afganka"（基尔扎人造革高靴）/ "gorka"（系带作战靴）/ "strichtarn"（高筒皮行军靴）
function ndh_pants_c(kind) = kind == "afganka" ? ndh_sand() : kind == "gorka" ? ndh_gorka() : ndh_stein();
function ndh_boot_c(kind) = kind == "afganka" ? [0.075, 0.068, 0.060] : kind == "gorka" ? [0.12, 0.10, 0.075] : [0.045, 0.045, 0.048];
function ndh_boot_r(kind) = kind == "afganka" ? 0.42 : kind == "gorka" ? 0.65 : 0.28;
// 高靴筒到小腿顶多高（calf 局部坐标，0 = 膝）
function ndh_shaft(kind) = kind == "afganka" ? -0.12 : kind == "strichtarn" ? -0.10 : -0.33;

module ndh_pelvis(kind)
{
    $fn = 10;
    c = ndh_pants_c(kind);
    ndh_cloth(c) ndh_box([0.375, 0.245, 0.235]);
    if (kind == "afganka")
    {
        // 苏军士兵皮腰带 + 黄铜星徽扣
        ndh_leather([0.33, 0.20, 0.10], 0.5) translate([0, 0, 0.08]) ndh_band(0.387, 0.257, 0.040);
        ndh_brass() translate([0, -0.131, 0.08]) cube([0.060, 0.008, 0.046], center = true);
        ndh_paint([0.55, 0.40, 0.16], 0.35) translate([0, -0.1355, 0.08]) rotate([90, 0, 0])
            linear_extrude(height = 0.002, center = true)
                polygon([for (i = [0 : 9]) let(a = 90 + i * 36, rr = i % 2 == 0 ? 0.016 : 0.0065) [rr * cos(a), rr * sin(a)]]);
    }
    else if (kind == "strichtarn")
    {
        ndh_leather([0.06, 0.055, 0.05], 0.4) translate([0, 0, 0.08]) ndh_band(0.387, 0.257, 0.040);
        ndh_metal([0.55, 0.56, 0.57], 0.35) translate([0, -0.131, 0.08]) cube([0.052, 0.007, 0.042], center = true);
        translate([0, 0, 0.06]) ndh_rain_box(0.375, 0.245, 0.17, 7, 4);
    }
    else
    {
        // 戈尔卡：松紧腰 + 臀部加固片 + 背带扣
        ndh_cloth(ndh_shade(c, 0.85)) translate([0, 0, 0.085]) ndh_band(0.383, 0.253, 0.030);
        ndh_cloth(ndh_gorka_patch(), 0.88) translate([0, 0.124, -0.035]) ndh_box([0.30, 0.008, 0.14], 0.006);
        ndh_metal([0.15, 0.15, 0.14], 0.45) for (s = [-1, 1], y = [-1, 1]) translate([s * 0.09, y * 0.128, 0.09]) cube([0.020, 0.006, 0.014], center = true);
    }
}

module ndh_leg(kind, calf = false)
{
    len = calf ? 0.42 : 0.44;
    width = calf ? 0.165 : 0.19;
    depth = calf ? 0.18 : 0.205;
    c = ndh_pants_c(kind);
    shaft = ndh_shaft(kind);
    top = -shaft;
    if (!calf)
    {
        ndh_cloth(c) translate([0, 0, -len / 2]) ndh_box([width, depth, len + 0.018]);
        if (kind == "afganka" || kind == "strichtarn")
        {
            // 大腿外侧大口袋 + 袋盖
            ndh_cloth(ndh_shade(c, 0.86)) translate([width / 2 + 0.008, 0.010, -0.215]) ndh_box([0.020, 0.140, 0.150], 0.005);
            ndh_cloth(ndh_shade(c, 0.76)) translate([width / 2 + 0.016, 0.010, -0.145]) ndh_box([0.010, 0.146, 0.034], 0.004);
        }
        if (kind == "afganka") ndh_cloth(ndh_shade(c, 0.9)) translate([0, -depth / 2 - 0.004, -0.40]) ndh_box([0.150, 0.010, 0.090], 0.006);   // 护膝
        if (kind == "gorka") ndh_cloth(ndh_gorka_patch(), 0.88) translate([0, -depth / 2 - 0.004, -0.38]) ndh_box([0.172, 0.012, 0.120], 0.006);
        if (kind == "strichtarn") ndh_rain_box(width, depth, len, 5, 10);
    }
    else
    {
        // 裤腿（到靴筒顶）+ 靴筒
        ndh_cloth(c) translate([0, 0, -top / 2 + 0.009]) ndh_box([width, depth, top + 0.018]);
        ndh_leather(ndh_boot_c(kind), ndh_boot_r(kind)) translate([0, 0, (shaft - len) / 2])
            ndh_box([width + 0.006, depth + 0.006, len + shaft + 0.004], 0.010);
        if (kind == "gorka")
        {
            ndh_cloth(ndh_gorka_patch(), 0.88) translate([0, -depth / 2 - 0.004, -0.050]) ndh_box([0.150, 0.012, 0.090], 0.006);
            ndh_cloth(ndh_shade(c, 0.78)) translate([0, 0, shaft + 0.012]) ndh_box([width + 0.010, depth + 0.010, 0.026], 0.006);   // 裤口
            // 系带靴的鞋带格
            for (z = [shaft - 0.025 : -0.022 : -len + 0.02]) ndh_cloth([0.05, 0.045, 0.04], 0.8) translate([0, -depth / 2 - 0.005, z]) cube([0.050, 0.004, 0.006], center = true);
        }
        else
            // 高靴筒口的翻边
            ndh_leather(ndh_shade(ndh_boot_c(kind), 1.3), ndh_boot_r(kind)) translate([0, 0, shaft - 0.010]) ndh_box([width + 0.012, depth + 0.012, 0.022], 0.006);
        if (kind == "strichtarn") ndh_rain_box(width, depth, top, 6, 3);
    }
}

module ndh_boot(kind)
{
    b = ndh_boot_c(kind);
    r = ndh_boot_r(kind);
    ndh_leather(b, r) translate([0, -0.07, 0.065]) ndh_box([0.175, 0.315, 0.13]);
    ndh_leather(b, r) translate([0, 0.005, 0.14]) ndh_box([0.163, 0.163, 0.13]);
    // 鞋底 + 鞋跟
    ndh_cloth([0.05, 0.05, 0.048], 0.85) translate([0, -0.07, 0.012]) ndh_box([0.181, 0.322, 0.024], 0.005);
    ndh_cloth([0.05, 0.05, 0.048], 0.85) translate([0, 0.040, 0.030]) ndh_box([0.172, 0.085, 0.028], 0.005);
    if (kind == "gorka")
        for (z = [0.090, 0.115, 0.140, 0.165]) ndh_cloth([0.05, 0.045, 0.04], 0.8) translate([0, -0.088, z]) cube([0.07, 0.010, 0.007], center = true);
}

// ================= 背包 =================
// RD-54：方正帆布主袋 + 两侧弹匣袋 + 前下袋 + 顶盖双扣带 + 侧挂 MPL-50 工兵锹
module ndh_rd54()
{
    $fn = 12;
    o = [0.28, 0.29, 0.17];
    w = 0.30; h = 0.38; d = 0.15;
    ctr = [0, 0.21 + d / 2, 0.29];
    ndh_cloth(o, 0.94) translate(ctr) ndh_box([w, d, h], 0.022);
    ndh_cloth(ndh_shade(o, 0.85), 0.92)
    {
        translate(ctr + [0, d / 2 + 0.025, -0.075]) ndh_box([w * 0.80, 0.050, h * 0.42], 0.012);              // 前下袋
        for (s = [-1, 1]) translate(ctr + [s * (w / 2 + 0.022), 0.010, -0.050]) ndh_box([0.044, d * 0.75, h * 0.48], 0.010);   // 侧袋
        translate(ctr + [0, 0.012, h / 2 - 0.035]) ndh_box([w + 0.008, d + 0.026, 0.080], 0.012);              // 顶盖
    }
    ndh_cloth(ndh_web(), 0.85) for (s = [-1, 1]) translate(ctr + [s * 0.08, d / 2 + 0.014, 0.08]) cube([0.026, 0.008, 0.20], center = true);
    ndh_metal([0.30, 0.30, 0.28], 0.45) for (s = [-1, 1]) translate(ctr + [s * 0.08, d / 2 + 0.020, 0.0]) cube([0.032, 0.006, 0.022], center = true);
    // MPL-50 工兵锹：木柄 + 军绿漆锹面，斜挂在左侧袋外
    translate(ctr + [w / 2 + 0.050, 0.010, 0.12]) rotate([0, -6, 0])
    {
        ndh_cloth([0.45, 0.30, 0.16], 0.6) cylinder(h = 0.24, r = 0.015, center = true);
        ndh_paint([0.24, 0.26, 0.15], 0.6) translate([0, 0, -0.17]) ndh_box([0.012, 0.13, 0.12], 0.004);
    }
}

// 阿巴拉科夫背包：宽而低、鼓胀的米色帆布袋 + 两侧大袋 + 前大袋 + 抽绳顶盖 + 皮带与钢扣
module ndh_abalakov()
{
    $fn = 14;
    c = [0.55, 0.50, 0.35];
    lea = [0.32, 0.18, 0.08];
    w = 0.40; h = 0.34; d = 0.20;
    ctr = [0, 0.21 + d / 2, 0.22];
    ndh_cloth(c, 0.95) hull()
    {
        translate(ctr + [0, 0, -h / 2 + 0.05]) ndh_box([w, d, 0.10], 0.04);
        translate(ctr + [0, -0.01, h / 2 - 0.06]) ndh_box([w * 0.86, d * 0.82, 0.12], 0.04);
    }
    ndh_cloth(ndh_shade(c, 0.9), 0.95)
    {
        for (s = [-1, 1]) translate(ctr + [s * (w / 2 + 0.020), 0.0, -0.045]) ndh_box([0.050, d * 0.70, h * 0.55], 0.018);
        translate(ctr + [0, d / 2 + 0.028, -0.040]) ndh_box([w * 0.62, 0.060, h * 0.52], 0.018);
        translate(ctr + [0, 0.01, h / 2 - 0.004]) ndh_box([w * 0.80, d * 0.86, 0.030], 0.010);     // 顶盖
    }
    ndh_leather(lea, 0.6)
    {
        for (s = [-1, 1]) translate(ctr + [s * 0.075, d / 2 + 0.060, 0.02]) cube([0.024, 0.008, 0.26], center = true);
        for (s = [-1, 1]) translate(ctr + [s * (w / 2 + 0.047), 0.0, -0.02]) cube([0.006, 0.022, 0.16], center = true);
    }
    ndh_metal([0.42, 0.42, 0.40], 0.4) for (s = [-1, 1]) translate(ctr + [s * 0.075, d / 2 + 0.065, -0.07]) cube([0.032, 0.005, 0.024], center = true);
    ndh_cloth([0.25, 0.22, 0.15], 0.9) translate(ctr + [0, -0.01, h / 2 + 0.014]) cylinder(h = 0.012, r = 0.012);
}

// 皮革双肩带（阿巴拉科夫）
module ndh_leather_harness()
{
    for (s = [-1, 1])
    {
        ndh_leather([0.32, 0.18, 0.08], 0.6)
        {
            translate([s * 0.140, -0.222, 0.32]) ndh_box([0.040, 0.018, 0.34], 0.006);
            translate([s * 0.140, 0.0, 0.497]) ndh_box([0.040, 0.445, 0.024], 0.006);
        }
        ndh_metal([0.42, 0.42, 0.40], 0.4) translate([s * 0.140, -0.236, 0.23]) ndh_box([0.046, 0.012, 0.030], 0.004);
    }
}

// ================= 地面 loot =================
// 叠好的外套：底面贴 z = 0
module ndh_loot_coat(kind)
{
    $fn = 10;
    c = kind == "afganka" ? ndh_sand() : kind == "gorka" ? ndh_gorka() : kind == "strichtarn" ? ndh_stein() : ndh_tel_white();
    ndh_cloth(c) translate([0, 0, 0.04]) ndh_box([0.32, 0.29, 0.08]);
    ndh_cloth(ndh_shade(c, 0.82)) translate([0.10, 0, 0.075]) ndh_box([0.105, 0.27, 0.04]);
    if (kind == "telnyashka")
        ndh_cloth(ndh_tel_blue(), 0.85) for (y = [-0.120 : 0.036 : 0.13]) translate([0, y, 0.0815]) cube([0.30, 0.016, 0.003], center = true);
    if (kind == "afganka" || kind == "strichtarn")
    {
        for (x = [-0.10, 0.0]) ndh_cloth(ndh_shade(c, 0.88)) translate([x, 0.05, 0.083]) ndh_box([0.08, 0.08, 0.010], 0.003);
        for (x = [-0.10, 0.0]) ndh_plastic([0.20, 0.17, 0.10]) translate([x, 0.075, 0.089]) cylinder(h = 0.004, r = 0.007, center = true, $fn = 10);
        ndh_cloth(ndh_shade(c, 0.9)) translate([0, 0.135, 0.085]) ndh_box([0.20, 0.035, 0.012], 0.004);   // 领子
    }
    if (kind == "gorka")
    {
        ndh_cloth(ndh_gorka_patch(), 0.88) translate([-0.04, -0.09, 0.082]) ndh_box([0.20, 0.08, 0.006], 0.003);
        ndh_cloth(c) translate([0, 0.10, 0.10]) ndh_box([0.20, 0.12, 0.05], 0.015);   // 卷起的兜帽
    }
    if (kind == "strichtarn")
        ndh_cloth(ndh_rain(), 0.9) for (row = [0 : 7], col = [0 : 8])
            let(i = row * 9 + col, x = -0.14 + col * 0.032 + ndh_rndf(i) * 0.01, y = -0.12 + row * 0.033)
                if (ndh_rnd(i + 3, 10) < 6 && x < 0.045) translate([x, y, 0.0812]) cube([0.0045, 0.018, 0.003], center = true);
}

// 叠好的长裤：两条裤腿并排 + 腰带
module ndh_loot_pants(kind)
{
    c = ndh_pants_c(kind);
    for (s = [-1, 1]) ndh_cloth(c) translate([s * 0.07, 0, 0.04]) ndh_box([0.135, 0.32, 0.08]);
    if (kind == "afganka")
    {
        ndh_leather([0.33, 0.20, 0.10], 0.5) translate([0, 0.12, 0.085]) ndh_box([0.28, 0.04, 0.012], 0.004);
        ndh_brass() translate([0.0, 0.12, 0.093]) cube([0.050, 0.038, 0.006], center = true);
    }
    if (kind == "gorka")
        for (s = [-1, 1]) ndh_cloth(ndh_gorka_patch(), 0.88) translate([s * 0.07, -0.06, 0.082]) ndh_box([0.10, 0.10, 0.006], 0.003);
    if (kind == "strichtarn")
    {
        ndh_leather([0.06, 0.055, 0.05], 0.4) translate([0, 0.12, 0.085]) ndh_box([0.28, 0.04, 0.012], 0.004);
        ndh_cloth(ndh_rain(), 0.9) for (row = [0 : 8], col = [0 : 7])
            let(i = row * 8 + col + 77, x = -0.125 + col * 0.034 + ndh_rndf(i) * 0.01, y = -0.14 + row * 0.03)
                if (ndh_rnd(i + 3, 10) < 6 && y < 0.09) translate([x, y, 0.0812]) cube([0.0045, 0.018, 0.003], center = true);
    }
    // 靴子横放在裤子旁
    ndh_leather(ndh_boot_c(kind), ndh_boot_r(kind)) translate([0, -0.22, 0.04]) ndh_box([0.26, 0.10, 0.08], 0.010);
}
