// playground.scad —— ScadPlay 起始关卡「试玩场」
// 边玩边改：游戏运行时保存本文件，1～2 秒内关卡原地重建，玩家留在原处。
// 由西向东三座岛：
//   岛 A（x -6..18）  出生点 → 金币 → 踩踏板开门（door_a 听 plate_a）
//   缺口（x 18..24）  移动平台往返渡过，掉下去是岩浆
//   岛 B（x 24..36）  检查点 → 旋转平台 → 巡逻敌人 → 扳拉杆放下吊桥（bridge_b 听 lever_b）
//   岛 C（x 41.5..55.5）弹跳垫跳上高台 → 终点（goal 听 all_coins：先把金币吃齐）
// gnb shot --target ScadPlay

use <../../lib/kit_scadplay.scad>

sp_level(title = "试玩场", goal = "吃齐金币，打开终点");

// ================= 岛 A =================
translate([6, 0, 0]) sp_ground(w = 24, d = 16);
translate([0, 0, 0]) sp_spawn(id = "start");
translate([2, 3.5, 0]) rotate([0, 0, -90]) sp_sign(text = "WASD 移动，空格跳，Shift 跑，右键拖动转视角。踩红色踏板开门。", label = "?");
translate([3, 0, 0]) sp_coin_row(n = 5, dx = 1.2, prefix = "coin_a");

// 东墙 + 门：门沿 y 展开（rotate 90），玩家沿 +x 穿过
translate([12, -4.75, 0]) rotate([0, 0, 90]) sp_wall(l = 6.5, h = 3.2);
translate([12, 4.75, 0]) rotate([0, 0, 90]) sp_wall(l = 6.5, h = 3.2);
translate([12, 0, 0]) rotate([0, 0, 90]) sp_door(id = "door_a", on = "plate_a", w = 2, h = 3);
translate([8, -4, 0]) sp_plate(id = "plate_a", latch = true);

translate([15, 5, 0]) sp_tree(seed = 1);
translate([16.5, -5.5, 0]) sp_tree(seed = 2);
translate([4, -5.5, 0]) sp_crate();
translate([5.1, -5.6, 0]) sp_crate(s = 0.8);

// ================= 缺口：移动平台 + 岩浆 =================
// 甲板顶面比岸低 5 cm（不与地面共面），两端离岸各 0.2 m
translate([19.7, 0, -0.45]) sp_mover(id = "ferry", travel = [3.1, 0, 0], speed = 1.4, wait = 1.2);
translate([21, 0, -4]) sp_hazard(w = 6, d = 16, kind = "lava");

// ================= 岛 B =================
translate([30, 0, 0]) sp_ground(w = 12, d = 16);
translate([25.5, 2.5, 0]) sp_checkpoint(id = "cp_b");
translate([29, -3.5, 0]) sp_spinner(id = "spin_b", r = 2.5, speed = 35);
translate([28, 4.5, 0]) sp_enemy(id = "slime_b", travel = [6, 0, 0], speed = 1.4);
translate([33, -3.5, 0]) sp_coin_row(n = 3, dx = 1.0, prefix = "coin_b");
translate([34.5, 3, 0]) rotate([0, 0, 180]) sp_lever(id = "lever_b");
translate([36, 0, 0]) sp_bridge(id = "bridge_b", on = "lever_b", len = 5.6, w = 2.4);
translate([38.75, 0, -4]) sp_hazard(w = 5.5, d = 16, kind = "lava");

// ================= 岛 C =================
translate([48.5, 0, 0]) sp_ground(w = 14, d = 16);
translate([44, 0, 0]) sp_jumppad(id = "pad_c", launch = 4.5);
translate([49, 0, 0]) sp_block(w = 5, d = 5, h = 3.2);
translate([49, 0, 3.2]) sp_goal(id = "goal", on = "all_coins");
sp_counter(id = "all_coins", from = "sp_coin");
translate([45, 4.5, 0]) rotate([0, 0, 180]) sp_sign(text = "终点要先吃齐全部金币。可以让 agent 改规则，比如“终点不需要金币”。", label = "!");

// ==== Agent additions（agent 只在这一行之后追加） ====
