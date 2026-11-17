// ============================================================================
// gk_texgraph.scad - 用 SCAD 编写过程纹理（纹理节点图），不改 C++ 就能加新印花
//
// gk_texture() 的第一个参数可以是印花名（"brick"、"woodland"…，即本文件下半部分的
// tex_<名字>()），也可以直接是一张节点图：这些函数只返回列表数据（["fbm", 4, 4, 3, 11] 之类），加载器把整棵树
// 编译一次、在 C++ 里逐 texel 求值——写法在 SCAD，速度是原生的。根节点
// tx_surface() 给出四个通道：颜色、起伏高度（烘成法线贴图）、粗糙度系数、
// 金属度系数（烘成 MRA，乘在 gk_material 上）。
//
//   use <../lib/gk_texgraph.scad>
//   gk_material([1,1,1], roughness = 0.8, metalness = 0.9)
//       gk_texture("corrugated", scale = 1.2) cube([2, 0.02, 1]);     // 印花名
//   gk_texture(tex_brickwall(rows = 12), scale = 1) ...                 // 带参数的印花
//   gk_texture(tx_surface(tx_mul(tx_fbm(6), [0.4, 0.3, 0.2]))) ...      // 现写一张图
//
// 规则：
// - 每个节点都求值成 RGB；标量场三个分量相同，数字与 [r,g,b] 常量自动广播，
//   所以颜色和标量可以混算（tx_mul(tx_palette(0), shade)）。
// - 网格数（cells / rows / repeats …）是整数，保证印花无缝平铺。
// - seed 是节点自己的种子；gk_texture(seed = n) 会把整张图一起重新随机。
// - tx_palette(i, 默认色) 让同一张图能被 gk_texture(palette = [...]) 换色。
// - 起伏只给块面加颗粒：tilt 是最陡 10% 像素的坡度（度），保持 4~15 度，
//   造型交给低模块面（见 docs/AGENT_GUIDE/SCADLoader.md）。
// - 写错的图会在加载时告警（指出哪个节点、期望的参数），几何照常无印花加载。
// ============================================================================

// ---- 场（0..1）----
function tx_u() = ["u"];
function tx_v() = ["v"];
// 周期 value-noise fBm：cells_v 少于 cells 时特征沿 u 拉长（反之沿 v）。
function tx_fbm(cells = 4, cells_v = undef, octaves = 4, seed = 0) =
    ["fbm", cells, cells_v == undef ? cells : cells_v, octaves, seed];
// Voronoi 细胞：mode "dist" 到中心距离，"edge" 到边界距离（0 在缝上），"rand" 每格一个随机值。
function tx_cells(count = 8, seed = 0, mode = "edge") = ["cells", count, seed, mode];
// 砌块行列（砖、瓦、条石）：shift 为隔行错缝（<0 随机错缝）；mode "fu"/"fv" 单元内坐标，
// "edge" 到最近缝的距离（单元高为 1），"rand" 每块随机值，"row"/"col" 行列序号（0..1）。
function tx_course(rows = 8, columns = 4, shift = 0.5, seed = 0, mode = "rand") =
    ["course", rows, columns, shift, seed, mode];
function tx_weave(threads = 48, twill = false) = ["weave", threads, twill ? 1 : 0];
// 锯齿条纹 fract(u*nu + v*nv)：nu、nv 是整数，(n,0) 竖条，(0,n) 横条，(n,n) 斜条。
function tx_stripes(nu = 8, nv = 0) = ["stripes", nu, nv];
function tx_checker(nu = 8, nv = undef) = ["checker", nu, nv == undef ? nu : nv];
// 每格一个随机值的块状噪声。
function tx_hash(cells = 32, cells_v = undef, seed = 0) = ["hash", cells, cells_v == undef ? cells : cells_v, seed];
function tx_palette(index, default = [1, 1, 1]) = ["palette", index, default];

