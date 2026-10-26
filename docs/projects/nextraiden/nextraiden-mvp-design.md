---
title: "NextRaiden 竖版街机射击 MVP 设计"
category: project
status: 实施中（P0–P2 已完成）
owner: NextRaiden
created: 2026-09-16
last_updated: 2026-09-16
---

# NextRaiden 竖版街机射击 MVP 设计

> 本文是 `NextRaiden` 的**设计契约**：把 `7e4192e5` 提交的 `kit_raiden` 资产做成一个能从头打到尾的
> 雷电式竖版射击 MVP（两关，各自有完整流程）。开发顺序见
> [开发计划](nextraiden-mvp-plan.md)。实施后现行契约转入 `docs/AGENT_GUIDE/NextRaiden.md`，本文退为
> 设计记录。

## 1. 决策摘要

1. **原生 C++ 子应用**，目标 `NextRaiden`，源码 `src/Application/Game/NextRaiden/`。理由见 §4.1：
   本项目的核心是"按模块名索引 `.scad` 节点 + 运行时克隆子树做对象池"，这两件事 C# 绑定面都没有。
2. **`.scad` 负责空间，JSON 负责时间。** 关卡的**景观带、地面/海面目标、预制件料架**写在
   `.scad` 里（`nr_stage_common.scad` 提供组合子）；**波次、弹幕、BOSS 阶段**写在
   `assets/configs/nextraiden/*.json`。竖版射击的关卡本质是一条时间线，硬塞进 `.scad` 会让调难度
   变成改几何。
3. **对象池在 `BeforeSceneRebuild` 里一次性建完，运行时零分配。** 引擎先解析完 `.scad` 再调这个钩子，
   钩子拿到的就是该场景的 `nodes/models/materials`；游戏在这里找到"料架"里每种预制件的根节点，
   深拷贝 N 份进同一个 `nodes` 数组（**共享 modelId**），全部停在地下不可见。运行时只改 transform
   和可见性。BOSS 走 `NextRig::DeclarePool`，同一个钩子里声明。
4. **屏幕是横的，战场是竖的，两侧立柱是不透明 HUD。** 相机竖直 FOV 决定竖向取景（`glm::perspective`
   的 fov 是竖直角），横向多出来的部分由左右 HUD 立柱盖住，所以景观带只需要覆盖中间那一列。
5. **物理只用来演碎片，不参与玩法。** 命中、擦弹、拾取全是飞行平面上的 2D 圆/矩形重叠，纯 C++
   数学，跑在固定步长上，确定性可回放。Jolt 只负责被打下来的**碎块**：它们带 dynamic body 真的
   下坠、落地、翻滚、堆叠，滑出视口后回收（§7.4）。这条边界是硬的——碎片**不产生任何伤害、
   不改变任何玩法状态**，所以物理在引擎自己的节拍上跑、和我们的定步长不同步也没关系，
   `gnb validate` 的断言仍然稳定。

## 2. 资产盘点：现有 kit 能给什么

`assets/scad/lib/kit_raiden.scad`（56 个公开 module）+ `raiden_parts.scad`（chunk 几何与配色）+
`raiden_airframes.scad`（6 种机体的 chunk 表）+ 3 个 ScadRig 角色。**MVP 不需要新增几何 kit**。

| 类别 | 模块 | MVP 用法 |
| --- | --- | --- |
| 滚动地块（16 m 节距，顶面 z=0） | `nr_ground_{grass,runway,road,water,quay,farmland}` | 景观带，按行拼 |
| 建筑 | `nr_bldg_{hangar,controltower,bunker,factory,fueldepot}` | 景观 + 可破坏目标（bunker 带 chunk 分级） |
| 场景道具 | `nr_prop_{radar,container,crane,barrier,crate,wreck,turret}` | 景观；`turret` 是可开火目标 |
| 自然物 | `nr_nature_{tree,rock}` | 景观带边缘填充 |
| 地面/水面目标 | `nr_veh_{tank,launcher,patrolboat}`、`nr_prop_turret` | 地面敌，带 `releaseStage` 分级损毁 |
| 空中敌机 | `nr_veh_{scout,interceptor,bomber,rotorcraft,gunship,fortress}` | 杂兵 / 中BOSS / BOSS |
| 我方 | `nr_veh_player`（interceptor，paint 0 蓝）、`nr_veh_wingman`（scout，paint 0） | 玩家机、僚机(option) |
| 弹药 | `nr_wpn_{bolt,orb,needle,missile,bomb,laser}` | 六种弹体，`bolt(hostile)` 分敌我配色 |
| 道具 | `nr_item_{power,medal,repair,bomb,option}` | 五种掉落，正好覆盖 MVP 的五条奖励线 |
| 特效体 | `nr_fx_{exhaust,flash,blast,smoke,crater}` | 纯网格，由游戏控制生命周期与缩放 |
| 碎片 | `nr_part_{armour,wing,canopy,engine,core,gun,fin,blade,pod}` | 物理碎片的"替身"池（真块直接从被击毁物上摘，见 §7.4） |
| ScadRig | `characters/next_raiden_{rotor,gunship,fortress}.scad` | 中BOSS / BOSS，见 §7.5 |

**已验证**：`gnb shot --scene assets/scad/source/next_raiden/airfield.scad` 在俯视角下
读数良好（342 节点 / 14 084 三角 / CPU 解析 22 ms），配色在俯视下能分清敌我与地面类型。

`airfield.scad` / `harbor.scad` / `fleet.scad` / `showcase.scad` 是**美术验收场景，不是关卡**：
MVP 另写两个长条关卡文件（§6），但可以照抄它们的摆放密度与配色搭配。

## 3. 产品目标：MVP 是什么

一次完整游玩：投币画面 → 关卡 1 → 关卡 2 → 结算。两关都有**序章 / 杂兵推进 / 地面目标段 /
中BOSS / 核心区 / BOSS / 过关**的完整节奏，关卡 2 更长、更密、弹速更快。

