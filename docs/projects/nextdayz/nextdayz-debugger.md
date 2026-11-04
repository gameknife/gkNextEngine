---
title: "NextDayz 游戏内调试器"
category: project
status: 现行
owner: NextDayz
created: 2026-11-04
last_updated: 2026-11-04
---

# NextDayz 游戏内调试器

NextDayz 的各个系统（玩家 3C、生存、背包、武器、感染者 AI、刷新导演、物资刷新、时间天气、噪声、战斗）都能在游戏运行时被**看见、追溯和操控**。调试器在 `src/Application/Game/NextDayz/Debugger/`，由 `NextDayzGameInstance` 持有，规则代码从不读取它——只有游戏向它要的几个旋钮（时间缩放、无敌、冻结需求、环绕相机）。

它取代了原来 HUD 里只读的 `FDebugHudState` 面板和 `DrawDebugWorldOverlay`；原有的 F5–F8、`nextdayz.debug.*` cvar 和 `game.*` 查询全部保留语义。

## 1. 快捷键

| 键 | 作用 |
| --- | --- |
| `F5` | 打开/关闭调试器（窗口 + 世界叠加层） |
| `F6` / `F7` / `F8` | 切换图层：感染者 AI / 物资 / 命中盒+射线 |
| `F9` | 调试光标：释放鼠标操作调试窗口；在世界里**左键点选**感染者、物资、刷新点 |
| `F10` | 冻结/恢复模拟；`Shift+F10` 冻结并单步一帧（1/60 s） |
| `PgUp` / `PgDn` | 切换标签页 |

不开 F9 时窗口是只读的（不拦截鼠标、不影响射击）；背包、地图、暂停或死亡释放鼠标时窗口自动可交互。冻结时相机仍可转动，便于检查某一帧。

## 2. 窗口

顶栏：帧率、游戏 tick 耗时、模拟时间、存活时间、游戏时钟；光标、冻结、单步、整体时间缩放（0–4×）、`Snapshot`；当前选中对象；自动诊断出的告警（见 §5）。

| 标签 | 看什么 | 能做什么 |
| --- | --- | --- |
| Overview | 每个系统一行状态；每系统 CPU 耗时（last/avg/peak）；60 s 曲线（帧耗时、tick、感染者活跃/警觉、寻路次数、可拾取物资） | 图层开关、标记/文字距离 |
| Player | 相机、移动（姿态/步态/跳跃/翻越探测）、动画层（基础 clip、瞄准、身体扭转、倾斜、握把误差、武器动作层） | 环绕相机、第一/三人称、无敌、冻结需求、传送到准星/POI |
| Survival | 生命/饥饿/口渴/寒冷及**每分钟变化率**、流血、虚弱、止痛、护甲保暖、最后伤害来源；60 s 曲线 | 直接设定数值和伤口数、恢复、饥饿、口渴、受伤、死亡、重开 |
| Inventory | 所有物品实例（容器、数量、已装弹、穿戴）、容器容量、工具 | 使用/丢弃、按 id 给物品（可过滤）、常用套装 |
| Weapons | 槽位、弹药、动作状态、当前武器定义（射速、散布、伤害衰减、噪声、冲击）、最近射线（命中节点名） | 补弹、一键装备任意武器 |
| Infected | 活跃/上限、按状态计数、视野倍率、本帧寻路/延后次数；按距离排序的表；选中个体的全部运行时（感知、路线、攻击、硬直、变体、rig clip、命中盒数、来源刷新点） | AI 时间缩放/冻结、全体惊醒、按 profile 在前方/准星处生成、全杀、全部回收；对选中个体：击杀、回收、环绕相机、传送过去 |
| Spawns | 刷新导演统计、窗口/回收/重生参数、**每个刷新点上次评估的结论**（见 §4） | 显示刷新点和激活圈、右键传送 |
| Loot | 按类别的可用/总数/冷却/上限/冷却时长与稀缺倍率、关键物资覆盖、按距离排序（可按类别/状态过滤）；选中条目内容与**补货受阻原因** | 立即补货全部/单个、传送过去、区域圈 |
| World | 时钟、昼夜/风暴/寒冷、提灯、补给箱、鞭炮；锚点计数和 POI 表；导航网格 | 拨时间/时速/跳时、天气、POI 传送、显示导航格、**从脚下到准星试算一条感染者路线** |
| Combat | 击杀/爆头/硬直、部位倍率与韧性参数、特效/音效计数、存活噪声、伤害流 | FX 时间缩放、命中盒/射线/噪声图层 |
| Log | 事件日志（见 §3）：按通道筛选、文字过滤、跟随、复制 | 悬停在有位置的事件上，世界里出现标记；双击传送过去 |

