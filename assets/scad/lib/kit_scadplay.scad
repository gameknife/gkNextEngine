// kit_scadplay.scad —— ScadPlay 玩法零件库：边玩边让 agent 用自然语言搭关卡、改玩法
// 纯零件库：只含 module/function 定义，无顶层几何。命名空间前缀 "sp_"。
// 运行时（projects/ScadPlay）按 **module 名** 找到每个调用节点，交给同名的 C# 类驱动
//   （sp_door → Mechanisms/Door.cs，登记表在 Scripts/Gameplay/MechanismTypes.cs）。
//   规则改 C#（热重载），关卡和接线改 .scad（保存即重建），两边都不用重启游戏。
//   协议、context.json 与 agent 流程见 docs/AGENT_GUIDE/ScadPlay.md。
//
// 尺度：1 unit = 1 m，Z 朝上。主角（astro_bot）高 1.6 m，碰撞胶囊高 1.5 m、半径 0.35 m，
//   能直接迈上 0.5 m 的台阶；跑速 6 m/s；单跳高 ≈ 2.0 m，助跑跳远 ≈ 5 m（关卡缺口 ≤ 4 m 才舒服）。
//
// 放置契约：
//   * 落地件底面 z = 0（站在地面上的东西直接 translate 到地面高度即可）。
//     例外：sp_ground 的**顶面** z = 0，地块向下延伸。
//   * 有朝向的件 front = +x（sp_spawn 出生时面朝 +x，rotate([0, 0, a]) 转朝向）。
//   * 门、墙沿局部 x 展开宽度，玩家沿 y 方向穿过。桥、传送带沿 +x 延伸。
//
// 玩法契约（C# 读的就是这些具名参数，loader 把它们写进 Node::metadata）：
//   * 带 `id` 参数的 module = 玩法件，每个都有一个同名 C# 类。没有 C# 类的会出现在
//     context.json 的 gameplay.unbound 里。
//   * id：名字，别人用它接线。空串 = 匿名（仍然生效，只是没人能听它）。
//   * on：要听的 id，字符串或列表，**全部**激活才启用：on = "plate_a" / on = ["plate_a", "lever_b"]。
//     any：同上但**任一**激活即可。两者都不给 = 一直启用（门例外：没有输入就一直关着，除非 near > 0）。
//     id 末尾的 * 是前缀通配：on = "coin_*" = 所有 id 以 coin_ 开头的件都已激活。
//   * 激活（active）是每个件对外的输出：踏板被踩住、拉杆扳下、金币被吃、门打开、检查点已到达……
//   * 向量参数（travel = [0, 0, 3]）用的是**该件自己的局部 SCAD 坐标**（随 rotate 转），单位米。
//   * 字符串参数里不要写 ';' 和 '='（会被元数据丢掉），列表元素里不要写 ','。
//   * collision = "none"：整棵子树不参与碰撞（金币、触发区、装饰）。默认参与。
//
// 活动件契约：
//   * 会动的部分是 sp_part_<件>() 子调用，必须是玩法件 module 的**直接**子调用、不得放进 gk_flatten。
//   * 活动件原点 = 运动基准（转轴 / 行程起点）。父 module 把它摆在绑定姿态；
//     运行时只改它的局部变换，碰撞随之变成运动学碰撞，站在上面的玩家会被带着走。
//   * 静态外壳一律包进 gk_flatten()：一个件的静态几何 = 一个 Node。

// ================= 配色 =================
function sp_GRASS()  = [0.33, 0.55, 0.24];
function sp_GRASSD() = [0.26, 0.45, 0.20];
function sp_SOIL()   = [0.47, 0.33, 0.21];
function sp_STONE()  = [0.56, 0.56, 0.58];
function sp_STONED() = [0.40, 0.41, 0.44];
function sp_SAND()   = [0.70, 0.62, 0.42];
function sp_WOOD()   = [0.55, 0.38, 0.22];
function sp_WOODD()  = [0.38, 0.26, 0.15];
function sp_METAL()  = [0.64, 0.65, 0.68];
function sp_METALD() = [0.30, 0.31, 0.34];
function sp_RED()    = [0.74, 0.16, 0.12];
function sp_ORANGE() = [0.88, 0.46, 0.10];
function sp_YELLOW() = [0.90, 0.72, 0.14];
function sp_GREEN()  = [0.30, 0.66, 0.30];
function sp_TEAL()   = [0.12, 0.58, 0.58];
function sp_BLUE()   = [0.16, 0.40, 0.78];
function sp_PURPLE() = [0.48, 0.28, 0.68];
function sp_WHITE()  = [0.82, 0.82, 0.80];
function sp_DARK()   = [0.12, 0.12, 0.14];
function sp_GOLD()   = [0.94, 0.76, 0.26];
function sp_LAVA()   = [0.96, 0.36, 0.06];
function sp_WATER()  = [0.14, 0.44, 0.60];
function sp_LEAF()   = [0.24, 0.52, 0.22];