### 3.1 关卡 1「机场突入」约 2:40

```text
0:04 跑道序章：scout 直线/正弦，只有 bolt，教走位
0:17 地面段：tank 纵队 + barrier，turret 夹道点射；首个 power 掉落
0:35 农地：launcher 发 missile；interceptor 左右弧线进场
1:12 中BOSS：rotorcraft 悬停（chin gun 连射 + orb 环），击破掉 option
1:17 机场核心：hangar / factory / fueldepot，防空密度峰值
2:32 BOSS：gunship 三阶段（§7.5）；击破 = 过关，自动载入关卡 2
```
（秒数取自 `stage1_waves.json` 与 `stages.json`，不是估的。滚动 12 m/s，`.scad` 里
`(y-22)/12` 就是同一把尺。）

### 3.2 关卡 2「军港破袭」约 3:05

```text
0:03 入港：左岸码头，岸炮教「岸上也要打」；开场就是四架编队，没有教学段
0:08 出港水道：patrolboat 编队，水道两岸在播放列外，只有船
0:16 右岸栈桥：岸换到右边，逼玩家换边
0:32 第一条 spiral（bomber 悬停）：旋转的弹幕站着躲不掉
0:44 第一条 sweep（interceptor）：单发，但角度来回扫
1:22 中BOSS：gunship 强化档（血量 1600，多一门 sweep 炮）
1:54 弹药库码头：三座油库挨着，一次连锁能清掉半条岸
3:00 BOSS：fortress 四阶段（§7.5）；击破 = All Clear
```

比关卡 1 长约 18%（景观 146 行 / 76 个地面目标 / 41 波，对关卡 1 的 131 / 35 / 29）。
**难度不来自表速**：滚动速度和关卡 1 一样是 12 m/s，因为它同时是地面目标的节奏和
「`.scad` 的 y 换算成第几秒」的那把尺，两关用两套心算只会把摆件摆错位。升的是三处——
弹速整体高约一成、编队更大、以及 `spiral` 与 `sweep` 这两种"角度自己会动"的弹幕。

海港景观有两种行（`nr_stage_common.scad`）：`nr_row_quay(side)` 一侧是岸、一侧是水，
岸的内缘落在 |x| = 8，所以播放列里 13 m 是码头、其余是水；`nr_row_channel` 两岸都在
|x| ≥ 24（HUD 立柱后面），所以水道段**只放船**——摆在岸上的东西玩家看不见也打不着。

### 3.3 玩家能力

| 能力 | 输入 | 规则 |
| --- | --- | --- |
| 移动 | WASD / 方向键 / 手柄左摇杆 | 34 m/s；按住慢速键 0.5 倍；限制在播放列内（§5.2）。横穿播放列 1.24 s |
| 主炮 | **默认自动射击**；C 切换回按住 Z / J / 空格 / 手柄扳机 | vulcan，等级 1–4：1 路 → 2 路 → 3 路 → 5 路；`nr_wpn_bolt` |
| 僚机 | 拾取 `option`（最多 2） | 跟随玩家两侧，发射 `nr_wpn_laser` 细束，等级随主炮 |
| 炸弹 | X / K / 手柄 B | 清屏：全屏敌弹消除 + 对全体敌人/地面目标/BOSS 造成大额伤害，1.2 s 无敌；初始 3 |
| 护盾 | 被弹 | 3 点；受击 −1 并 1.5 s 无敌闪烁；归零损失 1 条命，火力退 1 级，原地复活 |
| 命 | — | 3 条；全部耗尽 → Game Over（MVP 无续关） |

**判定点远小于机体**：玩家机体宽 5.5 m，受击判定半径 **0.6 m**（街机手感的关键）；敌机判定用
机体外接圆的 0.7 倍。

### 3.4 掉落与计分

| 道具 | 模块 | 效果 |
| --- | --- | --- |
| Power | `nr_item_power` | 主炮 +1 级（满级转 2000 分） |
| Option | `nr_item_option` | 僚机 +1（上限 2，满则转 3000 分） |
| Bomb | `nr_item_bomb` | 炸弹库存 +1（上限 5） |
| Repair | `nr_item_repair` | 护盾 +1（上限 3） |
| Medal | `nr_item_medal` | 纯分数，值随连续拾取递增（100 → 200 → 500 → 1000） |

计分：击破分（按敌种）+ medal + 关底结算（剩余 bomb ×5000、剩余护盾 ×3000、剩余命 ×10000）。
10 万分加 1 条命。

## 4. 技术路线

### 4.1 为什么是 C++

| MVP 需要 | C# 绑定现状（`EngineApi.def.h`） | C++ |
| --- | --- | --- |
| 按模块名索引 `.scad` 节点（地面目标、料架） | 无（只有 `FindNodeIdWithComponent`） | `Scene::Nodes()` / `Node::GetName()` |
| 遍历子树、读 `Node::GetMetadata()` | 无 | `Node::Children()` + `Assets::Scad::ParseScadMetadata` |
| 建场景时克隆 `.scad` 子树、共享 modelId | 无（只有 Box/Sphere 程序化模型） | `BeforeSceneRebuild(nodes, models, …)` |
| 分级隐藏 chunk 子节点 | `SetNodeVisibleRecursive` 有，但拿不到子节点 id | `Assets::NodeUtils::SetVisibleRecursive` |
| ScadRig BOSS | 有（`Rig.*`） | 有（`NextRig`） |

补三条绑定是独立工作，不在 MVP 范围。结论：**C++**，与 `NextAstrobot` / `NextDayz` 同路线。

```cmake
# src/Application/Game/NextRaiden/CMakeLists.txt
file(GLOB_RECURSE nextRaidenSources CONFIGURE_DEPENDS "*.cpp" "*.hpp" "*.h")
gk_add_application(NextRaiden
    SOURCES ${nextRaidenSources}
    MODULES ${GK_STANDARD_RUNTIME_MODULES} ScadLoader
    LINK NextGameplay)          # NextRig 的实现在 NextGameplay
```