// ---- 运算 ----
function tx_abs(a) = ["abs", a];
function tx_fract(a) = ["fract", a];
function tx_invert(a) = ["invert", a];
function tx_posterize(a, levels = 4) = ["posterize", a, levels];
function tx_smoothstep(a, e0, e1) = ["smoothstep", a, e0, e1];
function tx_step(a, edge = 0.5) = ["step", a, edge];
// 1 - |a - center| / half：在 center 处为 1 的三角形带（画线、脊、缝）。
function tx_band(a, center = 0.5, half = 0.1) = ["band", a, center, half];
function tx_pow(a, e) = ["pow", a, e];
function _tx_n(op, items) = concat([op], [for (i = items) if (i != undef) i]);
function tx_add(a, b, c = undef, d = undef) = _tx_n("add", [a, b, c, d]);
function tx_mul(a, b, c = undef, d = undef) = _tx_n("mul", [a, b, c, d]);
function tx_min(a, b, c = undef, d = undef) = _tx_n("min", [a, b, c, d]);
function tx_max(a, b, c = undef, d = undef) = _tx_n("max", [a, b, c, d]);
function tx_sub(a, b) = ["sub", a, b];
function tx_mix(a, b, t) = ["mix", a, b, t];
// mask > 0.5 取 a，否则取 b。
function tx_select(mask, a, b) = ["select", mask, a, b];
// 整张图按排名切：field 最高的 fraction 比例为 1，其余为 0（覆盖率精确，与噪声分布无关）。
function tx_top(field, fraction) = ["top", field, fraction];

// ---- 域变换（都保持无缝）----
function tx_warp(field, du, dv, amount = 0.1) = ["warp", field, du, dv, amount];
function tx_tile(field, nu = 2, nv = undef) = ["tile", field, nu, nv == undef ? nu : nv];
function tx_shear(field, k = 1) = ["shear", field, k];
function tx_pixelate(field, cells = 64) = ["pixelate", field, cells];
function tx_offset(field, du = 0, dv = 0) = ["offset", field, du, dv];

// ---- 根 ----
function tx_surface(color, height = 0.5, rough = 1, metal = 1, tilt = 10, blur = 1) =
    ["surface", color, height, rough, metal, tilt, blur];

// ============================================================================
// 印花库：function tex_<名字>() 就是 gk_texture("<名字>") —— 加载器按名字在本文件里找
// tex_<名字>() 求值成节点图（编译一次、缓存，文件改了自动重读）。往这里加一个
// tex_foo() 就多一个 gk_texture("foo")，不用改 C++。也可以直接
// gk_texture(tex_brickwall(rows = 12)) 传参数用。
// 色板约定：tx_palette(i, 默认色) 的 i 就是 gk_texture(palette = [...]) 的下标。
// - 迷彩：默认色板是历史配色，外层材质用白色。
// - 布纹、皮纹、地表、建筑：默认白色（只调明暗），乘在外层本色上；第二色是剥落、苔、干草等点缀。
// - chipped / rust / worn / corrugated：色板是绝对颜色（漆和钢差在金属度），外层材质用白色。
// ============================================================================

// ---- 小工具 ----
function _lt(f, edge) = tx_step(tx_invert(f), 1 - edge);          // f < edge
function _gt(f, edge) = tx_step(f, edge);                         // f >= edge
function _sh(base, amount, f) = tx_add(base, tx_mul(f, amount));  // base + amount * f
function _clamp01(f) = tx_min(tx_max(f, 0), 1);
// 迷彩印在斜纹布上：一层很淡的织纹明暗与起伏。
function _printed(c) = tx_mul(c, _sh(0.92, 0.08, tx_weave(64, true)));
function _cloth(c) = tx_surface(_printed(c), height = tx_weave(64, true), tilt = 8, blur = 0);

// ---- 迷彩 ----
// M81 林地：大块绿 / 棕斑 + 黑色枝条，整体做轻微域扭曲。
function _camo_warp(f, wu, wv) = tx_warp(f, wu, wv, 0.14);
function tex_woodland() =
    let(wu = tx_fbm(4, 4, 3, 3), wv = tx_fbm(4, 4, 3, 7),
        green = tx_top(_camo_warp(tx_fbm(3, 3, 4, 11), wu, wv), 0.58),
        brown = tx_top(_camo_warp(tx_fbm(3, 4, 4, 23), wu, wv), 0.34),
        black = tx_top(_camo_warp(tx_add(tx_mul(tx_band(tx_fbm(5, 5, 4, 31), 0.5, 0.25), 0.6),
                                         tx_mul(tx_fbm(4, 4, 3, 37), 0.4)), wu, wv), 0.09))
    _cloth(tx_select(black, tx_palette(3, [0.045, 0.045, 0.04]),
           tx_select(brown, tx_palette(2, [0.24, 0.17, 0.11]),
           tx_select(green, tx_palette(1, [0.15, 0.20, 0.12]), tx_palette(0, [0.36, 0.35, 0.23])))));

