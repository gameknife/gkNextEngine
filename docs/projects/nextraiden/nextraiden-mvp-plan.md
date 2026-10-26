---
title: "NextRaiden MVP 开发计划"
category: plan
status: 实施中（P0–P2 已完成）
owner: NextRaiden
created: 2026-09-16
last_updated: 2026-09-16
---

# NextRaiden MVP 开发计划

配套设计：[NextRaiden 竖版街机射击 MVP 设计](nextraiden-mvp-design.md)。本文只排**顺序与验收**，
契约不在这里重复。完成后按文档生命周期规则删除本文，有价值的内容并入
`docs/AGENT_GUIDE/NextRaiden.md`。

## 原则

- **每个里程碑都要能跑起来并截一张图**，不攒到最后。渲染/取景类改动一律
  `gnb shot`，玩法类改动一律 `gnb validate`。
- 只构建受影响目标：`gnb.bat build NextRaiden`；改到 Engine 才 `gnb.bat build`（默认核心目标）。
- 数值先写进 JSON 再调，不在代码里放魔数——调难度是这个品类的主要工作量。
- 每个里程碑结束提交一次，信息写清楚这一步"能玩到哪儿"。

## 进度

| 里程碑 | 状态 |
| --- | --- |
| P0 骨架 | **已完成**（2026-09-16），验收记录见该节末尾 |
| P1 战斗核心 | **已完成**（2026-09-16），验收记录见该节末尾 |
| P2 关卡 1 全流程 | **已完成**（2026-09-17），验收记录见该节末尾；人工通关待用户确认 |
| P3 关卡 2 | **已完成**（2026-09-17），验收记录见该节末尾 |
| P4 HUD 与打磨 | 未开始 |

## P0 骨架：能滚动、能飞、能看

**目标**：一个空关卡带、玩家机在竖版取景里能走位。

1. `src/Application/Game/NextRaiden/`：`gk_add_application(NextRaiden … MODULES … ScadLoader LINK NextGameplay)`，
   在 `src/Application/Game/CMakeLists.txt` 加 `add_subdirectory(NextRaiden)`。
2. `NextRaidenGameInstance`：窗口 1600×900、`Modules::Scad::Register()`、
   `Engine::RequestLoadScene(stage1)`、固定步长骨架。
3. `assets/scad/source/next_raiden/nr_stage_common.scad`：`nr_band_*` 行组合子 + `nr_lod_*` flatten
   包装 + `nr_rack()` 料架；`stage1_airfield.scad` 先只放 20 行跑道/草地 + 料架 + 取景相机。
4. `ScrollCamera`：`OverrideRenderCamera`，`up = (0,0,-1)`，FOV 50°，高 47 m，俯角 15°，
   camZ 随时间递减。
5. `PrefabRack`：料架索引 + 子树克隆（设计 §5.3），先只克隆玩家机一份。
6. `PlayerShip`：移动 + 夹取范围，yaw = π。

**验收**
- `gnb.bat build NextRaiden` 通过，日志出现 `committed scene [stage1_airfield.scad]`。
- `gnb shot --target NextRaiden --frames 180`：竖版取景正确，**SCAD +X 在屏幕右侧**，前方在上方，
  玩家机在下 1/3 且朝上。
- `gnb shot --scene assets/scad/source/next_raiden/stage1_airfield.scad` 与游戏内取景一致。

**两个必须在这一步定死的风险检查点**（拖到后面改代价大得多）：
1. 俯角导致景观带近端露白 → 把带宽从 3 地块加到 5 地块。
2. **静态碰撞预算**：这条景观带是碎片要落上去的地面。读加载日志里的
   `physics …: shape cooking` 与 `FNextPhysicsBodyStats`，按 20 行景观带的实测值外推到整关。
   参照值：`airfield.scad`（48×64 m、14 084 三角）烘焙 15.3 ms、建体 0.21 ms。
   超预算时的处置顺序：① 把装饰性行（纯草地/树）`SetRayCastVisibleRecursive(false)` 去掉碰撞；
   ② 只给"会被碎片砸到的"行留碰撞；③ 兜底换成一块覆盖全关的静态平面
   （`NextPhysics::CreatePlaneBody`，碎片只落在 y=0，不会停在屋顶）。