### 4.2 帧顺序

`OnTick(delta)` 里累加固定步长 **1/120 s**（上限 0.25 s），每个定步执行：

```text
1. 流程状态机（Title / StageIntro / Playing / BossIntro / Boss / StageClear / Dead / GameOver）
2. 滚动：camZ -= scrollSpeed * dt        （§5.1，唯一的世界时间基准）
3. 玩家：输入 → 速度 → 夹在播放列内 → 开火节流 → 僚机跟随
4. WaveDirector：按 stageTime 弹出到期波次 → EnemyPool.Spawn
5. 敌人：路径推进 → 弹幕 pattern 求值 → BulletPool.Emit
6. 地面目标：进入播放列则激活，瞄准玩家开火
7. 子弹：推进 → 出界回收
8. 碰撞：玩家弹×敌 / 玩家弹×地面目标 / 敌弹×玩家 / 玩家×敌机 / 玩家×道具
9. 伤害与死亡：分级隐藏/摘块 → 爆炸 FX → 碎片入库 → 掉落
10. 道具、FX 推进；`DebrisSystem` 只做**回收判定**（越界、沉底、超龄、池满）
```

定步结束后统一写节点 transform，最后 `Scene::MarkTransformDirty()` 一次。
**忘记这一行画面会完全静止且不报错**，是这套 API 最贵的坑。

碎片的位姿**不在上面这条链里**：body 由引擎在自己的节拍上模拟（`NextEngine` 每帧
`KickTick` / `TryCompleteTick` 后调 `Scene::SyncPhysics()`），节点 transform 由
`Node::SyncPhysics()` 从 body 写回。游戏对碎片只做三件事：抛出去、判定回收、暂停时
`NextPhysics::SetPaused(true)`。

### 4.3 代码结构

```text
src/Application/Game/NextRaiden/
├── NextRaidenGameInstance.{hpp,cpp}   生命周期、帧顺序、输入、相机覆盖、CVar、agent queries
├── NextRaidenConfig.{hpp,cpp}         gameplay.json / stages.json → FConfig
├── NextRaidenTypes.{hpp,cpp}          共享枚举与它们的严格解析
├── RaidenAudio.hpp                    音效单入口（占位音，见 §9）
├── Level/PrefabRack.{hpp,cpp}         料架索引 + 子树克隆（§5.3），本项目最核心的 40 行
├── Level/StageIndex.{hpp,cpp}         扫场景：景观行、地面目标、料架、预留相机
├── Level/StageFlow.{hpp,cpp}          纯逻辑状态机，可单测
├── Level/WaveDirector.{hpp,cpp}       时间线 → 生成请求，确定性
├── Level/PatternJson.{hpp,cpp}        弹幕 spec 的 JSON 读取，波次/地面/BOSS 共用一份
├── Combat/BulletPattern.{hpp,cpp}     六种弹幕的纯函数求值（可单测）
├── Combat/BulletPool.{hpp,cpp}        敌我弹体池、推进、回收、观感缩放
├── Combat/RaidenRandom.hpp            确定性 xorshift32
├── Combat/DebrisSystem.{hpp,cpp}      物理碎片：body 生命周期、抛射、回收、预算（§7.4）
├── World/EnemyPool.{hpp,cpp}          空中敌：路径 + 开火 + 生命
├── World/GroundTargetSystem.{hpp,cpp} 地面/水面目标：激活窗口 + 瞄准射击
├── World/BossSystem.{hpp,cpp}         rig BOSS：阶段、clip、弱点、部件损毁
├── World/ItemSystem.{hpp,cpp}         掉落、吸引、拾取
├── Player/PlayerShip.{hpp,cpp}        移动、武器、僚机、炸弹、护盾、复活
├── Player/ScrollCamera.{hpp,cpp}      滚动机位与取景换算
└── UI/RaidenHud.{hpp,cpp}             两侧立柱、BOSS 血条、流程画面（P4；P2 之前画在 GameInstance 里）
```

> **没有 `DamageModel` 这个文件。** 设计早期把"分级掉块"当成一个独立系统列在这里，实现下来
> 它只有一处调用者（`FGroundTargetSystem::ShedStage`），摘块本身在 `FDebrisSystem::Detach`——
> 再切一层只会多一个被调用一次的间接层。

> **没有宽相（broadphase）。** 最坏情况是 64 发我方子弹 × 16 架在场敌机，约一千次距离判定/定步，
> 在紧随其后的场景图写入面前不显形，所以判定就是 `NextRaidenGameInstance.cpp` 里一个
> `OverlapsXZ`。计划里原本列了 `CollisionGrid`，P1 实测后判定它是提前优化而删掉；
> 真等到帧时间掉了再加，那条风险仍写在计划的风险表里。

## 5. 空间契约（先读这节，改别的都得先对上）

### 5.1 坐标与朝向

SCAD → 引擎是 `world = (x, z, -y)`（Z-up → Y-up）。由此确定四条不可违背的约定：

| 项 | 约定 |
| --- | --- |
| 关卡前进方向 | **SCAD +Y** = 引擎 **−Z**。关卡从 SCAD y≈0 长到 y≈+800（关卡 2 更长），相机 Z 单调减小 |
| 屏幕上方 | 引擎 −Z（相机 `up = (0,0,-1)`），即"前方"在屏幕上方 |
| 屏幕右方 | 引擎 **+X** = SCAD +X。**关卡作者所见即所得，不镜像** |
| 默认朝向 | kit 的飞机/炮/船都朝 SCAD −Y = 引擎 +Z = **屏幕下方 = 朝向玩家**。所以敌方一律**不需要旋转**；只有玩家机取 yaw = π |

ScadRig 的 yaw 约定是"引擎 +Z 为正面，yaw = atan2(x, z)"，与上表自洽：BOSS 实例 yaw = 0 就是面朝玩家。

