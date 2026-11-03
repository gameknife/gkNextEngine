// nextdayz_loot_items.scad —— NextDayz 物资目录（借 ScadRig 格式打包一组静态模型）
// 每根 bone_item_<itemId> 就是一件物品的世界模型，itemId 与 ItemDefs.cpp 的 id 一一对应；
// 全部骨骼 pivot 在原点（无动画），NextDayz 在 BeforeSceneRebuild 把各部件注入场景，
// loot 刷新点 / 掉落堆按物品换模型。新增物品：在 ItemDefs.cpp 加定义，在这里加一根骨骼。
// 模型本体来自 kit_coldwar.scad 的 cw_wpn_* / cw_item_*，与地图里手摆的物资同源。
use <../lib/kit_coldwar.scad>

$fn = 12;

// ---- 枪械 ----
module bone_item_ak() { cw_wpn_ak(1); }
module bone_item_svd() { cw_wpn_svd(1); }
module bone_item_mosin() { cw_wpn_mosin(1); }
module bone_item_shotgun() { cw_wpn_shotgun(1); }
module bone_item_pistol() { cw_wpn_pistol(1); }
// ---- 近战 ----
module bone_item_knife() { cw_item_knife(); }
module bone_item_crowbar() { cw_item_crowbar(); }
module bone_item_axe() { cw_item_axe(); }
// ---- 弹药 ----
module bone_item_ammo_545() { cw_item_ammo_pack(0); }
module bone_item_ammo_762() { cw_item_ammo_pack(1); }
module bone_item_ammo_12g() { cw_item_ammo_pack(2); }
module bone_item_ammo_9mm() { cw_item_ammo_pack(3); }
// ---- 衣物 / 背包 ----
module bone_item_helmet() { cw_item_helmet(1); }
module bone_item_jacket() { cw_item_jacket(1); }
module bone_item_hoodie() { cw_item_jacket(4, hood = true); }
module bone_item_vest_military() { cw_item_vest(); }
module bone_item_work_pants() { cw_item_pants(1); }
module bone_item_cargo_pants() { cw_item_pants(0, cargo = true); }
module bone_item_backpack_small() { cw_item_backpack_small(1); }
module bone_item_backpack() { cw_item_backpack(1); }
module bone_item_backpack_military() { cw_item_backpack_military(); }
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
    bone_item_ammo_545(); bone_item_ammo_762(); bone_item_ammo_12g(); bone_item_ammo_9mm();
    bone_item_helmet(); bone_item_jacket(); bone_item_hoodie(); bone_item_vest_military();
    bone_item_work_pants(); bone_item_cargo_pants();
    bone_item_backpack_small(); bone_item_backpack(); bone_item_backpack_military();
    bone_item_food_can(); bone_item_food_tuna(); bone_item_food_crackers(); bone_item_food_apple();
    bone_item_food_chocolate(); bone_item_water_bottle(); bone_item_water_bottle_empty(); bone_item_drink_soda();
    bone_item_bandage(); bone_item_rag(); bone_item_medkit(); bone_item_painkillers();
    bone_item_can_opener(); bone_item_compass(); bone_item_map(); bone_item_radio(); bone_item_battery();
    bone_item_lantern(); bone_item_fuel(); bone_item_bedroll(); bone_item_firecrackers();
    bone_item_unknown();
}

bone_root();