### P0 验收记录（2026-09-16，全部通过）

- `gnb build NextRaiden` 通过；日志 `committed scene [stage1_airfield.scad]`。
- `gnb shot --target NextRaiden`：竖版取景成立，塔台（SCAD +X）在**右**、机库（−X）在**左**，
  前方在上方，玩家机在下 1/3 且机头朝上。关卡 `.scad` 自带的取景相机与游戏内一致。
- `gnb validate --script assets/agentscripts/nextraiden-smoke.agentscript.json`：38 步全过。
- **风险 1 结论**：景观带定死 **5 个地块 = 80 m**。默认 30% 立柱下带子只需覆盖 ±18.4 m
  （3 个地块就够），多出的两列是给"立柱收窄"留的余量——推导与代价见设计 §5.2。
- **风险 2 结论（碰撞预算，宽松通过）**：28 行景观带 + 2 栋建筑 = 25.5k 三角，
  `shape cooking` **13.3 ms**、`body creation` 0.07 ms、SCAD 解析 27 ms。
  按完整 2:40 关卡（约 75 行）外推 ≈ 68k 三角 / 36 ms 烘焙。**不需要任何降级手段**，
  整条景观带保留隐式静态碰撞，碎片有真实地面可落。
- 帧时间 2.7 ms（RTX 4070，2400×1350），119 节点 / 70 模型。

**两个踩到的坑，写在这里省得再踩：**

1. **`OnSceneUnloaded` 在 `BeforeSceneRebuild` 之后调用。** 在前者里清池子句柄，会把后者刚建好的
   池子连同旧世界一起扔掉（表现为"prefab 找得到但池子是空的"）。释放旧句柄要放在
   `BeforeSceneRebuild` 开头。引擎对 NextRig 池也有同样的注释，不是个例。
2. **`wait-frames` 不能用来断言"飞了多远"。** 这台机器一帧 2~3 ms，40 帧只够飞 2 米。
   位移类断言一律 `wait-until game.stageTime`，并且要按**净速度**算（往前飞是 18−7.5=10.5 m/s）。

## P1 战斗核心：打得死、会被打死

**目标**：杂兵会来、会开火、能被击坠，玩家会死。

1. `BulletPool` + `BulletPattern`（六种 pattern 的纯函数）+ `CollisionGrid`。
2. `EnemyPool`：六种 `path`、四种 `formation`，从料架克隆的 flatten 敌机。
3. `WaveDirector` + `stage1_waves.json` 的前 45 秒（scout 三波 + interceptor 包夹）。
4. 玩家主炮 vulcan 1–4 级、护盾/命/无敌闪烁/复活退级。
5. `DamageModel` 第 1 档 + **`DebrisSystem` 全套**（设计 §7.4）：dynamic box body、重心偏移、
   初速与角速度、四条回收规则、64 个预算、`nr.debrisBudget`。flatten 小敌先用"发替身"这条路径，
   "摘真块"留到 P2 接地面目标时一起做。
6. `RaidenAudio.hpp` + 占位音（拷到 `assets/sounds/nextraiden/`）。
7. CVar：`nr.invincible` / `nr.showHitboxes` / `nr.debrisBudget`。

**验收**
- `gkNextUnitTests "[NextRaiden]"`：pattern 六种各一条用例，波次解析拒绝未知字段。
- `gnb validate --script nextraiden-smoke`：移动 + 开火 30 s 后 `game.enemiesAlive` 归零、
  `game.score > 0`、`game.lives == 3`（开 `nr.invincible`）。
- `gnb shot --target NextRaiden --frames 600 --ui`：画面上同时有敌机、敌弹、我弹、爆炸。
- **碎片肉眼验收**：连续打 30 s 后截图，碎块应该躺在跑道上并随景观向下滑出，不悬空、不抖动、
  不穿地。悬空半个身位 = 重心偏移漏了；抖动/穿地 = box 尺寸或初速过大。
- **不泄漏**：`game.debrisAlive` 在打空后归零，物理 body 总数回到关卡初始值。

### P1 验收记录（2026-09-16，全部通过）