飞行平面固定在 **y = 7 m**（对应 `airfield.scad` 的 SCAD z=7），地面 y = 0。所有空战判定都在
y=7 平面上做 2D；地面目标用它们的 XZ 包围盒，玩家弹对空对地通吃。

### 5.2 取景与播放列

| 量 | 值 | 来源 |
| --- | --- | --- |
| 窗口 | 1600×900（可缩放，布局按 `UI.GetScreenSize()` 比例） | — |
| 相机竖直 FOV | 50°（`glm::perspective` 的 fov 是竖直角，横向随 aspect 展开） | 引擎 |
| 相机高度 H | **75 m** —— 整套尺度的唯一旋钮，见下 | 实测 |
| 俯角 | 15°（eye = target + (0, H, +H·tan15°)），给低多边形建筑留立体感 | 美术 |
| 播放列 | **42 m 宽 × 69 m 高**（飞行平面），即 **7.6 个机身宽** | 实测 |
| HUD 立柱 | 左右各占窗口宽 ~30%，**不透明** | §8 |
| 景观带宽度 | **5 个地块 = 80 m**（P0 定死，理由见下） | 实测 |

玩家夹在 `|x| ≤ 21`、`z ∈ [camZ − 16, camZ + 24]`。敌人可以在播放列外出现并飞入，屏幕上
表现为"从立柱后面钻出来"。

> **这几个数是量出来的，不是估的**（`FScrollCamera::EdgeZ` 同一套算法）：在上表的
> 高度/俯角/FOV 下，**飞行平面**（y=7）的可见范围是 `camZ − 37.0 … camZ + 32.1`，
> **地面平面**（y=0）是 `camZ − 42.8 … camZ + 33.3`。后缘必须给机体自身的 2.6 m 长度留出
> 余量，否则机尾会挂在屏幕外。碎片回收用**地面**那条线，玩家夹取用**飞行平面**那条线，
> 两者差着近 1 m，用混了就是"东西在眼前凭空消失"。
>
> ### 尺度是一根旋钮，拧它要动一整串数
>
> 竖版射击手感的硬指标是**播放列有几个机身宽**：原版雷电约 7 个，早期这里只有 4.7 个
> （相机 47 m、`halfWidth` 13），玩起来就是"单位太大、走位没地方"。把相机拉到 75 m
> （k = 1.6）之后是 7.6 个。
>
> 但相机高度不是一个孤立的显示参数——它定义了"一块屏幕装下多少米世界"，于是**所有
> 以米/秒和米为单位的量都必须同步乘 k**，否则屏幕上的观感全部走样：
>
> | 乘 k | 不乘 k |
> | --- | --- |
> | 滚动速度、敌我弹速、敌机速度、BOSS 横移幅度与停靠深度 | 机体/弹体/地面目标的**尺寸与判定半径**——变大的是世界，不是模型 |
> | 播放列边界、出生偏移、僚机站位 | 地块的 16 m 节距——资产固定尺寸，靠**加行数**覆盖更长的关卡 |
> | 道具吸取/拾取半径、碎片回收余量 | 固定步长、重力 |
> | `.scad` 里地面目标的 y 坐标（否则和波次表错位） | 玩家速度——乘 1.9，因为等比例只能保住原来的手感 |
>
> 弹体与道具的观感缩放只跟到 1.4：跟满 1.6 会让子弹比机体还抢眼。碎片初速只跟到 1.3，
> 因为重力没变，跟满会把碎块抛得过高。
>
> **跑道带也得跟着变宽。** 45 m 的播放列里铺一条 16 m 的跑道，画面会变成"草地里的一条路"，
> 机场读不出来；`nr_row_runway` 因此改成中间三列都是跑道。这是尺度变化的**内容**后果，
> 只改数值不改内容会得到一个空旷的关卡。
>
> **景观带为什么是 80 m**：整屏在远端能看到约 ±46 m 宽（未加 HUD 时截图里那两个暗角就是
> 80 m 的带子铺不满留下的），但立柱只露出中间那一列——默认 30% 立柱下，带子其实只需要覆盖
> 到 **±18.4 m**，3 个地块（±24 m）就够。取 5 个地块是给"立柱收窄"留的余量：
> `hudColumnFraction` 降到 0.20，需要的覆盖就涨到 ±27.7 m，3 个地块当场露白。
> 多出的两列成本很低（整关烘焙 36 ms 量级），这个余量买得起。

### 5.3 料架与对象池（本项目的技术支点）

`.scad` 的**料架段**（`nr_rack()`，见 §6）每种动态物体只放**一份**，停在 SCAD z = −500 的地下。
引擎解析完 `.scad` 才调 `BeforeSceneRebuild(nodes, models, materials, lights, tracks)`，游戏在里面：

1. 遍历 `nodes` 找到料架里每个模块名的根节点，记下它的子树；
2. 对每种预制件深拷贝 K 份追加进同一个 `nodes`（`Assets::Node::CreateNode` +
   复制源节点 `RenderComponent` 的 `modelId` / 16 槽 materials / visible，`instanceId` 取
   `nodes.size()`），**几何零复制**；
3. 全部设为不可见、停在地下，并**整棵子树 `SetRayCastVisibleRecursive(false)`**（见下方警告）；
4. `NextRig::DeclarePool(rigPath, capacity, models, materials)` 声明 BOSS/中BOSS 的 rig 池。

运行时只有 transform 与可见性改动，没有节点增删，也就没有 GPU buffer 重建的抖动。

**小敌机用 flatten 版**：`nr_stage_common.scad` 里 `module nr_lod_scout() { gk_flatten() nr_veh_scout(); }`
把 13 个 chunk 折成 1 个 Node/Model——12 个 scout 实例就是 12 个节点而不是 156 个。
`scout` / `interceptor` 用 flatten 版；**`bomber` 及以上保留 chunk 结构**，因为它们要在被击坠时
把真实部件摘下来变成物理碎片（§7.4），而且数量少（≤4 个实例）撑得起节点预算。

