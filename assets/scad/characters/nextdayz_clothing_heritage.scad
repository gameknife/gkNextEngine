// nextdayz_clothing_heritage.scad —— NextDayz 有史可考的服装（参考 innawoods.net 的手绘图鉴挑选）
// 苏军阿富汗战争：巴拿马帽 / SSh-68 钢盔 / 阿富汗卡上衣与长裤（基尔扎人造革靴）/ 56 式胸挂 /
//                  6B3 防弹衣 / RD-54 伞兵背包 / 海魂衫
// 山地部队与猎人：戈尔卡罩衫与长裤 / 阿巴拉科夫背包
// 东德国家人民军：M56 钢盔 / 雨点迷彩上衣与长裤（高筒行军靴）
// 迷彩军服：M81 林地 BDU / EMR "数码" / DBDU 六色沙漠 / VSR-98 "植物" + PASGT 头盔 / EMR 奔尼帽
//
// 只提供几何模块，不含 bone_*：nextdayz_clothing.scad（穿戴，按骨骼局部坐标）与
// nextdayz_loot_items.scad（地面 loot）各自用 bone_<item id>__<bone> / bone_item_<id> 调用这里。
// 坐标与 nextdayz_clothing.scad 完全一致（米，Z 向上，前方 = -Y），并沿用它的分层包络：
//   皮肤 < 外套（厚 .28）< 背心（后 .20 / 前 -.21）< 背包（前 .21）。
//
// 一件衣服 = 剪裁（cut：口袋、领子、加固片的几何）× 面料（fabric：颜色 + 质感）。
// 材质分层（PBR）：
//   * 布料一律是漫反射（roughness = 1, metalness = 0 → Lambertian）。粗糙度 < 1 会给布料叠一层
//     电介质高光，看起来像塑料。布料的质感靠 gk_texture()：斜纹 twill / 帆布 canvas 织纹，
//     迷彩是印在斜纹上的过程纹理（woodland / digital / desert / flora / rain）。
//   * 只有皮革、橡胶、漆面、塑料扣、金属件带高光，各自用不同的粗糙度 / 金属度。
// 每个部件不超过 16 种材质（颜色 × 粗糙度 × 金属度 × 纹理各算一种）。本文件不 use 其他服装文件。

// ---------------- 材质 ----------------
// 布料：漫反射；weave 非空时叠一层织纹（最终颜色 = c × 织纹明暗）
module ndh_cloth(c, weave = "", s = 0.12)
{
    if (weave == "") gk_material(c, roughness = 1, metalness = 0) children();
    else gk_material(c, roughness = 1, metalness = 0) gk_texture(weave, scale = s) children();
}
// 迷彩印花：白底材质 × 印花（tint < 1 用于压暗口袋盖等叠层）
module ndh_print(pattern, s, tint = 1, seed = 0)
    gk_material([tint, tint, tint], roughness = 1, metalness = 0) gk_texture(pattern, scale = s, seed = seed) children();
module ndh_leather(c, r = 0.55, grain = true)
{
    if (grain) gk_material(c, roughness = r, metalness = 0) gk_texture("grain", scale = 0.08) children();
    else gk_material(c, roughness = r, metalness = 0) children();
}
module ndh_rubber(c = [0.05, 0.05, 0.048]) { gk_material(c, roughness = 0.85, metalness = 0) children(); }
module ndh_metal(c = [0.30, 0.30, 0.29], r = 0.42) { gk_material(c, roughness = r, metalness = 0.9) children(); }
module ndh_brass() { gk_material([0.74, 0.56, 0.26], roughness = 0.32, metalness = 1.0) children(); }
module ndh_paint(c, r = 0.72) { gk_material(c, roughness = r, metalness = 0) children(); }      // 钢盔哑光漆
module ndh_enamel(c) { gk_material(c, roughness = 0.25, metalness = 0) children(); }          // 帽徽搪瓷
module ndh_plastic(c, r = 0.45) { gk_material(c, roughness = r, metalness = 0) children(); }

function ndh_sand() = [0.50, 0.42, 0.27];          // 阿富汗卡：晒褪的沙卡其
function ndh_gorka() = [0.35, 0.35, 0.22];         // 戈尔卡：帐篷布卡其
function ndh_gorka_patch() = [0.27, 0.20, 0.12];   // 戈尔卡：棕色加固片
function ndh_tel_white() = [0.80, 0.80, 0.78];
function ndh_tel_blue() = [0.14, 0.27, 0.56];
function ndh_web() = [0.09, 0.10, 0.095];
function ndh_shade(c, k = 0.8) = [c[0] * k, c[1] * k, c[2] * k];

// 面料：fabric 名 → 漫反射本色 + 织纹 / 迷彩印花。shade < 1 压暗叠层（口袋盖、门襟、领子）。
//   afganka  沙卡其斜纹布          gorka  卡其帐篷帆布        gorka_patch  棕色加固帆布
//   m81      M81 林地迷彩          emr    EMR 数码迷彩         dbdu  六色沙漠迷彩
//   flora    VSR-98 植物迷彩       strichtarn  东德雨点迷彩    tel   海魂衫白底针织
module ndh_fabric(fabric, shade = 1)
{
    if (fabric == "afganka") ndh_cloth(ndh_shade(ndh_sand(), shade), "twill", 0.10) children();
    else if (fabric == "gorka") ndh_cloth(ndh_shade(ndh_gorka(), shade), "canvas", 0.12) children();
    else if (fabric == "gorka_patch") ndh_cloth(ndh_shade(ndh_gorka_patch(), shade), "canvas", 0.12) children();
    else if (fabric == "m81") ndh_print("woodland", 0.55, shade) children();
    else if (fabric == "emr") ndh_print("digital", 0.50, shade) children();
    else if (fabric == "dbdu") ndh_print("desert", 0.60, shade) children();
    else if (fabric == "flora") ndh_print("flora", 0.50, shade) children();
    else if (fabric == "strichtarn") ndh_print("rain", 0.40, shade) children();
    else ndh_cloth(ndh_shade(ndh_tel_white(), shade)) children();
}
// 纽扣颜色随面料
function ndh_button(fabric) = fabric == "dbdu" ? [0.30, 0.24, 0.16] : fabric == "strichtarn" ? [0.18, 0.19, 0.15]
                            : fabric == "afganka" ? [0.22, 0.18, 0.10] : [0.12, 0.13, 0.10];

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

