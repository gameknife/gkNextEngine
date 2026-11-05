// coldwar/riverland_1km.scad —— 1km x 1km 冷战末日生存开放地图（NextDayz 主地图）
// 北部山脉 → 河流蜿蜒向南；主公路东西横贯（混凝土桥过河）；支路串起二十余个据点，
// 相邻据点 60~120 m：走出一处就能看见下一处的屋顶。
//   主公路带（西→东）：西口路障 / 斯塔里萨德村 / 曙光农场 / 公路旅馆 / 波戈列洛夫卡 /
//     桥西加油站 / 桥头前哨 / 新德沃尔（警察局）/ 卡车站 / 东口军车残队
//   南部：戈尔卡镇（医院与失败的撤离点）/ 戈尔卡火车站 / 红锻工厂 / 湖畔营地
//   西北：老墓地 / 锯木场 / 西山电台
//   北部：护林站 / 猎人小屋 / 坠机点 / 祖布尔要塞 / 7 号检查站 / 军事基地
// 地形 176x176 cells（<180^2，保留物理 MeshShape，可行走/寻路）。
// 运行时语义锚点（模块名即协议，见 docs/projects/nextdayz）：
//   nd_loot(p = profile)          物资刷新点：residential/commercial/medical/military/police/
//                                 industrial/farm/wilderness，按 profile 抽物资表
//   nd_zed(p = profile, group=n)  感染者刷新点：civilian/military/industrial/wilderness
//   nd_poi(name, kind)            地图上的地名
//   nd_spawn_player_safe          玩家安全出生点
// 建筑 + 家具用 gk_flatten 合成一个节点；锚点与手摆物资（cw_item_* / cw_wpn_*）必须留在外面。
// gnb shot --target NextDayz --scene assets/scad/proc/coldwar/riverland_1km.scad
use <../../lib/kit_layout.scad>
use <../../lib/kit_terrain.scad>
use <../../lib/kit_coldwar.scad>

$fn = 12;

// ================= 运行时语义锚点（极小几何保证导入；WorldAnchorRegistry 读取后隐藏） =================
module nd_spawn_player_safe() { color([0.1, 0.8, 0.2]) sphere(r = 0.18, $fn = 6); }
module nd_loot(p = "residential") { color([0.2, 0.4, 0.9]) sphere(r = 0.12, $fn = 6); }
module nd_zed(p = "civilian", group = 1) { color([0.8, 0.2, 0.2]) sphere(r = 0.16, $fn = 6); }
module nd_poi(name = "", kind = "village") { color([0.9, 0.85, 0.2]) sphere(r = 0.2, $fn = 6); }
module nd_extraction(id = "", name = "Helipad") { color([0.0, 0.9, 0.9]) sphere(r = 0.15, $fn = 6); }

// ================= 带家具的建筑组合件（几何 gk_flatten 成一个节点 + 刷新点） =================
// 家具靠后墙/侧墙，门洞一侧留通道；刷新点放在桌面、床面、货架层板或地面。

function nd_table_top(seed) = cw_rnd(seed, 4) == 0 ? 0.02 : 0.77;   // 与 cw_prop_table 的翻倒判定一致

module nd_house_rural(seed = 0, L = 7, D = 5.5, p = "residential")
{
    dx = (cw_rnd(seed + 5, 2) == 0 ? -1 : 1) * L * 0.24;
    gk_flatten()
    {
        cw_bldg_house_rural(seed, L, D);
        translate([-dx * 1.05, D / 2 - 0.72, 0]) cw_prop_bed(seed);
        translate([dx, D / 2 - 0.47, 0]) cw_prop_wardrobe(seed + 1);
        translate([-dx * 0.9, -D / 2 + 0.75, 0]) cw_prop_table(seed + 2, 1.0, 0.6);
        translate([-dx * 0.42, -D / 2 + 0.72, 0]) cw_prop_chair(seed + 3);
        if (cw_rnd(seed + 11, 2) == 0) translate([-L / 2 + 0.55, 0, 0]) rotate([0, 0, -90]) cw_prop_stove(seed);
    }
    translate([-dx * 0.9, -D / 2 + 0.75, nd_table_top(seed + 2)]) nd_loot(p = p);
    translate([-dx * 1.05 + 0.3, D / 2 - 0.72, 0.46]) nd_loot(p = p);
    translate([dx, D / 2 - 1.15, 0]) nd_loot(p = p);
}

module nd_house_two(seed = 0, L = 9, D = 7, p = "residential")
{
    dx = (cw_rnd(seed + 3, 2) == 0 ? -1 : 1) * L * 0.2;
    gk_flatten()
    {
        cw_bldg_house_two(seed, L, D);
        translate([-dx * 1.3, D / 2 - 0.75, 0]) cw_prop_sofa(seed);
        translate([dx * 1.4, D / 2 - 0.5, 0]) cw_prop_shelf_wood(seed + 1, 1.6);
        translate([-dx * 1.2, -0.9, 0]) cw_prop_table(seed + 2);
        translate([-dx * 1.2 + 0.8, -0.5, 0]) cw_prop_chair(seed + 3);
        translate([L / 2 - 0.6, -D / 2 + 1.6, 0]) rotate([0, 0, 90]) cw_prop_stove(seed + 4);
        translate([-L / 2 + 1.2, -D / 2 + 0.6, 0]) cw_prop_trash(seed + 5);
    }
    translate([-dx * 1.2, -0.9, nd_table_top(seed + 2)]) nd_loot(p = p);
    translate([dx * 1.4, D / 2 - 0.5, 0.67]) nd_loot(p = p);
    translate([-dx * 1.3, D / 2 - 0.85, 0.5]) nd_loot(p = p);
    translate([L / 2 - 0.7, -D / 2 + 0.8, 0]) nd_loot(p = p);
}

module nd_barn(seed = 0, L = 12, D = 8, p = "farm")
{
    gk_flatten()
    {
        cw_bldg_barn(seed, L, D);
        translate([-L / 2 + 1.0, D / 2 - 1.0, 0]) cw_prop_haybale(seed);
        translate([-L / 2 + 2.3, D / 2 - 1.1, 0]) cw_prop_haybale(seed + 1);
        translate([L / 2 - 1.4, D / 2 - 0.55, 0]) cw_prop_workbench(seed);
        translate([L / 2 - 1.0, -D / 2 + 1.2, 0]) cw_prop_barrel(seed);
    }
    translate([L / 2 - 1.4, D / 2 - 0.55, 0.93]) nd_loot(p = p);
    translate([-L / 2 + 1.2, -D / 2 + 1.0, 0]) nd_loot(p = p);
    translate([0, D / 2 - 1.0, 0]) nd_loot(p = p);
}

module nd_shed(seed = 0, p = "industrial")
{
    gk_flatten()
    {
        cw_bldg_shed(seed);
        translate([-0.5, 0.85, 0]) cw_prop_shelf_wood(seed, 1.4);
    }
    translate([-0.5, 0.85, 0.67]) nd_loot(p = p);
}

module nd_garage(seed = 0, p = "industrial")
{
    gk_flatten()
    {
        cw_bldg_garage(seed);
        translate([0, 2.85, 0]) cw_prop_workbench(seed);
        translate([-2.35, 0.6, 0]) rotate([0, 0, -90]) cw_prop_locker(seed, 2);
        translate([2.1, 1.6, 0]) cw_prop_tires(seed);
        translate([2.2, -1.8, 0]) cw_prop_barrel(seed + 1);
    }
    translate([0.4, 2.85, 0.93]) nd_loot(p = p);
    translate([1.6, -0.4, 0]) nd_loot(p = p);
}

module nd_police(seed = 0)
{
    gk_flatten()
    {
        cw_bldg_police(seed);
        translate([3.5, 3.35, 0]) cw_prop_locker(seed, 3);
        translate([2.6, -0.6, 0]) cw_prop_table(seed + 6);
        translate([2.6, 0.2, 0]) cw_prop_chair(seed + 7);
        translate([-3.6, 1.8, 0]) cw_prop_table(seed + 9, 1.4, 0.8);
        translate([-4.8, 3.2, 0]) cw_prop_shelf_wood(seed, 1.6);
        translate([-0.4, 2.6, 0]) cw_prop_counter(seed, 2.0);
    }
    translate([2.6, -0.6, nd_table_top(seed + 6)]) nd_loot(p = "police");
    translate([-3.6, 1.8, nd_table_top(seed + 9)]) nd_loot(p = "police");
    translate([-4.8, 3.2, 0.67]) nd_loot(p = "police");
    translate([3.5, 2.6, 0]) nd_loot(p = "police");
}

module nd_gas_shop(seed = 0, msg = "SHOP 24")
{
    gk_flatten()
    {
        cw_bldg_gas_shop(seed, msg = msg);
        translate([-2.6, 2.1, 0]) cw_prop_shelf_market(seed, 2.4);
        translate([0.4, 2.1, 0]) cw_prop_shelf_market(seed + 3, 2.0);
        translate([3.6, 0.9, 0]) rotate([0, 0, 90]) cw_prop_counter(seed, 2.0);
        translate([-3.4, -1.2, 0]) cw_prop_cart_shop(seed);
    }
    translate([-2.6, 2.2, 0.58]) nd_loot(p = "commercial");
    translate([0.4, 2.2, 1.13]) nd_loot(p = "commercial");
    translate([3.6, 0.9, 1.05]) nd_loot(p = "commercial");
    translate([-1.0, -1.5, 0]) nd_loot(p = "commercial");
}

module nd_motel(seed = 0, rooms = 5)
{
    W = 4.0;
    gk_flatten()
    {
        cw_bldg_motel(seed, rooms, W);
        for (i = [0 : rooms - 1]) translate([-rooms * W / 2 + W / 2 + i * W + 0.6, 2.3, 0]) cw_prop_bed(seed + i);
    }
    for (i = [0 : rooms - 1]) translate([-rooms * W / 2 + W / 2 + i * W + 0.9, 2.3, 0.46]) nd_loot(p = "residential");
}