- `gkNextUnitTests "[NextRaiden]"`：12 个用例 / 2735 条断言全过。覆盖六种 pattern 的角度与
  速度、扇形对称性与单发退化、ring 的 volley 旋转、缓冲区截断、四种编队几何、枚举严格解析，
  以及**真实的 `stage1_waves.json`**（不是 fixture——拼错的波次正是这条用例存在的理由）。
- `gnb validate --script nextraiden-smoke`：38 步全过（P0 的空间契约未被 P1 破坏）。
- `gnb validate --script nextraiden-combat`：28 步全过。按住开火跑完 48 秒，十波全部释放、
  13 杀 / 2600 分、无敌下不掉命、碎片数始终不超预算。
- **碎片肉眼验收通过**：碎块被抛出、落到跑道与农田上、随景观向下滑出视口后回收；
  末尾截图里同时有 4~6 块躺在地上。无悬空、无抖动、无穿地。
- **不泄漏**：一段战斗打空后 `game.debrisAlive` 回到 0。
- 全量 `gkNextUnitTests` 有 7 个**先于本工作存在**的失败（Test_PhysicsSync 的刚体/载具
  fixture、Test_TerrainWalkable 与两个 geo 用例缺生成资产）。已用 `git stash` 把本轮
  src/Tests 改动摘掉后复跑确认：失败数与断言数完全一致，与 NextRaiden 无关。

**与计划的两处偏离（都是有意的）：**

1. **没有写 `CollisionGrid`。** 最坏情况是 64 发我方子弹 × 16 架在场敌机，约一千次距离判定
   /定步，在紧随其后的场景图写入面前根本不显形。宽相加进来只会多一百多行没有价值的代码；
   风险表里那条"帧时间掉了就上宽相"仍然成立，等它真的掉了再说。判定折叠成
   `NextRaidenGameInstance.cpp` 里一个 `OverlapsXZ`。
2. **碎片 body 改成每个池位建一次、永久复用**，而不是设计里写的"现建现删"。每个池位绑死一种
   零件预制件，盒体尺寸恒定，所以复用是无条件更便宜的那一半；设计 §7.4 本来就留了这个口子。

**这一步踩到的坑（第二次撞上同一个）：** `debris_.Unbind()` 一开始放在 `OnSceneUnloaded`，
于是 `BeforeSceneRebuild` 刚建好的 70 个碎片池位又被清空。症状是"什么都不爆"，而且一行错误
日志都没有。**结论：所有池子的释放都必须在 `BeforeSceneRebuild` 开头，`OnSceneUnloaded` 里
只放 `sceneReady_ = false`。**

## P2 关卡 1 全流程

**目标**：关卡 1 从头打到通关。

1. `StageIndex`：扫 `.scad` 建景观行/地面目标索引，读 `Node::metadata`。
2. `GroundTargetSystem` + `DamageModel` 第 2 档：按 `releaseStage` 分档，**摘真块**交给
   `DebrisSystem`（保世界位姿的 `ClearParent` + 挂 body），crater、`fueldepot` 范围爆炸。
   `bomber` 改用非 flatten 版，让它被击坠时能甩出真部件。
3. `ItemSystem`：五种道具的掉落、吸引、拾取；僚机（option）跟随与 `nr_wpn_laser`。
4. 炸弹：清屏 + 大额伤害 + 无敌窗口。
5. `BossSystem` + rig 池：rotorcraft 中BOSS、gunship BOSS 的阶段脚本与部件损毁
   （`NextRig::GetBoneNodeId` → 摘块 → `DebrisSystem`）。
6. `StageFlow` 完整状态机 + `nr.spawnBoss` CVar。
7. `stage1_airfield.scad` 补完 2:40 的完整景观带与地面目标；`stage1_waves.json` 补完。

**验收**
- 人工通关一次关卡 1（这一步必须真玩，脚本替代不了手感）。
- `gnb validate`：`nr.spawnBoss` 直接进 BOSS，断言三个阶段的 `game.bossHp` 依次跨过阈值，
  最终 `game.state == "StageClear"`。
- `gkNextUnitTests`：`StageIndex` 对真实 `stage1_airfield.scad` 的索引数量断言
  （地面目标数、景观行数、料架完整性），照 `Test_NextAstrobot.cpp` 的写法。

### P2 验收记录（2026-09-17）

