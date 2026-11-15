// kit_deadly_wasteland.scad —— kit_deadly 的"末日后"一层：血、尸体、弃物、弃车、路障（前缀 dw_）
// =====================================================================================
// kit_deadly 给出的是一座被规划过的郊区小镇；这层件把它变成"出过事的"小镇。参考的俯视
// 末日游戏里，街面几乎没有一块干净的地方：血泊和拖痕、倒在路边的人、被丢下的车门大开的车、
// 撒了一地的行李和垃圾、警方没守住的路障。这些东西俯视下都很小，所以价值在"多"和"沿路"。
//
// 放置契约（同 kit_deadly）：落地件底面 z=0，front = -y，载具车头 +x。
// 地面贴花（血、纸、刹车痕）带 z 参数：贴在沥青路上用默认 0.12（路面顶，标线顶 0.14），
// 人行道用 0.16。它们自己再抬 1.5 cm，避开标线的共面。
//
// dw_ground_road_clutter(L, W, seed) 是主要入口：沿一段路（沿 x、宽 W）确定性地铺一层杂物，
// 整段包在 gk_flatten 里——一段路一个 Node，几百米的路网不会炸出几千个节点。
// =====================================================================================

use <kit_deadly.scad>

// ---- 配色 ----
// Old, dried blood: maroon, not paint red. Fresh gore is the game's own (Brotato3DGoreSystem).
function dw_BLOOD()   = [0.30, 0.03, 0.03];
function dw_BLOODD()  = [0.19, 0.02, 0.02];
function dw_SOOT()    = [0.10, 0.09, 0.09];
function dw_PAPER()   = [0.78, 0.76, 0.68];
function dw_BAG()     = [0.08, 0.08, 0.09];
function dw_SKIN(i)   = [[0.45, 0.50, 0.40], [0.56, 0.50, 0.42], [0.50, 0.44, 0.48]][dd_rnd(i, 3)];
function dw_CLOTH(i)  = [[0.34, 0.42, 0.56], [0.58, 0.30, 0.24], [0.62, 0.56, 0.36], [0.30, 0.46, 0.40],
                         [0.52, 0.42, 0.52], [0.40, 0.40, 0.42]][dd_rnd(i, 6)];
function dw_PANTS(i)  = [[0.20, 0.24, 0.34], [0.24, 0.22, 0.18], [0.30, 0.30, 0.32]][dd_rnd(i, 3)];

// ================= 地面贴花 =================

// 不规则血泊：几块压扁的七边形叠成一摊，r = 主体半径。
module dw_ground_blood(seed = 0, r = 1.0, z = 0.12)
{
    translate([0, 0, z + 0.015])
    {
        color(dw_BLOODD()) scale([1.0, dd_rndr(seed, 0.6, 0.95), 1]) cylinder(h = 0.012, r = r, $fn = 7);
        color(dw_BLOOD())
            for (i = [0 : 2])
                translate([dd_rndr(seed + i * 7, -r * 0.8, r * 0.8), dd_rndr(seed + i * 11 + 3, -r * 0.6, r * 0.6), 0.004])
                    rotate([0, 0, dd_rnd(seed + i, 180)])
                        scale([1, dd_rndr(seed + i * 5, 0.5, 0.9), 1]) cylinder(h = 0.012, r = r * dd_rndr(seed + i * 13, 0.3, 0.55), $fn = 6);
    }
}

// 拖痕：从血泊拉出去的一道，越拖越淡、越拖越断——一条连续的细线俯视下读成油漆。
module dw_ground_blood_drag(seed = 0, len = 2.4, z = 0.12)
{
    translate([0, 0, z + 0.015])
        for (i = [0 : 4])
            if (i < 2 || dd_rnd(seed + i * 5, 3) != 0)
                color(i < 2 ? dw_BLOOD() : dw_BLOODD())
                    translate([len * (i + 0.5) / 5, dd_rndr(seed + i, -0.1, 0.1), 0])
                        rotate([0, 0, dd_rndr(seed + i * 3, -14, 14)])
                            dd_slab(len / 5 * dd_rndr(seed + i * 7, 0.6, 1.0), 0.55 - i * 0.08, 0.01);
}