## 3. 事件日志

`DebugEventLog` 是固定 2048 条的环形缓冲，**始终在记录**（关着窗口也记），所以发现异常后再按 F5 也能看到它是怎么发生的。相同的连续事件折叠成一行并计数（例如 `Fired ak ×30`）。

事件大多由调试器在每帧末**比对系统状态**得到，不侵入规则代码：

- Infected：状态迁移及原因（看见你 / 听到噪声 / 韧性被打破 / 找不到路线）
- Spawn：生成（来自哪个刷新点、距离）、回收、尸体清理
- Loot：预留、取消、被拿走（何时补货）、补货、掉落堆
- Noise：每次噪声及其半径内的感染者数量
- Survival：伤口、虚弱、死亡原因；World：整点、天气变化；Player：翻越/攀爬；Weapon：换弹、切枪、持有武器

少数由游戏直接上报：伤害事件（部位、伤害、冲击、击杀/硬直/致残）、玩家被击中（是否开放伤口、无敌吸收）、开火、空仓、HUD 提示（`Hud` 通道）。

## 4. "为什么没有发生"的诊断字段

调试器回答最常见的迭代问题，需要系统自己记下判断结果。这些字段只写不读，不影响规则，均有单元测试（`[NextDayz][Debug]`）：

- `FZombieSpawnPoint::verdict`：`Populated / Cooldown / TooClose / TooFar / Visible / NotNavigable / CapReached / Spawned / Recycled`，以及 `playerDistance`。上限已满时一次评估在距离检查之前就结束，所以空闲点显示 `CapReached`。
- `FLootSlot::block`：冷却中的物资为什么还没补货——`Watched`（玩家在看）、`PlayerNear`（没离开区域）、`Cooling`（类别冷却未到）、`Offscreen`（离开视线的时间不够）、`CategoryFull`（类别库存已达上限）。`LootDirector::ReadyAt` 给出按稀缺倍率缩放后的就绪时间。
- `FZombieRuntime::seesPlayer / heardNoise / pathSolves / pathFailed`，`ZombieSystem::PathSolvesLastUpdate / PathDeferredLastUpdate`：感知结果和寻路预算消耗。
- `LootSystem::DebugRestock / DebugRestockAll`：只给调试器用的强制补货。

## 5. 自动告警

每 0.5 s 刷新一次，显示在窗口顶部，也写入快照：导航网格未构建；没有刷新点/物资点/水井；活跃感染者没有命中盒；感染者卡住（>1 s 无进展）、追猎时找不到路线、站在不可走格子上；寻路预算连续 30 次更新饱和；某类物资全图无货；游戏 tick 平均超过 8 ms；玩家掉出世界。

## 6. 世界叠加层

所有图层都用本帧渲染相机投影后画在 ImGui 背景层：线段在近平面裁剪，因此绕着玩家的大圈、相机身后的路径都能正确显示；不做深度测试，穿墙可见。这样它在任何渲染器、开启任何上采样器、以及带 UI 的截图里都一致。（引擎 aux line 在 NextDayz 的默认渲染配置下不可见，所以没有使用。）

| 图层 id | 内容 |
| --- | --- |
| `infected` | 状态色立柱、朝向箭头、剩余路线和路点、最后已知位置、漫游目标、攻击范围；近处带详细标签 |
| `perception` | 视野扇形（含夜晚/蹲伏/雾/提灯倍率），看见玩家时变红并连线，听到噪声时连向噪声点；有选中时只画选中个体 |
| `hitboxes` | 跟随骨架的胶囊命中盒（按部位着色），缺命中盒时标红 |
| `traces` | 最近 4 s 的射线：绿=感染者，红=世界（带节点名），黄=未命中 |
| `noise` | 存活噪声的半径圈、类型、强度、剩余时间 |
| `spawns` | 刷新点（按结论着色、成员连线、冷却）和玩家周围的最小/最大/回收距离圈 |
| `loot` | 物资标记，近处标签含类别、状态、补货受阻原因 |
| `zones` | 物资区域圈及"离开多远才会补货"的外圈 |
| `nav` | 玩家附近的感染者导航格（可走/不可走）和试算路线 |
| `anchors` | POI、水井、安全出生点、补给箱、鞭炮 |
| `player` | 速度箭头、拾取范围、准星命中点和节点名、悬停的物资 |