// 俄式"数码植物"：同样的斑块在 64 格像素网格上求值，再叠一层细抖动，斑块偏竖向。
function _digi(f, wu, wv) = tx_pixelate(_camo_warp(f, wu, wv), 64);
function _jitter(du, dv) = tx_mul(tx_sub(tx_offset(tx_hash(128, 128, 99), du, dv), 0.5), 0.10);
function tex_digital() =
    let(wu = tx_fbm(4, 4, 3, 3), wv = tx_fbm(4, 4, 3, 7),
        green = tx_top(tx_add(_digi(tx_fbm(6, 3, 4, 11), wu, wv), _jitter(0, 0)), 0.58),
        brown = tx_top(tx_add(_digi(tx_fbm(7, 3, 4, 23), wu, wv), _jitter(0.37, 0)), 0.30),
        black = tx_top(tx_add(_digi(tx_add(tx_mul(tx_band(tx_fbm(5, 5, 4, 31), 0.5, 0.25), 0.6),
                                           tx_mul(tx_fbm(4, 4, 3, 37), 0.4)), wu, wv), _jitter(0, 0.61)), 0.10))
    _cloth(tx_select(black, tx_palette(3, [0.05, 0.05, 0.045]),
           tx_select(brown, tx_palette(2, [0.21, 0.16, 0.11]),
           tx_select(green, tx_palette(1, [0.16, 0.21, 0.13]), tx_palette(0, [0.34, 0.36, 0.29])))));

// DBDU 六色"巧克力片"：卡其 / 棕色波带、深棕芯，黑色"石子"旁边落一块白点。
function tex_desert() =
    let(khaki = tx_warp(tx_fbm(2, 3, 4, 13), tx_fbm(3, 3, 2, 5), 0.5, 0.2),
        brown = tx_warp(tx_fbm(3, 2, 4, 17), 0.5, tx_fbm(3, 3, 2, 9), 0.2),
        rand = tx_cells(9, 41, "rand"),
        radius = _sh(0.09, 0.09, rand),
        present = _lt(tx_fract(tx_mul(rand, 7.13)), 0.62),
        rock = tx_warp(tx_mul(present, tx_step(tx_sub(radius, tx_cells(9, 41, "dist")), 0)),
                       tx_fbm(48, 48, 1, 43), tx_fbm(48, 48, 1, 44), 0.012),
        chip = tx_offset(rock, -0.014, 0.009))
    _cloth(tx_select(rock, tx_palette(4, [0.05, 0.05, 0.05]),
           tx_select(chip, tx_palette(5, [0.82, 0.81, 0.78]),
           tx_select(tx_top(brown, 0.09), tx_palette(3, [0.24, 0.17, 0.12]),
           tx_select(tx_top(brown, 0.30), tx_palette(2, [0.40, 0.29, 0.19]),
           tx_select(tx_top(khaki, 0.32), tx_palette(1, [0.45, 0.43, 0.31]), tx_palette(0, [0.60, 0.50, 0.38])))))));

// VSR-98 "西瓜皮"：竖向波浪绿条 + 棕色碎点。
function tex_flora() =
    let(green = tx_top(tx_warp(tx_fbm(8, 2, 4, 11), tx_fbm(2, 6, 2, 3), 0.5, 0.10), 0.42),
        brown = tx_top(tx_warp(tx_fbm(10, 3, 4, 19), tx_fbm(2, 6, 2, 5), 0.5, 0.10), 0.17))
    _cloth(tx_select(brown, tx_palette(2, [0.23, 0.18, 0.12]),
           tx_select(green, tx_palette(1, [0.13, 0.20, 0.12]), tx_palette(0, [0.31, 0.34, 0.27]))));

// 东德雨点纹：淡斑底上，46 列竖向短线，每列横向错位、断续出现。
function tex_rain() =
    let(column = _gt(tx_band(tx_stripes(46, 0), 0.5, 0.5), 0.58),
        dash = _lt(tx_warp(tx_hash(46, 30, 52), 0.5, tx_hash(46, 1, 53), 1 / 30), 0.55),
        stroke = tx_warp(tx_mul(column, dash), tx_hash(46, 1, 51), 0.5, 0.5 / 46),
        mottle = _sh(0.95, 0.10, tx_fbm(6, 6, 3, 61)))
    _cloth(tx_select(stroke, tx_palette(1, [0.25, 0.19, 0.13]), tx_mul(tx_palette(0, [0.37, 0.38, 0.30]), mottle)));