// 散落的纸片、传单。
module dw_ground_litter(seed = 0, r = 1.4, z = 0.12)
{
    for (i = [0 : 3 + dd_rnd(seed, 3)])
        color(i % 3 == 0 ? [0.62, 0.58, 0.48] : dw_PAPER())
            translate([dd_rndr(seed + i * 7, -r, r), dd_rndr(seed + i * 11 + 1, -r, r), z + 0.015])
                rotate([0, 0, dd_rnd(seed + i * 3, 180)]) dd_slab(0.3, 0.22, 0.008);
}

// 刹车痕：两道并行的黑胶印，略带弧度。
module dw_ground_skid(seed = 0, len = 9, z = 0.12)
{
    bend = dd_rndr(seed, -10, 10);
    color(dw_SOOT())
        for (sy = [-1, 1], i = [0 : 2])
            translate([len * (i - 1) / 3, sy * 0.78 + bend * 0.02 * (i - 1) * (i - 1), z + 0.015])
                rotate([0, 0, bend * (i - 1) * 0.5]) dd_slab(len / 3 + 0.1, 0.2, 0.008);
}

// 烧焦地面：车烧过的地方。
module dw_ground_scorch(seed = 0, r = 2.6, z = 0.12)
{
    translate([0, 0, z + 0.015])
    {
        color(dw_SOOT()) scale([1, dd_rndr(seed, 0.6, 0.8), 1]) cylinder(h = 0.01, r = r, $fn = 8);
        color([0.18, 0.16, 0.15])
            for (i = [0 : 2])
                translate([dd_rndr(seed + i * 5, -r, r), dd_rndr(seed + i * 9, -r * 0.6, r * 0.6), 0.003])
                    cylinder(h = 0.01, r = r * 0.35, $fn = 6);
    }
}

// ================= 小件 =================

// 倒地的人：仰面或侧身，四肢摊开，身下一摊血。与 Brotato3D 僵尸同一套配色。
module dw_prop_corpse(seed = 0, z = 0.12)
{
    skin = dw_SKIN(seed);
    cloth = dw_CLOTH(seed + 3);
    pants = dw_PANTS(seed + 5);
    armL = dd_rndr(seed + 7, 20, 80);
    armR = dd_rndr(seed + 9, -80, -10);
    legL = dd_rndr(seed + 11, 4, 22);
    legR = dd_rndr(seed + 13, -24, -2);
    dw_ground_blood(seed = seed + 17, r = dd_rndr(seed + 19, 0.6, 1.0), z = z);
    translate([0, 0, z])
    {
        color(cloth) translate([0.05, 0, 0.12]) dd_boxc([0.55, 0.38, 0.2]);                  // 躯干，头朝 +x
        color(skin) translate([0.46, 0.02, 0.13]) rotate([0, 0, dd_rndr(seed, -25, 25)]) dd_boxc([0.22, 0.19, 0.2]);
        color(dw_BLOOD()) translate([0.1, -0.06, 0.225]) dd_boxc([0.18, 0.14, 0.012]);
        color(pants) translate([-0.36, 0, 0.11]) dd_boxc([0.28, 0.34, 0.18]);
        // 双臂从肩部甩开
        color(cloth) for (a = [[armL, 1], [armR, -1]])
            translate([0.24, a[1] * 0.2, 0.09]) rotate([0, 0, a[0]]) translate([0, a[1] * 0.27, 0])
                dd_boxc([0.11, 0.54, 0.11]);
        color(pants) for (l = [[legL, 1], [legR, -1]])
            translate([-0.48, l[1] * 0.1, 0.09]) rotate([0, 0, l[0]])
                translate([-0.4, 0, 0]) dd_boxc([0.8, 0.14, 0.13]);
        color(dd_DARKC()) for (l = [[legL, 1], [legR, -1]])
            translate([-0.48, l[1] * 0.1, 0.09]) rotate([0, 0, l[0]]) translate([-0.85, 0, 0.03]) dd_boxc([0.14, 0.16, 0.16]);
    }
}

