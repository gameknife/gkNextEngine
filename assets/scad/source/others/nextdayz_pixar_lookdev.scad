// nextdayz_pixar_lookdev.scad —— NextDayz 视觉升级 lookdev 样板间（V0-1，Before 基准）
// 设计：docs/projects/nextdayz/nextdayz-visual-upgrade-design.md
// 40 m × 40 m 场地同框放齐视觉升级要覆盖的全部要素：穿外套/背心/背包、双手持枪的幸存者展示架，
// 一只伸手扑来的感染者，带室内家具的波希米亚农舍，椴树 + 云杉，碎石土路，弃车，木箱物资与武器陈列台。
// 只引用现有库：后续各阶段改库后，同机位重新截图即得 After。
//
// 机位（gnb shot 截第一个 gk_camera；改 FOCUS 切换默认机位）：
//   "diorama"  45° 微缩全景          "portrait" 幸存者半身肖像
//   "street"   街景（农舍 + 树 + 路） "interior" 农舍室内朝门窗
//   "face"     幸存者面部特写（五官、眼神光、皮肤）
// gnb shot --scene assets/scad/source/others/nextdayz_pixar_lookdev.scad --frames 60
use <../../lib/gk_camera.scad>
use <../../lib/rig_humanoid.scad>
use <../../lib/kit_bohemia.scad>
use <../../lib/kit_coldwar.scad>
use <../../lib/kit_nd_arsenal.scad>
use <../../characters/nextdayz_body.scad>
use <../../characters/nextdayz_clothing.scad>
use <../../characters/nextdayz_infected.scad>

FOCUS = "diorama";

$fn = 12;

// ================= 地面：与 Loket 同一 continental 调色板 + 细节印花/道路带 =================
// 路沿 -Y 侧东西向；农舍、展示区各压一块平台。

ROAD = [[-26, -2.8], [-12, -3.4], [0, -2.6], [12, -3.0], [26, -2.4]];
HOUSE = [4, 8];           // 农舍中心，正门朝 -Y（路）
YARD = [-3, -10];         // 展示区中心

TERR = ["gkterr1", [48, 48], [48, 48], 7, [0, 0.6, 0.5, 4], undef, "continental",
    [
        ["road", ROAD, 4.2],
        ["pad", HOUSE, [15, 11], 0, 2],
        ["pad", YARD, [16, 8], 0, 2, "road"]
    ]];

gk_terrain(TERR, detail = true);
function lk_z(x, y) = gk_terrain_height(TERR, x, y);
module lk_at(p, a = 0) translate([p[0], p[1], lk_z(p[0], p[1])]) rotate([0, 0, a]) children();

// ================= 幸存者展示架：rig 的绑定骨架 + 持枪 ready 姿态 =================
// 骨架、握点、持枪架与 nextdayz_survivor.scad 一致；双臂用同一个 rh_upper_pose 解 IK 到枪上，
// 所以展示的就是游戏里的 weapon_ready 上半身。服装直接调用衣柜的 bone_<item>__<bone> 部件；
// 和游戏一样，被衣物盖住的身体部件不画（只留头和手）。

SV_SKEL = rh_skeleton([
    ["pelvis_z", 0.94], ["hip", [0.105, 0, -0.08]], ["thigh", 0.44], ["calf", 0.42],
    ["heel", 0.085], ["ball", 0.21], ["torso_z", 0.02], ["shoulder", [0.235, 0, 0.48]],
    ["upperarm", 0.31], ["forearm", 0.29]]);
SV_POCKET = [-0.14, -0.12, 0.42];
SV_STOCK = 0.34;
SV_GRIP_R = [[0, 0.0837, 0.0547], [-20, 0, 0]];
SV_GRIP_L = [[0.045, -0.20, -0.075], [-160, -30, 0]];
function sv_shoulder_hold(blade, pitch, inward = 5) =
    let(e = [pitch, 0, blade + inward]) [SV_POCKET - rh_mv(rh_m(e), [0, SV_STOCK, 0]), e];
SV_BLADE = 20;
SV_HOLD = sv_shoulder_hold(SV_BLADE, 14);
SV_RIG = [["weapon_bind", SV_HOLD], ["grip_r", SV_GRIP_R], ["grip_l", SV_GRIP_L],
          ["pole_r", [-0.50, 0.08, 0.12]], ["pole_l", [0.45, -0.15, 0.08]]];
// [torso, head, wpos, wrot, hlpos, hlrot, upperarm_l, forearm_l, hand_l, upperarm_r, forearm_r, hand_r]
SV_POSE = rh_upper_pose(SV_SKEL, SV_RIG,
    [["weapon", SV_HOLD], ["torso", [0, 0, -SV_BLADE]], ["head", [0, 0, SV_BLADE * 0.9]]]);