module nd_shop_row(seed = 0, L = 10, D = 6)
{
    gk_flatten()
    {
        cw_bldg_shop_row(seed, L, D);
        translate([-1.5, D / 2 - 0.55, 0]) cw_prop_shelf_market(seed, 2.4);
        translate([1.0, 0.4, 0]) cw_prop_counter(seed, 2.0);
    }
    translate([-1.5, D / 2 - 0.45, 0.58]) nd_loot(p = "commercial");
    translate([1.0, 0.4, 1.05]) nd_loot(p = "commercial");
    translate([-3.8, -1.5, 0]) nd_loot(p = "commercial");
}

module nd_supermarket(seed = 0, L = 18, D = 12)
{
    gk_flatten()
    {
        cw_bldg_supermarket(seed, L, D);
        for (i = [0 : 2], j = [0 : 1]) translate([[-5.5, 1.0, 5.5][i], [-0.5, 2.6][j], 0]) cw_prop_shelf_market(seed + i * 3 + j, 2.4);
        translate([-6.5, -4.0, 0]) cw_prop_counter(seed, 2.4);
        translate([4.0, -3.6, 0]) cw_prop_cart_shop(seed + 1);
        translate([6.0, -4.4, 0]) cw_prop_cart_shop(seed + 2);
    }
    for (x = [-5.5, 1.0, 5.5]) translate([x, -0.4, 0.58]) nd_loot(p = "commercial");
    translate([1.0, 2.7, 1.13]) nd_loot(p = "commercial");
    translate([-6.5, -4.0, 1.05]) nd_loot(p = "commercial");
    translate([7.5, 4.5, 0]) nd_loot(p = "commercial");
}

module nd_panel_flat(seed = 0, floors = 4, L = 16, D = 10)
{
    dx = -L * 0.18;
    gk_flatten()
    {
        cw_bldg_panel_flat(seed, floors, L, D);
        translate([L * 0.22, D / 2 - 0.8, 0]) cw_prop_sofa(seed);
        translate([L * 0.25, 0.2, 0]) cw_prop_table(seed + 2);
        translate([-L / 2 + 1.5, D / 2 - 0.8, 0]) cw_prop_bed(seed + 3);
        translate([L / 2 - 0.6, -1.5, 0]) rotate([0, 0, 90]) cw_prop_wardrobe(seed + 4);
        translate([-L / 2 + 1.0, -D / 2 + 1.0, 0]) cw_prop_trash(seed + 6);
    }
    translate([L * 0.25, 0.2, nd_table_top(seed + 2)]) nd_loot(p = "residential");
    translate([-L / 2 + 1.8, D / 2 - 0.8, 0.46]) nd_loot(p = "residential");
    translate([L / 2 - 1.2, -0.6, 0]) nd_loot(p = "residential");
}

module nd_hospital(seed = 0, floors = 3, L = 18, D = 9)
{
    gk_flatten()
    {
        cw_bldg_hospital(seed, floors, L, D);
        for (i = [0 : 3]) translate([[-6.5, -3.4, 3.4, 6.5][i], D / 2 - 1.35, 0]) rotate([0, 0, 90]) cw_prop_bed_hospital(seed + i * 5);
        translate([-5.0, -2.6, 0]) cw_prop_counter(seed, 2.4);
        translate([6.8, -2.6, 0]) cw_prop_locker(seed, 2);
        translate([0, 2.2, 0]) cw_prop_blood_stain(seed, 1.5);
    }
    for (x = [-5.0, -1.9, 1.9, 5.0]) translate([x, D / 2 - 1.2, 0]) nd_loot(p = "medical");
    translate([-5.0, -2.6, 1.05]) nd_loot(p = "medical");
    translate([6.8, -1.8, 0]) nd_loot(p = "medical");
}

module nd_barracks(seed = 0, L = 14, D = 6, p = "military")
{
    gk_flatten()
    {
        cw_bldg_barracks(seed, L, D);
        for (i = [0 : 3]) translate([[-0.38, -0.15, 0.15, 0.38][i] * L, D / 2 - 0.55, 0]) cw_prop_bed(seed + i);
        translate([-L / 2 + 0.9, -D / 2 + 0.5, 0]) cw_prop_locker(seed, 2);
        translate([L / 2 - 0.9, -D / 2 + 0.5, 0]) cw_prop_locker(seed + 4, 2);
    }
    translate([-L * 0.38, D / 2 - 0.55, 0.46]) nd_loot(p = p);
    translate([L * 0.15, D / 2 - 0.55, 0.46]) nd_loot(p = p);
    translate([L / 2 - 0.9, -D / 2 + 1.2, 0]) nd_loot(p = p);
}

module nd_hq(seed = 0, L = 12, D = 8)
{
    gk_flatten()
    {
        cw_bldg_hq(seed, L, D);
        translate([-3.0, 1.2, 0]) cw_prop_table(seed + 2, 1.6, 0.9);
        translate([3.6, D / 2 - 0.65, 0]) cw_prop_locker(seed, 3);
        translate([-4.6, D / 2 - 0.5, 0]) cw_prop_shelf_wood(seed, 1.6);
    }
    translate([-3.0, 1.2, nd_table_top(seed + 2)]) nd_loot(p = "military");
    translate([3.6, D / 2 - 1.3, 0]) nd_loot(p = "military");
    translate([-4.6, D / 2 - 0.5, 0.67]) nd_loot(p = "military");
}

module nd_warehouse(seed = 0, L = 12, D = 8, p = "industrial")
{
    gk_flatten()
    {
        cw_bldg_warehouse(seed, L, D);
        for (i = [0 : 2]) translate([(i - 1) * L * 0.28, D / 2 - 0.5, 0]) cw_prop_shelf_wood(seed + i, 1.6);
        translate([L / 2 - 1.2, -D / 2 + 1.2, 0]) cw_prop_pallet(seed);
        translate([-L / 2 + 1.0, 0, 0]) cw_prop_barrel(seed + 1);
    }
    translate([-L * 0.28, D / 2 - 0.5, 0.67]) nd_loot(p = p);
    translate([L * 0.28, D / 2 - 0.5, 1.22]) nd_loot(p = p);
    translate([L / 2 - 1.2, -D / 2 + 1.2, 0.2]) nd_loot(p = p);
}

module nd_church(seed = 0, L = 14, D = 8)
{
    gk_flatten()
    {
        cw_bldg_church(seed, L, D);
        for (x = [-2.0, 0.2, 2.4], sy = [-1, 1]) translate([x, sy * 1.7, 0]) rotate([0, 0, 90]) cw_prop_bench();
        translate([L / 2 - 1.3, 0, 0]) cw_prop_table(seed + 1, 1.6, 0.8);
        translate([-3.6, 1.6, 0]) cw_prop_body_bag(seed);
        translate([-3.8, -1.7, 0]) cw_prop_body_bag(seed + 3);
    }
    translate([L / 2 - 1.3, 0, nd_table_top(seed + 1)]) nd_loot(p = "residential");
    translate([-5.0, 0.2, 0]) nd_loot(p = "medical");
}

module nd_station(seed = 0, msg = "GORKA")
{
    gk_flatten()
    {
        cw_bldg_station(seed, msg = msg);
        for (x = [-3.5, 3.5]) translate([x, 2.6, 0]) cw_prop_bench();
        translate([-5.0, -1.8, 0]) cw_prop_counter(seed, 2.0);
    }
    translate([-5.0, -1.8, 1.05]) nd_loot(p = "commercial");
    translate([4.5, -1.5, 0]) nd_loot(p = "residential");
}

// 坟墓：土堆 + 木十字
module nd_grave(seed = 0)
{
    color(cw_SOILC()) translate([0, 0, 0]) scale([0.6, 1.2, 0.3]) sphere(r = 1, $fn = 8);
    color(cw_WOODD())
    {
        translate([0, 1.25, 0.6]) cube([0.08, 0.08, 1.2], center = true);
        translate([0, 1.25, 0.95]) cube([0.5, 0.07, 0.07], center = true);
    }
}

// 尸袋排（撤离点 / 墓地等待掩埋）
module nd_body_bags(seed = 0, n = 4)
{
    gk_flatten() for (i = [0 : n - 1])
        translate([i * 0.9, cw_rndr(seed + i, -0.2, 0.2), 0]) rotate([0, 0, 90 + cw_rnd(seed + i, 16) - 8]) cw_prop_body_bag(seed + i);
}

// ================= 地形 =================
TERR = ["gkterr1", [1000, 1000], [176, 176], 7, [0, 2.6, 0.5], undef, "temperate",
    [
        ["mountain", [-260, 380], 190, 52, 0.6],
        ["mountain", [80, 430], 170, 44, 0.55],
        ["ridge", [[-430, 310], [-180, 400], [60, 390], [270, 340]], 130, 24],
        ["mountain", [-420, 60], 130, 20, 0.5],
        ["plateau", [350, 140], 155, 12],
        ["plateau", [-120, -430], 100, 5],
        ["lake", [-330, -330], 55, 2.6],
        ["river", [[-60, 470], [-30, 300], [-110, 160], [-60, 20], [20, -160], [-20, -330], [30, -500]], 12, 2.4],
        // 主公路 + 军事基地支路 + 原有支路
        ["road", [[-500, -140], [-390, -110], [-300, -150], [-210, -110], [-120, -150], [-30, -158], [60, -150], [150, -110], [240, -140], [330, -100], [420, -130], [500, -115]], 8],
        ["road", [[150, -110], [185, -10], [240, 80], [292, 150]], 13.6944233],
        ["road", [[240, -140], [240, -216]], 7],
        ["road", [[60, -150], [75, -240], [95, -308]], 6],
        ["road", [[-390, -110], [-380, -20], [-350, 60], [-345, 100]], 5],
        // 新据点支路 / 进场路
        ["road", [[-210, -112], [-208, -150], [-206, -182]], 5],
        ["road", [[-210, -108], [-206, -63]], 4],
        ["road", [[-150, -134], [-150, -121]], 8],
        ["road", [[-88, -155], [-88, -167]], 5],
        ["road", [[52, -153], [52, -166]], 6],
        ["road", [[100, -131], [104, -103]], 6],
        ["road", [[104, -61], [60, 20], [-4, 92]], 4],
        ["road", [[180, -121], [182, -145]], 6],
        ["road", [[-384, -58], [-355, -60]], 5],
        ["road", [[207, 27], [195, 30]], 6],
        ["road", [[50, -297], [345, -297]], 4],      // 铁路路基
        // 建筑基座
        ["pad", [-302, -180], [104, 52], 0],
        ["pad", [-205, -205], [64, 46], 0],
        ["pad", [-150, -108], [48, 28], 0],
        ["pad", [-88, -180], [40, 26], 0],
        ["pad", [-30, -180], [46, 32], 0],
        ["pad", [53, -180], [38, 28], 0],
        ["pad", [105, -82], [60, 42], 0],
        ["pad", [182, -160], [44, 30], 0],
        ["pad", [245, -250], [112, 82], 0],
        ["pad", [150, -286], [56, 20], 0],
        ["pad", [95, -332], [60, 44], 0],
        ["pad", [-330, -60], [50, 36], 0],
        ["pad", [-200, -50], [34, 26], 0],
        ["pad", [-345, 110], [40, 30], 0],
        ["pad", [160, 30], [70, 62], 0],
        ["pad", [340, 165], [90, 64], 0],
        ["pad", [-20, 95], [32, 24], 0],
        ["pad", [-60, 170], [18, 14], 0],
        ["pad", [-140, 240], [34, 26], 0],
        ["pad", [-246, -318], [28, 22], 0]
    ]];