// 一袋袋黑色垃圾袋，有一袋破了。
module dw_prop_trash_bags(seed = 0)
{
    for (i = [0 : 1 + dd_rnd(seed, 3)])
        color(dw_BAG())
            translate([dd_rndr(seed + i * 5, -0.6, 0.6), dd_rndr(seed + i * 7 + 2, -0.4, 0.4), 0.24])
                scale([1, dd_rndr(seed + i, 0.8, 1.1), 0.75]) sphere(r = dd_rndr(seed + i * 3, 0.26, 0.36), $fn = 6);
    dw_ground_litter(seed = seed + 9, r = 0.8, z = 0);
}

// 撒了一地的行李：打开的箱子 + 衣物。逃难的人到这里就没再往前走。
module dw_prop_luggage(seed = 0, z = 0.12)
{
    c = dw_CLOTH(seed + 1);
    translate([0, 0, z])
    {
        color([0.20, 0.22, 0.30]) dd_slab(0.7, 0.45, 0.12);
        color([0.24, 0.26, 0.34]) translate([0, 0.32, 0]) rotate([60, 0, 0]) dd_slab(0.7, 0.12, 0.45);
        for (i = [0 : 3])
            color(i % 2 == 0 ? c : dw_CLOTH(seed + i * 7))
                translate([dd_rndr(seed + i * 5, -1.1, 1.1), dd_rndr(seed + i * 9, -0.9, 0.2), 0])
                    rotate([0, 0, dd_rnd(seed + i, 180)]) dd_slab(0.42, 0.3, 0.03);
        color([0.42, 0.30, 0.20]) translate([1.0, 0.5, 0]) rotate([0, 0, 24]) dd_slab(0.55, 0.32, 0.36);   // 第二只箱子
    }
}

// 购物车，有时翻倒。
module dw_prop_cart(seed = 0)
{
    tipped = dd_rnd(seed, 3) == 0;
    rotate([tipped ? 90 : 0, 0, 0]) translate([0, 0, tipped ? 0.33 : 0])
        color(dd_METALC())
        {
            // Basket: floor and four walls (no CSG: carts are scattered by the hundred).
            translate([0, 0, 0.42]) dd_boxc([0.85, 0.55, 0.03]);
            for (sy = [-1, 1]) translate([0, sy * 0.27, 0.63]) dd_boxc([0.85, 0.02, 0.42]);
            for (sx = [-1, 1]) translate([sx * 0.42, 0, 0.63]) dd_boxc([0.02, 0.55, 0.42]);
            translate([-0.48, 0, 0.92]) dd_boxc([0.04, 0.5, 0.04]);
            for (sx = [-1, 1], sy = [-1, 1]) translate([sx * 0.36, sy * 0.22, 0.2]) dd_boxc([0.03, 0.03, 0.4]);
            color(dd_DARKC()) for (sx = [-1, 1], sy = [-1, 1]) translate([sx * 0.36, sy * 0.22, 0.04]) dd_boxc([0.08, 0.04, 0.08]);
        }
}

// ================= 载具 =================

// 被丢下的车：车门大开、一扇窗碎了，后备箱弹开，车旁一地东西。
module dw_veh_abandoned(seed = 0)
{
    dd_veh_sedan(seed = seed);
    c = dd_car_c(seed);
    // 驾驶侧车门向外打开
    translate([0.35, -0.92, 0.42]) rotate([0, 0, -58]) translate([-0.55, 0, 0])
    {
        color(c) translate([0, 0, 0.18]) dd_boxc([1.1, 0.08, 0.58]);
        color(dd_GLASSC()) translate([0, 0, 0.62]) dd_boxc([0.9, 0.06, 0.32]);
    }
    color(dd_DARKC()) translate([-0.25, 0, 1.2]) dd_boxc([1.0, 1.8, 0.36]);      // 碎窗的黑洞
    color(c) translate([-2.05, 0, 1.2]) rotate([0, -55, 0]) dd_boxc([0.07, 1.6, 0.85]);   // 后备箱盖
    color(dd_GLASSC()) for (i = [0 : 3])
        translate([dd_rndr(seed + i * 7, -1.2, 1.2), dd_rndr(seed + i * 5, -1.7, -1.1), 0.12]) dd_slab(0.18, 0.12, 0.02);
    translate([-2.9, 0.2, 0]) dw_prop_luggage(seed = seed + 21, z = 0.12);
}