module ndh_coat_shell(fabric)
{
    ndh_fabric(fabric) hull()
    {
        translate([0, 0, 0.04]) ndh_box([0.37, 0.26, 0.13]);
        translate([0, 0, 0.37]) ndh_box([0.425, 0.28, 0.25]);
    }
}

// 翻领 + 肩章 + 门襟纽扣（阿富汗卡 / BDU / 东德野战服共用）
module ndh_collar_and_straps(fabric, epaulets = true)
{
    $fn = 10;
    for (s = [-1, 1])
        ndh_fabric(fabric, 0.9) translate([s * 0.100, -0.035, 0.507]) rotate([0, s * 18, s * -14]) ndh_box([0.085, 0.13, 0.022], 0.006);
    ndh_fabric(fabric, 0.9) translate([0, 0.085, 0.507]) ndh_box([0.20, 0.05, 0.028], 0.006);
    if (epaulets)
    {
        for (s = [-1, 1]) ndh_fabric(fabric, 0.86) translate([s * 0.150, 0.0, 0.497]) ndh_box([0.050, 0.15, 0.012], 0.004);
        for (s = [-1, 1]) ndh_plastic(ndh_button(fabric)) translate([s * 0.150, -0.060, 0.505]) cylinder(h = 0.006, r = 0.008, center = true);
    }
    ndh_fabric(fabric, 0.92) translate([0, -0.142, 0.25]) ndh_box([0.040, 0.010, 0.47], 0.003);
    for (z = [0.07, 0.18, 0.29, 0.40]) ndh_plastic(ndh_button(fabric)) translate([0, -0.149, z]) rotate([90, 0, 0]) cylinder(h = 0.006, r = 0.008, center = true);
}

// 带盖贴袋：中心 (x, z)，宽 w 高 h，压在外套前面（y = -0.146）；angle 绕 Y 斜放
module ndh_patch_pocket(fabric, x, z, w, h, angle = 0, button = true)
{
    $fn = 10;
    ndh_fabric(fabric, 0.9) translate([x, -0.146, z]) rotate([0, angle, 0]) ndh_box([w, 0.016, h], 0.005);
    ndh_fabric(fabric, 0.8) translate([x, -0.153, z + h / 2 - 0.012]) rotate([0, angle, 0]) ndh_box([w + 0.006, 0.010, 0.034], 0.004);
    if (button) ndh_plastic(ndh_button(fabric)) translate([x, -0.160, z + h / 2 - 0.018]) rotate([90, 0, 0]) cylinder(h = 0.005, r = 0.007, center = true);
}

// ================= 头部 =================
// 巴拿马帽：圆顶 + 下斜宽檐 + 帽箍 + 两侧通风孔 + 正面红星（斜纹棉布）
module ndh_panama()
{
    $fn = 20;
    c = [0.56, 0.48, 0.31];
    ndh_cloth(c, "twill", 0.10)
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
    // 帽檐车缝线（同心圈）+ 帽箍
    ndh_cloth(ndh_shade(c, 0.86)) for (r = [0.175, 0.205])
        translate([0, 0.005, 0.168 + 0.022 * (0.232 - r) / 0.082 + 0.0008]) scale([1, 1.06, 1]) difference()
        {
            cylinder(h = 0.0022, r = r + 0.003);
            translate([0, 0, -0.01]) cylinder(h = 0.03, r = r);
        }
    ndh_cloth(ndh_shade(c, 0.72), "twill", 0.10) translate([0, 0.005, 0.192]) scale([1, 1.06, 1]) difference()
    {
        cylinder(h = 0.020, r = 0.1435);
        translate([0, 0, -0.01]) cylinder(h = 0.04, r = 0.130);
    }
    ndh_metal([0.22, 0.22, 0.20]) for (s = [-1, 1], a = [-12, 12])
        translate([s * 0.134 * cos(a), 0.005 + 0.134 * 1.06 * sin(a), 0.25]) rotate([0, 90, 0]) cylinder(h = 0.012, r = 0.0055, center = true, $fn = 10);
    ndh_enamel([0.62, 0.07, 0.04]) translate([0, -0.1445, 0.232]) rotate([90, 0, 0])
        linear_extrude(height = 0.004, center = true)
            polygon([for (i = [0 : 9]) let(a = 90 + i * 36, rr = i % 2 == 0 ? 0.018 : 0.0075) [rr * cos(a), rr * sin(a)]]);
    ndh_brass() translate([0, -0.1475, 0.232]) rotate([90, 0, 0]) cylinder(h = 0.002, r = 0.0045, center = true, $fn = 10);
}