// 迷彩（纯 SCAD 新增）：越战虎纹、碎片迷彩。
function tex_tigerstripe() =
    let(black = tx_top(tx_warp(tx_fbm(2, 10, 4, 0), tx_fbm(3, 3, 2, 7), tx_fbm(3, 3, 2, 9), 0.15), 0.30),
        brown = tx_top(tx_fbm(3, 9, 4, 1), 0.28))
    _cloth(tx_select(black, tx_palette(2, [0.04, 0.04, 0.035]),
           tx_select(brown, tx_palette(1, [0.20, 0.15, 0.09]), tx_palette(0, [0.30, 0.33, 0.20]))));
function tex_splinter() =
    let(green = _gt(tx_shear(tx_cells(7, 0, "rand"), 1), 0.55),
        brown = _gt(tx_shear(tx_cells(6, 1, "rand"), -1), 0.70),
        rain = tx_mul(tx_band(tx_stripes(56, 0), 0.5, 0.18), _gt(tx_hash(56, 28, 2), 0.55)))
    _cloth(tx_select(rain, tx_palette(3, [0.08, 0.10, 0.06]),
           tx_select(brown, tx_palette(2, [0.24, 0.18, 0.11]),
           tx_select(green, tx_palette(1, [0.16, 0.22, 0.12]), tx_palette(0, [0.42, 0.40, 0.30])))));

// ---- 布与皮 ----
// 帆布 / 斜纹：纱线顶部亮、缝隙暗，加一层慢变的粗细不匀；起伏就是同一张织纹。
function _woven(threads, twill, tilt) =
    let(relief = tx_weave(threads, twill), slub = _sh(0.93, 0.14, tx_fbm(12, 12, 3, 71)))
    tx_surface(tx_mul(tx_palette(0, [1, 1, 1]), _sh(0.80, 0.20, relief), slub),
               height = tx_add(tx_mul(relief, 0.8), tx_mul(slub, 0.6)), tilt = tilt, blur = 0);
function tex_canvas() = _woven(48, false, 12);
function tex_twill() = _woven(64, true, 10);

// 皮革：柔和斑驳、零星毛孔、褶皱网；褶皱之间的面被磨得更光滑。
function tex_grain() =
    let(mottle = tx_fbm(20, 20, 3, 81),
        pore = _gt(tx_hash(256, 256, 83), 0.94),
        fold = tx_min(tx_mul(tx_cells(18, 85, "edge"), 10), 1))
    tx_surface(tx_mul(tx_palette(0, [1, 1, 1]), tx_sub(_sh(0.86, 0.20, mottle), tx_mul(pore, 0.07)), _sh(0.94, 0.06, fold)),
               height = tx_add(0.35, tx_mul(fold, 0.45), tx_mul(mottle, 0.2)),
               rough = tx_select(pore, 1, _sh(0.70, 0.30, tx_invert(fold))), tilt = 12, blur = 1);

// ---- 地表（乘在 biome 本色上：低对比、色阶化）----
function tex_meadow() =
    let(mottle = tx_posterize(tx_fbm(4, 4, 4, 91), 4),
        shade = tx_mul(_sh(0.84, 0.20, mottle), tx_select(_gt(tx_fbm(20, 20, 2, 93), 0.64), 0.90, 1)),
        dry = _gt(tx_fbm(3, 3, 3, 95), 0.66),
        tuft = tx_max(tx_sub(1, tx_mul(tx_cells(24, 97, "dist"), 1 / 0.65)), 0))
    tx_surface(tx_mul(tx_select(dry, tx_palette(1, [1.0, 0.93, 0.72]), tx_palette(0, [1, 1, 1])), tx_min(shade, 1)),
               height = tx_add(tx_mul(mottle, 0.4), tx_mul(tx_posterize(tuft, 3), 0.6)), tilt = 8, blur = 2);