// 警车：黑白涂装 + 警灯条，车门开着。路障的主角。
module dw_veh_police(seed = 0)
{
    w = [0.86, 0.86, 0.84];
    k = [0.10, 0.10, 0.11];
    color(w) translate([0, 0, 0.62]) dd_boxc([4.1, 1.82, 0.62]);
    color(k) for (sx = [-1, 1]) translate([sx * 1.55, 0, 0.66]) dd_boxc([1.0, 1.84, 0.56]);
    color(w) translate([-0.2, 0, 1.2]) dd_boxc([2.1, 1.7, 0.56]);
    color(dd_GLASSC()) translate([-0.2, 0, 1.2]) dd_boxc([1.82, 1.78, 0.4]);
    color(k) translate([-0.2, 0, 1.5]) dd_boxc([2.0, 1.6, 0.05]);
    color([0.85, 0.12, 0.10]) translate([-0.2, 0.32, 1.58]) dd_boxc([0.3, 0.55, 0.12]);
    color([0.12, 0.30, 0.85]) translate([-0.2, -0.32, 1.58]) dd_boxc([0.3, 0.55, 0.12]);
    color(dd_MARKW()) for (sy = [-1, 1]) translate([2.06, sy * 0.6, 0.7]) dd_boxc([0.05, 0.3, 0.16]);
    color(dd_METALD()) translate([2.12, 0, 0.5]) dd_boxc([0.14, 1.6, 0.3]);                // 推杠
    for (sx = [-1, 1], sy = [-1, 1]) translate([1.25 * sx, 0.92 * sy, 0.34]) dd_veh_wheel();
    translate([0.4, -0.92, 0.42]) rotate([0, 0, -70]) translate([-0.55, 0, 0])
        color(w) translate([0, 0, 0.2]) dd_boxc([1.1, 0.08, 0.6]);
}

// 烧毁的车 + 烧焦的地面。
module dw_veh_burnt(seed = 0)
{
    dw_ground_scorch(seed = seed, r = 2.8, z = 0.12);
    color(dw_SOOT()) translate([0, 0, 0.1]) dd_veh_wreck(seed = seed);
}

// ================= 组合件 =================

// 警方路障：横跨一条路（沿 y 展开，宽 W），两辆警车斜停、隔离墩、锥桶、沙袋，
// 路障前倒着没守住的人。front = -y 之外没有方向性。
module dw_prop_roadblock(W = 9, seed = 0)
{
    translate([0, -W * 0.22, 0]) rotate([0, 0, 90 + dd_rndr(seed, 15, 30)]) dw_veh_police(seed = seed);
    translate([0.8, W * 0.26, 0]) rotate([0, 0, 90 - dd_rndr(seed + 3, 15, 30)]) dw_veh_police(seed = seed + 1);
    for (i = [-1, 0, 1]) translate([-2.6, i * W * 0.28, 0]) rotate([0, 0, 90]) dd_prop_jersey(len = 2.0, seed = seed + i);
    for (i = [0 : 4]) translate([-3.8, -W * 0.45 + i * W * 0.22, 0]) dd_prop_cone();
    translate([2.6, 0, 0]) rotate([0, 0, 90]) dd_prop_sandbags(len = 3.0, seed = seed + 5);
    for (i = [0 : 2])
        translate([-5.2 - i * 1.4, dd_rndr(seed + i * 7, -W * 0.4, W * 0.4), 0])
            rotate([0, 0, dd_rnd(seed + i * 3, 360)]) dw_prop_corpse(seed = seed + i * 11);
    translate([-5.0, 0, 0]) dw_ground_blood(seed = seed + 33, r = 1.6);
    translate([-7.5, W * 0.1, 0]) dw_ground_litter(seed = seed + 41, r = 2.2);
}