关卡 1 的完整内容已经就位：2:40 的景观带（82 行）、**35 个手摆的地面目标**、29 条波次、
五种掉落 + 僚机、中BOSS（rotorcraft，两阶段）与关底 BOSS（gunship，三阶段）、
Ready → Playing → BossIntro → Boss → StageClear 的完整流程。

- `gkNextUnitTests "[NextRaiden]"`：17 个用例 / 2831 条断言全过。新增 StageFlow 状态机
  （含"BOSS 战中死亡回到 BOSS 而不是波次"和"错状态的事件被忽略"）、地面目标枚举、
  以及**真实 `stage1_waves.json` 的覆盖性检查**——全程无 12 秒以上的空档、最后一波要撑到
  BOSS 触发时刻。全量套件仍是那 7 个先于本工作存在的失败，数量未变。
- `gnb validate --script nextraiden-boss`：41 步全过。跳到中BOSS → 两阶段 → 交还关卡 →
  跳到关底 BOSS → 三阶段 → `stageClear`，7 万分。
- `gnb validate --script nextraiden-combat`：33 步全过，新增"地面目标被拆掉"和"吃到 power"两条。
- `gnb validate --script nextraiden-smoke`：38 步全过（P0 的空间契约没被 P2 破坏）。
- **肉眼验收**：截图里地面炮塔在向玩家开火、被打掉的目标把**自己的 chunk** 甩成物理碎块落在
  农田和公路上、僚机跟在玩家侧后方、BOSS 血条随阶段推进、关底 BOSS 掉了一侧引擎后继续打。

**仍欠一项**：计划里"人工通关一次关卡 1"这条我做不了——脚本能验证状态机和数值，验证不了手感。
难度曲线、弹幕密度、BOSS 时长这些只有真玩一遍才知道，需要你跑一次
`gnb run NextRaiden`（`nr.spawnBoss` 可以跳到 BOSS）。

**与计划的偏离**：
- `DamageModel` 没有独立成文件。分级掉块只有一处调用者（`FGroundTargetSystem::ShedStage`），
  摘块本身在 `FDebrisSystem::Detach`；再切一层只会多一个只被调用一次的间接层。
- 炸弹在 P1 就做完了，P2 只是把它接到地面目标和 BOSS 上。

### P2 踩到的坑

1. **资产 bug：三个 rig 的四条 `anim_*` 少一个 `]`。** `next_raiden_rotor.scad` 的
   `anim_hover` / `anim_fire`、`next_raiden_gunship.scad` 与 `next_raiden_fortress.scad` 的
   `anim_idle`，全是同一个错误。这三个 rig 从提交到现在**没有被加载过**（静态 kit 场景不用
   rig），所以错误一直没暴露，直到 BOSS 去 `DeclarePool` 才炸出来。已修，并用括号平衡脚本
   全量复查过三个文件。**教训：只被 showcase 场景引用的资产等于没被验证过。**
2. **rig 的骨骼名保留 `bone_` 前缀，clip 名才剥掉 `anim_`。** `FScadRigLoader` 里
   `bone.name = boneNode.name` 是原样，而 clip 走 `varName.substr(animPrefix.size())`。
   写成 `"pod_l"` 会静默找不到骨骼（只有一行 warning），必须写 `"bone_pod_l"`。
3. **Unity build 会把各 `.cpp` 的匿名 namespace 合到一起。** 三个文件各自
   `constexpr float kTwoPi` 就是重定义错误。共享常量直接用 `glm::two_pi<float>()`，
   文件私有的常量名要带文件语义（`kRackPrefabPrefix` 而不是 `kPrefabPrefix`）。
4. **JSON 里以 key 为枚举的表必须跳过 `_comment`。** 否则每加一句注释就多一行
   "unknown ground target '_comment'" 的 warning。
5. **跳 BOSS 不能只推进关卡时钟。** `stageTime_` 一跳，WaveDirector 会把这之前所有波次
   在同一步里全放出来。加了 `SkipTo()`：只推游标、不生成。

### P2 之后的手感调整（2026-09-17，用户试玩后）

试玩反馈两条：单位太大、移动范围小且偏慢；以及不想一直按着键开火。