// 确定性伪随机（装饰件的 seed 变体用）
function sp_sq(x) = (x * x + x * 641 + 53) % 65521;
function sp_rnd(s, m) = sp_sq(sp_sq(((s % 65521) + 65521) % 65521) + 19) % m;
function sp_rndr(s, a, b) = a + (b - a) * sp_rnd(s, 1000) / 999;

// ================= 材质 =================
module sp_matte(c)   { color(c) children(); }
module sp_plastic(c) { gk_material(c, roughness = 0.35, metalness = 0) children(); }
module sp_gloss(c)   { gk_material(c, roughness = 0.12, metalness = 0) children(); }
module sp_metal(c)   { gk_material(c, roughness = 0.35, metalness = 0.6) children(); }
module sp_gold()     { gk_material(sp_GOLD(), roughness = 0.25, metalness = 0.55) children(); }

// ================= 基础几何 =================
module sp_boxc(s) cube(s, center = true);
// 底面 z = 0 的长方体（中心在 xy 原点）
module sp_box(L = 1, D = 1, H = 1) translate([0, 0, H / 2]) sp_boxc([L, D, H]);

// ================= 地形与结构（无玩法） =================

// 草地地块：顶面 z = 0，向下厚 t。
module sp_ground(w = 20, d = 20, t = 1.0, c = sp_GRASS())
{
    gk_flatten()
    {
        sp_matte(c) translate([0, 0, -0.2]) sp_box(w, d, 0.2);
        sp_matte(sp_SOIL()) translate([0, 0, -t]) sp_box(w - 0.1, d - 0.1, t - 0.2);
    }
}

// 方块 / 平台：底面 z = 0，顶面 z = h。
module sp_block(w = 2, d = 2, h = 1, c = sp_STONE())
{
    gk_flatten()
    {
        sp_plastic(c) sp_box(w, d, h - 0.06);
        sp_plastic([c[0] * 1.12, c[1] * 1.12, c[2] * 1.12]) translate([0, 0, h - 0.06]) sp_box(w - 0.04, d - 0.04, 0.06);
    }
}

// 斜坡：沿 +x 从 0 升到 h（底面 z = 0，宽 w 沿 y，水平长 l）。坡度别超过 45°（l ≥ h）。
module sp_ramp(l = 4, w = 2, h = 1, c = sp_STONE())
{
    gk_flatten() sp_plastic(c)
        translate([-l / 2, w / 2, 0]) rotate([90, 0, 0])
            linear_extrude(height = w) polygon([[0, 0], [l, 0], [l, h]]);
}

// 台阶：沿 +x 上升 n 级，每级高 rise、深 run（底面 z = 0）。rise ≤ 0.5 能直接走上去。
module sp_stairs(n = 4, w = 2, rise = 0.4, run = 0.6, c = sp_STONE())
{
    gk_flatten() sp_plastic(c)
        for (i = [0 : n - 1])
            translate([i * run + run / 2, 0, 0]) sp_box(run, w, (i + 1) * rise);
}

// 墙：沿 x 长 l，厚 t，高 h（底面 z = 0）。
module sp_wall(l = 6, h = 3, t = 0.4, c = sp_STONED())
{
    gk_flatten() sp_plastic(c) sp_box(l, t, h);
}