gk_terrain(TERR);

// 玩家安全出生点（新手区外围，远离感染者刷新点）
ter_place(TERR, -320, -165) nd_spawn_player_safe();
ter_place(TERR, -44, -190) nd_spawn_player_safe();
ter_place(TERR, -150, -126) nd_spawn_player_safe();
ter_place(TERR, 188, -279) nd_spawn_player_safe();
ter_place(TERR, 288, 151) nd_spawn_player_safe();

// ================= 公路桥（跨河点 ~(17,-154)，锚下切带外路面） =================
translate([17, -154, gk_terrain_height(TERR, 40, -152)])
    rotate([0, 0, 5]) cw_prop_bridge(L = 34, W = 9);

// ================= POI 1：斯塔里萨德村（pad -302,-180；公路在北） =================
ter_place(TERR, -300, -174, dz = 0)
{
    nd_poi(name = "Stary Sad", kind = "village");
    // 北排：面朝公路
    translate([-15, 4, 0]) rotate([0, 0, 180]) nd_house_rural(1);
    translate([0, 5, 0]) rotate([0, 0, 172]) nd_house_rural(4, L = 8, D = 6);
    translate([15, 4, 0]) rotate([0, 0, 188]) nd_house_rural(6);
    translate([-31, 3, 0]) rotate([0, 0, 180]) nd_house_two(2);
    translate([30, 4, 0]) rotate([0, 0, 180]) nd_house_rural(9);
    // 南排：面朝村中土路
    translate([-28, -22, 0]) rotate([0, 0, 180]) nd_church(3);
    translate([-6, -25, 0]) rotate([0, 0, 180]) nd_house_rural(12);
    translate([8, -25, 0]) rotate([0, 0, 180]) nd_house_two(7);
    translate([30, -22, 0]) rotate([0, 0, 180]) nd_barn(3);
    translate([21, -31, 0]) nd_shed(2, p = "farm");
    translate([-14, -31, 0]) cw_bldg_outhouse(1);
    translate([-4, -9, 0]) rotate([0, 0, 20]) cw_bldg_ruin(seed = 2, L = 7, D = 5.5);
    // 村中院落
    translate([9, -9, 0]) cw_prop_well(seed = 0);
    translate([-16, -17, 0]) cw_prop_water_pump(seed = 1);
    translate([-13, -8, 0]) cw_prop_woodpile(seed = 1);
    translate([-20, -11, 0]) cw_prop_fence_barbed(len = 7);
    translate([13, -12, 0]) cw_prop_fence_barbed(len = 8);
    translate([19, -6, 0]) cw_prop_barrel(seed = 2);
    translate([5, -12.5, 0]) cw_prop_campfire(seed = 1);
    translate([40, -6, 0]) cw_nature_field(L = 12, D = 9, seed = 2);
    translate([-44, -6, 0]) cw_nature_field(L = 10, D = 9, seed = 5);
    translate([-20, -13, 0]) rotate([0, 0, 25]) cw_prop_body_bag(seed = 4);
    translate([-22.5, -14.5, 0]) rotate([0, 0, 15]) cw_prop_body_bag(seed = 7);
    translate([-2, -16, 0]) cw_prop_blood_stain(seed = 3);
    translate([24, -3, 0]) cw_prop_trash(seed = 4);
    translate([-15, 1.2, 1.6]) cw_prop_graffiti("HELP", 0.8, seed = 0);
    // 手摆物资（首轮必有）
    translate([3, -14, 0]) cw_item_bedroll(seed = 1);
    translate([6.8, -11, 0]) cw_item_can(seed = 3);
    translate([16, 0.5, 0]) cw_wpn_mosin(seed = 1);
    translate([-9, 1, 0]) cw_item_jerrycan(seed = 2);
    translate([-15, 4, 0]) cw_item_can(seed = 5);
    translate([0, 5, 0]) cw_item_medkit();
    translate([15, 4, 0]) cw_wpn_shotgun(seed = 3);
    translate([10, -8.2, 0]) cw_item_water_bottle(seed = 1);
    // 感染者
    translate([-8, -14, 0]) nd_zed(p = "civilian", group = 2);
    translate([20, -12, 0]) nd_zed(p = "civilian", group = 2);
    translate([-30, -9, 0]) nd_zed(p = "civilian", group = 1);
    translate([2, 11, 0]) nd_zed(p = "civilian", group = 2);
    translate([36, 1, 0]) nd_zed(p = "civilian", group = 1);
    translate([-44, -14, 0]) nd_zed(p = "civilian", group = 2);
    translate([30, -14, 0]) nd_zed(p = "civilian", group = 1);
}
ter_place(TERR, -335, -140, dz = 0)
    rotate([0, 0, -24]) cw_prop_sign_town(seed = 1);
ter_place(TERR, -262, -158, dz = 0)
    cw_nature_tree_dead(s = 1.6, seed = 2);

// ================= POI 2：曙光农场（pad -205,-205；土路从北接公路） =================
ter_place(TERR, -205, -205, dz = 0)
{
    nd_poi(name = "Zarya Farm", kind = "farm");
    translate([-12, 6, 0]) rotate([0, 0, 180]) nd_barn(5, L = 14, D = 9);
    translate([-10, -12, 0]) nd_barn(8, L = 10, D = 7);
    translate([16, 0, 0]) rotate([0, 0, -90]) nd_house_two(11, p = "farm");
    translate([-24, -9, 0]) rotate([0, 0, 90]) nd_shed(3, p = "farm");
    translate([24, -14, 0]) nd_shed(6, p = "farm");
    translate([4, 13, 0]) cw_bldg_silo(seed = 0, h = 11);
    translate([9.6, 13, 0]) cw_bldg_silo(seed = 1, h = 9);
    translate([2, -4, 0]) rotate([0, 0, 30]) cw_veh_tractor(seed = 2);
    translate([-24, 4, 0]) cw_prop_haybale(seed = 1);
    translate([-26, 0, 0]) cw_prop_haybale(seed = 2);
    translate([22, -6, 0]) cw_prop_haybale(seed = 3);
    translate([4, -14, 0]) cw_prop_water_pump(seed = 2);
    translate([-28, -18, 0]) cw_prop_fence_barbed(len = 10);
    translate([-12, -21, 0]) cw_prop_fence_barbed(len = 12);
    translate([10, -21, 0]) cw_prop_fence_barbed(len = 12);
    translate([5, 2, 0]) cw_prop_blood_stain(seed = 8);
    translate([7, 3, 0]) rotate([0, 0, 70]) cw_prop_body_bag(seed = 9);
    translate([3.4, -13.6, 0]) cw_item_water_bottle(seed = 2);
    translate([-6.5, -10.5, 0]) cw_item_axe();
    translate([0, 0, 0]) nd_zed(p = "civilian", group = 2);
    translate([-20, -4, 0]) nd_zed(p = "civilian", group = 1);
    translate([18, -12, 0]) nd_zed(p = "civilian", group = 2);
    translate([-4, 19, 0]) nd_zed(p = "civilian", group = 1);
}
ter_place(TERR, -200, -240, dz = 0) cw_nature_field(L = 18, D = 12, seed = 3);
ter_place(TERR, -178, -238, dz = 0) cw_nature_field(L = 14, D = 12, seed = 7);

// ================= POI 3：公路旅馆（pad -150,-108；公路在南） =================
ter_place(TERR, -150, -108, dz = 0)
{
    nd_poi(name = "Highway Motel", kind = "motel");
    translate([-6, 4, 0]) nd_motel(2, rooms = 5);
    translate([15, 3, 0]) nd_gas_shop(5, msg = "DINER");
    translate([0, -9, 0]) cw_ground_parking(L = 30, D = 7, seed = 3);
    translate([-8, -8, 0]) rotate([0, 0, 8]) cw_veh_lada(seed = 3);
    translate([6, -9, 0]) rotate([0, 0, -5]) cw_veh_car_burnt(seed = 1);
    translate([14, -10, 0]) rotate([0, 0, 170]) cw_veh_uaz_van(seed = 4);
    translate([21, -3, 0]) cw_prop_dumpster();
    translate([19, -6, 0]) cw_prop_trash(seed = 7);
    translate([-18, -4, 0]) cw_prop_trash(seed = 9);
    translate([-12, -2.6, 0]) cw_prop_blood_stain(seed = 5);
    translate([-11, -3, 0]) cw_item_jacket(seed = 2);
    translate([-6, -4, 0]) nd_zed(p = "civilian", group = 2);
    translate([12, -6, 0]) nd_zed(p = "civilian", group = 1);
    translate([-19, 9, 0]) nd_zed(p = "civilian", group = 1);
}

// ================= POI 4：波戈列洛夫卡（路边小村，pad -88,-180） =================
ter_place(TERR, -88, -180, dz = 0)
{
    nd_poi(name = "Pogorelovka", kind = "village");
    translate([-10, 3, 0]) rotate([0, 0, 180]) nd_house_rural(14);
    translate([8, 4, 0]) rotate([0, 0, 180]) nd_house_rural(15, L = 8, D = 6);
    translate([14, -6, 0]) rotate([0, 0, 10]) cw_bldg_ruin(seed = 4, L = 7, D = 6);
    translate([-14, -7, 0]) nd_shed(5, p = "residential");
    translate([2, -6, 0]) cw_prop_well(seed = 3);
    translate([16, -6, 0]) cw_prop_debris(seed = 6);
    translate([-3, -9, 0]) cw_prop_woodpile(seed = 4);
    translate([0, -2, 0]) nd_zed(p = "civilian", group = 2);
    translate([16, 6, 0]) nd_zed(p = "civilian", group = 1);
}
ter_place(TERR, -102, -158, dz = 0) rotate([0, 0, 180]) cw_bldg_bus_stop(seed = 2);

