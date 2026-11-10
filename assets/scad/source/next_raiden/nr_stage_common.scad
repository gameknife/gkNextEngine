// ============================================================================
// nr_stage_common.scad - NextRaiden 关卡组合子。纯 module 库，无顶层几何。
//
// 契约（docs/projects/nextraiden/nextraiden-mvp-design.md §5、§6）：
//   * 关卡沿 SCAD +Y 生长（引擎 -Z），屏幕上方 = 前方，SCAD +X = 屏幕右方。
//   * kit 的飞机/炮/船默认朝 SCAD -Y = 朝向玩家，所以敌方一律不旋转。
//   * 一行景观 = 一次 nr_row_* 调用 = 一个 Node（内部 gk_flatten 折叠）。
//     行宽 5 个地块 = 80 m，行距 16 m，地块顶面 z=0。
//   * nr_pf_* 是"预制件"：料架里每种只放一份，运行时由 C++ 克隆成对象池。
//     名字必须唯一——池按模块名索引，重名会让两种预制件互相覆盖。
//   * 需要分级损毁的预制件（nr_pf_bomber）**不 flatten**，保留 chunk 子节点；
//     其余一律 flatten 成单 Node/Model。
// ============================================================================

use <../../lib/kit_raiden.scad>
use <../../lib/raiden_parts.scad>

// 料架停放位置：玩家出生点后方 240 m，不进画面，也不撑大场景在 Y 轴的包围盒。
function nr_rack_y() = -240;

// ================= 景观行（一行 = 一个 Node）=================
// Keep the 80m coverage / 16m row pitch, but do not repeat lane markings across X.
// The 24m runway and two 12m service aprons share exact, non-overlapping edges.
module nr_row_runway(y = 0, seed = 0, marking = 0)
{
    gk_flatten()
    {
        translate([0, y, 0]) nr_ground_runway(seed = seed, width = 24, marking = marking);
        for (s = [-1, 1]) translate([s * 18, y, 0]) rotate([0,0,s<0?180:0])
            nr_ground_apron(seed = seed + s * 23, width = 12, variant = nr_rnd(seed+s*7,3));
        for (s = [-1, 1]) translate([s * 32, y, 0]) nr_ground_grass(seed = seed + s * 29);
    }
}

module nr_row_grass(y = 0, seed = 0, props = 0)
{
    gk_flatten()
    {
        for (i = [-2 : 2]) translate([i * 16, y, 0]) nr_ground_grass(seed = seed + i * 17);
        if (props > 0)
            for (s = [-1, 1])
                translate([s * (21 + nr_rnd(seed + s * 5, 9)), y - 5 + nr_rnd(seed + s * 3, 10), 0.02])
                    nr_nature_tree(seed = seed + s);
        if (props > 0 && nr_rnd(seed+71,3) == 0)
            translate([nr_rnd(seed+5,2)==0?-25:25,y+2,0.02])
                nr_nature_grove(seed = seed, variant = nr_rnd(seed+17,2));
    }
}

module nr_row_road(y = 0, seed = 0, parcel = 0)
{
    gk_flatten()
    {
        translate([0, y, 0]) nr_ground_road(seed = seed);
        for (s = [-1, 1])
        {
            translate([s * 16, y, 0]) nr_ground_farmland(seed = seed+s*17,
                variant = nr_rnd(parcel+s*31,4), direction = nr_rnd(parcel+s*13,2));
            translate([s * 32, y, 0]) nr_ground_grass(seed = seed + s * 23);
        }
    }
}

module nr_row_farm(y = 0, seed = 0, parcel = 0, row = 0)
{
    gk_flatten()
    {
        for (i = [-1 : 1])
            let (field = parcel + i*29 + floor((row+i+3)/3)*71)
                translate([i * 16, y, 0]) nr_ground_farmland(seed = seed+i*19,
                    variant = nr_rnd(field,4), direction = nr_rnd(field+43,2));
        for (s = [-1, 1]) translate([s * 32, y, 0]) nr_ground_grass(seed = seed + s * 31);
    }
}