module lk_sv_arm(side)
{
    l = side > 0;
    translate([0.235 * side, 0, 0.48]) rotate(SV_POSE[l ? 6 : 9])
    {
        if (l) bone_jacket__upperarm_l(); else bone_jacket__upperarm_r();
        translate([0, 0, -0.31]) rotate(SV_POSE[l ? 7 : 10])
        {
            if (l) bone_jacket__forearm_l(); else bone_jacket__forearm_r();
            translate([0, 0, -0.29]) rotate(SV_POSE[l ? 8 : 11])
                if (l) ndb_part_hand(); else mirror([1, 0, 0]) ndb_part_hand();
        }
    }
}

module lk_sv_leg(side)
{
    l = side > 0;
    translate([0.105 * side, 0, -0.08])
    {
        if (l) bone_cargo_pants__thigh_l(); else bone_cargo_pants__thigh_r();
        translate([0, 0, -0.44])
        {
            if (l) bone_cargo_pants__calf_l(); else bone_cargo_pants__calf_r();
            translate([0, 0, -0.42])
                if (l) bone_cargo_pants__foot_l(); else bone_cargo_pants__foot_r();
        }
    }
}

module lk_survivor()
{
    $fn = 8;
    gk_flatten() translate([0, 0, 0.94])
    {
        bone_cargo_pants__pelvis();
        lk_sv_leg(1);
        lk_sv_leg(-1);
        translate([0, 0, 0.02]) rotate(SV_POSE[0])
        {
            bone_jacket__torso();
            bone_vest_military__torso();
            bone_backpack_military__torso();
            bone_backpack_military__harness();
            translate([0, 0, 0.58]) rotate(SV_POSE[1]) { ndb_part_head(); bone_hat_beanie__head(); }
            lk_sv_arm(1);
            lk_sv_arm(-1);
            // 枪挂在 weapon frame（= bone_weapon_socket），手持坐标系枪口 -Y。
            translate(SV_HOLD[0]) rotate(SV_HOLD[1]) nd_wpn_aks74u_held();
        }
    }
}

// ================= 感染者：rig 绑定骨架 + reach 扑抓姿态 =================
// 骨长与 nextdayz_infected.scad 一致；双臂按 reach 循环第 0 帧的腕点解 IK，躯干前倾。
// 部件读的是调用链上的全局色（被 use 的文件的顶层变量对这里不可见），所以照抄它的调色板；
// 衬衫是 ROLECOLOR（运行时逐个染色），这里给一件褪色卡其衬衫的底色。

ROLECOLOR = [0.36, 0.30, 0.22];
SKIN   = [0.47, 0.52, 0.42];
SKIN_D = [0.35, 0.39, 0.32];
WOUND  = [0.34, 0.05, 0.04];
GORE   = [0.19, 0.03, 0.03];
BONE   = [0.74, 0.70, 0.58];
PANTS  = [0.19, 0.21, 0.26];
BELT   = [0.12, 0.10, 0.08];
SHOE   = [0.09, 0.08, 0.07];
SOLE   = [0.30, 0.28, 0.25];
HAIR   = [0.09, 0.07, 0.05];
SOCKET = [0.05, 0.03, 0.03];
IF_SKEL = rh_skeleton([
    ["pelvis_z", 0.93], ["hip", [0.10, 0, -0.08]], ["thigh", 0.43], ["calf", 0.42],
    ["heel", 0.08], ["ball", 0.19], ["torso_z", 0.02], ["shoulder", [0.22, 0, 0.46]],
    ["upperarm", 0.32], ["forearm", 0.30]]);
IF_ARM_L = rh_arm_ik(IF_SKEL, [0.22, 0, 0.46], [[0.21, -0.52, 0.44], [-95, 0, 12]], [0.50, -0.05, 0.10]);
IF_ARM_R = rh_arm_ik(IF_SKEL, [-0.22, 0, 0.46], [[-0.19, -0.54, 0.43], [-95, 0, -12]], [-0.50, -0.05, 0.10]);

module lk_infected()
{
    $fn = 8;
    gk_flatten() translate([0, 0, 0.93])
    {
        part_pelvis();
        translate([0.10, 0, -0.08]) { part_thigh(); translate([0, 0, -0.43]) { part_calf_l(); translate([0, 0, -0.42]) part_shoe(); } }
        translate([-0.10, 0, -0.08]) { part_thigh_r(); translate([0, 0, -0.43]) { part_calf_r(); translate([0, 0, -0.42]) mirror([1, 0, 0]) part_bare_foot(); } }
        translate([0, 0, 0.02]) rotate([14, 4, 6])
        {
            part_torso();
            translate([0, 0, 0.56]) rotate([-10, -8, -4]) { part_head(); translate([0, 0.03, 0.06]) rotate([18, 0, 0]) part_jaw(); }
            translate([0.22, 0, 0.46]) rotate(IF_ARM_L[0])
            {
                part_upperarm_l();
                translate([0, 0, -0.32]) rotate(IF_ARM_L[1]) { part_forearm_l(); translate([0, 0, -0.30]) rotate(IF_ARM_L[2]) part_hand(); }
            }
            translate([-0.22, 0, 0.46]) rotate(IF_ARM_R[0])
            {
                part_upperarm_r();
                translate([0, 0, -0.32]) rotate(IF_ARM_R[1]) { part_forearm_r(); translate([0, 0, -0.30]) rotate(IF_ARM_R[2]) mirror([1, 0, 0]) part_hand(); }
            }
        }
    }
}