function tex_dirt() =
    let(layer = tx_posterize(tx_fbm(6, 6, 4, 103), 3),
        dist = tx_cells(40, 105, "dist"),
        clod = tx_select(_lt(dist, 0.22), tx_select(_gt(tx_cells(40, 105, "rand"), 0.5), 0.86, 1.06), 1))
    tx_surface(tx_mul(tx_palette(0, [1, 1, 1]), tx_min(tx_mul(_sh(0.84, 0.18, layer), clod), 1)),
               height = tx_add(tx_mul(layer, 0.45), tx_mul(tx_max(tx_sub(1, tx_mul(dist, dist, 1.6)), 0), 0.55)),
               tilt = 9, blur = 1);

// 岩石：横向岩层（侧面投影 v 沿 +Z）是台阶状的，加裂隙。
function tex_rock() =
    let(layer = tx_posterize(tx_fbm(2, 9, 4, 101), 4),
        crack = _lt(tx_cells(10, 105, "edge"), 0.06))
    tx_surface(tx_mul(tx_palette(0, [1, 1, 1]), tx_min(tx_mul(_sh(0.80, 0.22, layer), tx_select(crack, 0.72, 1)), 1)),
               height = tx_mul(layer, tx_select(crack, 0.5, 1)), tilt = 15, blur = 2);

function tex_gravel() =
    let(dist = tx_cells(36, 111, "dist"),
        gap = _lt(tx_cells(36, 111, "edge"), 0.10),
        dome = tx_pow(_clamp01(tx_sub(1, tx_mul(dist, dist, 1.6))), 0.5))
    tx_surface(tx_mul(tx_palette(0, [1, 1, 1]), tx_select(gap, 0.70, _sh(0.80, 0.20, tx_posterize(tx_cells(36, 111, "rand"), 3)))),
               height = tx_select(gap, 0.1, _sh(0.35, 0.65, dome)), rough = tx_select(gap, 1, 0.85), tilt = 11, blur = 1);

// 乡村旧沥青：细骨料斑点、更暗更光滑（且略凸）的补丁、发丝裂缝。
function tex_asphalt() =
    let(patched = tx_top(tx_fbm(3, 3, 3, 121), 0.14),
        swell = tx_posterize(tx_fbm(8, 8, 2, 123), 2),
        speck = tx_hash(256, 256, 125),
        crack = _gt(tx_band(tx_fbm(5, 5, 3, 127), 0.5, 0.006), 0.0001),
        shade = tx_mul(_sh(0.92, 0.08, swell),
                       tx_select(_gt(speck, 0.90), 0.88, tx_select(_lt(speck, 0.04), 1.08, 1)),
                       tx_select(patched, 0.84, 1), tx_select(crack, 0.70, 1)))
    tx_surface(tx_mul(tx_palette(0, [1, 1, 1]), tx_min(shade, 1)),
               height = tx_select(crack, 0.1, tx_add(tx_select(patched, 0.75, 0.5), tx_mul(swell, 0.1))),
               rough = tx_select(patched, 0.9, 1), tilt = 5, blur = 1);

// ---- 建筑（乘在本色上）----
// 石灰抹灰：柔和污斑、竖向雨痕、局部剥落露出砖色（palette[1]），剥落处凹下、边缘翘起。
function tex_plaster() =
    let(peelField = tx_fbm(5, 5, 4, 131),
        peel = tx_top(peelField, 0.035),
        rim = tx_top(peelField, 0.055),
        blotch = tx_posterize(tx_fbm(3, 3, 3, 133), 3),
        streak = _gt(tx_fbm(14, 2, 3, 135), 0.62),
        chip = tx_hash(42, 85, 137))
    tx_surface(tx_select(peel, tx_mul(tx_palette(1, [0.90, 0.68, 0.56]), _sh(0.90, 0.10, chip)),
               tx_select(rim, tx_palette(0, [1, 1, 1]),
                         tx_mul(tx_palette(0, [1, 1, 1]), _sh(0.90, 0.10, blotch), tx_select(streak, 0.92, 1)))),
               height = tx_select(peel, _sh(0.2, 0.1, chip), tx_select(rim, 0.95, _sh(0.75, 0.1, blotch))),
               tilt = 7, blur = 1);

// 顺砖错缝：砖面凸出、灰缝下凹，每块一个色调。
function tex_brick() =
    let(tone = tx_course(16, 5, 0.5, 143, "rand"),
        joint = tx_max(_lt(tx_course(16, 5, 0.5, 143, "fu"), 0.06), _lt(tx_course(16, 5, 0.5, 143, "fv"), 0.16)))
    tx_surface(tx_mul(tx_palette(0, [1, 1, 1]), tx_select(joint, 0.62, _sh(0.80, 0.20, tx_posterize(tone, 4)))),
               height = tx_select(joint, 0.1, _sh(0.8, 0.2, tone)), tilt = 14, blur = 1);