module nr_row_water(y = 0, seed = 0)
{
    gk_flatten()
    {
        for (i = [-2 : 2]) translate([i * 16, y, 0]) nr_ground_water(seed = seed+i*37);
        if (nr_rnd(seed+11,5) == 0) translate([nr_rnd(seed,2)==0?-22:22,y,0.02])
            nr_prop_buoy(variant = nr_rnd(seed+3,2));
    }
}

// 码头：一侧是岸，另一侧是水。side = -1 岸在左（屏幕左），+1 在右。
// 岸的内缘落在 |x| = 8，所以 42 m 的播放列里有 13 m 是码头、其余是水——地面目标摆在
// 岸这一侧 |x| = 10~18，船摆在水这一侧。左右交替铺就是逼玩家换边。
module nr_row_quay(y = 0, seed = 0, side = -1)
{
    gk_flatten()
    {
        for (i = [1 : 2]) translate([i * side * 16, y, 0])
            nr_ground_quay(seed = seed+i*23, edge = i==1?-side:0);
        for (i = [0 : 2]) translate([-i * side * 16, y, 0]) nr_ground_water(seed = seed+i*37);
        if (nr_rnd(seed+7,3) == 0) translate([side*8.7,y-3,0.02]) nr_prop_dockfender();
    }
}

// 水道：两岸都在播放列外（|x| >= 24），中间三列全是水。
// 用来做"开阔港区"：只放船，不放岸上目标——岸上那两列有一大半被 HUD 立柱盖着，
// 摆在那儿的东西玩家看不见也打不着。
module nr_row_channel(y = 0, seed = 0)
{
    gk_flatten()
    {
        for (s = [-1, 1]) translate([s * 32, y, 0]) nr_ground_quay(seed = seed+s*23, edge = -s);
        for (i = [-1 : 1]) translate([i * 16, y, 0]) nr_ground_water(seed = seed+i*37);
    }
}

// ================= 景观带（连续 rows 行）=================

module nr_band_runway(y0 = 0, rows = 4, seed = 0)
{
    for (i = [0 : rows - 1]) nr_row_runway(y = y0 + i * 16, seed = seed + i * 31,
        marking = i==0 || i==rows-1 ? 1 : i==3 ? 2 : 0);
}
module nr_band_grass(y0 = 0, rows = 4, seed = 0, props = 0)
{
    for (i = [0 : rows - 1]) nr_row_grass(y = y0 + i * 16, seed = seed + i * 37, props = props);
}
module nr_band_road(y0 = 0, rows = 4, seed = 0)
{
    for (i = [0 : rows - 1]) nr_row_road(y = y0 + i * 16, seed = seed + i * 41,
        parcel = seed + floor(i/4)*67);
}
module nr_band_farm(y0 = 0, rows = 4, seed = 0)
{
    for (i = [0 : rows - 1]) nr_row_farm(y = y0 + i * 16, seed = seed + i * 43,
        parcel = seed, row = i);
}
module nr_band_water(y0 = 0, rows = 4, seed = 0)
{
    for (i = [0 : rows - 1]) nr_row_water(y = y0 + i * 16, seed = seed + i * 47);
}
module nr_band_quay(y0 = 0, rows = 4, seed = 0, side = -1)
{
    for (i = [0 : rows - 1]) nr_row_quay(y = y0 + i * 16, seed = seed + i * 53, side = side);
}
module nr_band_channel(y0 = 0, rows = 4, seed = 0)
{
    for (i = [0 : rows - 1]) nr_row_channel(y = y0 + i * 16, seed = seed + i * 59);
}

// ================= 预制件（nr_pf_*，运行时克隆成对象池）=================

module nr_pf_player() { gk_flatten() nr_veh_player(); }
module nr_pf_wingman() { gk_flatten() nr_veh_wingman(); }
module nr_pf_scout() { gk_flatten() nr_veh_scout(); }
module nr_pf_interceptor() { gk_flatten() nr_veh_interceptor(); }
// 保留 chunk：被击坠时要把真部件摘下来交给物理碎片。
// paint=1（红）覆盖 kit 默认的青：俯视下青和玩家的蓝是同一族冷色，敌我一眼分不开。
module nr_pf_bomber() { nr_veh_bomber(paint = 1); }