// ================= 农舍 + 室内家具（nd_house_rural 的家具布局，去掉玩法刷新点） =================

HOUSE_SEED = 3;
HOUSE_L = 12;
HOUSE_D = 8;
module lk_farmhouse(seed, L, D)
{
    dx = (cw_rnd(seed + 5, 2) == 0 ? -1 : 1) * L * 0.24;
    gk_flatten()
    {
        bh_bldg_farmhouse(seed, L, D, 1, 0);
        translate([0, 0, 0.12])
        {
            translate([-dx * 1.05, D / 2 - 0.72, 0]) cw_prop_bed(seed);
            translate([dx, D / 2 - 0.47, 0]) cw_prop_wardrobe(seed + 1);
            translate([-dx * 0.9, -D / 2 + 0.75, 0]) cw_prop_table(seed + 2, 1.0, 0.6);
            translate([-dx * 0.42, -D / 2 + 0.72, 0]) cw_prop_chair(seed + 3);
        }
    }
}

// ================= 摆场 =================

lk_at(HOUSE) lk_farmhouse(HOUSE_SEED, HOUSE_L, HOUSE_D);

// 树：村口椴树（阔叶）+ 屋后云杉
lk_at([-11, 9], 20) gk_flatten() bh_nature_linden(7, 11);
lk_at([15.5, 16], 0) gk_flatten() bh_nature_spruce(23, 15);
lk_at([-16, 17], 0) gk_flatten() bh_nature_spruce(29, 12);

// 路边弃车 + 物资堆
lk_at([-9.5, -2.2], 6) gk_flatten() cw_veh_lada(2);
lk_at([-6.8, -13.2], 0) gk_flatten()
{
    cw_prop_pallet(1);
    translate([0, 0, 0.14]) cw_prop_crate_ammo(4);
}
lk_at([-8.6, -12.0], 0) gk_flatten() cw_prop_barrel(2);
lk_at([0.8, -13.4], 0) gk_flatten() cw_prop_woodpile(3);

// 武器陈列台：台面放 loot 版武器（平躺）
TABLE_SEED = 5;
TABLE_A = cw_rnd(TABLE_SEED, 20) - 10;   // 与 cw_prop_table 的随机偏转一致
lk_at([-5.6, -9.6], 0) gk_flatten()
{
    cw_prop_table(TABLE_SEED, 1.6, 0.8);
    rotate([0, 0, TABLE_A]) translate([0, 0, 0.755])
    {
        translate([-0.05, -0.18, 0]) nd_wpn_svt40();
        translate([-0.20, 0.12, 0]) nd_wpn_aks74u();
        translate([0.55, 0.20, 0]) nd_wpn_aps();
    }
}

// 角色：幸存者 3/4 侧身朝肖像机位；感染者在路边扑向他
lk_at([-2.2, -9.4], -18) lk_survivor();
lk_at([2.6, -11.6], -62) lk_infected();

// ================= 机位：第一个 gk_camera 是默认入场视角 =================

function lk_p(p, h) = [p[0], p[1], lk_z(p[0], p[1]) + h];
module cam_for(name)
{
    if (name == "diorama")  gk_camera_lookat([-19, -27, 16], lk_p([0, -1.5], 1.0), "diorama", 42);
    if (name == "portrait") gk_camera_lookat(lk_p([-3.1, -11.7], 1.62), lk_p([-2.25, -9.45], 1.38), "portrait", 34);
    if (name == "face")     gk_camera_lookat(lk_p([-2.47, -10.05], 1.70), lk_p([-2.24, -9.50], 1.645), "face", 26);
    if (name == "street")   gk_camera_lookat(lk_p([-14.5, -14.0], 1.70), lk_p([4, 5], 3.2), "street", 55);
    if (name == "interior") gk_camera_lookat(lk_p([HOUSE[0] + 1.2, HOUSE[1] + 2.6], 1.72), lk_p([HOUSE[0] - 0.8, HOUSE[1] - HOUSE_D / 2], 1.35), "interior", 72);
}
cam_for(FOCUS);
for (n = ["diorama", "portrait", "face", "street", "interior"]) if (n != FOCUS) cam_for(n);