// ================= POI 5：桥西加油站（pad -30,-180；公路在北） =================
ter_place(TERR, -30, -177, dz = 0)
{
    nd_poi(name = "Gas Station", kind = "gas");
    translate([0, 0, 0]) rotate([0, 0, 180]) cw_bldg_gas_canopy(seed = 0);
    translate([-3.5, 1, 0]) rotate([0, 0, 180]) cw_prop_pump_gas(seed = 0);
    translate([0, 1, 0]) rotate([0, 0, 180]) cw_prop_pump_gas(seed = 1);
    translate([3.5, 1, 0]) rotate([0, 0, 180]) cw_prop_pump_gas(seed = 2);
    translate([11, 3, 0]) cw_prop_sign_pylon(seed = 0);
    translate([9, -5, 0]) cw_prop_fueltank(seed = 0);
    translate([-10, 3, 0]) rotate([0, 0, 100]) cw_veh_wreck(seed = 2);
    translate([-12, -4, 0]) cw_prop_tires(seed = 1);
    translate([-8, -6, 0]) cw_prop_debris(seed = 3);
    translate([-5.5, -3, 0]) cw_prop_cart_shop(seed = 2);
    translate([-17, -12, 0]) rotate([0, 0, 180]) nd_gas_shop(4);
    translate([15, -12, 0]) rotate([0, 0, 180]) nd_garage(2);
    translate([-19, 8, 0]) rotate([0, 0, 12]) cw_veh_car_burnt(seed = 3);
    translate([6, 10, 0]) cw_prop_sign_quarantine(seed = 0);
    translate([-6, 10, 0]) cw_prop_jersey_barrier(len = 3);
    translate([-7, -11, 0]) cw_prop_trash(seed = 2);
    translate([5, -13, 0]) rotate([0, 0, 10]) cw_prop_body_bag(seed = 2);
    translate([4, -11, 0]) cw_prop_blood_stain(seed = 2);
    // 手摆物资：出生点正前方的油壶（agent 回归脚本依赖它的位置）
    translate([1.5, -3.5, 0]) cw_item_jerrycan(seed = 1);
    translate([4.5, -4.2, 0]) cw_item_jerrycan(seed = 4);
    translate([0, -4.9, 0]) cw_item_medkit();
    translate([17, -10.5, 0]) cw_item_crowbar();
    translate([-24, -4, 0]) nd_zed(p = "civilian", group = 2);
    translate([14, 6, 0]) nd_zed(p = "civilian", group = 1);
    translate([-2, -16, 0]) nd_zed(p = "industrial", group = 1);
}

// ================= POI 6：桥头前哨（检疫关卡，pad 53,-180） =================
ter_place(TERR, 50, -176, dz = 0)
{
    nd_poi(name = "Most Outpost", kind = "military");
    cw_bldg_bunker(seed = 0);
    translate([-1, 4.6, 0]) cw_prop_wall_sandbag(len = 4);
    translate([-5.5, 2, 0]) rotate([0, 0, 90]) cw_prop_wall_sandbag(len = 3);
    translate([5, 4.4, 0]) rotate([0, 0, 20]) cw_prop_hedgehog();
    translate([14, 6, 0]) cw_bldg_guard_tower(seed = 2);
    translate([16, -6, 0]) rotate([0, 0, 90]) nd_barracks(5, L = 10, D = 5);
    translate([-10, -10, 0]) cw_prop_tent_military(seed = 1);
    translate([7, -11, 0]) cw_prop_tent_medical(seed = 0);
    translate([-14, 4, 0]) rotate([0, 0, 180]) cw_prop_sandbag_nest(seed = 1);
    translate([2, -15.5, 0]) nd_body_bags(seed = 3, n = 4);
    translate([-4, 9, 0]) cw_prop_jersey_barrier(len = 3);
    translate([2, 9, 0]) cw_prop_jersey_barrier(len = 3);
    translate([10, 10, 0]) cw_prop_sign_quarantine(seed = 1, msg = "STOP");
    // 手摆物资
    translate([-4, -4.5, 0]) cw_prop_crate_ammo(seed = 1);
    translate([-2.2, -3.6, 0]) cw_wpn_ak(seed = 1);
    translate([-5.8, -3.2, 0]) cw_item_helmet(seed = 1);
    translate([4.5, -4, 0]) cw_item_ammobox(seed = 2);
    translate([4.5, -8.5, 0]) cw_item_medical_bag(seed = 0);
    translate([-12, -13, 0]) nd_loot(p = "military");
    translate([10, -14, 0]) nd_loot(p = "medical");
    translate([-8, 6, 0]) nd_zed(p = "military", group = 2);
    translate([10, -14, 0]) nd_zed(p = "military", group = 2);
    translate([18, 9, 0]) nd_zed(p = "military", group = 1);
}
// 桥头封锁线（路面上）
ter_place(TERR, -2, -158, dz = 0) rotate([0, 0, 95]) cw_prop_barrier_gate(seed = 0);
ter_place(TERR, 0, -155, dz = 0) rotate([0, 0, 40]) cw_prop_hedgehog();
ter_place(TERR, 36, -152, dz = 0) rotate([0, 0, -15]) cw_prop_hedgehog();
ter_place(TERR, 40, -156, dz = 0) rotate([0, 0, 5]) cw_prop_jersey_barrier(len = 3);

// ================= POI 7：新德沃尔（警察局小镇，pad 105,-82） =================
ter_place(TERR, 105, -82, dz = 0)
{
    nd_poi(name = "Novy Dvor", kind = "village");
    translate([0, -2, 0]) cw_ground_road(L = 58, W = 6, seed = 5);
    translate([-14, -12, 0]) nd_police(1);
    translate([8, -12, 0]) nd_house_two(4);
    translate([21, -11, 0]) nd_house_rural(21);
    translate([-19, 9, 0]) rotate([0, 0, 180]) nd_house_rural(22);
    translate([-4, 9, 0]) rotate([0, 0, 180]) nd_garage(5);
    translate([12, 9, 0]) rotate([0, 0, 180]) nd_house_two(9);
    translate([25, 13, 0]) nd_shed(7);
    translate([2, 4.6, 0]) cw_prop_water_pump(seed = 3);
    translate([-8, -1, 0]) rotate([0, 0, 80]) cw_veh_car_burnt(seed = 5);
    translate([17, -3, 0]) rotate([0, 0, 4]) cw_veh_lada(seed = 6);
    translate([0, -19, 0]) cw_prop_sign_quarantine(seed = 2);
    translate([-27, 2, 0]) rotate([0, 0, 90]) cw_prop_barricade(len = 4, seed = 1);
    translate([27, -2, 0]) rotate([0, 0, 90]) cw_prop_barricade(len = 4, seed = 3);
    translate([-9.5, -16.04, 1.6]) cw_prop_graffiti("NO ENTRY", 0.45, seed = 1);
    translate([-6, 2, 0]) cw_prop_trash(seed = 11);
    translate([5, -6, 0]) cw_prop_blood_stain(seed = 7);
    translate([-12.5, -10.5, 0]) cw_wpn_pistol(seed = 2);
    translate([0, -2, 0]) nd_zed(p = "civilian", group = 3);
    translate([-20, 0, 0]) nd_zed(p = "civilian", group = 2);
    translate([20, 1, 0]) nd_zed(p = "civilian", group = 2);
    translate([-14, -18, 0]) nd_zed(p = "civilian", group = 1);
}

// ================= POI 8：卡车站 / 修理厂（pad 182,-160） =================
ter_place(TERR, 182, -160, dz = 0)
{
    nd_poi(name = "Truck Stop", kind = "industrial");
    translate([-8, 3, 0]) rotate([0, 0, 180]) nd_warehouse(5, L = 14, D = 9);
    translate([8, 4, 0]) rotate([0, 0, 180]) nd_garage(8);
    translate([15, 4, 0]) rotate([0, 0, 180]) nd_garage(9);
    translate([-16, -8, 0]) cw_prop_fueltank(seed = 1);
    translate([2, -8, 0]) rotate([0, 0, 10]) cw_veh_truck_canvas(seed = 3);
    translate([13, -9, 0]) rotate([0, 0, -12]) cw_veh_wreck(seed = 5);
    translate([-5, -10, 0]) cw_prop_pallet(seed = 2);
    translate([-3, -12, 0]) cw_prop_barrel(seed = 4);
    translate([-2, -11, 0]) cw_prop_barrel(seed = 5);
    translate([19, -4, 0]) cw_prop_tires(seed = 3);
    translate([7, -12, 0]) cw_item_toolbox(seed = 1);
    translate([0, -3, 0]) nd_zed(p = "industrial", group = 2);
    translate([15, -8, 0]) nd_zed(p = "industrial", group = 1);
}

