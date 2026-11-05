// Shared Coldwar gameplay assemblies: room furniture, loot and semantic markers.
use <kit_coldwar.scad>

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
        if ($children > 0) children(); else cw_bldg_house_rural(seed, L, D);
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
        if ($children > 0) children(); else cw_bldg_house_two(seed, L, D);
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
        if ($children > 0) children(); else cw_bldg_shop_row(seed, L, D);
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
        if ($children > 0) children(); else cw_bldg_warehouse(seed, L, D);
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
