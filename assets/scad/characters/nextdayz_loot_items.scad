// nextdayz_loot_items.scad —— NextDayz 物资目录（借 ScadRig 格式打包一组静态模型）
// 每根 bone_item_<itemId> 就是一件物品的世界模型，itemId 与 ItemDefs.cpp 的 id 一一对应；
// 全部骨骼 pivot 在原点（无动画），NextDayz 在 BeforeSceneRebuild 把各部件注入场景，
// loot 刷新点 / 掉落堆按物品换模型。新增物品：在 ItemDefs.cpp 加定义，在这里加一根骨骼。
// 模型本体来自 kit_coldwar.scad 的 cw_wpn_* / cw_item_*，与地图里手摆的物资同源。
use <../lib/kit_coldwar.scad>
use <nextdayz_clothing.scad>
use <nextdayz_attachments.scad>
use <../lib/kit_nd_arsenal.scad>
use <nextdayz_clothing_heritage.scad>

$fn = 12;

module bone_item_signal_flare()
{
    color([0.93, 0.23, 0.06]) translate([0, 0, 0.07]) cube([0.26, 0.07, 0.09], center = true);
    color([0.15, 0.17, 0.18]) translate([-0.07, 0, 0.035]) rotate([0, -20, 0]) cube([0.07, 0.06, 0.13], center = true);
    color([0.38, 0.40, 0.42]) translate([0.10, 0, 0.07]) rotate([0, 90, 0]) cylinder(h = 0.045, r = 0.04, center = true);
}