估算节点预算：景观行 ~90 + 地面目标 ~40×6 + 空中敌 ~140 + 弹体 448 + 道具 32 + FX ~50 +
碎片预制件池 ~64 + rig BOSS ~80 ≈ **1 150 节点**，与 MagicaLego / Brotato3D 同量级。

> **两条会静默出错的规则，池子建完前必须落实：**
>
> 1. 隐藏一个 kit 对象**必须**用 `Assets::NodeUtils::SetVisibleRecursive`。SCAD 的材质包装本身也是
>    module，索引到的节点常常自己不画任何东西，只改根节点会静默无效。
> 2. 池节点**必须** `SetRayCastVisibleRecursive(false)`。`Scene::EnsureNodePhysicsBody` 会给
>    "有 RenderComponent 且 rayCastVisible" 的节点自动挂一个**静态** mesh body；子弹、敌机、道具
>    要是带上它，就会在停放位置留下一堆永不移动的碰撞体。景观带与地面目标**保持 true**——
>    它们的静态碰撞正是碎片要落上去的地面。

## 6. 关卡数据契约

### 6.1 `.scad` 负责的三段

`assets/scad/source/next_raiden/stage{1,2}_*.scad` 结构固定为三段，共用
`nr_stage_common.scad` 的组合子：

```scad
use <../../lib/kit_raiden.scad>
use <nr_stage_common.scad>
// 1) 景观带：每行一次调用，行内 gk_flatten 折成单节点
nr_band_runway(y0 = 0,   rows = 6);
nr_band_grass (y0 = 96,  rows = 4, props = 1);
// 2) 地面/海面目标：不 flatten，保留 chunk 以支持分级损毁
translate([-6, 120, 0]) nr_veh_tank();
translate([ 7, 148, 0]) nr_prop_turret();
// 3) 料架：每种动态物体一份，停在地下
nr_rack(scouts = 1, interceptors = 1, bombers = 1, bolts = 1, orbs = 1, …);
gk_camera_lookat([0, -12, 47], [0, 34, 7], name = "01_Gameplay_framing", fov = 50);
```

- 景观行与地面目标的 **SCAD y 坐标就是关卡进度**，游戏按 y 排序建索引，进入播放列才激活。
- 地面目标的玩法参数（HP 档、开火间隔、是否静默）写成 module 具名参数，ScadLoader 会写进
  `Node::metadata`，运行时用 `Assets::Scad::ParseScadMetadata` 读回。**不另建一份地面目标 JSON。**
- 每关带一个与游戏内取景一致的 `gk_camera_lookat`，这样 `gnb shot --scene <stage>` 看到的就是玩家
  会看到的画面。

### 6.2 JSON 负责的时间线

`assets/configs/nextraiden/`：

| 文件 | 内容 |
| --- | --- |
| `gameplay.json` | 玩家、武器、相机、屏幕、道具、计分等全局数值 |
| `stages.json` | 关卡表：id / 显示名 / `.scad` 路径 / 基础滚动速度 / 波次文件 / BOSS 配置 |
| `stage1_waves.json`、`stage2_waves.json` | 波次时间线 |

波次条目（严格解析，未知字段报错并跳过该条）：

```json
{ "t": 12.0, "kind": "scout", "count": 5, "formation": "vee",
  "x": -6.0, "path": "dive", "speed": 14.0,
  "fire": { "pattern": "aimed", "interval": 1.1, "bullet": "bolt", "speed": 22.0 },
  "drop": "power" }
```

`formation`：`line` / `vee` / `column` / `arc`；`path`：`straight` / `sine` / `arc_in_out` /
`dive` / `hover`（停在指定 z 开火数秒后撤离）/ `strafe`。

滚动速度可被"段"覆盖（BOSS 前减速到 0），写在 `stages.json` 的 `segments` 里。

## 7. 战斗系统

### 7.1 弹幕 pattern（六种，纯函数，可单测）

| pattern | 参数 | 用在哪 |
| --- | --- | --- |
| `aimed` | 朝玩家当前位置，可加提前量 | scout、tank、turret |
| `spread` | `count`、`arcDeg`，以朝向为中心扇形 | interceptor、patrolboat、BOSS 阶段 1 |
| `ring` | `count`，整圈等分，可带 `spinDeg` 每次偏转 | bomber、rotorcraft、fortress 阶段 2 |
| `spiral` | `armCount`、`rateDegPerSec`，连续旋转发射 | 关卡 2 后半、BOSS 濒死阶段 |
| `stream` | `burst`、`gapSec`，短点射 | 机炮类，配合 `aimed` 使用 |
| `sweep` | `nr_wpn_laser` 拉长的扫射梁，按角速度扫过播放列 | fortress 阶段 3 |

弹体外观与 pattern 解耦：`bolt`（敌用红）/ `orb`（大慢球）/ `needle`（快针）/ `missile`（低速追踪，
转向速率有上限）/ `bomb`（抛物线落地炸）。

> **弹体必须放大画，判定半径不跟着放大。** kit 按真实尺寸画弹药，而 47 m 俯视下一发 20 cm 炮弹
> 只有三个像素的深灰——压在跑道上根本看不见，在弹幕游戏里这不是难度，是不讲理。
> `gameplay.json` 的 `bullets.scale` 按弹种给观感缩放（bolt 2.6×、orb 2.0×、needle 2.4×），
> 判定半径是独立参数，**放大观感不会偷偷放大命中体积**。这条是 P1 截图里才暴露出来的。

### 7.2 空中敌