// ================= POI 9：戈尔卡镇（pad 245,-250；支路从北接入） =================
ter_place(TERR, 240, -246, dz = 0)
{
    nd_poi(name = "Gorka", kind = "town");
    // 街道：南北主街 + 两条东西街
    translate([0, -2, 0]) rotate([0, 0, 90]) cw_ground_road(L = 44, W = 6, seed = 1);
    translate([0, 8, 0]) cw_ground_cross(W = 6, seed = 2);
    translate([-18, 8, 0]) cw_ground_road(L = 28, W = 6, seed = 3);
    translate([18, 8, 0]) cw_ground_road(L = 28, W = 6, seed = 4);
    translate([0, -22, 0]) cw_ground_road(L = 72, W = 6, seed = 6);
    // 北排：板楼 + 教堂 + 医院
    translate([-18, 19, 0]) nd_panel_flat(1, floors = 3, L = 15, D = 9);
    translate([16, 19, 0]) nd_panel_flat(5, floors = 4, L = 14, D = 9);
    translate([30, 17, 0]) cw_bldg_chapel(seed = 0);
    translate([44, 20, 0]) nd_hospital(2);
    // Hospital rooftop rescue pad; the exterior staircase is ordinary walkable geometry.
    translate([47, 20, 9.55]) cw_ground_helipad(S = 7.5);
    translate([47, 20, 9.58]) nd_extraction(id = "gorka_hospital", name = "Gorka Hospital Roof");
    gk_flatten()
    {
        for (i = [0 : 49])
            color([0.39, 0.42, 0.40]) translate([54.5, 3.5 + i * 0.34, 0])
                cube([2.0, 0.36, (i + 1) * 0.20]);
        color([0.39, 0.42, 0.40]) translate([50.0, 20.0, 9.75]) cube([6.5, 2.0, 0.25]);
        // Railings leave the stair entrance and rooftop exit open.
        for (x = [54.4, 56.5], i = [0 : 7])
            color([0.25, 0.28, 0.27]) translate([x, 4.5 + i * 2.1, i * 1.25 + 0.7])
                cube([0.07, 0.07, 1.0]);
    }
    // 中排：超市 + 商铺
    translate([-16, -6, 0]) rotate([0, 0, 180]) nd_supermarket(0, L = 17, D = 11);
    translate([14, -5, 0]) rotate([0, 0, 180]) nd_shop_row(2, L = 11, D = 6);
    translate([-30, -14, 0]) nd_house_rural(8);
    // 南排：面朝南街
    translate([-22, -32, 0]) rotate([0, 0, 180]) nd_panel_flat(7, floors = 3, L = 15, D = 9);
    translate([-6, -31, 0]) rotate([0, 0, 180]) nd_house_rural(31);
    translate([18, -33, 0]) rotate([0, 0, 8]) cw_bldg_ruin(seed = 3, L = 8, D = 6);
    translate([32, -31, 0]) rotate([0, 0, 180]) nd_house_two(33);
    // 西侧车库排
    translate([-44, 2, 0]) rotate([0, 0, 90]) nd_garage(11);
    translate([-44, -5, 0]) rotate([0, 0, 90]) nd_garage(12);
    translate([-44, 12, 0]) rotate([0, 0, 90]) nd_house_two(14);
    // 失败的撤离点（医院前）：医疗帐篷、军车、尸袋、沙袋
    translate([40, -3, 0]) cw_prop_tent_medical(seed = 1);
    translate([49, -3, 0]) cw_prop_tent_medical(seed = 2);
    translate([36, -9, 0]) rotate([0, 0, 15]) cw_veh_truck_canvas(seed = 4);
    translate([41, -12, 0]) nd_body_bags(seed = 11, n = 6);
    translate([54, 6, 0]) rotate([0, 0, 90]) cw_prop_sandbag_nest(seed = 2);
    translate([34, 4, 0]) cw_prop_jersey_barrier(len = 3);
    translate([38, 4, 0]) cw_prop_jersey_barrier(len = 3);
    translate([46, 9, 0]) cw_prop_sign_quarantine(seed = 3, msg = "EVACUATION");
    translate([45, -12.5, 0]) cw_prop_blood_stain(seed = 9, s = 1.4);
    // 街道家具与洗劫痕迹
    translate([7, 12, 0]) cw_bldg_bus_stop(seed = 1);
    translate([-7, 12.5, 0]) cw_prop_kiosk(seed = 0);
    translate([-10.5, 12, 0]) cw_prop_phone_booth(seed = 0);
    translate([-28, 12, 0]) cw_prop_monument(seed = 2);
    translate([24, -14, 0]) cw_prop_playground(seed = 1);
    translate([-32, 4, 0]) cw_prop_dumpster();
    translate([26, 5, 0]) cw_prop_debris(seed = 5);
    for (x = [-24, -4, 16]) translate([x, 11.6, 0]) cw_prop_lamp();
    for (x = [-14, 8]) translate([x, 4.4, 0]) rotate([0, 0, 180]) cw_prop_lamp();
    translate([-6, 6.2, 0]) rotate([0, 0, 4]) cw_veh_lada(seed = 2);
    translate([16, 9.8, 0]) rotate([0, 0, 182]) cw_veh_bus(seed = 1);
    translate([1.5, -12, 0]) rotate([0, 0, 92]) cw_veh_wreck(seed = 3);
    translate([-12, 8, 0]) rotate([0, 0, -6]) cw_veh_car_burnt(seed = 6);
    translate([22, -22, 0]) rotate([0, 0, 170]) cw_veh_car_burnt(seed = 7);
    translate([-2, -40, 0]) rotate([0, 0, 180]) cw_prop_billboard(seed = 0);
    translate([0, -42, 0]) cw_prop_barricade(len = 6, seed = 5);
    translate([-9, -1, 0]) cw_prop_trash(seed = 13);
    translate([10, 2, 0]) cw_prop_trash(seed = 14);
    translate([-30, 6, 0]) cw_prop_trash(seed = 15);
    translate([-12, -11.6, 0]) cw_prop_blood_stain(seed = 12);
    translate([-18, 14.45, 4.6]) cw_prop_graffiti("HELP US", 0.9, seed = 0);
    translate([16, 14.45, 2.4]) cw_prop_graffiti("NO FOOD", 0.6, seed = 2);
    // 手摆物资
    translate([-10, 0.8, 0]) cw_item_can(seed = 2);
    translate([12, -0.5, 0]) cw_wpn_pistol(seed = 1);
    translate([-18, 19, 0]) cw_item_backpack(seed = 5);
    translate([16, 19, 0]) cw_item_ammobox(seed = 6);
    translate([30, 17, 0]) cw_item_lantern(seed = 2);
    translate([-30, -14, 0]) cw_item_bedroll(seed = 6);
    translate([44.5, 13.5, 0]) cw_item_medical_bag(seed = 2);
    translate([43, -7, 0]) nd_loot(p = "military");
    translate([47, -7.5, 0]) nd_loot(p = "medical");
    translate([51, -7, 0]) nd_loot(p = "medical");
    // 感染者：镇上最多
    translate([0, 0, 0]) nd_zed(p = "civilian", group = 3);
    translate([0, -22, 0]) nd_zed(p = "civilian", group = 2);
    translate([-20, 8, 0]) nd_zed(p = "civilian", group = 2);
    translate([20, 8, 0]) nd_zed(p = "civilian", group = 2);
    translate([-30, -10, 0]) nd_zed(p = "civilian", group = 1);
    translate([30, -12, 0]) nd_zed(p = "civilian", group = 1);
    translate([-10, -40, 0]) nd_zed(p = "civilian", group = 2);
    translate([40, -30, 0]) nd_zed(p = "civilian", group = 2);
    translate([-40, 18, 0]) nd_zed(p = "civilian", group = 1);
    translate([-16, -6, 0]) nd_zed(p = "civilian", group = 1);
    translate([44, -7, 0]) nd_zed(p = "military", group = 3);
    translate([50, 8, 0]) nd_zed(p = "military", group = 2);
    translate([44, 12, 0]) nd_zed(p = "civilian", group = 2);
}
ter_place(TERR, 232, -150, dz = 0) cw_prop_sign_road(seed = 1);

// ================= POI 10：戈尔卡火车站（pad 150,-286；铁路沿 y=-297） =================
ter_along(TERR, [[50, -297], [345, -297]], step = 20, seed = 9, offset = 10, dz = 0)
    cw_ground_rail(L = 20.2, seed = $seed);
ter_place(TERR, 150, -286, dz = 0)
{
    nd_poi(name = "Gorka Station", kind = "station");
    translate([0, 2, 0]) rotate([0, 0, 180]) nd_station(2, msg = "GORKA");
    translate([-22, 4, 0]) cw_bldg_water_tower(seed = 2);
    translate([18, 5, 0]) rotate([0, 0, 180]) nd_warehouse(7, L = 10, D = 7);
    translate([-14, 7, 0]) rotate([0, 0, 30]) cw_veh_car_burnt(seed = 8);
    translate([8, 10, 0]) cw_prop_pallet(seed = 5);
    translate([10, 11, 0]) cw_prop_crate_ammo(seed = 7);
    translate([-6, 9, 0]) nd_body_bags(seed = 21, n = 3);
    translate([0, 9, 0]) nd_zed(p = "civilian", group = 2);
    translate([20, -6, 0]) nd_zed(p = "civilian", group = 1);
    translate([-18, -5, 0]) nd_zed(p = "civilian", group = 1);
}
ter_place(TERR, 205, -297, dz = 0.25) cw_veh_freight_car(seed = 1);
ter_place(TERR, 220, -297, dz = 0.25) cw_veh_tank_car(seed = 0);
ter_place(TERR, 235, -297, dz = 0.25) cw_veh_freight_car(seed = 2);
ter_place(TERR, 262, -299.5, dz = 0.1) rotate([0, 0, 8]) cw_veh_freight_car(seed = 3);
ter_place(TERR, 236, -301.5, dz = 0) nd_loot(p = "industrial");
ter_place(TERR, 264, -295, dz = 0) nd_loot(p = "military");