// EMR 奔尼帽：数码迷彩圆筒帽冠 + 软宽檐 + 叶饰挂环带 + 下颚绳（loot 时不带）
module ndh_boonie_emr(strap = true)
{
    $fn = 20;
    ndh_fabric("emr")
    {
        hull()
        {
            translate([0, 0.005, 0.180]) scale([1, 1.08, 1]) cylinder(h = 0.05, r = 0.138);
            translate([0, 0.005, 0.282]) scale([1, 1.08, 0.40]) sphere(r = 0.132);
        }
        translate([0, 0.005, 0.168]) scale([1, 1.08, 1]) difference()
        {
            cylinder(h = 0.016, r1 = 0.215, r2 = 0.160);
            translate([0, 0, -0.01]) cylinder(h = 0.05, r = 0.120);
        }
    }
    // 叶饰挂环带：一圈压暗布带 + 竖向挂环
    ndh_fabric("emr", 0.78) translate([0, 0.005, 0.205]) scale([1, 1.08, 1]) difference()
    {
        cylinder(h = 0.030, r = 0.1425);
        translate([0, 0, -0.01]) cylinder(h = 0.06, r = 0.130);
    }
    ndh_fabric("emr", 0.7) for (a = [0 : 30 : 330])
        translate([0.1435 * cos(a), 0.005 + 0.1435 * 1.08 * sin(a), 0.220]) rotate([0, 0, a]) cube([0.004, 0.012, 0.034], center = true);
    if (strap)
        ndh_cloth([0.08, 0.08, 0.07]) for (s = [-1, 1])
            translate([s * 0.122, 0.0, 0.10]) rotate([0, s * 10, 0]) cylinder(h = 0.14, r = 0.0025, center = true, $fn = 6);
}

// SSh-68：近似半球的冲压钢盔 + 一圈外翻短檐 + 两侧铆钉 + 棕色皮下颚带（loot 时不带）
// 漆面哑光（漫反射为主，只留一点漆面高光）；檐口磨出钢色。
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
    ndh_paint(ndh_shade(olive, 0.92)) translate([0, 0.005, 0.158]) scale([1, 1.07, 1]) difference()
    {
        cylinder(h = 0.016, r1 = 0.200, r2 = 0.187);
        translate([0, 0, -0.01]) cylinder(h = 0.04, r = 0.176);
    }
    ndh_metal([0.36, 0.36, 0.34], 0.45) translate([0, 0.005, 0.157]) scale([1, 1.07, 1]) difference()
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
    ndh_paint(grey, 0.75) scale([1, 1.08, 1]) rotate_extrude()
        polygon([[0, 0.336], [0.070, 0.332], [0.128, 0.316], [0.164, 0.286], [0.180, 0.246], [0.188, 0.204],
                 [0.204, 0.166], [0.226, 0.134], [0.230, 0.124], [0.218, 0.124], [0.196, 0.150],
                 [0.176, 0.190], [0.170, 0.240], [0, 0.240]]);
    ndh_paint(ndh_shade(grey, 0.8), 0.65) scale([1, 1.08, 1]) difference()
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

// PASGT（"弗里茨"）：凯夫拉盔壳罩 M81 林地布罩——额前短檐、两侧压低护耳、后沿压低护颈；
// 一圈橄榄色松紧"猫眼"带（后面两块荧光片）+ 下颚带（loot 时不带）。布罩漫反射，猫眼片微亮。
module ndh_pasgt(strap = true)
{
    $fn = 28;
    ndh_fabric("m81") difference()
    {
        translate([0, 0.006, 0.168]) scale([1, 1.10, 0.95]) sphere(r = 0.192);
        translate([0, 0.006, 0.168]) scale([1, 1.10, 0.95]) sphere(r = 0.180);
        translate([0, 0, -0.10]) cube([0.6, 0.6, 0.40], center = true);         // 两侧与后沿 z = 0.10
        translate([0, -0.24, 0.05]) cube([0.6, 0.30, 0.28], center = true);    // 额前抬高到 z = 0.19
    }
    // 额前短檐：贴着前沿的一段弧形窄檐
    ndh_fabric("m81", 0.85) intersection()
    {
        translate([0, 0.006, 0.186]) scale([1, 1.10, 1]) difference()
        {
            cylinder(h = 0.012, r1 = 0.206, r2 = 0.190);
            translate([0, 0, -0.01]) cylinder(h = 0.04, r = 0.176);
        }
        translate([0, -0.20, 0.19]) cube([0.30, 0.20, 0.05], center = true);
    }
    // 猫眼带 + 两块荧光片
    ndh_cloth([0.16, 0.18, 0.11]) translate([0, 0.006, 0.212]) scale([1, 1.10, 1]) difference()
    {
        cylinder(h = 0.022, r = 0.1875);
        translate([0, 0, -0.01]) cylinder(h = 0.05, r = 0.176);
    }
    ndh_plastic([0.52, 0.56, 0.36], 0.35) for (s = [-1, 1])
        translate([s * 0.040, 0.006 + 0.1875 * 1.10, 0.223]) cube([0.022, 0.004, 0.010], center = true);
    if (strap)
    {
        ndh_cloth([0.14, 0.15, 0.10]) for (s = [-1, 1])
            translate([s * 0.122, 0.005, 0.07]) rotate([0, s * 8, 0]) cube([0.012, 0.020, 0.10], center = true);
        ndh_cloth([0.14, 0.15, 0.10]) translate([0, -0.040, 0.028]) ndh_box([0.24, 0.024, 0.016], 0.004);
        ndh_metal([0.10, 0.10, 0.10], 0.5) translate([0.07, -0.052, 0.028]) cube([0.016, 0.004, 0.018], center = true);
    }
}