// ---- 枪械 ----
module bone_item_ak() { cw_wpn_ak(1); }
module bone_item_svd() { cw_wpn_svd(1); }
module bone_item_mosin() { cw_wpn_mosin(1); }
module bone_item_shotgun() { cw_wpn_shotgun(1); }
module bone_item_pistol() { cw_wpn_pistol(1); }
// Loose accessories rest on their side, above the same ground plane as loot.
module bone_item_optic_red_dot() { gk_flatten() translate([0, 0, 0.03]) rotate([0, 90, 0]) nd_att_red_dot(); }
module bone_item_optic_holographic() { gk_flatten() translate([0, 0, 0.036]) rotate([0, 90, 0]) nd_att_holographic(); }
module bone_item_optic_4x() { gk_flatten() translate([0, 0, 0.038]) rotate([0, 90, 0]) nd_att_scope4x(); }
module bone_item_grip_vertical() { gk_flatten() translate([0, 0, 0.019]) rotate([0, 90, 0]) nd_att_grip(); }
module bone_item_mag_quick() { gk_flatten() translate([0, 0, 0.023]) rotate([0, 90, 0]) nd_att_quick_mag(); }
module bone_item_mag_extended() { gk_flatten() translate([0, 0, 0.023]) rotate([0, 90, 0]) nd_att_extended_mag(); }
module bone_item_muzzle_suppressor() { gk_flatten() translate([0, 0, 0.023]) nd_att_suppressor(); }
module bone_item_tactical_flashlight() { gk_flatten() translate([0, 0, 0.022]) nd_att_flashlight(); }
// ---- 近战 ----
module bone_item_knife() { cw_item_knife(); }
module bone_item_crowbar() { cw_item_crowbar(); }
module bone_item_axe() { cw_item_axe(); }
// ---- 弹药 ----
module bone_item_ammo_545() { cw_item_ammo_pack(0); }
module bone_item_ammo_762() { cw_item_ammo_pack(1); }
module bone_item_ammo_12g() { cw_item_ammo_pack(2); }
module bone_item_ammo_9mm() { cw_item_ammo_pack(3); }
// ---- Clothing / packs: six choices per slot ----
// Clothing drops use the same palettes and detail modules as the worn items.
module ndc_loot_head(style)
{
    base = [-0.005, 0.175, 0.172, 0.165, -0.005, 0.02][style];
    translate([0, 0, -base]) ndc_head(style);
}
module ndc_loot_coat(style)
{
    color(ndc_coat(style)) translate([0, 0, 0.04]) ndc_box([0.32, 0.29, 0.08]);
    color(ndc_coat(style) * 0.8) translate([0.10, 0, 0.075]) ndc_box([0.105, 0.27, 0.04]);
    color(ndc_zip()) translate([-0.02, 0, 0.083]) cube([0.01, 0.26, 0.008], center = true);
    if (style == 1 || style == 3)
        color(ndc_coat(style)) translate([0, 0.10, 0.10]) ndc_box([0.20, 0.12, 0.06]);
    if (style == 2)
        for (y = [-0.10, -0.04, 0.02, 0.08])
            color(ndc_coat(style) * 1.08) translate([0, y, 0.085]) ndc_box([0.30, 0.045, 0.025], 0.006);
    if (style == 4)
        for (x = [-0.10, 0.065])
            color(ndc_coat(style) * 1.2) translate([x, 0.05, 0.092]) ndc_box([0.08, 0.08, 0.02], 0.004);
    if (style == 5)
        for (y = [-0.08, 0.07])
            color(ndc_reflect()) translate([0, y, 0.098]) cube([0.30, 0.025, 0.008], center = true);
}
module ndc_loot_vest(style)
{
    // Back-mounted badge/reflective panels determine the resting face.
    base = style == 1 ? 0.163 : style == 5 ? 0.16525 : 0.151;
    translate([0, -0.23, base]) rotate([-90, 0, 0]) scale([0.75, 0.75, 0.75]) ndc_vest(style);
}
module ndc_loot_pants(style)
{
    for (s = [-1, 1])
        color(ndc_pants(style)) translate([s * 0.07, 0, 0.04]) ndc_box([0.135, 0.32, 0.08]);
    color(ndc_web()) translate([0, 0.12, 0.085]) ndc_box([0.28, 0.04, 0.025], 0.004);
    if (style == 1 || style == 3)
        for (s = [-1, 1])
            color(ndc_pants(style) * 0.8) translate([s * 0.10, 0, 0.09]) ndc_box([0.065, 0.12, 0.025], 0.004);
    if (style == 4 || style == 5)
        for (s = [-1, 1])
            color(style == 4 ? ndc_reflect() : ndc_web()) translate([s * 0.07, -0.10, 0.085])
                cube([0.12, 0.025, 0.01], center = true);
}
module ndc_loot_pack(style)
{
    h = [0.34, 0.43, 0.49, 0.29, 0.40, 0.46][style];
    d = [0.15, 0.20, 0.24, 0.13, 0.18, 0.18][style];
    bottom = 0.29 - h / 2 - (style == 2 ? 0.095 : 0);
    translate([0, -0.21 - d / 2, -bottom]) ndc_pack(style);
}
module bone_item_helmet() { ndc_loot_head(0); }
module bone_item_hat_beanie() { ndc_loot_head(1); }
module bone_item_cap_baseball() { ndc_loot_head(2); }
module bone_item_hat_boonie() { ndc_loot_head(3); }
module bone_item_helmet_tactical() { ndc_loot_head(4); }
module bone_item_hat_ushanka() { ndc_loot_head(5); }
module bone_item_jacket() { ndc_loot_coat(0); }
module bone_item_hoodie() { ndc_loot_coat(1); }
module bone_item_jacket_puffer() { ndc_loot_coat(2); }
module bone_item_jacket_rain() { ndc_loot_coat(3); }
module bone_item_jacket_denim() { ndc_loot_coat(4); }
module bone_item_jacket_rescue() { ndc_loot_coat(5); }
module bone_item_vest_military() { ndc_loot_vest(0); }
module bone_item_vest_police() { ndc_loot_vest(1); }
module bone_item_vest_chest_rig() { ndc_loot_vest(2); }
module bone_item_vest_hunting() { ndc_loot_vest(3); }
module bone_item_vest_utility() { ndc_loot_vest(4); }
module bone_item_vest_rescue() { ndc_loot_vest(5); }
module bone_item_work_pants() { ndc_loot_pants(0); }
module bone_item_cargo_pants() { ndc_loot_pants(1); }
module bone_item_jeans() { ndc_loot_pants(2); }
module bone_item_pants_tactical() { ndc_loot_pants(3); }
module bone_item_pants_rain() { ndc_loot_pants(4); }
module bone_item_pants_snow() { ndc_loot_pants(5); }
module bone_item_backpack_small() { ndc_loot_pack(0); }
module bone_item_backpack() { ndc_loot_pack(1); }
module bone_item_backpack_military() { ndc_loot_pack(2); }
module bone_item_backpack_messenger() { ndc_loot_pack(3); }
module bone_item_backpack_medical() { ndc_loot_pack(4); }
module bone_item_backpack_rolltop() { ndc_loot_pack(5); }
// ---- 有史可考的一组（innawoods.net 图鉴挑选）：武器在 kit_nd_arsenal.scad，服装在 nextdayz_clothing_heritage.scad ----
module bone_item_aks74u() { nd_wpn_aks74u(); }
module bone_item_bizon() { nd_wpn_bizon(); }
module bone_item_aps() { nd_wpn_aps(); }
module bone_item_svt40() { nd_wpn_svt40(); }
module bone_item_toz34() { nd_wpn_toz34(); }
// 帽盔立放：最低点（帽檐 / 盔沿）贴地，下颚带收起
module bone_item_hat_panama() { translate([0, 0, -0.166]) ndh_panama(); }
module bone_item_helmet_ssh68() { translate([0, 0, -0.155]) ndh_ssh68(strap = false); }
module bone_item_helmet_m56() { translate([0, 0, -0.122]) ndh_m56(strap = false); }
module bone_item_shirt_telnyashka() { ndh_loot_coat("telnyashka"); }
module bone_item_jacket_afganka() { ndh_loot_coat("afganka"); }
module bone_item_jacket_gorka() { ndh_loot_coat("gorka"); }
module bone_item_jacket_strichtarn() { ndh_loot_coat("strichtarn"); }
// 背心正面朝上压扁平放（前后厚度 × 0.35）：背面最外层（56 式过肩带 .18 / 6B3 钛板鼓包 .208）× 0.35 贴地
module bone_item_vest_type56() { translate([0, -0.23, 0.065]) rotate([-90, 0, 0]) scale([0.75, 0.35, 0.75]) ndh_type56(); }
module bone_item_vest_6b3() { translate([0, -0.23, 0.075]) rotate([-90, 0, 0]) scale([0.75, 0.35, 0.75]) ndh_6b3(); }
module bone_item_pants_afganka() { ndh_loot_pants("afganka"); }
module bone_item_pants_gorka() { ndh_loot_pants("gorka"); }
module bone_item_pants_strichtarn() { ndh_loot_pants("strichtarn"); }
// 背包立放：包底贴地（RD-54 底 z = .10，阿巴拉科夫底 z = .05）
module bone_item_backpack_rd54() { translate([0, -0.285, -0.099]) ndh_rd54(); }
module bone_item_backpack_abalakov() { translate([0, -0.31, -0.049]) ndh_abalakov(); }
// ---- 食物 / 饮水 ----
module bone_item_food_can() { cw_item_can(2); }
module bone_item_food_tuna() { cw_item_tuna(1); }
module bone_item_food_crackers() { cw_item_crackers(); }
module bone_item_food_apple() { cw_item_apple(1); }
module bone_item_food_chocolate() { cw_item_chocolate(1); }
module bone_item_water_bottle() { cw_item_water_bottle(); }
module bone_item_water_bottle_empty() { cw_item_water_bottle(full = false); }
module bone_item_drink_soda() { cw_item_soda(2); }
// ---- 医疗 ----
module bone_item_bandage() { cw_item_bandage(); }
module bone_item_rag() { cw_item_rags(1); }
module bone_item_medkit() { cw_item_medkit(); }
module bone_item_painkillers() { cw_item_painkillers(); }
// ---- 工具 ----
module bone_item_can_opener() { cw_item_can_opener(); }
module bone_item_compass() { cw_item_compass(); }
module bone_item_map() { cw_item_map(); }
module bone_item_radio() { cw_item_radio(1); }
module bone_item_battery() { cw_item_battery(); }
module bone_item_lantern() { cw_item_lantern(1); }
module bone_item_fuel() { cw_item_jerrycan(1); }
module bone_item_bedroll() { cw_item_bedroll(1); }
module bone_item_firecrackers() { cw_item_firecrackers(); }
// 未登记物品的兜底外观：一个旧纸箱
module bone_item_unknown()
{
    color([0.52, 0.42, 0.28]) translate([0, 0, 0.11]) cube([0.32, 0.24, 0.22], center = true);
    color([0.42, 0.34, 0.22]) translate([0, 0, 0.222]) cube([0.33, 0.04, 0.005], center = true);
}