// 参数化砖墙：行列、灰缝宽（单元高为 1）可调。
function tex_brickwall(rows = 16, columns = 5, joint = 0.07) =
    let(mortar = _lt(tx_course(rows, columns, 0.5, 0, "edge"), joint),
        tone = tx_posterize(tx_course(rows, columns, 0.5, 0, "rand"), 4))
    tx_surface(tx_mul(tx_palette(0, [1, 1, 1]), tx_select(mortar, 0.62, _sh(0.8, 0.2, tone))),
               height = tx_select(mortar, 0.1, _sh(0.8, 0.2, tone)), tilt = 14, blur = 1);

// 陶瓦：每行向上逐渐沉到上一行下面（楔形台阶），下沿有阴影，苔藓（palette[1]）。
function tex_tiles() =
    let(fv = tx_course(10, 14, 0.5, 143, "fv"),
        side = _lt(tx_course(10, 14, 0.5, 143, "fu"), 0.05),
        grown = tx_top(tx_fbm(4, 4, 4, 141), 0.12),
        shade = tx_mul(tx_select(side, 0.58, _sh(0.80, 0.20, tx_posterize(tx_course(10, 14, 0.5, 143, "rand"), 3))),
                       tx_select(_lt(fv, 0.18), 0.74, 1)))
    tx_surface(tx_mul(tx_palette(0, [1, 1, 1]), shade, tx_select(grown, tx_palette(1, [0.74, 0.90, 0.62]), 1)),
               height = tx_select(side, 0.35, tx_sub(1, tx_mul(fv, 0.75))),
               rough = tx_select(grown, 1, 0.8), tilt = 12, blur = 1);

// 乱石砌：随机错缝、石面鼓起（缝处为 0），石面比缝隙光滑，地衣（palette[1]）。
function tex_stone() =
    let(tone = tx_course(8, 4, -1, 143, "rand"),
        joint = tx_max(_lt(tx_course(8, 4, -1, 143, "fu"), 0.07), _lt(tx_course(8, 4, -1, 143, "fv"), 0.09)),
        grown = tx_top(tx_fbm(4, 4, 4, 141), 0.06),
        cushion = tx_pow(_clamp01(tx_mul(tx_sub(tx_course(8, 4, -1, 143, "edge"), 0.08), 1 / 0.22)), 0.5))
    tx_surface(tx_mul(tx_palette(0, [1, 1, 1]), tx_select(joint, 0.58, _sh(0.80, 0.20, tx_posterize(tone, 3))),
                      tx_select(grown, tx_palette(1, [0.90, 0.95, 0.76]), 1)),
               height = tx_select(joint, 0.1, _sh(0.1, 0.9, tx_mul(cushion, _sh(0.85, 0.15, tone)))),
               rough = tx_select(tx_max(joint, grown), 1, _sh(0.95, -0.2, cushion)), tilt = 13, blur = 2);

// 竖板：每块一个色调、深色板缝、顺板纹理、节疤，板面微微下凹。
function tex_planks() =
    let(fu = tx_offset(tx_stripes(8, 0), 1 / 16, 0),
        tone = tx_posterize(tx_offset(tx_hash(8, 1, 151), 1 / 16, 0), 3),
        streak = _gt(tx_fbm(48, 3, 2, 153), 0.6),
        gap = _lt(fu, 0.07),
        knot = tx_mul(_lt(tx_cells(6, 155, "dist"), 0.06), _gt(tx_cells(6, 155, "rand"), 0.6)),
        across = tx_mul(tx_sub(fu, 0.535), 1 / 0.465))
    tx_surface(tx_mul(tx_palette(0, [1, 1, 1]),
                      tx_select(gap, 0.55, tx_mul(_sh(0.80, 0.18, tone), tx_select(streak, 0.93, 1), tx_select(knot, 0.75, 1)))),
               height = tx_select(gap, 0.05, tx_sub(_sh(0.8, 0.1, tone),
                                                     tx_add(tx_mul(across, across, 0.25), tx_mul(streak, 0.06), tx_mul(knot, 0.2)))),
               tilt = 10, blur = 1);