1. **世界尺度 k = 1.6 整体重标定。** 根因是"播放列只有 4.7 个机身宽"（原版约 7）。相机
   47 → 75 m，`halfWidth` 13 → 21（7.6 个机身宽），玩家速度 18 → **34**（乘 1.9，不是 1.6——
   等比例只能保住原来的手感）。滚动、敌我弹速、敌机速度、BOSS 参数、吸取半径、
   `.scad` 里地面目标的 y 全部同步乘 1.6；模型尺寸与判定半径**不**乘。
   推导与"哪些乘哪些不乘"的完整清单写在设计 §5.2。
   - 关卡因此变长（12 m/s × 同样的秒数）：景观带从 82 行重排到 131 行，`endZ` −1180 → −1900。
   - `nr_row_runway` 改成中间三列跑道：45 m 的播放列里一条 16 m 跑道会变成"草地里的一条路"。
   - 三个按旧尺度写死的常量跟着改：子弹横向剔除 46 → 76、敌机剔除 60 → 95、
     BOSS 横移夹取 ±16 → ±26（原值正好等于关底 BOSS 的 moveAmplitude，会把横移两端削平）。
2. **自动射击默认开**，C 键切换，HUD 右栏显示 `FIRE AUTO` / `FIRE HOLD`，
   也可以用 `nr.autoFire` cvar。关掉后按住 Z / J / 空格 / 手柄扳机仍然能开火。

验证：三条 agentscript（38 + 33 + 41 步）与 17 个单测用例全过；另写了一条一次性探针脚本确认
"不碰键盘就有子弹 → C 之后停火 → 按住 Z 仍能开火 → C 切回来"。

**踩到的坑**：smoke 脚本里那些播放列边界的断言是硬编码的绝对值，尺度一改全部失效——这是
对的（它断言的就是边界），但另外两条"取景范围"的断言原本也写成绝对 Z，那两条不该随尺度变。
改成了 `game.groundNearLead` / `game.groundFarLead`（相对相机中心），与尺度无关。
另外"贴住边界"的断言不能写成等于边界：那是一场和固定步长的竞赛，要留一个步长的余量。

## P3 关卡 2

**目标**：第二关更长更难，通关进入 All Clear。

1. `stage2_harbor.scad`：水面/码头景观带 3:20，patrolboat / crane / container / factory 布置。
2. `stage2_waves.json`：新增 `spiral`、`sweep`，弹速与密度整体上调，中BOSS 用 gunship 的强参数档。
3. fortress BOSS 四阶段 + 反应堆弱点（受伤 ×3）+ 舱门 clip 联动。
4. `stages.json` 串起两关与 All Clear 结算。

**验收**
- 人工通关两关。
- `gnb validate` 的第二个脚本覆盖关卡 2 BOSS 的四个阶段。
- 关卡 2 的敌弹峰值（`game.bulletsAlive`）记录在 journal 里，作为后续性能基线。

### P3 验收记录（2026-09-17）

关卡 2「军港破袭」3:05：146 行海港景观、76 个地面/水面目标、41 波、中BOSS（gunship 强化档）
与关底 fortress（四阶段 + 反应堆弱点）。打完它 = All Clear，一局从关卡 1 自动串到关卡 2。

- `gkNextUnitTests "[NextRaiden]"`：**20 个用例 / 2981 条断言**全过。新增三个：关卡 2 波次表
  的解析与覆盖性（含"spiral 和 sweep 必须还在表里"——它们要是掉了，关卡 2 就悄悄变回一个
  更长的关卡 1）、弱点倍率的纯函数、以及**整张关卡表的结构不变量**（每关恰好最后一个 BOSS
  `endsStage`、阶段血量递减、`shedBones` 必须带 `bone_` 前缀）。最后那条是把 P2 踩过的坑
  写成了测试。
- `gnb validate --script nextraiden-stage2`：**98 步全过**。它验的是别处没覆盖的两件事——
  一局从关卡 1 打完关底 BOSS 后自动载入关卡 2、再打完要塞进 All Clear；以及要塞四个阶段里
  `game.bossWeakPoint` 的开→关→开。
- `gnb validate --script nextraiden-{smoke,combat,boss}`：38 / 33 / 41 步全过，关卡 1 没被碰坏。
- **加载与碰撞预算**：关卡 2 解析 91 ms、131 632 三角、1199 节点，`shape cooking` 47.8 ms，
  提交 162 ms。P0 给 75 行外推的是 36 ms；现在 146 行 47.8 ms，**仍然不需要任何降级手段**。