默认开启 `infected, perception, hitboxes, traces, noise, loot`。选中对象有脉动圈和竖直光柱；在表格或日志里悬停一行，世界里出现品红色标记。

## 7. cvar 与 agent 查询

调试器的状态都能从脚本驱动，`gnb validate` / `gnb shot` 可以直接截某个标签或图层：

| cvar | 说明 |
| --- | --- |
| `nextdayz.debug.panel` | 显示调试器（同 F5） |
| `nextdayz.debug.tab` | `overview` … `log` |
| `nextdayz.debug.layers` | `infected,noise` 替换；`+nav` / `-loot` 增减；`all` / `none` |
| `nextdayz.debug.select` | `nearest` / `z<槽位>` / `l<物资索引>` / `s<刷新点>` / `none` |
| `nextdayz.debug.timeScale` / `.freeze` / `.step` | 整体时间缩放、冻结、冻结并前进 N 帧 |
| `nextdayz.debug.god` / `.freezeNeeds` | 无敌、停止代谢 |
| `nextdayz.debug.dump` | 写快照（见 §8） |
| 既有 | `.orbit*`、`.zombieTimeScale`、`.fxTimeScale`、`.hour`、`.weather`、`.fog*`，现在与窗口里是同一份值 |

新增查询：`game.debugLayers / debugTab / debugCursor / debugFrozen / debugFrame / debugEvents / debugLastEvent / debugWarnings / debugWarning / debugSelected / debugSnapshot / debugTickMs / spawnVerdict / pathSolves`。原有 `game.debugVisible / debugZombieOverlay / debugLootOverlay / debugHitProxyOverlay / recentWeaponTraces / lastTraceResult / lastTraceNode` 保持不变；`zombieState` 等"焦点感染者"查询现在优先取调试器选中的个体。

## 8. 快照（给 agent 用）

`Snapshot` 按钮或 `nextdayz.debug.dump true` 把所有系统写成一个 JSON：帧、告警、各系统耗时、玩家、生存（含每分钟变化率）、背包、武器、每个活跃感染者（状态、感知、路线、寻路次数、来源刷新点、命中盒数）、刷新点及其结论计数、物资按类别统计和每个冷却条目的受阻原因、时钟天气、存活噪声、最近 200 条事件。

路径是可写运行目录下的 `debug/nextdayz_snapshot.json`（Windows 开发机上通常是 `%APPDATA%\gkNext\NextDayz\debug\`）；`game.debugSnapshot` 返回绝对路径。排查 AI、刷新或平衡问题时，agent 先 dump 再读 JSON，比逐个查询 `game.*` 快得多。

## 9. 验证

- 单元测试：`gkNextUnitTests "[NextDayz][Debug]"`（刷新结论、补货受阻原因、感知与寻路预算、事件日志与历史序列）。
- 端到端：`gnb validate --script assets/agentscripts/nextdayz-debug-visualization.agentscript.json`——开 F5、开火、选中最近感染者并环绕查看、冻结与单步、切换 Spawns/Loot/Log 标签、写快照，并在每一步截图。

## 10. 扩展约定

- 新系统要被看见：在 `FSystems` 加指针，在合适的标签里加一节；需要"为什么"时在系统里加只写的诊断字段并补单元测试，不要在调试器里复刻规则。
- 新事件优先通过 `DiffSystems()` 比对状态得到；只有状态里看不到的瞬时事件才在游戏里调用 `debugger_.Log(...)`。
- 需要改动多个系统或场景的操作走 `FCommands`，由游戏实例执行（例如生成感染者会同步视觉，击杀走正常伤害流程）。
- 新的子系统调用用 `ScopedTimer` 包进 `SimulateFrame` 对应的 `ETimer` 段。