// 柱子
module sp_pillar(r = 0.4, h = 3, c = sp_STONE())
{
    $fn = 12;
    gk_flatten()
    {
        sp_plastic(c) cylinder(h = h, r = r, $fn = 12);
        sp_plastic(sp_STONED()) cylinder(h = 0.2, r = r + 0.1, $fn = 12);
        sp_plastic(sp_STONED()) translate([0, 0, h - 0.2]) cylinder(h = 0.2, r = r + 0.1, $fn = 12);
    }
}

// 树（装饰，有碰撞：树干挡路）
module sp_tree(h = 4, seed = 0)
{
    $fn = 8;
    hh = h * sp_rndr(seed, 0.85, 1.15);
    gk_flatten()
    {
        sp_matte(sp_WOODD()) cylinder(h = hh * 0.45, r = 0.18, $fn = 8);
        sp_matte(sp_LEAF()) translate([0, 0, hh * 0.35]) cylinder(h = hh * 0.45, r1 = hh * 0.32, r2 = hh * 0.1, $fn = 8);
        sp_matte(sp_GRASSD()) translate([0, 0, hh * 0.6]) cylinder(h = hh * 0.4, r1 = hh * 0.24, r2 = 0.02, $fn = 8);
    }
}

// 木箱（可站上去）
module sp_crate(s = 1)
{
    gk_flatten()
    {
        sp_plastic(sp_WOOD()) sp_box(s, s, s);
        sp_plastic(sp_WOODD()) for (z = [0.04, s - 0.12]) translate([0, 0, z]) sp_box(s + 0.02, s + 0.02, 0.08);
    }
}

// ================= 玩法件 =================

// ---- 出生点：玩家从这里开始，面朝 +x。id 默认 "start"；传送门用 spawn = 某个 id 指定落点。
//      只是标记、不挡路：玩家站在它下面的东西上（可以放在移动平台上方）。
module sp_spawn(id = "start", collision = "none")
{
    $fn = 20;
    gk_flatten()
    {
        sp_plastic(sp_BLUE()) cylinder(h = 0.08, r = 0.9, $fn = 20);
        sp_gloss(sp_WHITE()) translate([0.45, 0, 0.08]) rotate([0, 0, -90])
            linear_extrude(height = 0.02) polygon([[-0.22, 0], [0.22, 0], [0, 0.32]]);
    }
}

// ---- 关卡信息（不可见）：标题与目标显示在 HUD 上。
module sp_level(title = "", goal = "", id = "level", collision = "none")
{
}

// ---- 检查点：碰到后成为复活点（激活）。旗子升到杆顶。
module sp_part_checkpoint_flag(c = sp_ORANGE())
{
    gk_flatten() sp_plastic(c) translate([0.35, 0, 0]) sp_box(0.6, 0.04, 0.4);
}
module sp_checkpoint(id = "", r = 1.2)
{
    $fn = 12;
    gk_flatten()
    {
        sp_metal(sp_METALD()) cylinder(h = 0.12, r = 0.5, $fn = 12);
        sp_metal(sp_METAL()) cylinder(h = 2.2, r = 0.05, $fn = 6);
    }
    translate([0, 0, 0.4]) sp_part_checkpoint_flag();
}

// ---- 金币：靠近即吃掉（激活）。value 计分。悬浮 hover 米。
module sp_coin(id = "", value = 1, hover = 0.9, collision = "none")
{
    $fn = 14;
    translate([0, 0, hover]) gk_flatten() sp_gold()
        rotate([90, 0, 0]) cylinder(h = 0.08, r = 0.32, center = true, $fn = 14);
}
// 一排金币（沿 +x，n 枚，间距 dx）。每枚都是独立 sp_coin，id 依次为 <prefix>1, <prefix>2 …
module sp_coin_row(n = 5, dx = 1.2, hover = 0.9, prefix = "")
{
    for (i = [0 : n - 1])
        translate([i * dx, 0, 0]) sp_coin(id = prefix == "" ? "" : str(prefix, i + 1), hover = hover);
}