// ================= 躯干：外套与海魂衫 =================
// cut：afganka（斜胸袋）/ bdu（四个带盖贴袋）/ emr（拉链立领、斜拉链胸袋、胸前军衔条）/
//      gorka（帐篷布罩衫）/ nva（东德野战服）/ telnyashka
module ndh_coat_torso(cut, fabric)
{
    $fn = 10;
    if (cut == "telnyashka")
    {
        // 贴身衫：比外套薄一圈，蓝白横条纹（针织，漫反射）+ 圆领
        ndh_cloth(ndh_tel_white()) hull()
        {
            translate([0, 0, 0.04]) ndh_box([0.355, 0.245, 0.13]);
            translate([0, 0, 0.37]) ndh_box([0.405, 0.262, 0.25]);
        }
        ndh_cloth(ndh_tel_blue()) for (z = [0.000 : 0.036 : 0.47])
            let(w = z < 0.105 ? 0.355 : z > 0.245 ? 0.405 : 0.355 + (z - 0.105) / 0.14 * 0.05,
                d = z < 0.105 ? 0.245 : z > 0.245 ? 0.262 : 0.245 + (z - 0.105) / 0.14 * 0.017)
                translate([0, 0, z]) ndh_band(w + 0.003, d + 0.003, 0.016);
        ndh_cloth(ndh_tel_blue()) translate([0, 0.012, 0.497]) difference()
        {
            ndh_band(0.20, 0.17, 0.012, 0.03);
            cube([0.15, 0.12, 0.05], center = true);
        }
    }
    else
    {
        ndh_coat_shell(fabric);
        if (cut == "afganka")
        {
            ndh_collar_and_straps(fabric);
            for (s = [-1, 1])
            {
                ndh_patch_pocket(fabric, s * 0.105, 0.34, 0.115, 0.115, s * 12);
                ndh_patch_pocket(fabric, s * 0.115, 0.09, 0.110, 0.120, s * -16, false);
            }
            ndh_fabric(fabric, 0.85) translate([0, 0, 0.170]) ndh_band(ndh_tw(0.17) + 0.006, ndh_td(0.17) + 0.006, 0.014);
        }
        if (cut == "bdu")
        {
            // BDU：两个胸袋 + 两个大下袋，全部带扣盖；腰部收紧带
            ndh_collar_and_straps(fabric, false);
            for (s = [-1, 1])
            {
                ndh_patch_pocket(fabric, s * 0.105, 0.335, 0.110, 0.115);
                ndh_patch_pocket(fabric, s * 0.110, 0.095, 0.135, 0.140);
            }
            for (s = [-1, 1]) ndh_fabric(fabric, 0.85) translate([s * 0.185, -0.08, 0.205]) ndh_box([0.012, 0.05, 0.024], 0.003);
        }
        if (cut == "emr")
        {
            // 2008 式：立领、拉链门襟外压挡片、两个斜拉链胸袋、胸前正中的军衔条
            ndh_fabric(fabric, 0.9) for (s = [-1, 1]) translate([s * 0.070, 0.0, 0.515]) ndh_box([0.030, 0.20, 0.050], 0.006);
            ndh_fabric(fabric, 0.9) translate([0, 0.085, 0.515]) ndh_box([0.17, 0.030, 0.050], 0.006);
            ndh_fabric(fabric, 0.92) translate([0.010, -0.143, 0.25]) ndh_box([0.046, 0.012, 0.47], 0.004);
            ndh_metal([0.10, 0.10, 0.10], 0.5) translate([0.010, -0.151, 0.475]) cube([0.010, 0.005, 0.022], center = true);
            for (s = [-1, 1])
            {
                ndh_fabric(fabric, 0.88) translate([s * 0.110, -0.146, 0.335]) rotate([0, s * 18, 0]) ndh_box([0.110, 0.014, 0.120], 0.005);
                ndh_cloth([0.07, 0.07, 0.06]) translate([s * 0.105, -0.154, 0.370]) rotate([0, s * 18, 0]) cube([0.095, 0.004, 0.006], center = true);
            }
            ndh_fabric(fabric, 0.75) translate([0, -0.151, 0.300]) ndh_box([0.032, 0.008, 0.085], 0.003);
            for (s = [-1, 1]) ndh_patch_pocket(fabric, s * 0.115, 0.095, 0.120, 0.120, s * -10, false);
        }
        if (cut == "nva")
        {
            ndh_collar_and_straps(fabric);
            for (s = [-1, 1])
            {
                ndh_patch_pocket(fabric, s * 0.105, 0.335, 0.110, 0.110);
                ndh_fabric(fabric, 0.85) translate([s * 0.115, -0.137, 0.120]) rotate([0, s * 30, 0]) ndh_box([0.012, 0.012, 0.11], 0.004);
            }
        }
        if (cut == "gorka")
        {
            // 防风门襟（拉链外压一道大挡片）+ 拉链头
            ndh_fabric(fabric, 0.9) translate([0.012, -0.143, 0.25]) ndh_box([0.050, 0.012, 0.48], 0.004);
            ndh_metal([0.12, 0.12, 0.12]) translate([0.012, -0.151, 0.46]) cube([0.010, 0.006, 0.026], center = true);
            // 双肩加固片 + 下摆加固带 + 两个竖插胸袋
            ndh_fabric("gorka_patch")
            {
                for (s = [-1, 1]) translate([s * 0.120, 0, 0.455]) ndh_box([0.17, 0.288, 0.085], 0.010);
                translate([0, 0, 0.030]) ndh_band(0.378, 0.268, 0.040);
                for (s = [-1, 1]) translate([s * 0.105, -0.144, 0.30]) ndh_box([0.075, 0.012, 0.130], 0.004);
            }
            ndh_metal([0.12, 0.12, 0.12]) for (s = [-1, 1]) translate([s * 0.105, -0.151, 0.36]) cube([0.006, 0.004, 0.010], center = true);
            // 折叠在后领的大兜帽 + 帽檐抽绳
            ndh_fabric(fabric) translate([0, 0.075, 0.500]) ndh_box([0.235, 0.18, 0.085], 0.025);
            ndh_fabric("gorka_patch") translate([0, 0.075, 0.543]) ndh_box([0.21, 0.16, 0.008], 0.003);
            ndh_cloth([0.12, 0.12, 0.11]) for (s = [-1, 1]) translate([s * 0.05, -0.146, 0.47]) cube([0.004, 0.004, 0.05], center = true);
        }
    }
}