// ================= POI 11：红锻工厂（pad 95,-332；支路从北接入） =================
ter_place(TERR, 95, -332, dz = 0)
{
    nd_poi(name = "Red Forge Factory", kind = "factory");
    translate([-12, 6, 0]) cw_bldg_factory_hall(seed = 1, L = 15, D = 10);
    translate([9, 9, 0]) cw_bldg_factory_saw(seed = 2, L = 13, D = 9);
    translate([20, 13, 0]) cw_bldg_smokestack(seed = 0, h = 14);
    translate([15, -10, 0]) nd_warehouse(3, L = 12, D = 8);
    translate([-26, 8, 0]) rotate([0, 0, 90]) nd_barracks(6, L = 12, D = 6, p = "industrial");
    // 厂房内：工作台与更衣柜
    translate([-17, 9.6, 0]) cw_prop_workbench(seed = 3);
    translate([-7, 9.6, 0]) cw_prop_locker(seed = 2, n = 3);
    translate([6, 11.5, 0]) cw_prop_workbench(seed = 4);
    translate([-18, -10, 0]) cw_prop_fueltank(seed = 0);
    translate([-22, -4, 0]) cw_prop_barrel(seed = 1);
    translate([-19.6, -4.6, 0]) cw_prop_barrel(seed = 6);
    translate([-2, -6, 0]) rotate([0, 0, 20]) cw_veh_truck_canvas(seed = 1);
    translate([-10, -14, 0]) rotate([0, 0, 170]) cw_veh_tractor(seed = 1);
    translate([2, -14, 0]) cw_prop_pallet(seed = 2);
    translate([5, -13.4, 0]) cw_prop_crate_ammo(seed = 3);
    translate([0, -16.5, 0]) cw_prop_tires(seed = 1);
    translate([-6, -16, 0]) cw_prop_debris(seed = 2);
    for (x = [-22, -6, 12]) translate([x, -19, 0]) cw_prop_fence_concrete(len = 13);
    translate([-24, 16, 0]) cw_prop_lamp();
    translate([10, -2, 0]) cw_prop_lamp();
    translate([-6, -19.1, 1.2]) cw_prop_graffiti("DEAD ZONE", 0.5, seed = 2);
    // 手摆物资
    translate([4, -12.2, 0]) cw_wpn_shotgun(seed = 1);
    translate([-16.8, -6.5, 0]) cw_item_jerrycan(seed = 3);
    translate([-12, 6, 0]) cw_prop_crate_ammo(seed = 6);
    translate([-13.5, 5, 0]) cw_wpn_svd(seed = 2);
    translate([9, 9, 0]) cw_item_jerrycan(seed = 5);
    translate([-17, 9.6, 0.93]) cw_item_toolbox(seed = 2);
    translate([-14, 3.5, 0]) nd_loot(p = "industrial");
    translate([-7, 8.8, 0]) nd_loot(p = "industrial");
    translate([6, 11.5, 0.93]) nd_loot(p = "industrial");
    translate([12, 6, 0]) nd_loot(p = "industrial");
    translate([0, 0, 0]) nd_zed(p = "industrial", group = 3);
    translate([10, -16, 0]) nd_zed(p = "industrial", group = 1);
    translate([-20, -14, 0]) nd_zed(p = "industrial", group = 2);
    translate([-10, 4, 0]) nd_zed(p = "industrial", group = 1);
}

// ================= POI 12：锯木场（pad -330,-60；土路西接） =================
ter_place(TERR, -330, -60, dz = 0)
{
    nd_poi(name = "Sawmill", kind = "industrial");
    translate([0, 4, 0]) cw_bldg_lumber_shed(seed = 0);
    translate([-16, 8, 0]) cw_prop_log_pile(seed = 1, L = 6);
    translate([14, 10, 0]) rotate([0, 0, 10]) cw_prop_log_pile(seed = 2, L = 5);
    translate([-15, -10, 0]) nd_house_rural(41, p = "industrial");
    translate([16, -10, 0]) nd_shed(9);
    translate([4, -11, 0]) rotate([0, 0, 75]) cw_veh_truck_canvas(seed = 5);
    translate([-4, -14, 0]) rotate([0, 0, -20]) cw_veh_tractor(seed = 3);
    translate([21, 2, 0]) cw_bldg_hunting_stand(seed = 1);
    translate([8, -3, 0]) cw_prop_woodpile(seed = 7);
    translate([-6, -5, 0]) cw_prop_campfire(seed = 6);
    translate([1.4, 4.2, 0.9]) cw_item_axe();
    translate([0, -4, 0]) nd_loot(p = "industrial");
    translate([-8, 5, 0]) nd_loot(p = "industrial");
    translate([0, -4, 0]) nd_zed(p = "industrial", group = 2);
    translate([-15, 2, 0]) nd_zed(p = "industrial", group = 1);
    translate([18, -2, 0]) nd_zed(p = "wilderness", group = 1);
}

// ================= POI 13：老墓地（pad -200,-50；土路南接公路） =================
ter_place(TERR, -200, -50, dz = 0)
{
    nd_poi(name = "Old Cemetery", kind = "cemetery");
    translate([0, 7, 0]) rotate([0, 0, 180]) cw_bldg_chapel(seed = 4);
    gk_flatten() for (i = [0 : 3], j = [0 : 2])
        translate([-10 + i * 4.2 + (j % 2) * 1.2, -2 - j * 3.4, 0]) rotate([0, 0, cw_rnd(i * 3 + j, 10) - 5]) nd_grave(i * 5 + j);
    for (x = [-12, -4, 4, 12]) translate([x, -12, 0]) cw_prop_fence_chain(len = 8);
    for (y = [-6, 4]) translate([-15.5, y, 0]) rotate([0, 0, 90]) cw_prop_fence_chain(len = 10);
    translate([9, -4, 0]) nd_body_bags(seed = 31, n = 5);
    translate([10, -8, 0]) color(cw_SOILC()) cube([5, 2.2, 0.3], center = true);
    translate([10, -8, 0.2]) color([0.18, 0.13, 0.09]) cube([4.4, 1.6, 0.12], center = true);
    translate([0, 7, 0]) nd_loot(p = "residential");
    translate([12, -2, 0]) nd_loot(p = "medical");
    translate([0, -4, 0]) nd_zed(p = "civilian", group = 3);
    translate([8, 8, 0]) nd_zed(p = "civilian", group = 1);
}

// ================= POI 14：西山电台（pad -345,110；土路从南接入） =================
ter_place(TERR, -345, 112, dz = 0)
{
    nd_poi(name = "Radio Station", kind = "military");
    translate([0, 3, 0]) cw_prop_antenna(seed = 0);
    translate([-6, -3, 0]) cw_bldg_guard_tower(seed = 1);
    translate([5, -4, 0]) cw_bldg_checkpoint(seed = 2);
    translate([5, 4, 0]) cw_prop_searchlight(seed = 1);
    translate([-2, -6.5, 0]) cw_prop_wall_sandbag(len = 3);
    translate([-7, 5, 0]) cw_prop_barrel(seed = 3);
    translate([7.5, -0.5, 0]) rotate([0, 0, 30]) cw_prop_fence_barbed(len = 5);
    translate([12, 8, 0]) nd_barracks(7, L = 10, D = 5);
    translate([-14, 8, 0]) cw_prop_fueltank(seed = 1);
    translate([-14, -8, 0]) cw_prop_sandbag_nest(seed = 3);
    translate([10, -9, 0]) cw_prop_jersey_barrier(len = 3);
    translate([1.5, -4.8, 0]) cw_item_radio(seed = 1);
    translate([-3.5, -5.2, 0]) cw_item_ammobox(seed = 3);
    translate([3.2, -6.8, 0]) cw_item_medkit();
    translate([0, -8, 0]) nd_zed(p = "military", group = 2);
    translate([10, 0, 0]) nd_zed(p = "military", group = 1);
}

// ================= POI 15：祖布尔要塞（棱堡，pad 160,30；东门接基地支路） =================
ter_place(TERR, 160, 30, dz = 0)
{
    nd_poi(name = "Fort Zubr", kind = "fortress");
    // 城墙（外侧朝外）+ 四角棱堡 + 东门
    translate([0, 22, 0]) rotate([0, 0, 180]) cw_bldg_fort_wall(len = 36, seed = 1);
    translate([0, -22, 0]) cw_bldg_fort_wall(len = 36, seed = 2);
    translate([-22, 0, 0]) rotate([0, 0, -90]) cw_bldg_fort_wall(len = 36, seed = 3);
    translate([22, 12.2, 0]) rotate([0, 0, 90]) cw_bldg_fort_wall(len = 15.6, seed = 4);
    translate([22, -12.2, 0]) rotate([0, 0, 90]) cw_bldg_fort_wall(len = 15.6, seed = 5);
    translate([22, 0, 0]) rotate([0, 0, 90]) cw_bldg_fort_gate(seed = 0);
    for (sx = [-1, 1], sy = [-1, 1]) translate([sx * 22, sy * 22, 0]) cw_bldg_fort_bastion(seed = sx + sy * 2);
    // 院内：营房、指挥所、帐篷、装甲车
    translate([-8, 12, 0]) rotate([0, 0, 180]) nd_barracks(2);
    translate([-8, -12, 0]) nd_barracks(4);
    translate([-14, 0, 0]) rotate([0, 0, 90]) nd_hq(1);
    translate([4, 16, 0]) cw_prop_tent_military(seed = 2);
    translate([11, 16, 0]) cw_prop_tent_military(seed = 3);
    translate([8, -15, 0]) cw_prop_tent_medical(seed = 3);
    translate([8, 7, 0]) rotate([0, 0, 30]) cw_veh_btr(seed = 1);
    translate([9, -7, 0]) rotate([0, 0, -20]) cw_veh_truck_canvas(seed = 6);
    translate([14, 4, 0]) rotate([0, 0, -90]) cw_prop_sandbag_nest(seed = 4);
    translate([-2, 4, 0]) nd_body_bags(seed = 41, n = 4);
    translate([0, -3, 0]) cw_prop_blood_stain(seed = 14, s = 1.3);
    translate([4, 0, 0]) cw_prop_monument(seed = 3);
    // 手摆物资
    translate([2, -5, 0]) cw_wpn_crate(seed = 2);
    translate([-14, 2.5, 0]) cw_item_vest(seed = 1);
    translate([-11.5, 4, 0]) cw_wpn_svd(seed = 3);
    translate([6, 13.5, 0]) cw_prop_crate_ammo(seed = 8);
    translate([12, -12, 0]) cw_item_medical_bag(seed = 3);
    translate([3, 13, 0]) nd_loot(p = "military");
    translate([12, 13, 0]) nd_loot(p = "military");
    translate([6, -12, 0]) nd_loot(p = "medical");
    translate([16, -2, 0]) nd_loot(p = "military");
    // 感染者：要塞驻军
    translate([0, 0, 0]) nd_zed(p = "military", group = 3);
    translate([10, 10, 0]) nd_zed(p = "military", group = 2);
    translate([-8, 0, 0]) nd_zed(p = "military", group = 2);
    translate([30, 0, 0]) nd_zed(p = "military", group = 2);
    translate([-8, 16, 0]) nd_zed(p = "military", group = 1);
    translate([2, -16, 0]) nd_zed(p = "military", group = 2);
}