// 现浇混凝土：斑驳、模板线、流痕、气孔。
function tex_concrete() =
    let(mottle = tx_posterize(tx_fbm(5, 5, 3, 161), 3),
        line = _lt(tx_offset(tx_stripes(0, 4), 0, 0.125), 0.02),
        stain = _gt(tx_fbm(16, 2, 3, 163), 0.64),
        hole = tx_mul(_lt(tx_cells(20, 165, "dist"), 0.10), _gt(tx_cells(20, 165, "rand"), 0.55)))
    tx_surface(tx_mul(tx_mul(tx_palette(0, [1, 1, 1]), _sh(0.88, 0.12, mottle), tx_select(line, 0.86, 1)),
                      tx_select(stain, 0.90, 1), tx_select(hole, 0.85, 1)),
               height = tx_select(line, 0.25, tx_select(hole, 0.3, _sh(0.7, 0.1, mottle))), tilt = 6, blur = 1);

// 不规则石板铺地：Voronoi 石块、下凹灰缝，石面鼓起且比缝隙光滑。
function tex_flagstone(count = 7) =
    let(edge = tx_cells(count, 0, "edge"),
        joint = _lt(edge, 0.05),
        tone = tx_posterize(tx_cells(count, 0, "rand"), 4))
    tx_surface(tx_mul(tx_palette(0, [1, 1, 1]), tx_select(joint, 0.6, _sh(0.8, 0.2, tone))),
               height = tx_mul(tx_smoothstep(edge, 0.05, 0.22), _sh(0.85, 0.15, tone)),
               rough = tx_select(joint, 1, 0.82), tilt = 14, blur = 1);

// 树皮：顺 +v 的纵向沟槽，被横向裂成鳞片；沟里更暗。
function tex_bark() =
    let(furrow = tx_band(tx_fbm(14, 2, 3, 0), 0.5, 0.12),
        ridge = tx_mul(tx_invert(furrow), tx_smoothstep(tx_cells(10, 1, "edge"), 0.0, 0.12)),
        tone = tx_posterize(tx_fbm(4, 8, 3, 2), 3))
    tx_surface(tx_mul(tx_palette(0, [1, 1, 1]), _sh(0.62, 0.3, ridge), _sh(0.92, 0.12, tone)),
               height = ridge, tilt = 14, blur = 1);

// ---- 旧金属与木（色板绝对，外层材质白色；金属度 / 粗糙度系数乘在 gk_material 上）----
// 漆面钢：漆（电介质、哑光）在磕碰处剥落露出钢（金属、较光滑），周围一圈磨薄发亮的漆。
function tex_chipped() =
    let(wear = tx_add(tx_mul(tx_fbm(6, 6, 4, 171), 0.7), tx_mul(tx_fbm(24, 24, 2, 173), 0.3)),
        chip = tx_top(wear, 0.055),
        scuff = tx_top(wear, 0.095),
        mottle = tx_fbm(8, 8, 2, 175),
        paint = tx_palette(0, [0.21, 0.23, 0.14]))
    tx_surface(tx_select(chip, tx_mul(tx_palette(1, [0.52, 0.52, 0.53]), _sh(0.9, 0.1, mottle)),
               tx_select(scuff, tx_mix(paint, 1, 0.12), tx_mul(paint, _sh(0.94, 0.06, tx_posterize(mottle, 3))))),
               height = tx_select(chip, 0.1, tx_select(scuff, 0.75, 0.9)),
               rough = tx_select(chip, 0.45, tx_select(scuff, 0.9, 1)),
               metal = tx_select(chip, 1, 0), tilt = 12, blur = 1);