module bone_root()
{
    bone_item_ak(); bone_item_svd(); bone_item_mosin(); bone_item_shotgun(); bone_item_pistol();
    bone_item_knife(); bone_item_crowbar(); bone_item_axe();
    bone_item_optic_red_dot(); bone_item_optic_holographic(); bone_item_optic_4x();
    bone_item_grip_vertical(); bone_item_mag_quick(); bone_item_mag_extended();
    bone_item_muzzle_suppressor(); bone_item_tactical_flashlight();
    bone_item_ammo_545(); bone_item_ammo_762(); bone_item_ammo_12g(); bone_item_ammo_9mm();
    bone_item_helmet();
    bone_item_hat_beanie();
    bone_item_cap_baseball();
    bone_item_hat_boonie();
    bone_item_helmet_tactical();
    bone_item_hat_ushanka();
    bone_item_jacket();
    bone_item_hoodie();
    bone_item_jacket_puffer();
    bone_item_jacket_rain();
    bone_item_jacket_denim();
    bone_item_jacket_rescue();
    bone_item_vest_military();
    bone_item_vest_police();
    bone_item_vest_chest_rig();
    bone_item_vest_hunting();
    bone_item_vest_utility();
    bone_item_vest_rescue();
    bone_item_work_pants();
    bone_item_cargo_pants();
    bone_item_jeans();
    bone_item_pants_tactical();
    bone_item_pants_rain();
    bone_item_pants_snow();
    bone_item_backpack_small();
    bone_item_backpack();
    bone_item_backpack_military();
    bone_item_backpack_messenger();
    bone_item_backpack_medical();
    bone_item_backpack_rolltop();
    bone_item_food_can(); bone_item_food_tuna(); bone_item_food_crackers(); bone_item_food_apple();
    bone_item_food_chocolate(); bone_item_water_bottle(); bone_item_water_bottle_empty(); bone_item_drink_soda();
    bone_item_bandage(); bone_item_rag(); bone_item_medkit(); bone_item_painkillers();
    bone_item_can_opener(); bone_item_compass(); bone_item_map(); bone_item_radio(); bone_item_battery();
    bone_item_lantern(); bone_item_fuel(); bone_item_bedroll(); bone_item_firecrackers();
    bone_item_aks74u(); bone_item_bizon(); bone_item_aps(); bone_item_svt40(); bone_item_toz34();
    bone_item_hat_panama(); bone_item_helmet_ssh68(); bone_item_helmet_m56();
    bone_item_shirt_telnyashka(); bone_item_jacket_afganka(); bone_item_jacket_gorka(); bone_item_jacket_strichtarn();
    bone_item_vest_type56(); bone_item_vest_6b3();
    bone_item_pants_afganka(); bone_item_pants_gorka(); bone_item_pants_strichtarn();
    bone_item_backpack_rd54(); bone_item_backpack_abalakov();
    bone_item_signal_flare();
    bone_item_unknown();
}

bone_root();