// ================= POI 16：7 号检查站（基地支路上） =================
ter_place(TERR, 244, 86, dz = 0)
{
    nd_poi(name = "Checkpoint 7", kind = "military");
    rotate([0, 0, 53])
    {
        translate([0, 0, 0]) rotate([0, 0, 90]) cw_prop_barrier_gate(seed = 1);
        translate([-4, -8, 0]) cw_bldg_checkpoint(seed = 3);
        translate([3, -9, 0]) rotate([0, 0, 90]) cw_prop_sandbag_nest(seed = 5);
        translate([6, 3, 0]) rotate([0, 0, 90]) cw_prop_jersey_barrier(len = 3);
        translate([-6, 3, 0]) rotate([0, 0, 90]) cw_prop_jersey_barrier(len = 3);
        translate([10, -2, 0]) rotate([0, 0, 120]) cw_veh_truck_canvas(seed = 7);
        translate([-3, 9, 0]) cw_prop_sign_quarantine(seed = 4, msg = "QUARANTINE");
        translate([2, 6, 0]) nd_body_bags(seed = 51, n = 3);
        translate([-4, -6, 0]) nd_loot(p = "military");
        translate([12, -6, 0]) nd_loot(p = "military");
    }
    translate([0, 4, 0]) nd_zed(p = "military", group = 2);
}

// ================= POI 17：军事基地（pad 340,165；大门朝西接支路） =================
ter_place(TERR, 340, 165, dz = 0)
{
    nd_poi(name = "Military Base", kind = "military");
    // 周界铁丝网（西边留门洞）+ 四角岗塔
    for (x = [-35, -23, -11, 1, 13, 25, 35])
    {
        translate([x, 28, 0]) cw_prop_fence_chain(len = 12);
        translate([x, -28, 0]) cw_prop_fence_chain(len = 12);
    }
    for (y = [-22, -10, 2, 14, 22]) translate([41, y, 0]) rotate([0, 0, 90]) cw_prop_fence_chain(len = 11);
    for (y = [2, 14, 22]) translate([-41, y, 0]) rotate([0, 0, 90]) cw_prop_fence_chain(len = 11);
    translate([-41, -25, 0]) rotate([0, 0, 90]) cw_prop_fence_chain(len = 7);
    for (sx = [-1, 1], sy = [-1, 1]) translate([sx * 38, sy * 25, 0]) cw_bldg_guard_tower(seed = sx + sy * 2);
    // 大门（西边 y=-15 洞口）
    translate([-38, -20, 0]) cw_bldg_checkpoint(seed = 1);
    translate([-39, -14, 0]) rotate([0, 0, 90]) cw_prop_barrier_gate(seed = 0);
    translate([-44, -8, 0]) cw_prop_hedgehog();
    translate([-44, -22, 0]) rotate([0, 0, 30]) cw_prop_hedgehog();
    translate([-36, -11, 0]) cw_prop_wall_sandbag(len = 3);
    translate([-46, -16, 0]) cw_prop_sign_quarantine(seed = 5, msg = "RESTRICTED");
    // 指挥区
    translate([-24, 12, 0]) rotate([0, 0, 180]) nd_hq(0, L = 13, D = 8);
    translate([-24, -2, 0]) cw_prop_monument(seed = 1);
    translate([-38, 20, 0]) cw_prop_antenna(seed = 0);
    translate([-33, -26, 0]) cw_prop_searchlight(seed = 0);
    // 营房排
    translate([-8, -22, 0]) nd_barracks(1, L = 13, D = 6);
    translate([8, -22, 0]) nd_barracks(3, L = 13, D = 6);
    // 机库 / 后勤（东侧）
    translate([28, 12, 0]) rotate([0, 0, -90]) cw_bldg_hangar(seed = 0, L = 12, D = 14);
    translate([28, -6, 0]) rotate([0, 0, -90]) cw_bldg_hangar(seed = 1, L = 12, D = 14);
    translate([36, -24, 0]) nd_warehouse(2, L = 11, D = 7, p = "military");
    translate([18, -26, 0]) cw_prop_fueltank(seed = 1);
    translate([38, 24, 0]) cw_bldg_water_tower(seed = 1);
    // 停机坪 + 装备
    translate([2, 20, 0]) cw_ground_helipad(S = 12);
    translate([2, 20, 0.08]) nd_extraction(id = "military_base", name = "Military Base Helipad");
    translate([0, 2, 0]) rotate([0, 0, 15]) cw_veh_btr(seed = 0);
    translate([13, 6, 0]) rotate([0, 0, -8]) cw_veh_tank(seed = 0);
    translate([-10, 0, 0]) rotate([0, 0, 90]) cw_veh_truck_canvas(seed = 0);
    translate([-15, -10, 0]) rotate([0, 0, 96]) cw_veh_uaz_van(seed = 0);
    // 帐篷营区 + 物资（北带）
    translate([-27, 24, 0]) cw_prop_tent_military(seed = 0);
    translate([-19, 25, 0]) rotate([0, 0, -12]) cw_prop_tent_military(seed = 1);
    translate([-23, 19.5, 0]) cw_prop_campfire(seed = 1);
    translate([12, 24, 0]) cw_prop_tent_medical(seed = 4);
    translate([18, 18, 0]) nd_body_bags(seed = 61, n = 5);
    translate([-2, -12, 0]) cw_prop_sandbag_nest(seed = 6);
    translate([22, -14, 0]) cw_prop_locker(seed = 9, n = 4);
    // 手摆物资
    translate([-14, 20.5, 0]) cw_wpn_crate(seed = 0);
    translate([-11, 23, 0]) cw_prop_crate_ammo(seed = 2);
    translate([-25.5, 17, 0]) cw_item_bedroll(seed = 3);
    translate([-20.5, 17.5, 0]) cw_item_radio(seed = 0);
    translate([-12.5, 25.8, 0]) cw_item_ammobox(seed = 1);
    translate([-16, 18.6, 0]) cw_item_helmet(seed = 1);
    translate([-18, 22, 0]) cw_wpn_svd(seed = 1);
    translate([-9, 18.5, 0]) cw_item_backpack(seed = 2);
    translate([14, 26, 0]) cw_item_crate_supply(seed = 1);
    translate([-24, 12, 0]) cw_prop_crate_ammo(seed = 5);
    translate([-25.5, 11, 0]) cw_item_radio(seed = 2);
    translate([-8, -22, 0]) cw_wpn_ak(seed = 4);
    translate([8, -22, 0]) cw_item_ammobox(seed = 4);
    translate([36, -24, 0]) cw_item_crate_supply(seed = 3);
    translate([10, 21, 0]) cw_item_medical_bag(seed = 4);
    translate([24, 9, 0]) nd_loot(p = "military");
    translate([24, -9, 0]) nd_loot(p = "military");
    translate([22, -12.8, 0]) nd_loot(p = "military");
    translate([-4, -10, 0]) nd_loot(p = "military");
    // 感染者：基地驻军
    translate([0, 0, 0]) nd_zed(p = "military", group = 3);
    translate([-20, 10, 0]) nd_zed(p = "military", group = 2);
    translate([20, -15, 0]) nd_zed(p = "military", group = 2);
    translate([-30, -20, 0]) nd_zed(p = "military", group = 2);
    translate([25, 15, 0]) nd_zed(p = "military", group = 1);
    translate([0, -25, 0]) nd_zed(p = "military", group = 2);
    translate([-24, 20, 0]) nd_zed(p = "military", group = 2);
}
ter_place(TERR, 160, -100, dz = 0) cw_prop_sign_road(seed = 0);

// ================= POI 18：护林站（pad -20,95；林间土路通新德沃尔） =================
ter_place(TERR, -20, 95, dz = 0)
{
    nd_poi(name = "Ranger Station", kind = "camp");
    translate([-6, 3, 0]) nd_house_rural(51, L = 8, D = 6, p = "wilderness");
    translate([8, 6, 0]) nd_shed(13, p = "wilderness");
    translate([10, -6, 0]) cw_bldg_hunting_stand(seed = 2);
    translate([-14, -2, 0]) cw_prop_woodpile(seed = 8);
    translate([2, -7, 0]) cw_prop_well(seed = 5);
    translate([-3, -9, 0]) rotate([0, 0, 20]) cw_veh_uaz_van(seed = 5);
    translate([-11, -8, 0]) cw_prop_campfire(seed = 7);
    translate([-10, -6.5, 0]) cw_item_knife();
    translate([0, -4, 0]) nd_zed(p = "wilderness", group = 2);
    translate([12, 8, 0]) nd_zed(p = "wilderness", group = 1);
}

// ================= POI 19：猎人小屋（pad -60,170） =================
ter_place(TERR, -60, 170, dz = 0)
{
    nd_poi(name = "Hunter's Lodge", kind = "camp");
    translate([-2, 2, 0]) nd_house_rural(61, L = 6, D = 5, p = "wilderness");
    translate([6, -3, 0]) cw_bldg_hunting_stand(seed = 3);
    translate([-6, -4, 0]) cw_prop_campfire(seed = 8);
    translate([-5, -5.5, 0]) cw_item_bedroll(seed = 7);
    translate([2, -5, 0]) nd_loot(p = "wilderness");
    translate([0, -5, 0]) nd_zed(p = "wilderness", group = 1);
}

// ================= POI 20：北坡坠机点（pad -140,240） =================
ter_place(TERR, -140, 240, dz = 0)
{
    nd_poi(name = "Crash Site", kind = "crash");
    rotate([0, 0, 30]) cw_veh_heli_wreck(seed = 0);
    translate([-10, 8, 0]) cw_prop_tent_military(seed = 4);
    translate([10, 8, 0]) cw_prop_tent_medical(seed = 5);
    translate([-12, -6, 0]) nd_body_bags(seed = 71, n = 4);
    translate([12, -8, 0]) cw_prop_wall_sandbag(len = 3);
    translate([6.5, -4.5, 0]) cw_item_crate_supply(seed = 1);
    translate([5, -6, 0]) cw_item_medkit();
    translate([-6, 5.5, 0]) cw_wpn_ak(seed = 3);
    translate([7.5, 3.5, 0]) cw_item_backpack(seed = 1);
    translate([-8, 4, 0]) nd_loot(p = "military");
    translate([12, 5, 0]) nd_loot(p = "medical");
    translate([0, -6, 0]) nd_zed(p = "military", group = 2);
    translate([8, 6, 0]) nd_zed(p = "military", group = 1);
}
ter_place(TERR, -128, 228, dz = 0) cw_prop_campfire(seed = 4);
ter_place(TERR, -126, 226, dz = 0) cw_item_bedroll(seed = 2);