module ndh_sleeve(cut, fabric, forearm = false)
{
    $fn = 10;
    len = forearm ? 0.29 : 0.31;
    width = forearm ? 0.137 : 0.158;
    depth = forearm ? 0.147 : 0.168;
    w = width - 0.012;
    d = depth - 0.012;
    if (cut == "telnyashka")
    {
        ndh_cloth(ndh_tel_white()) translate([0, 0, -len / 2]) ndh_box([w, d, len + 0.018]);
        ndh_cloth(ndh_tel_blue()) for (z = [-0.020 : -0.036 : -len + 0.01])
            translate([0, 0, z]) ndh_band(w + 0.003, d + 0.003, 0.016);
        if (forearm) ndh_cloth(ndh_tel_white()) translate([0, 0, -len + 0.016]) ndh_box([w + 0.004, d + 0.004, 0.030], 0.006);
    }
    else
    {
        ndh_fabric(fabric) translate([0, 0, -len / 2]) ndh_box([width, depth, len + 0.018]);
        if (cut == "gorka")
        {
            ndh_fabric("gorka_patch")
            {
                if (!forearm) translate([0, 0, -0.040]) ndh_box([width + 0.008, depth + 0.008, 0.070], 0.010);
                if (!forearm) translate([0, 0, -0.270]) ndh_box([width + 0.008, depth + 0.008, 0.075], 0.010);
                if (forearm) translate([0, 0, -0.050]) ndh_box([width + 0.008, depth + 0.008, 0.090], 0.010);
            }
            if (forearm) ndh_fabric(fabric, 0.75) translate([0, 0, -len + 0.020]) ndh_box([width - 0.004, depth - 0.004, 0.036], 0.008);
        }
        else
        {
            // 袖口（带扣襻）
            if (forearm)
            {
                ndh_fabric(fabric, 0.85) translate([0, 0, -len + 0.022]) ndh_box([width + 0.004, depth + 0.004, 0.036], 0.006);
                ndh_plastic(ndh_button(fabric)) translate([0.03, -depth / 2 - 0.004, -len + 0.022]) rotate([90, 0, 0]) cylinder(h = 0.004, r = 0.006, center = true);
            }
            // BDU：肘部双层加固；EMR：上臂带魔术贴臂章区的袖袋；阿富汗卡：上臂小袖袋
            if (cut == "bdu" && forearm) ndh_fabric(fabric, 0.92) translate([0, 0, -0.045]) ndh_box([width + 0.006, depth + 0.006, 0.085], 0.010);
            if (cut == "emr" && !forearm)
            {
                ndh_fabric(fabric, 0.88) translate([width / 2 + 0.004, 0, -0.140]) ndh_box([0.012, 0.100, 0.120], 0.004);
                ndh_cloth([0.10, 0.12, 0.08]) translate([width / 2 + 0.011, 0, -0.120]) cube([0.003, 0.070, 0.050], center = true);
            }
            if (cut == "afganka" && !forearm) ndh_fabric(fabric, 0.88) translate([width / 2 + 0.003, -0.01, -0.12]) ndh_box([0.010, 0.085, 0.095], 0.004);
        }
    }
}

// ================= 背心 =================
// 56 式胸挂：帆布（漫反射 + 帆布纹），三个长弹匣袋 + 两侧各两个手榴弹袋 + 肩带 + 背后交叉带
module ndh_type56()
{
    $fn = 10;
    k = [0.46, 0.43, 0.27];
    s2 = [0.36, 0.33, 0.20];
    ndh_cloth(k, "canvas", 0.10)
    {
        translate([0, -0.190, 0.245]) ndh_box([0.36, 0.030, 0.150], 0.008);
        for (x = [-0.058, 0, 0.058]) translate([x, -0.228, 0.235]) ndh_box([0.054, 0.050, 0.150], 0.008);
        for (x = [-0.150, -0.105, 0.105, 0.150]) translate([x, -0.222, 0.208]) ndh_box([0.042, 0.042, 0.090], 0.008);
    }
    ndh_cloth(ndh_shade(k, 0.82), "canvas", 0.10)
    {
        for (x = [-0.058, 0, 0.058]) translate([x, -0.246, 0.302]) ndh_box([0.058, 0.020, 0.036], 0.006);
        for (x = [-0.150, -0.105, 0.105, 0.150]) translate([x, -0.236, 0.245]) ndh_box([0.046, 0.016, 0.026], 0.005);
    }
    ndh_plastic([0.20, 0.16, 0.09], 0.5) for (x = [-0.058, 0, 0.058]) translate([x, -0.257, 0.296]) rotate([90, 0, 0]) cylinder(h = 0.005, r = 0.006, center = true);
    ndh_cloth(s2, "twill", 0.06)
    {
        for (s = [-1, 1])
        {
            translate([s * 0.130, -0.182, 0.395]) ndh_box([0.036, 0.016, 0.170], 0.004);
            translate([s * 0.133, 0, 0.482]) ndh_box([0.040, 0.360, 0.022], 0.005);
            translate([s * 0.222, 0, 0.240]) ndh_box([0.020, 0.300, 0.034], 0.004);
            hull()
            {
                translate([s * 0.130, 0.168, 0.465]) cube([0.036, 0.012, 0.010], center = true);
                translate([-s * 0.150, 0.152, 0.215]) cube([0.036, 0.012, 0.010], center = true);
            }
        }
        translate([0, 0.150, 0.240]) ndh_box([0.34, 0.012, 0.034], 0.004);
    }
}

