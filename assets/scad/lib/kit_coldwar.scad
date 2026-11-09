// kit_coldwar.scad —— 冷战东欧生存射击零件库（DayZ 风格废弃世界）
// 纯零件库：只含 module/function 定义，无顶层几何。命名空间前缀 "cw_"。
// 常量以零参 function 内置（use 语义不传播顶层赋值），调用方无需重申环境常量。
// 放置契约：落地件底面 z=0，带朝向件 front = -y（载具车头朝 +x，武器枪口朝 +x）。
// 调用方自设 $fn（建议 12）。尺度：mid，1 unit = 1 m。
// 类别：ground 地面 / bldg 建筑 / nature 植被生态 / prop 道具路标 / veh 载具 /
//       wpn 武器（平躺地面 loot）/ item 物资（小件 loot）。
// 覆盖场景：废弃城镇、工厂、加油站、军事基地、监狱、城市超市、城市医院；
// 扩展段（文件末尾）：NextDayz 功能物资、室内家具、末日痕迹（尸袋/血迹/路障/检疫牌/涂鸦）、
// 农场/车库/警察局/汽车旅馆/要塞/火车站/教堂等建筑。
//
// 表面标准（与 kit_bohemia / kit_training 一致，见 docs/AGENT_GUIDE/SCADLoader.md）：
// - 一切表面走 cw_m_* 包装：多孔材料（抹灰、砖、混凝土、木板、帆布、土）漫反射 roughness=1，
//   质感来自低对比 gk_texture 印花；只有漆、釉、玻璃、水、金属带高光。
// - 旧钢铁：漆面钢 cw_m_steel（"chipped" 掉漆）、锈铁 cw_m_iron（"rust"）、瓦楞 cw_m_tin / cw_m_corr。
// - 反照率 < 0.5（路径追踪会整体提亮，0.5 已读作白）。玻璃不透明高光（透射介质在阴天会发黑）。
// - 外形由块面承担，印花只给块面加颗粒；三角面不必过省，但细节要讲故事
//   （谁住过、发生过什么、人们怎么撤离的），而不是平均地堆小方块。
// 可进入建筑契约：外壳 cw_part_shell 的地面装修层只有 1 cm（室内家具与刷新点仍按 z=0 摆放），
// 足印 L×D、门洞位置与净高不变，nextdayz_coldwar.scad 的家具与 loot 点据此布置。

use <gk_texgraph.scad>

// ================= 配色（低饱和铁幕灰绿；PT 强日光下会整体提亮，故基色偏深、全部 < 0.5） =================
function cw_ASPH()    = [0.25, 0.25, 0.26];   // 沥青路面
function cw_ASPHD()   = [0.19, 0.19, 0.20];   // 沥青裂缝/补丁
function cw_CONC()    = [0.43, 0.42, 0.39];   // 混凝土
function cw_CONCD()   = [0.33, 0.32, 0.30];   // 脏污混凝土/基座
function cw_PANEL()   = [0.46, 0.44, 0.39];   // 预制板米灰
function cw_PANELD()  = [0.36, 0.34, 0.31];   // 预制板拼缝
function cw_BRICK()   = [0.42, 0.21, 0.14];   // 红砖
function cw_BRICKD()  = [0.32, 0.17, 0.11];   // 深砖
function cw_PLASTER() = [0.47, 0.40, 0.27];   // 赭石抹灰
function cw_WHITEW()  = [0.49, 0.49, 0.46];   // 白灰墙（医院/检查站）
function cw_RUSTC()   = [0.33, 0.17, 0.10];   // 锈
function cw_METALC()  = [0.40, 0.41, 0.42];   // 镀锌金属
function cw_METALD()  = [0.23, 0.24, 0.25];   // 深金属
function cw_OLIVE()   = [0.21, 0.25, 0.14];   // 军绿
function cw_OLIVED()  = [0.15, 0.18, 0.11];   // 深军绿
function cw_KHAKI()   = [0.38, 0.34, 0.22];   // 卡其帆布
function cw_WOODC()   = [0.34, 0.25, 0.16];   // 原木
function cw_WOODD()   = [0.22, 0.16, 0.11];   // 深木
function cw_TRUNKC()  = [0.27, 0.21, 0.15];   // 树干
function cw_BIRCHW()  = [0.50, 0.49, 0.45];   // 白桦树皮
function cw_PINEC()   = [0.14, 0.25, 0.14];   // 松针中
function cw_PINED()   = [0.10, 0.18, 0.11];   // 松针深
function cw_LEAFC()   = [0.28, 0.37, 0.18];   // 阔叶
function cw_LEAFD()   = [0.21, 0.30, 0.14];   // 阔叶深
function cw_GRASSC()  = [0.32, 0.37, 0.21];   // 草地
function cw_GRASSD()  = [0.26, 0.31, 0.17];   // 深草斑
function cw_SOILC()   = [0.33, 0.25, 0.17];   // 泥土
function cw_DARKC()   = [0.06, 0.06, 0.06];   // 窗芯/洞口
function cw_GLASSC()  = [0.06, 0.08, 0.09];   // 玻璃
function cw_MARKW()   = [0.52, 0.52, 0.48];   // 白标线/白漆
function cw_REDC()    = [0.44, 0.10, 0.07];   // 苏式红
function cw_YELLOWC() = [0.50, 0.38, 0.08];   // 警示黄
function cw_SIGNB()   = [0.11, 0.24, 0.40];   // 路牌蓝
function cw_ROOFM()   = [0.30, 0.33, 0.34];   // 金属屋面
function cw_ROOFR()   = [0.36, 0.18, 0.12];   // 锈红屋面
function cw_FRAMEC()  = [0.40, 0.39, 0.35];   // 旧白漆窗框（灰黄）
function cw_BLOODC()  = [0.20, 0.03, 0.02];   // 干涸血迹

// ---- 确定性伪随机（必须含平方项：线性同余的组合仍是线性，连续 seed 会出等差伪影） ----
function cw_sq(x) = (x * x + x * 601 + 37) % 65521;
function cw_rnd(s, m) = cw_sq(cw_sq(((s % 65521) + 65521) % 65521) + 11) % m;
function cw_rndf(s) = cw_rnd(s, 1000) / 999;                       // [0, 1]
function cw_rndr(s, a, b) = a + (b - a) * cw_rndf(s);              // [a, b]
function cw_mul(c, k) = [c[0] * k, c[1] * k, c[2] * k];

// ---- 变体调色板 ----
function cw_panel_c(i) = [[0.46, 0.44, 0.39], [0.44, 0.40, 0.33], [0.41, 0.43, 0.41],
                          [0.46, 0.38, 0.29]][cw_rnd(i, 4)];                          // 预制板墙
function cw_wall_c(i)  = [cw_PLASTER(), cw_BRICK(), [0.44, 0.41, 0.34], cw_WHITEW(),
                          [0.39, 0.31, 0.24]][cw_rnd(i, 5)];                          // 杂墙
function cw_roof_c(i)  = [cw_ROOFM(), cw_ROOFR(), [0.23, 0.26, 0.29], [0.31, 0.23, 0.16]][cw_rnd(i, 4)];
function cw_car_c(i)   = [[0.42, 0.11, 0.08], [0.46, 0.45, 0.40], [0.15, 0.26, 0.34],
                          [0.47, 0.37, 0.10], [0.19, 0.30, 0.19], [0.24, 0.24, 0.27]][cw_rnd(i, 6)];
function cw_drum_c(i)  = [[0.30, 0.12, 0.07], cw_OLIVE(), [0.15, 0.25, 0.34], cw_METALD()][cw_rnd(i, 4)];
function cw_goods_c(i) = [[0.44, 0.11, 0.08], [0.14, 0.27, 0.38], [0.48, 0.40, 0.12],
                          [0.19, 0.32, 0.19], [0.46, 0.45, 0.40], [0.33, 0.20, 0.12]][cw_rnd(i, 6)];
// 室内：石灰刷白 + 齐腰油漆墙裙（苏式绿 / 蓝 / 棕 / 赭）
function cw_inner_c(i) = [[0.46, 0.45, 0.41], [0.42, 0.44, 0.43], [0.45, 0.42, 0.35], [0.40, 0.42, 0.44]][cw_rnd(i, 4)];
function cw_dado_c(i)  = [[0.14, 0.25, 0.18], [0.17, 0.24, 0.31], [0.29, 0.15, 0.10], [0.34, 0.29, 0.16],
                          [0.24, 0.27, 0.26]][cw_rnd(i, 5)];
function cw_lino_c(i)  = [[[0.30, 0.13, 0.09], [0.36, 0.29, 0.18]], [[0.16, 0.16, 0.15], [0.40, 0.39, 0.35]],
                          [[0.24, 0.20, 0.14], [0.33, 0.27, 0.17]]][cw_rnd(i, 3)];
function cw_fabric_c(i) = [[0.30, 0.12, 0.09], [0.18, 0.24, 0.30], [0.36, 0.30, 0.18], [0.22, 0.28, 0.20],
                           [0.34, 0.24, 0.20]][cw_rnd(i, 5)];

// ================= PBR 表面（gk_material 粗糙度 / 金属度 + gk_texture 印花） =================
// 多孔材料：漫反射 + 低对比印花（乘在本色上）。
module cw_m_plaster(c)  { gk_material(c, roughness = 1) gk_texture("plaster", palette = [[1, 1, 1], [0.84, 0.76, 0.68]], scale = 2.2, relief = 0.45) children(); }          // 抹灰外墙
module cw_m_lime(c)     { gk_material(c, roughness = 1) gk_texture("plaster", palette = [[1, 1, 1], [0.86, 0.82, 0.76]], scale = 2.0, seed = 4, relief = 0.4) children(); } // 室内石灰刷白
module cw_m_brick(c)    { gk_material(c, roughness = 1) gk_texture("brick", scale = 1.2) children(); }
module cw_m_silicate(c) { gk_material(c, roughness = 1) gk_texture("brick", scale = 1.2, seed = 3) children(); }   // 灰白硅酸盐砖
module cw_m_concrete(c) { gk_material(c, roughness = 1) gk_texture("concrete", scale = 3.0) children(); }
module cw_m_panel(c)    { gk_material(c, roughness = 1) gk_texture("concrete", scale = 1.6, seed = 2) children(); } // 预制大板
module cw_m_stone(c)    { gk_material(c, roughness = 1) gk_texture("stone", scale = 2.4) children(); }
module cw_m_rock(c)     { gk_material(c, roughness = 1) gk_texture("rock", scale = 2.0) children(); }
module cw_m_planks(c)   { gk_material(c, roughness = 1) gk_texture("planks", scale = 1.4) children(); }           // 风化木板
module cw_m_timber(c)   { gk_material(c, roughness = 1) gk_texture("woodgrain", scale = 1.6) children(); }        // 原木、方木、立柱
module cw_m_varnish(c)  { gk_material(c, roughness = 0.45) gk_texture("woodgrain", scale = 0.9, seed = 5) children(); } // 家具清漆木
module cw_m_tile(c)     { gk_material(c, roughness = 1) gk_texture("tiles", scale = 3.5) children(); }           // 陶瓦屋面
module cw_m_eternit(c)  { gk_material(c, roughness = 1) gk_texture("concrete", scale = 2.0, seed = 1) children(); } // 石棉水泥瓦
module cw_m_felt(c)     { gk_material(c, roughness = 1) gk_texture("gravel", scale = 1.6, seed = 3) children(); }  // 油毡平屋面
module cw_m_asphalt(c)  { gk_material(c, roughness = 1) gk_texture("asphalt", scale = 4.0) children(); }
module cw_m_gravel(c)   { gk_material(c, roughness = 1) gk_texture("gravel", scale = 3.0) children(); }
module cw_m_soil(c)     { gk_material(c, roughness = 1) gk_texture("dirt", scale = 4.0) children(); }
module cw_m_turf(c)     { gk_material(c, roughness = 1) gk_texture("meadow", scale = 4.0) children(); }
module cw_m_bark(c)     { gk_material(c, roughness = 1) gk_texture("bark", scale = 1.2) children(); }
module cw_m_leaf(c)     { gk_material(c, roughness = 1) gk_texture("dirt", scale = 1.5, seed = 7) children(); }  // 树冠团块的明暗颗粒
module cw_m_canvas(c)   { gk_material(c, roughness = 1) gk_texture("canvas", scale = 0.8) children(); }
module cw_m_sandbag(c)  { gk_material(c, roughness = 1) gk_texture("canvas", scale = 0.35, seed = 2) children(); }
module cw_m_fabric(c)   { gk_material(c, roughness = 1) gk_texture("twill", scale = 0.25) children(); }           // 床垫、沙发、衣物
module cw_m_hay(c)      { gk_material(c, roughness = 1) gk_texture("meadow", scale = 0.8, seed = 2) children(); }
module cw_m_paper(c)    { gk_material(c, roughness = 1) children(); }
// 漆、釉、塑料、玻璃、水：带高光。
module cw_m_oilpaint(c) { gk_material(c, roughness = 0.42) children(); }   // 油漆：门窗、墙裙
module cw_m_matte(c)    { gk_material(c, roughness = 0.7) children(); }    // 哑光漆 / 塑料：招牌、外壳
module cw_m_ceramic(c)  { gk_material(c, roughness = 0.14) children(); }   // 釉面砖、搪瓷、卫生陶瓷
module cw_m_plastic(c)  { gk_material(c, roughness = 0.5) children(); }
module cw_m_glass()     { gk_material([0.06, 0.075, 0.08], roughness = 0.04) children(); }
module cw_m_shard()     { gk_material([0.22, 0.27, 0.26], roughness = 0.03) children(); }   // 地上的碎玻璃
module cw_m_water()     { gk_material([0.03, 0.035, 0.03], roughness = 0.02) children(); }
module cw_m_oil()       { gk_material([0.025, 0.025, 0.025], roughness = 0.08, metalness = 0.3) children(); }
module cw_m_damp(c)     { gk_material(c, roughness = 0.45) children(); }   // 浸湿的地面、水渍
module cw_m_blood()     { gk_material(cw_BLOODC(), roughness = 0.35) children(); }
module cw_m_lamp()      { gk_material([0.50, 0.48, 0.40], roughness = 0.08) children(); }    // 灯罩玻璃
// 地面装修：苏式棋盘油地毡、方砖。
function cw_tex_lino(n = 10) = let(ch = tx_checker(n), wear = tx_fbm(6, 6, 3, 9))
    tx_surface(tx_mul(tx_select(ch, tx_palette(0, [1, 1, 1]), tx_palette(1, [0.6, 0.6, 0.6])), tx_add(0.88, tx_mul(wear, 0.12))),
               height = 0.5, rough = tx_add(0.55, tx_mul(wear, 0.45)), tilt = 3, blur = 1);
function cw_tex_floortile(n = 12) =
    let(edge = tx_course(n, n, 0, 7, "edge"), tone = tx_posterize(tx_course(n, n, 0, 7, "rand"), 3),
        joint = tx_step(tx_invert(edge), 0.94))
    tx_surface(tx_mul(tx_palette(0, [1, 1, 1]), tx_select(joint, 0.66, tx_add(0.86, tx_mul(tone, 0.14)))),
               height = tx_select(joint, 0.1, 0.8), rough = tx_select(joint, 1, 0.45), tilt = 6, blur = 1);
module cw_m_lino(cc)    { gk_material([1, 1, 1], roughness = 1) gk_texture(cw_tex_lino(), palette = cc, scale = 3.0) children(); }
module cw_m_floortile(c){ gk_material(c, roughness = 0.6) gk_texture(cw_tex_floortile(), scale = 2.4) children(); }
// 旧钢铁：色板绝对（漆 / 钢 / 锈），外层材质白色。
// 大面积漆面（车身、柜体、油桶）：漆色带细微斑驳与积灰，零星锈点和流痕；不用 "chipped" 的亮边
// （它在大面板上读作斑点）。漆面是电介质，锈点更粗糙。
function cw_tex_paint() =
    let(wear = tx_add(tx_mul(tx_fbm(6, 6, 4, 171), 0.7), tx_mul(tx_fbm(24, 24, 2, 173), 0.3)),
        pit = tx_top(wear, 0.03),
        halo = tx_top(wear, 0.06),
        grime = tx_top(tx_fbm(3, 3, 3, 177), 0.3),
        streak = tx_step(tx_fbm(14, 2, 3, 179), 0.68),
        mottle = tx_posterize(tx_fbm(8, 8, 2, 175), 3),
        paint = tx_palette(0, [0.21, 0.23, 0.14]))
    tx_surface(tx_select(pit, tx_palette(1, [0.15, 0.07, 0.035]),
                         tx_mul(paint, tx_add(0.93, tx_mul(mottle, 0.07)), tx_select(halo, 0.82, 1), tx_select(grime, 0.9, 1), tx_select(streak, 0.9, 1))),
               height = tx_select(pit, 0.25, tx_select(halo, 0.6, 0.8)), rough = tx_select(pit, 1, tx_select(grime, 1, 0.75)),
               metal = 0, tilt = 7, blur = 1);
module cw_m_steel(c = [0.21, 0.25, 0.14]) { gk_material([1, 1, 1], roughness = 0.85) gk_texture(cw_tex_paint(), palette = [c, [0.15, 0.07, 0.035]], scale = 1.2) children(); }
module cw_m_carpaint(c) { gk_material([1, 1, 1], roughness = 0.6) gk_texture(cw_tex_paint(), palette = [c, [0.15, 0.07, 0.035]], scale = 1.6, seed = 2) children(); }
module cw_m_iron(c = [0.20, 0.19, 0.18]) { gk_material([1, 1, 1], roughness = 0.85, metalness = 0.8) gk_texture("rust", palette = [c, [0.20, 0.075, 0.03], [0.08, 0.035, 0.02]], scale = 0.9) children(); }
module cw_m_burnt()     { gk_material([1, 1, 1], roughness = 0.95, metalness = 0.6) gk_texture("rust", palette = [[0.06, 0.055, 0.05], [0.13, 0.06, 0.03], [0.03, 0.028, 0.027]], scale = 1.3, seed = 4) children(); } // 焚毁车壳
module cw_m_galv(c = [0.40, 0.41, 0.42])  { gk_material([1, 1, 1], roughness = 0.5, metalness = 0.85) gk_texture("worn", palette = [c, cw_mul(c, 1.25)], scale = 0.5, seed = 3) children(); } // 镀锌 / 不锈钢
module cw_m_zinc(c = [0.33, 0.34, 0.34])  { gk_material(c, roughness = 0.36, metalness = 0.85) children(); }   // 檐沟、落水管
module cw_m_chrome()    { gk_material([0.55, 0.55, 0.55], roughness = 0.12, metalness = 1) children(); }
module cw_m_tin(c = [0.30, 0.33, 0.34])   { gk_material([1, 1, 1], roughness = 1, metalness = 0.3) gk_texture(tex_corrugated(24, 0.06), palette = [c, [0.17, 0.075, 0.035]], scale = 1.6) children(); }   // 瓦楞屋面
module cw_m_corr(c = [0.36, 0.37, 0.37])  { gk_material([1, 1, 1], roughness = 1, metalness = 0.3) gk_texture(tex_corrugated(30, 0.06), palette = [c, [0.17, 0.075, 0.035]], scale = 1.8, seed = 4) children(); } // 瓦楞墙板
// 墙体按种类选材质（外壳 / 墙段共用）。
module cw_m_wall(kind = "plaster", c = [0.45, 0.40, 0.30])
{
    if (kind == "brick") cw_m_brick(c) children();
    else if (kind == "silicate") cw_m_silicate(c) children();
    else if (kind == "panel") cw_m_panel(c) children();
    else if (kind == "concrete") cw_m_concrete(c) children();
    else if (kind == "wood") cw_m_planks(c) children();
    else if (kind == "log") cw_m_timber(c) children();
    else if (kind == "metal") cw_m_corr(c) children();
    else cw_m_plaster(c) children();
}
// 屋面按种类选材质。
module cw_m_roof(kind = "tin", c = [0.30, 0.33, 0.34])
{
    if (kind == "tile") cw_m_tile(c) children();
    else if (kind == "eternit") cw_m_eternit(c) children();
    else if (kind == "felt") cw_m_felt(c) children();
    else if (kind == "boards") cw_m_planks(c) children();
    else cw_m_tin(c) children();
}

// ---- 基础工具 ----
module cw_boxc(s) cube(s, center = true);
module cw_slab(L = 4, D = 4, t = 0.2) translate([0, 0, t / 2]) cw_boxc([L, D, t]);   // 底面 z=0 平板
// 两点间方杆（斜撑、梯帮、栏杆）与圆杆（管线、钢筋、拉索）。
module cw_beam(a, b, w = 0.1) hull() { translate(a) cw_boxc([w, w, w]); translate(b) cw_boxc([w, w, w]); }
module cw_rod(a, b, r = 0.03, fn = 6)
    let(d = b - a, l = norm(d)) translate(a) rotate([0, acos(d[2] / l), atan2(d[1], d[0])]) cylinder(h = l, r = r, $fn = fn);
// 不规则扁平团块（水洼、血迹、油渍）与碎玻璃轮廓。
function cw_blob(s, r, k = 0.7, n = 9) = [for (i = [0 : n - 1]) let(a = i * 360 / n, q = r * (0.62 + 0.38 * cw_rndf(s + i * 13))) [q * cos(a), q * sin(a) * k]];
function cw_shard(s, sz) = [[0, 0], [sz * cw_rndr(s + 1, 0.6, 1.2), sz * cw_rndr(s + 2, -0.2, 0.2)], [sz * cw_rndr(s + 3, 0.1, 0.5), sz * cw_rndr(s + 4, 0.3, 0.8)]];
// 地面贴片（z=0 起 h 厚）。
module cw_decal(s, r, k = 0.7, h = 0.004) linear_extrude(height = h) polygon(cw_blob(s, r, k));
// 文字铭牌（front=-y，锚点=板中心）。
module cw_plate(msg = "", w = 1.2, h = 0.3, size = 0.16, bg = [0.50, 0.50, 0.47], fg = [0.06, 0.06, 0.06], t = 0.03)
{
    cw_m_matte(bg) cw_boxc([w, t, h]);
    if (msg != "") cw_m_matte(fg) translate([0, -t / 2 - 0.004, 0]) rotate([90, 0, 0]) linear_extrude(height = 0.008, center = true)
        text(msg, size = size, halign = "center", valign = "center");
}

// ================= 通用构件 =================

// 坡屋面：正脊沿 x。L,D=檐口对应墙皮，h=脊高，ov=出檐，rin=山面内收（0=双坡，rin>=L/2=攒尖）。
// mat：tin 瓦楞铁 / tile 陶瓦 / eternit 石棉瓦 / felt 油毡 / boards 木板。带封檐板、屋脊盖板、檐沟。
module cw_part_roof(L = 8, D = 6, h = 1.8, ov = 0.5, rin = 0, c = [0.28, 0.31, 0.36], mat = "tin", trim = true)
{
    hw = L / 2 + ov;
    hd = D / 2 + ov;
    rx = max(0.02, L / 2 - rin);
    cw_m_roof(mat, c) polyhedron(
        points = [[-hw, -hd, 0], [hw, -hd, 0], [hw, hd, 0], [-hw, hd, 0], [-rx, 0, h], [rx, 0, h]],
        faces = [[0, 1, 2, 3], [4, 5, 1, 0], [5, 4, 3, 2], [5, 2, 1], [4, 0, 3]]);
    if (trim && L > 1.5)
    {
        // 封檐板 + 檐沟（铁皮檐沟在瓦楞 / 石棉瓦屋面上）
        cw_m_planks(cw_WOODD()) for (sy = [-1, 1]) translate([0, sy * (hd - 0.02), -0.09]) cw_boxc([2 * hw, 0.04, 0.18]);
        if (mat != "boards") cw_m_zinc() for (sy = [-1, 1]) translate([0, sy * (hd + 0.06), -0.06]) rotate([0, 90, 0]) cylinder(h = 2 * hw, r = 0.07, center = true, $fn = 6);
        // 屋脊盖板
        if (rin < L / 2 - 0.3) cw_m_galv(cw_mul(c, 0.8)) translate([0, 0, h - 0.02]) rotate([45, 0, 0]) cw_boxc([2 * rx + 0.1, 0.16, 0.16]);
    }
}

// ---- 窗 ----
// 变体：0/1 完好，2 碎一扇（锯齿残片），3 钉木板，4 只剩框，5 拉着窗帘。seed < 0 = 完好。
function cw_win_var(s) = s < 0 ? 0 : cw_rnd(s + 17, 6);
function cw_win_broken(s) = cw_win_var(s) == 2 || cw_win_var(s) == 4;

// 窗扇（框 + 玻璃 + 变体），中心在原点，玻璃面 y=0，外侧 -y。
module cw_part_sash(w = 1.0, h = 1.3, seed = -1)
{
    v = cw_win_var(seed);
    zt = h * 0.22;                     // 上亮子横档
    cw_m_oilpaint(cw_FRAMEC())
    {
        for (sx = [-1, 1]) translate([sx * (w / 2 - 0.03), 0, 0]) cw_boxc([0.06, 0.07, h]);
        for (sz = [-1, 1]) translate([0, 0, sz * (h / 2 - 0.03)]) cw_boxc([w, 0.07, 0.06]);
        if (v != 4)
        {
            translate([0, 0, -h * 0.11 - 0.03]) cw_boxc([0.05, 0.06, h * 0.78]);
            translate([0, 0, zt]) cw_boxc([w, 0.06, 0.05]);
        }
    }
    gw = w / 2 - 0.06;
    gh = h / 2 + zt - 0.08;
    gz = (zt - h / 2) / 2;
    if (v <= 1 || v == 5) cw_m_glass() cw_boxc([w - 0.08, 0.012, h - 0.08]);
    if (v == 2)
    {
        cw_m_glass()
        {
            translate([0, 0, (zt + h / 2) / 2]) cw_boxc([w - 0.08, 0.012, h / 2 - zt - 0.06]);
            translate([-(gw / 2 + 0.03), 0, gz]) cw_boxc([gw, 0.012, gh]);
            translate([0.03, 0, -h / 2 + 0.04]) rotate([90, 0, 0]) linear_extrude(height = 0.012, center = true)
                polygon([[0, 0], [gw, 0], [gw, gh * 0.5], [gw * 0.55, gh * 0.18], [gw * 0.3, gh * 0.42], [0, gh * 0.62]]);
        }
    }
    if (v == 5) cw_m_fabric(cw_fabric_c(seed)) translate([0, 0.05, 0])
    {
        translate([-w * 0.24, 0, -0.02]) cw_boxc([w * 0.42, 0.02, h - 0.1]);
        translate([w * 0.3, 0, h * 0.1]) rotate([0, 6, 0]) cw_boxc([w * 0.3, 0.02, h * 0.7]);
    }
    if (v == 3) cw_m_planks(cw_WOODC()) for (i = [0 : 2])
        translate([0, -0.07, (i - 1) * h * 0.3]) rotate([0, cw_rndr(seed + i, -12, 12), 0]) cw_boxc([w + 0.24, 0.035, 0.17]);
    if (v == 3) cw_m_iron() for (i = [0 : 2], sx = [-1, 1]) translate([sx * w * 0.45, -0.09, (i - 1) * h * 0.3]) cw_boxc([0.02, 0.02, 0.02]);
}

// 贴墙窗（装饰用：front=-y，锚点=窗中心；墙面 y≈+0.04）：窗套 + 窗台 + 暗腔 + 窗扇。
module cw_part_window(w = 1.0, h = 1.3, seed = -1, fc = [0.45, 0.44, 0.40])
{
    cw_m_plaster(fc)
    {
        for (sx = [-1, 1]) translate([sx * (w / 2 + 0.06), -0.01, 0]) cw_boxc([0.12, 0.09, h + 0.2]);
        translate([0, -0.01, h / 2 + 0.08]) cw_boxc([w + 0.24, 0.09, 0.14]);
    }
    cw_m_concrete(cw_CONCD()) translate([0, -0.07, -h / 2 - 0.05]) cw_boxc([w + 0.3, 0.2, 0.08]);
    color(cw_DARKC()) translate([0, 0.03, 0]) cw_boxc([w, 0.04, h]);
    translate([0, -0.0, 0]) cw_part_sash(w, h, seed);
    // 窗台下的雨水锈痕
    if (seed >= 0 && cw_rnd(seed + 3, 3) == 0)
        cw_m_damp(cw_mul(fc, 0.62)) translate([cw_rndr(seed, -w * 0.3, w * 0.3), -0.058, -h / 2 - 0.5]) cw_boxc([0.18, 0.005, 0.8]);
}

// 破损/钉板窗变体：seed 决定（同 cw_part_window 的变体表）
module cw_part_window_worn(w = 1.0, h = 1.3, seed = 0) cw_part_window(w, h, seed);

// 铁栏窗（监狱/仓库，front=-y）：混凝土窗套 + 暗腔 + 脏玻璃 + 锈铁栏
module cw_part_window_bar(w = 0.8, h = 0.9)
{
    cw_m_concrete(cw_CONCD()) cw_boxc([w + 0.18, 0.10, h + 0.16]);
    color(cw_DARKC()) translate([0, -0.02, 0]) cw_boxc([w, 0.10, h]);
    cw_m_glass() translate([0, -0.04, h * 0.2]) cw_boxc([w - 0.04, 0.01, h * 0.55]);
    cw_m_iron() for (i = [-1, 0, 1]) translate([i * w * 0.28, -0.08, 0]) cw_boxc([0.035, 0.035, h + 0.06]);
    cw_m_iron() translate([0, -0.08, 0]) cw_boxc([w + 0.04, 0.03, 0.04]);
}

// 门扇：铰链在 x=0，向 +x 展开，宽 w、高 h，底 z=0（厚 0.05，面 y=0）。
module cw_part_leaf(w = 0.9, h = 2.05, c = [0.30, 0.22, 0.16], steel = false, seed = 0)
{
    if (steel) cw_m_steel(c) translate([w / 2, 0, h / 2]) cw_boxc([w, 0.05, h]);
    else
    {
        cw_m_oilpaint(c) translate([w / 2, 0, h / 2]) cw_boxc([w, 0.045, h]);
        cw_m_oilpaint(cw_mul(c, 0.8)) for (z = [h * 0.28, h * 0.72]) for (sy = [-1, 1])
            translate([w / 2, sy * 0.026, z]) cw_boxc([w * 0.7, 0.01, h * 0.34]);
    }
    cw_m_galv([0.40, 0.36, 0.24]) for (sy = [-1, 1]) translate([w - 0.1, sy * 0.05, h * 0.48]) cw_boxc([0.12, 0.03, 0.03]);
}

// 木门带框（front=-y，底面 z=0；贴墙用：门扇关闭）
module cw_part_door(w = 1.0, h = 2.1, c = [0.30, 0.22, 0.16])
{
    cw_m_concrete(cw_CONCD()) translate([0, 0, h / 2]) difference() { cw_boxc([w + 0.24, 0.12, h + 0.12]); translate([0, -0.1, -0.06]) cw_boxc([w, 0.12, h]); }
    translate([-w / 2, -0.04, 0]) cw_part_leaf(w, h - 0.02, c);
}

// 铁门（军营/监狱，front=-y，底面 z=0）
module cw_part_door_metal(w = 1.1, h = 2.1, c = [0.26, 0.28, 0.30])
{
    cw_m_concrete(cw_CONCD()) translate([0, 0, h / 2]) difference() { cw_boxc([w + 0.22, 0.12, h + 0.12]); translate([0, -0.1, -0.06]) cw_boxc([w, 0.12, h]); }
    translate([-w / 2, -0.04, 0]) cw_part_leaf(w, h - 0.02, c, true);
    cw_m_iron() translate([w * 0.3, -0.10, h * 0.5]) cw_boxc([0.06, 0.05, 0.16]);
}

// ---- 可进入空心外壳（DayZ 掩体攻防：角色可走入，室内可放 loot） ----
// 四面墙 + 混凝土脚圈，室内挖空直通地面（无门槛），门/窗为真实洞口。front = -y。
// 每个材质桶都会生成三角网碰撞：任何装饰件都不得横跨门洞的行走高度，否则会挡住入口。
//   L,D,wh   外墙足印/墙高；wc 墙色；t 墙厚；wall 墙体材质（plaster/brick/silicate/panel/concrete/wood/log/metal）
//   dw,dh,dx 前墙门洞 宽/高/中心x（洞口直通地面）
//   roomH    室内净高：0 => 直通墙顶（坡屋顶另置于其上）；>0 => 该高度以上留实心（平顶/多层地面楼板）
//   wins     窗洞列表 [face,u,z,w,h]；face 0=前(-y)/1=后(+y)/2=左(-x)/3=右(+x)，u=沿墙偏移，z=洞心高
//   plinth   是否绘制混凝土脚圈
//   seed     窗玻璃 / 门扇 / 痕迹变体
//   floor    室内地面装修（1 cm 厚）：concrete / boards / lino / tiles / none
//   door     门扇：wood / steel / none（宽门洞 >1.7 m 画对开两扇，全部贴外墙敞开）
//   skin     室内石灰墙皮 + 油漆墙裙 + 顶棚（薄木棚 / 金属棚一般关掉）
module cw_part_shell_hole(L, D, t, w)
{
    if (w[0] == 0)      translate([w[1], -D / 2, w[2]]) cw_boxc([w[3], t * 4, w[4]]);
    else if (w[0] == 1) translate([w[1],  D / 2, w[2]]) cw_boxc([w[3], t * 4, w[4]]);
    else if (w[0] == 2) translate([-L / 2, w[1], w[2]]) cw_boxc([t * 4, w[3], w[4]]);
    else                translate([ L / 2, w[1], w[2]]) cw_boxc([t * 4, w[3], w[4]]);
}

// 把子件放到窗洞 w 的墙中面上：局部 x 沿墙、外侧 -y、z=洞心。
module cw_part_at_hole(L, D, w)
{
    if (w[0] == 0)      translate([w[1], -D / 2, w[2]]) children();
    else if (w[0] == 1) translate([w[1],  D / 2, w[2]]) rotate([0, 0, 180]) children();
    else if (w[0] == 2) translate([-L / 2, w[1], w[2]]) rotate([0, 0, -90]) children();
    else                translate([ L / 2, w[1], w[2]]) rotate([0, 0, 90]) children();
}

module cw_part_shell(L, D, wh, wc, t = 0.28, dw = 1.5, dh = 2.1, dx = 0, roomH = 0, wins = [], plinth = true,
                     seed = 0, wall = "plaster", floor = "concrete", door = "wood", skin = true, dc = undef)
{
    ih = (roomH > 0 ? roomH : wh + 1.0);
    sh = (roomH > 0 ? roomH : wh);
    Li = L - 2 * t;
    Di = D - 2 * t;
    // 墙体：实心块挖空 + 门洞 + 窗洞
    cw_m_wall(wall, wc) difference()
    {
        cw_slab(L, D, wh);
        translate([0, 0, -0.1]) cw_slab(Li, Di, ih + 0.1);
        translate([dx, -D / 2, dh / 2 - 0.06]) cw_boxc([dw, t * 4, dh]);
        for (w = wins) cw_part_shell_hole(L, D, t, w);
    }
    // 混凝土脚圈（沥青防潮层颜色更深）：门洞处断开，避免门槛
    if (plinth)
        cw_m_concrete(cw_mul(cw_CONCD(), 0.85)) difference()
        {
            cw_slab(L + 0.12, D + 0.12, 0.42);
            translate([0, 0, -0.1]) cw_slab(L - 0.02, D - 0.02, 0.7);
            translate([dx, -D / 2, 0.12]) cw_boxc([dw, 1.4, 0.8]);
        }
    // 室内墙皮：石灰刷白 + 齐腰油漆墙裙 + 顶棚
    if (skin && t >= 0.16)
    {
        ins = [cw_inner_c(seed), cw_dado_c(seed + 1)];
        cw_m_lime(ins[0]) difference()
        {
            cw_slab(Li + 0.002, Di + 0.002, sh);
            translate([0, 0, -0.1]) cw_slab(Li - 0.02, Di - 0.02, sh + 0.2);
            translate([dx, -D / 2, dh / 2 - 0.06]) cw_boxc([dw, t * 4, dh]);
            for (w = wins) cw_part_shell_hole(L, D, t, w);
        }
        cw_m_oilpaint(ins[1]) difference()
        {
            cw_slab(Li - 0.018, Di - 0.018, 1.25);
            translate([0, 0, -0.1]) cw_slab(Li - 0.03, Di - 0.03, 1.5);
            translate([dx, -D / 2, dh / 2 - 0.06]) cw_boxc([dw, t * 4, dh]);
            for (w = wins) cw_part_shell_hole(L, D, t, w);
        }
        cw_m_lime(cw_mul(ins[0], 0.95)) translate([0, 0, sh - 0.012]) cw_slab(Li, Di, 0.012);
    }
    // 地面装修（1 cm，家具仍按 z=0 摆放）
    if (floor == "boards") cw_m_planks(cw_mul(cw_WOODC(), 0.9)) cw_slab(Li, Di, 0.01);
    else if (floor == "lino")
    {
        cw_m_lino(cw_lino_c(seed)) cw_slab(Li, Di, 0.01);
        cw_m_concrete(cw_CONCD()) for (i = [0 : 1])   // 磨穿 / 撕开的油地毡露出水泥
            translate([cw_rndr(seed + i * 7, -Li * 0.3, Li * 0.3), cw_rndr(seed + i * 11, -Di * 0.2, Di * 0.3), 0.0105])
                rotate([0, 0, cw_rnd(seed + i, 180)]) cw_decal(seed + i * 5, cw_rndr(seed + i, 0.3, 0.6), 0.6, 0.002);
    }
    else if (floor == "tiles") cw_m_floortile(cw_mul(cw_WHITEW(), 0.9)) cw_slab(Li, Di, 0.01);
    else if (floor == "concrete") cw_m_concrete(cw_mul(cw_CONCD(), 0.92)) cw_slab(Li, Di, 0.008);
    // 门：外侧门套 + 敞开的门扇（贴着外墙，不挡通道）
    if (door != "none")
    {
        doorc = dc != undef ? dc : (door == "steel" ? [0.24, 0.27, 0.24] : [[0.30, 0.18, 0.11], [0.20, 0.27, 0.22], [0.26, 0.25, 0.22]][cw_rnd(seed + 9, 3)]);
        cw_m_oilpaint(cw_mul(doorc, 0.9)) translate([dx, -D / 2 - 0.025, 0])
        {
            for (sx = [-1, 1]) translate([sx * (dw / 2 + 0.05), 0, dh / 2]) cw_boxc([0.1, 0.05, dh]);
            translate([0, 0, dh + 0.04]) cw_boxc([dw + 0.2, 0.05, 0.1]);
        }
        if (dw <= 1.7)
        {
            lw = min(dw, 1.0);
            hang = cw_rnd(seed + 13, 4);
            // 0-2 贴墙敞开，3 被踹掉（斜靠在墙边）
            if (hang < 3) translate([dx - dw / 2 - 0.1, -D / 2 - 0.06, 0]) rotate([0, 0, 180 + cw_rndr(seed, 4, 22)])
                cw_part_leaf(lw, dh - 0.08, doorc, door == "steel", seed);
            else translate([dx - dw / 2 - 0.25, -D / 2 - 0.55, 0]) rotate([0, 0, 180]) rotate([12, 0, 0])
                cw_part_leaf(lw, dh - 0.08, doorc, door == "steel", seed);
        }
        else for (sx = [-1, 1])
            translate([dx + sx * (dw / 2 + 0.1), -D / 2 - 0.06, 0]) rotate([0, 0, sx < 0 ? 188 : -8])
                cw_part_leaf(dw / 2, dh - 0.08, doorc, door == "steel", seed + sx);
    }
    // 窗：洞内窗扇（变体）+ 外窗台；碎窗下的室内散落玻璃
    if (len(wins) > 0) for (k = [0 : len(wins) - 1])
    {
        w = wins[k];
        ws = seed * 7 + k * 13;
        cw_part_at_hole(L, D, w)
        {
            translate([0, t * 0.15, 0]) cw_part_sash(w[3], w[4], ws);
            cw_m_concrete(cw_CONCD()) translate([0, -t / 2 - 0.05, -w[4] / 2 - 0.04]) cw_boxc([w[3] + 0.2, 0.18, 0.08]);
            if (cw_win_broken(ws) && w[2] - w[4] / 2 < 1.6)
                cw_m_shard() for (i = [0 : 4])
                    translate([cw_rndr(ws + i * 7, -w[3] / 2, w[3] / 2), t / 2 + cw_rndr(ws + i * 11, 0.1, 0.9), -w[2] + 0.012])
                        rotate([0, 0, cw_rnd(ws + i * 19, 360)]) linear_extrude(height = 0.004) polygon(cw_shard(ws + i * 23, cw_rndr(ws + i, 0.06, 0.16)));
        }
    }
}

// 墙脚的末日痕迹：碎砖、苔藓杂草、雨水污痕（沿外墙，避开前墙门洞 dx±dw）。
module cw_part_decay(seed = 0, L = 10, D = 8, dx = 0, dw = 2, h = 3)
{
    for (i = [0 : 6])
    {
        side = cw_rnd(seed + i * 23, 4);
        u = cw_rndr(seed + i * 11, -0.45, 0.45);
        p = side == 0 ? [u * L, -D / 2 - 0.25] : side == 1 ? [u * L, D / 2 + 0.25] : [(side == 2 ? -1 : 1) * (L / 2 + 0.25), u * D];
        if (!(side == 0 && abs(p[0] - dx) < dw / 2 + 0.8))
            cw_m_concrete(i % 3 == 0 ? cw_BRICKD() : cw_CONCD()) translate([p[0], p[1], 0.05])
                rotate([cw_rndr(seed + i, -12, 12), cw_rndr(seed + i * 3, -15, 15), cw_rnd(seed + i * 7, 180)])
                    cw_boxc([cw_rndr(seed + i, 0.18, 0.5), cw_rndr(seed + i * 5, 0.14, 0.3), 0.12]);
    }
    for (i = [0 : 7])
    {
        side = cw_rnd(seed + i * 31 + 5, 4);
        u = cw_rndr(seed + i * 17 + 3, -0.47, 0.47);
        p = side == 0 ? [u * L, -D / 2 - 0.1] : side == 1 ? [u * L, D / 2 + 0.1] : [(side == 2 ? -1 : 1) * (L / 2 + 0.1), u * D];
        if (!(side == 0 && abs(p[0] - dx) < dw / 2 + 0.4))
            cw_m_leaf(i % 2 == 0 ? cw_GRASSD() : [0.36, 0.36, 0.20]) translate([p[0], p[1], 0])
                for (a = [-30, 0, 30]) rotate([a, cw_rndr(seed + i, -20, 20), i * 40]) cylinder(h = cw_rndr(seed + i * 3, 0.35, 0.9), r1 = 0.07, r2 = 0.01, $fn = 4);
    }
    // 雨水 / 霉斑流痕（贴外墙面）
    for (i = [0 : 2]) let(sx = cw_rnd(seed + i * 41, 2) == 0 ? -1 : 1)
        cw_m_damp([0.16, 0.15, 0.12]) translate([sx * (L / 2 + 0.006), cw_rndr(seed + i * 43, -D * 0.35, D * 0.35), h * 0.55])
            cw_boxc([0.006, cw_rndr(seed + i, 0.3, 0.9), h * cw_rndr(seed + i * 5, 0.4, 0.8)]);
}

// 室内地面的撤离痕迹：散落文件纸、翻倒的物品、水洼 / 血迹（只是贴片，不挡路，避开刷新点高度）。
module cw_part_litter(seed = 0, Li = 6, Di = 4, n = 8, blood = false, wet = false)
{
    for (i = [0 : n - 1])
        cw_m_paper(i % 3 == 0 ? [0.42, 0.40, 0.33] : [0.48, 0.47, 0.43])
            translate([cw_rndr(seed + i * 7, -Li * 0.42, Li * 0.42), cw_rndr(seed + i * 13, -Di * 0.42, Di * 0.42), 0.012 + i * 0.0006])
                rotate([0, 0, cw_rnd(seed + i * 3, 180)]) cw_boxc([0.21, 0.297, 0.002]);
    if (blood) cw_m_blood() translate([cw_rndr(seed + 5, -Li * 0.25, Li * 0.25), cw_rndr(seed + 9, -Di * 0.2, Di * 0.2), 0.012])
    {
        cw_decal(seed, 0.45, 0.7, 0.003);
        for (i = [0 : 3]) translate([0.35 + i * 0.32, cw_rndr(seed + i, -0.1, 0.1), 0]) cw_decal(seed + i * 3, 0.1, 0.8, 0.003);
    }
    if (wet) cw_m_water() translate([cw_rndr(seed + 15, -Li * 0.3, Li * 0.3), cw_rndr(seed + 19, -Di * 0.3, Di * 0.3), 0.012])
        cw_decal(seed + 21, cw_rndr(seed, 0.4, 0.8), 0.7, 0.003);
}

// ================= 地面（沿 x 铺设，底面 z=0） =================

// 老化沥青路段：褪色单虚中线 + 裂缝 + 补丁 + 积水 + 碎石路肩
module cw_ground_road(L = 24, W = 7, seed = 0)
{
    cw_m_asphalt(cw_ASPH()) cw_slab(L, W, 0.12);
    cw_m_gravel([0.36, 0.34, 0.30]) for (sy = [-1, 1]) translate([0, sy * (W / 2 + 0.35), 0]) cw_slab(L, 0.7, 0.08);
    for (i = [0 : 2])
        cw_m_asphalt(cw_ASPHD())
            translate([cw_rndr(seed * 7 + i * 31, -(L - 4) / 2, (L - 4) / 2),
                       cw_rndr(seed * 13 + i * 17 + 5, -(W - 2.4) / 2, (W - 2.4) / 2), 0.12])
                cw_slab(1.4 + cw_rnd(seed + i, 3) * 0.5, 1.0 + cw_rnd(seed + i + 9, 3) * 0.4, 0.008);
    for (i = [0 : 3])
        color(cw_mul(cw_ASPHD(), 0.6))
            translate([cw_rndr(seed * 3 + i * 43, -(L - 3) / 2, (L - 3) / 2),
                       cw_rndr(seed * 5 + i * 23 + 7, -(W - 1.6) / 2, (W - 1.6) / 2), 0.12])
                rotate([0, 0, cw_rnd(seed + i * 7, 180)]) cw_slab(1.8, 0.05, 0.006);
    cw_m_water() translate([cw_rndr(seed + 77, -L / 3, L / 3), (W / 2 - 0.8) * (cw_rnd(seed, 2) == 0 ? -1 : 1), 0.12]) cw_decal(seed, 0.9, 0.5, 0.004);
    nd = floor(L / 3.0);
    cw_m_matte(cw_MARKW()) for (i = [0 : nd - 1])
        if (cw_rnd(seed + i * 13, 4) != 0)
            translate([-L / 2 + 1.5 + i * 3.0, 0, 0.12]) cw_slab(1.5, 0.13, 0.006);
}

// 十字路口块：W x W 沥青 + 褪色人行横道
module cw_ground_cross(W = 7, seed = 0)
{
    cw_m_asphalt(cw_ASPH()) cw_slab(W, W, 0.12);
    cw_m_asphalt(cw_ASPHD()) translate([cw_rndr(seed + 3, -1.5, 1.5), cw_rndr(seed + 11, -1.5, 1.5), 0.12])
        cw_slab(1.8, 1.3, 0.008);
    for (a = [0, 90, 180, 270])
        rotate([0, 0, a])
            cw_m_matte(cw_MARKW()) for (i = [0 : 4])
                if (cw_rnd(seed + a + i * 7, 5) != 0)
                    translate([-W / 2 + 0.9 + i * (W - 1.8) / 4, -W / 2 + 0.75, 0.12])
                        cw_slab(0.4, 1.1, 0.006);
}

// 混凝土板人行道（伸缩缝 + 缺角破损 + 缝里长草）
module cw_ground_sidewalk(L = 8, W = 1.8, seed = 0)
{
    cw_m_concrete(cw_CONC()) cw_slab(L, W, 0.16);
    nj = floor(L / 1.6);
    color(cw_mul(cw_CONCD(), 0.7)) for (i = [1 : nj - 1])
        translate([-L / 2 + i * 1.6, 0, 0.16]) cw_slab(0.03, W, 0.004);
    cw_m_leaf(cw_GRASSD()) for (i = [1 : nj - 1]) if (cw_rnd(seed + i, 3) == 0)
        translate([-L / 2 + i * 1.6, cw_rndr(seed + i, -W / 3, W / 3), 0.16]) rotate([0, 0, 45]) cylinder(h = 0.12, r1 = 0.12, r2 = 0.01, $fn = 4);
    if (cw_rnd(seed, 3) == 0)
        cw_m_concrete(cw_CONCD()) translate([cw_rndr(seed, -L / 3, L / 3), W * 0.2, 0.16]) cw_slab(0.9, 0.7, 0.008);
}

// 荒草地块（深色草斑）
module cw_ground_grass(L = 20, D = 20, seed = 0)
{
    cw_m_turf(cw_GRASSC()) cw_slab(L, D, 0.10);
    for (i = [0 : 5])
        cw_m_turf(cw_GRASSD())
            translate([cw_rndr(seed * 31 + i * 47, -(L - 4) / 2, (L - 4) / 2),
                       cw_rndr(seed * 17 + i * 71 + 3, -(D - 4) / 2, (D - 4) / 2), 0.10])
                scale([cw_rndr(seed + i, 1.6, 3.6), cw_rndr(seed + i + 5, 1.2, 2.8), 1]) cylinder(h = 0.008, r = 0.5, $fn = 9);
}

// 裸土斑块：多边形叠片近似有机轮廓
module cw_ground_dirt(L = 10, D = 8, seed = 0)
{
    cw_m_soil(cw_SOILC())
    {
        rotate([0, 0, cw_rnd(seed, 180)]) scale([L * 0.5, D * 0.5, 1]) cylinder(h = 0.08, r = 1, $fn = 11);
        translate([L * 0.2, D * 0.14, 0]) rotate([0, 0, cw_rnd(seed + 3, 180)])
            scale([L * 0.3, D * 0.26, 1]) cylinder(h = 0.08, r = 1, $fn = 9);
        translate([-L * 0.22, -D * 0.15, 0]) rotate([0, 0, cw_rnd(seed + 5, 180)])
            scale([L * 0.26, D * 0.3, 1]) cylinder(h = 0.08, r = 1, $fn = 9);
    }
    // 车辙积水
    cw_m_water() for (sy = [-1, 1]) translate([cw_rndr(seed + sy, -L * 0.15, L * 0.15), sy * 0.8, 0.08])
        scale([1, 0.25, 1]) cw_decal(seed + sy * 7, L * 0.12, 1, 0.004);
}

// 停车场：沥青 + 褪色白线车位（车位口朝 -y）+ 混凝土轮挡
module cw_ground_parking(L = 18, D = 10, seed = 0)
{
    cw_m_asphalt(cw_ASPH()) cw_slab(L, D, 0.12);
    ns = floor(L / 3.0);
    cw_m_matte(cw_MARKW()) for (i = [0 : ns])
        if (cw_rnd(seed + i * 17, 5) != 0)
            translate([-L / 2 + 0.6 + i * (L - 1.2) / ns, -D / 2 + 2.75, 0.12]) cw_slab(0.12, 5.5, 0.006);
    cw_m_concrete(cw_CONCD()) for (i = [0 : ns - 1]) if (cw_rnd(seed + i * 5, 3) != 0)
        translate([-L / 2 + 0.6 + (i + 0.5) * (L - 1.2) / ns, -D / 2 + 5.0, 0.12]) rotate([0, 0, cw_rndr(seed + i, -8, 8)]) cw_slab(1.4, 0.18, 0.12);
    cw_m_oil() for (i = [0 : 2]) translate([cw_rndr(seed + i * 9, -L / 3, L / 3), -D / 2 + 3, 0.12]) cw_decal(seed + i, 0.4, 0.8, 0.003);
}

// 停机坪：方形混凝土 + 白 H + 圆环
module cw_ground_helipad(S = 12)
{
    cw_m_concrete(cw_CONC()) cw_slab(S, S, 0.15);
    cw_m_matte(cw_MARKW())
    {
        translate([0, 0, 0.15]) difference()
        {
            cylinder(h = 0.006, r = S * 0.36, $fn = 32);
            translate([0, 0, -0.05]) cylinder(h = 0.2, r = S * 0.31, $fn = 32);
        }
        for (sx = [-1, 1]) translate([sx * S * 0.09, 0, 0.15]) cw_slab(0.34, S * 0.3, 0.006);
        translate([0, 0, 0.15]) cw_slab(0.34, 0.9, 0.006);
        rotate([0, 0, 90]) translate([0, 0, 0.15]) cw_slab(0.34, S * 0.12, 0.006);
    }
    cw_m_leaf(cw_GRASSD()) for (i = [0 : 5]) translate([cw_rndr(i * 7, -S / 2, S / 2), (i % 2 == 0 ? -1 : 1) * S / 2, 0.15])
        cylinder(h = 0.2, r1 = 0.2, r2 = 0.01, $fn = 4);
}

// 铁路轨道段（沿 x）：道砟 + 枕木 + 双轨 + 扣件
module cw_ground_rail(L = 20, seed = 0)
{
    cw_m_gravel([0.33, 0.32, 0.30]) hull() { cw_slab(L, 3.0, 0.02); cw_slab(L, 2.4, 0.16); }
    nt = floor(L / 0.65);
    cw_m_timber(cw_WOODD()) for (i = [0 : nt - 1])
        translate([-L / 2 + 0.35 + i * (L - 0.7) / (nt - 1), 0, 0.12]) rotate([0, 0, cw_rndr(seed + i, -2, 2)]) cw_slab(0.24, 2.4, 0.12);
    cw_m_iron([0.30, 0.29, 0.28]) for (sy = [-1, 1])
    {
        translate([0, sy * 0.76, 0.24]) cw_slab(L, 0.14, 0.02);
        translate([0, sy * 0.76, 0.26]) cw_slab(L, 0.03, 0.1);
        translate([0, sy * 0.76, 0.36]) cw_slab(L, 0.07, 0.04);
    }
}

// ================= 建筑（front = -y） =================

// 平屋顶：女儿墙 + 油毡面 + 积水 + 通风帽 / 检修口（屋面顶在 wh+0.03）
module cw_part_flatroof(L, D, wh, seed = 0, par = 0.4, c = [0.33, 0.32, 0.30], vents = true)
{
    cw_m_concrete(c) translate([0, 0, wh]) difference()
    {
        cw_slab(L + 0.16, D + 0.16, par);
        translate([0, 0, -0.05]) cw_slab(L - 0.4, D - 0.4, par + 0.2);
    }
    cw_m_galv(cw_METALD()) translate([0, 0, wh + par]) difference()
    {
        cw_slab(L + 0.22, D + 0.22, 0.04);
        translate([0, 0, -0.05]) cw_slab(L - 0.46, D - 0.46, 0.2);
    }
    cw_m_felt([0.16, 0.16, 0.15]) translate([0, 0, wh]) cw_slab(L - 0.38, D - 0.38, 0.03);
    cw_m_water() translate([cw_rndr(seed, -L * 0.3, L * 0.3), cw_rndr(seed + 3, -D * 0.25, D * 0.25), wh + 0.03]) cw_decal(seed, min(1.4, L * 0.1), 0.6, 0.004);
    if (vents)
    {
        cw_m_galv(cw_METALC()) for (i = [0 : 1]) translate([cw_rndr(seed + i * 7, -L * 0.35, L * 0.35), cw_rndr(seed + i * 9, -D * 0.3, D * 0.3), wh])
        {
            cylinder(h = 0.7, r = 0.11, $fn = 8);
            translate([0, 0, 0.7]) cylinder(h = 0.12, r1 = 0.24, r2 = 0.05, $fn = 8);
        }
        cw_m_steel([0.24, 0.26, 0.22]) translate([L * 0.3, -D * 0.2, wh]) cw_slab(0.8, 0.8, 0.5);
    }
}

// 消防板（苏式红色消防盾：锥形桶、斧、钩杆、铲、灭火器；挂墙，背面 y=+0.03，底 z=0 时板在 0.9~1.9）
module cw_prop_fire_board(seed = 0)
{
    cw_m_oilpaint([0.40, 0.08, 0.05]) translate([0, 0, 1.4]) cw_boxc([1.3, 0.04, 1.0]);
    cw_m_matte(cw_MARKW()) translate([0, -0.025, 1.83]) cw_boxc([1.2, 0.01, 0.07]);
    cw_m_steel([0.42, 0.08, 0.05])
    {
        for (i = [0 : 1]) if (cw_rnd(seed + i, 3) != 0) translate([-0.42 + i * 0.26, -0.13, 1.18]) rotate([180, 0, 0]) cylinder(h = 0.32, r1 = 0.12, r2 = 0.02, $fn = 10);
        translate([0.42, -0.1, 1.05]) cylinder(h = 0.48, r = 0.08, $fn = 10);
    }
    cw_m_iron() translate([0.42, -0.1, 1.53]) cylinder(h = 0.08, r = 0.03, $fn = 6);
    cw_m_timber([0.30, 0.20, 0.12]) translate([0, -0.06, 1.6]) rotate([0, 90, 0]) cylinder(h = 1.2, r = 0.02, center = true, $fn = 5);
    cw_m_iron() translate([0.6, -0.06, 1.6]) cw_boxc([0.04, 0.03, 0.16]);
    if (cw_rnd(seed + 5, 2) == 0)   // 斧子已被人取走，只剩挂钩
    {
        cw_m_timber([0.30, 0.20, 0.12]) translate([0.1, -0.06, 1.35]) rotate([0, 90, 0]) cylinder(h = 0.5, r = 0.018, center = true, $fn = 5);
        cw_m_steel([0.42, 0.08, 0.05]) translate([0.33, -0.06, 1.35]) cw_boxc([0.08, 0.02, 0.16]);
    }
    cw_m_iron() for (sx = [-1, 1]) translate([sx * 0.2, -0.04, 1.45]) cw_boxc([0.03, 0.04, 0.03]);
}

// 告示板（两柱 + 木框 + 褪色告示；front=-y）
module cw_prop_notice_board(seed = 0, msg = "NOTICE")
{
    cw_m_timber(cw_WOODD()) for (sx = [-1, 1]) translate([sx * 0.75, 0, 0.95]) cw_boxc([0.1, 0.1, 1.9]);
    cw_m_planks(cw_WOODC()) translate([0, -0.03, 1.4]) cw_boxc([1.4, 0.04, 0.9]);
    cw_m_planks(cw_WOODD()) translate([0, -0.08, 1.95]) rotate([-25, 0, 0]) cw_boxc([1.6, 0.4, 0.04]);
    translate([0, -0.06, 1.78]) cw_plate(msg, 1.2, 0.16, 0.1, [0.46, 0.45, 0.40], [0.08, 0.08, 0.08], 0.01);
    for (i = [0 : 5]) if (cw_rnd(seed + i, 4) != 0)
        cw_m_paper(i % 3 == 0 ? [0.44, 0.40, 0.30] : [0.48, 0.47, 0.43])
            translate([-0.5 + (i % 3) * 0.48 + cw_rndr(seed + i, -0.05, 0.05), -0.056, 1.5 - floor(i / 3) * 0.36]) rotate([0, cw_rndr(seed + i * 3, -8, 8), 0]) cw_boxc([0.21, 0.004, 0.297]);
}

// 苏式预制板筒子楼（赫鲁晓夫楼）：板缝 + 窗阵（部分钉板）+ 阳台 + 门斗 + 女儿墙
module cw_bldg_panel_flat(seed = 0, floors = 4, L = 16, D = 10)
{
    fh = 2.8;
    wh = floors * fh + 0.5;
    wc = cw_panel_c(seed);
    sc = cw_mul(wc, 0.72);
    nw = max(3, floor((L - 2.6) / 2.2));
    dx = -L * 0.18;
    wx = [for (i = [0 : nw - 1]) -L / 2 + 1.5 + i * (L - 3.0) / (nw - 1)];
    // 底层可进入（门斗洞 + 底层前后窗洞），以上楼层实心
    cw_part_shell(L, D, wh, wc, t = 0.3, dw = 1.5, dh = 2.1, dx = dx, roomH = fh - 0.3,
                  wins = [ for (x = wx) if (abs(x - dx) > 1.2) [0, x, 1.6, 1.0, 1.2],
                           for (x = wx) [1, x, 1.6, 1.0, 1.2] ],
                  seed = seed, wall = "panel", floor = "lino", door = "wood");
    // 板缝（竖向 + 层缝，深色密封胶）
    nb = max(2, floor(L / 3.2));
    cw_m_matte(cw_mul(sc, 0.6)) for (i = [1 : nb - 1], sy = [-1, 1])
        translate([-L / 2 + i * L / nb, sy * (D / 2 + 0.005), wh / 2 + 0.25]) cw_boxc([0.05, 0.02, wh - 0.5]);
    cw_m_matte(cw_mul(sc, 0.6)) for (f = [1 : floors - 1], sy = [-1, 1])
        translate([0, sy * (D / 2 + 0.005), 0.5 + f * fh]) cw_boxc([L, 0.02, 0.05]);
    // 上层窗阵（贴墙，seed 决定破碎 / 钉板 / 窗帘；底层为洞口）
    for (f = [1 : floors - 1], i = [0 : nw - 1], sy = [-1, 1])
        translate([wx[i], sy * (D / 2 + 0.04), 0.5 + f * fh + 1.5])
            rotate([0, 0, sy > 0 ? 180 : 0])
                cw_part_window(1.0, 1.3, seed * 13 + f * 31 + i * 7 + sy * 3, cw_mul(wc, 0.9));
    // 阳台（前立面部分开间，二层起）：楼板 + 波纹栏板 + 住户留下的杂物 / 晾衣绳
    if (floors > 1)
        for (f = [1 : floors - 1], i = [0 : nw - 1])
            if (cw_rnd(seed + f * 17 + i * 29, 3) == 0)
                translate([wx[i], -D / 2 - 0.55, 0.5 + f * fh])
                {
                    k = seed + f * 5 + i;
                    cw_m_concrete(cw_CONCD()) translate([0, 0, 0.75]) cw_slab(1.8, 1.1, 0.12);
                    if (cw_rnd(k, 2) == 0) cw_m_corr(cw_rnd(k + 1, 2) == 0 ? [0.30, 0.33, 0.28] : [0.36, 0.30, 0.22])
                        translate([0, -0.52, 1.35]) cw_boxc([1.8, 0.04, 0.95]);
                    else cw_m_iron() { translate([0, -0.52, 1.8]) cw_boxc([1.8, 0.05, 0.05]); for (x = [-0.8 : 0.2 : 0.81]) translate([x, -0.52, 1.35]) cw_boxc([0.02, 0.02, 0.9]); }
                    if (cw_rnd(k + 3, 2) == 0) cw_m_planks(cw_WOODC()) translate([0.5, 0.1, 0.87]) cw_slab(0.5, 0.4, 0.5);
                    if (cw_rnd(k + 4, 3) == 0) cw_m_fabric(cw_fabric_c(k)) for (j = [0 : 2]) translate([-0.6 + j * 0.42, -0.2, 1.75]) cw_boxc([0.3, 0.01, 0.45]);
                }
    // 门斗雨棚 + 门牌号
    cw_m_concrete(cw_CONCD()) translate([dx, -D / 2 - 0.6, 2.7]) cw_boxc([1.9, 1.2, 0.12]);
    translate([dx + 1.2, -D / 2 - 0.02, 2.4]) cw_plate(str(1 + cw_rnd(seed, 40)), 0.32, 0.24, 0.14, cw_SIGNB(), cw_MARKW(), 0.02);
    if (cw_rnd(seed + 6, 2) == 0) cw_m_matte([0.40, 0.07, 0.05]) translate([dx - 1.6, -D / 2 - 0.02, 1.4]) rotate([90, 0, 0])
        linear_extrude(height = 0.01) text(["EVAC 14.6", "NO WATER", "HELP"][cw_rnd(seed, 3)], size = 0.3, halign = "center", valign = "center");
    cw_part_flatroof(L, D, wh, seed, 0.5, sc);
    cw_m_concrete(sc) translate([L * 0.25, D * 0.12, wh]) cw_slab(1.6, 1.4, 1.6);
    cw_m_iron() translate([-L * 0.2, -D * 0.1, wh]) { cylinder(h = 2.4, r = 0.03, $fn = 5); for (z = [1.6, 2.1]) translate([0, 0, z]) rotate([0, 90, 30]) cylinder(h = 1.2, r = 0.015, center = true, $fn = 4); }
    cw_part_decay(seed, L, D, dx, 1.5, wh);
}

// 乡村木屋：原木墙 + 白窗套 + 金属坡顶 + 砖烟囱 + 门廊
module cw_bldg_house_rural(seed = 0, L = 7, D = 5.5)
{
    wh = 2.5;
    rc = cw_roof_c(seed + 3);
    wc = cw_rnd(seed, 2) == 0 ? cw_WOODC() : [0.40, 0.31, 0.20];
    dx = (cw_rnd(seed + 5, 2) == 0 ? -1 : 1) * L * 0.24;
    // 可进入空心木屋：门洞 + 前/侧窗洞
    cw_part_shell(L, D, wh, wc, t = 0.24, dw = 1.3, dh = 2.0, dx = dx,
                  wins = [[0, -dx, 1.5, 0.9, 1.1], [2, D * 0.12, 1.5, 0.9, 1.1], [3, -D * 0.12, 1.5, 0.9, 1.1]],
                  seed = seed, wall = "log", floor = "boards", door = "wood");
    // 原木端头（墙角交叉出头）
    cw_m_timber(cw_mul(wc, 0.9)) for (sx = [-1, 1], sy = [-1, 1], z = [0.4 : 0.42 : wh - 0.1])
        translate([sx * (L / 2 + 0.06), sy * (D / 2 + 0.06), z]) rotate([0, 90, 0]) cylinder(h = 0.24, r = 0.12, center = true, $fn = 7);
    translate([0, 0, wh]) cw_part_roof(L, D, 1.6, 0.5, 0, rc, cw_rnd(seed + 7, 2) == 0 ? "tin" : "eternit");
    // 木山墙封板
    cw_m_planks(cw_WOODD()) for (sx = [-1, 1]) translate([sx * (L / 2 - 0.06), 0, wh]) rotate([90, 0, 90])
        linear_extrude(height = 0.08, center = true) polygon([[-D / 2, 0], [D / 2, 0], [0, 1.55]]);
    cw_m_brick(cw_BRICKD()) translate([L * 0.26, D * 0.15, wh + 0.8]) cw_boxc([0.5, 0.5, 2.0]);
    cw_m_concrete(cw_CONCD()) translate([L * 0.26, D * 0.15, wh + 1.82]) cw_boxc([0.62, 0.62, 0.08]);
    // 门廊台阶 + 雨棚（台阶与室内同高，门洞不设门槛）
    cw_m_planks(cw_WOODD()) translate([dx, -D / 2 - 0.5, 0]) cw_slab(1.5, 0.9, 0.06);
    cw_m_timber(cw_WOODD()) for (sx = [-1, 1]) translate([dx + sx * 0.82, -D / 2 - 0.95, 1.12]) cw_boxc([0.1, 0.1, 2.24]);
    cw_m_roof("tin", rc) translate([dx, -D / 2 - 0.55, 2.3]) rotate([-14, 0, 0]) cw_boxc([1.9, 1.15, 0.04]);
    // 窗套 + 百叶（都在洞口外缘，不挡通行）
    cw_m_oilpaint(cw_MARKW())
    {
        for (z = [0.92, 2.08]) translate([-dx, -D / 2 - 0.04, z]) cw_boxc([1.12, 0.1, 0.07]);
        for (z = [0.92, 2.08]) translate([-L / 2 - 0.04, D * 0.12, z]) cw_boxc([0.1, 1.12, 0.07]);
        for (z = [0.92, 2.08]) translate([L / 2 + 0.04, -D * 0.12, z]) cw_boxc([0.1, 1.12, 0.07]);
    }
    sh = [[0.18, 0.29, 0.25], [0.34, 0.20, 0.15], [0.25, 0.29, 0.36]][cw_rnd(seed + 2, 3)];
    cw_m_oilpaint(sh) for (sx = [-1, 1])
        translate([-dx + sx * 0.72, -D / 2 - 0.08, 1.5]) rotate([0, 0, sx * 20]) translate([sx * 0.2, 0, 0]) cw_boxc([0.4, 0.04, 1.1]);
    // 屋主钉死了一扇窗、门边堆着柴，墙上挂着锌盆
    if (cw_rnd(seed + 9, 3) == 0)
        cw_m_planks(cw_WOODC()) for (i = [0 : 2])
            translate([-dx, -D / 2 - 0.12, 1.15 + i * 0.32]) rotate([0, cw_rnd(seed + i, 16) - 8, 0]) cw_boxc([1.2, 0.04, 0.16]);
    translate([L / 2 + 0.45, D * 0.2, 0]) rotate([0, 0, 90]) cw_prop_woodpile(seed);
    cw_m_galv() translate([-L / 2 - 0.08, -D * 0.3, 1.6]) rotate([0, 90, 0]) cylinder(h = 0.14, r1 = 0.3, r2 = 0.24, $fn = 12);
    cw_part_decay(seed, L, D, dx, 1.3, wh);
}

// 单层临街商铺排：抹灰墙 + 大橱窗（部分钉板 / 卷帘）+ 招牌字
module cw_bldg_shop_row(seed = 0, L = 10, D = 6)
{
    wh = 3.4;
    wc = cw_wall_c(seed);
    sc = cw_goods_c(seed + 2);
    nw = max(2, floor(L / 3.4));
    dx = L / 2 - 1.2;
    wins = [ for (i = [0 : nw - 1]) let (wx = -L / 2 + (i + 0.5) * L / nw) if (abs(wx - dx) > 1.4) [0, wx, 1.5, 2.0, 1.6] ];
    cw_part_shell(L, D, wh, wc, t = 0.22, dw = 1.0, dh = 2.1, dx = dx, roomH = wh - 0.35, wins = wins,
                  seed = seed, wall = cw_rnd(seed, 5) == 1 ? "brick" : "plaster", floor = "tiles", door = "wood");
    cw_part_flatroof(L, D, wh, seed, 0.3, cw_CONCD(), false);
    // 招牌带 + 字
    cw_m_matte(sc) translate([0, -D / 2 - 0.10, 2.85]) cw_boxc([L - 0.6, 0.16, 0.7]);
    cw_m_matte(cw_MARKW()) translate([-L * 0.05, -D / 2 - 0.185, 2.85]) rotate([90, 0, 0]) linear_extrude(height = 0.01, center = true)
        text(["POTRAVINY", "MAGAZIN", "DROGERIE", "PRODEJNA", "KIOSK"][cw_rnd(seed + 4, 5)], size = 0.34, halign = "center", valign = "center");
    // 卷帘（拉下一半，被撬起一角）
    for (w = wins) if (cw_rnd(seed + round(w[1] * 7), 2) == 0)
        cw_m_galv(cw_METALC()) translate([w[1], -D / 2 - 0.08, 2.3 - 0.4]) cw_boxc([2.0, 0.04, 0.8]);
    cw_m_steel(cw_mul(sc, 0.8)) translate([dx + 0.75, -D / 2 - 0.12, 2.3]) cw_boxc([0.1, 0.1, 0.25]);
    cw_part_decay(seed, L, D, dx, 1.0, wh);
}

// 城市超市（大盒子）：肋板墙 + 玻璃门面带 + 大招牌 + 后侧装卸台
module cw_bldg_supermarket(seed = 0, L = 18, D = 12)
{
    wh = 4.6;
    dx = -L * 0.12;
    cw_part_shell(L, D, wh, cw_PANEL(), t = 0.3, dw = 2.0, dh = 2.5, dx = dx, roomH = wh - 0.4,
                  wins = [[0, dx - L * 0.22, 1.4, L * 0.16, 1.9], [0, dx + L * 0.22, 1.4, L * 0.16, 1.9],
                          [1, -L * 0.22, 1.8, 1.4, 1.2], [1, -L * 0.02, 1.8, 1.4, 1.2],
                          [2, 0, 1.8, 1.4, 1.2], [3, 0, 1.8, 1.4, 1.2]],
                  seed = seed, wall = "panel", floor = "tiles", door = "none");
    // 自动门：一扇被砸碎，一扇卡在半开
    cw_m_galv(cw_METALC()) translate([dx, -D / 2 - 0.03, 2.45]) cw_boxc([2.2, 0.08, 0.1]);
    cw_m_galv(cw_METALC()) translate([dx - 1.55, -D / 2 - 0.08, 1.2]) cw_boxc([1.0, 0.04, 2.4]);
    cw_m_glass() translate([dx - 1.55, -D / 2 - 0.1, 1.2]) cw_boxc([0.9, 0.01, 2.3]);
    // 竖肋（前排避开店面洞口 → 充当橱窗竖梃）
    cw_m_concrete(cw_PANELD()) for (i = [0 : floor(L / 1.6)])
    {
        rx = -L / 2 + 0.8 + i * 1.6;
        translate([rx, D / 2 + 0.02, wh / 2 + 0.6]) cw_boxc([0.18, 0.05, wh - 1.6]);
        if (abs(rx - dx) > 1.2)
            translate([rx, -D / 2 - 0.02, wh / 2 + 0.6]) cw_boxc([0.18, 0.05, wh - 1.6]);
    }
    // 大招牌带（红底白字）+ 掉了两个字母
    cw_m_matte(cw_REDC()) translate([0, -D / 2 - 0.14, wh - 0.55]) cw_boxc([L - 1.0, 0.2, 0.95]);
    cw_m_matte(cw_MARKW()) translate([0, -D / 2 - 0.25, wh - 0.55]) rotate([90, 0, 0]) linear_extrude(height = 0.01, center = true)
        text(cw_rnd(seed, 2) == 0 ? "SAMOOBSLUHA" : "UNIVERSAM", size = 0.55, halign = "center", valign = "center");
    cw_m_matte(cw_MARKW()) translate([L * 0.3, -D / 2 - 0.6, 0.02]) rotate([0, 0, 30]) cw_boxc([0.35, 0.05, 0.03]);
    cw_part_flatroof(L, D, wh, seed, 0.4, cw_CONCD());
    cw_m_galv(cw_METALC()) translate([L * 0.22, D * 0.1, wh]) { cw_slab(2.0, 1.4, 0.9); translate([0.5, 0, 0.9]) cylinder(h = 0.04, r = 0.45, $fn = 16); }
    // 背面装卸台 + 卷帘门 + 堆放的托盘
    cw_m_concrete(cw_CONCD()) translate([L * 0.2, D / 2 + 0.9, 0]) cw_slab(4.5, 1.8, 1.0);
    cw_m_iron() for (sx = [-1, 1]) translate([L * 0.2 + sx * 2.1, D / 2 + 1.75, 1.0]) cw_boxc([0.2, 0.1, 0.25]);
    cw_m_corr(cw_METALD()) translate([L * 0.2, D / 2 + 0.04, 2.3]) cw_boxc([2.6, 0.08, 2.6]);
    translate([L * 0.2 - 3.4, D / 2 + 1.0, 0]) { cw_prop_pallet(seed); translate([0, 0, 0.15]) cw_prop_pallet(seed + 1); }
    translate([-L * 0.35, D / 2 + 1.2, 0]) cw_prop_dumpster();
    cw_part_decay(seed, L, D, dx, 3.0, wh);
}

// 城市医院：白灰板楼 + 红十字 + 入口雨棚柱廊 + 急救坡道 + 撤离时的检疫现场
module cw_bldg_hospital(seed = 0, floors = 3, L = 18, D = 9)
{
    fh = 3.0;
    wh = floors * fh + 0.5;
    nw = max(3, floor((L - 2.6) / 2.0));
    wx = [for (i = [0 : nw - 1]) -L / 2 + 1.4 + i * (L - 2.8) / (nw - 1)];
    // 底层可进入（中央大门 + 底层前后窗洞），以上楼层实心
    cw_part_shell(L, D, wh, cw_WHITEW(), t = 0.3, dw = 2.2, dh = 2.4, dx = 0, roomH = fh - 0.3,
                  wins = [ for (x = wx) if (abs(x) > 1.4) [0, x, 1.55, 1.1, 1.4],
                           for (x = wx) if (abs(x) > 1.4) [1, x, 1.55, 1.1, 1.4] ],
                  seed = seed, wall = "plaster", floor = "tiles", door = "wood", dc = [0.40, 0.42, 0.40]);
    // 底层瓷砖墙裙外饰面（浅绿釉面砖）
    cw_m_floortile([0.30, 0.38, 0.34]) for (sy = [-1, 1]) difference()
    {
        translate([0, sy * (D / 2 + 0.012), 0.48]) cw_boxc([L + 0.02, 0.02, 0.96]);
        if (sy < 0) translate([0, sy * D / 2, 1.2]) cw_boxc([2.2, 0.2, 2.4]);
        for (x = wx) if (abs(x) > 1.4) translate([x, sy * D / 2, 1.55]) cw_boxc([1.1, 0.2, 1.4]);
    }
    // 层间带
    cw_m_concrete([0.40, 0.43, 0.43]) for (f = [1 : floors - 1], sy = [-1, 1])
        translate([0, sy * (D / 2 + 0.02), 0.5 + f * fh]) cw_boxc([L, 0.05, 0.5]);
    // 上层窗阵（贴墙装饰；底层为洞口）—— 有几扇被白床单封住，上面写着字
    for (f = [1 : floors - 1], i = [0 : nw - 1], sy = [-1, 1])
        translate([wx[i], sy * (D / 2 + 0.04), 0.5 + f * fh + 1.55])
            rotate([0, 0, sy > 0 ? 180 : 0]) cw_part_window(1.1, 1.4, seed * 11 + f * 7 + i * 3 + sy, cw_mul(cw_WHITEW(), 0.92));
    if (floors > 1) translate([wx[nw - 2], -D / 2 - 0.1, 0.5 + fh + 1.55])
    {
        cw_m_fabric([0.46, 0.46, 0.43]) cw_boxc([1.5, 0.02, 1.9]);
        cw_m_matte([0.38, 0.06, 0.04]) translate([0, -0.012, 0]) rotate([90, 0, 0]) linear_extrude(height = 0.004, center = true)
            text("SOS", size = 0.42, halign = "center", valign = "center");
    }
    // 红十字灯箱 + 院名
    cw_m_matte(cw_MARKW()) translate([0, -D / 2 - 0.12, wh - 0.9]) cw_boxc([1.7, 0.16, 1.7]);
    cw_m_matte(cw_REDC())
    {
        translate([0, -D / 2 - 0.205, wh - 0.9]) cw_boxc([0.42, 0.01, 1.3]);
        translate([0, -D / 2 - 0.205, wh - 0.9]) cw_boxc([1.3, 0.01, 0.42]);
    }
    translate([0, -D / 2 - 2.62, 3.36]) cw_plate("HOSPITAL", 3.6, 0.42, 0.3, [0.46, 0.46, 0.43], [0.30, 0.06, 0.04], 0.06);
    // 入口柱廊雨棚 + 坡道（坡道在门旁，不挡门洞）
    cw_m_concrete(cw_CONCD())
    {
        translate([0, -D / 2 - 1.3, 3.0]) cw_boxc([5.5, 2.6, 0.2]);
        for (sx = [-1, 1]) translate([sx * 2.3, -D / 2 - 2.2, 1.5]) cw_boxc([0.3, 0.3, 3.0]);
        translate([-4.2, -D / 2 - 0.9, 0]) rotate([0, -7, 0]) cw_slab(3.6, 1.6, 0.36);
    }
    cw_m_iron() for (sy = [-1, 1]) translate([-4.2, -D / 2 - 0.9 + sy * 0.78, 0.9]) rotate([0, -7, 0]) cw_boxc([3.6, 0.04, 0.04]);
    // 检疫现场：门旁的沙袋、隔离告示、倒下的担架、氧气瓶、排在墙边的尸袋
    translate([3.6, -D / 2 - 1.6, 0]) rotate([0, 0, 90]) cw_prop_wall_sandbag(len = 2.2);
    translate([-2.6, -D / 2 - 3.4, 0]) rotate([0, 0, 12]) cw_prop_sign_quarantine(seed, "QUARANTINE");
    translate([1.6, -D / 2 - 3.6, 0]) rotate([0, 0, 70]) cw_prop_stretcher(seed);
    cw_m_steel([0.12, 0.22, 0.32]) for (i = [0 : 2]) translate([L / 2 - 0.4 - i * 0.28, -D / 2 - 0.3, 0])
        rotate([cw_rnd(seed + i, 3) == 0 ? 80 : 0, 0, 0]) { cylinder(h = 1.3, r = 0.11, $fn = 10); translate([0, 0, 1.3]) sphere(r = 0.11, $fn = 8); }
    for (i = [0 : 2]) translate([L / 2 + 0.7, -D / 2 + 1.2 + i * 0.85, 0]) rotate([0, 0, -90 + cw_rndr(seed + i, -6, 6)]) cw_prop_body_bag_straight(seed + i);
    cw_part_flatroof(L, D, wh, seed, 0.45, [0.42, 0.43, 0.41]);
    cw_m_plaster(cw_WHITEW()) translate([-L * 0.28, 0, wh]) cw_slab(2.4, 2.6, 1.7);
    cw_part_litter(seed, L - 1, D - 1, 10, true, false);
    cw_part_decay(seed, L, D, 0, 6.0, wh);
}

// 厂房（双坡 + 天窗脊）：砖墙 + 高窗带 + 大推拉门
module cw_bldg_factory_hall(seed = 0, L = 14, D = 9)
{
    wh = 4.6;
    bc = cw_rnd(seed, 2) == 0 ? cw_BRICK() : [0.40, 0.37, 0.31];
    cw_part_shell(L, D, wh, bc, t = 0.3, dw = 3.0, dh = 3.4, dx = -L * 0.15,
                  wins = [[0, L * 0.28, 3.95, L * 0.3, 1.0], [1, 0, 3.95, L - 2.4, 1.0]],
                  seed = seed, wall = cw_rnd(seed, 2) == 0 ? "brick" : "plaster", floor = "concrete", door = "none");
    translate([0, 0, wh]) cw_part_roof(L, D, 1.8, 0.4, 0, cw_roof_c(seed + 1), "eternit");
    // 天窗脊（玻璃多处碎掉）
    cw_m_glass() translate([0, 0, wh + 1.35]) cw_boxc([L * 0.65, 0.9, 0.7]);
    cw_m_steel(cw_METALD()) translate([0, 0, wh + 1.75]) cw_boxc([L * 0.68, 1.1, 0.12]);
    color(cw_DARKC()) for (i = [0 : 2]) translate([-L * 0.25 + i * L * 0.2, -0.46, wh + 1.35]) cw_boxc([0.8, 0.02, 0.5]);
    // 推拉门：导轨 + 被拉开到一侧的门扇
    cw_m_iron() translate([-L * 0.15, -D / 2 - 0.13, 3.55]) cw_boxc([6.4, 0.08, 0.14]);
    cw_m_corr([0.28, 0.32, 0.30]) translate([-L * 0.15 - 3.1, -D / 2 - 0.2, 1.75]) cw_boxc([3.1, 0.06, 3.4]);
    cw_m_matte(cw_MARKW()) translate([L * 0.28, -D / 2 - 0.02, 2.2]) rotate([90, 0, 0]) linear_extrude(height = 0.01, center = true)
        text(str("HALA ", 1 + cw_rnd(seed, 6)), size = 0.5, halign = "center", valign = "center");
    cw_part_litter(seed, L - 1, D - 1, 4, false, true);
    cw_part_decay(seed, L, D, -L * 0.15, 3.0, wh);
}

// 锯齿厂房：三连锯齿顶（北向天窗）+ 砖墙 + 侧门
module cw_bldg_factory_saw(seed = 0, L = 14, D = 10)
{
    wh = 3.8;
    bc = cw_rnd(seed, 2) == 0 ? cw_BRICK() : cw_PANELD();
    cw_part_shell(L, D, wh, bc, t = 0.3, dw = 2.2, dh = 3.0, dx = -L * 0.2,
                  wins = [[0, L * 0.25, 1.05, 1.1, 2.1],
                          for (i = [0 : 2]) [2, -D / 2 + 2 + i * 3, 2.6, 1.0, 1.2]],
                  seed = seed, wall = cw_rnd(seed, 2) == 0 ? "brick" : "panel", floor = "concrete", door = "steel");
    nt = 3;
    for (i = [0 : nt - 1])
    {
        ty = -D / 2 + (i + 0.5) * D / nt;
        cw_m_tin(cw_roof_c(seed + 2)) translate([0, ty + D / nt * 0.08, wh + 0.75])
            rotate([-22, 0, 0]) cw_boxc([L + 0.4, D / nt * 1.12, 0.06]);
        cw_m_glass() translate([0, ty - D / nt * 0.42, wh + 0.62]) rotate([70, 0, 0]) cw_boxc([L - 0.8, 1.2, 0.04]);
        cw_m_steel(cw_METALD()) for (x = [-L / 2 + 0.6 : 1.4 : L / 2 - 0.5]) translate([x, ty - D / nt * 0.42, wh + 0.62]) rotate([70, 0, 0]) cw_boxc([0.06, 1.22, 0.06]);
    }
    for (sx = [-1, 1]) cw_m_brick(cw_mul(bc, 0.95)) translate([sx * (L / 2 - 0.15), 0, wh]) rotate([90, 0, 90])
        linear_extrude(height = 0.3, center = true) polygon([for (i = [0 : nt - 1]) each [[-D / 2 + i * D / nt, 0], [-D / 2 + (i + 0.08) * D / nt, 1.2]], [D / 2, 0]]);
    cw_part_decay(seed, L, D, -L * 0.2, 2.2, wh);
}

// 砖砌烟囱：锥柱 + 白环带 + 顶箍 + 爬梯
module cw_bldg_smokestack(seed = 0, h = 15)
{
    cw_m_brick(cw_BRICKD()) cylinder(h = 1.2, r = 1.7, $fn = 12);
    cw_m_brick(cw_BRICK()) cylinder(h = h, r1 = 1.35, r2 = 0.85, $fn = 12);
    cw_m_oilpaint(cw_MARKW()) translate([0, 0, h * 0.82]) cylinder(h = h * 0.06, r1 = 0.97, r2 = 0.94, $fn = 12);
    cw_m_oilpaint(cw_MARKW()) translate([0, 0, h * 0.62]) cylinder(h = h * 0.06, r1 = 1.06, r2 = 1.03, $fn = 12);
    cw_m_iron() translate([0, 0, h - 0.3]) cylinder(h = 0.35, r = 0.92, $fn = 12);
    color(cw_mul(cw_DARKC(), 0.5)) translate([0, 0, h - 0.6]) cylinder(h = 0.62, r = 0.7, $fn = 12);
    cw_m_iron() for (i = [0 : floor((h - 1.6) / 0.4)]) let(z = 1.4 + i * 0.4, r = 1.35 - (1.35 - 0.85) * z / h)
        translate([0, -r - 0.12, z]) cw_boxc([0.4, 0.03, 0.03]);
    cw_m_iron() for (sx = [-1, 1]) cw_rod([sx * 0.2, -1.47, 1.2], [sx * 0.2, -0.97, h - 0.5], 0.02, 4);
}

// 金属仓库：瓦楞墙 + 浅坡金属顶 + 推拉大门
module cw_bldg_warehouse(seed = 0, L = 12, D = 8)
{
    wh = 3.6;
    mc = cw_rnd(seed, 2) == 0 ? cw_METALC() : [0.32, 0.36, 0.32];
    dx = -L * 0.12;
    cw_part_shell(L, D, wh, mc, t = 0.26, dw = 2.8, dh = 2.6, dx = dx,
                  wins = [[0, L * 0.35, wh - 0.7, 1.2, 0.6], [1, L * 0.35, wh - 0.7, 1.2, 0.6],
                          [1, -L * 0.25, wh - 0.7, 1.2, 0.6]],
                  seed = seed, wall = "metal", floor = "concrete", door = "none", skin = false);
    // 钢立柱（外露）+ 推拉门（一扇拉开、一扇脱轨歪斜）
    cw_m_steel(cw_METALD()) for (i = [0 : floor(L / 3)], sy = [-1, 1]) let(x = -L / 2 + 0.1 + i * (L - 0.2) / floor(L / 3))
        if (sy > 0 || abs(x - dx) > 1.6) translate([x, sy * (D / 2 + 0.05), wh / 2]) cw_boxc([0.14, 0.1, wh]);
    cw_m_iron() translate([dx, -D / 2 - 0.12, 2.75]) cw_boxc([5.8, 0.08, 0.12]);
    cw_m_corr(cw_mul(mc, 0.85)) translate([dx - 2.1, -D / 2 - 0.18, 1.3]) cw_boxc([1.45, 0.06, 2.6]);
    cw_m_corr(cw_mul(mc, 0.85)) translate([dx + 2.1, -D / 2 - 0.3, 1.25]) rotate([0, 6, -8]) cw_boxc([1.45, 0.06, 2.6]);
    translate([0, 0, wh]) cw_part_roof(L, D, 1.0, 0.45, 0, cw_roof_c(seed + 4), "tin");
    cw_m_matte(cw_YELLOWC()) translate([L * 0.25, -D / 2 - 0.02, 1.6]) rotate([90, 0, 0]) linear_extrude(height = 0.01, center = true)
        text(str("SKLAD ", 1 + cw_rnd(seed, 9)), size = 0.42, halign = "center", valign = "center");
    cw_part_litter(seed, L - 1, D - 1, 3, false, true);
    cw_part_decay(seed, L, D, dx, 2.8, wh);
}

// 加油站雨棚 + 收银亭（油泵用 cw_prop_pump_gas 另摆；front=-y）
module cw_bldg_gas_canopy(seed = 0)
{
    cw_m_concrete(cw_CONC()) translate([0, 0, 0]) cw_slab(11, 7, 0.12);
    cw_m_steel([0.36, 0.36, 0.34]) for (sx = [-1, 1]) translate([sx * 3.0, 0, 1.95]) cw_boxc([0.4, 0.4, 3.8]);
    cw_m_concrete(cw_CONCD()) for (sx = [-1, 1]) translate([sx * 3.0, 0, 0]) cw_slab(1.4, 0.9, 0.25);
    cw_m_matte(cw_MARKW()) translate([0, 0, 3.9]) cw_boxc([10.5, 6.5, 0.35]);
    cw_m_matte(cw_REDC()) for (sy = [-1, 1]) translate([0, sy * 3.27, 3.9]) cw_boxc([10.5, 0.04, 0.3]);
    cw_m_matte(cw_REDC()) for (sx = [-1, 1]) translate([sx * 5.27, 0, 3.9]) cw_boxc([0.04, 6.5, 0.3]);
    cw_m_lamp() for (sx = [-2, 0, 2], sy = [-1, 1]) translate([sx * 2, sy * 1.6, 3.71]) cw_boxc([1.2, 0.3, 0.03]);
    color(cw_DARKC()) translate([2, 1.6, 3.69]) cw_boxc([1.2, 0.3, 0.03]);   // 碎掉的灯罩
    cw_m_matte(cw_MARKW()) translate([-2.4, -3.31, 3.9]) rotate([90, 0, 0]) linear_extrude(height = 0.01, center = true)
        text("BENZINA", size = 0.22, halign = "center", valign = "center");
    // 收银亭（雨棚后侧，可进入：右门洞 + 左橱窗洞）
    translate([0, 4.9, 0])
    {
        cw_part_shell(4.5, 3.2, 2.9, cw_WHITEW(), t = 0.2, dw = 0.95, dh = 2.1, dx = 1.5, roomH = 2.6,
                      wins = [[0, -0.6, 1.5, 1.9, 1.2]], seed = seed, wall = "plaster", floor = "tiles", door = "wood");
        cw_m_concrete(cw_CONCD()) translate([0, 0, 2.9]) cw_slab(4.7, 3.4, 0.15);
        cw_m_matte(cw_REDC()) translate([0, -1.7, 2.55]) cw_boxc([4.0, 0.1, 0.5]);
    }
}

// 军用瞭望塔：四内倾腿 + 平台 + 舱室环窗 + 攒尖顶 + 爬梯 + 沙袋 + 探照灯
module cw_bldg_guard_tower(seed = 0)
{
    ph = 4.2;
    tl = 4;      // 腿倾角：顶端内收
    r0 = 1.0;    // 腿在杆件中点高度的半间距
    // 顶端内收 = rotate 的 X 角与 y 偏移同号、Y 角与 x 偏移反号
    cw_m_timber(cw_WOODD()) for (sx = [-1, 1], sy = [-1, 1])
        translate([sx * r0, sy * r0, ph / 2]) rotate([sy * tl, sx * -tl, 0]) cw_boxc([0.18, 0.18, ph + 0.4]);
    cw_m_concrete(cw_CONCD()) for (sx = [-1, 1], sy = [-1, 1]) translate([sx * (r0 + 0.15), sy * (r0 + 0.15), 0]) cw_slab(0.5, 0.5, 0.25);
    cw_m_timber(cw_WOODD()) for (z = [1.4, 2.8])
    {
        rz = r0 + (ph / 2 - z) * tan(tl) + 0.05;   // 横撑跟随腿的收分
        translate([0, -rz, z]) cw_boxc([rz * 2 + 0.2, 0.08, 0.12]);
        translate([0, rz, z]) cw_boxc([rz * 2 + 0.2, 0.08, 0.12]);
        translate([-rz, 0, z]) cw_boxc([0.08, rz * 2 + 0.2, 0.12]);
        translate([rz, 0, z]) cw_boxc([0.08, rz * 2 + 0.2, 0.12]);
    }
    // X 形斜撑（每面一条）
    cw_m_timber(cw_WOODC()) for (a = [0, 90, 180, 270]) rotate([0, 0, a])
        cw_beam([-r0 - 0.08, -r0 - 0.12, 0.3], [r0 - 0.02, -r0 + 0.05, 1.4], 0.08);
    cw_m_planks(cw_WOODC()) translate([0, 0, ph]) cw_slab(2.9, 2.9, 0.14);
    // 舱室：木板墙（军绿漆）+ 环窗（部分碎）+ 角柱
    cw_m_planks(cw_OLIVED()) translate([0, 0, ph + 0.14]) difference() { cw_slab(2.6, 2.6, 1.0); translate([0, 0, 0.1]) cw_slab(2.48, 2.48, 1.2); }
    cw_m_planks(cw_mul(cw_OLIVED(), 0.9)) translate([0, 0, ph + 0.14]) cw_slab(2.48, 2.48, 0.02);
    cw_m_timber(cw_OLIVED()) for (sx = [-1, 1], sy = [-1, 1])
        translate([sx * 1.24, sy * 1.24, ph + 1.45]) cw_boxc([0.14, 0.14, 0.66]);
    cw_m_glass() for (a = [0, 90, 180, 270]) rotate([0, 0, a]) if (cw_rnd(seed + a, 3) != 0)
        translate([0, -1.25, ph + 1.45]) cw_boxc([2.3, 0.01, 0.58]);
    cw_m_planks(cw_OLIVED()) translate([0, 0, ph + 1.78]) cw_slab(2.62, 2.62, 0.06);
    translate([0, 0, ph + 1.84]) cw_part_roof(3.1, 3.1, 1.0, 0.2, 1.55, cw_ROOFM(), "tin", false);
    // 舱外：沙袋护墙 + 探照灯 + 军用电话箱
    translate([0, -1.65, ph + 0.14]) cw_prop_wall_sandbag(len = 2.2);
    cw_m_steel(cw_OLIVED()) translate([1.0, -1.0, ph + 2.8]) rotate([0, 0, 20])
    {
        translate([0, 0, 0.1]) rotate([62, 0, 0]) cylinder(h = 0.4, r = 0.24, $fn = 10);
        cylinder(h = 0.12, r = 0.06, $fn = 6);
    }
    cw_m_lamp() translate([1.0, -1.0, ph + 2.8]) rotate([0, 0, 20]) translate([0, -0.36, 0.3]) rotate([62, 0, 0]) cylinder(h = 0.02, r = 0.22, $fn = 10);
    cw_m_steel(cw_OLIVE()) translate([1.31, 0.6, ph + 1.0]) cw_boxc([0.06, 0.3, 0.4]);
    // 爬梯
    cw_m_iron()
    {
        for (sx = [-1, 1]) translate([sx * 0.25, -1.35, ph / 2 + 0.2]) cw_boxc([0.05, 0.05, ph + 0.4]);
        for (i = [0 : 7]) translate([0, -1.35, 0.5 + i * 0.5]) cw_boxc([0.5, 0.04, 0.04]);
    }
}

// 军营板房：长条单层 + 抹灰墙 + 成排窗 + 低坡绿铁皮顶 + 门廊 + 营区生活痕迹
module cw_bldg_barracks(seed = 0, L = 14, D = 6)
{
    wh = 2.8;
    wc = cw_rnd(seed, 2) == 0 ? [0.40, 0.38, 0.27] : [0.36, 0.37, 0.28];
    nw = max(3, floor((L - 2.4) / 2.0));
    wx = [for (i = [0 : nw - 1]) -L / 2 + 1.4 + i * (L - 2.8) / (nw - 1)];
    cw_part_shell(L, D, wh, wc, t = 0.24, dw = 1.5, dh = 2.1, dx = 0,
                  wins = [ for (x = wx) [1, x, 1.6, 0.9, 1.0],
                           for (x = wx) if (abs(x) > 1.1) [0, x, 1.6, 0.9, 1.0] ],
                  seed = seed, wall = "plaster", floor = "boards", door = "wood", dc = [0.16, 0.22, 0.15]);
    // 硅酸盐砖勒脚
    cw_m_silicate([0.40, 0.39, 0.35]) difference()
    {
        cw_slab(L + 0.06, D + 0.06, 0.7);
        translate([0, 0, -0.1]) cw_slab(L - 0.1, D - 0.1, 1);
        translate([0, -D / 2, 0.3]) cw_boxc([1.5, 1, 1]);
    }
    rc = [0.17, 0.25, 0.18];
    translate([0, 0, wh]) cw_part_roof(L, D, 1.1, 0.45, 0, rc, "tin");
    cw_m_plaster(wc) for (sx = [-1, 1]) translate([sx * (L / 2 - 0.12), 0, wh]) rotate([90, 0, 90])
        linear_extrude(height = 0.24, center = true) polygon([[-D / 2, 0], [D / 2, 0], [0, 1.08]]);
    // 炉子烟囱（铁皮管穿屋面）
    cw_m_iron() for (sx = [-1, 1]) translate([sx * L * 0.32, D * 0.1, wh + 0.4]) { cylinder(h = 1.3, r = 0.1, $fn = 8); translate([0, 0, 1.3]) cylinder(h = 0.1, r1 = 0.2, r2 = 0.05, $fn = 8); }
    // 门廊：雨棚 + 两柱 + 门牌（连队号）
    cw_m_tin(rc) translate([0, -D / 2 - 0.7, 2.45]) rotate([-12, 0, 0]) cw_boxc([2.6, 1.5, 0.04]);
    cw_m_timber(cw_WOODD()) for (sx = [-1, 1]) translate([sx * 1.15, -D / 2 - 1.3, 1.2]) cw_boxc([0.1, 0.1, 2.4]);
    translate([1.25, -D / 2 - 0.02, 1.9]) cw_plate(str((1 + cw_rnd(seed, 4)), ". ROTA"), 0.6, 0.24, 0.09, [0.12, 0.17, 0.11], [0.50, 0.48, 0.40], 0.02);
    cw_m_matte(cw_MARKW()) translate([-L / 2 - 0.13, 0, wh + 0.45]) rotate([90, 0, -90]) linear_extrude(height = 0.01, center = true)
        text(str(1 + cw_rnd(seed + 3, 9)), size = 0.6, halign = "center", valign = "center");
    // 侧墙消防板 + 吸烟点（长椅 + 埋地沙桶）
    translate([-L / 2 - 0.05, -D * 0.15, 0]) rotate([0, 0, 90]) cw_prop_fire_board(seed);
    translate([L / 2 + 1.1, -D * 0.2, 0]) rotate([0, 0, 90]) cw_prop_bench();
    cw_m_steel([0.40, 0.08, 0.05]) translate([L / 2 + 1.1, D * 0.25, 0]) difference() { cylinder(h = 0.55, r = 0.22, $fn = 10); translate([0, 0, 0.45]) cylinder(h = 0.2, r = 0.19, $fn = 10); }
    cw_m_soil([0.40, 0.34, 0.24]) translate([L / 2 + 1.1, D * 0.25, 0]) cylinder(h = 0.48, r = 0.19, $fn = 10);
    // 背面：晾衣绳上没收的军装
    cw_m_timber(cw_WOODD()) for (sx = [-1, 1]) translate([sx * L * 0.3, D / 2 + 1.6, 0.9]) cw_boxc([0.08, 0.08, 1.8]);
    cw_m_iron() translate([0, D / 2 + 1.6, 1.75]) rotate([0, 90, 0]) cylinder(h = L * 0.6, r = 0.008, center = true, $fn = 4);
    for (i = [0 : 3]) if (cw_rnd(seed + i * 5, 3) != 0)
        cw_m_fabric(i % 2 == 0 ? cw_OLIVE() : [0.36, 0.36, 0.33]) translate([-L * 0.22 + i * L * 0.14, D / 2 + 1.6, 1.42]) rotate([0, cw_rndr(seed + i, -6, 6), 0]) cw_boxc([0.55, 0.03, 0.62]);
    cw_part_litter(seed, L - 1, D - 1, 5, false, false);
    cw_part_decay(seed, L, D, 0, 3.0, wh);
}

// 军事指挥部：两层赭石抹灰 + 门廊双柱 + 红星徽 + 旗杆 + 沙袋工事
module cw_bldg_hq(seed = 0, L = 12, D = 8)
{
    wh = 6.2;
    nw = max(3, floor((L - 2.4) / 2.2));
    wx = [for (i = [0 : nw - 1]) -L / 2 + 1.4 + i * (L - 2.8) / (nw - 1)];
    cw_part_shell(L, D, wh, cw_PLASTER(), t = 0.3, dw = 1.6, dh = 2.4, dx = 0, roomH = 2.7,
                  wins = [ for (x = wx) if (abs(x) > 1.2) [0, x, 1.7, 1.0, 1.4],
                           for (x = wx) if (abs(x) > 1.2) [1, x, 1.7, 1.0, 1.4] ],
                  seed = seed, wall = "plaster", floor = "lino", door = "wood", dc = [0.28, 0.16, 0.10]);
    // 层间檐线 + 檐口
    cw_m_plaster(cw_mul(cw_PLASTER(), 0.82)) for (sy = [-1, 1]) translate([0, sy * (D / 2 + 0.04), 3.2]) cw_boxc([L + 0.08, 0.1, 0.3]);
    cw_m_plaster(cw_mul(cw_PLASTER(), 0.82)) translate([0, 0, wh - 0.15]) difference() { cw_slab(L + 0.3, D + 0.3, 0.3); translate([0, 0, -0.1]) cw_slab(L - 0.1, D - 0.1, 0.5); }
    // 二层窗阵（贴墙装饰；底层为洞口）
    for (i = [0 : nw - 1], sy = [-1, 1])
        translate([wx[i], sy * (D / 2 + 0.04), 1.7 + 2.9])
            rotate([0, 0, sy > 0 ? 180 : 0]) cw_part_window(1.0, 1.4, seed * 5 + i * 3 + sy, cw_mul(cw_PLASTER(), 1.05));
    // 中央门廊 + 红星 + 铭牌
    cw_m_plaster(cw_WHITEW()) for (sx = [-1, 1]) translate([sx * 1.1, -D / 2 - 0.9, 0]) { cylinder(h = 3.1, r = 0.17, $fn = 12); cw_slab(0.42, 0.42, 0.25); }
    cw_m_concrete(cw_CONCD()) translate([0, -D / 2 - 0.85, 3.1]) cw_boxc([3.4, 2.0, 0.25]);
    cw_m_concrete(cw_CONCD()) translate([0, -D / 2 - 0.85, 0]) cw_slab(3.2, 1.9, 0.02);
    cw_m_oilpaint(cw_REDC()) translate([0, -D / 2 - 0.08, wh - 1.2]) rotate([90, 0, 0])
        linear_extrude(height = 0.08, center = true) polygon([for (i = [0 : 9]) let(a = 90 + i * 36, r = i % 2 == 0 ? 0.45 : 0.18) [r * cos(a), r * sin(a)]]);
    translate([1.35, -D / 2 - 0.02, 1.7]) cw_plate("COMMAND", 0.7, 0.36, 0.1, [0.40, 0.33, 0.12], [0.08, 0.06, 0.04], 0.02);
    // 门廊两侧沙袋工事（门洞前留出通道）
    for (sx = [-1, 1]) translate([sx * 2.6, -D / 2 - 1.3, 0]) rotate([0, 0, sx * 90]) cw_prop_wall_sandbag(len = 2.4);
    // 平顶 + 旗杆（褪色破旗）+ 鞭状天线
    cw_part_flatroof(L, D, wh, seed, 0.4, cw_CONCD());
    cw_m_galv() translate([-L * 0.3, D * 0.2, wh]) cylinder(h = 3.2, r = 0.04, $fn = 6);
    cw_m_fabric([0.34, 0.08, 0.06]) translate([-L * 0.3 + 0.4, D * 0.2, wh + 2.85]) rotate([0, 0, 8])
        polyhedron(points = [[-0.37, 0, -0.25], [0.45, 0, -0.18], [0.3, 0, 0.0], [0.45, 0, 0.2], [-0.37, 0, 0.25]],
                   faces = [[0, 1, 2, 3, 4], [4, 3, 2, 1, 0]]);
    cw_m_iron() translate([L * 0.3, -D * 0.2, wh]) { cylinder(h = 5.0, r = 0.025, $fn = 5); for (a = [0, 120, 240]) rotate([0, 0, a]) cw_rod([0, 0, 3.0], [1.2, 0, 0.02], 0.006, 3); }
    cw_part_litter(seed, L - 1, D - 1, 12, false, false);
    cw_part_decay(seed, L, D, 0, 4.0, wh);
}

// 拱顶机库：椭拱壳 + 后墙 + 前大门（半开）
module cw_bldg_hangar(seed = 0, L = 12, D = 14)
{
    cw_m_corr([0.20, 0.24, 0.16]) difference()
    {
        scale([1, 1, 0.62]) rotate([90, 0, 0]) cylinder(h = D, r = L / 2, center = true, $fn = 24);
        translate([0, 0, -L / 2]) cw_boxc([L + 2, D + 2, L]);
        scale([1, 1, 0.62]) rotate([90, 0, 0]) cylinder(h = D - 0.5, r = L / 2 - 0.15, center = true, $fn = 24);
    }
    cw_m_steel(cw_OLIVED()) for (y = [-D / 2 + 0.3 : D / 4 : D / 2]) translate([0, y, 0]) difference()
    {
        scale([1, 1, 0.62]) rotate([90, 0, 0]) cylinder(h = 0.2, r = L / 2 + 0.06, center = true, $fn = 24);
        scale([1, 1, 0.62]) rotate([90, 0, 0]) cylinder(h = 0.3, r = L / 2 - 0.02, center = true, $fn = 24);
        translate([0, 0, -L / 2]) cw_boxc([L + 2, 2, L]);
    }
    cw_m_concrete(cw_OLIVE()) difference()
    {
        translate([0, D / 2 - 0.15, 0]) scale([0.96, 1, 0.60]) rotate([90, 0, 0]) cylinder(h = 0.5, r = L / 2, $fn = 24);
        translate([0, D / 2, -L / 2]) cw_boxc([L + 2, 3, L]);
    }
    cw_m_concrete(cw_CONCD()) cw_slab(L - 0.4, D - 0.4, 0.02);
    // 前门框 + 双扇滑门（一扇半开露黑）
    cw_m_steel(cw_METALD()) translate([-L * 0.14, -D / 2 - 0.05, 1.9]) cw_boxc([L * 0.36, 0.16, 3.8]);
    cw_m_steel(cw_OLIVE()) translate([L * 0.22, -D / 2 - 0.10, 1.9]) cw_boxc([L * 0.32, 0.14, 3.8]);
    color(cw_DARKC()) translate([L * 0.05, -D / 2 - 0.02, 1.8]) cw_boxc([L * 0.34, 0.06, 3.6]);
    cw_m_matte(cw_MARKW()) translate([-L * 0.14, -D / 2 - 0.14, 2.4]) rotate([90, 0, 0]) linear_extrude(height = 0.01, center = true)
        text(str("0", 1 + cw_rnd(seed, 8)), size = 0.6, halign = "center", valign = "center");
}

// 混凝土碉堡：八角低墩 + 射击缝 + 顶板 + 后门
module cw_bldg_bunker(seed = 0)
{
    cw_m_concrete(cw_CONCD()) cylinder(h = 1.7, r1 = 2.7, r2 = 2.3, $fn = 8);
    cw_m_concrete(cw_CONC()) translate([0, 0, 1.7]) cylinder(h = 0.35, r = 2.5, $fn = 8);
    color(cw_DARKC()) for (a = [-40, 0, 40])
        rotate([0, 0, a]) translate([0, -2.3, 1.25]) cw_boxc([1.1, 0.5, 0.28]);
    color(cw_DARKC()) translate([0, 2.35, 0.9]) cw_boxc([1.0, 0.5, 1.8]);
    cw_m_turf(cw_GRASSD()) translate([0, 0, 2.05]) scale([1, 1, 0.25]) sphere(r = 2.3, $fn = 10);
    cw_m_soil(cw_SOILC()) translate([0, -1.2, 0]) rotate([0, 0, 30]) scale([2.2, 1.4, 1]) cylinder(h = 0.25, r = 1, $fn = 8);
    cw_m_iron() for (i = [0 : 3]) translate([cw_rndr(seed + i, -1.8, 1.8), -2.55, 0.8]) rotate([0, cw_rndr(seed + i * 3, -30, 30), 0]) cylinder(h = 0.6, r = 0.012, $fn = 4);
}

// 监狱囚室楼：灰墙 + 小铁栏窗阵 + 铁门 + 探照灯
module cw_bldg_prison_block(seed = 0, floors = 2, L = 14, D = 7)
{
    fh = 2.9;
    wh = floors * fh + 0.4;
    nw = max(4, floor((L - 2.0) / 1.7));
    cw_part_shell(L, D, wh, [0.40, 0.40, 0.38], t = 0.3, dw = 1.5, dh = 2.1, dx = -L * 0.2, roomH = fh - 0.3,
                  seed = seed, wall = "concrete", floor = "concrete", door = "steel");
    for (f = [0 : floors - 1], i = [0 : nw - 1], sy = [-1, 1])
        translate([-L / 2 + 1.2 + i * (L - 2.4) / (nw - 1), sy * (D / 2 + 0.04), 0.4 + f * fh + 1.7])
            rotate([0, 0, sy > 0 ? 180 : 0]) cw_part_window_bar(0.7, 0.8);
    cw_part_flatroof(L, D, wh, seed, 0.4, cw_CONCD());
    cw_m_iron() translate([L * 0.35, -D / 2 + 0.4, wh + 0.35]) cw_boxc([0.1, 0.1, 0.7]);
    cw_m_steel(cw_YELLOWC()) translate([L * 0.35, -D / 2 + 0.25, wh + 0.72]) rotate([30, 0, 0]) cw_boxc([0.3, 0.3, 0.24]);
    cw_part_decay(seed, L, D, -L * 0.2, 1.5, wh);
}

// 监狱高墙段（沿 x 通长）：壁柱 + 顶部铁丝网
module cw_bldg_prison_wall(len = 10)
{
    h = 4.0;
    cw_m_concrete([0.41, 0.41, 0.39]) translate([0, 0, h / 2]) cw_boxc([len, 0.5, h]);
    cw_m_concrete(cw_CONCD()) for (i = [0 : floor(len / 5)])
        translate([min(-len / 2 + i * 5, len / 2 - 0.3), 0, h / 2]) cw_boxc([0.7, 0.7, h]);
    cw_m_concrete(cw_CONCD()) translate([0, 0, h]) cw_boxc([len, 0.6, 0.18]);
    cw_m_iron() for (i = [0 : floor(len / 2.5)])
        translate([-len / 2 + 0.5 + i * 2.5, 0, h + 0.35]) rotate([25, 0, 0]) cw_boxc([0.06, 0.06, 0.75]);
    cw_m_galv() for (k = [0 : 2])
        translate([0, -0.14 * k + 0.14, h + 0.25 + k * 0.16]) rotate([0, 90, 0]) cylinder(h = len, r = 0.01, center = true, $fn = 4);
    cw_m_galv() translate([0, -0.1, h + 0.5]) rotate([0, 90, 0]) scale([1, 1, 1]) for (i = [0 : floor(len / 0.5) - 1])
        translate([0, 0, -len / 2 + 0.25 + i * 0.5]) rotate_extrude($fn = 8) translate([0.2, 0]) circle(r = 0.008, $fn = 4);
}

// 监狱大门段：门楼 + 双扇铁门 + 灯
module cw_bldg_prison_gate(seed = 0)
{
    h = 4.6;
    for (sx = [-1, 1]) cw_m_concrete([0.41, 0.41, 0.39]) translate([sx * 2.4, 0, h / 2]) cw_boxc([1.4, 1.0, h]);
    cw_m_concrete(cw_CONCD()) translate([0, 0, h]) cw_boxc([6.6, 1.1, 0.7]);
    cw_m_matte(cw_REDC()) translate([0, -0.56, h + 0.02]) cw_boxc([2.4, 0.04, 0.4]);
    cw_m_steel([0.20, 0.24, 0.20])
    {
        translate([-0.9, -0.2, 1.75]) rotate([0, 0, -14]) cw_boxc([1.9, 0.08, 3.5]);
        translate([1.1, 0.1, 1.75]) rotate([0, 0, 10]) cw_boxc([1.9, 0.08, 3.5]);
    }
    translate([0, -0.5, h - 0.75]) cw_plate("VEZENI", 1.2, 0.3, 0.16);
    cw_m_steel(cw_YELLOWC()) translate([0, -0.62, h + 0.5]) cw_boxc([0.4, 0.3, 0.26]);
}

// 检查站岗亭：白亭红带 + 环窗
module cw_bldg_checkpoint(seed = 0)
{
    cw_m_concrete(cw_CONCD()) cw_slab(2.6, 2.6, 0.25);
    cw_m_plaster(cw_WHITEW()) translate([0, 0, 0.25]) difference() { cw_slab(2.2, 2.2, 2.3); translate([0, 0, 0.05]) cw_slab(2.0, 2.0, 2.4); translate([0, -1.1, 1.05]) cw_boxc([0.8, 0.5, 1.9]); for (a = [0, 90, 180, 270]) rotate([0, 0, a]) translate([0, 0, 1.7]) cw_boxc([1.7, 2.6, 0.75]); }
    cw_m_oilpaint(cw_REDC()) translate([0, 0, 0.7]) difference() { cw_boxc([2.24, 2.24, 0.28]); cw_boxc([2.1, 2.1, 0.4]); translate([0, -1.1, 0]) cw_boxc([0.8, 0.5, 0.5]); }
    cw_m_glass() for (a = [90, 180, 270]) rotate([0, 0, a]) translate([0, -1.05, 1.95]) cw_boxc([1.7, 0.01, 0.75]);
    cw_m_roof("tin", cw_ROOFM()) translate([0, 0, 2.55]) cw_slab(2.7, 2.7, 0.08);
    cw_m_varnish(cw_WOODC()) translate([0, 0.8, 0.25]) cw_slab(1.6, 0.4, 0.8);
}

// 水塔：四内倾腿桁架 + 柱身筒 + 锥顶罐 + 环形走道 + 爬梯
module cw_bldg_water_tower(seed = 0)
{
    lh = 6.5;
    tl = 6;      // 腿倾角：顶端内收
    r0 = 1.2;    // 腿在杆件中点高度的半间距
    tc = cw_rnd(seed, 2) == 0 ? [0.28, 0.32, 0.28] : [0.26, 0.15, 0.09];
    cw_m_concrete(cw_CONCD()) for (sx = [-1, 1], sy = [-1, 1]) translate([sx * (r0 + 0.35), sy * (r0 + 0.35), 0]) cw_slab(0.6, 0.6, 0.3);
    cw_m_iron() for (sx = [-1, 1], sy = [-1, 1])
        translate([sx * r0, sy * r0, lh / 2]) rotate([sy * tl, sx * -tl, 0]) cw_boxc([0.2, 0.2, lh + 0.5]);
    cw_m_iron() for (z = [2.0, 4.2])
    {
        rz = r0 + (lh / 2 - z) * tan(tl) + 0.12;   // 横撑跟随腿的收分
        translate([0, -rz, z]) cw_boxc([rz * 2 + 0.16, 0.08, 0.12]);
        translate([0, rz, z]) cw_boxc([rz * 2 + 0.16, 0.08, 0.12]);
        translate([-rz, 0, z]) cw_boxc([0.08, rz * 2 + 0.16, 0.12]);
        translate([rz, 0, z]) cw_boxc([0.08, rz * 2 + 0.16, 0.12]);
    }
    // 交叉拉杆（每面一组）
    cw_m_iron() for (a = [0, 90, 180, 270]) rotate([0, 0, a]) for (k = [0 : 1]) let(z0 = k == 0 ? 0.3 : 2.0, z1 = k == 0 ? 2.0 : 4.2,
        r0a = r0 + (lh / 2 - z0) * tan(tl), r1a = r0 + (lh / 2 - z1) * tan(tl))
    {
        cw_rod([-r0a, -r0a, z0], [r1a, -r1a, z1], 0.025, 4);
        cw_rod([r0a, -r0a, z0], [-r1a, -r1a, z1], 0.025, 4);
    }
    cw_m_steel(tc) translate([0, 0, lh]) cylinder(h = 2.8, r = 1.9, $fn = 20);
    cw_m_iron(tc) for (z = [0.05, 1.4, 2.75]) translate([0, 0, lh + z]) cylinder(h = 0.06, r = 1.93, $fn = 20);
    cw_m_steel(cw_mul(tc, 0.8)) translate([0, 0, lh + 2.8]) cylinder(h = 1.0, r1 = 2.0, r2 = 0.2, $fn = 20);
    // 环形走道 + 栏杆
    cw_m_iron() translate([0, 0, lh - 0.05]) difference() { cylinder(h = 0.06, r = 2.45, $fn = 20); translate([0, 0, -0.1]) cylinder(h = 0.3, r = 1.85, $fn = 20); }
    cw_m_iron() translate([0, 0, lh + 0.95]) difference() { cylinder(h = 0.04, r = 2.42, $fn = 20); translate([0, 0, -0.1]) cylinder(h = 0.3, r = 2.38, $fn = 20); }
    cw_m_iron() for (a = [0 : 30 : 330]) rotate([0, 0, a]) translate([2.4, 0, lh + 0.47]) cw_boxc([0.03, 0.03, 0.95]);
    // 爬梯 + 溢流管
    cw_m_iron()
    {
        for (sx = [-1, 1]) translate([sx * 0.22, -2.0, lh / 2]) cw_boxc([0.04, 0.04, lh]);
        for (i = [0 : floor(lh / 0.35)]) translate([0, -2.0, 0.3 + i * 0.35]) cw_boxc([0.44, 0.03, 0.03]);
        translate([1.0, 1.0, 0]) cylinder(h = lh, r = 0.09, $fn = 8);
    }
    cw_m_water() translate([1.0, 1.0, 0.01]) cw_decal(seed, 0.9, 0.8, 0.004);
}

// 乡村小教堂：白墙 + 坡顶 + 鼓座洋葱顶 + 十字
module cw_bldg_chapel(seed = 0)
{
    wh = 3.4;
    L = 4.2;
    cw_part_shell(L, L, wh, cw_WHITEW(), t = 0.24, dw = 1.0, dh = 2.0, dx = 0,
                  wins = [[1, 0, 2.1, 0.55, 1.1], [2, 0, 2.1, 0.55, 1.1], [3, 0, 2.1, 0.55, 1.1]],
                  seed = seed, wall = "plaster", floor = "boards", door = "wood", dc = [0.25, 0.16, 0.10]);
    translate([0, 0, wh]) cw_part_roof(L, L, 1.3, 0.4, 0, cw_ROOFM(), "tin");
    cw_m_plaster(cw_WHITEW()) translate([0, 0, wh + 1.2]) cylinder(h = 1.3, r = 0.75, $fn = 12);
    cw_m_zinc([0.40, 0.30, 0.13])
    {
        translate([0, 0, wh + 2.5]) sphere(r = 0.85, $fn = 12);
        translate([0, 0, wh + 3.1]) cylinder(h = 1.0, r1 = 0.6, r2 = 0.04, $fn = 12);
    }
    gk_material([0.55, 0.42, 0.17], roughness = 0.25, metalness = 1)
    {
        translate([0, 0, wh + 4.1]) cw_boxc([0.06, 0.06, 0.55]);
        translate([0, 0, wh + 4.28]) cw_boxc([0.3, 0.06, 0.06]);
    }
    cw_part_decay(seed, L, L, 0, 1.0, wh);
}

// 公交候车亭：混凝土壳 + 马赛克色带 + 内置坐凳 + 褪色时刻表
module cw_bldg_bus_stop(seed = 0)
{
    cw_m_concrete(cw_CONCD()) cw_slab(4.2, 2.0, 0.15);
    cw_m_concrete(cw_CONC())
    {
        translate([0, 0.85, 1.25]) cw_boxc([4.2, 0.18, 2.2]);
        for (sx = [-1, 1]) translate([sx * 2.0, 0.2, 1.25]) cw_boxc([0.18, 1.5, 2.2]);
        translate([0, 0.1, 2.35]) cw_boxc([4.6, 2.1, 0.16]);
    }
    for (i = [0 : 7])
        cw_m_ceramic(cw_goods_c(seed * 3 + i)) translate([-1.7 + i * 0.5, 0.75, 1.5]) cw_boxc([0.42, 0.02, 0.6]);
    cw_m_planks(cw_WOODD()) translate([0, 0.45, 0.55]) cw_boxc([3.4, 0.5, 0.06]);
    cw_m_concrete(cw_CONCD()) for (sx = [-1, 1]) translate([sx * 1.4, 0.45, 0.28]) cw_boxc([0.3, 0.4, 0.45]);
    translate([1.85, 0.6, 1.4]) rotate([0, 0, 90]) cw_plate("ZASTAVKA", 0.6, 0.16, 0.07, cw_MARKW(), cw_SIGNB(), 0.01);
    cw_m_paper([0.46, 0.45, 0.40]) translate([-1.0, 0.75, 1.0]) cw_boxc([0.4, 0.01, 0.3]);
}

// 废墟残楼：断墙圈 + 齿状破口 + 瓦砾堆 + 外露钢筋
module cw_bldg_ruin(seed = 0, L = 8, D = 6)
{
    brick = cw_rnd(seed, 2) == 0;
    bc = brick ? cw_BRICKD() : cw_CONCD();
    hs = [2.6, 1.1, 1.9, 0.7, 2.2, 1.5];
    for (i = [0 : 3], sy = [-1, 1])
    {
        hh = hs[cw_rnd(seed + i * 7 + sy * 3, 6)];
        cw_m_wall(brick ? "brick" : "concrete", bc) translate([-L / 2 + (i + 0.5) * L / 4, sy * (D / 2 - 0.15), 0])
            hull() { translate([0, 0, hh / 4]) cw_boxc([L / 4 + 0.02, 0.3, hh / 2]); translate([cw_rndr(seed + i, -0.3, 0.3), 0, hh - 0.05]) cw_boxc([L / 4 * 0.7, 0.3, 0.1]); }
    }
    for (i = [0 : 2], sx = [-1, 1])
    {
        hh = hs[cw_rnd(seed + i * 13 + sx * 5 + 1, 6)];
        cw_m_wall(brick ? "brick" : "concrete", bc) translate([sx * (L / 2 - 0.15), -D / 2 + (i + 0.5) * D / 3, hh / 2]) cw_boxc([0.3, D / 3 + 0.02, hh]);
    }
    color(cw_DARKC()) translate([-L * 0.1, -(D / 2 - 0.15), 1.0]) cw_boxc([1.2, 0.34, 2.0]);
    cw_m_concrete(cw_CONCD()) translate([L * 0.1, 0, 0]) scale([L * 0.32, D * 0.3, 1]) cylinder(h = 0.55, r = 1, $fn = 10);
    for (i = [0 : 6])
        cw_m_wall(cw_rnd(seed + i, 2) == 0 && brick ? "brick" : "concrete", cw_rnd(seed + i, 2) == 0 ? bc : cw_CONC())
            translate([cw_rndr(seed * 5 + i * 17, -L * 0.3, L * 0.3), cw_rndr(seed * 7 + i * 23 + 2, -D * 0.28, D * 0.28), 0.3 + i * 0.05])
                rotate([cw_rndr(seed + i, -20, 20), cw_rndr(seed + i * 3, -15, 15), cw_rnd(seed + i * 3, 180)]) cw_boxc([0.8, 0.5, 0.2]);
    cw_m_iron() for (i = [0 : 3]) translate([-L * 0.25 + i * 0.3, D * 0.1, 0.4]) rotate([cw_rndr(seed + i, -30, 30), 60 + i * 5, 25]) cylinder(h = 1.8, r = 0.012, $fn = 4);
    cw_m_planks(cw_WOODD()) translate([L * 0.2, -D * 0.1, 0.9]) rotate([0, 28, 30]) cw_boxc([3.0, 0.2, 0.12]);
}

// ================= 植被生态（底面 z=0） =================

// 云杉（东欧针叶林主树）：窄高多层下垂锥 + 树皮干
module cw_nature_pine(s = 1.0, seed = 0)
{
    c1 = cw_rnd(seed, 2) == 0 ? cw_PINED() : cw_PINEC();
    c2 = cw_rnd(seed, 2) == 0 ? cw_PINEC() : cw_PINED();
    tw = cw_rndr(seed + 3, -6, 6);
    scale([s, s, s]) rotate([0, 0, cw_rnd(seed, 360)])
    {
        cw_m_bark(cw_TRUNKC()) cylinder(h = 6.4, r1 = 0.2, r2 = 0.05, $fn = 7);
        for (k = [0 : 5]) let(z = 0.9 + k * 0.95, r = 1.55 - k * 0.24)
            cw_m_leaf(k % 2 == 0 ? c1 : c2) translate([0, 0, z]) rotate([cw_rndr(seed + k, -3, 3), 0, k * 23 + tw])
                cylinder(h = 1.35, r1 = r, r2 = r * 0.25, $fn = 8);
        cw_m_leaf(c2) translate([0, 0, 6.2]) cylinder(h = 0.9, r1 = 0.28, r2 = 0.02, $fn = 6);
    }
}

// 白桦：白干黑斑 + 疏朗浅色团冠
module cw_nature_birch(s = 1.0, seed = 0)
{
    scale([s, s, s])
    {
        cw_m_bark(cw_BIRCHW()) cylinder(h = 3.8, r1 = 0.16, r2 = 0.08, $fn = 7);
        color(cw_mul(cw_DARKC(), 1.4)) for (i = [0 : 6])
            rotate([0, 0, cw_rnd(seed + i, 360)]) translate([0.12 - i * 0.008, 0, 0.4 + i * 0.5]) cw_boxc([0.06, 0.12, 0.06]);
        cw_m_bark(cw_BIRCHW()) for (a = [0, 130, 250]) rotate([0, 0, a + cw_rnd(seed, 60)]) translate([0, 0, 2.6]) rotate([0, 32, 0]) cylinder(h = 1.2, r1 = 0.06, r2 = 0.02, $fn = 5);
        cw_m_leaf(cw_rnd(seed, 2) == 0 ? [0.34, 0.41, 0.20] : cw_LEAFC()) translate([0, 0, 3.7]) scale([1, 1, 1.25]) sphere(r = 1.1, $fn = 8);
        cw_m_leaf(cw_LEAFC()) translate([0.55, 0.3, 4.2]) sphere(r = 0.7, $fn = 7);
        cw_m_leaf([0.37, 0.44, 0.22]) translate([-0.5, -0.25, 4.4]) sphere(r = 0.6, $fn = 7);
    }
}

// 枯树：光秃分叉
module cw_nature_tree_dead(s = 1.0, seed = 0)
{
    scale([s, s, s])
    {
        cw_m_bark([0.25, 0.22, 0.18]) cylinder(h = 3.0, r1 = 0.24, r2 = 0.12, $fn = 7);
        cw_m_bark([0.25, 0.22, 0.18])
        {
            translate([0, 0, 2.6]) rotate([0, 35, cw_rnd(seed, 180)]) cylinder(h = 1.5, r1 = 0.10, r2 = 0.03, $fn = 5);
            translate([0, 0, 2.2]) rotate([0, -42, cw_rnd(seed + 3, 180)]) cylinder(h = 1.2, r1 = 0.09, r2 = 0.03, $fn = 5);
            translate([0, 0, 1.5]) rotate([0, 55, cw_rnd(seed + 7, 180)]) cylinder(h = 0.9, r1 = 0.07, r2 = 0.02, $fn = 5);
            translate([0, 0, 2.95]) rotate([0, 12, cw_rnd(seed + 9, 180)]) cylinder(h = 0.9, r1 = 0.08, r2 = 0.02, $fn = 5);
        }
        color([0.40, 0.36, 0.30]) translate([0, 0, 2.95]) cylinder(h = 0.03, r = 0.1, $fn = 6);   // 折断的白茬
    }
}

// 灌木丛
module cw_nature_bush(s = 1.0, seed = 0)
{
    scale([s, s, s])
    {
        cw_m_leaf(cw_rnd(seed, 2) == 0 ? cw_LEAFD() : cw_PINEC()) translate([0, 0, 0.42]) sphere(r = 0.55, $fn = 7);
        cw_m_leaf(cw_LEAFC()) translate([0.4, 0.15, 0.36]) sphere(r = 0.38, $fn = 7);
        cw_m_leaf(cw_LEAFD()) translate([-0.35, -0.2, 0.34]) sphere(r = 0.34, $fn = 7);
        cw_m_bark(cw_TRUNKC()) for (a = [0, 140, 260]) rotate([0, 0, a]) translate([0.5, 0, 0]) rotate([0, -20, 0]) cylinder(h = 0.4, r = 0.02, $fn = 4);
    }
}

// 荒草簇
module cw_nature_grass_tuft(seed = 0)
{
    for (i = [0 : 5])
        cw_m_leaf(i % 2 == 0 ? cw_GRASSD() : [0.42, 0.42, 0.25])
            rotate([cw_rndr(seed + i, -25, 25), cw_rndr(seed + i * 3, -25, 25), cw_rnd(seed + i, 180)])
                cylinder(h = cw_rndr(seed + i + 7, 0.25, 0.5), r1 = 0.05, r2 = 0.005, $fn = 3);
}

// 芦苇丛（水边/沟渠）
module cw_nature_reeds(seed = 0)
{
    for (i = [0 : 8])
    {
        rx = cw_rndr(seed * 3 + i * 11, -0.35, 0.35);
        ry = cw_rndr(seed * 5 + i * 17 + 2, -0.3, 0.3);
        rh = cw_rndr(seed + i, 0.9, 1.6);
        cw_m_leaf([0.33, 0.38, 0.20]) translate([rx, ry, 0]) rotate([cw_rnd(seed + i, 14) - 7, cw_rnd(seed + i * 3, 10) - 5, 0]) cylinder(h = rh, r1 = 0.025, r2 = 0.008, $fn = 4);
        if (i % 2 == 0)
            cw_m_fabric([0.30, 0.20, 0.11]) translate([rx, ry, rh]) cylinder(h = 0.22, r = 0.04, $fn = 6);
    }
}

// 冰碛岩块
module cw_nature_rock(s = 1.0, seed = 0)
{
    scale([s, s, s])
    {
        cw_m_rock([0.38, 0.38, 0.36]) rotate([0, 0, cw_rnd(seed, 180)]) scale([1, 0.75, 0.55]) sphere(r = 0.9, $fn = 9);
        cw_m_rock([0.32, 0.32, 0.31]) translate([0.5, 0.25, 0]) scale([0.6, 0.5, 0.4]) sphere(r = 0.8, $fn = 8);
        cw_m_rock([0.42, 0.42, 0.39]) translate([-0.4, -0.2, 0.1]) scale([0.5, 0.4, 0.45]) sphere(r = 0.7, $fn = 8);
        cw_m_leaf([0.24, 0.28, 0.14]) translate([0.1, 0.05, 0.42]) scale([0.6, 0.45, 0.1]) sphere(r = 0.7, $fn = 7);   // 苔藓
    }
}

// 树桩
module cw_nature_stump(s = 1.0, seed = 0)
{
    scale([s, s, s])
    {
        cw_m_bark(cw_TRUNKC()) cylinder(h = 0.5, r1 = 0.36, r2 = 0.3, $fn = 9);
        cw_m_timber([0.46, 0.37, 0.24]) translate([0, 0, 0.5]) rotate([cw_rndr(seed, -6, 6), 0, 0]) cylinder(h = 0.03, r = 0.27, $fn = 9);
        cw_m_bark(cw_TRUNKC()) for (a = [0, 120, 240]) rotate([0, 0, cw_rnd(seed, 180) + a]) translate([0.28, 0, 0]) rotate([0, 70, 0]) cylinder(h = 0.35, r1 = 0.1, r2 = 0.03, $fn = 5);
    }
}

// 倒木
module cw_nature_log(seed = 0)
{
    cw_m_bark(cw_TRUNKC()) translate([-1.2, 0, 0.26]) rotate([0, 90, 0]) cylinder(h = 2.4, r = 0.26, $fn = 9);
    cw_m_timber([0.46, 0.37, 0.24]) translate([1.2, 0, 0.26]) rotate([0, 90, 0]) cylinder(h = 0.03, r = 0.22, $fn = 9);
    cw_m_timber([0.46, 0.37, 0.24]) translate([-1.23, 0, 0.26]) rotate([0, 90, 0]) cylinder(h = 0.03, r = 0.22, $fn = 9);
    cw_m_bark(cw_TRUNKC()) translate([0.35, 0.12, 0.38]) rotate([-35, 0, 0]) cylinder(h = 0.5, r = 0.08, $fn = 5);
    cw_m_leaf([0.22, 0.27, 0.13]) translate([-0.3, 0, 0.3]) scale([1.6, 1, 0.6]) sphere(r = 0.28, $fn = 7);   // 苔藓
    if (cw_rnd(seed, 2) == 0)
        cw_m_leaf(cw_LEAFD()) translate([-0.6, -0.25, 0.3]) sphere(r = 0.24, $fn = 6);
}

// 撂荒农田：土垄 + 杂草侵占
module cw_nature_field(L = 12, D = 9, seed = 0)
{
    cw_m_soil(cw_SOILC()) cw_slab(L, D, 0.14);
    nr = max(2, floor(D / 1.4));
    for (r = [0 : nr - 1])
    {
        py = -D / 2 + 0.8 + r * (D - 1.6) / (nr - 1);
        cw_m_soil([0.28, 0.21, 0.14]) translate([0, py, 0.14]) hull() { cw_slab(L - 0.8, 0.6, 0.02); cw_slab(L - 0.8, 0.25, 0.12); }
    }
    for (i = [0 : 7])
        cw_m_leaf(cw_rnd(seed + i, 2) == 0 ? cw_GRASSD() : [0.40, 0.40, 0.22])
            translate([cw_rndr(seed * 7 + i * 19, -(L - 2) / 2, (L - 2) / 2),
                       cw_rndr(seed * 11 + i * 29 + 4, -(D - 2) / 2, (D - 2) / 2), 0.2])
                scale([1, 1, 0.6]) sphere(r = cw_rndr(seed + i + 8, 0.24, 0.5), $fn = 7);
}

// ================= 道具 / 路标（底面 z=0；带朝向者 front=-y） =================

// 预制板围墙段（ПО-2 式菱形纹板，沿 x 通长）
module cw_prop_fence_concrete(len = 5, h = 2.0)
{
    np = max(1, floor(len / 2.5));
    cw_m_concrete(cw_CONCD()) for (i = [0 : np])
        translate([-len / 2 + i * len / np, 0, (h + 0.3) / 2]) cw_boxc([0.3, 0.34, h + 0.3]);
    cw_m_concrete(cw_CONC()) translate([0, 0, h / 2 + 0.1]) cw_boxc([len, 0.14, h - 0.2]);
    // 菱形浮雕纹（双面）
    cw_m_concrete(cw_mul(cw_CONC(), 0.92)) for (i = [0 : np - 1], sy = [-1, 1], r = [0 : 2], c = [0 : 3])
        let(pw = len / np - 0.5, cx = -len / 2 + (i + 0.5) * len / np - pw / 2 + (c + 0.5) * pw / 4, cz = 0.5 + (r + 0.5) * (h - 0.7) / 3)
            translate([cx, sy * 0.075, cz]) rotate([0, 45, 0]) cw_boxc([min(pw / 4, (h - 0.7) / 3) * 0.6, 0.03, min(pw / 4, (h - 0.7) / 3) * 0.6]);
    cw_m_leaf(cw_GRASSD()) for (i = [0 : np - 1]) translate([-len / 2 + (i + 0.5) * len / np, -0.12, 0]) for (a = [-30, 0, 30]) rotate([a, 0, i * 50]) cylinder(h = 0.45, r1 = 0.06, r2 = 0.01, $fn = 4);
}

// 铁丝网围栏段（菱形网 + 顶部刺线，沿 x 通长）
module cw_prop_fence_chain(len = 6, h = 2.2)
{
    np = max(1, floor(len / 3));
    cw_m_galv(cw_METALD()) for (i = [0 : np])
        translate([-len / 2 + i * len / np, 0, h / 2]) cylinder(h = h, r = 0.04, center = true, $fn = 6);
    cw_m_galv(cw_METALD()) for (z = [0.1, h - 0.05]) translate([0, 0, z]) rotate([0, 90, 0]) cylinder(h = len, r = 0.025, center = true, $fn = 5);
    // 菱形网：两组斜线
    cw_m_galv([0.36, 0.37, 0.37]) for (k = [0 : floor(len / 0.18)], d = [-1, 1])
        let(x0 = -len / 2 + k * 0.18) if (x0 + d * (h - 0.3) * 0 >= -len / 2)
            intersection()
            {
                translate([0, 0, h / 2 + 0.03]) cw_boxc([len, 0.04, h - 0.2]);
                translate([x0, 0, 0.1]) rotate([0, d * 35, 0]) translate([0, 0, h]) cw_boxc([0.012, 0.012, 2 * h + 1]);
            }
    // 顶部三道刺线斜出
    cw_m_iron() for (i = [0 : np])
        translate([-len / 2 + i * len / np, -0.18, h + 0.2]) rotate([28, 0, 0]) cw_boxc([0.05, 0.04, 0.55]);
    cw_m_galv() for (k = [0 : 2])
        translate([0, -0.08 - k * 0.1, h + 0.12 + k * 0.1]) rotate([0, 90, 0]) cylinder(h = len, r = 0.008, center = true, $fn = 4);
}

// 木桩铁丝网（战地/农田边界，沿 x 通长）
module cw_prop_fence_barbed(len = 6)
{
    np = max(1, floor(len / 2));
    cw_m_timber(cw_WOODD()) for (i = [0 : np])
        translate([-len / 2 + i * len / np, 0, 0.6]) rotate([cw_rnd(i * 7, 10) - 5, 0, 0]) cylinder(h = 1.2, r = 0.05, center = true, $fn = 6);
    cw_m_iron() for (k = [0 : 2])
        translate([0, 0, 0.35 + k * 0.35 - (k == 1 ? 0.06 : 0)]) rotate([0, 90, 0]) cylinder(h = len, r = 0.008, center = true, $fn = 4);
    cw_m_iron() for (k = [0 : 2], i = [0 : floor(len / 0.25)])
        translate([-len / 2 + i * 0.25, 0, 0.35 + k * 0.35 - (k == 1 ? 0.06 : 0)]) rotate([45, 0, 0]) cw_boxc([0.008, 0.008, 0.07]);
}

// 沙袋胸墙（沿 x 通长，三层错缝；袋口折边朝外）
module cw_prop_wall_sandbag(len = 3)
{
    n = max(1, floor(len / 0.55));
    for (r = [0 : 2], i = [0 : n - 1 - (r % 2 == 1 ? 1 : 0)])
        cw_m_sandbag(cw_mul(r % 2 == 0 ? cw_KHAKI() : [0.36, 0.33, 0.21], 0.92 + 0.12 * cw_rndf(r * 31 + i)))
            translate([-len / 2 + 0.3 + i * 0.55 + (r % 2) * 0.27, cw_rndr(r * 7 + i, -0.03, 0.03), 0.13 + r * 0.23])
                rotate([0, 0, cw_rndr(r * 13 + i, -4, 4)]) scale([1, 0.62, 0.42]) sphere(r = 0.31, $fn = 10);
}

// 反坦克拒马（三向工字钢，交叉点离地）
module cw_prop_hedgehog()
{
    translate([0, 0, 0.68])
    {
        cw_m_iron()
        {
            for (a = [[0, 45, 0], [0, -45, 0], [45, 0, 90]]) rotate(a)
            {
                cw_boxc([0.16, 0.02, 1.9]);
                for (s = [-1, 1]) translate([0, s * 0.07, 0]) cw_boxc([0.16, 0.012, 1.9]);
                cw_boxc([0.012, 0.15, 1.9]);
            }
        }
        cw_m_iron() cw_boxc([0.22, 0.22, 0.22]);
    }
}

// 苏式路灯（混凝土杆，灯头悬向 -y）
module cw_prop_lamp()
{
    cw_m_concrete(cw_CONCD())
    {
        cylinder(h = 0.3, r1 = 0.22, r2 = 0.16, $fn = 8);
        cylinder(h = 5.2, r1 = 0.12, r2 = 0.07, $fn = 8);
    }
    cw_m_iron() for (z = [0.9, 2.6]) translate([0, 0, z]) cylinder(h = 0.03, r = 0.125 - z * 0.008, $fn = 8);   // 抱箍
    cw_m_galv(cw_METALD()) translate([0, -0.35, 5.05]) rotate([18, 0, 0]) cw_boxc([0.07, 0.85, 0.06]);
    cw_m_steel([0.26, 0.28, 0.27]) translate([0, -0.72, 5.12]) hull() { cw_boxc([0.24, 0.52, 0.06]); translate([0, 0, 0.08]) cw_boxc([0.16, 0.4, 0.04]); }
    cw_m_lamp() translate([0, -0.72, 5.08]) cw_boxc([0.18, 0.42, 0.02]);
    cw_m_iron() cw_rod([0, 0, 5.0], [0.9, 0, 4.7], 0.006, 3);   // 垂下的电线
}

// 混凝土电力杆：横担 + 绝缘子
module cw_prop_pole_concrete(seed = 0)
{
    cw_m_concrete(cw_CONCD()) cylinder(h = 7.2, r1 = 0.16, r2 = 0.10, $fn = 8);
    cw_m_iron() translate([0, 0, 6.5]) cw_boxc([0.1, 1.8, 0.1]);
    cw_m_ceramic([0.36, 0.30, 0.20]) for (i = [-1, 0, 1])
        translate([0, i * 0.7, 6.55]) { cylinder(h = 0.16, r = 0.05, $fn = 8); translate([0, 0, 0.06]) cylinder(h = 0.03, r = 0.08, $fn = 8); }
    if (cw_rnd(seed, 3) == 0)
        cw_m_concrete(cw_CONCD()) translate([0, 0.4, 5.6]) rotate([35, 0, 0]) cw_boxc([0.1, 0.1, 1.6]);
    cw_m_paper([0.46, 0.44, 0.38]) translate([0, -0.16, 1.7]) cw_boxc([0.2, 0.01, 0.28]);   // 寻人启事
}

// 城镇名牌（白板黑带，出城版红斜杠；front=-y）
module cw_prop_sign_town(seed = 0)
{
    cw_m_galv(cw_METALC()) for (sx = [-1, 1]) translate([sx * 0.8, 0, 1.1]) cylinder(h = 2.2, r = 0.04, center = true, $fn = 6);
    cw_m_matte(cw_MARKW()) translate([0, -0.06, 1.8]) cw_boxc([2.2, 0.04, 0.75]);
    cw_m_matte(cw_DARKC()) translate([0, -0.083, 1.8]) rotate([90, 0, 0]) linear_extrude(height = 0.006, center = true)
        text(["LESNA", "HORKA", "DUBOVA", "KAMENEC"][cw_rnd(seed + 3, 4)], size = 0.26, halign = "center", valign = "center");
    if (cw_rnd(seed, 2) == 0)
        cw_m_matte(cw_REDC()) translate([0, -0.088, 1.8]) rotate([0, 18, 0]) cw_boxc([2.3, 0.006, 0.1]);
    cw_m_iron() for (i = [0 : 2]) translate([cw_rndr(seed + i, -0.9, 0.9), -0.085, cw_rndr(seed + i * 3, 1.5, 2.1)]) rotate([90, 0, 0]) cylinder(h = 0.01, r = 0.02, $fn = 6);   // 弹孔
}

// 蓝底方向路牌（单柱白箭头；front=-y）
module cw_prop_sign_road(seed = 0)
{
    cw_m_galv(cw_METALC()) translate([0, 0, 1.35]) cylinder(h = 2.7, r = 0.04, center = true, $fn = 6);
    cw_m_matte(cw_SIGNB()) translate([0, -0.07, 2.35]) cw_boxc([2.0, 0.04, 0.9]);
    cw_m_matte(cw_MARKW())
    {
        translate([-0.3, -0.093, 2.5]) rotate([90, 0, 0]) linear_extrude(height = 0.006, center = true)
            text(["PLZEN 43", "CHEB 28", "SOKOLOV 12"][cw_rnd(seed, 3)], size = 0.14, halign = "center", valign = "center");
        translate([-0.3, -0.093, 2.2]) cw_boxc([0.8, 0.006, 0.08]);
        translate([0.62, -0.093, 2.35]) rotate([0, 45, 0]) cw_boxc([0.3, 0.006, 0.3]);
    }
}

// 宣传牌坊（红底白字 + 五角星；front=-y）
module cw_prop_billboard(seed = 0)
{
    cw_m_steel(cw_METALD()) for (sx = [-1, 1]) translate([sx * 2.2, 0, 1.5]) cw_boxc([0.18, 0.18, 3.0]);
    cw_m_matte(cw_REDC()) translate([0, -0.1, 3.7]) cw_boxc([5.6, 0.1, 2.2]);
    cw_m_matte(cw_MARKW())
    {
        translate([-0.7, -0.157, 4.1]) rotate([90, 0, 0]) linear_extrude(height = 0.01, center = true) text("PEACE", size = 0.45, halign = "center", valign = "center");
        translate([-0.7, -0.157, 3.45]) rotate([90, 0, 0]) linear_extrude(height = 0.01, center = true) text("AND LABOUR", size = 0.3, halign = "center", valign = "center");
    }
    cw_m_matte(cw_YELLOWC()) translate([1.9, -0.157, 3.8]) rotate([90, 0, 0])
        linear_extrude(height = 0.01, center = true) polygon([for (i = [0 : 9]) let(a = 90 + i * 36, r = i % 2 == 0 ? 0.5 : 0.2) [r * cos(a), r * sin(a)]]);
    cw_m_paper([0.42, 0.40, 0.34]) translate([1.2, -0.157, 3.0]) rotate([0, 8, 0]) cw_boxc([1.1, 0.008, 0.6]);   // 一张盖在上面的撤离通告
    cw_m_steel(cw_METALD()) for (sx = [-1, 1]) translate([sx * 1.6, 0.5, 1.7]) rotate([28, 0, 0]) cw_boxc([0.12, 0.12, 3.4]);
}

// 纪念碑：台阶基座 + 方尖碑 + 红星 + 花圈残骸
module cw_prop_monument(seed = 0)
{
    cw_m_concrete(cw_CONC()) cw_slab(3.2, 3.2, 0.3);
    cw_m_concrete(cw_CONC()) translate([0, 0, 0.3]) cw_slab(2.4, 2.4, 0.3);
    cw_m_stone(cw_CONCD()) translate([0, 0, 0.6]) cw_slab(1.3, 1.3, 0.8);
    cw_m_stone([0.44, 0.44, 0.42]) translate([0, 0, 1.4]) rotate([0, 0, 45]) cylinder(h = 4.2, r1 = 0.55, r2 = 0.32, $fn = 4);
    cw_m_oilpaint(cw_REDC()) translate([0, 0, 6.05]) rotate([90, 0, 0])
        linear_extrude(height = 0.1, center = true) polygon([for (i = [0 : 9]) let(a = 90 + i * 36, r = i % 2 == 0 ? 0.4 : 0.16) [r * cos(a), r * sin(a)]]);
    gk_material([0.40, 0.30, 0.12], roughness = 0.3, metalness = 1) translate([0, -0.66, 1.0]) cw_boxc([0.9, 0.02, 0.3]);
    cw_m_leaf([0.17, 0.22, 0.12]) translate([0.5, -1.4, 0.6]) rotate([70, 0, 10]) rotate_extrude($fn = 12) translate([0.3, 0]) circle(r = 0.07, $fn = 5);
}

// 铜像：基座 + 抬臂人像（front=-y）
module cw_prop_statue(seed = 0)
{
    cw_m_concrete(cw_CONCD()) cw_slab(1.8, 1.8, 0.25);
    cw_m_stone(cw_CONC()) translate([0, 0, 0.25]) cw_slab(1.2, 1.2, 1.6);
    gk_material([0.14, 0.20, 0.16], roughness = 0.5, metalness = 0.7)
    {
        translate([0, 0, 2.35]) hull() { translate([0, 0, -0.5]) cw_boxc([0.6, 0.45, 0.1]); translate([0, 0, 0.5]) cw_boxc([0.5, 0.3, 0.1]); }
        translate([0, 0, 3.1]) sphere(r = 0.19, $fn = 10);
        translate([-0.38, -0.18, 2.85]) rotate([0, 55, 15]) cylinder(h = 0.8, r = 0.08, center = true, $fn = 8);
        translate([0.3, 0, 2.5]) rotate([0, -12, 0]) cylinder(h = 0.7, r = 0.08, center = true, $fn = 8);
        translate([0, 0, 1.95]) cw_boxc([0.5, 0.36, 0.35]);
    }
    gk_material([0.40, 0.30, 0.12], roughness = 0.3, metalness = 1) translate([0, -0.62, 1.0]) cw_boxc([0.7, 0.03, 0.24]);
    cw_m_matte([0.42, 0.40, 0.36]) translate([0.1, -0.24, 3.0]) cw_boxc([0.1, 0.02, 0.12]);   // 鸟粪
}

// 油桶（seed 选色；带滚箍、桶盖、凹痕和漏油）
module cw_prop_barrel(seed = 0)
{
    c = cw_drum_c(seed);
    cw_m_steel(c) cylinder(h = 0.86, r = 0.29, $fn = 14);
    cw_m_steel(cw_mul(c, 0.85)) for (z = [0.28, 0.58]) translate([0, 0, z]) cylinder(h = 0.03, r = 0.302, $fn = 14);
    cw_m_steel(cw_mul(c, 0.85)) for (z = [0, 0.84]) translate([0, 0, z]) cylinder(h = 0.025, r = 0.296, $fn = 14);
    cw_m_iron() for (p = [[0.16, 0.05], [-0.12, -0.1]]) translate([p[0], p[1], 0.86]) cylinder(h = 0.02, r = 0.035, $fn = 8);
    if (cw_rnd(seed + 5, 3) == 0) cw_m_oil() translate([0.4, 0.15, 0]) cw_decal(seed, 0.35, 0.7, 0.003);
}

// 军用弹药箱堆（大件；seed 决定 1-3 只错叠）
module cw_prop_crate_ammo(seed = 0)
{
    n = 1 + cw_rnd(seed, 3);
    for (i = [0 : n - 1])
        translate([cw_rndr(seed + i * 7, -0.2, 0.2), cw_rndr(seed + i * 13 + 2, -0.15, 0.15), i * 0.42])
            rotate([0, 0, cw_rnd(seed + i * 3, 30) - 15])
            {
                cw_m_planks(cw_OLIVE()) translate([0, 0, 0.19]) cw_boxc([1.0, 0.5, 0.38]);
                cw_m_planks(cw_OLIVED()) translate([0, 0, 0.40]) cw_boxc([1.04, 0.54, 0.04]);
                cw_m_planks(cw_OLIVED()) for (sx = [-1, 1]) translate([sx * 0.42, 0, 0.19]) cw_boxc([0.06, 0.52, 0.39]);
                cw_m_iron() for (sx = [-1, 1]) translate([sx * 0.25, -0.27, 0.32]) cw_boxc([0.05, 0.02, 0.08]);
                cw_m_matte([0.46, 0.45, 0.40]) translate([0, -0.257, 0.19]) rotate([90, 0, 0]) linear_extrude(height = 0.004, center = true)
                    text(cw_rnd(seed + i, 2) == 0 ? "7.62x39" : "5.45x39", size = 0.07, halign = "center", valign = "center");
                cw_m_timber(cw_WOODD()) for (sx = [-1, 1]) translate([sx * 0.52, 0, 0.18]) cw_boxc([0.04, 0.3, 0.1]);
            }
}

// 木托盘（板条 + 垫块）
module cw_prop_pallet(seed = 0)
{
    rotate([0, 0, cw_rnd(seed, 40) - 20])
    {
        cw_m_planks(cw_mul(cw_WOODC(), 0.95 + 0.1 * cw_rndf(seed))) for (i = [0 : 6]) if (!(i == 3 && cw_rnd(seed + 1, 3) == 0))
            translate([-0.54 + i * 0.18, 0, 0.13]) cw_boxc([0.12, 1.2, 0.022]);
        cw_m_planks(cw_WOODD()) for (y = [-0.52, 0, 0.52]) translate([0, y, 0.105]) cw_boxc([1.18, 0.1, 0.022]);
        cw_m_timber(cw_WOODD()) for (x = [-0.52, 0, 0.52], y = [-0.52, 0, 0.52]) translate([x, y, 0.047]) cw_boxc([0.14, 0.1, 0.094]);
        cw_m_planks(cw_WOODD()) for (i = [0 : 2]) translate([-0.52 + i * 0.52, 0, 0.0]) cw_slab(0.12, 1.2, 0.022);
    }
}

// 轮胎堆（胎面 + 轮辋孔）
module cw_prop_tire(r = 0.36, w = 0.22)
{
    cw_m_rubber([0.05, 0.05, 0.05]) rotate_extrude($fn = 16) translate([r - w * 0.42, 0]) scale([w * 0.45, w * 0.5]) circle(r = 1, $fn = 10);
}
module cw_prop_tires(seed = 0)
{
    for (i = [0 : 2])
        translate([cw_rndr(seed + i, -0.1, 0.1), cw_rndr(seed + i + 4, -0.1, 0.1), 0.11 + i * 0.22])
            rotate([0, 0, cw_rnd(seed + i, 60)]) cw_prop_tire();
    translate([0.62, 0.3, 0.36]) rotate([80, 0, 20]) cw_prop_tire();
    cw_m_water() translate([0, 0, 0.6]) cylinder(h = 0.01, r = 0.18, $fn = 12);   // 顶胎里的积水
}

// 铁皮垃圾箱（front=-y）
module cw_prop_dumpster()
{
    cw_m_iron() for (sx = [-1, 1]) translate([sx * 0.9, 0, 0.09]) cw_boxc([0.3, 1.1, 0.18]);
    cw_m_rubber() for (sx = [-1, 1], sy = [-1, 1]) translate([sx * 0.9, sy * 0.5, 0.09]) rotate([0, 90, 0]) cylinder(h = 0.06, r = 0.09, center = true, $fn = 8);
    cw_m_steel([0.24, 0.30, 0.24]) translate([0, 0, 0.72]) hull() { translate([0, 0, -0.5]) cw_boxc([2.0, 1.0, 0.1]); translate([0, 0, 0.5]) cw_boxc([2.2, 1.2, 0.1]); }
    cw_m_matte(cw_MARKW()) translate([0.4, -0.6, 0.85]) rotate([90, 0, 0]) linear_extrude(height = 0.006, center = true)
        text("TKO", size = 0.18, halign = "center", valign = "center");
    cw_m_steel([0.20, 0.25, 0.20])
    {
        translate([-0.56, -0.03, 1.29]) cw_boxc([1.06, 1.26, 0.05]);
        translate([0.56, 0.32, 1.62]) rotate([62, 0, 0]) cw_boxc([1.06, 1.26, 0.05]);
    }
    for (i = [0 : 2]) cw_m_plastic([0.06, 0.06, 0.07]) translate([cw_rndr(i * 7, -0.7, 0.7), cw_rndr(i * 9, -0.3, 0.3), 1.3]) scale([1, 0.85, 0.7]) sphere(r = 0.26, $fn = 9);
}

// 加油机（front=-y）：机柜 + 字轮表 + 油枪胶管
module cw_prop_pump_gas(seed = 0)
{
    c = cw_rnd(seed, 2) == 0 ? cw_REDC() : [0.15, 0.26, 0.34];
    cw_m_concrete(cw_CONCD()) cw_slab(1.2, 0.8, 0.14);
    cw_m_steel(c) translate([0, 0, 0.14]) cw_slab(0.8, 0.45, 1.5);
    cw_m_steel(cw_MARKW()) translate([0, 0, 1.64]) cw_slab(0.84, 0.5, 0.12);
    cw_m_matte(cw_MARKW()) translate([0, -0.24, 1.25]) cw_boxc([0.6, 0.02, 0.5]);
    cw_m_glass() translate([0, -0.255, 1.3]) cw_boxc([0.44, 0.01, 0.24]);
    cw_m_matte(cw_DARKC()) for (i = [0 : 3]) translate([-0.15 + i * 0.1, -0.262, 1.3]) cw_boxc([0.07, 0.004, 0.1]);
    cw_m_steel(cw_METALD()) translate([0.42, 0, 1.2]) cw_boxc([0.06, 0.12, 0.3]);
    cw_m_rubber() for (i = [0 : 5]) let(t0 = i / 6, t1 = (i + 1) / 6)
        cw_rod([0.45, -0.05 - t0 * 0.3, 1.1 - sin(t0 * 180) * 0.8], [0.45, -0.05 - t1 * 0.3, 1.1 - sin(t1 * 180) * 0.8], 0.02, 5);
    cw_m_steel(cw_METALD()) translate([0.45, -0.35, 1.1]) rotate([0, 20, 0]) cw_boxc([0.06, 0.06, 0.24]);
}

// 卧式储油罐：鞍座 + 顶部人孔 + 爬梯（长轴沿 x）
module cw_prop_fueltank(seed = 0)
{
    c = cw_rnd(seed, 2) == 0 ? [0.28, 0.32, 0.28] : [0.30, 0.14, 0.08];
    cw_m_concrete(cw_CONCD()) for (sx = [-1, 1]) translate([sx * 1.4, 0, 0.35]) cw_boxc([0.5, 1.6, 0.7]);
    cw_m_steel(c) translate([-2.4, 0, 1.5]) rotate([0, 90, 0]) cylinder(h = 4.8, r = 1.1, $fn = 18);
    cw_m_iron()
    {
        translate([0.6, 0, 2.55]) cylinder(h = 0.3, r = 0.28, $fn = 10);
        for (sx = [-1, 1]) translate([sx * 2.42, 0, 1.5]) rotate([0, 90, 0]) scale([1, 1, 0.25]) sphere(r = 1.1, $fn = 18);
        for (sx = [-1, 1]) translate([sx * 1.4, 0, 1.5]) rotate([0, 90, 0]) cylinder(h = 0.06, r = 1.13, center = true, $fn = 18);
    }
    cw_m_matte([0.46, 0.45, 0.40]) translate([-0.6, -1.105, 1.5]) rotate([90, 0, 0]) linear_extrude(height = 0.006, center = true)
        text("NAFTA", size = 0.3, halign = "center", valign = "center");
    cw_m_iron()
    {
        for (sy = [-1, 1]) translate([2.85, sy * 0.2, 1.3]) cw_boxc([0.05, 0.05, 2.6]);
        for (i = [0 : 4]) translate([2.85, 0, 0.4 + i * 0.5]) cw_boxc([0.05, 0.45, 0.05]);
    }
    cw_m_oil() translate([-1.2, -0.3, 0.005]) cw_decal(seed, 1.0, 0.6, 0.004);
}

// 加油站价目立牌
module cw_prop_sign_pylon(seed = 0)
{
    cw_m_steel(cw_METALD()) translate([0, 0, 2.2]) cw_boxc([0.45, 0.3, 4.4]);
    cw_m_matte(cw_REDC()) translate([0, 0, 4.9]) cw_boxc([1.7, 0.4, 1.0]);
    cw_m_matte(cw_MARKW()) translate([0, -0.205, 5.05]) rotate([90, 0, 0]) linear_extrude(height = 0.006, center = true) text("BENZINA", size = 0.22, halign = "center", valign = "center");
    cw_m_matte(cw_MARKW()) translate([0, 0, 3.6]) cw_boxc([1.0, 0.34, 0.55]);
    cw_m_matte(cw_DARKC()) translate([0, -0.173, 3.6]) rotate([90, 0, 0]) linear_extrude(height = 0.006, center = true) text("--.--", size = 0.18, halign = "center", valign = "center");
}

// 道闸（杆指向 +x；seed 决定开/合）
module cw_prop_barrier_gate(seed = 0)
{
    up = cw_rnd(seed, 2);
    cw_m_concrete(cw_CONCD()) cw_slab(0.7, 0.7, 0.2);
    cw_m_steel([0.38, 0.30, 0.08]) translate([0, 0, 0.2]) cw_slab(0.4, 0.4, 0.9);
    rotate([0, up == 1 ? -62 : 0, 0]) translate([1.9, 0, 1.0])
    {
        cw_m_steel(cw_MARKW()) rotate([0, 90, 0]) cylinder(h = 3.8, r = 0.06, center = true, $fn = 8);
        cw_m_steel(cw_REDC()) for (i = [0 : 2]) translate([-1.2 + i * 1.2, 0, 0]) rotate([0, 90, 0]) cylinder(h = 0.55, r = 0.065, center = true, $fn = 8);
    }
    cw_m_iron() translate([-0.5, 0, 1.0]) cw_boxc([0.6, 0.2, 0.3]);
    if (up == 0) cw_m_steel([0.38, 0.30, 0.08]) translate([3.7, 0, 0.45]) cw_boxc([0.1, 0.3, 0.9]);
}

// 桁架通信塔：三段收分 + 横撑 + 斜拉 + 鞭状天线
module cw_prop_antenna(seed = 0)
{
    hs = [4.0, 3.2, 2.6];
    ws = [1.4, 1.0, 0.65];
    cw_m_concrete(cw_CONCD()) cw_slab(2.0, 2.0, 0.3);
    for (k = [0 : 2])
    {
        z0 = k == 0 ? 0 : (k == 1 ? 4.0 : 7.2);
        w0 = ws[k];
        w1 = k < 2 ? ws[k + 1] : ws[k] * 0.6;
        // 顶端内收的四腿
        cw_m_steel([0.36, 0.36, 0.34]) for (sx = [-1, 1], sy = [-1, 1])
            cw_beam([sx * w0 / 2, sy * w0 / 2, z0], [sx * w1 / 2, sy * w1 / 2, z0 + hs[k]], 0.07);
        // 横撑 + 斜撑
        cw_m_steel([0.36, 0.36, 0.34]) for (a = [0, 90, 180, 270]) rotate([0, 0, a])
        {
            wm = (w0 + w1) / 2;
            translate([0, -wm / 2, z0 + hs[k] / 2]) cw_boxc([wm, 0.04, 0.05]);
            cw_rod([-w0 / 2, -w0 / 2, z0 + 0.1], [w1 / 2, -w1 / 2, z0 + hs[k] - 0.1], 0.015, 4);
        }
    }
    cw_m_galv(cw_METALC()) translate([0, 0, 9.8]) cylinder(h = 2.4, r = 0.035, $fn = 6);
    cw_m_matte(cw_REDC()) translate([0, 0, 12.2]) sphere(r = 0.12, $fn = 8);
    cw_m_matte([0.36, 0.36, 0.34]) translate([0.35, 0, 8.2]) rotate([0, 0, 0]) cylinder(h = 0.12, r = 0.3, $fn = 12);   // 微波盘
    cw_m_steel(cw_OLIVED()) translate([0.9, 0.9, 0]) cw_slab(0.6, 0.4, 1.1);   // 设备箱
}

// 探照灯（front=-y 照向）
module cw_prop_searchlight(seed = 0)
{
    cw_m_iron() for (a = [0, 120, 240])
        rotate([0, 0, a]) translate([0, 0.35, 0.55]) rotate([16, 0, 0]) cylinder(h = 1.2, r = 0.035, center = true, $fn = 6);
    cw_m_steel(cw_OLIVED()) translate([0, 0, 1.15]) sphere(r = 0.14, $fn = 8);
    cw_m_steel(cw_OLIVED()) for (sx = [-1, 1]) translate([sx * 0.36, -0.1, 1.35]) cw_boxc([0.04, 0.08, 0.45]);
    cw_m_steel(cw_OLIVED()) translate([0, -0.15, 1.35]) rotate([55, 0, 0]) cylinder(h = 0.5, r1 = 0.3, r2 = 0.33, $fn = 14);
    cw_m_lamp() translate([0, -0.31, 1.55]) rotate([55, 0, 0]) cylinder(h = 0.02, r = 0.31, $fn = 14);
    cw_m_rubber() cw_rod([0, 0, 1.1], [0.6, 0.4, 0.02], 0.015, 4);   // 电缆
}

// 军用帐篷（脊沿 x，front=-y 开口；帆布 + 绷绳 + 地钉）
module cw_prop_tent_military(seed = 0, L = 4.5, D = 3.2)
{
    c = cw_rnd(seed, 2) == 0 ? cw_OLIVE() : cw_KHAKI();
    cw_m_canvas(c) for (sy = [-1, 1]) translate([0, sy * D * 0.28, 0]) rotate([sy * 62, 0, 0]) cw_boxc([L, 0.04, D * 0.72]);
    cw_m_canvas(cw_mul(c, 0.85)) translate([L / 2 - 0.04, 0, 0]) rotate([90, 0, 90]) linear_extrude(height = 0.04, center = true) polygon([[-D * 0.48, 0], [D * 0.48, 0], [0, 1.4]]);
    cw_m_canvas(cw_mul(c, 0.85)) translate([-L / 2 + 0.04, 0, 0]) rotate([90, 0, 90]) linear_extrude(height = 0.04, center = true) polygon([[-D * 0.48, 0], [-0.4, 0], [0, 0.9], [0, 1.4]]);
    color(cw_DARKC()) translate([-L / 2 + 0.02, 0.2, 0]) rotate([90, 0, 90]) linear_extrude(height = 0.02, center = true) polygon([[-0.6, 0], [0.3, 0], [-0.2, 1.0]]);
    cw_m_timber(cw_WOODD()) for (sx = [-1, 1]) translate([sx * (L / 2 - 0.1), 0, 0.7]) cw_boxc([0.05, 0.05, 1.4]);
    cw_m_timber(cw_WOODD()) translate([0, 0, 1.38]) cw_boxc([L + 0.1, 0.05, 0.05]);
    cw_m_paper([0.40, 0.38, 0.30]) for (sy = [-1, 1], sx = [-1, 1])
        cw_rod([sx * (L / 2 - 0.3), sy * D * 0.46, 0.25], [sx * (L / 2 - 0.3), sy * (D / 2 + 0.55), 0.02], 0.006, 3);
    cw_m_iron() for (sy = [-1, 1], sx = [-1, 1]) translate([sx * (L / 2 - 0.3), sy * (D / 2 + 0.55), 0.05]) cw_boxc([0.03, 0.03, 0.12]);
}

// 篝火营地：石圈 + 焦木 + 吊锅架 + 灰烬
module cw_prop_campfire(seed = 0)
{
    for (a = [0 : 40 : 320])
        cw_m_rock([0.36, 0.36, 0.34]) rotate([0, 0, a + cw_rnd(seed + a, 14)]) translate([0.5, 0, 0.08])
            scale([1, 0.7, 0.6]) sphere(r = 0.16, $fn = 7);
    cw_m_burnt()
    {
        translate([0, 0, 0.1]) rotate([0, 80, 20]) cylinder(h = 0.7, r = 0.07, $fn = 6);
        translate([0.1, -0.1, 0.1]) rotate([0, 82, 130]) cylinder(h = 0.6, r = 0.06, $fn = 6);
        translate([-0.1, 0.1, 0.1]) rotate([0, 78, 260]) cylinder(h = 0.65, r = 0.06, $fn = 6);
    }
    cw_m_soil([0.16, 0.15, 0.14]) cylinder(h = 0.04, r = 0.42, $fn = 12);
    cw_m_paper([0.36, 0.35, 0.33]) translate([0, 0, 0.04]) cw_decal(seed, 0.25, 1, 0.006);   // 灰
    cw_m_timber(cw_WOODD()) for (a = [0, 120, 240])
        rotate([0, 0, a]) translate([0, 0.4, 0.55]) rotate([20, 0, 0]) cylinder(h = 1.2, r = 0.025, center = true, $fn = 5);
    cw_m_iron([0.10, 0.10, 0.10]) translate([0, 0, 0.75]) difference() { cylinder(h = 0.22, r1 = 0.12, r2 = 0.16, $fn = 12); translate([0, 0, 0.04]) cylinder(h = 0.3, r = 0.14, $fn = 12); }
    cw_m_iron() cw_rod([0, 0, 1.08], [0, 0, 0.97], 0.006, 3);
}

// 购物车（front=-y 推行向，铁丝筐）
module cw_prop_cart_shop(seed = 0)
{
    rotate([cw_rnd(seed, 3) == 0 ? 78 : 0, 0, cw_rnd(seed, 50)])   // 部分翻倒
    {
        cw_m_galv(cw_METALC())
        {
            for (sx = [-1, 1], sy = [-1, 1]) translate([sx * 0.3, sy * 0.4, 0.42]) cylinder(h = 0.66, r = 0.015, center = true, $fn = 5);
            translate([0, 0.5, 1.02]) rotate([0, 90, 0]) cylinder(h = 0.75, r = 0.02, center = true, $fn = 6);
            for (sx = [-1, 1]) translate([sx * 0.3, 0, 0.1]) cw_boxc([0.03, 0.85, 0.03]);
        }
        // 铁丝筐：框 + 竖丝
        cw_m_galv(cw_METALC())
        {
            for (z = [0.72, 1.06]) translate([0, 0, z]) difference() { cw_boxc([0.7, 0.92, 0.02]); cw_boxc([0.67, 0.89, 0.05]); }
            for (x = [-0.33 : 0.066 : 0.34], sy = [-1, 1]) translate([x, sy * 0.45, 0.89]) cw_boxc([0.008, 0.008, 0.34]);
            for (y = [-0.44 : 0.066 : 0.45], sx = [-1, 1]) translate([sx * 0.34, y, 0.89]) cw_boxc([0.008, 0.008, 0.34]);
            translate([0, 0, 0.72]) cw_boxc([0.68, 0.9, 0.006]);
        }
        cw_m_rubber() for (sx = [-1, 1], sy = [-1, 1])
            translate([sx * 0.28, sy * 0.38, 0.07]) rotate([0, 90, 0]) cylinder(h = 0.04, r = 0.07, center = true, $fn = 10);
        cw_m_plastic(cw_REDC()) translate([0, 0.52, 1.02]) rotate([0, 90, 0]) cylinder(h = 0.5, r = 0.025, center = true, $fn = 8);
        if (cw_rnd(seed + 3, 2) == 0) cw_m_paper(cw_goods_c(seed)) translate([0.1, 0.1, 0.8]) rotate([0, 0, 20]) cw_boxc([0.25, 0.2, 0.15]);
    }
}

// 超市货架（front=-y 取货面；seed 决定余货/倾倒）
module cw_prop_shelf_market(seed = 0, L = 2.4)
{
    cw_m_steel(cw_METALC()) translate([0, 0.12, 0]) cw_slab(L, 0.5, 0.1);
    cw_m_steel([0.42, 0.44, 0.42]) translate([0, 0.3, 1.0]) cw_boxc([L, 0.04, 2.0]);
    cw_m_steel([0.42, 0.44, 0.42]) for (sx = [-1, 1]) translate([sx * (L / 2 - 0.02), 0.12, 1.0]) cw_boxc([0.04, 0.4, 2.0]);
    cw_m_steel(cw_METALC()) for (z = [0.55, 1.1, 1.65])
        translate([0, 0.1, z]) cw_boxc([L, 0.44, 0.05]);
    cw_m_matte([0.46, 0.45, 0.42]) for (z = [0.55, 1.1, 1.65]) translate([0, -0.125, z - 0.02]) cw_boxc([L, 0.01, 0.05]);   // 价签条
    for (z = [0.55, 1.1, 1.65], i = [0 : floor(L / 0.25) - 1])
        if (cw_rnd(seed * 5 + z * 41 + i * 7, 4) == 0)
        {
            c = cw_goods_c(seed * 7 + z * 13 + i);
            k = cw_rnd(seed + i * 11 + z * 3, 3);
            translate([-L / 2 + 0.18 + i * 0.25, 0.08, z + 0.025]) rotate([0, 0, cw_rnd(seed + i * 3, 40) - 20])
                if (k == 0) cw_m_paper(c) translate([0, 0, 0.13]) cw_boxc([0.18, 0.12, 0.26]);
                else if (k == 1) cw_m_plastic(c) cylinder(h = 0.24, r = 0.05, $fn = 10);
                else cw_m_galv(cw_METALC()) cylinder(h = 0.11, r = 0.045, $fn = 10);
        }
    // 散落地面的货品
    for (i = [0 : 2])
        cw_m_paper(cw_goods_c(seed * 11 + i * 5))
            translate([cw_rndr(seed + i * 13, -L / 2, L / 2), -0.55 - cw_rndr(seed + i * 7, 0, 0.4), 0.06])
                rotate([cw_rnd(seed + i, 80), 0, cw_rnd(seed + i * 17, 180)]) cw_boxc([0.18, 0.12, 0.26]);
}

// 报刊亭（front=-y；seed 决定卷帘关闭）
module cw_prop_kiosk(seed = 0)
{
    cw_m_concrete(cw_CONCD()) cw_slab(2.4, 2.0, 0.15);
    cw_m_steel(cw_rnd(seed, 2) == 0 ? [0.18, 0.30, 0.25] : [0.36, 0.25, 0.18]) translate([0, 0, 0.15]) cw_slab(2.2, 1.8, 2.3);
    if (cw_rnd(seed + 1, 2) == 0)
    {
        color(cw_DARKC()) translate([0, -0.92, 1.5]) cw_boxc([1.6, 0.04, 0.9]);
        cw_m_glass() translate([0.4, -0.93, 1.5]) cw_boxc([0.78, 0.01, 0.88]);
    }
    else cw_m_galv(cw_METALC()) translate([0, -0.92, 1.5]) cw_boxc([1.6, 0.06, 0.9]);
    cw_m_matte(cw_MARKW()) translate([0, -0.94, 2.15]) cw_boxc([1.9, 0.04, 0.4]);
    cw_m_matte(cw_REDC()) translate([0, -0.965, 2.15]) rotate([90, 0, 0]) linear_extrude(height = 0.006, center = true) text("TABAK", size = 0.22, halign = "center", valign = "center");
    cw_m_tin(cw_ROOFM()) translate([0, 0, 2.45]) cw_slab(2.5, 2.1, 0.06);
}

// 电话亭（front=-y）
module cw_prop_phone_booth(seed = 0)
{
    cw_m_steel([0.40, 0.33, 0.10])
    {
        for (sx = [-1, 1], sy = [-1, 1]) translate([sx * 0.44, sy * 0.44, 1.15]) cw_boxc([0.08, 0.08, 2.3]);
        translate([0, 0, 2.3]) cw_slab(1.1, 1.1, 0.14);
        cw_slab(1.0, 1.0, 0.1);
    }
    cw_m_glass() for (a = [0, 90, 180]) if (!(a == 90 && cw_rnd(seed, 2) == 0))
        rotate([0, 0, a]) translate([0, -0.45, 1.3]) cw_boxc([0.8, 0.01, 1.7]);
    cw_m_steel(cw_METALD()) translate([0, 0.4, 1.5]) cw_boxc([0.3, 0.12, 0.45]);
    cw_m_plastic(cw_DARKC()) translate([0.1, 0.32, 1.55]) rotate([0, 0, 0]) cw_boxc([0.05, 0.05, 0.22]);
    cw_m_rubber() cw_rod([0.1, 0.32, 1.45], [0.0, 0.2, 0.3], 0.008, 3);   // 听筒吊着
    cw_m_shard() for (i = [0 : 3]) translate([cw_rndr(seed + i, -0.6, 0.6), cw_rndr(seed + i * 3, -0.9, -0.6), 0.003]) rotate([0, 0, i * 77]) linear_extrude(height = 0.004) polygon(cw_shard(seed + i, 0.12));
}

// 村井：石圈 + 双柱小顶 + 辘轳
module cw_prop_well(seed = 0)
{
    cw_m_stone([0.40, 0.40, 0.38]) difference()
    {
        cylinder(h = 0.8, r = 0.7, $fn = 14);
        translate([0, 0, 0.15]) cylinder(h = 0.8, r = 0.5, $fn = 14);
    }
    cw_m_water() translate([0, 0, 0.2]) cylinder(h = 0.01, r = 0.5, $fn = 14);
    cw_m_timber(cw_WOODD()) for (sx = [-1, 1]) translate([sx * 0.62, 0, 1.0]) cw_boxc([0.12, 0.12, 1.6]);
    translate([0, 0, 1.8]) cw_part_roof(1.9, 1.5, 0.55, 0.15, 0, cw_WOODD(), "boards", false);
    cw_m_timber(cw_WOODC()) translate([-0.62, 0, 1.35]) rotate([0, 90, 0]) cylinder(h = 1.24, r = 0.08, $fn = 8);
    cw_m_iron() translate([0.72, 0, 1.35]) rotate([0, 0, 30]) cw_boxc([0.3, 0.04, 0.04]);
    cw_m_paper([0.36, 0.30, 0.20]) translate([0, 0, 0.9]) cylinder(h = 0.45, r = 0.008, $fn = 4);
    cw_m_galv(cw_METALC()) translate([0, 0, 0.75]) cylinder(h = 0.2, r1 = 0.1, r2 = 0.12, $fn = 10);
}

// 柴垛（两桩夹劈柴）
module cw_prop_woodpile(seed = 0)
{
    for (r = [0 : 2], i = [0 : 3 - (r % 2 == 1 ? 1 : 0)])
    {
        p = [0, -0.45 + i * 0.3 + (r % 2) * 0.15, 0.15 + r * 0.26];
        cw_m_bark(cw_rnd(seed + r * 7 + i, 2) == 0 ? cw_WOODC() : cw_TRUNKC()) translate(p)
            rotate([0, 90, 0]) cylinder(h = 1.6, r = 0.14, center = true, $fn = 6);
        cw_m_timber([0.46, 0.36, 0.22]) translate(p + [0.801, 0, 0]) rotate([0, 90, 0]) cylinder(h = 0.004, r = 0.13, center = true, $fn = 6);
    }
    cw_m_timber(cw_WOODD()) for (sx = [-1, 1]) translate([sx * 0.85, 0, 0.6]) cw_boxc([0.08, 0.08, 1.2]);
    cw_m_tin(cw_ROOFM()) translate([0, 0, 1.0]) rotate([8, 0, 0]) cw_boxc([1.9, 1.2, 0.03]);
}

// 混凝土腿长椅（front=-y）
module cw_prop_bench()
{
    cw_m_concrete(cw_CONCD()) for (sx = [-1, 1]) translate([sx * 0.7, 0, 0.2]) cw_boxc([0.12, 0.48, 0.4]);
    cw_m_planks(cw_WOODC()) for (y = [-0.16, 0, 0.16]) translate([0, y, 0.44]) cw_boxc([1.8, 0.13, 0.04]);
    cw_m_planks(cw_WOODC()) for (z = [0.64, 0.8]) translate([0, 0.26, z]) cw_boxc([1.8, 0.04, 0.12]);
    cw_m_concrete(cw_CONCD()) for (sx = [-1, 1]) translate([sx * 0.7, 0.24, 0.62]) rotate([-10, 0, 0]) cw_boxc([0.1, 0.06, 0.45]);
}

// 废弃儿童乐园：锈秋千 + 滑梯 + 沙坑
module cw_prop_playground(seed = 0)
{
    translate([-1.6, 0, 0])
    {
        cw_m_steel([0.18, 0.28, 0.38]) for (sx = [-1, 1])
        {
            translate([sx * 1.1, 0.45, 1.1]) rotate([20, 0, 0]) cylinder(h = 2.3, r = 0.045, center = true, $fn = 6);
            translate([sx * 1.1, -0.45, 1.1]) rotate([-20, 0, 0]) cylinder(h = 2.3, r = 0.045, center = true, $fn = 6);
        }
        cw_m_steel([0.18, 0.28, 0.38]) translate([0, 0, 2.15]) rotate([0, 90, 0]) cylinder(h = 2.4, r = 0.05, center = true, $fn = 6);
        cw_m_iron() for (x = [-0.6, -0.25]) translate([x, 0, 1.55]) cylinder(h = 1.2, r = 0.01, center = true, $fn = 4);
        cw_m_planks(cw_WOODD()) translate([-0.42, 0, 0.95]) cw_boxc([0.45, 0.24, 0.04]);
        cw_m_iron() translate([0.5, 0.1, 2.0]) rotate([35, 0, 0]) cylinder(h = 1.0, r = 0.01, center = true, $fn = 4);   // 缠住的断链
        cw_m_soil(cw_SOILC()) translate([-0.42, 0, 0]) scale([1, 0.6, 1]) cylinder(h = 0.01, r = 0.5, $fn = 10);
    }
    translate([1.8, 0.3, 0])
    {
        cw_m_steel([0.15, 0.27, 0.36]) translate([0.8, 0, 0.75]) rotate([0, 32, 0]) cw_boxc([2.0, 0.6, 0.04]);
        cw_m_galv(cw_METALC()) for (sy = [-1, 1]) translate([0.8, sy * 0.32, 0.8]) rotate([0, 32, 0]) cw_boxc([2.0, 0.03, 0.14]);
        cw_m_iron() for (sy = [-1, 1]) translate([-0.25, sy * 0.22, 0.7]) cylinder(h = 1.4, r = 0.03, center = true, $fn = 5);
        cw_m_iron() for (i = [0 : 3]) translate([-0.25, 0, 0.25 + i * 0.35]) cw_boxc([0.04, 0.44, 0.04]);
        cw_m_steel([0.38, 0.30, 0.08]) translate([0.05, 0, 1.35]) cw_boxc([0.5, 0.6, 0.05]);
    }
    cw_m_soil([0.42, 0.37, 0.26]) translate([0, -2.0, 0]) cw_slab(2.6, 1.6, 0.12);
    cw_m_planks(cw_WOODD()) for (sy = [-1, 1]) translate([0, -2.0 + sy * 0.83, 0]) cw_slab(2.7, 0.14, 0.2);
    cw_m_planks(cw_WOODD()) for (sx = [-1, 1]) translate([sx * 1.32, -2.0, 0]) cw_slab(0.14, 1.6, 0.2);
    cw_m_plastic([0.40, 0.30, 0.06]) translate([0.6, -2.1, 0.12]) rotate([20, 0, 30]) difference() { cylinder(h = 0.14, r1 = 0.06, r2 = 0.09, $fn = 10); translate([0, 0, 0.02]) cylinder(h = 0.2, r = 0.075, $fn = 10); }   // 遗落的小桶
}

// 病床（front=-y；seed 决定翻倒）：钢管床架 + 床垫 + 床单 + 输液架
module cw_prop_bed_hospital(seed = 0)
{
    rotate([0, cw_rnd(seed, 4) == 0 ? 85 : 0, cw_rnd(seed, 30) - 15])
    {
        cw_m_galv([0.42, 0.43, 0.42])
        {
            for (sx = [-1, 1], sy = [-1, 1]) translate([sx * 0.88, sy * 0.4, 0.3]) cylinder(h = 0.6, r = 0.025, center = true, $fn = 6);
            for (sy = [-1, 1]) translate([0, sy * 0.4, 0.56]) rotate([0, 90, 0]) cylinder(h = 1.8, r = 0.025, center = true, $fn = 6);
            for (sx = [-1, 1]) translate([sx * 0.98, 0, 0.8]) { for (sy = [-1, 1]) translate([0, sy * 0.4, 0]) cylinder(h = sx > 0 ? 0.6 : 0.4, r = 0.025, center = true, $fn = 6); }
            translate([0.98, 0, 1.1]) rotate([90, 0, 0]) cylinder(h = 0.82, r = 0.025, center = true, $fn = 6);
            translate([-0.98, 0, 1.0]) rotate([90, 0, 0]) cylinder(h = 0.82, r = 0.025, center = true, $fn = 6);
        }
        cw_m_steel([0.40, 0.42, 0.40]) translate([0.98, 0, 0.85]) cw_boxc([0.02, 0.78, 0.4]);
        cw_m_fabric([0.30, 0.36, 0.36]) translate([0, 0, 0.66]) cw_boxc([1.9, 0.86, 0.14]);
        cw_m_fabric([0.46, 0.46, 0.43]) translate([-0.25, 0.02, 0.735]) rotate([0, 0, 4]) cw_boxc([1.3, 0.92, 0.02]);
        cw_m_fabric([0.46, 0.46, 0.43]) translate([0.65, 0, 0.78]) scale([1, 2.2, 0.5]) sphere(r = 0.15, $fn = 10);
        cw_m_rubber() for (sx = [-1, 1], sy = [-1, 1]) translate([sx * 0.88, sy * 0.4, 0.05]) rotate([90, 0, 0]) cylinder(h = 0.04, r = 0.05, center = true, $fn = 10);
        if (cw_rnd(seed + 7, 3) == 0) cw_m_blood() translate([-0.1, 0, 0.746]) cw_decal(seed, 0.25, 0.8, 0.002);
    }
    if (cw_rnd(seed + 2, 2) == 0) translate([0.7, -0.6, 0])
    {
        cw_m_galv() { cylinder(h = 1.9, r = 0.015, $fn = 5); for (a = [0, 120, 240]) rotate([0, 0, a]) cw_rod([0, 0, 0.05], [0.25, 0, 0.0], 0.012, 4); translate([0, 0, 1.88]) rotate([0, 90, 0]) cylinder(h = 0.3, r = 0.01, center = true, $fn = 4); }
        cw_m_plastic([0.42, 0.44, 0.42]) translate([0.12, 0, 1.7]) cw_boxc([0.08, 0.03, 0.15]);
    }
}

// 担架（帆布 + 两根木杆；front=-y）
module cw_prop_stretcher(seed = 0)
{
    rotate([0, 0, cw_rnd(seed, 20) - 10])
    {
        cw_m_timber(cw_WOODC()) for (sy = [-1, 1]) translate([0, sy * 0.28, 0.08]) rotate([0, 90, 0]) cylinder(h = 2.2, r = 0.025, center = true, $fn = 6);
        cw_m_canvas(cw_OLIVE()) translate([0, 0, 0.09]) cw_boxc([1.8, 0.56, 0.01]);
        cw_m_iron() for (sx = [-1, 1]) translate([sx * 0.7, 0, 0.04]) cw_boxc([0.04, 0.6, 0.08]);
        if (cw_rnd(seed + 1, 2) == 0) cw_m_blood() translate([0.2, 0, 0.096]) cw_decal(seed, 0.2, 0.9, 0.002);
        cw_m_fabric([0.36, 0.34, 0.28]) translate([-0.4, 0.05, 0.1]) rotate([0, 0, 12]) cw_boxc([0.7, 0.5, 0.03]);
    }
}

// 杂物堆：碎砖 + 板材 + 管件 + 钢筋
module cw_prop_debris(seed = 0)
{
    cw_m_concrete(cw_CONCD()) scale([1.3, 1.0, 0.5]) cylinder(h = 0.4, r = 1, $fn = 10);
    for (i = [0 : 6])
    {
        k = cw_rnd(seed + i, 3);
        translate([cw_rndr(seed * 3 + i * 13, -0.9, 0.9), cw_rndr(seed * 5 + i * 19 + 1, -0.7, 0.7), 0.22 + i * 0.05])
            rotate([cw_rnd(seed + i, 30), cw_rnd(seed + i + 3, 24), cw_rnd(seed + i * 7, 180)])
                if (k == 0) cw_m_brick(cw_BRICKD()) cw_boxc([0.25, 0.12, 0.07]);
                else if (k == 1) cw_m_planks(cw_WOODD()) cw_boxc([0.9, 0.15, 0.03]);
                else cw_m_concrete(cw_CONC()) cw_boxc([0.5, 0.34, 0.12]);
    }
    cw_m_iron() translate([0.4, -0.5, 0.35]) rotate([0, 65, 30]) cylinder(h = 1.1, r = 0.04, $fn = 8);
    cw_m_iron() for (i = [0 : 2]) translate([-0.3 + i * 0.1, 0.2, 0.3]) rotate([cw_rndr(seed + i, -20, 20), 70, -20 + i * 15]) cylinder(h = 1.3, r = 0.01, $fn = 4);
}

// 混凝土公路桥（沿 x 跨越；桥面顶 z≈0.8，锚点=引桥端路面高度，桥墩向下没入河床）
// 布置契约（见 AGENT_GUIDE/ScadTerrain.md）：桥长 ≥ 2.5x河宽，锚点取河岸下切带外的路面高度。
module cw_prop_bridge(L = 30, W = 9, seed = 0)
{
    cw_m_concrete(cw_CONC()) translate([0, 0, 0.45]) cw_boxc([L, W, 0.5]);
    cw_m_asphalt(cw_ASPH()) translate([0, 0, 0.72]) cw_boxc([L, W - 1.2, 0.14]);
    cw_m_concrete(cw_CONCD()) for (sy = [-1, 1]) translate([0, sy * (W / 2 - 0.3), 0.76]) cw_boxc([L, 0.6, 0.22]);
    for (sx = [-1, 1])
        cw_m_concrete(cw_CONCD()) translate([sx * (L / 2 + 1.6), 0, 0.35]) rotate([0, sx * 12, 0]) cw_boxc([3.8, W + 0.4, 0.14]);
    cw_m_concrete(cw_CONCD()) for (px = [-L / 5, L / 5], sy = [-1, 1])
        translate([px, sy * (W / 2 - 0.9), -2.6]) cylinder(h = 6.4, r = 0.55, center = true, $fn = 12);
    cw_m_concrete(cw_CONCD()) for (px = [-L / 5, L / 5]) translate([px, 0, 0.0]) cw_boxc([1.2, W - 0.6, 0.4]);
    cw_m_steel([0.30, 0.32, 0.30]) for (sy = [-1, 1])
    {
        translate([0, sy * (W / 2 + 0.05), 1.35]) cw_boxc([L, 0.08, 0.1]);
        for (i = [0 : floor(L / 2.4)])
            translate([-L / 2 + 0.6 + i * 2.4, sy * (W / 2 + 0.05), 1.05]) cw_boxc([0.08, 0.08, 0.55]);
    }
    cw_m_matte(cw_MARKW()) translate([0, 0, 0.795]) cw_boxc([L * 0.88, 0.12, 0.006]);
}

// ================= 载具（车头朝 +x，底面 z=0） =================
// 车身材质按状态：0 弃置（掉漆车漆）/ 1 锈蚀残骸 / 2 焚毁焦壳。

module cw_veh_skin(state = 0, c = [0.4, 0.1, 0.1])
{
    if (state == 2) cw_m_burnt() children();
    else if (state == 1) cw_m_iron([0.27, 0.22, 0.18]) children();
    else cw_m_carpaint(c) children();
}

// 车轮（轴沿 y，中心在原点）：胎面圆环 + 钢轮辋 + 轮毂盖。state 2 只剩锈轮辋，flat 瘪胎。
module cw_veh_wheel(r = 0.34, w = 0.26, state = 0, flat = false, rimc = [0.30, 0.31, 0.30])
{
    rotate([90, 0, 0])
    {
        if (state != 2)
            cw_m_rubber([0.05, 0.05, 0.05]) scale([1, flat ? 0.86 : 1, 1]) translate([0, flat ? -r * 0.08 : 0, 0])
                rotate_extrude($fn = 20) translate([r * 0.8, 0]) scale([r * 0.2, w * 0.5]) circle(r = 1, $fn = 10);
        if (state == 2) cw_m_burnt() cylinder(h = w * 0.7, r = r * 0.66, center = true, $fn = 16);
        else cw_m_steel(rimc) cylinder(h = w * 0.72, r = r * 0.64, center = true, $fn = 16);
        cw_m_iron() for (s = [-1, 1]) translate([0, 0, s * w * 0.37]) cylinder(h = 0.02, r = r * 0.24, center = true, $fn = 10);
        if (state == 0) cw_m_chrome() for (s = [-1, 1]) translate([0, 0, s * w * 0.39]) cylinder(h = 0.02, r = r * 0.16, center = true, $fn = 10);
    }
}

// 拉达式方盒轿车车身（state 见上；open = 前门敞开；roof = 车顶行李）
module cw_veh_sedan(c = [0.4, 0.1, 0.1], state = 0, seed = 0, open = false, roof = false)
{
    // 车身下段：圆角盒 + 轮拱
    cw_veh_skin(state, c) difference()
    {
        hull()
        {
            translate([0, 0, 0.66]) cw_boxc([4.05, 1.60, 0.42]);
            translate([0, 0, 0.42]) cw_boxc([3.92, 1.54, 0.12]);
            translate([0.1, 0, 0.88]) cw_boxc([3.9, 1.56, 0.02]);
        }
        for (sx = [-1, 1]) translate([1.24 * sx, 0, 0.33]) rotate([90, 0, 0]) cylinder(h = 2.0, r = 0.4, center = true, $fn = 16);
        if (open) translate([0.25, -0.85, 0.75]) cw_boxc([0.95, 0.2, 0.6]);
    }
    // 座舱：玻璃罩 + 车顶 + 立柱
    if (state == 0) cw_m_glass() hull() { translate([-0.2, 0, 0.9]) cw_boxc([2.25, 1.5, 0.02]); translate([-0.32, 0, 1.3]) cw_boxc([1.55, 1.36, 0.02]); }
    else color(cw_DARKC()) hull() { translate([-0.2, 0, 0.9]) cw_boxc([2.2, 1.46, 0.02]); translate([-0.32, 0, 1.3]) cw_boxc([1.5, 1.32, 0.02]); }
    cw_veh_skin(state, c)
    {
        translate([-0.32, 0, 1.3]) cw_boxc([1.62, 1.4, 0.07]);
        for (sy = [-1, 1])
        {
            cw_beam([0.9, sy * 0.73, 0.9], [0.47, sy * 0.68, 1.3], 0.06);
            cw_beam([-1.3, sy * 0.73, 0.9], [-1.1, sy * 0.68, 1.3], 0.08);
            cw_beam([-0.3, sy * 0.745, 0.9], [-0.3, sy * 0.69, 1.3], 0.07);
        }
    }
    // 车门缝、门把手
    color(cw_mul(cw_DARKC(), 0.8)) for (sy = [-1, 1], x = [0.78, -0.28, -1.2]) translate([x, sy * 0.802, 0.66]) cw_boxc([0.012, 0.004, 0.42]);
    if (state != 2)
    {
        cw_m_chrome() for (sy = [-1, 1], x = [0.6, -0.45]) translate([x, sy * 0.805, 0.8]) cw_boxc([0.12, 0.012, 0.025]);
        // 前脸：格栅 + 圆灯 + 保险杠；尾灯
        cw_m_chrome() translate([2.03, 0, 0.66]) cw_boxc([0.03, 1.0, 0.16]);
        cw_m_chrome() for (sx = [-1, 1]) translate([sx * 2.06, 0, 0.44]) cw_boxc([0.06, 1.62, 0.09]);
        cw_m_lamp() for (sy = [-1, 1], k = [0, 1]) translate([2.03, sy * (0.54 + k * 0.17), 0.68]) rotate([0, 90, 0]) cylinder(h = 0.03, r = 0.075, $fn = 12);
        gk_material([0.36, 0.05, 0.03], roughness = 0.15) for (sy = [-1, 1]) translate([-2.03, sy * 0.58, 0.68]) cw_boxc([0.03, 0.34, 0.14]);
        cw_m_matte([0.44, 0.44, 0.40]) translate([-2.04, 0, 0.6]) cw_boxc([0.01, 0.42, 0.1]);   // 车牌
        cw_m_matte(cw_DARKC()) translate([-2.046, 0, 0.6]) rotate([90, 0, 90]) linear_extrude(height = 0.004, center = true)
            text(str("KV ", 10 + cw_rnd(seed, 89), "-", 10 + cw_rnd(seed + 1, 89)), size = 0.06, halign = "center", valign = "center");
        cw_m_steel([0.08, 0.08, 0.08]) for (sy = [-1, 1]) translate([0.75, sy * 0.86, 0.98]) cw_boxc([0.06, 0.12, 0.08]);   // 后视镜
    }
    else color(cw_DARKC()) for (sy = [-1, 1]) translate([2.03, sy * 0.6, 0.68]) cw_boxc([0.03, 0.3, 0.14]);
    // 敞开的前门
    if (open) cw_veh_skin(state, c) translate([0.75, -0.8, 0.45]) rotate([0, 0, -58]) translate([-0.48, 0, 0])
    {
        translate([0, 0, 0.3]) cw_boxc([0.95, 0.05, 0.6]);
        if (state == 0) cw_m_glass() translate([0.05, 0, 0.72]) cw_boxc([0.8, 0.02, 0.38]);
    }
    // 车顶行李架 + 捆着的箱包（撤离途中弃车）
    if (roof)
    {
        cw_m_iron() for (sy = [-1, 1]) translate([-0.32, sy * 0.6, 1.4]) cw_boxc([1.5, 0.03, 0.03]);
        cw_m_fabric(cw_fabric_c(seed)) translate([-0.1, 0.15, 1.52]) cw_boxc([0.7, 0.5, 0.25]);
        cw_m_canvas(cw_KHAKI()) translate([-0.7, -0.2, 1.5]) rotate([0, 0, 15]) cw_boxc([0.6, 0.4, 0.2]);
        cw_m_paper([0.30, 0.24, 0.14]) translate([-0.4, 0, 1.6]) rotate([0, 90, 0]) cylinder(h = 1.6, r = 0.006, center = true, $fn = 3);
    }
    // 底盘阴影块（车身下方的黑色底板，免得透光）
    color(cw_DARKC()) translate([0, 0, 0.3]) cw_boxc([3.4, 1.3, 0.12]);
}

// 方盒轿车（拉达式；seed 选色 / 车门 / 行李 / 瘪胎）
module cw_veh_lada(seed = 0)
{
    cw_veh_sedan(cw_car_c(seed), 0, seed, cw_rnd(seed + 2, 3) == 0, cw_rnd(seed + 3, 3) == 0);
    for (sx = [-1, 1], sy = [-1, 1]) translate([1.24 * sx, 0.72 * sy, 0.33]) cw_veh_wheel(0.33, 0.2, 0, cw_rnd(seed + sx * 3 + sy * 5, 4) == 0);
}

// 面包车（UAZ-452 式圆头厢车；seed 选涂装：军绿 / 灰绿 / 白底红十字救护车）
module cw_veh_uaz_van(seed = 0, kind = -1)
{
    k = kind >= 0 ? kind : cw_rnd(seed, 3);
    c = k == 0 ? [0.20, 0.25, 0.15] : k == 1 ? [0.36, 0.40, 0.37] : [0.46, 0.46, 0.43];
    cw_veh_skin(0, c) difference()
    {
        hull()
        {
            translate([-0.25, 0, 0.62]) cw_boxc([3.6, 1.86, 0.2]);
            translate([-0.35, 0, 1.75]) cw_boxc([3.4, 1.8, 0.2]);
            translate([1.7, 0, 0.75]) cw_boxc([0.3, 1.8, 0.45]);
            translate([1.45, 0, 1.6]) cw_boxc([0.3, 1.74, 0.3]);
        }
        for (sx = [-1, 1]) translate([1.15 * sx - 0.05, 0, 0.38]) rotate([90, 0, 0]) cylinder(h = 2.2, r = 0.44, center = true, $fn = 16);
    }
    cw_veh_skin(0, c) translate([-0.35, 0, 1.86]) cw_boxc([3.3, 1.7, 0.05]);
    // 车窗：风挡 + 侧窗带 + 后窗
    cw_m_glass() translate([1.62, 0, 1.4]) rotate([0, 18, 0]) cw_boxc([0.04, 1.6, 0.48]);
    cw_m_glass() for (sy = [-1, 1]) translate([0.95, sy * 0.935, 1.45]) cw_boxc([0.6, 0.01, 0.44]);
    cw_m_glass() for (sy = [-1, 1], x = [-0.2, -1.1]) if (k != 2 || x > 0) translate([x, sy * 0.92, 1.45]) cw_boxc([0.7, 0.01, 0.4]);
    if (k == 2) cw_m_glass() for (sy = [-1, 1]) translate([-0.65, sy * 0.92, 1.45]) cw_boxc([1.5, 0.01, 0.3]);
    color(cw_DARKC()) for (sy = [-1, 1]) translate([0.5, sy * 0.935, 1.0]) cw_boxc([0.012, 0.01, 0.9]);
    // 灯 + 保险杠 + 格栅条
    cw_m_lamp() for (sy = [-1, 1]) translate([1.86, sy * 0.62, 0.86]) rotate([0, 90, 0]) cylinder(h = 0.04, r = 0.1, $fn = 12);
    cw_m_steel(cw_mul(c, 0.7)) for (i = [0 : 3]) translate([1.86, 0, 0.72 + i * 0.07]) cw_boxc([0.02, 0.6, 0.03]);
    cw_m_steel([0.16, 0.16, 0.15]) for (sx = [-1, 1]) translate([sx * 2.0 - 0.15, 0, 0.5]) cw_boxc([0.1, 1.86, 0.12]);
    cw_m_iron() translate([-2.08, 0.6, 1.6]) for (i = [0 : 4]) translate([0, 0, -i * 0.25]) cw_boxc([0.04, 0.4, 0.03]);   // 后爬梯
    if (k == 2)
    {
        // 救护车：红十字 + 红腰线 + 顶灯
        cw_m_matte([0.42, 0.07, 0.05]) for (sy = [-1, 1]) translate([-0.6, sy * 0.942, 0.95]) { cw_boxc([3.0, 0.008, 0.08]); translate([0, 0, 0.0]) cw_boxc([0.14, 0.008, 0.46]); cw_boxc([0.46, 0.008, 0.14]); }
        cw_m_plastic([0.12, 0.20, 0.40]) translate([1.2, 0, 1.95]) cylinder(h = 0.14, r = 0.09, $fn = 10);
    }
    else cw_m_matte(cw_MARKW()) for (sy = [-1, 1]) translate([-0.7, sy * 0.942, 1.0]) rotate([90, 0, sy > 0 ? 180 : 0]) linear_extrude(height = 0.006, center = true)
        text(str(10 + cw_rnd(seed, 80), "-", 10 + cw_rnd(seed + 1, 80)), size = 0.18, halign = "center", valign = "center");
    color(cw_DARKC()) translate([-0.2, 0, 0.42]) cw_boxc([3.2, 1.4, 0.16]);
    for (sx = [-1, 1], sy = [-1, 1]) translate([1.15 * sx - 0.05, 0.8 * sy, 0.38]) cw_veh_wheel(0.38, 0.22, 0, cw_rnd(seed + sx + sy * 3, 5) == 0);
}

// 救护车（UAZ-452 救护型）
module cw_veh_ambulance(seed = 0) cw_veh_uaz_van(seed, 2);

// 军卡（乌拉尔式长鼻卡车 + 帆布篷）
module cw_veh_truck_canvas(seed = 0)
{
    c = [0.19, 0.23, 0.14];
    cw_m_iron([0.12, 0.12, 0.11]) translate([-0.4, 0, 0.78]) cw_boxc([6.6, 0.9, 0.2]);   // 大梁
    // 长鼻机盖 + 翼子板
    cw_veh_skin(0, c)
    {
        hull() { translate([2.3, 0, 1.0]) cw_boxc([1.3, 1.1, 0.5]); translate([2.25, 0, 1.33]) cw_boxc([1.2, 0.95, 0.08]); }
        for (sy = [-1, 1]) translate([2.25, sy * 0.88, 1.03]) hull() { cw_boxc([1.3, 0.55, 0.06]); translate([0.35, 0, -0.35]) cw_boxc([0.5, 0.55, 0.04]); }
    }
    cw_m_steel([0.10, 0.10, 0.10]) for (i = [0 : 6]) translate([2.96, -0.42 + i * 0.14, 1.0]) cw_boxc([0.02, 0.05, 0.42]);   // 格栅
    cw_m_lamp() for (sy = [-1, 1]) translate([2.6, sy * 0.85, 1.12]) rotate([0, 90, 0]) cylinder(h = 0.1, r = 0.11, $fn = 12);
    cw_m_steel([0.12, 0.12, 0.11]) translate([3.0, 0, 0.66]) cw_boxc([0.14, 2.1, 0.2]);   // 保险杠
    cw_m_iron() translate([3.15, 0, 0.66]) rotate([0, 90, 0]) cylinder(h = 0.25, r = 0.13, center = true, $fn = 10);   // 绞盘
    // 驾驶室：钢壳 + 窗
    cw_veh_skin(0, c) difference()
    {
        translate([1.1, 0, 1.4]) cw_boxc([1.1, 2.1, 1.3]);
        translate([1.7, 0, 1.75]) cw_boxc([0.3, 1.8, 0.5]);
        for (sy = [-1, 1]) translate([1.15, sy * 1.1, 1.75]) cw_boxc([0.6, 0.3, 0.45]);
    }
    cw_m_glass() translate([1.6, 0, 1.75]) cw_boxc([0.02, 1.82, 0.52]);
    cw_m_glass() for (sy = [-1, 1]) translate([1.15, sy * 1.0, 1.75]) cw_boxc([0.62, 0.01, 0.46]);
    cw_veh_skin(0, c) translate([1.1, 0, 2.08]) cw_boxc([1.16, 2.14, 0.06]);
    cw_m_steel([0.10, 0.10, 0.10]) for (sy = [-1, 1]) translate([1.62, sy * 1.12, 1.9]) cw_boxc([0.04, 0.16, 0.28]);
    // 备胎 + 油箱
    translate([0.4, 0, 1.6]) rotate([0, 0, 90]) cw_veh_wheel(0.5, 0.32);
    cw_m_steel(c) for (sy = [-1, 1]) translate([0.3, sy * 0.85, 0.75]) rotate([0, 90, 0]) cylinder(h = 0.9, r = 0.22, center = true, $fn = 12);
    // 货台（木栏板）+ 帆布拱篷
    cw_m_planks([0.20, 0.24, 0.15]) translate([-1.65, 0, 1.15]) cw_boxc([3.6, 2.3, 0.12]);
    cw_m_planks([0.20, 0.24, 0.15]) for (sy = [-1, 1]) translate([-1.65, sy * 1.12, 1.45]) cw_boxc([3.6, 0.06, 0.5]);
    cw_m_planks([0.20, 0.24, 0.15]) translate([-3.42, 0, 1.45]) cw_boxc([0.06, 2.3, 0.5]);
    cw_m_canvas([0.26, 0.27, 0.17]) difference()
    {
        translate([-1.65, 0, 1.5]) scale([1, 1, 0.82]) rotate([0, 90, 0]) cylinder(h = 3.6, r = 1.15, center = true, $fn = 20);
        translate([-1.65, 0, 1.5]) scale([1, 1, 0.82]) rotate([0, 90, 0]) cylinder(h = 3.5, r = 1.11, center = true, $fn = 20);
        translate([-1.65, 0, 0.4]) cw_boxc([3.8, 2.6, 2.2]);
        translate([-3.5, 0, 1.6]) cw_boxc([0.3, 1.4, 1.2]);   // 后篷帘卷起
    }
    cw_m_iron() for (i = [0 : 3]) translate([-3.2 + i * 1.05, 0, 1.5]) scale([1, 1, 0.82]) rotate([0, 90, 0]) difference() { cylinder(h = 0.04, r = 1.13, center = true, $fn = 20); cylinder(h = 0.1, r = 1.1, center = true, $fn = 20); translate([0.6, 0, 0]) cw_boxc([1.2, 2.4, 0.2]); }
    cw_m_canvas([0.22, 0.23, 0.14]) translate([-3.45, 0, 2.15]) rotate([0, 90, 0]) cylinder(h = 0.12, r = 0.12, center = true, $fn = 8);
    color(cw_DARKC()) translate([-1.65, 0, 1.6]) cw_boxc([3.4, 2.1, 0.02]);
    // 车厢里：弹药箱
    for (i = [0 : 1]) translate([-2.7 + i * 0.8, 0.4 - i * 0.6, 1.21]) rotate([0, 0, 90]) cw_prop_crate_ammo(seed + i);
    cw_m_matte([0.46, 0.45, 0.40]) for (sy = [-1, 1]) translate([1.0, sy * 1.06, 1.2]) rotate([90, 0, sy > 0 ? 180 : 0]) linear_extrude(height = 0.006, center = true)
        text(str(1 + cw_rnd(seed, 9), cw_rnd(seed + 1, 9), "-", 1 + cw_rnd(seed + 2, 9), cw_rnd(seed + 3, 9)), size = 0.14, halign = "center", valign = "center");
    for (sy = [-1, 1])
    {
        translate([2.15, sy * 0.98, 0.5]) cw_veh_wheel(0.5, 0.34);
        translate([-0.9, sy * 0.98, 0.5]) cw_veh_wheel(0.5, 0.34, 0, cw_rnd(seed + sy, 3) == 0);
        translate([-2.1, sy * 0.98, 0.5]) cw_veh_wheel(0.5, 0.34);
    }
}

// 中巴（PAZ 式；seed 决定涂装）
module cw_veh_bus(seed = 0)
{
    c = cw_rnd(seed, 2) == 0 ? [0.48, 0.44, 0.32] : [0.15, 0.29, 0.34];
    cw_veh_skin(0, c) difference()
    {
        hull() { translate([0, 0, 0.75]) cw_boxc([6.4, 2.2, 0.7]); translate([-0.1, 0, 2.15]) cw_boxc([6.0, 2.1, 0.12]); translate([3.15, 0, 1.6]) cw_boxc([0.1, 2.0, 0.7]); }
        for (sx = [-1, 1]) translate([2.0 * sx, 0, 0.4]) rotate([90, 0, 0]) cylinder(h = 2.4, r = 0.5, center = true, $fn = 16);
    }
    cw_m_glass() translate([0.0, 0, 1.75]) cw_boxc([5.6, 2.22, 0.62]);
    cw_veh_skin(0, c) for (x = [-2.6 : 0.95 : 2.4]) translate([x, 0, 1.75]) cw_boxc([0.1, 2.24, 0.62]);
    cw_m_glass() translate([3.19, 0, 1.5]) rotate([0, -8, 0]) cw_boxc([0.02, 1.9, 0.9]);
    cw_veh_skin(0, cw_mul(c, 0.7)) translate([0, 0, 0.62]) cw_boxc([6.42, 2.22, 0.3]);
    color(cw_DARKC()) translate([2.2, -1.1, 1.1]) cw_boxc([0.9, 0.04, 1.5]);   // 敞开的车门
    cw_m_glass() translate([2.7, -1.25, 1.1]) rotate([0, 0, -50]) cw_boxc([0.45, 0.02, 1.4]);
    cw_m_lamp() for (sy = [-1, 1]) translate([3.2, sy * 0.75, 0.78]) rotate([0, 90, 0]) cylinder(h = 0.04, r = 0.11, $fn = 12);
    gk_material([0.36, 0.05, 0.03], roughness = 0.15) for (sy = [-1, 1]) translate([-3.21, sy * 0.75, 0.8]) cw_boxc([0.03, 0.26, 0.16]);
    cw_m_matte(cw_MARKW()) translate([3.2, 0, 2.05]) cw_boxc([0.02, 1.2, 0.18]);
    cw_m_matte(cw_DARKC()) translate([3.212, 0, 2.05]) rotate([90, 0, 90]) linear_extrude(height = 0.004, center = true) text("EVAKUACE", size = 0.11, halign = "center", valign = "center");
    color(cw_DARKC()) translate([0, 0, 0.45]) cw_boxc([5.4, 1.8, 0.16]);
    for (sx = [-1, 1], sy = [-1, 1]) translate([2.0 * sx, 0.95 * sy, 0.42]) cw_veh_wheel(0.42, 0.28, 0, cw_rnd(seed + sx + sy * 2, 4) == 0);
}

// 农用拖拉机（Zetor 式：发动机罩 + 带玻璃驾驶室 + 大后轮）
module cw_veh_tractor(seed = 0)
{
    c = cw_rnd(seed, 2) == 0 ? [0.42, 0.10, 0.06] : [0.17, 0.30, 0.18];
    cw_m_iron([0.14, 0.14, 0.13]) translate([0.4, 0, 0.75]) cw_boxc([2.6, 0.7, 0.4]);   // 底盘
    cw_veh_skin(0, c) hull() { translate([1.0, 0, 1.0]) cw_boxc([1.6, 0.78, 0.6]); translate([1.0, 0, 1.36]) cw_boxc([1.5, 0.66, 0.06]); }
    cw_m_steel([0.12, 0.12, 0.12]) for (i = [0 : 4]) translate([1.81, -0.24 + i * 0.12, 1.05]) cw_boxc([0.02, 0.06, 0.5]);
    cw_m_lamp() for (sy = [-1, 1]) translate([1.8, sy * 0.3, 1.32]) rotate([0, 90, 0]) cylinder(h = 0.04, r = 0.06, $fn = 10);
    cw_m_iron() translate([1.3, 0.22, 1.36]) cylinder(h = 0.8, r = 0.05, $fn = 8);   // 排气管
    // 驾驶室：立柱 + 玻璃 + 顶
    cw_veh_skin(0, c) for (sx = [-1, 1], sy = [-1, 1]) translate([-0.55 + sx * 0.5, sy * 0.62, 1.75]) cw_boxc([0.06, 0.06, 1.3]);
    cw_veh_skin(0, c) translate([-0.55, 0, 2.42]) cw_boxc([1.2, 1.36, 0.06]);
    cw_m_glass() translate([-0.05, 0, 1.8]) cw_boxc([0.01, 1.2, 1.1]);
    cw_m_glass() translate([-1.05, 0, 1.8]) cw_boxc([0.01, 1.2, 1.1]);
    cw_m_glass() translate([-0.55, 0.62, 1.8]) cw_boxc([0.95, 0.01, 1.1]);
    cw_veh_skin(0, c) for (sy = [-1, 1]) translate([-0.85, sy * 0.85, 1.55]) hull() { cw_boxc([0.9, 0.42, 0.04]); translate([0.4, 0, -0.35]) cw_boxc([0.1, 0.42, 0.04]); }   // 后挡泥板
    cw_m_fabric([0.10, 0.10, 0.10]) translate([-0.7, 0, 1.25]) cw_boxc([0.45, 0.45, 0.1]);
    cw_m_iron() translate([-0.25, 0, 1.4]) rotate([0, -30, 0]) cylinder(h = 0.45, r = 0.025, $fn = 6);
    cw_m_plastic([0.08, 0.08, 0.08]) translate([-0.08, 0, 1.78]) rotate([0, 60, 0]) cylinder(h = 0.03, r = 0.18, center = true, $fn = 12);
    cw_m_iron() translate([-1.35, 0, 0.7]) cw_boxc([0.3, 0.5, 0.15]);   // 牵引钩
    for (sy = [-1, 1]) translate([-0.85, sy * 0.85, 0.78]) cw_veh_wheel(0.78, 0.42, 0, false, c);
    for (sy = [-1, 1]) translate([1.35, sy * 0.68, 0.42]) cw_veh_wheel(0.42, 0.24, 0, false, c);
}

// 装甲运兵车（BTR 式八轮 + 小炮塔 + 舱口 + 工具 + 牵引钢缆）
module cw_veh_btr(seed = 0)
{
    c = [0.20, 0.24, 0.14];
    // 车体：上下两层倾斜装甲
    cw_veh_skin(0, c) hull()
    {
        translate([0, 0, 0.75]) cw_boxc([6.2, 2.0, 0.1]);
        translate([-0.1, 0, 1.25]) cw_boxc([6.6, 2.6, 0.5]);
        translate([-0.4, 0, 1.92]) cw_boxc([5.2, 2.1, 0.06]);
        translate([3.45, 0, 1.15]) cw_boxc([0.1, 1.9, 0.5]);
        translate([-3.35, 0, 1.25]) cw_boxc([0.1, 2.2, 0.6]);
    }
    color(cw_DARKC()) translate([0, 0, 0.62]) cw_boxc([6.0, 1.9, 0.3]);
    // 炮塔 + 机枪
    cw_veh_skin(0, c) translate([0.6, 0, 1.92]) cylinder(h = 0.45, r1 = 0.68, r2 = 0.5, $fn = 16);
    cw_m_steel([0.12, 0.13, 0.11]) translate([1.1, 0, 2.18]) rotate([0, 86, 0]) cylinder(h = 1.6, r = 0.05, $fn = 8);
    cw_m_steel([0.12, 0.13, 0.11]) translate([1.1, 0.14, 2.16]) rotate([0, 86, 0]) cylinder(h = 0.7, r = 0.025, $fn = 6);
    // 驾驶员观察窗 + 舱盖
    cw_m_glass() for (sy = [-1, 1]) translate([2.45, sy * 0.4, 1.68]) rotate([0, 40, 0]) cw_boxc([0.02, 0.5, 0.18]);
    cw_veh_skin(0, cw_mul(c, 0.85)) for (p = [[1.8, -0.45], [1.8, 0.45], [-1.2, 0], [-2.1, 0]]) translate([p[0], p[1], 1.94]) cylinder(h = 0.08, r = 0.3, $fn = 12);
    cw_veh_skin(0, cw_mul(c, 0.85)) translate([-1.2, 0, 2.3]) rotate([0, -70, 0]) translate([0.3, 0, 0]) cylinder(h = 0.06, r = 0.3, $fn = 12);   // 一个舱盖开着
    // 侧门 + 射击孔
    color(cw_mul(cw_DARKC(), 0.8)) for (sy = [-1, 1]) translate([-0.3, sy * 1.21, 1.2]) cw_boxc([0.9, 0.02, 0.6]);
    cw_m_iron() for (sy = [-1, 1], x = [-2.4, -1.7, 1.0]) translate([x, sy * 1.215, 1.4]) rotate([90, 0, 0]) cylinder(h = 0.03, r = 0.06, center = true, $fn = 8);
    // 工具：撬棍、铲子、钢缆、备用油桶
    cw_m_timber(cw_WOODC()) translate([0.5, -1.12, 1.62]) rotate([0, 90, 0]) cylinder(h = 1.4, r = 0.03, center = true, $fn = 6);
    cw_m_iron() for (sy = [-1, 1]) for (i = [0 : 7]) translate([-2.6 + i * 0.6, sy * 1.0, 1.95]) rotate([0, 90, 0]) cylinder(h = 0.62, r = 0.022, center = true, $fn = 4);
    cw_m_steel(c) translate([-3.0, 0.5, 1.95]) rotate([0, 90, 0]) cylinder(h = 0.6, r = 0.22, center = true, $fn = 12);
    cw_m_lamp() for (sy = [-1, 1]) translate([3.3, sy * 0.75, 1.45]) rotate([0, 90, 0]) cylinder(h = 0.06, r = 0.08, $fn = 10);
    cw_m_matte([0.46, 0.45, 0.40]) for (sy = [-1, 1]) translate([0.0, sy * 1.312, 1.25]) rotate([90, 0, sy > 0 ? 180 : 0]) linear_extrude(height = 0.006, center = true)
        text(str(1 + cw_rnd(seed, 8), 1 + cw_rnd(seed + 1, 8), 1 + cw_rnd(seed + 2, 8)), size = 0.32, halign = "center", valign = "center");
    for (sy = [-1, 1], i = [0 : 3])
        translate([2.25 - i * 1.4 - (i >= 2 ? 0.25 : 0), sy * 1.1, 0.55]) cw_veh_wheel(0.55, 0.36, 0, cw_rnd(seed + i + sy * 5, 6) == 0, c);
}

// 主战坦克（T 系）：履带 + 负重轮 + 侧裙 + 楔形首上 + 扁圆炮塔 + 热护套炮管 + 尾部油桶
module cw_veh_tank(seed = 0)
{
    c = [0.20, 0.23, 0.14];
    for (sy = [-1, 1])
    {
        // 履带：上下两段 + 前后弯
        cw_m_iron([0.14, 0.13, 0.12]) translate([0, sy * 1.25, 0])
        {
            translate([0, 0, 0.06]) cw_boxc([5.2, 0.62, 0.12]);
            translate([0.15, 0, 0.98]) cw_boxc([5.6, 0.62, 0.08]);
            for (sx = [-1, 1]) translate([sx * 2.75, 0, 0.55]) rotate([90, 0, 0]) difference() { cylinder(h = 0.62, r = 0.48, center = true, $fn = 14); cylinder(h = 0.7, r = 0.38, center = true, $fn = 14); translate([-sx * 0.5, 0, 0]) cw_boxc([1.0, 1.2, 1]); }
        }
        cw_m_iron([0.12, 0.12, 0.11]) for (i = [0 : 15]) translate([-2.4 + i * 0.32, sy * 1.25, 0.12]) cw_boxc([0.08, 0.64, 0.04]);   // 履带齿
        cw_m_steel(c) for (i = [0 : 5]) translate([-2.2 + i * 0.85, sy * 1.25, 0.42]) rotate([90, 0, 0]) cylinder(h = 0.5, r = 0.33, center = true, $fn = 14);
        cw_m_rubber() for (i = [0 : 5]) translate([-2.2 + i * 0.85, sy * 1.25, 0.42]) rotate([90, 0, 0]) cylinder(h = 0.52, r = 0.2, center = true, $fn = 10);
        // 侧裙（橡胶裙板，前段缺失）
        cw_m_rubber([0.08, 0.09, 0.07]) translate([-0.4, sy * 1.58, 0.82]) cw_boxc([4.6, 0.03, 0.32]);
    }
    // 车体 + 首上楔形
    cw_veh_skin(0, c) hull()
    {
        translate([-0.2, 0, 0.75]) cw_boxc([5.6, 2.0, 0.5]);
        translate([-0.4, 0, 1.3]) cw_boxc([5.4, 3.3, 0.12]);
        translate([3.0, 0, 0.9]) cw_boxc([0.1, 2.0, 0.4]);
    }
    cw_veh_skin(0, cw_mul(c, 0.9)) for (sy = [-1, 1]) translate([-0.3, sy * 1.55, 1.38]) cw_boxc([5.0, 0.3, 0.04]);   // 翼子板
    // 炮塔：扁圆 + 楔形前装甲
    cw_veh_skin(0, c) translate([-0.3, 0, 1.35]) hull()
    {
        scale([1.25, 1, 0.42]) sphere(r = 1.1, $fn = 18);
        translate([0.9, 0, 0.12]) scale([0.5, 0.9, 0.25]) sphere(r = 1.0, $fn = 12);
    }
    cw_veh_skin(0, cw_mul(c, 0.85)) translate([-0.6, 0.45, 1.75]) cylinder(h = 0.12, r = 0.32, $fn = 12);
    cw_veh_skin(0, cw_mul(c, 0.85)) translate([-0.5, -0.45, 1.8]) rotate([0, -70, 0]) translate([0.3, 0, 0]) cylinder(h = 0.06, r = 0.3, $fn = 12);   // 车长舱盖开着
    cw_m_steel([0.12, 0.13, 0.11]) translate([-0.4, -0.45, 1.85]) rotate([0, 85, 0]) cylinder(h = 0.9, r = 0.03, $fn = 6);   // 高射机枪
    // 主炮：炮根 + 热护套 + 抽气筒 + 炮口
    cw_veh_skin(0, c) translate([0.8, 0, 1.55]) rotate([0, 88, 0]) cylinder(h = 0.5, r = 0.2, $fn = 12);
    cw_veh_skin(0, cw_mul(c, 0.9)) translate([1.2, 0, 1.57]) rotate([0, 88, 0]) cylinder(h = 3.6, r = 0.1, $fn = 12);
    cw_veh_skin(0, cw_mul(c, 0.9)) translate([2.8, 0, 1.62]) rotate([0, 88, 0]) cylinder(h = 0.5, r = 0.14, $fn = 12);
    cw_m_iron() translate([4.78, 0, 1.69]) rotate([0, 88, 0]) cylinder(h = 0.12, r = 0.11, $fn = 12);
    // 车尾：外挂油桶 + 自救圆木 + 备用履带板
    cw_m_steel([0.24, 0.12, 0.07]) for (sy = [-1, 1]) translate([-3.15, sy * 0.55, 1.3]) rotate([90, 0, 0]) cylinder(h = 0.9, r = 0.28, center = true, $fn = 12);
    cw_m_bark(cw_TRUNKC()) translate([-3.2, 0, 1.72]) rotate([90, 0, 0]) cylinder(h = 2.8, r = 0.13, center = true, $fn = 8);
    cw_m_iron() for (sy = [-1, 1]) translate([1.5, sy * 1.0, 1.36]) cw_boxc([0.6, 0.4, 0.04]);
    cw_m_lamp() for (sy = [-1, 1]) translate([2.6, sy * 0.85, 1.32]) rotate([0, 80, 0]) cylinder(h = 0.06, r = 0.08, $fn = 10);
    cw_m_matte([0.46, 0.45, 0.40]) for (sy = [-1, 1]) translate([-0.4, sy * 1.21, 1.62]) rotate([90, 0, sy > 0 ? 180 : 0]) linear_extrude(height = 0.006, center = true)
        text(str(1 + cw_rnd(seed, 8), 1 + cw_rnd(seed + 1, 8), 1 + cw_rnd(seed + 2, 8)), size = 0.24, halign = "center", valign = "center");
}

// 坠毁运输直升机（Mi-8 式）：机身侧倾 + 断尾梁 + 折弯旋翼 + 焦土
module cw_veh_heli_wreck(seed = 0)
{
    cw_m_soil([0.14, 0.13, 0.12]) scale([5.5, 3.5, 1]) cylinder(h = 0.03, r = 1, $fn = 14);
    rotate([0, 0, cw_rnd(seed, 40) - 20])
    {
        rotate([8, 0, 0]) translate([0, 0, 0.9])
        {
            cw_m_carpaint(cw_OLIVE()) scale([1, 0.62, 0.62]) rotate([0, 90, 0]) cylinder(h = 6.0, r = 1.5, center = true, $fn = 16);
            cw_m_glass() translate([2.9, 0, 0]) scale([0.5, 0.56, 0.56]) sphere(r = 1.45, $fn = 14);
            cw_m_carpaint(cw_OLIVED()) translate([0.3, 0, 1.0]) hull() { cw_boxc([2.6, 1.3, 0.5]); translate([0, 0, 0.3]) cw_boxc([2.2, 1.0, 0.1]); }
            color(cw_DARKC()) translate([0.5, -0.94, 0.2]) cw_boxc([1.3, 0.1, 0.9]);
            cw_m_glass() for (x = [-1.8 : 0.7 : 0.2]) translate([x, -0.93, 0.35]) rotate([90, 0, 0]) cylinder(h = 0.02, r = 0.18, center = true, $fn = 12);
            cw_m_burnt() translate([1.2, 0.2, 1.1]) scale([1, 0.8, 0.5]) sphere(r = 0.8, $fn = 10);   // 发动机舱烧穿
            cw_m_iron() translate([0.3, 0, 1.4]) cylinder(h = 0.5, r = 0.22, $fn = 10);
            for (a = [0, 72, 144, 216, 288])
                cw_m_carpaint(cw_OLIVED()) rotate([0, 0, a + cw_rnd(seed, 30)]) translate([2.1, 0, 1.75])
                    rotate([0, a % 144 == 0 ? 28 : 14, 0]) cw_boxc([4.0, 0.32, 0.05]);
            cw_m_matte(cw_REDC()) for (sy = [-1, 1]) translate([-1.2, sy * 0.92, 0.3]) rotate([90, 0, 0]) linear_extrude(height = 0.01, center = true)
                polygon([for (i = [0 : 9]) let(a = 90 + i * 36, r = i % 2 == 0 ? 0.3 : 0.12) [r * cos(a), r * sin(a)]]);
        }
        translate([-4.6, 1.2, 0.4]) rotate([0, 0, 25])
        {
            cw_m_carpaint(cw_OLIVE()) rotate([0, 85, 0]) cylinder(h = 3.4, r1 = 0.5, r2 = 0.28, $fn = 10);
            cw_m_carpaint(cw_OLIVED()) translate([3.2, 0, 0.6]) cw_boxc([0.8, 0.06, 1.3]);
            cw_m_iron() translate([3.3, 0.35, 0.9]) rotate([90, 0, 0]) cylinder(h = 0.16, r = 0.5, $fn = 10);
        }
        cw_m_carpaint(cw_OLIVED()) translate([1.5, -2.6, 0.1]) rotate([0, 0, 70]) cw_boxc([2.6, 0.3, 0.06]);
        for (i = [0 : 5])
            cw_m_iron(cw_rnd(seed + i, 2) == 0 ? cw_OLIVED() : cw_METALD())
                translate([cw_rndr(seed * 3 + i * 17, -3.5, 3.5), cw_rndr(seed * 7 + i * 23 + 2, -2.6, 2.6), 0.05])
                    rotate([cw_rndr(seed + i, -20, 20), 0, cw_rnd(seed + i * 5, 180)]) cw_boxc([0.6, 0.4, 0.03]);
    }
}

// 锈蚀弃车残骸：无玻璃 + 缺轮 + 引擎盖掀开 + 杂草长进车里
module cw_veh_wreck(seed = 0)
{
    rotate([0, 0, cw_rnd(seed, 7) - 3]) translate([0, 0, -0.08]) rotate([cw_rndr(seed, -2, 2), 1.5, 0])
    {
        cw_veh_sedan([0.3, 0.25, 0.2], 1, seed, cw_rnd(seed + 1, 2) == 0, false);
        cw_m_iron() translate([1.55, 0, 1.15]) rotate([0, -40, 0]) cw_boxc([1.0, 1.4, 0.03]);   // 掀开的机盖
        cw_m_iron([0.15, 0.15, 0.14]) translate([1.35, 0, 0.75]) cw_boxc([0.6, 0.6, 0.35]);   // 发动机
        for (sx = [-1, 1]) translate([1.24 * sx, 0.72, 0.33]) cw_veh_wheel(0.33, 0.2, 0, true);
        translate([1.24, -0.72, 0.33]) cw_veh_wheel(0.33, 0.2, 0, true);
        cw_m_iron() translate([-1.24, -0.72, 0.18]) rotate([90, 0, 0]) cylinder(h = 0.1, r = 0.15, $fn = 10);
        translate([-1.24, -0.72, 0.1]) cw_m_concrete(cw_CONCD()) cw_boxc([0.3, 0.3, 0.2]);   // 垫的砖块
        cw_m_leaf(cw_GRASSD()) translate([-0.4, 0, 0.92]) for (a = [0 : 60 : 300]) rotate([0, 25, a]) cylinder(h = 0.6, r1 = 0.08, r2 = 0.01, $fn = 4);
    }
}

// 烧毁的轿车（焦黑车壳 + 锈；只剩轮辋，车身沉到轮辋上；四周碎玻璃和焦痕）
module cw_veh_car_burnt(seed = 0)
{
    cw_m_soil([0.10, 0.09, 0.09]) translate([0, 0, 0.005]) scale([2.6, 1.4, 1]) cw_decal(seed, 1.0, 1, 0.004);
    rotate([0, 0, cw_rnd(seed, 9) - 4]) translate([0, 0, -0.12])
    {
        cw_veh_sedan([0.1, 0.1, 0.1], 2, seed, cw_rnd(seed + 2, 3) == 0, false);
        for (sx = [-1, 1], sy = [-1, 1]) translate([1.24 * sx, 0.72 * sy, 0.33]) cw_veh_wheel(0.33, 0.2, 2);
        cw_m_burnt() translate([1.5, 0.1, 0.9]) rotate([0, -6, 3]) cw_boxc([1.0, 1.4, 0.03]);
    }
    cw_m_shard() for (i = [0 : 5]) translate([cw_rndr(seed + i, -1.5, 1.5), (i % 2 == 0 ? -1 : 1) * cw_rndr(seed + i * 3, 0.9, 1.4), 0.005])
        rotate([0, 0, i * 61]) linear_extrude(height = 0.004) polygon(cw_shard(seed + i * 7, 0.14));
}

// 货运车厢（棚车，沿 x）
module cw_veh_freight_car(seed = 0)
{
    c = [[0.36, 0.17, 0.11], [0.26, 0.30, 0.26], [0.32, 0.26, 0.19]][cw_rnd(seed, 3)];
    cw_m_planks(c) translate([0, 0, 2.3]) cw_boxc([13.0, 2.9, 2.8]);
    cw_m_steel(cw_mul(c, 0.75))
    {
        for (i = [0 : 12], sy = [-1, 1]) translate([-6.2 + i * 1.03, sy * 1.46, 2.3]) cw_boxc([0.08, 0.04, 2.8]);
        translate([0, 0, 3.75]) hull() { cw_boxc([13.1, 3.0, 0.06]); translate([0, 0, 0.18]) cw_boxc([13.1, 1.0, 0.02]); }
    }
    color(cw_DARKC()) translate([0.5, -1.47, 2.0]) cw_boxc([2.2, 0.03, 2.2]);
    cw_m_planks(c) translate([2.0, -1.56, 2.0]) cw_boxc([2.2, 0.06, 2.2]);
    cw_m_iron() translate([1.3, -1.6, 3.15]) cw_boxc([4.4, 0.06, 0.06]);
    cw_m_matte([0.46, 0.45, 0.40]) translate([-3.6, -1.48, 3.0]) rotate([90, 0, 0]) linear_extrude(height = 0.006, center = true)
        text(str("CSD ", 20 + cw_rnd(seed, 70), " ", 100 + cw_rnd(seed + 1, 800)), size = 0.24, halign = "center", valign = "center");
    cw_m_iron([0.14, 0.13, 0.12])
    {
        translate([0, 0, 0.85]) cw_boxc([12.4, 2.4, 0.3]);
        for (sx = [-1, 1]) translate([sx * 4.8, 0, 0.5]) cw_boxc([2.2, 2.0, 0.5]);
        for (sx = [-1, 1], k = [-1, 1], sy = [-1, 1]) translate([sx * 4.8 + k * 0.8, sy * 0.72, 0.45])
            rotate([90, 0, 0]) cylinder(h = 0.12, r = 0.42, center = true, $fn = 16);
        for (sx = [-1, 1], sy = [-1, 1]) translate([sx * 6.6, sy * 0.85, 0.9]) rotate([0, 90, 0]) cylinder(h = 0.4, r = 0.18, center = true, $fn = 10);
    }
}

// 油罐车厢（沿 x）
module cw_veh_tank_car(seed = 0)
{
    cw_m_steel(cw_rnd(seed, 2) == 0 ? [0.10, 0.10, 0.11] : [0.34, 0.36, 0.36])
        translate([0, 0, 2.2]) rotate([0, 90, 0]) cylinder(h = 11.0, r = 1.4, center = true, $fn = 20);
    cw_m_iron([0.14, 0.13, 0.12])
    {
        for (sx = [-1, 1]) translate([sx * 5.5, 0, 2.2]) scale([0.2, 1, 1]) sphere(r = 1.4, $fn = 16);
        translate([0, 0, 3.6]) cylinder(h = 0.35, r = 0.35, $fn = 12);
        translate([0, 0, 0.85]) cw_boxc([11.4, 2.2, 0.3]);
        for (sx = [-1, 1], k = [-1, 1], sy = [-1, 1]) translate([sx * 4.2 + k * 0.8, sy * 0.72, 0.45])
            rotate([90, 0, 0]) cylinder(h = 0.12, r = 0.42, center = true, $fn = 16);
        for (sx = [-1, 0, 1]) translate([sx * 3.6, 0, 2.2]) rotate([0, 90, 0]) cylinder(h = 0.12, r = 1.43, center = true, $fn = 20);
    }
    cw_m_oil() translate([2.0, -1.38, 1.6]) rotate([0, 90, 0]) scale([1, 0.3, 1]) cylinder(h = 3.0, r = 0.4, $fn = 10);
}
// ================= 武器（PBR：gk_material 粗糙度 / 金属度） =================
// 每件武器两层：
//   cw_wpn_<id>_held()  手持坐标系：原点 = 右手握把（握拳）中心；枪口 / 刃尖朝 -Y，顶面 +Z，
//                       +X 为武器左侧（拉机柄、快慢机、枪栓柄都在 -X 的右侧）。与
//                       nextdayz_survivor.scad 的 weapon frame / bone_weapon_socket 一致：
//                       NextDayz 经 nextdayz_weapons.scad 把它原样挂到手上，第一人称视模也用它。
//   cw_wpn_<id>(seed)   地面 loot / 地图手摆：右侧朝上平躺，枪口朝 +X，沿长度居中，贴 z = 0。
// 侧视坐标一律写成 [f, z]：f = 朝枪口的前向距离（= -Y），z = 高度。
// 近战武器（刀 / 撬棍 / 斧）同理：cw_wpn_<id>_held + 物资名 cw_item_<id> 的平躺版本。

// ---- 材质 ----
// 发蓝 / 磷化钢带 gk_texture("worn")：涂层磨穿处露出更亮、更光滑的钢（印花自带颜色与金属度/粗糙度系数，
// 所以 gk_material 用白色）；木件用 "woodgrain"：顺 +u 的年轮、清漆填平的早材更光滑。
module cw_m_blued()  { gk_material([1, 1, 1], roughness = 0.32, metalness = 0.85) gk_texture("worn", palette = [[0.17, 0.175, 0.19], [0.50, 0.50, 0.52]], scale = 0.12) children(); } // 发蓝钢：枪管、枪机座
module cw_m_park()   { gk_material([1, 1, 1], roughness = 0.55, metalness = 0.6) gk_texture("worn", palette = [[0.21, 0.21, 0.20], [0.48, 0.48, 0.48]], scale = 0.12, seed = 1) children(); }   // 磷化哑光钢：冲压机匣
module cw_m_bright() { gk_material([0.64, 0.64, 0.65], roughness = 0.2, metalness = 1.0) children(); }     // 磨亮钢：枪机、刃口
module cw_m_silver() { gk_material([0.55, 0.55, 0.54], roughness = 0.28, metalness = 0.95) children(); }   // 白钢 / 镀镍机匣
module cw_m_etch()   { gk_material([0.34, 0.34, 0.33], roughness = 0.6, metalness = 0.9) children(); }     // 雕花面（压暗）
module cw_m_hole()   { gk_material([0.025, 0.025, 0.028], roughness = 0.75, metalness = 0.2) children(); } // 孔洞 / 内腔
module cw_m_anod()   { gk_material([0.05, 0.052, 0.056], roughness = 0.42, metalness = 0.5) children(); }  // 黑色阳极氧化：瞄具
module cw_m_wood(c)  { gk_material(c, roughness = 0.36, metalness = 0) gk_texture("woodgrain", scale = 0.25) children(); } // 清漆木
module cw_m_check(c) { gk_material(c, roughness = 0.85, metalness = 0) children(); }                         // 握把网纹
module cw_m_bake(c)  { gk_material(c, roughness = 0.3, metalness = 0) children(); }                          // 胶木：AK-74 弹匣、握片
module cw_m_rubber(c = [0.035, 0.035, 0.035]) { gk_material(c, roughness = 0.9, metalness = 0) children(); }
module cw_m_leather(c) { gk_material(c, roughness = 0.7, metalness = 0) children(); }
module cw_m_brass()  { gk_material([0.80, 0.60, 0.30], roughness = 0.26, metalness = 1.0) children(); }
module cw_m_paint(c) { gk_material(c, roughness = 0.3, metalness = 0.05) children(); }                       // 搪瓷漆
module cw_m_lens()   { gk_material([0.05, 0.10, 0.16], roughness = 0.02, metalness = 0.3) children(); }

// ---- 武器几何工具 ----
// 注意：本引擎求值器忽略 builtin 调用参数里的 $fn（cylinder(..., $fn = 8) 无效），
// 只认作用域内的 `$fn = N;` 赋值，所以分段数一律经下面这些在模块体内赋值的工具传入。
module cw_cyl(h, r, fn = 16, r2 = -1, center = false) { $fn = fn; cylinder(h = h, r1 = r, r2 = r2 < 0 ? r : r2, center = center); }
module cw_sph(r, fn = 16) { $fn = fn; sphere(r = r); }
module cw_torus(R, r, fn = 16) { $fn = fn; rotate_extrude() translate([R, 0]) circle(r = r); }
module cw_wf(f, z = 0, x = 0) translate([x, -f, z]) children();
// 沿枪轴（-Y）的圆柱 / 圆锥：从前向 f0 起，长 len
module cw_bore(f0, len, r, z, x = 0, r2 = -1, fn = 20)
    translate([x, -f0, z]) rotate([90, 0, 0]) cw_cyl(len, r, fn, r2);
// 侧视轮廓 [[f, z], ...] 沿 X 居中拉伸成厚度 w
module cw_side(w, pts, x = 0)
    translate([x, 0, 0]) rotate([90, 0, -90]) linear_extrude(height = w, center = true) polygon(pts);
// 带一个孔的侧视轮廓（镂空托、扳机护圈）
module cw_side_hole(w, outer, hole, x = 0)
    translate([x, 0, 0]) rotate([90, 0, -90]) linear_extrude(height = w, center = true)
        polygon(points = concat(outer, hole),
                paths = [[for (i = [0 : len(outer) - 1]) i], [for (i = [0 : len(hole) - 1]) len(outer) + i]]);
// 绕枪轴的旋转体：pts = [[r, f], ...]
module cw_lathe(pts, z, x = 0, fn = 24)
{
    $fn = fn;
    translate([x, 0, z]) rotate([90, 0, 0]) rotate_extrude() polygon(pts);
}
// 倒角盒（三向内缩盒的 hull），s = [宽 X, 长 f, 高 z]
module cw_cbox(s, c = 0.003)
    hull()
    {
        cube([s[0], max(s[1] - 2 * c, 0.0004), max(s[2] - 2 * c, 0.0004)], center = true);
        cube([max(s[0] - 2 * c, 0.0004), s[1], max(s[2] - 2 * c, 0.0004)], center = true);
        cube([max(s[0] - 2 * c, 0.0004), max(s[1] - 2 * c, 0.0004), s[2]], center = true);
    }
module cw_blk(f, z, s, x = 0, c = 0.003) cw_wf(f, z, x) cw_cbox(s, c);
// 侧视轮廓向内斜接偏移 c（凸 / 近凸多边形），给木托、护木做倒角用
function cw_vsum(v, i = 0) = i >= len(v) ? 0 : v[i] + cw_vsum(v, i + 1);
function cw_sarea(p) = cw_vsum([for (i = [0 : len(p) - 1]) p[i][0] * p[(i + 1) % len(p)][1] - p[(i + 1) % len(p)][0] * p[i][1]]);
function cw_inset(p, c) =
    let(n = len(p), sg = cw_sarea(p) > 0 ? 1 : -1)
    [for (i = [0 : n - 1])
        let(a = p[(i + n - 1) % n], b = p[i], d = p[(i + 1) % n],
            l1 = norm([b[0] - a[0], b[1] - a[1]]), l2 = norm([d[0] - b[0], d[1] - b[1]]),
            n1 = [-sg * (b[1] - a[1]) / l1, sg * (b[0] - a[0]) / l1],
            n2 = [-sg * (d[1] - b[1]) / l2, sg * (d[0] - b[0]) / l2],
            k = c / (1 + n1[0] * n2[0] + n1[1] * n2[1]))
        [b[0] + k * (n1[0] + n2[0]), b[1] + k * (n1[1] + n2[1])]];
// 武器倒角厚板：侧视轮廓（凸）沿 X 拉伸成 w 厚，四周倒角 c；与地面/建筑的 cw_slab(L, D, t) 分开命名。
module cw_wpn_slab(w, pts, c = 0.004, x = 0)
    hull() { cw_side(w - 2 * c, pts, x); cw_side(w, cw_inset(pts, c), x); }
// 沿 X 的横销 / 铆钉
module cw_pin(f, z, r, w, x = 0, fn = 12) cw_wf(f, z, x) rotate([0, 90, 0]) cw_cyl(w, r, fn, -1, true);
// 沿 -X（右侧）伸出的杆
module cw_rod_r(f, z, x0, len, r, fn = 12) cw_wf(f, z, x0) rotate([0, -90, 0]) cw_cyl(len, r, fn);
// 手持坐标 → 地面平躺：右侧朝上、枪口 +X；cf = 沿枪轴的居中点，lift = 左侧最大外凸
module cw_lay(cf, lift)
    translate([-cf, 0, lift]) multmatrix([[0, -1, 0, 0], [0, 0, 1, 0], [-1, 0, 0, 0], [0, 0, 0, 1]]) children();
// 弧形弹匣脊线：[[f, z, 倾角], ...]，每段长 L，倾角从 a0 起每段增加 da（向前弯）
function cw_arc(n, a0, da, L, p, i = 0) =
    i > n ? [] : concat([[p[0], p[1], a0 + da * i]],
                        cw_arc(n, a0, da, L, [p[0] + L * sin(a0 + da * i), p[1] - L * cos(a0 + da * i)], i + 1));
// 弹匣截面片：脊线点 p，前后厚 d，宽 w，厚 t
module cw_mag_slab(p, d, w, t = 0.002, c = 0.0025) cw_wf(p[0], p[1]) rotate([-p[2], 0, 0])
    hull() { cube([w - 2 * c, d, t], center = true); cube([w, d - 2 * c, t], center = true); }

// ---------------- AK-74 ----------------
// 辨识点：橙褐层压木托（两侧减重槽）+ 胶木弧形弹匣 + 圆柱制退器两侧大窗 + 三角准星座 +
// 带横筋的防尘盖 + 右侧大拨片快慢机。
function cw_AK_WOOD()  = [0.44, 0.20, 0.09];
function cw_AK_PLUM()  = [0.36, 0.12, 0.07];
function cw_AK_GRIP()  = [0.20, 0.075, 0.05];

module cw_wpn_ak_held()
{
    BZ = 0.068;
    MAG = cw_arc(7, 6, 4.6, 0.027, [0.115, 0.030]);
    // 机匣：冲压钢盒，右侧开抛壳窗，窗内露出磨亮的枪机框
    cw_m_park() difference()
    {
        cw_wpn_slab(0.034, [[-0.086, 0.031], [0.205, 0.031], [0.205, 0.084], [-0.086, 0.084]], 0.0025);
        cw_blk(0.085, 0.072, [0.012, 0.070, 0.015], -0.017, 0.001);
    }
    cw_m_bright() cw_blk(0.085, 0.072, [0.004, 0.068, 0.012], -0.0118, 0.001);
    cw_m_blued()
    {
        cw_blk(0.189, 0.058, [0.037, 0.034, 0.054], 0, 0.004);                       // 前机匣座
        for (p = [[0.008, 0.037], [0.034, 0.037], [0.060, 0.037], [-0.040, 0.040], [-0.068, 0.040],
                  [0.176, 0.040], [0.198, 0.077]])
            cw_pin(p[0], p[1], 0.0024, 0.0364);                                      // 机匣铆钉
        // 防尘盖：圆弧顶 + 4 道横筋 + 复进簧按钮
        hull()
        {
            cw_bore(-0.084, 0.232, 0.0168, 0.0815, 0, -1, 24);
            cw_blk(0.032, 0.0835, [0.035, 0.232, 0.004], 0, 0.001);
        }
        for (f = [-0.060, -0.034, -0.008, 0.018]) cw_bore(f, 0.006, 0.0177, 0.0815, 0, -1, 24);
        cw_blk(-0.090, 0.082, [0.020, 0.010, 0.010], 0, 0.002);
        // 表尺座 + 表尺板 + 缺口
        cw_side(0.026, [[0.150, 0.084], [0.206, 0.084], [0.206, 0.097], [0.168, 0.099], [0.150, 0.097]]);
        cw_side(0.017, [[0.156, 0.098], [0.202, 0.099], [0.202, 0.102], [0.156, 0.104]]);
        for (s = [-1, 1]) cw_blk(0.160, 0.1065, [0.0048, 0.006, 0.006], s * 0.0046, 0.001);
        // 枪管、导气箍、导气管
        cw_bore(0.205, 0.372, 0.0102, BZ, 0, 0.0094, 20);
        cw_bore(0.205, 0.016, 0.0145, BZ, 0, -1, 20);
        hull()
        {
            cw_bore(0.436, 0.030, 0.0128, BZ, 0, -1, 20);
            cw_bore(0.441, 0.022, 0.0112, 0.0885, 0, -1, 16);
        }
        cw_bore(0.208, 0.240, 0.0092, 0.0885, 0, -1, 16);
        // 三角准星座 + 护翼 + 刺刀座
        cw_bore(0.514, 0.044, 0.0128, BZ, 0, -1, 20);
        cw_side(0.013, [[0.517, BZ], [0.557, BZ], [0.551, 0.099], [0.531, 0.099]]);
        for (s = [-1, 1]) cw_side(0.0026, [[0.532, 0.093], [0.550, 0.093], [0.548, 0.111], [0.535, 0.111]], s * 0.0066);
        cw_blk(0.530, 0.052, [0.010, 0.024, 0.010], 0, 0.002);
        // 护木前箍、上护木钢帽
        cw_bore(0.418, 0.014, 0.0236, 0.057, 0, -1, 22);
        cw_bore(0.379, 0.010, 0.0165, 0.0895, 0, -1, 20);
        // 扳机护圈、弹匣卡笋
        cw_side(0.008, [[0.072, 0.031], [0.078, 0.031], [0.068, 0.006], [0.024, 0.003], [0.019, 0.010],
                        [0.064, 0.011]]);
        cw_blk(0.079, 0.025, [0.012, 0.007, 0.010], 0, 0.001);
        // 快慢机大拨片（右侧）+ 轴座
        cw_side(0.0022, [[-0.074, 0.074], [-0.064, 0.080], [0.070, 0.066], [0.073, 0.057], [0.063, 0.056],
                         [-0.066, 0.068]], -0.0182);
        cw_pin(-0.069, 0.073, 0.0055, 0.040);
        // 钢托底板 + 机匣尾托板
        cw_side(0.042, [[-0.357, 0.042], [-0.366, 0.041], [-0.368, -0.082], [-0.357, -0.081]]);
        cw_blk(-0.094, 0.0815, [0.022, 0.020, 0.006], 0, 0.001);
    }
    // 准星柱、扳机、拉机柄（右侧，磨亮）
    cw_m_bright()
    {
        cw_wf(0.541, 0.099) cw_cyl(0.0088, 0.0016, 8);
        cw_side(0.005, [[0.046, 0.031], [0.051, 0.031], [0.047, 0.017], [0.040, 0.011], [0.042, 0.021]]);
        cw_rod_r(0.136, 0.074, -0.016, 0.022, 0.0042);
        cw_wf(0.137, 0.074, -0.040) scale([1, 1.5, 1]) cw_sph(0.0062, 14);
    }
    // AK-74 制退器：圆柱筒 + 两侧大窗 + 前端斜切；窗内为暗腔
    cw_m_blued() difference()
    {
        union()
        {
            cw_bore(0.574, 0.088, 0.0122, BZ, 0, -1, 24);
            cw_bore(0.570, 0.009, 0.0131, BZ, 0, -1, 24);
        }
        cw_bore(0.560, 0.120, 0.0042, BZ, 0, -1, 12);
        cw_blk(0.606, BZ, [0.040, 0.020, 0.0105], 0, 0.001);
        cw_wf(0.664, BZ - 0.0115) rotate([32, 0, 0]) cube([0.040, 0.014, 0.014], center = true);
    }
    cw_m_hole() cw_bore(0.593, 0.026, 0.0058, BZ, 0, -1, 16);
    // 护木（层压木）
    cw_m_wood(cw_AK_WOOD())
    {
        difference()
        {
            hull()
            {
                cw_side(0.044, [[0.210, 0.078], [0.420, 0.078], [0.424, 0.060], [0.418, 0.046], [0.300, 0.039],
                                [0.222, 0.044], [0.210, 0.058]]);
                cw_bore(0.214, 0.205, 0.0195, 0.056, 0, -1, 20);
            }
            for (s = [-1, 1]) cw_blk(0.258, 0.059, [0.008, 0.058, 0.010], s * 0.0245, 0.002);   // 两侧指槽
        }
        hull()
        {
            cw_bore(0.212, 0.168, 0.0152, 0.0895, 0, -1, 20);
            cw_blk(0.296, 0.081, [0.032, 0.168, 0.004], 0, 0.001);
        }
        // 枪托：渐宽 hull + 两侧 AK-74 减重槽。托顶自机匣尾向后明显下斜（AK 的大托腮落差），
        // 贴腮时托腮在视线下约 4 cm，第一人称开镜不再沿整根木托看出去。
        difference()
        {
            hull()
            {
                cw_wpn_slab(0.031, [[-0.084, 0.082], [-0.200, 0.062], [-0.355, 0.040], [-0.358, -0.080],
                                [-0.300, -0.064], [-0.150, -0.010], [-0.095, 0.024], [-0.084, 0.030]], 0.005);
                cw_wpn_slab(0.041, [[-0.345, 0.042], [-0.358, 0.040], [-0.358, -0.080], [-0.345, -0.078]], 0.006);
            }
            for (s = [-1, 1])
                cw_side(0.012, [[-0.165, 0.040], [-0.335, 0.020], [-0.335, -0.002], [-0.165, 0.020]], s * 0.0205);
        }
    }
    // 胶木握把
    cw_m_bake(cw_AK_GRIP()) hull()
    {
        cw_side(0.025, [[0.018, 0.031], [-0.011, -0.066], [-0.040, -0.070], [-0.024, 0.031]]);
        cw_side(0.031, [[0.012, 0.024], [-0.010, -0.054], [-0.031, -0.057], [-0.018, 0.024]]);
    }
    // 胶木弧形弹匣 + 侧筋 + 钢挂耳 / 底板
    cw_m_bake(cw_AK_PLUM())
    {
        for (i = [0 : 6]) hull() { cw_mag_slab(MAG[i], 0.062 + 0.0014 * i, 0.025); cw_mag_slab(MAG[i + 1], 0.0634 + 0.0014 * i, 0.025); }
        for (i = [1 : 2 : 5]) cw_mag_slab(MAG[i], 0.052, 0.0274, 0.004, 0.003);
    }
    cw_m_blued()
    {
        cw_mag_slab(MAG[0], 0.064, 0.0262, 0.006);
        cw_mag_slab(MAG[7], 0.074, 0.0268, 0.006);
        cw_wf(MAG[0][0] + 0.033, MAG[0][1] - 0.004) cube([0.010, 0.008, 0.010], center = true);
    }
}

module cw_wpn_ak(seed = 0) gk_flatten() cw_lay(0.147, 0.0236) cw_wpn_ak_held();

// ---------------- SVD 德拉贡诺夫 ----------------
// 辨识点：镂空拇指孔木托 + 托腮板 + PSO-1 侧装镜（空心镜筒，第一人称开镜可从筒里看出去）+
// 开槽护木 + 五槽消焰器 + 高准星座 + 10 发直弹匣。
function cw_SVD_WOOD() = [0.42, 0.22, 0.11];
function cw_SVD_SZ() = 0.128;   // 瞄准镜光轴高

module cw_svd_scope(lens = false)
{
    SZ = cw_SVD_SZ();
    cw_m_anod()
    {
        cw_side(0.010, [[0.010, 0.052], [0.120, 0.052], [0.115, 0.112], [0.020, 0.112]], 0.0205);   // 左侧燕尾座板
        cw_blk(0.065, 0.1115, [0.032, 0.090, 0.009], 0.011, 0.003);                                 // 镜座横梁
        cw_blk(0.072, 0.064, [0.006, 0.030, 0.014], 0.0285, 0.002);                                 // 夹紧手柄
        cw_lathe([[0.0108, -0.150], [0.0168, -0.150], [0.0168, -0.110], [0.0138, -0.090], [0.0138, 0.080],
                  [0.0205, 0.112], [0.0205, 0.170], [0.0186, 0.170], [0.0186, 0.118], [0.0118, 0.090],
                  [0.0118, -0.105], [0.0108, -0.110]], SZ, 0, 28);                                  // 镜筒（空心）
        cw_wf(0.010, SZ + 0.011) cw_cyl(0.020, 0.0112, 18);                          // 高低手轮
        cw_wf(0.010, SZ, 0.011) rotate([0, 90, 0]) cw_cyl(0.020, 0.0112, 18);        // 方向手轮
        cw_wf(-0.045, SZ - 0.005, 0.011) rotate([0, 90, 0]) cw_cyl(0.012, 0.0065, 14); // 分划照明电池盖
    }
    cw_m_bright()
    {
        cw_wf(0.010, SZ + 0.031) cw_cyl(0.0015, 0.0095, 18);                         // 手轮刻度圈
        cw_wf(0.010, SZ, 0.031) rotate([0, 90, 0]) cw_cyl(0.0015, 0.0095, 18);
    }
    cw_m_rubber() cw_lathe([[0.0110, -0.188], [0.0186, -0.188], [0.0178, -0.150], [0.0150, -0.146],
                            [0.0110, -0.146]], SZ, 0, 24);                                          // 橡胶眼罩
    if (lens)
        cw_m_lens()
        {
            cw_bore(0.150, 0.003, 0.0186, SZ, 0, -1, 24);
            cw_bore(-0.140, 0.003, 0.0118, SZ, 0, -1, 20);
        }
}

module cw_wpn_svd_held(lens = false, scope = true)
{
    BZ = 0.066;
    cw_m_park() difference()
    {
        cw_side(0.032, [[-0.066, 0.032], [0.222, 0.032], [0.222, 0.082], [-0.066, 0.082]]);
        cw_blk(0.095, 0.069, [0.012, 0.072, 0.016], -0.016, 0.001);
    }
    cw_m_bright() cw_blk(0.095, 0.069, [0.004, 0.070, 0.012], -0.0108, 0.001);
    cw_m_blued()
    {
        hull()
        {
            cw_bore(-0.064, 0.240, 0.0158, 0.0795, 0, -1, 24);
            cw_blk(0.056, 0.0815, [0.033, 0.240, 0.004], 0, 0.001);
        }
        cw_blk(0.206, 0.057, [0.036, 0.036, 0.050], 0, 0.004);
        cw_side(0.024, [[0.178, 0.082], [0.222, 0.082], [0.222, 0.091], [0.182, 0.093]]);
        for (p = [[0.010, 0.037], [0.040, 0.037], [-0.040, 0.040], [0.196, 0.040]]) cw_pin(p[0], p[1], 0.0023, 0.0342);
        // 枪管 + 导气
        cw_bore(0.222, 0.560, 0.0098, BZ, 0, 0.0084, 20);
        hull()
        {
            cw_bore(0.462, 0.034, 0.0122, BZ, 0, -1, 20);
            cw_bore(0.466, 0.026, 0.0105, 0.087, 0, -1, 16);
        }
        cw_bore(0.224, 0.240, 0.0085, 0.087, 0, -1, 16);
        cw_blk(0.452, 0.066, [0.045, 0.008, 0.064], 0, 0.006);                 // 护木前箍
        // 高准星座 + 护圈
        cw_bore(0.730, 0.026, 0.0108, BZ, 0, -1, 20);
        cw_side(0.010, [[0.733, BZ], [0.754, BZ], [0.750, 0.098], [0.737, 0.098]]);
        cw_lathe([[0.0085, 0.736], [0.0106, 0.736], [0.0106, 0.751], [0.0085, 0.751]], 0.100, 0, 20);
        // 五槽消焰器
        difference()
        {
            cw_bore(0.780, 0.064, 0.0118, BZ, 0, -1, 20);
            cw_bore(0.770, 0.090, 0.0056, BZ, 0, -1, 12);
            for (a = [0 : 72 : 288])
                cw_wf(0.816, BZ) rotate([0, a, 0]) translate([0, 0, 0.009]) cube([0.0028, 0.046, 0.010], center = true);
        }
        // 10 发直弹匣：横竖加强筋 + 底板
        cw_side(0.024, [[0.100, 0.032], [0.168, 0.032], [0.160, -0.060], [0.098, -0.060]]);
        for (z = [-0.040, -0.014, 0.010]) cw_side(0.0262, [[0.103, z + 0.004], [0.163, z + 0.004], [0.163, z], [0.103, z]]);
        for (f = [0.118, 0.142]) cw_side(0.0262, [[f, 0.028], [f + 0.004, 0.028], [f + 0.003, -0.056], [f - 0.001, -0.056]]);
        cw_blk(0.129, -0.062, [0.027, 0.070, 0.006], 0, 0.0015);
        // 扳机护圈（与扳机座一体）
        cw_side_hole(0.010, [[0.026, 0.032], [0.090, 0.032], [0.086, 0.008], [0.060, 0.002], [0.022, 0.004], [0.018, 0.016]],
                     [[0.031, 0.026], [0.078, 0.026], [0.075, 0.012], [0.058, 0.008], [0.032, 0.010]]);
        // 快慢机（右侧）
        cw_side(0.0022, [[-0.058, 0.072], [-0.050, 0.078], [0.070, 0.066], [0.072, 0.058], [0.062, 0.057],
                         [-0.052, 0.067]], -0.0172);
        cw_pin(-0.054, 0.072, 0.0052, 0.038);
        // 钢托底板
        cw_side(0.036, [[-0.414, 0.066], [-0.421, 0.066], [-0.412, -0.100], [-0.405, -0.098]]);
    }
    cw_m_bright()
    {
        cw_side(0.005, [[0.048, 0.032], [0.053, 0.032], [0.050, 0.016], [0.044, 0.012], [0.045, 0.022]]);  // 扳机
        cw_rod_r(0.150, 0.071, -0.015, 0.018, 0.0045);                                                   // 拉机柄
        cw_wf(0.151, 0.071, -0.035) scale([1, 1.4, 1]) cw_sph(0.0064, 14);
        cw_wf(0.743, 0.098) cw_cyl(0.007, 0.0014, 8);                                    // 准星柱
    }
    // 开槽护木（槽内露出深色导气管腔）
    cw_m_hole() hull()
    {
        cw_bore(0.228, 0.220, 0.0160, 0.077, 0, -1, 16);
        cw_blk(0.338, 0.053, [0.031, 0.220, 0.026], 0, 0.004);
    }
    cw_m_wood(cw_SVD_WOOD())
    {
        difference()
        {
            hull()
            {
                cw_bore(0.226, 0.226, 0.0215, 0.077, 0, -1, 22);
                cw_blk(0.339, 0.052, [0.040, 0.226, 0.030], 0, 0.006);
            }
            for (i = [0 : 5]) for (s = [-1, 1])
                cw_wf(0.258 + i * 0.032, 0.072, s * 0.0215) rotate([28, 0, 0]) cube([0.012, 0.0075, 0.028], center = true);
        }
        // 镂空拇指孔枪托（握把一体）
        // 镂空拇指孔枪托：握把 / 下梁 / 托底 / 托腮梁四个倒角凸块拼成，中间自然留出拇指孔
        cw_wpn_slab(0.034, [[0.028, 0.032], [-0.004, -0.078], [-0.050, -0.086], [-0.046, -0.054], [-0.028, 0.014],
                        [-0.020, 0.032]], 0.005);
        cw_wpn_slab(0.032, [[-0.040, -0.054], [-0.044, -0.086], [-0.322, -0.079], [-0.306, -0.052]], 0.005);
        cw_wpn_slab(0.038, [[-0.334, -0.064], [-0.322, -0.080], [-0.405, -0.098], [-0.414, 0.066], [-0.320, 0.075],
                        [-0.334, 0.044]], 0.006);
        cw_wpn_slab(0.034, [[-0.066, 0.032], [-0.066, 0.082], [-0.300, 0.075], [-0.336, 0.050], [-0.300, 0.044],
                        [-0.090, 0.049]], 0.005);
    }
    // 托腮板（人造革）
    cw_m_leather([0.13, 0.09, 0.06]) hull()
    {
        cw_blk(-0.250, 0.079, [0.037, 0.120, 0.012], 0, 0.003);
        cw_blk(-0.250, 0.091, [0.026, 0.100, 0.006], 0, 0.002);
    }
    if (scope) cw_svd_scope(lens);
}

module cw_wpn_svd(seed = 0) gk_flatten() cw_lay(0.212, 0.032) cw_wpn_svd_held(lens = true);

// ---------------- 莫辛-纳甘 M91/30 ----------------
// 辨识点：通长直颈木托 + 上护木 + 两道枪箍 + 六角机匣前环 + 直拉球形枪栓柄 + 蘑菇头击针帽 +
// 护圈准星 + 托上背带孔（贯通）。
function cw_MOSIN_WOOD() = [0.38, 0.20, 0.09];

module cw_wpn_mosin_held()
{
    BZ = 0.070;
    cw_m_wood(cw_MOSIN_WOOD())
    {
        // 直握颈（比托身细）+ 机匣座
        cw_wpn_slab(0.030, [[-0.074, 0.054], [-0.035, 0.059], [-0.020, 0.064], [0.048, 0.066], [0.048, 0.022],
                        [-0.030, 0.000], [-0.074, -0.004]], 0.005);
        cw_wpn_slab(0.033, [[0.030, 0.066], [0.212, 0.067], [0.212, 0.036], [0.150, 0.030], [0.030, 0.022]], 0.004);
        // 托身（向后渐宽）+ 贯通背带孔
        difference()
        {
            hull()
            {
                cw_wpn_slab(0.030, [[-0.074, 0.054], [-0.062, 0.055], [-0.062, -0.004], [-0.074, -0.005]], 0.005);
                cw_wpn_slab(0.044, [[-0.446, 0.042], [-0.436, 0.043], [-0.436, -0.080], [-0.448, -0.082]], 0.007);
            }
            cw_blk(-0.250, -0.012, [0.060, 0.034, 0.010], 0, 0.004);
        }
        // 前托（圆底）+ 前背带孔
        difference()
        {
            hull()
            {
                cw_wpn_slab(0.034, [[0.200, 0.068], [0.708, 0.070], [0.708, 0.052], [0.600, 0.047], [0.200, 0.036]], 0.004);
                cw_bore(0.204, 0.500, 0.0148, 0.052, 0, 0.0115, 20);
            }
            cw_blk(0.565, 0.050, [0.060, 0.030, 0.008], 0, 0.003);
        }
        // 上护木（表尺前到第一道箍）
        hull()
        {
            cw_bore(0.272, 0.200, 0.0142, BZ + 0.001, 0, -1, 20);
            cw_blk(0.372, 0.070, [0.031, 0.200, 0.004], 0, 0.001);
        }
    }
    cw_m_blued()
    {
        // 圆机匣（顶部装弹口露出枪机）+ 六角前环 + 尾舌
        difference()
        {
            cw_bore(-0.028, 0.160, 0.0158, BZ, 0, -1, 24);
            cw_blk(0.050, BZ + 0.012, [0.017, 0.078, 0.012], -0.004, 0.001);
        }
        cw_bore(0.128, 0.048, 0.0188, BZ, 0, -1, 6);
        cw_blk(-0.050, 0.062, [0.016, 0.044, 0.006], 0, 0.002);
        // 枪管 + 表尺 + 护圈准星
        cw_bore(0.176, 0.646, 0.0108, BZ, 0, 0.0088, 20);
        cw_side(0.024, [[0.200, BZ], [0.272, BZ], [0.272, BZ + 0.014], [0.214, BZ + 0.019], [0.200, BZ + 0.017]]);
        cw_side(0.012, [[0.208, BZ + 0.018], [0.268, BZ + 0.015], [0.268, BZ + 0.019], [0.210, BZ + 0.025]]);
        for (s = [-1, 1]) cw_blk(0.211, BZ + 0.0275, [0.0042, 0.006, 0.006], s * 0.004, 0.001);
        cw_bore(0.792, 0.022, 0.0118, BZ, 0, -1, 18);
        cw_side(0.006, [[0.796, BZ], [0.810, BZ], [0.808, BZ + 0.026], [0.799, BZ + 0.026]]);
        cw_lathe([[0.0095, 0.794], [0.0116, 0.794], [0.0116, 0.812], [0.0095, 0.812]], BZ + 0.018, 0, 18);
        // 两道枪箍
        for (f = [0.470, 0.655])
            hull()
            {
                cw_blk(f, 0.057, [0.038, 0.011, 0.034], 0, 0.003);
                cw_bore(f - 0.0055, 0.011, 0.0156, BZ + 0.002, 0, -1, 20);
            }
        // 通条
        cw_bore(0.640, 0.160, 0.0028, BZ - 0.016, 0, -1, 8);
        cw_bore(0.800, 0.008, 0.0042, BZ - 0.016, 0, -1, 10);
        // 弹仓 + 扳机护圈
        cw_wpn_slab(0.029, [[0.042, 0.026], [0.140, 0.031], [0.128, -0.022], [0.064, -0.030], [0.046, -0.026]], 0.003);
        cw_side_hole(0.009, [[-0.032, -0.008], [0.040, 0.022], [0.042, -0.036], [0.010, -0.040], [-0.024, -0.024]],
                     [[-0.018, -0.010], [0.034, 0.012], [0.034, -0.028], [0.010, -0.031], [-0.013, -0.020]]);
        // 钢托底板
        cw_side(0.045, [[-0.447, 0.043], [-0.455, 0.042], [-0.457, -0.083], [-0.449, -0.083]]);
        // 蘑菇头击针帽（尾部）
        cw_lathe([[0, -0.121], [0.0112, -0.119], [0.0128, -0.111], [0.0110, -0.098], [0, -0.098]], BZ + 0.003, 0, 16);
    }
    cw_m_bright()
    {
        cw_bore(-0.098, 0.200, 0.0094, BZ + 0.003, 0, -1, 18);                  // 枪机体
        cw_rod_r(-0.004, BZ - 0.004, -0.008, 0.058, 0.0042);                    // 直拉柄
        cw_wf(-0.004, BZ - 0.006, -0.068) cw_sph(0.0098, 16);         // 球形柄头
        cw_side(0.005, [[0.014, 0.010], [0.019, 0.010], [0.016, -0.010], [0.010, -0.016], [0.012, -0.004]]);  // 扳机
        cw_blk(0.090, -0.029, [0.006, 0.010, 0.005], 0, 0.001);                  // 弹仓底盖卡笋
        cw_wf(0.803, BZ) cw_cyl(0.024, 0.0013, 8);               // 准星柱
    }
}

module cw_wpn_mosin(seed = 0) gk_flatten() cw_lay(0.183, 0.0226) cw_wpn_mosin_held();

// ---------------- 双管猎枪（IZh-43K 式外露击锤） ----------------
// 辨识点：并列双管 + 枪口两个膛孔 + 黄铜准星珠 + 白钢雕花机匣 + 双外露击锤 + 双扳机 +
// 半手枪握把胡桃木托（网纹）+ 白线橡胶托底。
function cw_SG_WALNUT() = [0.30, 0.16, 0.08];

module cw_wpn_shotgun_held()
{
    BZ = 0.056;
    BX = 0.0118;
    BR = 0.0118;
    cw_m_blued()
    {
        for (s = [-1, 1]) difference()
        {
            cw_bore(0.112, 0.668, BR, BZ, s * BX, BR * 0.93, 22);
            cw_bore(0.700, 0.100, 0.0093, BZ, s * BX, -1, 18);
        }
        cw_blk(0.446, BZ - 0.0085, [0.010, 0.668, 0.0075], 0, 0.001);      // 下筋
        cw_blk(0.132, BZ, [0.0475, 0.040, 0.026], 0, 0.004);               // 枪管后段（monoblock）
        cw_blk(0.150, BZ - 0.020, [0.020, 0.030, 0.012], 0, 0.002);       // 钩座
        cw_blk(0.168, 0.030, [0.040, 0.020, 0.010], 0, 0.002);            // 前托铁
        // 双外露击锤 + 击锤轴
        for (s = [-1, 1])
            cw_side(0.0055, [[0.040, 0.046], [0.050, 0.058], [0.044, 0.072], [0.026, 0.086], [0.018, 0.084],
                             [0.030, 0.068], [0.028, 0.050]], s * 0.0215);
        cw_pin(0.034, 0.050, 0.0040, 0.056);
        // 开闩扳手（向右后偏）+ 保险
        cw_wf(0.010, 0.072) rotate([0, 0, 20]) translate([0, 0.018, 0]) cube([0.010, 0.040, 0.006], center = true);
        cw_blk(-0.030, 0.0705, [0.007, 0.012, 0.004], 0, 0.001);
        // 扳机护圈
        cw_side(0.008, [[0.084, 0.014], [0.090, 0.014], [0.078, -0.017], [0.040, -0.025], [0.000, -0.020],
                        [-0.020, -0.004], [-0.014, 0.000], [0.002, -0.013], [0.040, -0.018], [0.072, -0.011]]);
    }
    cw_m_park() cw_blk(0.446, BZ + 0.0085, [0.010, 0.668, 0.0075], 0, 0.001);   // 哑光顶筋
    cw_m_hole() for (s = [-1, 1]) cw_bore(0.700, 0.010, 0.0094, BZ, s * BX, -1, 18);   // 膛内暗底
    cw_m_brass() cw_wf(0.774, BZ + 0.0128) cw_sph(0.0027, 12);               // 黄铜准星珠
    cw_m_bright() for (f = [0.036, 0.060])
        cw_side(0.0045, [[f, 0.014], [f + 0.005, 0.014], [f + 0.002, -0.004], [f - 0.004, -0.010], [f - 0.002, 0.002]]);
    // 白钢机匣：侧面压一圈边框，框内是压暗的雕花面
    cw_m_silver() difference()
    {
        cw_side(0.050, [[0.010, 0.070], [0.112, 0.070], [0.112, 0.042], [0.168, 0.040], [0.168, 0.026],
                        [0.112, 0.014], [0.030, 0.012], [0.010, 0.026]]);
        for (s = [-1, 1]) cw_side(0.004, [[0.030, 0.022], [0.100, 0.022], [0.100, 0.060], [0.030, 0.060]], s * 0.0262);
    }
    cw_m_etch() for (s = [-1, 1])
        cw_side(0.0012, [[0.032, 0.024], [0.098, 0.024], [0.098, 0.058], [0.032, 0.058]], s * 0.0244);
    cw_m_wood(cw_SG_WALNUT())
    {
        // 半手枪握把段（凹形轮廓直接拉伸）。托颈从机匣顶向托腮下斜：托腮低于枪管顶筋约 2.5 cm、
        // 托踵约 5 cm，贴腮时视线沿顶筋看准星珠，而不是沿整根木托看出去。
        cw_wpn_slab(0.040, [[0.012, 0.070], [-0.030, 0.062], [-0.070, 0.050], [-0.110, 0.044], [-0.110, -0.054], [-0.075, -0.060],
                        [-0.042, -0.064], [-0.022, -0.050], [-0.006, -0.020], [0.004, 0.004], [0.012, 0.012]], 0.006);
        // 托身（hull 渐宽）
        hull()
        {
            cw_wpn_slab(0.040, [[-0.108, 0.044], [-0.100, 0.0445], [-0.100, -0.054], [-0.108, -0.053]], 0.006);
            cw_wpn_slab(0.046, [[-0.400, 0.018], [-0.392, 0.019], [-0.392, -0.110], [-0.400, -0.112]], 0.007);
        }
        // 前托（splinter）
        hull()
        {
            cw_wpn_slab(0.046, [[0.160, 0.046], [0.368, 0.046], [0.374, 0.040], [0.366, 0.030], [0.160, 0.028]], 0.005);
            cw_bore(0.162, 0.200, 0.020, 0.040, 0, 0.018, 20);
        }
    }
    cw_m_check([0.17, 0.085, 0.045]) for (s = [-1, 1])
        cw_side(0.0012, [[-0.014, 0.026], [-0.050, 0.036], [-0.066, -0.028], [-0.030, -0.042]], s * 0.0201);   // 握把网纹
    cw_m_paint([0.82, 0.80, 0.74]) cw_wpn_slab(0.0462, [[-0.400, 0.018], [-0.403, 0.018], [-0.403, -0.112], [-0.400, -0.112]], 0.0065);
    cw_m_rubber([0.07, 0.04, 0.03]) cw_wpn_slab(0.0466, [[-0.403, 0.0185], [-0.418, 0.018], [-0.420, -0.110], [-0.403, -0.1125]], 0.0068);
}

module cw_wpn_shotgun(seed = 0) gk_flatten() cw_lay(0.18, 0.0236) cw_wpn_shotgun_held();

// ---------------- 马卡洛夫 PM ----------------
// 按真实尺寸（m）建模再整体放大 1.5 倍以配合方块角色的手。辨识点：圆鼻套筒 + 尾部竖防滑纹 +
// 左侧保险拨片（红点）+ 外露击锤 + 大圆扳机护圈 + 褐色胶木一体握片（五角星）+ 尾部挂绳环。
function cw_PM_SCALE() = 1.5;
function cw_PM_GRIP() = [0.24, 0.11, 0.055];

module cw_pm_star(r = 0.0065)
    polygon([for (i = [0 : 9]) let(a = 90 + i * 36, rr = i % 2 == 0 ? r : r * 0.42) [rr * cos(a), rr * sin(a)]]);

module cw_wpn_pistol_held()
{
    scale(cw_PM_SCALE())
    {
        // 套筒：圆顶 + 圆鼻，尾部竖防滑纹，右侧抛壳窗，枪口钻孔
        cw_m_blued() difference()
        {
            hull()
            {
                cw_bore(-0.040, 0.150, 0.0128, 0.0345, 0, -1, 24);
                cw_wf(0.110, 0.0345) cw_sph(0.0128, 24);
                cw_side(0.029, [[-0.040, 0.021], [0.110, 0.021], [0.110, 0.033], [-0.040, 0.033]]);
                cw_wf(0.112, 0.027) rotate([0, 90, 0]) cw_cyl(0.029, 0.0062, 16, -1, true);
            }
            for (i = [0 : 9]) for (s = [-1, 1]) cw_blk(-0.036 + i * 0.0026, 0.034, [0.003, 0.0011, 0.020], s * 0.0148, 0);
            cw_blk(0.024, 0.038, [0.010, 0.028, 0.013], -0.0145, 0.001);
            cw_bore(0.100, 0.040, 0.0062, 0.0345, 0, -1, 16);
        }
        cw_m_bright() cw_blk(0.024, 0.038, [0.004, 0.026, 0.011], -0.0105, 0.001);   // 抛壳窗内枪管节套
        cw_m_blued() difference()
        {
            cw_bore(0.090, 0.028, 0.0060, 0.0345, 0, -1, 16);
            cw_bore(0.085, 0.040, 0.0046, 0.0345, 0, -1, 14);
        }
        cw_m_hole() cw_bore(0.098, 0.004, 0.0047, 0.0345, 0, -1, 14);
        cw_m_blued()
        {
            // 准星 + 照门
            cw_side(0.003, [[0.106, 0.046], [0.116, 0.046], [0.115, 0.0525], [0.109, 0.0525]]);
            for (s = [-1, 1]) cw_blk(-0.031, 0.0495, [0.0045, 0.009, 0.007], s * 0.004, 0.001);
            // 套座（枪身）+ 大圆扳机护圈
            cw_side(0.026, [[-0.042, 0.022], [0.112, 0.022], [0.112, 0.012], [0.064, 0.010], [0.004, 0.012], [-0.042, 0.014]]);
            cw_side_hole(0.008, [[0.064, 0.014], [0.066, 0.000], [0.058, -0.014], [0.040, -0.021], [0.012, -0.020],
                                 [0.004, -0.010], [0.004, 0.014]],
                         [[0.057, 0.008], [0.058, -0.002], [0.052, -0.011], [0.040, -0.015], [0.016, -0.014],
                          [0.011, -0.006], [0.011, 0.008]]);
            // 握把骨架（后背条外露）
            cw_side(0.022, [[0.004, 0.014], [-0.012, -0.068], [-0.042, -0.072], [-0.044, 0.022]]);
            // 外露击锤：圆形击锤尾
            hull()
            {
                cw_side(0.008, [[-0.040, 0.026], [-0.040, 0.038], [-0.045, 0.038], [-0.045, 0.026]]);
                cw_pin(-0.0465, 0.0445, 0.0052, 0.008, 0, 16);
            }
            // 左侧保险拨片 + 空仓挂机柄
            cw_side(0.0022, [[-0.037, 0.039], [-0.016, 0.044], [-0.014, 0.040], [-0.035, 0.035]], 0.0156);
            cw_pin(-0.034, 0.037, 0.0035, 0.0322);
            cw_side(0.0022, [[0.034, 0.019], [0.056, 0.020], [0.056, 0.024], [0.036, 0.025]], 0.0141);
            // 弹匣底板
            cw_blk(-0.027, -0.0725, [0.025, 0.031, 0.005], 0, 0.0015);
        }
        cw_m_paint([0.72, 0.06, 0.04]) cw_pin(-0.026, 0.031, 0.0018, 0.0012, 0.0150);  // 保险红点（左侧）
        cw_m_bright() cw_side(0.0045, [[0.030, 0.010], [0.035, 0.010], [0.033, -0.004], [0.027, -0.010], [0.029, 0.000]]);  // 扳机
        // 一体握片：胶木，两侧五角星 + 网纹区
        cw_m_bake(cw_PM_GRIP()) hull()
        {
            cw_side(0.025, [[0.000, 0.012], [-0.013, -0.068], [-0.041, -0.070], [-0.039, 0.016]]);
            cw_side(0.030, [[-0.004, 0.006], [-0.015, -0.062], [-0.037, -0.064], [-0.035, 0.010]]);
        }
        cw_m_bake([0.17, 0.075, 0.04]) for (s = [-1, 1])
            cw_wf(-0.017, -0.004, s * 0.0152) rotate([0, 90, 0]) cw_cyl(0.0012, 0.0085, 20, -1, true);
        cw_m_check([0.20, 0.09, 0.045]) for (s = [-1, 1])
            cw_side(0.0012, [[-0.012, -0.018], [-0.034, -0.018], [-0.038, -0.058], [-0.018, -0.058]], s * 0.0150);
        cw_m_bake([0.40, 0.20, 0.10]) for (s = [-1, 1])
            cw_wf(-0.017, -0.004, s * 0.0160) rotate([0, s * 90, 0]) rotate([0, 0, s * 90])
                linear_extrude(height = 0.0012, center = true) cw_pm_star();
        // 挂绳环
        cw_m_bright() cw_wf(-0.043, -0.066) rotate([0, 90, 0]) cw_torus(0.0055, 0.0013, 12);
    }
}

module cw_wpn_pistol(seed = 0) gk_flatten() cw_lay(0.051, 0.0255) cw_wpn_pistol_held();

// ---------------- RPG-7（地图道具） ----------------
function cw_RPG_WOOD() = [0.40, 0.21, 0.10];

module cw_wpn_rpg_held()
{
    BZ = 0.060;
    cw_m_blued()
    {
        cw_bore(-0.620, 0.950, 0.0205, BZ, 0, -1, 24);                                       // 发射筒
        cw_lathe([[0.0175, -0.620], [0.0215, -0.620], [0.0425, -0.745], [0.0395, -0.745],
                  [0.0175, -0.640]], BZ, 0, 28);                                              // 喇叭形尾喷管
        cw_bore(-0.630, 0.014, 0.0235, BZ, 0, -1, 24);
        cw_bore(0.320, 0.012, 0.0235, BZ, 0, -1, 24);                                        // 筒口箍
        // 机械瞄具
        cw_side(0.006, [[0.200, BZ + 0.018], [0.214, BZ + 0.018], [0.211, BZ + 0.040], [0.203, BZ + 0.040]]);
        cw_side(0.014, [[-0.010, BZ + 0.018], [0.012, BZ + 0.018], [0.010, BZ + 0.034], [-0.008, BZ + 0.034]]);
        // 扳机护圈
        cw_side(0.008, [[0.058, 0.040], [0.064, 0.040], [0.056, 0.008], [0.022, 0.004], [0.018, 0.010], [0.052, 0.013]]);
    }
    cw_m_bright() cw_side(0.005, [[0.034, 0.040], [0.039, 0.040], [0.036, 0.022], [0.030, 0.016], [0.032, 0.026]]);
    // 木隔热护套 + 铜螺钉
    cw_m_wood(cw_RPG_WOOD()) cw_bore(-0.300, 0.290, 0.0275, BZ, 0, -1, 24);
    cw_m_brass() for (f = [-0.27, -0.04]) for (s = [-1, 1]) cw_pin(f, BZ, 0.0032, 0.0565);
    // 握把 ×2
    cw_m_bake([0.18, 0.09, 0.05])
    {
        hull()
        {
            cw_side(0.026, [[0.016, 0.040], [-0.012, -0.060], [-0.040, -0.064], [-0.026, 0.040]]);
            cw_side(0.031, [[0.010, 0.032], [-0.010, -0.050], [-0.032, -0.053], [-0.020, 0.032]]);
        }
        hull()
        {
            cw_side(0.026, [[-0.180, 0.040], [-0.200, -0.040], [-0.226, -0.042], [-0.214, 0.040]]);
            cw_side(0.031, [[-0.186, 0.032], [-0.202, -0.032], [-0.222, -0.034], [-0.210, 0.032]]);
        }
    }
    // PGO-7 光学镜（左侧）
    cw_m_anod()
    {
        cw_blk(-0.020, BZ + 0.012, [0.022, 0.070, 0.040], 0.036, 0.004);
        cw_bore(-0.080, 0.130, 0.0110, BZ + 0.024, 0.046, -1, 20);
        cw_bore(0.040, 0.020, 0.0135, BZ + 0.024, 0.046, -1, 20);
    }
    cw_m_rubber() cw_lathe([[0.0105, -0.115], [0.0160, -0.115], [0.0140, -0.080], [0.0105, -0.080]], BZ + 0.024, 0.046, 20);
    // PG-7V 火箭弹：橄榄绿战斗部 + 灰色发动机段 + 铝引信尖
    cw_m_paint([0.27, 0.06, 0.03]) cw_bore(0.300, 0.032, 0.0165, BZ, 0, 0.0175, 20);
    cw_m_paint([0.30, 0.34, 0.20]) cw_lathe([[0, 0.330], [0.0205, 0.330], [0.0420, 0.392], [0.0425, 0.432],
                                             [0.0200, 0.525], [0.0085, 0.575], [0, 0.578]], BZ, 0, 28);
    cw_m_bright() cw_lathe([[0, 0.570], [0.0092, 0.570], [0.0040, 0.598], [0, 0.600]], BZ, 0, 16);
}

module cw_wpn_rpg(seed = 0) gk_flatten() cw_lay(-0.072, 0.058) cw_wpn_rpg_held();

// 开盖武器箱（内置步枪 loot）
module cw_wpn_crate(seed = 0)
{
    color(cw_OLIVE()) difference()
    {
        translate([0, 0, 0.26]) cw_boxc([1.4, 0.7, 0.52]);
        translate([0, 0, 0.36]) cw_boxc([1.24, 0.54, 0.5]);
    }
    color(cw_OLIVED()) translate([0, 0.52, 0.12]) rotate([70, 0, 0]) cw_boxc([1.44, 0.74, 0.05]);   // 掀开盖靠边
    color(cw_DARKC()) translate([0, 0, 0.14]) cw_boxc([1.22, 0.52, 0.06]);
    translate([-0.05, 0.12, 0.17]) rotate([0, 0, 4]) cw_wpn_ak(seed);
    translate([0.05, -0.12, 0.17]) rotate([0, 0, -3]) cw_wpn_ak(seed + 3);
    color(cw_MARKW()) translate([0, -0.36, 0.3]) cw_boxc([0.5, 0.02, 0.14]);
}

// ---------------- 近战：猎刀 ----------------
// 夹尖刃 + 磨亮刃口斜面 + 血槽 + 黄铜护手 / 尾锤 + 叠层皮革柄（红色纤维垫片）。刃口朝 -Z。
function cw_KNIFE_LEATHER(i) = [[0.36, 0.22, 0.12], [0.29, 0.17, 0.09]][i % 2];

module cw_wpn_knife_held()
{
    // 刃身平面（缎面钢）+ 血槽
    gk_material([0.52, 0.53, 0.55], roughness = 0.3, metalness = 1.0) difference()
    {
        cw_side(0.0048, [[0.008, 0.013], [0.150, 0.012], [0.205, 0.005], [0.205, 0.001], [0.030, -0.007], [0.008, -0.007]]);
        for (s = [-1, 1]) cw_side(0.0012, [[0.032, 0.0065], [0.128, 0.0065], [0.128, 0.0030], [0.032, 0.0030]], s * 0.0024);
    }
    // 刃口斜面（磨亮）
    cw_m_bright() hull()
    {
        cw_side(0.0044, [[0.012, -0.005], [0.205, 0.003], [0.205, 0.001], [0.012, -0.007]]);
        cw_side(0.0006, [[0.236, 0.000], [0.215, -0.009], [0.175, -0.017], [0.060, -0.020], [0.010, -0.017], [0.010, -0.007]]);
    }
    // 黄铜护手 + 尾锤
    cw_m_brass()
    {
        hull()
        {
            cw_bore(-0.004, 0.009, 0.0062, 0.017, 0, -1, 16);
            cw_bore(-0.004, 0.009, 0.0068, -0.025, 0, -1, 16);
        }
        cw_wf(-0.120, -0.002) scale([1.0, 0.75, 1.25]) cw_sph(0.0112, 18);
    }
    // 叠层皮革柄（中段鼓起）+ 两端红色纤维垫片
    for (i = [0 : 15])
        cw_m_leather(cw_KNIFE_LEATHER(i))
            cw_wf(-0.008 - i * 0.0066, -0.002) rotate([90, 0, 0]) scale([1, 1.25, 1])
                cw_cyl(0.0066, 0.0094 + 0.0016 * sin(180 * (i + 0.5) / 16), 20);
    cw_m_paint([0.55, 0.08, 0.05]) for (f = [-0.006, -0.1135])
        cw_wf(f, -0.002) rotate([90, 0, 0]) scale([1, 1.25, 1]) cw_cyl(0.0018, 0.0098, 20);
}

module cw_item_knife(seed = 0) gk_flatten() cw_lay(0.052, 0.0118) cw_wpn_knife_held();

// ---------------- 近战：撬棍（鹅颈撬棍） ----------------
// 红漆六角钢杆 + 鹅颈弯钩（向 +Z）+ 分叉起钉爪 + 扁凿头；两端露出磨亮的钢。
function cw_BAR_RED() = [0.60, 0.07, 0.04];

module cw_bar_sec(a, gr, r) cw_wf(0.440 + gr * sin(a), gr - gr * cos(a)) rotate([-a, 0, 0]) rotate([90, 0, 0])
    cw_cyl(0.002, r, 6, -1, true);

module cw_wpn_crowbar_held()
{
    R = 0.0115;
    GR = 0.058;
    cw_m_paint(cw_BAR_RED())
    {
        cw_bore(-0.240, 0.680, R, 0, 0, -1, 6);
        for (a = [0 : 15 : 150]) hull() { cw_bar_sec(a, GR, R); cw_bar_sec(a + 15, GR, R); }
    }
    cw_m_park()
    {
        // 起钉爪：从弯钩末端压扁成宽爪，中间 V 形分叉
        difference()
        {
            hull()
            {
                cw_bar_sec(165, GR, R);
                cw_wf(0.440 + GR * sin(165) - 0.052 * cos(15), GR - GR * cos(165) + 0.052 * sin(15)) rotate([15, 0, 0])
                    cube([0.026, 0.002, 0.004], center = true);
            }
            cw_wf(0.440 + GR * sin(165) - 0.050 * cos(15), GR - GR * cos(165) + 0.050 * sin(15)) rotate([15, 0, 0])
                rotate([0, 0, 0]) linear_extrude(height = 0.03, center = true)
                    polygon([[-0.0035, -0.001], [0.0035, -0.001], [0.0008, -0.036], [-0.0008, -0.036]]);
        }
        // 扁凿头（尾端略上翘）
        hull()
        {
            cw_bore(-0.250, 0.010, R, 0, 0, -1, 6);
            cw_wf(-0.300, 0.008) rotate([-12, 0, 0]) cube([0.024, 0.002, 0.0035], center = true);
        }
    }
    cw_m_bright()
    {
        cw_wf(0.440 + GR * sin(165) - 0.053 * cos(15), GR - GR * cos(165) + 0.053 * sin(15)) rotate([15, 0, 0])
            cube([0.0262, 0.0022, 0.0028], center = true);
        cw_wf(-0.3012, 0.0083) rotate([-12, 0, 0]) cube([0.0242, 0.0022, 0.0022], center = true);
    }
}

module cw_item_crowbar(seed = 0) gk_flatten() cw_lay(0.11, 0.0145) cw_wpn_crowbar_held();

// ---------------- 近战：消防斧（尖镐背） ----------------
// 红漆斧头 + 磨亮弧形刃口（朝 -Z，劈砍时刃口领先）+ 尖镐背 + 白蜡木椭圆斧柄（尾端鼓包）。
function cw_AXE_RED() = [0.62, 0.06, 0.04];
function cw_AXE_ASH() = [0.64, 0.46, 0.26];

module cw_axe_disk(h) cw_wf(h[0], 0) rotate([90, 0, 0]) scale([h[1], h[2], 1]) cw_cyl(0.002, 0.5, 20, -1, true);

module cw_wpn_axe_held()
{
    HAFT = [[-0.276, 0.022, 0.028], [-0.262, 0.026, 0.040], [-0.235, 0.023, 0.031], [0.300, 0.023, 0.031],
            [0.470, 0.024, 0.036], [0.566, 0.024, 0.036]];
    cw_m_wood(cw_AXE_ASH()) for (i = [0 : len(HAFT) - 2]) hull() { cw_axe_disk(HAFT[i]); cw_axe_disk(HAFT[i + 1]); }
    cw_m_blued() cw_blk(0.566, 0, [0.004, 0.004, 0.030], 0, 0.001);   // 柄顶铁楔
    cw_m_paint(cw_AXE_RED())
    {
        cw_blk(0.515, 0, [0.026, 0.088, 0.052], 0, 0.005);          // 斧身（包住柄眼）
        hull()                                                      // 斧刃（漆面部分）
        {
            cw_blk(0.515, -0.024, [0.024, 0.080, 0.004], 0, 0.001);
            cw_blk(0.520, -0.094, [0.010, 0.150, 0.004], 0, 0.001);
        }
        for (i = [0 : 2]) hull()                                    // 尖镐背：方截面渐细，略向柄尾回弯
        {
            cw_blk(0.515 - [0, 0.004, 0.012][i], 0.024 + [0, 0.045, 0.090][i],
                   [[0.020, 0.030, 0.004], [0.015, 0.022, 0.004], [0.010, 0.014, 0.004]][i], 0, 0.001);
            cw_blk(0.515 - [0.004, 0.012, 0.024][i], 0.024 + [0.045, 0.090, 0.120][i],
                   [[0.015, 0.022, 0.004], [0.010, 0.014, 0.004], [0.004, 0.005, 0.003]][i], 0, 0.001);
        }
    }
    cw_m_bright()
    {
        hull()                                                      // 磨亮的弧形刃口斜面
        {
            cw_blk(0.520, -0.094, [0.0096, 0.150, 0.004], 0, 0.001);
            for (p = [[0.440, -0.112], [0.470, -0.126], [0.520, -0.132], [0.570, -0.126], [0.600, -0.110]])
                cw_wf(p[0], p[1]) cube([0.0012, 0.006, 0.003], center = true);
        }
        hull()                                                      // 镐尖磨亮
        {
            cw_blk(0.495, 0.140, [0.0052, 0.0062, 0.002], 0, 0.0005);
            cw_wf(0.488, 0.156) cube([0.0012, 0.0012, 0.001], center = true);
        }
    }
}

module cw_item_axe(seed = 0) gk_flatten() cw_lay(0.162, 0.0142) cw_wpn_axe_held();


// ================= 物资（小件 loot，底面 z=0） =================

// 罐头
module cw_item_can(seed = 0)
{
    cw_m_galv(cw_METALC()) cylinder(h = 0.13, r = 0.05, $fn = 7);
    cw_m_matte(cw_goods_c(seed)) translate([0, 0, 0.03]) cylinder(h = 0.07, r = 0.052, $fn = 7);
}

// 油壶（jerry can）
module cw_item_jerrycan(seed = 0)
{
    c = cw_rnd(seed, 2) == 0 ? cw_OLIVE() : cw_REDC();
    cw_m_matte(c) translate([0, 0, 0.24]) cw_boxc([0.35, 0.16, 0.48]);
    cw_m_matte([c[0] * 0.8, c[1] * 0.8, c[2] * 0.8])
    {
        translate([0, 0, 0.45]) rotate([0, 45, 0]) cw_boxc([0.1, 0.17, 0.1]);
        translate([-0.09, 0, 0.51]) cw_boxc([0.1, 0.05, 0.05]);
    }
    cw_m_steel(cw_METALD()) translate([0.13, 0, 0.5]) cylinder(h = 0.05, r = 0.035, $fn = 6);
}

// 医疗箱（白盒红十字）
module cw_item_medkit()
{
    cw_m_matte(cw_MARKW()) translate([0, 0, 0.11]) cw_boxc([0.36, 0.26, 0.22]);
    cw_m_matte(cw_REDC())
    {
        translate([0, -0.131, 0.12]) cw_boxc([0.05, 0.004, 0.14]);
        translate([0, -0.131, 0.12]) cw_boxc([0.14, 0.004, 0.05]);
        translate([0, 0, 0.221]) cw_boxc([0.05, 0.14, 0.004]);
        translate([0, 0, 0.221]) cw_boxc([0.14, 0.05, 0.004]);
    }
    cw_m_steel(cw_METALD()) translate([0, -0.12, 0.2]) cw_boxc([0.08, 0.03, 0.03]);
}

// 弹药盒（军绿提箱）
module cw_item_ammobox(seed = 0)
{
    cw_m_matte(cw_OLIVE()) translate([0, 0, 0.13]) cw_boxc([0.32, 0.16, 0.26]);
    cw_m_matte(cw_OLIVED()) translate([0, 0, 0.25]) cw_boxc([0.34, 0.18, 0.05]);
    cw_m_matte(cw_MARKW()) translate([0, -0.081, 0.12]) cw_boxc([0.18, 0.004, 0.07]);
    cw_m_steel(cw_METALD()) translate([0, 0, 0.29]) cw_boxc([0.1, 0.04, 0.03]);
}

// 行军背包（靠放）
module cw_item_backpack(seed = 0)
{
    c = cw_rnd(seed, 2) == 0 ? cw_KHAKI() : cw_OLIVE();
    rotate([0, 0, cw_rnd(seed, 90)]) rotate([-16, 0, 0])
    {
        cw_m_fabric(c) translate([0, 0, 0.3]) scale([1, 0.6, 1.15]) sphere(r = 0.26, $fn = 7);
        cw_m_fabric([c[0] * 0.8, c[1] * 0.8, c[2] * 0.8]) translate([0, -0.1, 0.52]) scale([0.8, 0.5, 0.5]) sphere(r = 0.2, $fn = 6);
        cw_m_fabric(cw_WOODD()) for (sx = [-1, 1]) translate([sx * 0.12, 0.15, 0.3]) cw_boxc([0.05, 0.03, 0.4]);
    }
}

// 野战电台（旋钮 + 鞭天线）
module cw_item_radio(seed = 0)
{
    cw_m_matte(cw_OLIVED()) translate([0, 0, 0.16]) cw_boxc([0.38, 0.2, 0.32]);
    cw_m_steel(cw_METALD()) for (i = [0 : 2]) translate([-0.1 + i * 0.1, -0.11, 0.22]) cylinder(h = 0.025, r = 0.025, $fn = 6);
    cw_m_matte(cw_DARKC()) translate([0.05, -0.11, 0.1]) cw_boxc([0.14, 0.01, 0.06]);
    cw_m_galv(cw_METALC()) translate([0.14, 0.06, 0.32]) cylinder(h = 0.9, r = 0.012, $fn = 5);
}

// 煤油马灯（暖光罩）
module cw_item_lantern(seed = 0)
{
    cw_m_steel(cw_METALD()) cylinder(h = 0.05, r = 0.08, $fn = 7);
    color([0.90, 0.80, 0.52, 0.8]) translate([0, 0, 0.05]) cylinder(h = 0.14, r1 = 0.06, r2 = 0.045, $fn = 7);
    cw_m_steel(cw_METALD())
    {
        translate([0, 0, 0.19]) cylinder(h = 0.04, r = 0.06, $fn = 7);
        translate([0, 0, 0.24]) rotate([0, 90, 0]) cylinder(h = 0.02, r = 0.06, $fn = 6);
    }
}

// 铺盖卷（行军床垫）
module cw_item_bedroll(seed = 0)
{
    c = cw_rnd(seed, 2) == 0 ? cw_OLIVE() : [0.44, 0.36, 0.28];
    cw_m_fabric(c) translate([-0.35, 0, 0.12]) rotate([0, 90, 0]) cylinder(h = 0.7, r = 0.12, $fn = 8);
    cw_m_fabric([c[0] * 0.8, c[1] * 0.8, c[2] * 0.8]) for (x = [-0.2, 0.15])
        translate([x, 0, 0.12]) rotate([0, 90, 0]) cylinder(h = 0.05, r = 0.125, $fn = 8);
}

// 钢盔
module cw_item_helmet(seed = 0)
{
    cw_m_matte(cw_rnd(seed, 2) == 0 ? cw_OLIVE() : cw_OLIVED()) difference()
    {
        scale([1.15, 1, 0.8]) sphere(r = 0.15, $fn = 8);
        translate([0, 0, -0.2]) cw_boxc([0.5, 0.5, 0.4]);
    }
    cw_m_matte(cw_OLIVED()) cylinder(h = 0.02, r = 0.16, $fn = 8);
}

// 救援物资箱（木箱红十字，空投样式）
module cw_item_crate_supply(seed = 0)
{
    cw_m_matte([0.48, 0.38, 0.24]) translate([0, 0, 0.3]) cw_boxc([0.8, 0.8, 0.6]);
    cw_m_matte(cw_WOODD())
    {
        for (sz = [0.06, 0.55]) translate([0, 0, sz]) cw_boxc([0.84, 0.84, 0.09]);
        for (a = [45, -45]) translate([0, -0.41, 0.3]) rotate([0, a, 0]) cw_boxc([0.85, 0.02, 0.1]);
    }
    cw_m_matte(cw_MARKW()) translate([0, -0.42, 0.32]) cw_boxc([0.3, 0.02, 0.3]);
    cw_m_matte(cw_REDC())
    {
        translate([0, -0.43, 0.32]) cw_boxc([0.07, 0.02, 0.24]);
        translate([0, -0.43, 0.32]) cw_boxc([0.24, 0.02, 0.07]);
    }
}


// =====================================================================================
// ================= 扩展：NextDayz 末日生存（功能物资 / 室内家具 / 末日痕迹 / 新建筑） =================
// =====================================================================================
// 物资件同时是 loot 目录 characters/nextdayz_loot_items.scad 的模型来源：改这里两处一起变。

// ================= 扩展物资（底面 z=0，小件平放） =================

// 塑料水瓶（直立；full=false 为空瓶）
module cw_item_water_bottle(seed = 0, full = true)
{
    body = full ? [0.26, 0.46, 0.62] : [0.60, 0.64, 0.64];
    cw_m_matte(body)
    {
        cylinder(h = 0.2, r = 0.042, $fn = 14);
        translate([0, 0, 0.2]) cylinder(h = 0.045, r1 = 0.042, r2 = 0.017, $fn = 14);
    }
    cw_m_matte([0.15, 0.34, 0.66]) translate([0, 0, 0.245]) cylinder(h = 0.024, r = 0.019, $fn = 10);
    cw_m_matte(cw_MARKW()) translate([0, 0, 0.07]) cylinder(h = 0.075, r = 0.0435, $fn = 14);
    cw_m_matte(cw_SIGNB()) translate([0, 0, 0.095]) cylinder(h = 0.025, r = 0.0445, $fn = 14);
}

// 汽水罐
module cw_item_soda(seed = 0)
{
    cw_m_matte(cw_goods_c(seed + 3)) cylinder(h = 0.115, r = 0.033, $fn = 14);
    cw_m_galv(cw_METALC())
    {
        translate([0, 0, 0.115]) cylinder(h = 0.008, r1 = 0.033, r2 = 0.026, $fn = 14);
        cylinder(h = 0.006, r = 0.03, $fn = 14);
    }
    cw_m_matte(cw_MARKW()) translate([0, 0, 0.045]) cylinder(h = 0.025, r = 0.0335, $fn = 14);
}

// 金枪鱼扁罐
module cw_item_tuna(seed = 0)
{
    cw_m_galv(cw_METALC()) cylinder(h = 0.036, r = 0.045, $fn = 16);
    cw_m_matte(cw_rnd(seed, 2) == 0 ? cw_SIGNB() : [0.70, 0.52, 0.16]) translate([0, 0, 0.008]) cylinder(h = 0.02, r = 0.0455, $fn = 16);
    cw_m_steel(cw_METALD()) translate([0.026, 0, 0.037]) cw_boxc([0.02, 0.012, 0.004]);
}

// 苏打饼干盒
module cw_item_crackers(seed = 0)
{
    cw_m_matte([0.72, 0.58, 0.22]) translate([0, 0, 0.06]) cw_boxc([0.18, 0.06, 0.12]);
    cw_m_matte(cw_REDC()) translate([0, -0.031, 0.075]) cw_boxc([0.12, 0.004, 0.05]);
    cw_m_matte(cw_MARKW()) translate([0, -0.032, 0.03]) cw_boxc([0.14, 0.004, 0.018]);
}

// 苹果
module cw_item_apple(seed = 0)
{
    cw_m_matte(cw_rnd(seed, 3) == 0 ? [0.50, 0.62, 0.20] : [0.62, 0.14, 0.10])
        translate([0, 0, 0.038]) scale([1, 1, 0.9]) sphere(r = 0.042, $fn = 12);
    cw_m_matte(cw_WOODD()) translate([0, 0, 0.07]) rotate([12, 0, 0]) cylinder(h = 0.022, r = 0.004, $fn = 5);
    cw_m_matte(cw_LEAFC()) translate([0.01, 0, 0.088]) rotate([0, 50, 0]) scale([1, 0.5, 0.15]) sphere(r = 0.014, $fn = 6);
}

// 巧克力条
module cw_item_chocolate(seed = 0)
{
    cw_m_matte([0.30, 0.16, 0.10]) translate([0, 0, 0.008]) cw_boxc([0.15, 0.06, 0.016]);
    cw_m_matte(cw_rnd(seed, 2) == 0 ? cw_REDC() : cw_SIGNB()) translate([0.02, 0, 0.009]) cw_boxc([0.115, 0.062, 0.018]);
    cw_m_matte(cw_MARKW()) translate([0.02, 0, 0.0185]) cw_boxc([0.06, 0.03, 0.002]);
}

// 绷带卷（卧放）
module cw_item_bandage(seed = 0)
{
    cw_m_fabric(cw_MARKW()) translate([0, 0, 0.034]) rotate([90, 0, 0]) cylinder(h = 0.075, r = 0.034, center = true, $fn = 14);
    cw_m_fabric([0.82, 0.80, 0.74]) translate([0, 0, 0.034]) rotate([90, 0, 0]) cylinder(h = 0.077, r = 0.012, center = true, $fn = 10);
    cw_m_fabric(cw_MARKW()) translate([0.06, 0, 0.003]) cw_boxc([0.08, 0.07, 0.004]);
}

// 碎布堆
module cw_item_rags(seed = 0)
{
    for (i = [0 : 2])
        cw_m_fabric([[0.52, 0.48, 0.42], [0.36, 0.40, 0.44], [0.48, 0.30, 0.26]][(seed + i) % 3])
            translate([cw_rndr(seed + i * 7, -0.04, 0.04), cw_rndr(seed + i * 11, -0.03, 0.03), 0.01 + i * 0.012])
                rotate([0, 0, cw_rnd(seed + i * 5, 90)]) cw_boxc([0.16, 0.11, 0.012]);
}

// 止痛药瓶
module cw_item_painkillers(seed = 0)
{
    cw_m_matte(cw_MARKW()) cylinder(h = 0.07, r = 0.022, $fn = 12);
    cw_m_matte(cw_REDC()) translate([0, 0, 0.07]) cylinder(h = 0.018, r = 0.024, $fn = 12);
    cw_m_matte(cw_SIGNB()) translate([0, 0, 0.022]) cylinder(h = 0.03, r = 0.0225, $fn = 12);
}

// 开罐器
module cw_item_can_opener(seed = 0)
{
    cw_m_galv(cw_METALC())
    {
        translate([0, 0, 0.006]) cw_boxc([0.14, 0.018, 0.01]);
        translate([0.075, 0, 0.012]) cylinder(h = 0.008, r = 0.018, $fn = 10);
    }
    cw_m_matte(cw_REDC()) translate([-0.045, 0, 0.007]) cw_boxc([0.07, 0.024, 0.014]);
}

// 军用指南针（开盖）
module cw_item_compass(seed = 0)
{
    cw_m_matte(cw_OLIVED()) cylinder(h = 0.016, r = 0.036, $fn = 16);
    cw_m_matte(cw_MARKW()) translate([0, 0, 0.016]) cylinder(h = 0.002, r = 0.03, $fn = 16);
    cw_m_matte(cw_REDC()) translate([0.012, 0, 0.019]) cw_boxc([0.026, 0.006, 0.003]);
    cw_m_steel(cw_METALD()) translate([-0.012, 0, 0.019]) cw_boxc([0.026, 0.006, 0.003]);
    cw_m_matte(cw_OLIVED()) translate([-0.055, 0, 0.004]) rotate([0, 0, 0]) cw_boxc([0.04, 0.05, 0.006]);   // 翻开的盖
}

// 折叠地图
module cw_item_map(seed = 0)
{
    cw_m_fabric([0.80, 0.76, 0.62]) translate([0, 0, 0.006]) cw_boxc([0.22, 0.15, 0.012]);
    cw_m_fabric(cw_GRASSC()) translate([-0.04, 0.02, 0.0125]) cw_boxc([0.09, 0.07, 0.002]);
    cw_m_fabric(cw_SIGNB()) translate([0.03, -0.01, 0.0125]) rotate([0, 0, 35]) cw_boxc([0.16, 0.012, 0.002]);
    cw_m_fabric(cw_REDC()) translate([0.05, 0.04, 0.0125]) cw_boxc([0.012, 0.012, 0.003]);
    cw_m_fabric([0.64, 0.60, 0.48]) translate([0, 0, 0.0125]) cw_boxc([0.004, 0.15, 0.002]);
}

// 电池
module cw_item_battery(seed = 0)
{
    cw_m_matte([0.12, 0.12, 0.13]) translate([0, 0, 0.024]) cw_boxc([0.026, 0.017, 0.048]);
    cw_m_matte([0.72, 0.52, 0.18]) translate([0, 0, 0.012]) cw_boxc([0.027, 0.018, 0.024]);
    cw_m_galv(cw_METALC()) for (sx = [-1, 1]) translate([sx * 0.006, 0, 0.05]) cylinder(h = 0.005, r = 0.0035, $fn = 6);
}

// 鞭炮串
module cw_item_firecrackers(seed = 0)
{
    for (i = [0 : 5])
        cw_m_matte(cw_REDC()) translate([-0.05 + i * 0.02, (i % 2) * 0.012, 0.009]) rotate([90, 0, 0])
            cylinder(h = 0.07, r = 0.009, center = true, $fn = 8);
    cw_m_matte(cw_YELLOWC()) translate([0, 0, 0.018]) cw_boxc([0.13, 0.03, 0.004]);
    cw_m_matte(cw_DARKC()) translate([0.07, 0.03, 0.01]) rotate([0, 0, 40]) cw_boxc([0.06, 0.003, 0.003]);
}

// 弹药纸盒：kind 0=5.45 / 1=7.62 / 2=12 号霰弹 / 3=9mm
module cw_item_ammo_pack(kind = 0)
{
    c = [cw_OLIVE(), [0.42, 0.28, 0.16], cw_REDC(), [0.46, 0.46, 0.44]][kind];
    sz = [[0.13, 0.08, 0.05], [0.14, 0.07, 0.06], [0.12, 0.07, 0.07], [0.09, 0.06, 0.035]][kind];
    cw_m_matte(c) translate([0, 0, sz[2] / 2]) cw_boxc(sz);
    cw_m_matte(cw_MARKW()) translate([0, -sz[1] / 2 - 0.001, sz[2] * 0.55]) cw_boxc([sz[0] * 0.6, 0.002, sz[2] * 0.35]);
    if (kind == 2)
        for (i = [0 : 3]) cw_m_matte([0.72, 0.56, 0.20]) translate([-0.045 + i * 0.03, 0.045, 0.012]) rotate([90, 0, 0])
            cylinder(h = 0.05, r = 0.011, center = true, $fn = 8);
    else
        for (i = [0 : 2]) cw_m_matte([0.72, 0.56, 0.20]) translate([-0.03 + i * 0.03, 0.05, 0.006]) rotate([0, 90, 30])
            cylinder(h = 0.045, r = 0.005, center = true, $fn = 6);
}

// 叠好的夹克 / 卫衣（hood=true 为卫衣）
module cw_item_jacket(seed = 0, hood = false)
{
    c = hood ? [[0.40, 0.40, 0.42], [0.22, 0.30, 0.44], [0.46, 0.24, 0.20]][cw_rnd(seed, 3)]
             : (cw_rnd(seed, 2) == 0 ? cw_OLIVE() : [0.30, 0.26, 0.20]);
    cd = [c[0] * 0.8, c[1] * 0.8, c[2] * 0.8];
    cw_m_fabric(c) translate([0, 0, 0.04]) cw_boxc([0.42, 0.32, 0.08]);
    cw_m_fabric(cd)
    {
        translate([0, 0.14, 0.085]) cw_boxc([0.18, 0.06, 0.02]);
        for (sx = [-1, 1]) translate([sx * 0.12, -0.06, 0.082]) cw_boxc([0.1, 0.07, 0.012]);
        if (hood) translate([0, 0.18, 0.03]) scale([1.4, 0.7, 0.45]) sphere(r = 0.08, $fn = 10);
        else translate([0, 0.165, 0.06]) cw_boxc([0.24, 0.04, 0.05]);
    }
}

// 叠好的长裤
module cw_item_pants(seed = 0, cargo = false)
{
    c = cargo ? cw_OLIVED() : [[0.22, 0.26, 0.36], [0.30, 0.26, 0.20]][cw_rnd(seed, 2)];
    cw_m_fabric(c) translate([0, 0, 0.035]) cw_boxc([0.38, 0.26, 0.07]);
    cw_m_fabric([c[0] * 0.8, c[1] * 0.8, c[2] * 0.8])
    {
        translate([0, 0, 0.071]) cw_boxc([0.38, 0.006, 0.004]);
        if (cargo) for (sx = [-1, 1]) translate([sx * 0.1, -0.08, 0.075]) cw_boxc([0.1, 0.08, 0.012]);
    }
    cw_m_fabric([0.24, 0.18, 0.12]) translate([-0.17, 0, 0.072]) cw_boxc([0.035, 0.27, 0.01]);
}

// 防弹背心（平放）
module cw_item_vest(seed = 0)
{
    cw_m_fabric(cw_OLIVED()) translate([0, 0, 0.04]) cw_boxc([0.4, 0.34, 0.08]);
    cw_m_fabric(cw_OLIVE())
    {
        for (i = [0 : 2]) translate([-0.12 + i * 0.12, -0.13, 0.1]) cw_boxc([0.09, 0.07, 0.05]);
        for (sx = [-1, 1]) translate([sx * 0.13, 0.16, 0.085]) cw_boxc([0.08, 0.06, 0.012]);
    }
    cw_m_steel(cw_METALD()) translate([0, 0.02, 0.083]) cw_boxc([0.28, 0.14, 0.006]);
}

// 书包（小背包）
module cw_item_backpack_small(seed = 0)
{
    c = [[0.20, 0.30, 0.48], [0.50, 0.18, 0.16], [0.30, 0.30, 0.32]][cw_rnd(seed, 3)];
    rotate([-14, 0, 0])
    {
        cw_m_fabric(c) translate([0, 0, 0.2]) cw_boxc([0.3, 0.14, 0.4]);
        cw_m_fabric([c[0] * 0.75, c[1] * 0.75, c[2] * 0.75]) translate([0, -0.08, 0.14]) cw_boxc([0.24, 0.05, 0.18]);
        cw_m_fabric(cw_DARKC()) for (sx = [-1, 1]) translate([sx * 0.09, 0.075, 0.22]) cw_boxc([0.04, 0.02, 0.32]);
    }
}

// 军用突击背包（大）
module cw_item_backpack_military(seed = 0)
{
    rotate([-12, 0, 0])
    {
        cw_m_fabric(cw_OLIVE()) translate([0, 0, 0.32]) cw_boxc([0.42, 0.26, 0.64]);
        cw_m_fabric(cw_OLIVED())
        {
            translate([0, -0.15, 0.24]) cw_boxc([0.32, 0.06, 0.26]);
            for (sx = [-1, 1]) translate([sx * 0.23, 0, 0.3]) cw_boxc([0.06, 0.18, 0.3]);
            translate([0, 0, 0.66]) cw_boxc([0.44, 0.28, 0.06]);
        }
        cw_m_fabric(cw_KHAKI()) translate([0, 0.02, 0.72]) rotate([0, 90, 0]) cylinder(h = 0.4, r = 0.06, center = true, $fn = 10);
    }
}

// 急救包（红白软包）
module cw_item_medical_bag(seed = 0)
{
    cw_m_fabric(cw_REDC()) translate([0, 0, 0.11]) scale([1, 0.62, 0.7]) rotate([0, 90, 0]) cylinder(h = 0.42, r = 0.16, center = true, $fn = 12);
    cw_m_fabric(cw_MARKW())
    {
        translate([0, -0.1, 0.13]) cw_boxc([0.04, 0.004, 0.12]);
        translate([0, -0.1, 0.13]) cw_boxc([0.12, 0.004, 0.04]);
    }
    cw_m_fabric(cw_DARKC()) translate([0, 0, 0.22]) cw_boxc([0.2, 0.03, 0.03]);
}

// 工具箱
module cw_item_toolbox(seed = 0)
{
    cw_m_matte(cw_rnd(seed, 2) == 0 ? cw_REDC() : [0.20, 0.34, 0.50]) translate([0, 0, 0.09]) cw_boxc([0.46, 0.2, 0.18]);
    cw_m_steel(cw_METALD())
    {
        translate([0, 0, 0.19]) cw_boxc([0.46, 0.21, 0.02]);
        for (sx = [-1, 1]) translate([sx * 0.12, 0, 0.23]) cw_boxc([0.02, 0.02, 0.06]);
        translate([0, 0, 0.26]) cw_boxc([0.26, 0.025, 0.02]);
    }
}

// ================= 室内家具（front=-y，靠墙摆放；不得挡门洞） =================
// 刷新点高度契约（nextdayz_coldwar.scad 依赖）：桌面 0.77、床面 0.46、木架层板 0.67/1.22、
// 柜台 1.05、工作台 0.93、超市货架 0.58/1.13、沙发 0.5。

// 木桌（seed 决定翻倒）：清漆桌面 + 腿 + 横撑 + 桌上的生活痕迹
module cw_prop_table(seed = 0, L = 1.2, D = 0.7)
{
    flipped = cw_rnd(seed, 4) == 0;
    translate([0, 0, flipped ? D / 2 : 0]) rotate([flipped ? 90 : 0, 0, cw_rnd(seed, 20) - 10])
    {
        cw_m_varnish(cw_WOODC()) translate([0, 0, 0.73]) cw_boxc([L, D, 0.05]);
        cw_m_varnish(cw_WOODD())
        {
            for (sx = [-1, 1], sy = [-1, 1]) translate([sx * (L / 2 - 0.06), sy * (D / 2 - 0.06), 0.355]) cw_boxc([0.05, 0.05, 0.71]);
            for (sy = [-1, 1]) translate([0, sy * (D / 2 - 0.06), 0.64]) cw_boxc([L - 0.12, 0.025, 0.08]);
            for (sx = [-1, 1]) translate([sx * (L / 2 - 0.06), 0, 0.64]) cw_boxc([0.025, D - 0.12, 0.08]);
        }
        if (!flipped)
        {
            k = cw_rnd(seed + 3, 3);
            if (k == 0) cw_m_fabric(cw_fabric_c(seed)) translate([0, 0, 0.756]) cw_boxc([L * 0.8, D * 0.85, 0.004]);   // 桌布
            cw_m_ceramic(cw_MARKW()) translate([-L * 0.28, cw_rndr(seed + 1, -0.15, 0.15), 0.755]) difference() { cylinder(h = 0.09, r = 0.04, $fn = 10); translate([0, 0, 0.01]) cylinder(h = 0.1, r = 0.034, $fn = 10); }
            if (k == 1) cw_m_glass() translate([L * 0.25, 0.1, 0.755]) { cylinder(h = 0.22, r = 0.035, $fn = 10); translate([0, 0, 0.22]) cylinder(h = 0.07, r1 = 0.035, r2 = 0.012, $fn = 10); }
            cw_m_paper([0.46, 0.45, 0.41]) translate([L * 0.08, -0.12, 0.756]) rotate([0, 0, cw_rnd(seed, 60)]) cw_boxc([0.21, 0.297, 0.004]);
            if (k == 2) cw_m_ceramic([0.30, 0.30, 0.28]) translate([L * 0.3, -0.1, 0.755]) cylinder(h = 0.02, r = 0.06, $fn = 10);   // 烟灰缸
        }
    }
}

// 椅子（seed 决定倒地）
module cw_prop_chair(seed = 0)
{
    rotate([cw_rnd(seed, 3) == 0 ? -80 : 0, 0, cw_rnd(seed + 2, 90)]) translate([0, 0, cw_rnd(seed, 3) == 0 ? 0.25 : 0])
    {
        cw_m_varnish(cw_WOODC()) translate([0, 0, 0.45]) cw_boxc([0.44, 0.42, 0.035]);
        cw_m_varnish(cw_WOODD())
        {
            for (sx = [-1, 1], sy = [-1, 1]) translate([sx * 0.19, sy * 0.18, 0.22]) cw_boxc([0.035, 0.035, 0.45]);
            for (sx = [-1, 1]) translate([sx * 0.19, 0.19, 0.7]) rotate([-6, 0, 0]) cw_boxc([0.035, 0.035, 0.5]);
            translate([0, 0.2, 0.86]) rotate([-6, 0, 0]) cw_boxc([0.42, 0.025, 0.14]);
            translate([0, 0.2, 0.62]) cw_boxc([0.38, 0.02, 0.05]);
            for (sy = [-1, 1]) translate([0, sy * 0.18, 0.15]) cw_boxc([0.36, 0.02, 0.03]);
        }
    }
}

// 苏式铁架床 + 弹簧垫 + 床垫 + 揉乱的毯子（床面 0.46）
module cw_prop_bed(seed = 0)
{
    fc = [[0.18, 0.24, 0.26], [0.30, 0.30, 0.28], [0.26, 0.18, 0.14]][cw_rnd(seed + 9, 3)];
    cw_m_steel(fc)
    {
        for (sx = [-1, 1], sy = [-1, 1]) translate([sx * 0.97, sy * 0.44, sx > 0 ? 0.5 : 0.4]) cylinder(h = sx > 0 ? 1.0 : 0.8, r = 0.02, center = true, $fn = 6);
        for (sx = [-1, 1]) for (z = sx > 0 ? [0.9, 0.55] : [0.7, 0.45]) translate([sx * 0.97, 0, z]) rotate([90, 0, 0]) cylinder(h = 0.9, r = 0.016, center = true, $fn = 6);
        for (sx = [-1, 1]) for (y = [-0.3 : 0.15 : 0.31]) translate([sx * 0.97, y, sx > 0 ? 0.72 : 0.57]) cylinder(h = sx > 0 ? 0.35 : 0.25, r = 0.008, center = true, $fn = 4);
        for (sy = [-1, 1]) translate([0, sy * 0.44, 0.27]) cw_boxc([1.94, 0.03, 0.05]);
    }
    cw_m_galv() translate([0, 0, 0.26]) cw_boxc([1.9, 0.86, 0.01]);   // 弹簧网
    cw_m_fabric([0.40, 0.38, 0.33]) translate([0, 0, 0.33]) cw_boxc([1.9, 0.86, 0.12]);
    cw_m_fabric(cw_MARKW()) translate([0.72, 0, 0.42]) scale([1, 2.2, 0.55]) sphere(r = 0.16, $fn = 10);
    cw_m_fabric(cw_fabric_c(seed + 1)) translate([-0.3, cw_rndr(seed, -0.08, 0.08), 0.42]) rotate([0, 0, cw_rnd(seed, 16) - 8])
        hull() { cw_boxc([1.2, 0.94, 0.03]); translate([0.1, 0.1, 0.02]) cw_boxc([0.6, 0.5, 0.03]); }
    if (cw_rnd(seed + 4, 3) == 0)
        cw_m_blood() translate([0.1, 0.1, 0.455]) scale([1, 0.7, 1]) cylinder(h = 0.004, r = 0.24, $fn = 12);
    cw_m_ceramic([0.42, 0.42, 0.40]) translate([0.6, 0.2, 0]) difference() { cylinder(h = 0.22, r1 = 0.1, r2 = 0.12, $fn = 12); translate([0, 0, 0.02]) cylinder(h = 0.3, r = 0.1, $fn = 12); }   // 床下的搪瓷盆
}

// 衣柜（门半开，镜子 + 挂着的衣服）
module cw_prop_wardrobe(seed = 0)
{
    cw_m_varnish(cw_WOODD()) difference()
    {
        translate([0, 0, 0.95]) cw_boxc([0.95, 0.55, 1.9]);
        translate([0, -0.05, 1.0]) cw_boxc([0.85, 0.5, 1.7]);
    }
    cw_m_varnish(cw_WOODC())
    {
        translate([0.22, -0.28, 1.0]) cw_boxc([0.44, 0.025, 1.7]);
        translate([-0.45, -0.5, 1.0]) rotate([0, 0, -60]) translate([0.22, 0, 0]) cw_boxc([0.44, 0.025, 1.7]);
        translate([0, 0, 1.92]) cw_boxc([1.0, 0.6, 0.05]);
    }
    cw_m_chrome() translate([-0.45, -0.5, 1.15]) rotate([0, 0, -60]) translate([0.22, -0.015, 0]) cw_boxc([0.34, 0.005, 0.9]);   // 门内镜子
    cw_m_galv() translate([0, 0.0, 1.7]) rotate([0, 90, 0]) cylinder(h = 0.85, r = 0.01, center = true, $fn = 4);
    for (i = [0 : 2]) if (cw_rnd(seed + i, 3) != 0)
        cw_m_fabric(cw_fabric_c(seed + i)) translate([-0.25 + i * 0.22, 0, 1.3]) rotate([0, 0, cw_rndr(seed + i, -10, 10)]) cw_boxc([0.08, 0.42, 0.75]);
    cw_m_fabric(cw_fabric_c(seed + 7)) translate([0.1, -0.35, 0.03]) rotate([0, 0, 30]) cw_boxc([0.5, 0.35, 0.05]);   // 掉在地上的衣服
}

// 铁皮柜 / 更衣柜（军营、厂房）：百叶通风孔 + 名牌 + 被撬开的门
module cw_prop_locker(seed = 0, n = 3)
{
    for (i = [0 : n - 1])
    {
        x = (i - (n - 1) / 2) * 0.42;
        c = cw_rnd(seed + i, 2) == 0 ? [0.28, 0.34, 0.32] : [0.36, 0.37, 0.36];
        cw_m_steel(c) translate([x, 0, 0.9]) difference() { cw_boxc([0.4, 0.45, 1.8]); translate([0, -0.03, 0.05]) cw_boxc([0.36, 0.42, 1.66]); }
        color(cw_DARKC()) for (z = [1.55, 1.6, 1.65, 0.3, 0.35]) translate([x, -0.226, z]) cw_boxc([0.2, 0.004, 0.02]);
        cw_m_paper([0.46, 0.45, 0.40]) translate([x, -0.227, 1.4]) cw_boxc([0.12, 0.003, 0.05]);
        if (cw_rnd(seed + i * 7, 3) == 0)
        {
            cw_m_steel(c) translate([x - 0.2, -0.38, 0.9]) rotate([0, 0, -50]) translate([0.19, 0, 0]) cw_boxc([0.38, 0.02, 1.7]);
            cw_m_fabric(cw_OLIVE()) translate([x, -0.05, 1.2]) cw_boxc([0.3, 0.2, 0.7]);
        }
        else cw_m_steel(c) translate([x, -0.215, 0.9]) cw_boxc([0.36, 0.01, 1.68]);
        cw_m_galv() translate([x + 0.12, -0.23, 1.0]) cw_boxc([0.02, 0.02, 0.1]);
    }
}

// 木货架（杂物：罐头瓶、纸箱、书）
module cw_prop_shelf_wood(seed = 0, L = 1.6)
{
    cw_m_planks(cw_WOODD())
    {
        for (sx = [-1, 1]) translate([sx * (L / 2 - 0.03), 0, 0.9]) cw_boxc([0.05, 0.4, 1.8]);
        for (z = [0.1, 0.65, 1.2, 1.75]) translate([0, 0, z]) cw_boxc([L, 0.4, 0.04]);
        translate([0, 0.19, 0.9]) cw_boxc([L, 0.02, 1.8]);
    }
    for (z = [0.65, 1.2, 1.75], i = [0 : 3])
        if (cw_rnd(seed + i * 3 + z * 10, 3) != 0)
        {
            k = cw_rnd(seed + i * 5 + z * 7, 3);
            translate([-L * 0.375 + i * L / 4, 0, z + 0.02])
                if (k == 0) cw_m_paper(cw_mul(cw_goods_c(seed + i + z * 7), 0.9)) translate([0, 0, 0.1]) cw_boxc([0.3, 0.26, 0.2]);
                else if (k == 1) cw_m_glass() for (j = [0 : 1]) translate([j * 0.12 - 0.06, 0, 0]) cylinder(h = 0.16, r = 0.045, $fn = 10);
                else cw_m_paper(cw_goods_c(seed + i * 3)) for (j = [0 : 3]) translate([j * 0.05 - 0.08, 0, 0.12]) rotate([0, j == 3 ? 20 : 0, 0]) cw_boxc([0.04, 0.2, 0.24]);
        }
}

// 搪瓷灶台 + 液化气罐
module cw_prop_stove(seed = 0)
{
    cw_m_ceramic([0.46, 0.45, 0.42]) translate([0, 0, 0.43]) cw_boxc([0.6, 0.6, 0.86]);
    cw_m_ceramic([0.42, 0.41, 0.38]) translate([0, 0.27, 0.95]) cw_boxc([0.6, 0.06, 0.2]);
    color(cw_DARKC()) translate([0, -0.302, 0.4]) cw_boxc([0.44, 0.004, 0.36]);
    cw_m_glass() translate([0, -0.304, 0.42]) cw_boxc([0.3, 0.004, 0.18]);
    cw_m_iron([0.08, 0.08, 0.08]) for (sx = [-1, 1], sy = [-1, 1]) translate([sx * 0.14, sy * 0.14, 0.86]) cylinder(h = 0.015, r = 0.08, $fn = 12);
    cw_m_plastic([0.08, 0.08, 0.08]) for (i = [0 : 3]) translate([-0.21 + i * 0.14, -0.31, 0.78]) rotate([90, 0, 0]) cylinder(h = 0.03, r = 0.025, center = true, $fn = 8);
    cw_m_galv(cw_METALD()) translate([0.14, 0.14, 0.875]) difference() { cylinder(h = 0.14, r = 0.11, $fn = 12); translate([0, 0, 0.02]) cylinder(h = 0.2, r = 0.1, $fn = 12); }
    cw_m_steel([0.40, 0.08, 0.05]) translate([0.48, 0.1, 0]) { cylinder(h = 0.55, r = 0.15, $fn = 12); translate([0, 0, 0.55]) sphere(r = 0.15, $fn = 10); }
    cw_m_rubber() cw_rod([0.48, 0.1, 0.68], [0.3, 0.25, 0.7], 0.01, 4);
}

// 旧沙发（布面 + 坐垫 + 扶手 + 一只掉在地上的靠垫）
module cw_prop_sofa(seed = 0)
{
    c = cw_fabric_c(seed + 2);
    cw_m_fabric(c)
    {
        translate([0, 0.05, 0.22]) cw_boxc([1.9, 0.8, 0.3]);
        translate([0, 0.33, 0.62]) hull() { cw_boxc([1.9, 0.18, 0.5]); translate([0, -0.04, 0.27]) cw_boxc([1.8, 0.12, 0.04]); }
        for (sx = [-1, 1]) translate([sx * 0.87, 0.02, 0.5]) hull() { cw_boxc([0.18, 0.8, 0.22]); translate([0, 0, 0.1]) cw_boxc([0.14, 0.74, 0.04]); }
    }
    cw_m_fabric(cw_mul(c, 0.88)) for (sx = [-1, 1]) translate([sx * 0.38, -0.02, 0.42]) hull() { cw_boxc([0.74, 0.66, 0.08]); translate([0, 0, 0.04]) cw_boxc([0.66, 0.58, 0.04]); }
    cw_m_varnish(cw_WOODD()) for (sx = [-1, 1], sy = [-1, 1]) translate([sx * 0.88, sy * 0.36, 0.035]) cylinder(h = 0.07, r1 = 0.025, r2 = 0.035, $fn = 6);
    cw_m_soil(cw_SOILC()) translate([0.3, -0.1, 0.485]) cw_boxc([0.5, 0.4, 0.004]);
    cw_m_fabric(cw_fabric_c(seed + 5)) translate([-0.6, -0.65, 0.07]) rotate([10, 0, 30]) cw_boxc([0.4, 0.4, 0.12]);
}

// 店铺柜台 + 收银机 + 秤（台面 1.05）
module cw_prop_counter(seed = 0, L = 2.2)
{
    cw_m_varnish(cw_WOODC()) translate([0, 0, 0.5]) cw_boxc([L, 0.6, 1.0]);
    cw_m_varnish(cw_WOODD()) for (x = [-L / 2 + 0.3 : 0.6 : L / 2 - 0.2]) translate([x, -0.302, 0.5]) cw_boxc([0.5, 0.004, 0.8]);
    cw_m_ceramic(cw_mul(cw_PANELD(), 0.9)) translate([0, 0, 1.02]) cw_boxc([L + 0.06, 0.66, 0.04]);
    cw_m_steel([0.30, 0.30, 0.28]) translate([L * 0.3, 0, 1.12]) hull() { cw_boxc([0.36, 0.3, 0.16]); translate([0, 0.05, 0.1]) cw_boxc([0.3, 0.16, 0.04]); }
    color(cw_DARKC()) translate([L * 0.3, 0.06, 1.26]) rotate([-30, 0, 0]) cw_boxc([0.24, 0.02, 0.14]);
    cw_m_steel([0.30, 0.30, 0.28]) translate([L * 0.3, -0.12, 1.06]) cw_boxc([0.3, 0.14, 0.06]);   // 被拉开的钱箱
    cw_m_galv() translate([-L * 0.3, 0, 1.04]) { cw_slab(0.3, 0.25, 0.08); translate([0, 0.1, 0.08]) cw_slab(0.06, 0.06, 0.2); translate([0, 0.1, 0.28]) cylinder(h = 0.03, r = 0.1, $fn = 12); }
    cw_m_paper([0.46, 0.45, 0.41]) for (i = [0 : 2]) translate([cw_rndr(seed + i, -L / 2, L / 2), -0.5 - i * 0.1, 0.003]) rotate([0, 0, i * 50]) cw_boxc([0.15, 0.08, 0.002]);
}

// 工作台 + 台钳 + 工具挂板（台面 0.93）
module cw_prop_workbench(seed = 0)
{
    cw_m_planks(cw_WOODD()) translate([0, 0, 0.88]) cw_boxc([1.8, 0.75, 0.08]);
    cw_m_steel([0.22, 0.26, 0.24])
    {
        for (sx = [-1, 1], sy = [-1, 1]) translate([sx * 0.82, sy * 0.32, 0.42]) cw_boxc([0.05, 0.05, 0.84]);
        translate([0, 0, 0.2]) cw_boxc([1.7, 0.7, 0.03]);
    }
    cw_m_steel([0.12, 0.22, 0.32]) translate([0.65, -0.3, 0.98]) { cw_boxc([0.18, 0.16, 0.12]); translate([0, -0.12, 0]) cw_boxc([0.16, 0.06, 0.1]); }
    cw_m_planks(cw_WOODC()) translate([-0.2, 0.37, 1.35]) cw_boxc([1.4, 0.03, 0.8]);
    cw_m_iron() for (i = [0 : 4]) translate([-0.7 + i * 0.25, 0.34, 1.35 + (i % 2) * 0.1]) cw_boxc([0.03, 0.03, 0.28]);
    cw_m_steel([0.40, 0.08, 0.05]) translate([0.3, 0.33, 1.5]) cw_boxc([0.25, 0.04, 0.06]);
    cw_m_steel([0.40, 0.08, 0.05]) translate([-0.55, 0.05, 0.22]) cw_slab(0.45, 0.22, 0.2);   // 工具箱
    cw_m_oil() translate([0.2, -0.6, 0.003]) cw_decal(seed, 0.3, 0.8, 0.003);
}

// ================= 末日痕迹 =================

// 尸袋（黑 / 军绿乙烯基长袋 + 拉链 + 吊牌；不旋转版）
module cw_prop_body_bag_straight(seed = 0)
{
    c = cw_rnd(seed, 3) == 0 ? cw_OLIVED() : [0.06, 0.06, 0.07];
    cw_m_plastic(c)
    {
        hull()
        {
            translate([0, 0, 0.12]) scale([1, 0.42, 0.22]) rotate([0, 90, 0]) cylinder(h = 1.7, r = 0.55, center = true, $fn = 14);
            translate([-0.2, 0, 0.18]) scale([0.6, 0.38, 0.22]) sphere(r = 0.5, $fn = 12);
        }
        translate([0.84, 0, 0.13]) scale([0.9, 1, 0.85]) sphere(r = 0.2, $fn = 12);
        for (sy = [-1, 1]) translate([0.75, sy * 0.15, 0.04]) cw_boxc([0.06, 0.06, 0.06]);   // 提手
    }
    cw_m_galv() translate([0, 0.04, 0.245]) cw_boxc([1.5, 0.015, 0.01]);
    cw_m_paper([0.46, 0.44, 0.38]) translate([0.6, 0.08, 0.25]) rotate([0, 0, 20]) cw_boxc([0.12, 0.07, 0.003]);
}
module cw_prop_body_bag(seed = 0) rotate([0, 0, cw_rnd(seed, 180)]) cw_prop_body_bag_straight(seed);

// 干涸血迹（地面贴片 + 拖痕）
module cw_prop_blood_stain(seed = 0, s = 1.0)
{
    cw_m_blood() for (i = [0 : 3])
        translate([cw_rndr(seed + i * 7, -0.4, 0.4) * s, cw_rndr(seed + i * 13, -0.3, 0.3) * s, 0.004 + i * 0.0005])
            scale([s, s, 1]) cw_decal(seed + i * 5, cw_rndr(seed + i, 0.15, 0.4), 0.7, 0.003);
    gk_material(cw_mul(cw_BLOODC(), 0.8), roughness = 0.7) for (i = [0 : 5]) translate([(0.4 + i * 0.22) * s, cw_rndr(seed + i, -0.06, 0.06) * s, 0.004])
        scale([s, s, 1]) cw_boxc([0.18, 0.12 - i * 0.012, 0.002]);
}

// 垃圾堆（黑袋 + 纸屑 + 罐子 + 瓶子）
module cw_prop_trash(seed = 0)
{
    for (i = [0 : 2 + cw_rnd(seed, 3)])
        cw_m_plastic(cw_rnd(seed + i, 3) == 0 ? [0.16, 0.22, 0.28] : [0.04, 0.04, 0.05])
            translate([cw_rndr(seed + i * 11, -0.6, 0.6), cw_rndr(seed + i * 17, -0.45, 0.45), 0.16])
                rotate([0, 0, i * 40]) hull() { scale([1, 0.85, 0.7]) sphere(r = cw_rndr(seed + i * 3, 0.2, 0.3), $fn = 10); translate([0, 0, 0.22]) sphere(r = 0.04, $fn = 6); }
    for (i = [0 : 5])
        cw_m_paper(cw_rnd(seed + i, 2) == 0 ? [0.46, 0.45, 0.41] : [0.42, 0.38, 0.28])
            translate([cw_rndr(seed + i * 5, -0.9, 0.9), cw_rndr(seed + i * 23, -0.7, 0.7), 0.004])
                rotate([cw_rndr(seed + i, -10, 10), 0, cw_rnd(seed + i * 9, 180)]) cw_boxc([0.2, 0.14, 0.006]);
    cw_m_galv(cw_goods_c(seed)) translate([0.5, -0.4, 0.035]) rotate([90, 0, cw_rnd(seed, 180)]) cylinder(h = 0.12, r = 0.035, center = true, $fn = 10);
    cw_m_glass() translate([-0.55, 0.45, 0.035]) rotate([90, 0, cw_rnd(seed + 3, 180)]) { cylinder(h = 0.22, r = 0.035, center = true, $fn = 10); translate([0, 0, 0.14]) cylinder(h = 0.06, r1 = 0.035, r2 = 0.012, center = true, $fn = 10); }
}

// 木板路障（钉死的门窗 / 街垒，沿 x；front=-y）
module cw_prop_barricade(len = 3, h = 1.4, seed = 0)
{
    cw_m_timber(cw_WOODD()) for (sx = [-1, 1]) translate([sx * (len / 2 - 0.1), 0, h / 2]) cw_boxc([0.1, 0.1, h]);
    for (i = [0 : floor(h / 0.3) - 1])
        cw_m_planks(cw_rnd(seed + i, 2) == 0 ? cw_WOODC() : [0.38, 0.31, 0.22])
            translate([0, -0.07, 0.2 + i * 0.3]) rotate([0, cw_rnd(seed + i * 7, 12) - 6, 0]) cw_boxc([len, 0.035, 0.2]);
    cw_m_planks(cw_WOODD()) translate([0, -0.1, h / 2]) rotate([0, 38, 0]) cw_boxc([len * 0.95, 0.035, 0.16]);
    cw_m_steel([0.30, 0.16, 0.10]) translate([len * 0.25, -0.35, 0.4]) rotate([0, 0, 15]) cw_boxc([0.9, 0.5, 0.8]);   // 塞进来的旧柜子
    cw_m_varnish(cw_WOODC()) translate([-len * 0.2, -0.4, 0.3]) rotate([0, 90, 10]) { cw_boxc([0.6, 0.5, 0.04]); for (sx = [-1, 1], sy = [-1, 1]) translate([sx * 0.25, sy * 0.2, 0.2]) cw_boxc([0.04, 0.04, 0.4]); }
    cw_m_iron() for (i = [0 : 2]) translate([cw_rndr(seed + i, -len / 2, len / 2), -0.09, 0.2 + i * 0.3]) rotate([90, 0, 0]) cylinder(h = 0.01, r = 0.01, $fn = 4);
}

// 混凝土新泽西护栏（沿 x）
module cw_prop_jersey_barrier(len = 3)
{
    cw_m_concrete(cw_CONC()) hull()
    {
        translate([0, 0, 0.04]) cw_boxc([len, 0.6, 0.08]);
        translate([0, 0, 0.3]) cw_boxc([len, 0.36, 0.1]);
        translate([0, 0, 0.8]) cw_boxc([len, 0.18, 0.04]);
    }
    cw_m_matte(cw_YELLOWC()) for (i = [0 : floor(len / 1.2)])
        translate([-len / 2 + 0.4 + i * 1.2, -0.2, 0.42]) rotate([0, 30, 0]) cw_boxc([0.3, 0.02, 0.18]);
    cw_m_iron() for (sx = [-1, 1]) translate([sx * (len / 2 - 0.05), 0, 0.5]) rotate([0, 90, 0]) difference() { cylinder(h = 0.04, r = 0.06, center = true, $fn = 8); cylinder(h = 0.1, r = 0.04, center = true, $fn = 8); }
}

// 检疫告示牌（双柱 + 黄黑警示 + 文字）
module cw_prop_sign_quarantine(seed = 0, msg = "QUARANTINE")
{
    cw_m_steel(cw_METALD()) for (sx = [-1, 1]) translate([sx * 0.9, 0, 1.0]) cw_boxc([0.06, 0.06, 2.0]);
    cw_m_matte(cw_MARKW()) translate([0, 0, 1.65]) cw_boxc([2.1, 0.03, 0.6]);
    cw_m_matte(cw_REDC()) translate([0, -0.017, 1.68]) rotate([90, 0, 0]) linear_extrude(height = 0.004, center = true)
        text(msg, size = 0.2, halign = "center", valign = "center");
    cw_m_matte(cw_DARKC()) translate([0, -0.017, 1.48]) rotate([90, 0, 0]) linear_extrude(height = 0.004, center = true)
        text("NO ENTRY", size = 0.08, halign = "center", valign = "center");
    cw_m_matte(cw_YELLOWC()) translate([0, 0, 1.2]) cw_boxc([2.1, 0.03, 0.22]);
    cw_m_matte(cw_DARKC()) for (i = [0 : 5]) translate([-0.85 + i * 0.34, -0.017, 1.2]) rotate([0, 35, 0]) cw_boxc([0.1, 0.004, 0.3]);
    cw_m_plastic([0.44, 0.40, 0.08]) for (sx = [-1, 1]) cw_rod([sx * 0.9, -0.02, 1.0], [sx * 2.6, -0.4, 0.9], 0.012, 3);   // 警戒带
}

// 墙面涂鸦（贴墙；front=-y，锚点=文字中心）
module cw_prop_graffiti(msg = "HELP", size = 0.6, seed = 0)
{
    gk_material([[0.44, 0.06, 0.04], [0.48, 0.47, 0.43], [0.06, 0.06, 0.06]][cw_rnd(seed, 3)], roughness = 0.75)
        rotate([90, 0, 0]) linear_extrude(height = 0.006)
            text(msg, size = size, halign = "center", valign = "center");
}

// 医疗帐篷（白帐 + 红十字 + 绷绳）
module cw_prop_tent_medical(seed = 0, L = 5, D = 3.6)
{
    c = [0.44, 0.44, 0.41];
    cw_m_canvas(c) for (sy = [-1, 1]) translate([0, sy * D * 0.28, 0]) rotate([sy * 62, 0, 0]) cw_boxc([L, 0.04, D * 0.72]);
    cw_m_canvas(cw_mul(c, 0.9)) translate([L / 2 - 0.04, 0, 0]) rotate([90, 0, 90]) linear_extrude(height = 0.04, center = true) polygon([[-D * 0.48, 0], [D * 0.48, 0], [0, 1.55]]);
    cw_m_canvas(cw_mul(c, 0.9)) translate([-L / 2 + 0.04, 0, 0]) rotate([90, 0, 90]) linear_extrude(height = 0.04, center = true) polygon([[-D * 0.48, 0], [-0.55, 0], [0, 1.1], [0, 1.55]]);
    color(cw_DARKC()) translate([-L / 2 + 0.02, 0.27, 0]) rotate([90, 0, 90]) linear_extrude(height = 0.02, center = true) polygon([[-0.8, 0], [0.27, 0], [0.27, 1.1], [-0.27, 1.2]]);
    cw_m_matte(cw_REDC()) for (sy = [-1, 1])
        translate([0, sy * (D * 0.28 + 0.03), 0.9]) rotate([sy * 62, 0, 0])
        {
            cw_boxc([0.18, 0.01, 0.6]);
            cw_boxc([0.6, 0.01, 0.18]);
        }
    cw_m_timber(cw_WOODD()) for (sx = [-1, 1]) translate([sx * (L / 2 - 0.1), 0, 0.77]) cw_boxc([0.05, 0.05, 1.55]);
    cw_m_paper([0.40, 0.38, 0.30]) for (sy = [-1, 1], sx = [-1, 1])
        cw_rod([sx * (L / 2 - 0.3), sy * D * 0.46, 0.25], [sx * (L / 2 - 0.3), sy * (D / 2 + 0.55), 0.02], 0.006, 3);
}

// 圆草捆（卧放）
module cw_prop_haybale(seed = 0)
{
    rotate([0, 0, cw_rnd(seed, 180)])
    {
        cw_m_hay([0.50, 0.42, 0.22]) translate([0, 0, 0.6]) rotate([90, 0, 0]) cylinder(h = 1.2, r = 0.6, center = true, $fn = 18);
        cw_m_hay([0.42, 0.34, 0.17]) for (sy = [-1, 1]) translate([0, sy * 0.601, 0.6]) rotate([90, 0, 0])
            cylinder(h = 0.01, r = 0.52, center = true, $fn = 18);
        cw_m_plastic([0.40, 0.40, 0.38]) for (y = [-0.3, 0.3]) translate([0, y, 0.6]) rotate([90, 0, 0]) difference() { cylinder(h = 0.02, r = 0.605, center = true, $fn = 18); cylinder(h = 0.1, r = 0.59, center = true, $fn = 18); }
    }
}

// 原木堆（锯木场）
module cw_prop_log_pile(seed = 0, L = 5)
{
    for (r = [0 : 2], i = [0 : 4 - r])
    {
        p = [0, -0.9 + i * 0.42 + r * 0.21, 0.21 + r * 0.36];
        ll = L + cw_rndr(seed + i, -0.4, 0.4);
        cw_m_bark(cw_rnd(seed + r * 5 + i, 3) == 0 ? cw_BIRCHW() : cw_TRUNKC()) translate(p) rotate([0, 90, 0]) cylinder(h = ll, r = 0.2, center = true, $fn = 9);
        cw_m_timber([0.46, 0.37, 0.24]) for (s = [-1, 1]) translate(p + [s * ll / 2, 0, 0]) rotate([0, 90, 0]) cylinder(h = 0.01, r = 0.18, center = true, $fn = 9);
    }
    cw_m_timber(cw_WOODD()) for (sx = [-1, 1], sy = [-1, 1]) translate([sx * L * 0.35, sy * 1.2, 0.6]) cw_boxc([0.12, 0.12, 1.2]);
}

// 沙袋机枪巢（U 形，开口朝 +y）：沙袋 + PK 机枪 + 弹药箱 + 弹壳
module cw_prop_sandbag_nest(seed = 0)
{
    translate([0, -1.4, 0]) cw_prop_wall_sandbag(len = 3);
    for (sx = [-1, 1]) translate([sx * 1.55, 0, 0]) rotate([0, 0, 90]) cw_prop_wall_sandbag(len = 2.4);
    cw_m_steel([0.12, 0.13, 0.11])
    {
        translate([0, -1.25, 0.82]) rotate([90, 0, 0]) cylinder(h = 1.1, r = 0.025, center = true, $fn = 8);
        translate([0, -0.75, 0.82]) cw_boxc([0.1, 0.45, 0.12]);
        for (sx = [-1, 1]) translate([sx * 0.1, -1.55, 0.72]) rotate([sx * 0, sx * 20, 0]) cw_boxc([0.02, 0.02, 0.22]);
    }
    cw_m_varnish([0.30, 0.14, 0.06]) translate([0, -0.42, 0.8]) cw_boxc([0.05, 0.25, 0.08]);
    translate([0.6, -0.2, 0]) rotate([0, 0, 90]) cw_m_steel(cw_OLIVE()) translate([0, 0, 0.12]) cw_boxc([0.36, 0.2, 0.24]);
    gk_material([0.62, 0.46, 0.22], roughness = 0.3, metalness = 1) for (i = [0 : 11])
        translate([cw_rndr(seed + i, -0.8, 0.8), cw_rndr(seed + i * 3, -1.0, 0.4), 0.008]) rotate([0, 90, i * 37]) cylinder(h = 0.05, r = 0.006, center = true, $fn = 5);
}

// 手压水泵（东欧乡村水源；NextDayz 识别为水井）：铸铁泵身 + 压杆 + 混凝土井台 + 水桶
module cw_prop_water_pump(seed = 0)
{
    cw_m_concrete(cw_CONCD()) cw_slab(1.0, 1.0, 0.18);
    cw_m_steel([0.15, 0.27, 0.20])
    {
        translate([0, 0, 0.18]) cylinder(h = 0.95, r = 0.09, $fn = 14);
        translate([0, 0, 0.18]) cylinder(h = 0.12, r = 0.14, $fn = 14);
        translate([0, 0, 1.13]) cylinder(h = 0.14, r1 = 0.11, r2 = 0.06, $fn = 14);
        translate([0, 0, 1.27]) sphere(r = 0.05, $fn = 10);
        translate([0, -0.14, 0.86]) rotate([60, 0, 0]) cylinder(h = 0.24, r = 0.035, $fn = 10);
    }
    cw_m_iron()
    {
        translate([0, 0.22, 1.24]) rotate([-18, 0, 0]) cw_boxc([0.035, 0.6, 0.035]);
        translate([0, 0.05, 1.2]) cw_boxc([0.04, 0.04, 0.12]);
    }
    cw_m_galv(cw_METALC()) translate([0, -0.34, 0.18]) difference() { cylinder(h = 0.26, r1 = 0.12, r2 = 0.15, $fn = 14); translate([0, 0, 0.02]) cylinder(h = 0.3, r1 = 0.11, r2 = 0.14, $fn = 14); }
    cw_m_water() translate([0, -0.34, 0.36]) cylinder(h = 0.01, r = 0.135, $fn = 14);
    cw_m_damp([0.20, 0.19, 0.17]) translate([0, -0.3, 0.18]) cw_decal(seed, 0.35, 0.8, 0.003);
}

// =====================================================================================
// ================= 扩展建筑（front=-y；可进入外壳 + 细部） =================

// 两层砖房：底层可进入，二层窗 + 层间腰线 + 陶瓦坡顶 + 烟囱 + 门廊
module cw_bldg_house_two(seed = 0, L = 9, D = 7)
{
    fh = 2.9;
    wh = fh * 2 + 0.3;
    kind = cw_rnd(seed, 4);
    wc = [cw_BRICK(), cw_PLASTER(), [0.44, 0.41, 0.34], cw_WHITEW()][kind];
    dx = (cw_rnd(seed + 3, 2) == 0 ? -1 : 1) * L * 0.2;
    cw_part_shell(L, D, wh, wc, t = 0.3, dw = 1.2, dh = 2.1, dx = dx, roomH = fh - 0.2,
                  wins = [[0, -dx, 1.5, 1.1, 1.3], [1, -L * 0.25, 1.5, 1.1, 1.3], [1, L * 0.25, 1.5, 1.1, 1.3],
                          [2, 0, 1.5, 1.0, 1.3], [3, D * 0.15, 1.5, 1.0, 1.3]],
                  seed = seed, wall = kind == 0 ? "brick" : "plaster", floor = "boards", door = "wood");
    // 底层窗套 + 百叶
    for (w = [[-dx, -1], [-L * 0.25, 1], [L * 0.25, 1]])
        translate([w[0], w[1] * (D / 2 + 0.02), 0])
        {
            cw_m_plaster(cw_mul(wc, kind == 0 ? 1.3 : 0.85)) translate([0, 0, 2.18]) cw_boxc([1.3, 0.1, 0.12]);
            cw_m_oilpaint([0.18, 0.26, 0.20]) for (sx = [-1, 1]) translate([sx * 0.8, w[1] * 0.04, 1.5]) rotate([0, 0, sx * w[1] * (cw_rnd(seed + sx, 2) == 0 ? 0 : 25)]) cw_boxc([0.45, 0.03, 1.3]);
        }
    // 二层窗阵 + 腰线
    cw_m_plaster(cw_mul(wc, kind == 0 ? 1.3 : 0.85)) for (sy = [-1, 1]) translate([0, sy * (D / 2 + 0.03), fh + 0.15]) cw_boxc([L + 0.06, 0.08, 0.2]);
    for (i = [-1, 0, 1], sy = [-1, 1])
        translate([i * L * 0.3, sy * (D / 2 + 0.04), fh + 1.55]) rotate([0, 0, sy > 0 ? 180 : 0])
            cw_part_window(1.0, 1.25, seed * 7 + i * 3 + sy * 5, cw_mul(wc, kind == 0 ? 1.3 : 0.92));
    translate([0, 0, wh]) cw_part_roof(L, D, 2.2, 0.5, 0, kind == 3 ? cw_ROOFM() : [0.30, 0.13, 0.08], kind == 3 ? "eternit" : "tile");
    cw_m_plaster(wc) for (sx = [-1, 1]) translate([sx * (L / 2 - 0.15), 0, wh]) rotate([90, 0, 90])
        linear_extrude(height = 0.3, center = true) polygon([[-D / 2, 0], [D / 2, 0], [0, 2.18]]);
    cw_m_brick(cw_BRICKD()) translate([-L * 0.28, D * 0.18, wh + 1.0]) cw_boxc([0.6, 0.6, 2.4]);
    cw_m_concrete(cw_CONCD()) translate([-L * 0.28, D * 0.18, wh + 2.25]) cw_boxc([0.72, 0.72, 0.1]);
    cw_m_iron() translate([-L * 0.28 + 0.4, D * 0.18, wh + 2.25]) cylinder(h = 1.2, r = 0.012, $fn = 4);   // 旧电视天线
    // 门廊（雨棚 + 双柱 + 台阶）
    cw_m_timber(cw_WOODD()) for (sx = [-1, 1]) translate([dx + sx * 0.85, -D / 2 - 1.0, 1.25]) cw_boxc([0.12, 0.12, 2.5]);
    cw_m_roof("tin", cw_roof_c(seed + 5)) translate([dx, -D / 2 - 0.55, 2.6]) rotate([-12, 0, 0]) cw_boxc([2.1, 1.3, 0.04]);
    cw_m_concrete(cw_CONCD()) translate([dx, -D / 2 - 0.55, 0]) cw_slab(1.8, 1.1, 0.02);
    // 檐沟 + 落水管 + 信箱
    cw_m_zinc()
    {
        translate([L / 2 + 0.3, -D / 2 - 0.4, wh / 2]) cylinder(h = wh, r = 0.05, center = true, $fn = 8);
        translate([L / 2 + 0.3, -D / 2 - 0.5, 0.15]) rotate([0, 50, 0]) cylinder(h = 0.3, r = 0.05, $fn = 8);
    }
    cw_m_steel([0.15, 0.24, 0.32]) translate([dx + 0.95, -D / 2 - 0.05, 1.3]) cw_boxc([0.3, 0.1, 0.36]);
    cw_part_decay(seed, L, D, dx, 1.2, fh);
}

// 农舍谷仓：木板墙 + 折线（复斜）屋顶 + 双开大门（可进入）
module cw_bldg_barn(seed = 0, L = 12, D = 8)
{
    wh = 3.4;
    wc = cw_rnd(seed, 2) == 0 ? [0.36, 0.15, 0.10] : [0.33, 0.26, 0.19];
    cw_part_shell(L, D, wh, wc, t = 0.22, dw = 3.2, dh = 3.0, dx = 0,
                  wins = [[2, 0, 2.2, 1.0, 0.9], [3, 0, 2.2, 1.0, 0.9], [1, L * 0.3, 2.3, 0.9, 0.8]],
                  seed = seed, wall = "wood", floor = "none", door = "none", skin = false);
    cw_m_soil([0.30, 0.24, 0.16]) cw_slab(L - 0.44, D - 0.44, 0.008);
    cw_m_hay([0.46, 0.38, 0.20]) for (i = [0 : 4]) translate([cw_rndr(seed + i, -L * 0.35, L * 0.35), cw_rndr(seed + i * 3, -D * 0.1, D * 0.35), 0]) cw_decal(seed + i, 0.6, 0.7, 0.03);
    // 木框架（外露立柱 + 斜撑）
    cw_m_timber(cw_mul(wc, 0.6)) for (i = [0 : 4], sy = [-1, 1]) let(x = -L / 2 + i * L / 4)
        if (!(sy < 0 && abs(x) < 2.0)) translate([x, sy * (D / 2 + 0.06), wh / 2]) cw_boxc([0.16, 0.1, wh]);
    rc = cw_roof_c(seed + 8);
    translate([0, 0, wh]) cw_part_roof(L, D, 1.4, 0.3, 0, rc, "tin");
    translate([0, 0, wh + 1.25]) cw_part_roof(L * 0.98, D * 0.55, 1.3, 0.2, 0, rc, "tin");
    cw_m_planks(wc) for (sx = [-1, 1]) translate([sx * (L / 2 - 0.11), 0, wh]) rotate([90, 0, 90])
        linear_extrude(height = 0.22, center = true) polygon([[-D / 2, 0], [D / 2, 0], [D * 0.275, 1.4], [0, 2.6], [-D * 0.275, 1.4]]);
    // 半开的谷仓门（门洞两侧）+ Z 字撑
    cw_m_planks(cw_mul(wc, 0.85))
    {
        translate([-2.4, -D / 2 - 0.5, 1.5]) rotate([0, 0, 30]) cw_boxc([1.6, 0.06, 3.0]);
        translate([2.3, -D / 2 - 0.1, 1.5]) cw_boxc([1.6, 0.06, 3.0]);
    }
    cw_m_oilpaint(cw_MARKW()) translate([2.3, -D / 2 - 0.14, 1.5]) rotate([0, 45, 0]) cw_boxc([2.0, 0.02, 0.1]);
    cw_m_iron() translate([0, -D / 2 - 0.14, 3.15]) cw_boxc([7, 0.05, 0.06]);
    cw_m_planks(cw_WOODD()) translate([0, -D / 2 - 0.02, wh + 0.6]) cw_boxc([1.2, 0.06, 0.8]);
    cw_m_iron() translate([0, -D / 2 - 0.4, wh + 1.0]) rotate([90, 0, 0]) cylinder(h = 0.6, r = 0.04, center = true, $fn = 6);   // 吊草料杆
    translate([L / 2 + 1.2, -D * 0.2, 0]) cw_prop_haybale(seed);
    cw_part_decay(seed, L, D, 0, 3.2, wh);
}

// 筒仓（混凝土 + 锈顶 + 爬梯护笼）
module cw_bldg_silo(seed = 0, h = 11)
{
    cw_m_concrete(cw_CONC()) cylinder(h = h, r = 2.2, $fn = 24);
    cw_m_concrete(cw_CONCD()) for (z = [h * 0.33, h * 0.66]) translate([0, 0, z]) cylinder(h = 0.2, r = 2.25, $fn = 24);
    cw_m_iron() translate([0, 0, h]) scale([1, 1, 0.45]) sphere(r = 2.25, $fn = 24);
    cw_m_iron() for (i = [0 : floor(h / 0.4)]) translate([0, -2.28, 0.4 + i * 0.4]) cw_boxc([0.4, 0.03, 0.03]);
    cw_m_iron() for (sx = [-1, 1]) translate([sx * 0.2, -2.28, h / 2]) cw_boxc([0.04, 0.04, h]);
    cw_m_iron() for (z = [2.5 : 0.8 : h]) translate([0, -2.45, z]) rotate([90, 0, 0]) difference() { cylinder(h = 0.03, r = 0.4, center = true, $fn = 12); cylinder(h = 0.1, r = 0.37, center = true, $fn = 12); translate([0, -0.4, 0]) cw_boxc([1, 0.8, 0.2]); }
    cw_m_damp([0.22, 0.21, 0.19]) for (a = [30, 150, 260]) rotate([0, 0, a]) translate([2.205, 0, h * 0.6]) cw_boxc([0.01, 0.5, h * 0.7]);
}

// 木工具棚（可进入小门）
module cw_bldg_shed(seed = 0, L = 3.2, D = 2.6)
{
    wc = cw_rnd(seed, 2) == 0 ? cw_WOODC() : [0.32, 0.29, 0.23];
    cw_part_shell(L, D, 2.3, wc, t = 0.12, dw = 0.95, dh = 1.95, dx = L * 0.2, plinth = false,
                  wins = [[2, 0, 1.5, 0.5, 0.4]], seed = seed, wall = "wood", floor = "boards", door = "none", skin = false);
    translate([0, 0, 2.3]) cw_part_roof(L, D, 0.6, 0.25, 0, cw_ROOFM(), cw_rnd(seed, 2) == 0 ? "tin" : "felt");
    cw_m_planks(cw_mul(wc, 0.85)) translate([L * 0.2 - 0.62, -D / 2 - 0.35, 1.0]) rotate([0, 0, 40]) cw_boxc([0.95, 0.04, 1.95]);
    cw_m_iron() translate([L * 0.2 - 0.25, -D / 2 - 0.65, 1.0]) cw_boxc([0.15, 0.03, 0.06]);
    cw_m_timber(cw_WOODD()) translate([-L / 2 - 0.1, D * 0.2, 0.7]) rotate([0, -15, 0]) cylinder(h = 1.4, r = 0.02, center = true, $fn = 5);   // 靠墙的铁锹
    cw_m_steel([0.22, 0.22, 0.20]) translate([-L / 2 - 0.29, D * 0.2, 0.08]) rotate([0, -15, 0]) cw_boxc([0.02, 0.22, 0.28]);
}

// 旱厕（心形门洞）
module cw_bldg_outhouse(seed = 0)
{
    cw_m_planks(cw_WOODC()) difference()
    {
        translate([0, 0, 1.1]) cw_boxc([1.0, 1.0, 2.2]);
        translate([0, 0, 1.1]) cw_boxc([0.85, 0.85, 2.5]);
    }
    cw_m_planks(cw_WOODD())
    {
        translate([0, 0, 2.25]) rotate([-10, 0, 0]) cw_boxc([1.2, 1.3, 0.05]);
        translate([0.3, -0.75, 1.0]) rotate([0, 0, 50]) difference() { cw_boxc([0.75, 0.04, 1.9]); translate([0, 0, 0.7]) scale([1, 1, 1]) for (s = [-1, 1]) translate([s * 0.04, 0, 0]) rotate([0, s * 45, 0]) cw_boxc([0.08, 0.2, 0.08]); }
    }
    cw_m_planks(cw_WOODD()) translate([0, 0.2, 0.25]) cw_boxc([0.85, 0.45, 0.5]);
}

// 车库（卷帘门半开，可进入）：油渍、卸下的机盖、被搬空的工具墙
module cw_bldg_garage(seed = 0, L = 6, D = 7)
{
    wh = 3.0;
    brick = cw_rnd(seed, 2) == 1;
    wc = brick ? cw_BRICKD() : [0.42, 0.41, 0.37];
    cw_part_shell(L, D, wh, wc, t = 0.24, dw = 3.0, dh = 2.5, dx = 0, roomH = wh - 0.3,
                  seed = seed, wall = brick ? "brick" : "plaster", floor = "concrete", door = "none", skin = false);
    cw_m_concrete(cw_CONCD()) translate([0, 0, 0.004]) cw_slab(L - 0.48, D - 0.48, 0.004);
    cw_m_concrete(cw_CONCD()) translate([0, 0, wh]) cw_slab(L + 0.2, D + 0.2, 0.18);
    cw_m_felt([0.15, 0.15, 0.14]) translate([0, 0, wh + 0.18]) cw_slab(L + 0.1, D + 0.1, 0.02);
    // 卷到一半的卷帘 + 卷帘箱 + 导轨
    cw_m_corr([0.36, 0.37, 0.36]) translate([0, -D / 2 - 0.04, 2.55 - 0.35]) cw_boxc([3.0, 0.04, 0.7]);
    cw_m_steel([0.30, 0.31, 0.30]) translate([0, -D / 2 - 0.15, 2.65]) rotate([0, 90, 0]) cylinder(h = 3.2, r = 0.16, center = true, $fn = 12);
    cw_m_steel(cw_YELLOWC()) for (sx = [-1, 1]) translate([sx * 1.56, -D / 2 - 0.05, 1.25]) cw_boxc([0.08, 0.06, 2.5]);
    cw_m_matte(cw_MARKW()) translate([1.9, -D / 2 - 0.012, 2.1]) rotate([90, 0, 0]) linear_extrude(height = 0.006, center = true)
        text(str(1 + cw_rnd(seed, 40)), size = 0.32, halign = "center", valign = "center");
    // 油渍 + 散落零件 + 斜靠墙的机盖（都在两侧，中间车道留空）
    cw_m_oil() translate([0, -D / 2 + 1.5, 0.009]) cw_decal(seed, 0.7, 0.7, 0.003);
    cw_m_oil() translate([0.3, 0.8, 0.009]) cw_decal(seed + 3, 0.5, 0.7, 0.003);
    cw_m_carpaint(cw_car_c(seed)) translate([L / 2 - 0.45, D * 0.014, 0.75]) rotate([0, -15, 90]) cw_boxc([1.3, 0.04, 1.4]);
    cw_m_iron([0.14, 0.14, 0.13]) translate([-L / 2 + 0.6, -D * 0.25, 0]) cw_slab(0.5, 0.6, 0.45);   // 发动机缸体
    cw_m_steel([0.40, 0.08, 0.05]) translate([-L / 2 + 0.5, -D * 0.25 + 0.7, 0]) { cw_slab(0.22, 0.15, 0.45); translate([0, 0, 0.45]) cw_boxc([0.06, 0.06, 0.06]); }   // 千斤顶
    cw_part_decay(seed, L, D, 0, 3.0, wh);
}

// 警察局：白墙 + 蓝色腰带 + POLICE 字样 + 屋顶警灯 + 铁栏窗（可进入）
module cw_bldg_police(seed = 0, L = 12, D = 8)
{
    wh = 3.6;
    wins = [[0, L * 0.2, 1.6, 1.4, 1.1], [0, -L * 0.38, 1.6, 1.2, 1.1],
            [1, -L * 0.25, 1.6, 1.2, 1.0], [1, L * 0.25, 1.6, 1.2, 1.0], [3, 0, 1.6, 1.2, 1.0]];
    cw_part_shell(L, D, wh, cw_WHITEW(), t = 0.28, dw = 1.6, dh = 2.3, dx = -L * 0.15, roomH = wh - 0.35, wins = wins,
                  seed = seed, wall = "plaster", floor = "lino", door = "steel", dc = [0.16, 0.22, 0.30]);
    // 蓝色腰线（门窗处断开）
    cw_m_oilpaint(cw_SIGNB()) for (sy = [-1, 1]) difference()
    {
        translate([0, sy * (D / 2 + 0.012), 2.6]) cw_boxc([L + 0.03, 0.02, 0.32]);
        for (w = wins) if ((w[0] == 0 && sy < 0) || (w[0] == 1 && sy > 0)) translate([w[1], sy * D / 2, w[2]]) cw_boxc([w[3], 0.2, w[4]]);
    }
    cw_part_flatroof(L, D, wh, seed, 0.45, cw_CONCD());
    translate([-L * 0.15, -D / 2 - 0.06, 3.2]) cw_plate("POLICE", 3.0, 0.5, 0.34, cw_SIGNB(), cw_MARKW(), 0.06);
    // 窗洞里的铁栏
    for (w = wins) cw_part_at_hole(L, D, w) cw_m_iron() for (u = [-w[3] / 2 + 0.15 : 0.18 : w[3] / 2 - 0.1]) translate([u, -0.18, 0]) cw_boxc([0.025, 0.025, w[4]]);
    // 屋顶警灯 + 天线
    cw_m_galv() translate([L * 0.3, D * 0.1, wh + 0.03]) cylinder(h = 3.0, r = 0.04, $fn = 6);
    cw_m_plastic(cw_SIGNB()) translate([L * 0.3 - 0.15, D * 0.1, wh + 3.1]) cw_boxc([0.3, 0.2, 0.15]);
    cw_m_plastic(cw_REDC()) translate([L * 0.3 + 0.15, D * 0.1, wh + 3.1]) cw_boxc([0.3, 0.2, 0.15]);
    // 门前：路障护栏 + 通缉 / 宵禁告示
    cw_m_galv(cw_METALC()) for (i = [0 : 1]) translate([L * 0.25 + i * 1.6, -D / 2 - 1.8, 0])
    {
        for (sx = [-1, 1]) translate([sx * 0.7, 0, 0.5]) cylinder(h = 1.0, r = 0.02, center = true, $fn = 5);
        translate([0, 0, 1.0]) rotate([0, 90, 0]) cylinder(h = 1.42, r = 0.02, center = true, $fn = 5);
        for (x = [-0.6 : 0.15 : 0.61]) translate([x, 0, 0.6]) cylinder(h = 0.75, r = 0.008, center = true, $fn = 4);
        for (sx = [-1, 1]) translate([sx * 0.7, 0, 0.01]) cw_boxc([0.04, 0.5, 0.02]);
    }
    translate([L / 2 + 0.6, -D * 0.25, 0]) rotate([0, 0, 90]) cw_prop_notice_board(seed, "CURFEW 20:00");
    cw_part_litter(seed, L - 1, D - 1, 14, cw_rnd(seed + 7, 2) == 0, false);
    cw_part_decay(seed, L, D, -L * 0.15, 1.6, wh);
}

// 加油站便利店：大玻璃面 + 招牌字样（可进入）
module cw_bldg_gas_shop(seed = 0, L = 9, D = 6, msg = "SHOP 24")
{
    wh = 3.4;
    cw_part_shell(L, D, wh, cw_WHITEW(), t = 0.22, dw = 1.4, dh = 2.2, dx = L * 0.28, roomH = wh - 0.3,
                  wins = [[0, -L * 0.15, 1.5, L * 0.42, 1.6], [3, 0, 1.6, 1.2, 1.0]],
                  seed = seed, wall = "plaster", floor = "tiles", door = "wood", dc = [0.30, 0.31, 0.30]);
    cw_m_concrete(cw_CONCD()) translate([0, 0, wh]) cw_slab(L + 0.6, D + 0.6, 0.25);
    cw_m_matte(cw_REDC()) translate([0, -D / 2 - 0.32, wh + 0.12]) cw_boxc([L + 0.6, 0.06, 0.45]);
    cw_m_matte(cw_MARKW()) translate([0, -D / 2 - 0.355, wh + 0.12]) rotate([90, 0, 0]) linear_extrude(height = 0.006, center = true)
        text(msg, size = 0.3, halign = "center", valign = "center");
    cw_m_steel(cw_METALD()) for (i = [0 : 3]) translate([-L / 2 + 0.6 + i * L * 0.14, -D / 2 - 0.02, 1.5]) cw_boxc([0.06, 0.05, 1.6]);
    cw_m_concrete(cw_CONCD()) translate([0, -D / 2 - 0.6, 0]) cw_slab(L, 1.2, 0.02);
    cw_m_paper([0.44, 0.42, 0.34]) translate([-L * 0.3, -D / 2 - 0.135, 1.6]) rotate([0, 5, 0]) cw_boxc([0.4, 0.004, 0.55]);   // 贴在玻璃上的告示
    translate([-L / 2 - 1.0, 0, 0]) rotate([0, 0, 90]) cw_prop_dumpster();
    cw_part_decay(seed, L, D, L * 0.28, 1.4, wh);
}

// 路边汽车旅馆：一排独立客房（每间可进入）+ 前廊
module cw_bldg_motel(seed = 0, rooms = 5, W = 4.0, D = 6)
{
    L = rooms * W;
    for (i = [0 : rooms - 1])
        translate([-L / 2 + W / 2 + i * W, 0, 0])
            cw_part_shell(W, D, 2.9, [[0.48, 0.40, 0.29], [0.40, 0.43, 0.40]][cw_rnd(seed, 2)], t = 0.18, dw = 0.95, dh = 2.1,
                          dx = -W * 0.22, roomH = 2.6, wins = [[0, W * 0.2, 1.5, 1.1, 1.0]], seed = seed + i * 7, wall = "plaster", floor = "lino", door = "wood");
    cw_m_concrete(cw_CONCD()) translate([0, 0, 2.9]) cw_slab(L + 0.2, D + 0.2, 0.16);
    cw_m_tin([0.44, 0.15, 0.10]) translate([0, -D / 2 - 1.1, 2.75]) cw_boxc([L + 0.4, 2.3, 0.05]);
    cw_m_timber(cw_WOODD()) for (i = [0 : rooms]) translate([-L / 2 + i * W, -D / 2 - 2.1, 1.35]) cw_boxc([0.12, 0.12, 2.7]);
    cw_m_concrete(cw_CONC()) translate([0, -D / 2 - 1.1, 0]) cw_slab(L + 0.4, 2.2, 0.02);
    for (i = [0 : rooms - 1])
        translate([-L / 2 + W / 2 + i * W - W * 0.22, -D / 2 - 0.03, 2.35]) cw_plate(str(i + 1), 0.22, 0.16, 0.1, cw_MARKW(), cw_DARKC(), 0.01);
    cw_m_steel(cw_METALD()) translate([L / 2 + 1.5, -D / 2 - 3.0, 2.5]) cylinder(h = 5.0, r = 0.1, center = true, $fn = 8);
    cw_m_matte(cw_REDC()) translate([L / 2 + 1.5, -D / 2 - 3.0, 5.2]) cw_boxc([2.6, 0.2, 0.9]);
    cw_m_matte(cw_MARKW()) translate([L / 2 + 1.5, -D / 2 - 3.105, 5.2]) rotate([90, 0, 0]) linear_extrude(height = 0.006, center = true)
        text("MOTEL", size = 0.45, halign = "center", valign = "center");
}

// 要塞城墙段（沿 x；厚墙 + 顶部步道 + 垛口；front=-y 为外侧）
module cw_bldg_fort_wall(len = 12, h = 5.0, seed = 0)
{
    t = 1.6;
    sc = [0.40, 0.37, 0.33];
    cw_m_stone(sc) hull()
    {
        translate([0, 0, 0.1]) cw_boxc([len, t + 0.5, 0.2]);
        translate([0, 0, h - 0.1]) cw_boxc([len, t, 0.2]);
    }
    cw_m_stone([0.34, 0.32, 0.28])
    {
        translate([0, -t / 2 + 0.2, h + 0.5]) cw_boxc([len, 0.4, 1.0]);
        for (i = [0 : floor(len / 1.6) - 1])
            translate([-len / 2 + 0.8 + i * 1.6, -t / 2 + 0.2, h + 1.25]) cw_boxc([0.8, 0.4, 0.5]);
    }
    cw_m_gravel([0.30, 0.28, 0.25]) translate([0, 0.2, h]) cw_slab(len, t - 0.4, 0.04);
    cw_m_leaf([0.24, 0.30, 0.17]) translate([cw_rndr(seed, -len / 3, len / 3), -t / 2 - 0.22, 0.6]) cw_boxc([len * 0.3, 0.06, 1.0]);
}

// 要塞棱堡塔（八角，带垛口与门洞装饰）
module cw_bldg_fort_bastion(seed = 0, r = 4.0, h = 7.0)
{
    cw_m_stone([0.40, 0.37, 0.33]) cylinder(h = h, r1 = r + 0.6, r2 = r, $fn = 8);
    cw_m_stone([0.34, 0.32, 0.28])
    {
        translate([0, 0, h]) difference()
        {
            cylinder(h = 1.0, r = r + 0.1, $fn = 8);
            translate([0, 0, -0.1]) cylinder(h = 1.3, r = r - 0.5, $fn = 8);
        }
        for (a = [0 : 45 : 315]) rotate([0, 0, a + 22.5]) translate([r - 0.2, 0, h + 1.25]) cw_boxc([0.4, 0.9, 0.5]);
    }
    color(cw_DARKC()) for (a = [0 : 90 : 270]) rotate([0, 0, a]) translate([0, -r - 0.05, h * 0.7]) cw_boxc([0.3, 0.2, 0.9]);
    cw_m_timber(cw_WOODD()) translate([0, -r - 0.32, 1.1]) cw_boxc([1.3, 0.1, 2.2]);
    cw_m_galv() translate([0, 0, h + 1.0]) cylinder(h = 3.5, r = 0.05, $fn = 6);
    cw_m_fabric(cw_REDC()) translate([0.45, 0, h + 4.2]) cw_boxc([0.9, 0.02, 0.6]);
}

// 要塞门楼（门洞 4m，可通行；沿 x 接城墙）
module cw_bldg_fort_gate(seed = 0, h = 6.5)
{
    for (sx = [-1, 1]) cw_m_stone([0.40, 0.37, 0.33]) translate([sx * 3.2, 0, h / 2]) cw_boxc([2.4, 2.4, h]);
    cw_m_stone([0.36, 0.34, 0.30]) translate([0, 0, h - 0.9]) cw_boxc([8.8, 2.4, 1.8]);
    cw_m_stone([0.34, 0.32, 0.28]) for (i = [0 : 4]) translate([-3.6 + i * 1.8, -1.0, h + 0.3]) cw_boxc([0.9, 0.4, 0.6]);
    translate([0, -1.22, h - 0.9]) cw_plate("1887", 2.6, 0.7, 0.4, cw_MARKW(), cw_DARKC(), 0.04);
    cw_m_iron() translate([-1.4, -1.6, 2.0]) rotate([0, 0, -40]) { cw_boxc([2.0, 0.08, 4.0]); for (x = [-0.8 : 0.4 : 0.81]) translate([x, -0.05, 0]) cw_boxc([0.06, 0.06, 4.0]); }   // 被撞开的铁门
}

// 小火车站：砖站房 + 站台 + 站名牌（可进入）
module cw_bldg_station(seed = 0, L = 14, D = 7, msg = "RIVERLAND")
{
    wh = 4.0;
    cw_part_shell(L, D, wh, [0.46, 0.36, 0.24], t = 0.3, dw = 1.6, dh = 2.5, dx = 0, roomH = wh - 0.4,
                  wins = [[0, -L * 0.3, 1.8, 1.2, 1.6], [0, L * 0.3, 1.8, 1.2, 1.6],
                          [1, -L * 0.3, 1.8, 1.2, 1.6], [1, 0, 1.8, 1.4, 1.8], [1, L * 0.3, 1.8, 1.2, 1.6]],
                  seed = seed, wall = "plaster", floor = "tiles", door = "wood", dc = [0.24, 0.16, 0.10]);
    cw_m_brick(cw_BRICK()) for (sy = [-1, 1], x = [-L / 2 + 0.25, L / 2 - 0.25]) translate([x, sy * (D / 2 + 0.03), wh / 2]) cw_boxc([0.5, 0.06, wh]);
    translate([0, 0, wh]) cw_part_roof(L, D, 2.0, 0.6, 1.2, cw_ROOFR(), "tile");
    cw_m_concrete(cw_CONC()) translate([0, D / 2 + 2.0, 0]) cw_slab(L + 8, 4.0, 0.9);
    cw_m_matte(cw_YELLOWC()) translate([0, D / 2 + 3.85, 0.9]) cw_boxc([L + 8, 0.2, 0.006]);
    cw_m_tin([0.34, 0.17, 0.10]) translate([0, D / 2 + 1.6, 3.4]) cw_boxc([L + 2, 2.6, 0.05]);
    cw_m_iron() for (i = [0 : 4]) translate([-L / 2 - 0.5 + i * (L + 1) / 4, D / 2 + 2.6, 2.15]) cylinder(h = 2.5, r = 0.08, center = true, $fn = 8);
    translate([0, -D / 2 - 0.06, wh - 0.6]) cw_plate(msg, 4.6, 0.6, 0.36, cw_SIGNB(), cw_MARKW(), 0.06);
    // 站钟（停在某个时刻）
    cw_m_matte(cw_MARKW()) translate([L * 0.3, -D / 2 - 0.08, wh + 1.0]) rotate([90, 0, 0]) cylinder(h = 0.05, r = 0.4, $fn = 24);
    cw_m_matte(cw_DARKC()) translate([L * 0.3, -D / 2 - 0.11, wh + 1.0]) { rotate([0, 50, 0]) translate([0, 0, 0.13]) cw_boxc([0.03, 0.01, 0.26]); rotate([0, 160, 0]) translate([0, 0, 0.17]) cw_boxc([0.02, 0.01, 0.34]); }
    for (x = [-L / 2 - 2.5, L / 2 + 2.5]) translate([x, D / 2 + 2.6, 0.9]) cw_prop_bench();
    cw_part_decay(seed, L, D, 0, 1.6, wh);
}

// 狩猎高台（木架 + 小棚）
module cw_bldg_hunting_stand(seed = 0, h = 3.6)
{
    cw_m_bark(cw_TRUNKC()) for (sx = [-1, 1], sy = [-1, 1])
        translate([sx * 0.8, sy * 0.8, h / 2]) rotate([sy * 3, -sx * 3, 0]) cylinder(h = h + 0.3, r = 0.07, center = true, $fn = 7);
    cw_m_planks(cw_WOODC())
    {
        translate([0, 0, h]) cw_slab(1.9, 1.9, 0.06);
        for (a = [0, 90, 180, 270]) rotate([0, 0, a]) translate([0, -0.92, h + 0.45]) cw_boxc([1.9, 0.04, 0.9]);
    }
    translate([0, 0, h + 1.7]) cw_part_roof(2.0, 2.0, 0.5, 0.2, 0, [0.16, 0.16, 0.15], "felt", false);
    cw_m_bark(cw_TRUNKC()) for (sx = [-1, 1], sy = [-1, 1]) translate([sx * 0.88, sy * 0.88, h + 0.85]) cylinder(h = 1.7, r = 0.05, center = true, $fn = 6);
    cw_m_bark(cw_TRUNKC())
    {
        for (sx = [-1, 1]) translate([sx * 0.25, -1.6, h / 2]) rotate([-20, 0, 0]) cylinder(h = h + 0.4, r = 0.04, center = true, $fn = 6);
        for (i = [0 : floor(h / 0.4)]) translate([0, -1.0 - i * 0.14, 0.3 + i * 0.38]) rotate([0, 90, 0]) cylinder(h = 0.55, r = 0.025, center = true, $fn = 5);
    }
}

// 锯木场敞棚（立柱 + 单坡顶 + 锯台）
module cw_bldg_lumber_shed(seed = 0, L = 12, D = 6)
{
    cw_m_timber(cw_WOODD()) for (i = [0 : 3], sy = [-1, 1])
        translate([-L / 2 + 0.2 + i * (L - 0.4) / 3, sy * (D / 2 - 0.2), sy > 0 ? 2.2 : 1.7]) cw_boxc([0.22, 0.22, sy > 0 ? 4.4 : 3.4]);
    cw_m_tin(cw_ROOFM()) translate([0, 0, 3.9]) rotate([-9, 0, 0]) cw_boxc([L + 0.8, D + 1.0, 0.05]);
    cw_m_steel([0.28, 0.30, 0.26]) translate([0, 0, 0.45]) cw_boxc([3.5, 1.0, 0.9]);
    cw_m_galv(cw_METALC()) translate([0, -0.1, 1.0]) rotate([90, 0, 0]) cylinder(h = 0.02, r = 0.5, center = true, $fn = 24);
    cw_m_bark(cw_TRUNKC()) translate([-1.0, 0, 1.15]) rotate([0, 90, 0]) cylinder(h = 3.0, r = 0.25, center = true, $fn = 10);
    cw_m_hay([0.48, 0.40, 0.25]) translate([1.5, 1.4, 0]) scale([1.6, 1, 0.35]) sphere(r = 0.8, $fn = 10);   // 木屑堆
    translate([-L * 0.25, -D / 2 - 1.5, 0]) cw_prop_log_pile(seed, 4);
}

// 乡村教堂（大）：中殿可进入 + 钟楼 + 洋葱顶
module cw_bldg_church(seed = 0, L = 14, D = 8)
{
    wh = 5.0;
    cw_part_shell(L, D, wh, cw_WHITEW(), t = 0.4, dw = 1.6, dh = 2.8, dx = -L * 0.32,
                  wins = [for (i = [0 : 2]) [1, -L * 0.25 + i * L * 0.25, 3.0, 0.8, 1.8],
                          for (i = [1 : 2]) [0, -L * 0.25 + i * L * 0.25, 3.0, 0.8, 1.8],
                          [3, 0, 3.2, 1.0, 1.8]],
                  seed = seed, wall = "plaster", floor = "tiles", door = "wood", dc = [0.26, 0.15, 0.09]);
    translate([0, 0, wh]) cw_part_roof(L, D, 2.6, 0.4, 0, cw_ROOFM(), "tin");
    cw_m_plaster(cw_WHITEW()) for (sx = [-1, 1]) translate([sx * (L / 2 - 0.2), 0, wh]) rotate([90, 0, 90])
        linear_extrude(height = 0.4, center = true) polygon([[-D / 2, 0], [D / 2, 0], [0, 2.58]]);
    // 钟楼（入口侧，实心）
    translate([-L / 2 - 1.8, 0, 0])
    {
        cw_m_plaster(cw_WHITEW()) translate([0, 0, 5.0]) cw_boxc([3.6, 3.6, 10.0]);
        cw_m_plaster(cw_mul(cw_WHITEW(), 0.85)) for (z = [3.4, 7.2]) translate([0, 0, z]) cw_boxc([3.7, 3.7, 0.2]);
        color(cw_DARKC()) for (a = [0, 90, 180, 270]) rotate([0, 0, a]) translate([0, -1.81, 8.4]) cw_boxc([0.9, 0.04, 1.6]);
        cw_m_planks(cw_WOODD()) for (a = [0, 90, 180, 270]) rotate([0, 0, a]) for (z = [7.8 : 0.3 : 9.0]) translate([0, -1.8, z]) rotate([30, 0, 0]) cw_boxc([0.85, 0.02, 0.22]);
        cw_m_zinc([0.36, 0.28, 0.12])
        {
            translate([0, 0, 10.0]) cylinder(h = 0.8, r = 1.6, $fn = 12);
            translate([0, 0, 11.4]) scale([1, 1, 1.15]) sphere(r = 1.4, $fn = 14);
            translate([0, 0, 12.4]) cylinder(h = 1.4, r1 = 0.9, r2 = 0.05, $fn = 12);
        }
        gk_material([0.55, 0.42, 0.17], roughness = 0.25, metalness = 1)
        {
            translate([0, 0, 14.2]) cw_boxc([0.08, 0.08, 0.9]);
            translate([0, 0, 14.4]) cw_boxc([0.5, 0.08, 0.08]);
        }
    }
    cw_m_concrete(cw_CONCD()) translate([-L * 0.32, -D / 2 - 0.6, 0]) cw_slab(2.4, 1.2, 0.02);
    cw_part_decay(seed, L, D, -L * 0.32, 1.6, wh);
}