module nr_pf_bolt() { gk_flatten() nr_wpn_bolt(); }
module nr_pf_bolt_hostile() { gk_flatten() nr_wpn_bolt(hostile = true); }
module nr_pf_orb() { gk_flatten() nr_wpn_orb(); }
module nr_pf_needle() { gk_flatten() nr_wpn_needle(); }
module nr_pf_missile() { gk_flatten() nr_wpn_missile(); }
module nr_pf_bomb() { gk_flatten() nr_wpn_bomb(); }
module nr_pf_laser() { gk_flatten() nr_wpn_laser(length = 4); }

module nr_pf_item_power() { gk_flatten() nr_item_power(); }
module nr_pf_item_medal() { gk_flatten() nr_item_medal(); }
module nr_pf_item_repair() { gk_flatten() nr_item_repair(); }
module nr_pf_item_bomb() { gk_flatten() nr_item_bomb(); }
module nr_pf_item_option() { gk_flatten() nr_item_option(); }

module nr_pf_fx_blast() { gk_flatten() nr_fx_blast(); }
module nr_pf_fx_smoke() { gk_flatten() nr_fx_smoke(); }
module nr_pf_fx_flash() { gk_flatten() nr_fx_flash(); }
module nr_pf_fx_crater() { gk_flatten() nr_fx_crater(); }

module nr_pf_part_armour() { gk_flatten() nr_part_armour(); }
module nr_pf_part_wing() { gk_flatten() nr_part_wing(); }
module nr_pf_part_engine() { gk_flatten() nr_part_engine(); }
module nr_pf_part_canopy() { gk_flatten() nr_part_canopy(); }
module nr_pf_part_fin() { gk_flatten() nr_part_fin(); }
module nr_pf_part_pod() { gk_flatten() nr_part_pod(); }
module nr_pf_part_gun() { gk_flatten() nr_part_gun(); }

// ================= 料架 =================
// 每种预制件一份，摆在出生点后方。运行时 C++ 在 BeforeSceneRebuild 里按名字找到它们、
// 深拷贝成池，然后把原件和拷贝一起隐藏。摆放间距只为避免互相穿插，没有玩法含义。

module nr_rack(y = -240)
{
    translate([0, y, 0])
    {
        translate([-36, 0, 6]) nr_pf_player();
        translate([-28, 0, 6]) nr_pf_wingman();
        translate([-20, 0, 6]) nr_pf_scout();
        translate([-10, 0, 6]) nr_pf_interceptor();
        translate([4, 0, 6]) nr_pf_bomber();

        translate([-36, -10, 0]) nr_pf_bolt();
        translate([-33, -10, 0]) nr_pf_bolt_hostile();
        translate([-30, -10, 0]) nr_pf_orb();
        translate([-27, -10, 0]) nr_pf_needle();
        translate([-24, -10, 0]) nr_pf_missile();
        translate([-21, -10, 0]) nr_pf_bomb();
        translate([-17, -10, 0]) nr_pf_laser();

        translate([-12, -10, 0]) nr_pf_item_power();
        translate([-9, -10, 0]) nr_pf_item_medal();
        translate([-6, -10, 0]) nr_pf_item_repair();
        translate([-3, -10, 0]) nr_pf_item_bomb();
        translate([0, -10, 0]) nr_pf_item_option();

        translate([4, -10, 0]) nr_pf_fx_blast();
        translate([8, -10, 0]) nr_pf_fx_smoke();
        translate([12, -10, 0]) nr_pf_fx_flash();
        translate([16, -10, 0]) nr_pf_fx_crater();

        translate([-36, -18, 0]) nr_pf_part_armour();
        translate([-32, -18, 0]) nr_pf_part_wing();
        translate([-28, -18, 0]) nr_pf_part_engine();
        translate([-24, -18, 0]) nr_pf_part_canopy();
        translate([-20, -18, 0]) nr_pf_part_fin();
        translate([-16, -18, 0]) nr_pf_part_pod();
        translate([-12, -18, 0]) nr_pf_part_gun();
    }
}