// ---- 终点：碰到即通关（激活）。on 给出前不开放（例如 on = "all_coins"）；
//      next = 下一关文件名（同目录、不带 .scad），留空 = 停在本关。
module sp_part_goal_ring(c = sp_YELLOW())
{
    $fn = 24;
    gk_flatten() sp_gloss(c) rotate([90, 0, 0])
        rotate_extrude($fn = 24) translate([1.1, 0]) circle(r = 0.12, $fn = 8);
}
module sp_goal(id = "goal", on = "", any = "", next = "")
{
    $fn = 24;
    gk_flatten()
    {
        sp_plastic(sp_WHITE()) cylinder(h = 0.15, r = 1.5, $fn = 24);
        sp_plastic(sp_YELLOW()) translate([0, 0, 0.15]) cylinder(h = 0.04, r = 1.2, $fn = 24);
    }
    translate([0, 0, 1.6]) sp_part_goal_ring();
}

// ---- 踏板：站上去即激活，离开即复位；latch = true 踩一次永久激活。
module sp_part_plate_cap(w = 1.4, c = sp_RED())
{
    gk_flatten() sp_gloss(c) sp_box(w, w, 0.1);
}
module sp_plate(id = "", latch = false, w = 1.4)
{
    gk_flatten() sp_metal(sp_METALD()) sp_box(w + 0.3, w + 0.3, 0.08);
    translate([0, 0, 0.08]) sp_part_plate_cap(w);
}

// ---- 拉杆：走近按 E 扳动。toggle = true 来回切换，false 只能扳一次。front = +x。
module sp_part_lever_arm()
{
    $fn = 6;
    gk_flatten()
    {
        sp_metal(sp_METAL()) cylinder(h = 0.9, r = 0.04, $fn = 6);
        sp_gloss(sp_RED()) translate([0, 0, 0.92]) sphere(r = 0.11, $fn = 8);
    }
}
module sp_lever(id = "", toggle = true)
{
    gk_flatten()
    {
        sp_plastic(sp_STONED()) sp_box(0.6, 0.5, 0.35);
        sp_metal(sp_METALD()) translate([0, 0, 0.35]) sp_box(0.5, 0.2, 0.05);
    }
    translate([0, 0, 0.4]) rotate([0, -35, 0]) sp_part_lever_arm();
}

// ---- 门：输入满足时门板按 travel（局部米）移开，不满足时关回；latch = true 开了就不再关。
//      没有输入时：near > 0 = 玩家走近 near 米内自动开；否则一直关着。激活 = 门开着。
//      travel = [0, 0, 0]（默认）= 门板沉进地面 h + 0.2 米。门洞宽 w（沿 x）、高 h。
module sp_part_door_slab(w = 2, h = 3, c = sp_PURPLE())
{
    gk_flatten()
    {
        sp_plastic(c) sp_box(w, 0.25, h);
        sp_gloss(sp_YELLOW()) translate([0, 0, h * 0.5]) sp_box(w * 0.3, 0.3, 0.3);
    }
}
module sp_door(id = "", on = "", any = "", near = 0, latch = false, w = 2, h = 3, travel = [0, 0, 0],
               speed = 2.5, c = sp_PURPLE())
{
    gk_flatten() sp_plastic(sp_STONED())
    {
        for (sx = [-1, 1]) translate([sx * (w / 2 + 0.25), 0, 0]) sp_box(0.5, 0.6, h + 0.4);
        translate([0, 0, h]) sp_box(w + 1.0, 0.6, 0.4);
    }
    sp_part_door_slab(w, h, c);
}

// ---- 移动平台：在起点与 起点 + travel 之间往返（mode = "loop"），或输入满足时去终点、
//      不满足时回起点（mode = "toggle"）。speed 米/秒，wait 端点停留秒。平台顶面 z = t。
//      on / any 不给 = 一直运行。站在上面的玩家会被带着走。
module sp_part_mover_deck(w = 3, d = 3, t = 0.4, c = sp_TEAL())
{
    gk_flatten()
    {
        sp_plastic(c) sp_box(w, d, t);
        sp_plastic(sp_YELLOW()) translate([0, 0, t]) sp_box(w - 0.3, d - 0.3, 0.02);
    }
}
module sp_mover(id = "", on = "", any = "", travel = [0, 0, 3], speed = 1.5, wait = 1, mode = "loop",
                w = 3, d = 3, t = 0.4, c = sp_TEAL())
{
    sp_part_mover_deck(w, d, t, c);
}