// 锈钢：钢（金属、较光滑）上长出锈（电介质、粗糙、麻点凸起），锈芯是深色锈皮，下方有流痕。
function tex_rust() =
    let(bloom = tx_add(tx_mul(tx_fbm(5, 5, 4, 181), 0.8), tx_mul(tx_fbm(20, 20, 2, 183), 0.2)),
        rusty = tx_top(bloom, 0.40),
        scale = tx_top(bloom, 0.12),
        pits = tx_fbm(24, 24, 2, 185),
        lumps = tx_posterize(pits, 3),
        streak = _gt(tx_fbm(14, 2, 3, 187), 0.62),
        brushed = tx_fbm(2, 32, 2, 189),
        steel = tx_palette(0, [0.50, 0.50, 0.51]),
        oxide = tx_palette(1, [0.24, 0.085, 0.03]))
    tx_surface(tx_select(scale, tx_mul(tx_palette(2, [0.09, 0.04, 0.02]), _sh(0.9, 0.2, pits)),
               tx_select(rusty, tx_mul(oxide, _sh(0.88, 0.2, lumps)),
               tx_select(streak, tx_mix(steel, oxide, 0.45), tx_mul(steel, _sh(0.92, 0.08, brushed))))),
               height = tx_select(scale, _sh(0.55, 0.35, lumps), tx_select(rusty, _sh(0.45, 0.3, lumps), 0.3)),
               rough = tx_select(rusty, 1, tx_select(streak, 0.85, _sh(0.45, 0.15, brushed))),
               metal = tx_select(rusty, 0, tx_select(streak, 0.4, 1)), tilt = 11, blur = 1);

// 发蓝 / 磷化涂层磨穿：少量小块露出亮钢（最光滑），细划痕顺 +u；全部是金属。
function tex_worn() =
    let(wear = tx_add(tx_mul(tx_fbm(10, 10, 3, 191), 0.6), tx_mul(tx_fbm(32, 32, 2, 193), 0.4)),
        rub = tx_top(wear, 0.04),
        mottle = tx_fbm(8, 8, 2, 195),
        scratch = _gt(tx_band(tx_fbm(2, 40, 2, 197), 0.5, 0.012), 0.0001),
        finish = tx_palette(0, [0.17, 0.175, 0.19]),
        bright = tx_palette(1, [0.55, 0.55, 0.56]))
    tx_surface(tx_select(rub, tx_mix(finish, bright, _sh(0.75, 0.25, mottle)),
               tx_select(scratch, tx_mix(finish, bright, 0.3), tx_mul(finish, _sh(0.95, 0.05, tx_posterize(mottle, 3))))),
               height = tx_select(rub, 0.35, tx_select(scratch, 0.3, _sh(0.5, 0.05, mottle))),
               rough = tx_select(rub, 0.5, tx_select(scratch, 0.7, 1)), metal = 1, tilt = 7, blur = 1);

// 木纹：顺 +u 的连续年轮（枪托、家具），晚材更暗、有导管孔；清漆填平的早材更光滑。
function tex_woodgrain() =
    let(ring = tx_warp(tx_stripes(0, 14), 0.5, tx_fbm(2, 4, 3, 201), 0.12),
        late = _clamp01(tx_mul(tx_sub(ring, 0.62), 1 / 0.3)),
        pore = tx_mul(_gt(late, 0.2), _gt(tx_hash(85, 256, 203), 0.82)))
    tx_surface(tx_mul(tx_palette(0, [1, 1, 1]), _sh(0.88, 0.12, tx_posterize(tx_fbm(3, 6, 2, 205), 3)),
                      tx_sub(1, tx_mul(tx_posterize(late, 3), 0.16)), tx_select(pore, 0.85, 1)),
               height = tx_select(pore, 0.2, tx_sub(0.7, tx_mul(late, 0.3))),
               rough = tx_select(pore, 1, _sh(0.75, 0.25, late)), tilt = 8, blur = 1);

// 瓦楞铁皮：竖向波纹，锈斑从镀锌面上长出来并顺坡流下。
function tex_corrugated(ribs = 24, rust = 0.35) =
    let(profile = tx_band(tx_stripes(ribs, 0), 0.5, 0.5),
        bloom = tx_add(tx_mul(tx_fbm(5, 5, 4, 1), 0.75), tx_mul(tx_fbm(6, 1, 3, 2), 0.25)),
        rusty = tx_top(bloom, rust),
        pits = tx_posterize(tx_fbm(24, 24, 2, 3), 3))
    tx_surface(tx_select(rusty, tx_mul(tx_palette(1, [0.22, 0.08, 0.03]), _sh(0.85, 0.3, pits)),
                         tx_mul(tx_palette(0, [0.46, 0.47, 0.47]), _sh(0.9, 0.1, profile))),
               height = tx_add(profile, tx_mul(rusty, pits, 0.15)),
               rough = tx_select(rusty, 1, 0.5), metal = tx_select(rusty, 0, 1), tilt = 16, blur = 1);