- **弹幕峰值（性能基线）**：跑到 2:30 为止 `game.bulletsPeak` = **109**（其中到中BOSS 提示点
  为止是 77）。测法是一条一次性探针脚本：玩家无敌、停在原地、只有自动射击，所以每一波都活到
  自己飞出屏幕为止并全程开火——比真人玩出来的密度更高。对照弹体池（`boltHostile` 192 /
  `orb` 96 / `needle` 96），**峰值离池上限还有一倍余量**。采样时刻 41 发在飞、37 块碎片
  （上限 64）、帧率 295 FPS（RTX 4070，2400×1350）。
- **肉眼验收**：码头在屏幕左、水面在右（取景契约）；要塞开舱后血条下方出现 `CORE EXPOSED`；
  甩下来的挂架落在码头和水面上；All Clear 画面上 17 块碎片还留在场上。

**仍欠一项**：和 P2 一样，"人工通关两关"这条我做不了。脚本能验证状态机和数值，验证不了手感——
尤其是**要塞的弱点窗口够不够抢**：4000 血、弱点 ×3 / 躯干 ×0.35，这两个数决定这场仗是 25 秒
还是 90 秒，只有真打一遍才知道。旋钮就是 `stages.json` 里 `fortress_boss` 的 `hitPoints`
和两个 `weakPoint` 块。

**与计划的偏离**：
- **关卡 2 的滚动速度没有提**，仍是 12 m/s。它同时是"`.scad` 的 y 换算成第几秒"的那把尺，
  两关用两套心算只会把摆件摆错位。难度改从密度、弹速和 BOSS 上要。
- 时长 3:05 而不是 3:20——BOSS 触发在 3:00，再长就是一段没有内容的水面。
- 新增了 `nr.bossHp`（把在场 BOSS 的血量设成某个比例）。阶段是按剩余血量选的，所以它就是
  "跳到第 N 阶段"；`nr.spawnBoss` 省的是打到 BOSS 的两分钟，这个省的是打到第四阶段的一分钟。

### P3 踩到的坑

1. **`gnb shot --frames 240` 有一半的概率拍不到玩家机。** 出生无敌 1.5 s 期间机体以 0.24 s
   的周期闪烁，240 帧（这台机器上约 0.65 s）正落在无敌窗口里，有一半机会撞上"灭"的那半个
   周期。查了半天以为是克隆或可见性坏了——不是，**截关卡取景要 `--frames 750` 以上**，
   跳过无敌期再拍。
2. **agentscript 的单步超时键是 `timeoutMs`，不是 `stepTimeoutMs`。** 后者只在 `defaults`
   里有效，写在步骤上会被静默忽略，于是"给足 200 秒"实际还是 20 秒默认值。因为断言本来就
   在 20 秒内过了，这个错误不会让脚本失败——只会在你真需要长等待的那天才发作。
3. **`game.bulletsAlive` 不能用来断言"屏幕是干净的"。** 自动射击一开，玩家的弹流永不为零，
   `== 0` 这条断言永远不可能成立。改成了新的 `game.hostileBullets`。顺带发现一个真问题：
   All Clear 之后玩家还在继续开火——已修，`FStageFlow::RunEnded()` 期间不接受开火输入
   （机体照常飞，冻住会被读成崩溃）。
4. **"关卡刚载入时世界是干净的"这条只能在 `ready` 状态断言。** 进 `playing` 一秒，岸炮就已经
   在开火了，`hostileBullets == 0` 会因为关卡在正常工作而失败。
5. **尺度变了，内容也得跟着变**（和 P2 那条同源）：45 m 的播放列里，`nr_row_quay` 那种"一侧
   是岸"的行才读得出海港；而 `nr_row_channel` 的两岸落在 |x| ≥ 24，**整个在 HUD 立柱后面**，
   所以水道段只能放船——摆在那两列上的地面目标玩家看不见也打不着。

## P4 HUD、流程与打磨

1. `RaidenHud`：两侧立柱、BOSS 血条、Title / Stage Clear / Game Over / All Clear 画面，
   全部按 `UI.GetScreenSize()` 比例布局。