// ---- 旋转平台：绕自己的竖轴转，speed 度/秒（正 = 逆时针）。顶面 z = 0.4。
module sp_part_spinner_top(r = 3, c = sp_ORANGE())
{
    $fn = 24;
    gk_flatten()
    {
        sp_plastic(c) cylinder(h = 0.4, r = r, $fn = 24);
        sp_plastic(sp_WHITE()) translate([0, 0, 0.4]) for (a = [0 : 90 : 270])
            rotate([0, 0, a]) translate([r * 0.55, 0, 0]) sp_box(r * 0.7, 0.25, 0.02);
    }
}
module sp_spinner(id = "", on = "", any = "", speed = 40, r = 3, c = sp_ORANGE())
{
    $fn = 12;
    gk_flatten() sp_metal(sp_METALD()) translate([0, 0, -0.6]) cylinder(h = 0.6, r = 0.5, $fn = 12);
    sp_part_spinner_top(r, c);
}

// ---- 吊桥：桥面铰链在原点、沿 +x 伸出 len 米。平时竖起，输入满足时放平（激活 = 已放下）。
//      铰链侧放在岸边、+x 指向对岸，len 比缺口长 1 m 左右。顶面 z = 0（与岸齐平放在岸顶）。
module sp_part_bridge_deck(len = 6, w = 2, c = sp_WOOD())
{
    gk_flatten()
    {
        sp_plastic(c) translate([len / 2, 0, -0.2]) sp_box(len, w, 0.2);
        sp_plastic(sp_WOODD()) for (sy = [-1, 1]) translate([len / 2, sy * (w / 2 - 0.08), -0.25]) sp_box(len, 0.16, 0.06);
    }
}
module sp_bridge(id = "", on = "", any = "", len = 6, w = 2, speed = 60, c = sp_WOOD())
{
    gk_flatten() sp_metal(sp_METALD()) for (sy = [-1, 1]) translate([0, sy * (w / 2 + 0.15), -0.3]) sp_box(0.3, 0.3, 0.8);
    rotate([0, -90, 0]) sp_part_bridge_deck(len, w, c);
}

// ---- 弹跳垫：站上去即弹起 launch 米高（输入满足时才工作；不给 on/any = 一直工作）。
module sp_part_jumppad_cap(r = 0.9, c = sp_GREEN())
{
    $fn = 20;
    gk_flatten() sp_gloss(c) cylinder(h = 0.12, r = r, $fn = 20);
}
module sp_jumppad(id = "", on = "", any = "", launch = 6, r = 0.9)
{
    $fn = 20;
    gk_flatten()
    {
        sp_metal(sp_METALD()) cylinder(h = 0.2, r = r + 0.15, $fn = 20);
        sp_metal(sp_METAL()) for (a = [0 : 60 : 300]) rotate([0, 0, a]) translate([r * 0.6, 0, 0.2]) sp_box(0.12, 0.12, 0.1);
    }
    translate([0, 0, 0.2]) sp_part_jumppad_cap(r);
}

// ---- 危险区：掉进去 / 碰到就死，回到最近的检查点。kind = "lava" | "spikes" | "water"。
//      岩浆、水面顶在 z = 0.1，玩家会陷进去；尖刺高 0.5。宽 w（x）、深 d（y）。
module sp_hazard(w = 4, d = 4, kind = "lava", id = "", collision = "none")
{
    $fn = 4;
    if (kind == "spikes")
    {
        gk_flatten()
        {
            sp_metal(sp_METALD()) sp_box(w, d, 0.06);
            sp_metal(sp_METAL()) for (x = [-w / 2 + 0.25 : 0.5 : w / 2 - 0.2], y = [-d / 2 + 0.25 : 0.5 : d / 2 - 0.2])
                translate([x, y, 0.06]) cylinder(h = 0.45, r1 = 0.14, r2 = 0.0, $fn = 4);
        }
    }
    else
    {
        gk_flatten() gk_material(kind == "water" ? sp_WATER() : sp_LAVA(), roughness = 0.08, metalness = 0)
            sp_box(w, d, 0.1);
    }
}