// 沿一段路（沿 x、全长 L、宽 W，原点在路中心）铺一层杂物。
//   step  平均每隔多少米一件；越小越密
//   cars  是否允许放弃车（地图上已摆了车阵的路段传 false）
//   z     路面顶面高度（kit_deadly 的路是 0.12）
module dw_ground_road_clutter(L = 100, W = 8, seed = 0, step = 9, cars = true, z = 0.12)
{
    n = max(1, floor(L / step));
    gk_flatten() for (i = [0 : n - 1])
    {
        s = seed * 131 + i * 17;
        x = -L / 2 + step * (i + 0.5) + dd_rndr(s + 1, -step * 0.3, step * 0.3);
        lane = (dd_rnd(s + 2, 2) == 0 ? -1 : 1) * W * 0.24;
        y = dd_rndr(s + 3, -W * 0.38, W * 0.38);
        a = dd_rnd(s + 4, 360);
        k = dd_rnd(s, 22);
        translate([x, 0, 0])
        {
            if (k < 4)       translate([0, y, 0]) dw_ground_blood(seed = s, r = dd_rndr(s + 5, 0.5, 1.3), z = z);
            else if (k < 6)  translate([0, y, 0]) rotate([0, 0, a]) { dw_ground_blood(seed = s, r = 0.9, z = z); dw_ground_blood_drag(seed = s, len = dd_rndr(s + 6, 1.6, 3.0), z = z); }
            else if (k < 9)  translate([0, y, 0]) rotate([0, 0, a]) dw_prop_corpse(seed = s, z = z);
            else if (k < 10) translate([0, y, 0]) rotate([0, 0, a]) { dw_prop_corpse(seed = s, z = z); translate([0.9, 0.8, 0]) rotate([0, 0, 70]) dw_prop_corpse(seed = s + 3, z = z); }
            else if (k < 12) translate([0, y, 0]) dw_ground_litter(seed = s, z = z);
            else if (k < 13) translate([0, (y < 0 ? -1 : 1) * W * 0.42, 0]) dw_prop_trash_bags(seed = s);
            else if (k < 14) translate([0, y, 0]) rotate([0, 0, a]) dw_prop_luggage(seed = s, z = z);
            else if (k < 15) translate([0, lane, 0]) rotate([0, 0, dd_rndr(s + 7, -8, 8)]) dw_ground_skid(seed = s, z = z);
            else if (k < 16) translate([0, y, 0]) rotate([0, 0, a]) dw_prop_cart(seed = s);
            else if (k < 18 && cars)
                translate([0, lane, 0]) rotate([0, 0, (lane > 0 ? 180 : 0) + dd_rndr(s + 8, -20, 20)]) dw_veh_abandoned(seed = s);
            else if (k < 19 && cars)
                translate([0, lane, 0]) rotate([0, 0, dd_rndr(s + 9, -35, 35)]) dw_veh_burnt(seed = s);
            else if (k < 20)
                translate([0, y, 0]) rotate([0, 0, a]) { dw_ground_blood(seed = s + 1, r = 1.1, z = z); dw_ground_litter(seed = s + 2, z = z); }
        }
    }
}

// ================= 幸存者巴士（Brotato3D 撤离车） =================
// 一辆改装过的校车：车窗焊了钢板只留射击缝，车头加了推土铲，车顶是整个车队的家当——
// 行李架、备胎、油桶、床垫、沙袋围起来的探照灯位。俯视镜头下看到的主要是车顶，
// 所以车顶是这辆车最用心的地方。车头 +x（kit 约定），车门在右侧（-y）靠车头处。
// 车身约 11.4 x 2.6 m（含推铲），车顶面 z = 2.86。
function dw_BUSY()  = [0.78, 0.58, 0.12];   // 褪色校车黄
function dw_BUSYD() = [0.58, 0.42, 0.10];
function dw_PLATE() = [0.36, 0.37, 0.38];   // 焊上去的钢板
function dw_PLATED() = [0.27, 0.27, 0.28];