| 敌种 | 模块 | HP | 典型行为 |
| --- | --- | --- | --- |
| scout | `nr_lod_scout` | 12 | 直线/俯冲，`aimed` 单发 |
| interceptor | `nr_lod_interceptor` | 28 | 弧线进场 + 正弦横移，`spread3` |
| bomber | `nr_pf_bomber`（保留 chunk） | 90 | 悬停/横越，投 `bomb` + `ring10` orb |
| rotorcraft | rig 池 | 900 | 关卡 1 中BOSS（§7.5） |
| gunship | rig 池 | 2 200 / 1 600 | 关卡 1 关底 / 关卡 2 中BOSS（同一台的强化档：多一门 sweep 炮） |
| fortress | rig 池 | 4 000 | 关卡 2 关底，带反应堆弱点 |

> **敌方一律用暖色（paint 1 红）。** kit 给 bomber 的默认 paint 是青，俯视下和玩家的蓝是同一族
> 冷色，一眼分不出敌我——`nr_pf_bomber` 因此显式覆盖成红。新增敌种时照此办理。

### 7.3 地面与水面目标

从 `.scad` 索引，进入播放列 + 3 m 时激活，离开后停火。`tank` 瞄准慢速 `bolt`，
`prop_turret` 双管 `stream+aimed`，`launcher` 发 `missile`，`patrolboat` `spread3 needle`，
`bldg_bunker` / `fueldepot` 不开火但可炸（`fueldepot` 爆炸有范围伤害，是关卡 1 的送分点）。

### 7.4 损毁表现：碎块是真的物理刚体

被打下来的部件**带 dynamic body 真的掉下去**：受爆炸冲量抛出、翻滚、砸在跑道/甲板/屋顶上、
互相堆叠，随景观一起滑出屏幕下方后回收。这是这套资产最值得兑现的地方——`kit_raiden` 的每个
`nr_chunk` 本来就写着 `massKg` / `releaseStage` / `collider="convex"`，文件头也明说
"未来的应用负责摘块、保持世界位姿、交给物理"。

`Combat/DebrisSystem` 是唯一碰 `NextPhysics` 的地方。

#### 碎块从哪来（两个来源，一套生命周期）

| 来源 | 做法 | 用在 |
| --- | --- | --- |
| **摘真块**（首选） | 把**原来那个 chunk 节点**从父节点摘下：先取 `WorldTranslation/Rotation/Scale`，`ClearParent()` 后把世界位姿写回成局部位姿，再挂 body。视觉上"掉下去的就是刚才长在那儿的那块" | 地面目标、`bomber` 及以上的敌机、rig BOSS 的部件 |
| **发替身** | 从 `nr_part_*` 预制件池取一片（`armour`/`wing`/`engine`/`canopy`/`fin`/`pod`/`gun`），摆到爆点 | flatten 小敌（`scout`/`interceptor`，几何已合并，没有块可摘） |

摘真块是一次性的——竖版射击里被摧毁的目标不会再出现，所以节点不必还回池子，回收时隐藏并
停回地下即可。

#### body 契约

- **形状**：dynamic **box**（`NextPhysics::CreateBoxBody(position, rotation, extent, Dynamic)`）。
  Jolt 的 mesh shape 只能静态，而 chunk 的 `size` 本来就是个盒子；kit 文件头建议的
  "convex proxy 在包围盒中心重新对中"就是这件事。
- **半尺寸**：由 chunk 的 SCAD `size` 换轴得到 `extent = (sx, sz, sy) / 2`。
- **重心偏移**：chunk 的枢轴是**底面中心**，body 位置是**重心**，所以
  `PhysicsComponent::SetPhysicsOffset({0, sz/2, 0})`；`Node::SyncPhysics()` 会用这个偏移把
  body 位姿还原成节点位姿。漏掉它碎片会整体悬空半个身位。
- **绑定**：`Scene::BindPhysicsBody(node, bodyId, ENodeMobility::Dynamic)`。它会先把该节点
  可能存在的隐式静态 mesh body 拆掉，避免重复接触。绑定后节点位姿由 `Node::SyncPhysics()`
  从 body 写回，游戏不再碰它的 transform——**停放也要用 `SetBodyTransform`，改节点会被回写覆盖**。
- **body 生命周期**：每个池位建一次、永久复用（P1 实测方案）。池位与零件预制件一一绑死，
  盒体尺寸恒定，所以"现建现删"没有任何好处。取用 = `SetBodyTransform(resetVelocity) +
  SetBodyVelocity + SetBodyActive(true)`，回收 = 停回停放点 + `SetBodyActive(false)`。
  P2 的"摘真块"是一次性物体、尺寸各异，那条路径才需要现建现删。
- **初速**：`linear = 受害者速度 + 爆心外向 6–14 m/s + 向上 4–9 m/s`，
  `angular = 各轴 ±6 rad/s` 随机（用游戏自己的确定性随机，不用 `rand()`）。
- **落地**：靠景观带与地面目标的**隐式静态 mesh body**（scad 网格自带，见 §5.3 警告 2）。
  水面地块同样有碰撞，碎片会停在水面上——MVP 接受，后续可换成落水 FX + 立即回收。

#### 回收（"出了视口再销毁"）

每个定步检查活动碎片，满足任一条即回收（`SetBodyActive(false)` → `RemoveBody` → 节点隐藏 →
停回地下）：

1. **滑出视口下方**：`z > zRecycle`，其中 `zRecycle` 取**地面平面 y=0 与视锥下边界的交点**再加
   4 m 余量。注意它和飞行平面（y=7）的下边界不是同一条线，相机有俯角，用地面那条才不会当着
   玩家的面消失；
2. 掉出世界：`y < −5`；
3. 超龄：存活 > 12 s 且 body 已休眠（Jolt 自动 sleep，静止碎片几乎不耗，这条只防长期堆积）；
4. 预算满：回收最老的一个。

**同时最多 64 个活动碎片 body**（`nr.debrisBudget` 可调）。一次击坠给多少块：flatten 小敌 2–3，
大型敌机 4–6，地面目标每掉一档 ≤4 块，BOSS 每掉一个部件 1 块、阶段切换 4–6 块。

#### 仍然保留的分级损毁

物理碎片是**表现**，血量分档仍是原来那套：