// ---- 旋转扫杆：绕竖轴转的横杆，碰到就死。len 杆长（半径），height 杆离地高。
module sp_part_sweeper_arm(len = 4, height = 0.6, c = sp_RED())
{
    $fn = 10;
    gk_flatten()
    {
        sp_gloss(c) translate([len / 2, 0, height]) rotate([0, 90, 0]) cylinder(h = len, r = 0.18, center = true, $fn = 10);
        sp_gloss(sp_YELLOW()) translate([len, 0, height]) sphere(r = 0.28, $fn = 10);
    }
}
module sp_sweeper(id = "", on = "", any = "", len = 4, height = 0.6, speed = 90, collision = "none")
{
    $fn = 12;
    gk_flatten() sp_metal(sp_METALD()) cylinder(h = height + 0.4, r = 0.3, $fn = 12);
    sp_part_sweeper_arm(len, height);
}

// ---- 巡逻敌人：在起点与 起点 + travel 之间来回走，碰到就死；从上方踩它可以消灭（激活 = 已消灭）。
module sp_part_enemy_body(c = sp_PURPLE())
{
    $fn = 14;
    gk_flatten()
    {
        sp_gloss(c) translate([0, 0, 0.45]) scale([1, 1, 0.8]) sphere(r = 0.55, $fn = 14);
        sp_gloss(sp_WHITE()) for (sy = [-1, 1]) translate([0.42, sy * 0.18, 0.6]) sphere(r = 0.12, $fn = 8);
        sp_gloss(sp_DARK()) for (sy = [-1, 1]) translate([0.52, sy * 0.18, 0.62]) sphere(r = 0.05, $fn = 6);
    }
}
module sp_enemy(id = "", travel = [4, 0, 0], speed = 1.6, stompable = true, collision = "none")
{
    sp_part_enemy_body();
}

// ---- 告示牌：走近时把 text 显示在屏幕下方（牌面也刻着 label）。r = 触发半径。
module sp_sign(text = "", label = "!", r = 3, id = "")
{
    $fn = 6;
    gk_flatten()
    {
        sp_plastic(sp_WOODD()) cylinder(h = 1.2, r = 0.07, $fn = 6);
        sp_plastic(sp_WOOD()) translate([0, 0, 1.25]) sp_box(0.12, 1.1, 0.7);
        sp_plastic(sp_WHITE()) translate([0.07, 0, 1.6]) rotate([90, 0, 90])
            linear_extrude(height = 0.02) text(label, size = 0.32, halign = "center", valign = "center");
    }
}

// ---- 传送门：走进圈里去 to 关（同目录文件名、不带 .scad），落在那一关 id = spawn 的出生点。
//      输入满足时才开放。to 留空 = 回到本关出生点。
module sp_part_portal_ring(c = sp_BLUE())
{
    $fn = 24;
    gk_flatten() sp_gloss(c) rotate([90, 0, 0])
        rotate_extrude($fn = 24) translate([1.0, 0]) circle(r = 0.14, $fn = 8);
}
module sp_portal(id = "", on = "", any = "", to = "", spawn = "start")
{
    $fn = 24;
    gk_flatten() sp_plastic(sp_STONED()) cylinder(h = 0.12, r = 1.3, $fn = 24);
    translate([0, 0, 1.3]) sp_part_portal_ring();
}

// ---- 计数器（不可见）：from 匹配到的件里已激活的个数 ≥ count 时激活。
//      from 是 id 前缀（"coin_"）或 module 名（"sp_coin"）；count = 0 表示全部。
module sp_counter(id = "", from = "sp_coin", count = 0, collision = "none")
{
}

// ---- 逻辑门（不可见）：on / any 满足时激活；invert 取反；delay 秒 = 条件持续这么久才翻转。
module sp_logic(id = "", on = "", any = "", invert = false, delay = 0, collision = "none")
{
}

// ---- 易碎平台：站上去 warn 秒后塌落，respawn 秒后复原。
module sp_part_crumble_slab(w = 2, d = 2, c = sp_SAND())
{
    gk_flatten()
    {
        sp_plastic(c) sp_box(w, d, 0.35);
        sp_plastic([c[0] * 0.8, c[1] * 0.8, c[2] * 0.8]) for (i = [-1, 1]) translate([i * w * 0.2, 0, 0.35]) sp_box(0.06, d * 0.8, 0.01);
    }
}
module sp_crumble(id = "", w = 2, d = 2, warn = 0.6, respawn = 3)
{
    sp_part_crumble_slab(w, d);
}