module dw_veh_survivor_bus(seed = 0)
{
    // --- 车身 ---
    color(dw_BUSY())
    {
        translate([-0.6, 0, 1.8]) dd_boxc([9.4, 2.5, 1.9]);
        translate([4.6, 0, 1.35]) dd_boxc([1.7, 2.3, 1.1]);           // 发动机舱
    }
    color(dw_BUSYD())
    {
        translate([-0.6, 0, 2.78]) dd_boxc([9.3, 2.42, 0.16]);         // 车顶
        for (sy = [-1, 1]) translate([-0.6, sy * 1.255, 1.42]) dd_boxc([9.4, 0.02, 0.08]);   // 腰线
    }
    color([0.14, 0.14, 0.14])
    {
        translate([-0.6, 0, 1.2]) dd_boxc([9.5, 2.54, 0.22]);           // 黑裙边
        translate([1.0, 0, 0.62]) dd_boxc([8.0, 2.2, 0.44]);            // 底盘
    }
    // 车窗：焊钢板 + 射击缝；挡风玻璃上一块网格护板
    for (sy = [-1, 1])
    {
        for (i = [0 : 6])
        {
            px = -4.6 + i * 1.3;
            color(i % 2 == 0 ? dw_PLATE() : dw_PLATED()) translate([px, sy * 1.27, 2.2]) dd_boxc([1.2, 0.04, 0.9]);
            color(dd_DARKC()) translate([px, sy * 1.295, 2.38]) dd_boxc([0.7, 0.02, 0.08]);
            color(dd_RUSTC()) translate([px + 0.5, sy * 1.295, 2.05]) dd_boxc([0.08, 0.02, 0.5]);
        }
    }
    color(dd_GLASSC()) translate([4.1, 0, 2.3]) dd_boxc([0.5, 2.2, 0.85]);
    color(dd_METALD()) for (i = [0 : 4]) translate([4.37, -0.9 + i * 0.45, 2.3]) dd_boxc([0.04, 0.05, 0.85]);
    color(dd_METALD()) for (z = [2.0, 2.3, 2.6]) translate([4.37, 0, z]) dd_boxc([0.04, 2.2, 0.05]);
    // 车门（右侧靠车头）：开着，门里黑洞
    color(dd_DARKC()) translate([3.25, -1.27, 1.75]) dd_boxc([0.95, 0.05, 1.7]);
    color(dw_BUSYD()) translate([3.25, -1.62, 1.75]) rotate([0, 0, 75]) dd_boxc([0.95, 0.05, 1.6]);
    color(dd_METALD()) translate([3.25, -1.55, 0.5]) dd_boxc([0.9, 0.5, 0.08]);   // 踏板

    // --- 推土铲（车头 +x） ---
    color(dd_RUSTC())
    {
        translate([5.75, 0, 0.75]) rotate([0, -18, 0]) dd_boxc([0.12, 2.7, 0.9]);
        for (sy = [-1, 0, 1]) translate([5.55, sy * 0.95, 0.75]) rotate([0, 50, 0]) dd_boxc([0.6, 0.1, 0.1]);
    }
    color(dd_METALD()) for (i = [0 : 6]) translate([5.86, -1.2 + i * 0.4, 1.12]) rotate([0, 60, 0]) cylinder(h = 0.35, r1 = 0.06, r2 = 0, $fn = 4);
    color(dd_MARKW()) for (sy = [-1, 1]) translate([5.44, sy * 0.75, 1.1]) dd_boxc([0.06, 0.34, 0.2]);

    // --- 车轮 ---
    for (sy = [-1, 1])
    {
        translate([3.4, sy * 1.15, 0.5]) dd_veh_wheel(0.5, 0.3);
        translate([-2.6, sy * 1.15, 0.5]) dd_veh_wheel(0.5, 0.3);
        translate([-3.7, sy * 1.15, 0.5]) dd_veh_wheel(0.5, 0.3);
    }

