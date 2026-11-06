// nextdayz_weapons.scad —— NextDayz 手持武器目录（借 ScadRig 格式打包一组静态模型）
// 每根 bone_wpn_<weaponId> 是一把武器的手持模型，weaponId 与 WeaponDefs.hpp 的 kWeapons 一一对应。
// 模型就是 kit_coldwar.scad 的 cw_wpn_<id>_held：原点 = 右手握把中心，枪口 -Y、顶面 +Z，
// 与 nextdayz_survivor.scad 的 weapon frame / bone_weapon_socket 一致，NextDayz 原样挂到
// 第三人称的手上、也用作第一人称视模；材质（gk_material 粗糙度 / 金属度）随部件分段带入。
// 枪口 / 枪管轴 / 瞄准线高度写在 WeaponDefs.hpp 的 WeaponModelPoints，改模型时同步。
use <../lib/kit_coldwar.scad>

module bone_wpn_ak() { cw_wpn_ak_held(); }
module bone_wpn_svd() { cw_wpn_svd_held(scope = false); }
module bone_wpn_mosin() { cw_wpn_mosin_held(); }
module bone_wpn_shotgun() { cw_wpn_shotgun_held(); }
module bone_wpn_pistol() { cw_wpn_pistol_held(); }
module bone_wpn_knife() { cw_wpn_knife_held(); }
module bone_wpn_crowbar() { cw_wpn_crowbar_held(); }
module bone_wpn_axe() { cw_wpn_axe_held(); }

module bone_root()
{
    bone_wpn_ak(); bone_wpn_svd(); bone_wpn_mosin(); bone_wpn_shotgun(); bone_wpn_pistol();
    bone_wpn_knife(); bone_wpn_crowbar(); bone_wpn_axe();
}

bone_root();