// 6B3TM-01：橄榄色外罩前后片 + 叠压钛板的横向鼓包 + 胸前四个 AK 弹匣袋 + 护领 + 侧面束带与扣具
module ndh_6b3()
{
    o = [0.27, 0.30, 0.18];
    ndh_cloth(o, "twill", 0.08)
    {
        translate([0, -0.184, 0.31]) ndh_box([0.36, 0.056, 0.310]);
        translate([0, 0.174, 0.31]) ndh_box([0.34, 0.052, 0.310]);
        for (s = [-1, 1])
        {
            translate([s * 0.212, 0, 0.25]) ndh_box([0.028, 0.34, 0.130]);
            translate([s * 0.133, 0, 0.480]) ndh_box([0.056, 0.360, 0.034]);
        }
        translate([0, 0.090, 0.512]) ndh_box([0.25, 0.060, 0.040], 0.012);
        for (s = [-1, 1]) translate([s * 0.105, 0.020, 0.506]) rotate([0, 0, s * 28]) ndh_box([0.050, 0.120, 0.032], 0.010);
    }
    ndh_cloth(ndh_shade(o, 1.08), "twill", 0.08)
    {
        for (z = [0.390, 0.430]) translate([0, -0.215, z]) ndh_box([0.33, 0.010, 0.034], 0.006);
        for (z = [0.200, 0.250, 0.300, 0.350, 0.400]) translate([0, 0.203, z]) ndh_box([0.31, 0.010, 0.040], 0.006);
    }
    ndh_cloth(ndh_shade(o, 0.85), "twill", 0.08)
        for (x = [-0.126, -0.042, 0.042, 0.126]) translate([x, -0.232, 0.250]) ndh_box([0.074, 0.040, 0.140], 0.008);
    ndh_cloth(ndh_shade(o, 0.72), "twill", 0.08)
        for (x = [-0.126, -0.042, 0.042, 0.126]) translate([x, -0.254, 0.308]) ndh_box([0.078, 0.010, 0.040], 0.004);
    ndh_metal([0.20, 0.21, 0.19], 0.5) for (x = [-0.126, -0.042, 0.042, 0.126])
        translate([x, -0.261, 0.298]) cube([0.016, 0.004, 0.010], center = true);
    ndh_cloth(ndh_web()) for (s = [-1, 1], z = [0.20, 0.30]) translate([s * 0.228, 0, z]) ndh_box([0.006, 0.300, 0.026], 0.002);
    ndh_metal([0.18, 0.18, 0.17], 0.45) for (s = [-1, 1], z = [0.20, 0.30]) translate([s * 0.233, -0.12, z]) cube([0.006, 0.030, 0.030], center = true);
}

// ================= 下装 =================
// cut：afganka / bdu / emr / gorka / nva。靴型由剪裁与面料决定：
//   kirza 基尔扎人造革高靴 / jackboot 高筒皮行军靴 / laced 黑色系带作战靴 /
//   laced_brown 棕色系带靴（戈尔卡）/ desert 沙色翻毛沙漠靴（DBDU）
function ndh_boot_style(cut, fabric) =
    cut == "nva" ? "jackboot" : cut == "gorka" ? "laced_brown" : fabric == "afganka" ? "kirza" :
    fabric == "dbdu" ? "desert" : "laced";
function ndh_boot_c(style) = style == "kirza" ? [0.075, 0.068, 0.060] : style == "laced_brown" ? [0.12, 0.10, 0.075]
                           : style == "desert" ? [0.47, 0.37, 0.25] : style == "jackboot" ? [0.045, 0.045, 0.048] : [0.055, 0.052, 0.050];
// 抛光行军靴最亮、人造革次之，翻毛沙漠靴是纯漫反射
function ndh_boot_r(style) = style == "jackboot" ? 0.30 : style == "kirza" ? 0.45 : style == "desert" ? 1.0
                           : style == "laced_brown" ? 0.65 : 0.55;
// 靴筒顶在小腿局部坐标的高度（0 = 膝）
function ndh_shaft(style) = style == "kirza" ? -0.12 : style == "jackboot" ? -0.10 : style == "laced_brown" ? -0.33 : -0.29;

module ndh_boot_leather(style) ndh_leather(ndh_boot_c(style), ndh_boot_r(style)) children();

module ndh_pelvis(cut, fabric)
{
    $fn = 10;
    ndh_fabric(fabric) ndh_box([0.375, 0.245, 0.235]);
    if (cut == "afganka")
    {
        // 苏军士兵皮腰带 + 黄铜星徽扣
        ndh_leather([0.33, 0.20, 0.10], 0.5) translate([0, 0, 0.08]) ndh_band(0.387, 0.257, 0.040);
        ndh_brass() translate([0, -0.131, 0.08]) cube([0.060, 0.008, 0.046], center = true);
        ndh_enamel([0.55, 0.40, 0.16]) translate([0, -0.1355, 0.08]) rotate([90, 0, 0])
            linear_extrude(height = 0.002, center = true)
                polygon([for (i = [0 : 9]) let(a = 90 + i * 36, rr = i % 2 == 0 ? 0.016 : 0.0065) [rr * cos(a), rr * sin(a)]]);
    }
    else if (cut == "nva")
    {
        ndh_leather([0.06, 0.055, 0.05], 0.4) translate([0, 0, 0.08]) ndh_band(0.387, 0.257, 0.040);
        ndh_metal([0.55, 0.56, 0.57], 0.35) translate([0, -0.131, 0.08]) cube([0.052, 0.007, 0.042], center = true);
    }
    else if (cut == "bdu")
    {
        // 黑色尼龙腰带（织带，漫反射）+ 黄铜开口扣
        ndh_cloth([0.07, 0.07, 0.065], "twill", 0.05) translate([0, 0, 0.08]) ndh_band(0.385, 0.255, 0.034);
        ndh_brass() translate([0, -0.130, 0.08]) cube([0.040, 0.006, 0.036], center = true);
    }
    else if (cut == "emr")
    {
        ndh_leather([0.06, 0.055, 0.05], 0.5) translate([0, 0, 0.08]) ndh_band(0.385, 0.255, 0.034);
        ndh_metal([0.10, 0.10, 0.10], 0.45) translate([0, -0.130, 0.08]) cube([0.046, 0.006, 0.036], center = true);
    }
    else
    {
        // 戈尔卡：松紧腰 + 臀部加固片 + 背带扣
        ndh_fabric(fabric, 0.85) translate([0, 0, 0.085]) ndh_band(0.383, 0.253, 0.030);
        ndh_fabric("gorka_patch") translate([0, 0.124, -0.035]) ndh_box([0.30, 0.008, 0.14], 0.006);
        ndh_metal([0.15, 0.15, 0.14], 0.45) for (s = [-1, 1], y = [-1, 1]) translate([s * 0.09, y * 0.128, 0.09]) cube([0.020, 0.006, 0.014], center = true);
    }
}