2. 计分与结算（medal 递增、关底奖励、10 万加命）。
3. 手柄映射与死区；慢速键。
4. 性能：关卡 2 峰值帧时间、节点数、三角数、物理 body 数（静态/动态分开）记录；必要时把景观行
   合并粒度调大或收紧 `nr.debrisBudget`。
5. 文档：新增 `docs/AGENT_GUIDE/NextRaiden.md`（现行契约），在 `AGENTS.md` 的 Subprojects
   与 `docs/README.md` 的索引里登记，删除本计划。

**验收**
- `gnb shot --target NextRaiden --ui` 的截图能直接当宣传图（HUD 完整、无穿帮）。
- 从 Title 连打到 All Clear 全程无崩溃、无节点泄漏（结算后 `game.bulletsAlive == 0`）。

## 顺带产出的资产改动

只在这几处，其余 kit 文件不动：

| 文件 | 改动 |
| --- | --- |
| `assets/scad/source/next_raiden/nr_stage_common.scad` | **新增**：`nr_band_*` / `nr_lod_*` / `nr_rack` |
| `assets/scad/source/next_raiden/stage1_airfield.scad` | **新增**：关卡 1 |
| `assets/scad/source/next_raiden/stage2_harbor.scad` | **新增**：关卡 2 |
| `assets/sounds/nextraiden/*.wav` | **新增**：占位音 |
| `assets/configs/nextraiden/*.json` | **新增**：四个配置 |
| `assets/agentscripts/nextraiden-*.agentscript.json` | **新增**：两个验证脚本 |

`assets/scad/lib/kit_raiden.scad` 等 kit 文件**不改**——新增的都是 source 层组合子，所以不需要
重跑 `gnb scad catalog`。若后续确实要往 kit 里加 module，记得同步 catalog。

## 风险与对策

| 风险 | 征兆 | 对策 |
| --- | --- | --- |
| 长条关卡解析慢 / 节点爆炸 | `parsed scene` 超过 300 ms，或节点数 >3000 | 景观按行 `gk_flatten` 已是主要手段；再不够就把相邻 3 行合成一个 module 调用 |
| 俯角让景观带露白 | 画面近端出现背景色 | P0 就定死带宽（3 或 5 地块），不要拖到 P3 |
| 子弹数上去后帧时间掉 | 关卡 2 后半掉帧 | `CollisionGrid` 宽相 + 每帧一次 `MarkTransformDirty`；仍不够就降弹体网格面数（用 `bolt` 替 `orb`） |
| BOSS rig 骨骼隐藏找不到节点 | 打掉部件没有视觉反馈 | 用 `NextRig::GetBoneNodeId` 拿到节点后走 `NodeUtils::SetVisibleRecursive`，不要只改根 |
| 难度调不动 | 改一个数值要改代码 | 所有数值进 JSON 是 P1 的硬要求，不是打磨项 |
| 整条景观带的静态碰撞太贵 | 加载日志 `shape cooking` 明显变长，或 body 数上千 | P0 就量（见上），按"去掉装饰行碰撞 → 只留会被砸到的行 → 静态平面兜底"三级处置 |
| 池节点被自动挂上静态碰撞 | 停放位置留下一堆看不见的碰撞体，碎片撞空气 | 池节点建完必须 `SetRayCastVisibleRecursive(false)`，这是设计 §5.3 的硬规则 |
| 碎片穿地 / 悬空 / 抖动 | 碎块陷进跑道，或浮在半空，或原地颤 | 穿地=box 太薄或初速过大，悬空=漏了 `SetPhysicsOffset({0, sz/2, 0})`，抖动=堆叠过深，降 `nr.debrisBudget` |
| 碎片泄漏 | `game.debrisAlive` 只增不减，帧时间随时间劣化 | 四条回收规则必须都实现，尤其"滑出视口"那条要用**地面平面**的下边界而不是飞行平面的 |
| 碎片影响玩法判定 | 回放/`gnb validate` 断言开始飘 | 碎片 body 只和静态景观与彼此碰撞；玩法物体一律无 body（设计 §7.4 边界） |

## 不在本计划内

设计 §11 列的全部内容。其中"武器颜色切换"和"续关"是最可能的下一步，两者都只动
`gameplay.json` + `PlayerShip` + HUD，不触碰本计划确立的空间契约与对象池机制。