// ================= POI 21：湖畔营地（西南湖东岸，pad -246,-318） =================
ter_place(TERR, -246, -318, dz = 0)
{
    nd_poi(name = "Lake Camp", kind = "camp");
    translate([6, 5, 0]) nd_house_rural(71, L = 5.5, D = 4.5, p = "wilderness");
    translate([8, -6, 0]) rotate([0, 0, 90]) nd_house_rural(72, L = 5.5, D = 4.5, p = "wilderness");
    translate([-4, 6, 0]) cw_prop_woodpile(seed = 9);
    translate([0, -8, 0]) nd_zed(p = "wilderness", group = 2);
}
ter_place(TERR, -266, -318, dz = 0) rotate([0, 0, 250]) cw_prop_tent_military(seed = 3);
ter_place(TERR, -262, -326, dz = 0) cw_prop_campfire(seed = 2);
ter_place(TERR, -264, -330, dz = 0) cw_item_bedroll(seed = 4);
ter_place(TERR, -260, -322, dz = 0) cw_item_can(seed = 7);
ter_place(TERR, -268, -312, dz = 0) cw_item_lantern(seed = 1);
ter_place(TERR, -258, -334, dz = 0) cw_wpn_shotgun(seed = 2);
ter_place(TERR, -257, -328, dz = 0) cw_item_water_bottle(seed = 3);
// 湖岸芦苇圈
ter_place(TERR, -273, -330, dz = 0) cw_nature_reeds(seed = 1);
ter_place(TERR, -286, -293, dz = 0) cw_nature_reeds(seed = 2);
ter_place(TERR, -320, -274, dz = 0) cw_nature_reeds(seed = 3);
ter_place(TERR, -374, -293, dz = 0) cw_nature_reeds(seed = 4);
ter_place(TERR, -384, -350, dz = 0) cw_nature_reeds(seed = 5);
ter_place(TERR, -320, -386, dz = 0) cw_nature_reeds(seed = 6);
ter_place(TERR, -286, -367, dz = 0) cw_nature_reeds(seed = 7);

// ================= POI 22：东口军车残队 + 西口路障（主公路两端） =================
ter_place(TERR, 455, -122, dz = 0)
{
    nd_poi(name = "East Roadblock", kind = "military");
    rotate([0, 0, 10])
    {
        translate([-12, 0, 0]) rotate([0, 0, 4]) cw_veh_truck_canvas(seed = 8);
        translate([0, 1.5, 0]) rotate([0, 0, -8]) cw_veh_btr(seed = 2);
        translate([12, -1, 0]) rotate([0, 0, 15]) cw_veh_car_burnt(seed = 9);
        translate([22, 0, 0]) rotate([0, 0, 90]) cw_prop_barricade(len = 7, seed = 7);
        translate([6, -6, 0]) cw_prop_sandbag_nest(seed = 7);
        translate([-4, -6, 0]) nd_body_bags(seed = 81, n = 4);
        translate([18, 6, 0]) cw_prop_sign_quarantine(seed = 6, msg = "STOP");
        translate([-6, 5, 0]) nd_loot(p = "military");
        translate([4, -9, 0]) nd_loot(p = "military");
        translate([-14, -5, 0]) nd_loot(p = "military");
    }
    translate([0, 0, 0]) nd_zed(p = "military", group = 3);
}
ter_place(TERR, -455, -128, dz = 0)
{
    translate([0, 0, 0]) rotate([0, 0, -14]) cw_veh_bus(seed = 2);
    translate([10, 3, 0]) rotate([0, 0, 20]) cw_veh_car_burnt(seed = 10);
    translate([-10, -2, 0]) rotate([0, 0, 76]) cw_prop_barricade(len = 6, seed = 9);
    translate([4, -6, 0]) cw_prop_trash(seed = 17);
    translate([2, 6, 0]) nd_loot(p = "residential");
    translate([4, -2, 0]) nd_zed(p = "civilian", group = 2);
}

// ================= 公路道具 =================
// 电力杆沿主公路北侧
ter_along(TERR, [[-500, -140], [-390, -110], [-300, -150], [-210, -110], [-120, -150], [-30, -158]], step = 85, seed = 5, offset = 0, dz = 0)
    translate([0, 5.5, 0]) cw_prop_pole_concrete(seed = $seed);
ter_along(TERR, [[60, -150], [150, -110], [240, -140], [330, -100], [420, -130], [500, -115]], step = 85, seed = 6, offset = 0, dz = 0)
    translate([0, 5.5, 0]) cw_prop_pole_concrete(seed = $seed);
// 路面弃车与路牌
ter_place(TERR, -210, -110, dz = 0) rotate([0, 0, 204]) cw_veh_lada(seed = 5);
ter_place(TERR, -120, -150, dz = 0) rotate([0, 0, -22]) cw_veh_wreck(seed = 4);
ter_place(TERR, 330, -100, dz = 0) rotate([0, 0, 12]) cw_veh_truck_canvas(seed = 2);
ter_place(TERR, 100, -138, dz = 0) rotate([0, 0, 160]) cw_veh_uaz_van(seed = 3);
ter_place(TERR, 100, -160, dz = 0) rotate([0, 0, 180]) cw_prop_billboard(seed = 1);
ter_place(TERR, 152, -102, dz = 0) rotate([0, 0, -22]) cw_prop_sign_town(seed = 2);
ter_place(TERR, 66, -242, dz = 0) rotate([0, 0, 80]) cw_prop_sign_road(seed = 2);
ter_place(TERR, -255, -130, dz = 0) rotate([0, 0, 30]) cw_veh_car_burnt(seed = 11);
ter_place(TERR, 285, -122, dz = 0) rotate([0, 0, -8]) cw_veh_car_burnt(seed = 12);
ter_place(TERR, 200, -32, dz = 0) rotate([0, 0, 62]) cw_veh_wreck(seed = 6);
ter_place(TERR, 380, -116, dz = 0) cw_prop_trash(seed = 19);
ter_place(TERR, -168, -132, dz = 0) cw_prop_blood_stain(seed = 21, s = 1.2);
// 路边游荡的感染者（据点之间也不安全）
ter_place(TERR, -255, -135, dz = 0) nd_zed(p = "civilian", group = 1);
ter_place(TERR, 15, -170, dz = 0) nd_zed(p = "civilian", group = 1);
ter_place(TERR, 140, -120, dz = 0) nd_zed(p = "civilian", group = 2);
ter_place(TERR, 285, -128, dz = 0) nd_zed(p = "civilian", group = 1);
ter_place(TERR, 200, -40, dz = 0) nd_zed(p = "military", group = 1);
ter_place(TERR, -380, -40, dz = 0) nd_zed(p = "wilderness", group = 1);
ter_place(TERR, 60, -230, dz = 0) nd_zed(p = "industrial", group = 1);

// ================= 植被生态（ter_scatter 过滤散布；pad/road 生物群系自动避开） =================
// 针叶/白桦混交林：缓坡草地，避水
ter_scatter(TERR, 101, 920, [-490, -490, 490, 490], [0.5, 40, 26, 3, ["grass", "grass_dark"]], rot = true, dz = 0, variants = 12, scale = [1.5, 2.9])
    lay_pick(i = $variant)
    {
        cw_nature_pine(s = 1, seed = $variant_seed);
        cw_nature_pine(s = 1, seed = $variant_seed + 1);
        cw_nature_birch(s = 1, seed = $variant_seed);
        cw_nature_pine(s = 1, seed = $variant_seed + 2);
    }
// 北坡密林带
ter_scatter(TERR, 103, 704, [-490, 120, 490, 460], [2, 42, 28, 2, ["grass", "grass_dark"]], rot = true, dz = 0, variants = 2, scale = [1.6, 2.8])
    lay_pick(i = $variant)
    {
        cw_nature_pine(s = 1, seed = $variant_seed);
        cw_nature_birch(s = 1, seed = $variant_seed);
    }
// 高地岩块（随机朝向）
ter_scatter(TERR, 107, 50, [-490, -490, 490, 490], [1, 60, 60, 0, ["rock", "rock_high"]], rot = true, dz = 0, variants = 0, scale = [1, 1])
    cw_nature_rock(s = lay_randr($seed, 4, 1.4, 3.0), seed = $seed);
// 枯草带灌木与枯树
ter_scatter(TERR, 109, 70, [-490, -490, 490, 490], [1, 45, 30, 1, ["dry_grass"]], rot = true, dz = 0, variants = 0, scale = [1, 1])
    lay_pick($seed)
    {
        cw_nature_bush(s = lay_randr($seed, 3, 1.2, 2.0), seed = $seed);
        cw_nature_tree_dead(s = lay_randr($seed, 4, 1.3, 2.0), seed = $seed);
        cw_nature_grass_tuft(seed = $seed);
    }
// 河谷两岸疏林（让河道有掩体接近路线）
ter_scatter(TERR, 115, 80, [-150, -400, 120, 300], [0.5, 30, 24, 4, ["grass", "grass_dark"]], rot = true, dz = 0, variants = 0, scale = [1, 1])
    lay_pick($seed)
    {
        cw_nature_birch(s = lay_randr($seed, 5, 1.5, 2.2), seed = $seed);
        cw_nature_pine(s = lay_randr($seed, 6, 1.6, 2.4), seed = $seed);
        cw_nature_bush(s = lay_randr($seed, 7, 1.2, 1.8), seed = $seed);
    }
// 低地灌木丛
ter_scatter(TERR, 113, 100, [-490, -490, 490, 490], [0.3, 25, 22, 2, ["grass", "grass_dark"]], rot = true, dz = 0, variants = 8, scale = [1, 1])
    lay_pick($seed)
    {
        cw_nature_bush(s = lay_randr($seed, 3, 1.0, 1.8), seed = $seed);
        cw_nature_grass_tuft(seed = $seed);
        cw_nature_stump(s = lay_randr($seed, 5, 1.0, 1.5), seed = $seed);
    }
// 荒野枯树（末日的灰色剪影，沿公路与河谷稀疏分布）
ter_scatter(TERR, 117, 60, [-490, -330, 490, 60], [0.3, 30, 20, 3, ["grass", "grass_dark", "dry_grass"]], rot = true, dz = 0, variants = 4, scale = [1.3, 2.2])
    cw_nature_tree_dead(s = 1, seed = $variant_seed);