module ndh_leg(cut, fabric, calf = false)
{
    len = calf ? 0.42 : 0.44;
    width = calf ? 0.165 : 0.19;
    depth = calf ? 0.18 : 0.205;
    style = ndh_boot_style(cut, fabric);
    shaft = ndh_shaft(style);
    top = -shaft;
    big = cut == "bdu" || cut == "emr";   // 风箱式大货袋
    if (!calf)
    {
        ndh_fabric(fabric) translate([0, 0, -len / 2]) ndh_box([width, depth, len + 0.018]);
        if (cut != "gorka")
        {
            // 大腿外侧大口袋 + 袋盖（BDU / EMR 是风箱式大货袋）
            ndh_fabric(fabric, 0.88) translate([width / 2 + (big ? 0.012 : 0.008), 0.010, -0.215]) ndh_box([big ? 0.026 : 0.020, 0.150, big ? 0.165 : 0.150], 0.005);
            ndh_fabric(fabric, 0.78) translate([width / 2 + (big ? 0.024 : 0.016), 0.010, big ? -0.140 : -0.145]) ndh_box([0.010, 0.156, 0.034], 0.004);
        }
        // 护膝：阿富汗卡衬垫 / BDU 双层 / EMR 外挂护膝袋 / 戈尔卡棕色加固
        if (cut == "afganka" || cut == "bdu" || cut == "emr")
            ndh_fabric(fabric, cut == "emr" ? 0.82 : 0.9) translate([0, -depth / 2 - 0.004, -0.40]) ndh_box([0.150, 0.010, 0.090], 0.006);
        if (cut == "gorka") ndh_fabric("gorka_patch") translate([0, -depth / 2 - 0.004, -0.38]) ndh_box([0.172, 0.012, 0.120], 0.006);
    }
    else
    {
        // 裤腿（到靴筒顶）+ 靴筒
        ndh_fabric(fabric) translate([0, 0, -top / 2 + 0.009]) ndh_box([width, depth, top + 0.018]);
        ndh_boot_leather(style) translate([0, 0, (shaft - len) / 2])
            ndh_box([width + 0.006, depth + 0.006, len + shaft + 0.004], 0.010);
        if (style == "laced" || style == "laced_brown" || style == "desert")
        {
            // 裤口塞进 / 扎在靴口上的一圈 + 鞋带格
            ndh_fabric(fabric, 0.8) translate([0, 0, shaft + 0.012]) ndh_box([width + 0.010, depth + 0.010, 0.026], 0.006);
            for (z = [shaft - 0.025 : -0.022 : -len + 0.02])
                ndh_cloth([0.05, 0.045, 0.04]) translate([0, -depth / 2 - 0.005, z]) cube([0.050, 0.004, 0.006], center = true);
        }
        else
            ndh_leather(ndh_shade(ndh_boot_c(style), 1.3), ndh_boot_r(style), false)
                translate([0, 0, shaft - 0.010]) ndh_box([width + 0.012, depth + 0.012, 0.022], 0.006);
        if (cut == "gorka") ndh_fabric("gorka_patch") translate([0, -depth / 2 - 0.004, -0.050]) ndh_box([0.150, 0.012, 0.090], 0.006);
    }
}

module ndh_boot(cut, fabric)
{
    style = ndh_boot_style(cut, fabric);
    ndh_boot_leather(style) translate([0, -0.07, 0.065]) ndh_box([0.175, 0.315, 0.13]);
    ndh_boot_leather(style) translate([0, 0.005, 0.14]) ndh_box([0.163, 0.163, 0.13]);
    // 橡胶鞋底 + 鞋跟
    ndh_rubber() translate([0, -0.07, 0.012]) ndh_box([0.181, 0.322, 0.024], 0.005);
    ndh_rubber() translate([0, 0.040, 0.030]) ndh_box([0.172, 0.085, 0.028], 0.005);
    if (style == "laced" || style == "laced_brown" || style == "desert")
        for (z = [0.090, 0.115, 0.140, 0.165]) ndh_cloth([0.05, 0.045, 0.04]) translate([0, -0.088, z]) cube([0.07, 0.010, 0.007], center = true);
}