1. **地面目标**：按 `releaseStage` 分档——掉到 2/3 HP 摘掉 `releaseStage<=1` 的外甲，1/3 HP 摘掉
   `<=2`，死亡摘掉剩下的并放 `nr_fx_crater` + `nr_fx_smoke`。`releaseStage` / `hitPoints` 是资产
   作者写在 chunk 上的元数据，直接读，不另配表。
2. **rig BOSS**：按骨骼名分组（`armour_*` → `pod_*`/`gun_*` → `engine_*` → `wing_*`），用
   `NextRig::GetBoneNodeId` 拿到节点后摘块 + 放 `nr_fx_blast`，阶段切换时播 clip。
3. **flatten 小敌**：整机隐藏 + `nr_fx_blast` + 发替身碎片。

#### 边界（别越过去）

- 碎片**不做任何玩法判定**：不挡子弹、不伤敌人、不伤玩家、不给分。它跑在引擎的物理节拍上，
  和游戏的 1/120 定步长不同步，一旦回流到玩法就会让回放和 `gnb validate` 的断言失去确定性。
- 玩法物体（敌机、弹体、道具、玩家）**永远没有 body**。物理世界里只有两类东西：景观/地面目标的
  静态碰撞，和碎片的 dynamic body。
- 流程画面与暂停用 `NextPhysics::SetPaused(true)` 冻结碎片，和玩法的暂停同一个开关。

> **ScadRig 的命名规则，写错了只有一行 warning。** 骨骼名**保留** `bone_` 前缀
> （`FScadRigLoader` 原样使用 module 名），clip 名**剥掉** `anim_` 前缀。所以
> `stages.json` 里 `shedBones` 要写 `"bone_pod_l"`，`clip` 要写 `"fire"`。
>
> **另：只被 showcase 引用的资产等于没验证过。** `next_raiden_{rotor,gunship,fortress}.scad`
> 的四条 `anim_*` 从一开始就少一个 `]`，因为静态 kit 场景不加载 rig，直到 P2 的 BOSS 去
> `DeclarePool` 才暴露。新增 rig 资产后要立刻让某个东西真的加载它。

### 7.5 BOSS 阶段脚本

**关卡 1 中BOSS —— rotorcraft**（`next_raiden_rotor.scad`，clip：`idle` / `hover` / `fire` / `bank_*`）

| 阶段 | 触发 | 行为 |
| --- | --- | --- |
| 进场 | — | 从上方降入，`hover`，无敌 2 s |
| 1 | 100–50% | 左右横移，`chin_gun` 的 `aimed+stream`，间隔 1.4 s |
| 2 | 50–0% | 加入 `ring10` orb，横移加速；击破掉 `bomb` + `option` |

**关卡 1 BOSS —— gunship**（clip：`deploy` / `fold` / `fire` / `bank_*`）

| 阶段 | 触发 | 行为 |
| --- | --- | --- |
| 进场 | — | 折翼飞入，播 `deploy` 展开，无敌至展开结束 |
| 1 | 100–65% | 双机炮 `spread5 bolt`，缓慢横移 |
| 2 | 65–30% | 加 `needle` 扇形齐射，横移提速；打掉一侧 `pod_*` 会减少一路弹 |
| 3 | 30–0% | 隐藏一侧 `engine_*`，倾斜飞行，`ring12 orb` + 冲刺；击破 = 过关 |

**关卡 2 BOSS —— fortress**（43 骨骼，clip：`idle` / `open` / `close` / `core_exposed` / `fire`）

| 阶段 | 触发 | 行为 |
| --- | --- | --- |
| 进场 | — | 从画面上方缓慢压入，`idle` 浮动，无敌 3 s |
| 1 | 100–75% | 舱门关闭，`spread7 bolt` + `stream needle`，横移；全身 ×1，硬碰硬 |
| 2 | 75–50% | 播 `open` 露出 `reactor`：**弱点窗口**，停住不动改用 `spiral5`；甩掉两侧 `pod_*` |
| 3 | 50–25% | `close` 收回弱点，横移提速 1.6 倍，`ring16` + `spread9`；甩掉 `armour_*0` |
| 4 | 25–0% | `core_exposed`——舱门机构打坏，弱点常驻，但它开始冲脸；`spiral6` + `stream5 needle` |

**弱点是一段 JSON，不是一段代码。** 一个阶段写 `weakPoint: {bone, radius, multiplier,
bodyMultiplier}` 就有弱点：命中该骨骼 `radius` 米内按 `multiplier` 倍算（要塞是 ×3），
打别处按 `bodyMultiplier` 算（第 2 阶段 0.35、第 4 阶段 0.5）。要点有三条：

- **弱点位置取自 rig 的骨骼世界变换**，不是写死的偏移量。露出来的反应堆就在资产画它的地方，
  舱门开在它上面——改资产就改了玩法，不用改代码。
- **`bodyMultiplier < 1` 才是机制。** 只加不减的话，弱点只是"打中中间有奖励"，不是"要不要瞄准"
  的分界线。倍率比（3 ÷ 0.35 ≈ 8.6）是这场仗打 25 秒还是 90 秒的分水岭。
- **弱点必须说出来。** 70 米外 15 度俯角下，舱门开没开是看不清的，所以血条下面直接写
  `CORE EXPOSED`。一个没人看得见的弱点是个数值，不是个机制。

炸弹打 BOSS 永远按 `bodyMultiplier` 算：它覆盖全屏，里面没有"瞄准"可言。这需要一个
**单独的入口**（`ApplyAreaDamage`），不能靠"把 BOSS 自己的坐标当命中点传进去"——要塞的反应堆
就长在机体中轴线上，那样传等于正中弱点，一发炸弹白拿三倍。

## 8. HUD 与流程画面

两侧不透明立柱（深色金属底 + 铆钉分隔线），中间留出播放列：