    // --- 车尾：梯子 + 绑着的自行车 ---
    color(dd_METALD())
    {
        for (sy = [-0.3, 0.3]) translate([-5.38, sy, 1.9]) dd_boxc([0.05, 0.05, 1.9]);
        for (z = [1.2 : 0.35 : 2.8]) translate([-5.38, 0, z]) dd_boxc([0.05, 0.62, 0.04]);
    }

    // --- 车顶：行李架与家当 ---
    translate([0, 0, 2.86])
    {
        color(dd_METALD())
        {
            for (sy = [-1, 1]) translate([-0.8, sy * 1.12, 0.12]) dd_boxc([8.6, 0.05, 0.05]);
            for (i = [0 : 7]) translate([-4.9 + i * 1.2, 0, 0.06]) dd_boxc([0.05, 2.25, 0.05]);
        }
        // 沙袋围起来的探照灯位（车头上方）
        translate([2.7, 0, 0])
        {
            color([0.52, 0.46, 0.32]) for (a = [0 : 45 : 315])
                rotate([0, 0, a]) translate([0.75, 0, 0.14]) scale([1, 0.62, 0.55]) sphere(r = 0.32, $fn = 6);
            color(dd_METALD()) translate([0, 0, 0.45]) cylinder(h = 0.5, r = 0.06, $fn = 6);
            color(dd_METALD()) translate([0.1, 0, 0.98]) rotate([0, 90, 0]) cylinder(h = 0.32, r = 0.16, $fn = 8);
            color([1.0, 0.92, 0.65]) translate([0.27, 0, 0.98]) rotate([0, 90, 0]) cylinder(h = 0.02, r = 0.13, $fn = 8);
        }
        // 备胎一摞、油桶、油壶
        for (i = [0 : 2]) translate([0.6, 0.55, 0.12 + i * 0.22]) color([0.10, 0.10, 0.10]) cylinder(h = 0.2, r = 0.42, $fn = 9);
        color([0.62, 0.17, 0.12]) for (i = [0 : 1]) translate([0.45 - i * 0.7, -0.6, 0.06]) cylinder(h = 0.85, r = 0.3, $fn = 9);
        color([0.26, 0.40, 0.22]) for (i = [0 : 2]) translate([-0.9 + i * 0.35, -0.55, 0.06]) dd_boxc([0.3, 0.16, 0.42]);
        // 绑着的床垫
        color([0.70, 0.66, 0.58]) translate([-1.9, 0, 0.16]) rotate([0, 0, 4]) dd_boxc([1.95, 1.4, 0.2]);
        color([0.40, 0.30, 0.22]) for (sx = [-0.5, 0.5]) translate([-1.9 + sx, 0, 0.28]) dd_boxc([0.06, 1.5, 0.04]);
        // 木箱、行李、篷布包
        color([0.45, 0.33, 0.20]) for (p = [[-3.4, 0.55, 0.5], [-3.4, -0.45, 0.42], [-4.25, 0.5, 0.38]])
            translate([p[0], p[1], p[2] / 2 + 0.06]) dd_boxc([0.7, 0.75, p[2]]);
        color([0.22, 0.36, 0.30]) translate([-4.3, -0.5, 0.3]) scale([1, 0.8, 0.55]) sphere(r = 0.55, $fn = 8);
        color([0.24, 0.27, 0.40]) translate([-2.95, 0.05, 0.62]) dd_boxc([0.6, 0.42, 0.24]);
        // 喷漆 "SOS" 的木板靠在行李上（俯视可读的一块亮色）
        color([0.80, 0.78, 0.70]) translate([1.7, 0, 0.1]) dd_boxc([0.9, 1.6, 0.05]);
        color([0.70, 0.10, 0.08]) for (i = [0 : 2]) translate([1.7, -0.5 + i * 0.5, 0.13]) dd_boxc([0.55, 0.32, 0.02]);
    }
}