// ================= 背包 =================
// RD-54：方正帆布主袋 + 两侧弹匣袋 + 前下袋 + 顶盖双扣带 + 侧挂 MPL-50 工兵锹
module ndh_rd54()
{
    $fn = 12;
    o = [0.28, 0.29, 0.17];
    w = 0.30; h = 0.38; d = 0.15;
    ctr = [0, 0.21 + d / 2, 0.29];
    ndh_cloth(o, "canvas", 0.12) translate(ctr) ndh_box([w, d, h], 0.022);
    ndh_cloth(ndh_shade(o, 0.85), "canvas", 0.12)
    {
        translate(ctr + [0, d / 2 + 0.025, -0.075]) ndh_box([w * 0.80, 0.050, h * 0.42], 0.012);
        for (s = [-1, 1]) translate(ctr + [s * (w / 2 + 0.022), 0.010, -0.050]) ndh_box([0.044, d * 0.75, h * 0.48], 0.010);
        translate(ctr + [0, 0.012, h / 2 - 0.035]) ndh_box([w + 0.008, d + 0.026, 0.080], 0.012);
    }
    ndh_cloth(ndh_web()) for (s = [-1, 1]) translate(ctr + [s * 0.08, d / 2 + 0.014, 0.08]) cube([0.026, 0.008, 0.20], center = true);
    ndh_metal([0.30, 0.30, 0.28], 0.45) for (s = [-1, 1]) translate(ctr + [s * 0.08, d / 2 + 0.020, 0.0]) cube([0.032, 0.006, 0.022], center = true);
    // MPL-50 工兵锹：清漆木柄 + 军绿漆锹面
    translate(ctr + [w / 2 + 0.050, 0.010, 0.12]) rotate([0, -6, 0])
    {
        gk_material([0.45, 0.30, 0.16], roughness = 0.55, metalness = 0) cylinder(h = 0.24, r = 0.015, center = true);
        ndh_paint([0.24, 0.26, 0.15]) translate([0, 0, -0.17]) ndh_box([0.012, 0.13, 0.12], 0.004);
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
    ndh_cloth(c, "canvas", 0.14) hull()
    {
        translate(ctr + [0, 0, -h / 2 + 0.05]) ndh_box([w, d, 0.10], 0.04);
        translate(ctr + [0, -0.01, h / 2 - 0.06]) ndh_box([w * 0.86, d * 0.82, 0.12], 0.04);
    }
    ndh_cloth(ndh_shade(c, 0.9), "canvas", 0.14)
    {
        for (s = [-1, 1]) translate(ctr + [s * (w / 2 + 0.020), 0.0, -0.045]) ndh_box([0.050, d * 0.70, h * 0.55], 0.018);
        translate(ctr + [0, d / 2 + 0.028, -0.040]) ndh_box([w * 0.62, 0.060, h * 0.52], 0.018);
        translate(ctr + [0, 0.01, h / 2 - 0.004]) ndh_box([w * 0.80, d * 0.86, 0.030], 0.010);
    }
    ndh_leather(lea, 0.6)
    {
        for (s = [-1, 1]) translate(ctr + [s * 0.075, d / 2 + 0.060, 0.02]) cube([0.024, 0.008, 0.26], center = true);
        for (s = [-1, 1]) translate(ctr + [s * (w / 2 + 0.047), 0.0, -0.02]) cube([0.006, 0.022, 0.16], center = true);
    }
    ndh_metal([0.42, 0.42, 0.40], 0.4) for (s = [-1, 1]) translate(ctr + [s * 0.075, d / 2 + 0.065, -0.07]) cube([0.032, 0.005, 0.024], center = true);
    ndh_cloth([0.25, 0.22, 0.15]) translate(ctr + [0, -0.01, h / 2 + 0.014]) cylinder(h = 0.012, r = 0.012);
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
module ndh_loot_coat(cut, fabric)
{
    $fn = 10;
    if (cut == "telnyashka")
    {
        ndh_cloth(ndh_tel_white()) translate([0, 0, 0.04]) ndh_box([0.32, 0.29, 0.08]);
        ndh_cloth(ndh_tel_white()) translate([0.10, 0, 0.075]) ndh_box([0.105, 0.27, 0.04]);
        ndh_cloth(ndh_tel_blue()) for (y = [-0.120 : 0.036 : 0.13]) translate([0, y, 0.0815]) cube([0.30, 0.016, 0.003], center = true);
    }
    else
    {
        ndh_fabric(fabric) translate([0, 0, 0.04]) ndh_box([0.32, 0.29, 0.08]);
        ndh_fabric(fabric, 0.85) translate([0.10, 0, 0.075]) ndh_box([0.105, 0.27, 0.04]);
        if (cut == "gorka")
        {
            ndh_fabric("gorka_patch") translate([-0.04, -0.09, 0.082]) ndh_box([0.20, 0.08, 0.006], 0.003);
            ndh_fabric(fabric) translate([0, 0.10, 0.10]) ndh_box([0.20, 0.12, 0.05], 0.015);   // 卷起的兜帽
        }
        else
        {
            // 两个带盖胸袋 + 领子
            for (x = [-0.10, 0.0]) ndh_fabric(fabric, 0.88) translate([x, 0.05, 0.083]) ndh_box([0.08, 0.08, 0.010], 0.003);
            for (x = [-0.10, 0.0]) ndh_plastic(ndh_button(fabric)) translate([x, 0.075, 0.089]) cylinder(h = 0.004, r = 0.007, center = true);
            ndh_fabric(fabric, 0.9) translate([0, 0.135, 0.085]) ndh_box([0.20, 0.035, 0.012], 0.004);
        }
    }
}

// 叠好的长裤：两条裤腿并排 + 腰带 + 横放的靴子
module ndh_loot_pants(cut, fabric)
{
    style = ndh_boot_style(cut, fabric);
    for (s = [-1, 1]) ndh_fabric(fabric) translate([s * 0.07, 0, 0.04]) ndh_box([0.135, 0.32, 0.08]);
    if (cut == "afganka")
    {
        ndh_leather([0.33, 0.20, 0.10], 0.5) translate([0, 0.12, 0.085]) ndh_box([0.28, 0.04, 0.012], 0.004);
        ndh_brass() translate([0.0, 0.12, 0.093]) cube([0.050, 0.038, 0.006], center = true);
    }
    else if (cut == "gorka")
        for (s = [-1, 1]) ndh_fabric("gorka_patch") translate([s * 0.07, -0.06, 0.082]) ndh_box([0.10, 0.10, 0.006], 0.003);
    else
    {
        ndh_leather([0.06, 0.055, 0.05], 0.45) translate([0, 0.12, 0.085]) ndh_box([0.28, 0.04, 0.012], 0.004);
        for (s = [-1, 1]) ndh_fabric(fabric, 0.88) translate([s * 0.10, -0.02, 0.083]) ndh_box([0.06, 0.10, 0.010], 0.003);
    }
    ndh_boot_leather(style) translate([0, -0.22, 0.04]) ndh_box([0.26, 0.10, 0.08], 0.010);
}