| 左柱 | 右柱 |
| --- | --- |
| SCORE / HI-SCORE | 武器等级条（1–4） |
| STAGE 名与进度条 | 僚机数（0–2） |
| 剩余命（玩家机剪影） | 炸弹库存（`nr_item_bomb` 图标） |
| medal 连击计数 | 护盾（3 格） |

BOSS 出现时在播放列顶部叠一条血条与阶段刻度。流程画面（Title / Stage Clear / Game Over /
All Clear）画在播放列中央，不覆盖立柱。

全部用 `NextUI` 的立即模式绘制，布局按 `UI.GetScreenSize()` 的**比例**算，不硬编码像素——
这样在编辑器 PIE 的 viewport 面板里也对得上。

## 9. 配置、音效与调试

- 数值全部在 `assets/configs/nextraiden/*.json`，代码里不出现魔数；解析失败有默认值兜底并
  `spdlog::warn`，不静默。
- 音效先照 `NextAstrobot` 的做法：拷一份占位音到 `assets/sounds/nextraiden/`，
  全部调用走 `RaidenAudio.hpp` 一个文件，换音是一文件改动。
- CVar：`nr.invincible`、`nr.showHitboxes`、`nr.scrollSpeed`、`nr.freeze`、`nr.stage`、`nr.power`、
  `nr.spawnBoss`（直接跳到 BOSS，调 BOSS 时省 2 分钟）、`nr.bossHp`（把在场 BOSS 的血量
  直接设成某个比例——阶段是按剩余血量选的，所以这就是"跳到第 N 阶段"，看第四阶段不用先打完
  前三段；0 = 结束这场）、`nr.debrisBudget`（碎片上限，0 = 关掉物理碎片）、`nr.autoFire`。
  物理体的可视化直接用引擎自带的物理调试叠加，不自己画。
- Agent queries（`RegisterAgentQueries`）：进度类 `game.state` / `stage` / `stageIndex` /
  `stageCount` / `stageTime`，玩家类 `score` / `lives` / `shield` / `power` / `options` /
  `bombs` / `autoFire` / `playerX` / `playerLeadZ`，世界类 `enemiesAlive` / `bulletsAlive` /
  `hostileBullets` / `bulletsPeak` / `debrisAlive` / `itemsAlive` / `groundTotal` /
  `groundDestroyed` / `sceneryRows` / `prefabs`，BOSS 类 `bossActive` / `bossVulnerable` /
  `bossHp` / `bossHpFraction` / `bossPhase` / `bossWeakPoint` / `bossesFought`。
  `gnb validate` 的断言全建在这些上面。

  三个容易写错的地方：**`bulletsAlive` 不能用来断言"屏幕干净"**——自动射击让玩家的弹流永不为零，
  该用 `hostileBullets`；**取景类断言用 `groundNearLead` / `groundFarLead`**（相对相机中心），
  绝对 Z 一改尺度就失效；**BOSS 断言前先等 `bossVulnerable`**，进场那两三秒它打不动也没有弱点。

## 10. 验证

| 手段 | 用途 |
| --- | --- |
| `gnb shot --target NextRaiden --scene <关卡.scad> --frames 750` | 关卡取景、配色与 HUD |
| `gnb validate --script nextraiden-smoke` | 空间契约：滚动方向、屏幕左右、播放列夹取 |
| `gnb validate --script nextraiden-combat` | 开火/拾取/地面目标/掉块 |
| `gnb validate --script nextraiden-boss` | 关卡 1 两个 BOSS 的阶段推进 |
| `gnb validate --script nextraiden-stage2` | 两关串联 → All Clear，要塞四阶段与弱点开合 |
| `gkNextUnitTests "[NextRaiden]"` | pattern 数学、波次解析、StageFlow 状态机、弱点倍率、关卡表结构不变量 |

> **截图的帧数不能太少。** 出生无敌 1.5 s 期间机体以 0.24 s 周期闪烁，`--frames 240`
> 在这台机器上只有 0.65 s，有一半机会拍到"灭"的那半个周期——画面里就没有玩家机。
> 拍关卡取景一律 `--frames 750` 以上。

确定性：固定步长 + 自带 xorshift 随机（同种子同序列），`gnb validate` 的断言才稳定。
碎片物理**不在确定性范围内**（§7.4 边界），所以断言不许写 `game.debrisAlive` 的具体数值，
只许写"存在/归零"这种单调性质——比如通关后 `game.debrisAlive == 0`（验证回收没漏）。

物理相关的两个必测项：
- **静态碰撞预算**：关卡加载后看日志的 `shape cooking` 耗时与 `FNextPhysicsBodyStats`，
  确认整条景观带的静态 body 数与烘焙时间在可接受范围（对策见计划的风险表）。
- **碎片不泄漏**：打空一关后 `game.debrisAlive == 0` 且 body 总数回到关卡初始值。

## 11. MVP 明确不做

- 续关、双打、难度选择、排行榜存档
- 武器颜色切换（红/蓝/紫三系）与充能武器
- 可破坏建筑的形变、碎片二次伤害、碎片落水的水花与下沉
- 碎片参与玩法（挡弹、砸伤）——见 §7.4 边界，这是刻意不做
- 敌机 AI 的动态难度调节（rank）
- 关卡 3 及以后、Boss Rush、隐藏要素
- 移动端触控布局（`MobileApplications.json` 暂不登记）

这些都不影响上面的契约，后续加在同一套 pattern / wave / phase 数据上即可。

## 相关文档

- [开发计划](nextraiden-mvp-plan.md)
- [SCAD Loader](../../AGENT_GUIDE/SCADLoader.md) —— 节点/模型装配语义、`gk_flatten`、metadata
- [ScadRig](../../AGENT_GUIDE/ScadRig.md) —— BOSS 骨骼与 clip
- [NextAstrobot](../../AGENT_GUIDE/NextAstrobot.md) —— "`.scad` 就是关卡"的先例与 LevelIndex 写法
- [Brotato3D](../../AGENT_GUIDE/Brotato3D.md) —— 建场景期对象池与 god-class + 系统拆分的先例
