# Brotato3D 代码结构梳理

本文梳理 `src/Application/Game/Brotato3D/` 目录下 Brotato3D 小游戏的**代码结构与工程模式**，目标是让你在动手改 **C++** 之前先建立整体心智模型：知道每个文件负责什么、一帧数据怎么流、对象怎么复用、约定有哪些。

> **分工**：本文讲"代码怎么组织"。如果你只想**调数值 / 加敌人武器物品 / 改波次**（多数情况只改 JSON），请读 [`docs/projects/brotato-3d/developer-guide.md`](../projects/brotato-3d/developer-guide.md)（配置/玩法向）。两篇互补。

---

## 1. 一句话架构

Brotato3D 是一个 **C++ 原生子应用**（不是脚本游戏），核心是一个"上帝类" `Brotato3DGameInstance`，它继承引擎的 `NextGameInstanceBase`，把所有运行时状态作为成员持有；**实现按功能域拆分到多个 `*System.cpp` 翻译单元**里，但它们都是同一个类的成员函数。

```
Brotato3DGameInstance  (类声明集中在 Brotato3DGameInstance.hpp)
   │  成员函数实现分散在：
   ├── Brotato3DGameInstance.cpp     入口、OnTick 主循环、相机、Best Record
   ├── Brotato3DGameFlowSystem.cpp   状态切换（开局/暂停/结算/升级抽卡）
   ├── Brotato3DPlayerSystem.cpp     输入、加减速移动、Dash、瞄准、属性结算、ResetRuntimeState
   ├── Brotato3DProjectileSystem.cpp 武器开火（集火）、子弹、命中、爆炸、武器合并
   ├── Brotato3DEnemySystem.cpp      敌人生成/AI/尸群挤压/死亡、僵尸 rig 表现与尸体
   ├── Brotato3DGoreSystem.cpp       按血量断肢、爬行、踉跄、血泊与血块
   ├── Brotato3DCombatSystem.cpp     伤害结算、被动 Item 触发器、漂浮文字
   ├── Brotato3DDebrisSystem.cpp     物理碎块池、拾取磁吸、kinematic body 池、竞技场墙体
   ├── Brotato3DEffectSystem.cpp     场景重建(!)、灯光、屏幕震动、撤离巴士、天空过渡
   ├── Brotato3DShopSystem.cpp       商店购买、属性卡应用、被动加成
   └── Brotato3DValidation.cpp       brotato3d.validation.* cvar 与 game.* agent 查询
```

**为什么这么拆？** 把 130+ 个方法塞进一个 `.cpp` 会让编译变慢、阅读困难；按"游戏子系统"切分翻译单元是这套代码的核心组织手法。**所有 `*System.cpp` 第一行都是 `#include "Brotato3DGameInstance.hpp"`**，因为它们实现的都是这同一个类。

独立于上帝类、可单独复用/测试的有四块：
- `FPlayerRigVisual`（[Brotato3DPlayerRigVisual.hpp](../../src/Application/Game/Brotato3D/Brotato3DPlayerRigVisual.hpp)）—— 主角 ScadRig 的分层动画图与手持武器，只吃 `FPlayerRigInput`。
- `FZombieRigResource` / `FZombieRigVisual`（[Brotato3DZombieVisual.hpp](../../src/Application/Game/Brotato3D/Brotato3DZombieVisual.hpp)）—— 一种僵尸的 rig 资源、一只僵尸的身体（步态混合、动作、死亡、flinch、断肢弹道）。
- `FWaveSystem`（[Brotato3DWaveSystem.hpp](../../src/Application/Game/Brotato3D/Brotato3DWaveSystem.hpp)）—— 波次/黄昏状态机，纯逻辑、无渲染依赖。
- `FShop`（[Brotato3DShop.hpp](../../src/Application/Game/Brotato3D/Brotato3DShop.hpp)）—— 商店抽卡，纯逻辑。

> **架构启示**：要加一个新子系统（例如"天气系统"），优先开一个新的 `Brotato3DXxxSystem.cpp`，在 `Brotato3DGameInstance.hpp` 里加成员/方法声明，在 `OnTick` 里插入调用——而不是往现有大文件里堆。能做成无渲染依赖的纯逻辑类（像 `FWaveSystem`）就更好，可单测。

---

## 2. 文件地图

| 文件 | 角色 | 关键内容 |
| --- | --- | --- |
| `Brotato3DGameInstance.hpp` | **类总声明** | `EAppState`、所有成员变量与方法声明、`FEnemyVisualResource`/`FTempLightRuntime` 等内嵌结构 |
| `Brotato3DGameInstance.cpp` | 入口 + 主循环 | `CreateGameInstance` 工厂、`OnInit/OnTick/OnDestroy`、`OnRenderUI`、相机平滑、Best Record 读写 |
| `Brotato3DCommon.hpp` | 共享常量/工具 | 配置文件路径常量、`ClampToArena`/`DistanceXZ`/`RotateY`/`HiddenPosition` 等 inline 工具 |
| `Brotato3DDataLoader.{hpp,cpp}` | 配置加载 | 所有 `FXxxDef` **只读原型结构** + `LoadXxx(json)` 函数 |
| `Brotato3DPlayer.hpp` | 玩家数据 | `FPlayerStats`（属性）、`FPlayerRuntime`（运行时状态） |
| `Brotato3DEnemy.hpp` | 敌人数据 | `FEnemyRuntime`（含 `visual` 僵尸身体、`crawling`、`limbStage`、尸体计时）、`ELanceState` |
| `Brotato3DZombieVisual.{hpp,cpp}` | 僵尸表现 | `FZombieRigResource`（加载、借 walker 的动作、按部件注入色调材质）、`FZombieRigVisual`（动画层、断肢） |
| `Brotato3DPlayerRigVisual.{hpp,cpp}` | 主角表现 | `FPlayerRigVisual`：移动混合空间、持枪层、后坐力、冲刺、倾斜；武器挂 `bone_weapon_socket` |
| `Brotato3DGoreSystem.cpp` | 断肢与血 | `ReactEnemyToDamage`、`SeverEnemyLimb`、血泊贴花环形池 |
| `Brotato3DValidation.cpp` | 自动化验证 | `ConfigureCVars` / `RegisterAgentQueries` |
| `Brotato3DProjectile.hpp` | 投射物/特效数据 | `FProjectileRuntime`、`FEnemyProjectileRuntime`、`FExpandingRing`、`FLaserBeam`、`FGroundIndicator` |
| `Brotato3DWeapon.hpp` | 武器数据 | `FWeaponRuntime`（含 `tieredDef` 副本） |
| `Brotato3DDebris.hpp` | 碎块数据 | `FDebrisRuntime`、`EDebrisKind/EPickupState/EDebrisPayload` |
| `assets/scad/source/brotato3d/*.scad` | **固定竞技场** | 基于 `kit_deadly.scad` 的城镇、公路郊外、沙漠荒野场景 + `kit_deadly_wasteland.scad` 杂物层，可直接人工修缮 |
| `assets/scad/characters/brotato_*.scad` | **角色资产** | `brotato_survivor`（主角）、`brotato_zombie_<rig>`（9 种僵尸，部件来自 `lib/kit_bzombie.scad`）、`brotato_weapons`（武器目录）、`brotato_vehicles`（撤离巴士） |
| `Brotato3DWaveSystem.{hpp,cpp}` | 波次状态机 | `FWaveSystem`：Active/DuskSurge/Intermission，事件用 `Consume*()` 拉取 |
| `Brotato3DShop.{hpp,cpp}` | 商店抽卡 | `FShop`：按权重抽属性卡/被动/武器 |
| `Brotato3DUI.{hpp,cpp}` | ImGui 界面 | 主菜单、HUD、升级/商店/暂停/结算/设置面板 + 本地化辅助 |
| `Brotato3DAudio.hpp` | **音频唯一入口** | 全部 `PlayXxxSfx`/`StartBgm` inline 函数；全局 `SfxVolume`/`MasterDifficulty` 等 |
| `Brotato3DAssetPaths.hpp` | 资源路径解析 | `Assets::Sfx/Icon` + `PlaceholderAssets::Resolve`（运行时根/仓库根双 fallback） |

> 历史上还有 `Brotato3DEnemy.cpp / Player.cpp / Projectile.cpp / Weapon.cpp` 四个**只 include 头文件、无任何实现**的空壳文件，已删除（逻辑早就搬进了 `*System.cpp`）。新增数据类型时**不要**再建这种空 `.cpp`。

**构建方式**：`src/Application/Game/Brotato3D/CMakeLists.txt` 用带 `CONFIGURE_DEPENDS` 的 `GLOB_RECURSE` 收集本目录源码，并在同一文件定义 target 与 `DEV_MODE=1`。普通增量 `./gnb.sh build Brotato3D` 会让 CMake 在 glob 变化时自动重扫；只有改 CMake/preset 或生成器没有重扫时才显式加 `--reconfigure`。

---

## 3. 数据模型：Def（原型）vs Runtime（实例）

这是整套代码最重要的一条心智线——**两类结构体严格分开**：

| | `FXxxDef`（原型 / 配置） | `FXxxRuntime`（实例 / 运行时） |
| --- | --- | --- |
| 来源 | JSON 加载，启动后**只读** | 游戏过程中**每帧可变** |
| 数量 | 每种类型一份（`std::map<id, Def>`） | 每个活动实体一份（对象池里复用） |
| 例子 | `FEnemyDef`、`FWeaponDef`、`FItemDef` | `FEnemyRuntime`、`FProjectileRuntime`、`FDebrisRuntime` |
| 关系 | —— | Runtime 持有 `const FXxxDef* def`（回指原型） |

典型：`FEnemyRuntime.def` 指向 `enemyDefs_` 里的某个 `FEnemyDef`，运行时只在 Runtime 上改 `currentHp/worldPos/...`，原型永不变。

**一个值得注意的例外 —— 武器分层**：`FWeaponRuntime` 不是简单回指原型，而是**自带一份 `tieredDef` 副本**（因为 Tier 2 是在 1 级数值上算出来的，不是 JSON 配置），`def` 指向自己的 `tieredDef`。所以每当 `equippedWeapons_` 这个 `vector` 重新分配（push_back 后地址可能变），必须调 **`NormalizeWeaponDefPointers()`** 把每个 `def` 重新指回自己的 `tieredDef`——这是个容易踩的坑，改武器相关逻辑时务必记得。

---

## 4. 一帧的数据流（`OnTick`）

`Brotato3DGameInstance::OnTick` 是总编排器（[Brotato3DGameInstance.cpp](../../src/Application/Game/Brotato3D/Brotato3DGameInstance.cpp)）：

```
1. SetWorldPhysicsPaused(state != Playing)        // 非游玩态冻结 Jolt
2. 非游玩态（菜单/暂停）→ 只 MarkTransformDirty 后 return
3. 实时计时（不受慢动作影响）：屏幕震动 / 受伤闪 / 天空过渡 / 相机跟随 / 武器合并 banner
4. 计算 globalTimeScale_（Boss 击杀后 0.4→1.0 缓动）→ effectiveDt = dt * timeScale
5. if Playing:
      UpdatePlayer(dt)            // 玩家用真实 dt（手感优先）
      UpdateWeapons(dt)          // ⚠ 武器开火也用真实 dt——见下方注释
      UpdateProjectiles(effectiveDt)
      UpdateEnemies(effectiveDt)
      UpdateEnemyProjectiles(effectiveDt)
      ProcessItemTriggers(effectiveDt)
      UpdateExtractionVehicle(dt)
      waveSystem_.Update(dt, spawnCallback)    // 回调里调 SpawnEnemy
      // 然后拉取波次事件：
      ConsumeDuskBegan / ConsumeExtractionCompleted / ConsumeWaveEnded
      / ConsumeIntermissionStarted / ConsumeVictory
   else if Hitstop: 倒计时结束回到 Playing
6. if Playing: UpdateDebris(effectiveDt) + UpdateCombatEffects(effectiveDt)
7. UpdateFloatingTexts(dt)
8. MarkTransformDirty()          // 通知引擎本帧 transform 变了
```

**两个刻意设计，别"优化"掉**（代码里有注释守护）：

1. **慢动作下武器仍用真实 dt**：Boss 击杀后子弹/敌人/碎块用 `effectiveDt` 爬行，但玩家可以正常开火——这是"savor the kill"的演出节奏。注释在 [Brotato3DProjectileSystem.cpp](../../src/Application/Game/Brotato3D/Brotato3DProjectileSystem.cpp) `UpdateWeapons` 上方。
2. **事件用 `Consume*()` 一次性拉取**：`FWaveSystem` 不回调主类、不持有主类指针，而是把"本波结束了""该开商店了"等事件存成 bool，主类每帧用 `std::exchange` 语义的 `ConsumeXxx()` 取走。这让波次逻辑保持零渲染依赖、可单测。

### 状态机

```
MainMenu → CharacterSelect → Playing ⇄ {Hitstop, Paused, LevelUpPicking, Shopping} → Result
```
`EAppState` 切换集中在 `Brotato3DGameFlowSystem.cpp`（`StartNewRun/PauseGame/EnterResult/BeginLevelUp/StartShopping`...）。几乎每个切换都会 `SetWorldPhysicsPaused()` + `ClearMovementInput()`，这是约定动作。

波次内部状态机（独立于 `EAppState`，在 `FWaveSystem` 里）：
```
Idle → Active →(时间到)→ DuskSurge →(玩家在撤离车驻留够秒数)→ Intermission(开商店) → 下一波
                  └─ Boss 波(bgmCue=="boss") 跳过 DuskSurge，杀光即 Victory
```

---

## 5. 关键工程模式

### 5.1 对象池（贯穿全代码）

为避免运行时频繁创建/销毁场景节点和物理体，**所有高频实体都用预分配池 + active 标志复用**：

| 池 | 容量 | 建池位置 | 复用方式 |
| --- | --- | --- | --- |
| 子弹 `projectilePool_` | 每种武器 128 | `BeforeSceneRebuild` | 按 `weaponId` 找第一个 `!active` 的槽 |
| 敌方子弹 `enemyProjectilePool_` | 128 | 同上 | 找第一个 `!active` |
| 碎块 `debrisPool_` | Tiny 800/Chunk 480/Boss 80 | `BuildDebrisPool` | 找 `!active`，没有则抢占 `activatedTickId` 最旧的 |
| 临时灯光 `tempLightPool_` | 32 | `BeforeSceneRebuild` | 找 `!active`，没有则抢 `remainingMs` 最小的 |
| 敌人 `enemies_` | 动态增长 | 运行时 | `SpawnEnemy` 优先复用同 `def`、既不活着也不是尸体（`!alive && !fading`）的槽，连同它的僵尸 rig 一起 `Reset`；没有可复用槽才实例化新 rig |
| 血泊贴花 `bloodDecals_` | 192 | `BuildGorePool` | 环形缓冲，最旧的先被覆盖 |
| Kinematic 推挤体 | 每类敌人 32/Boss 4/Rat 96 | `BuildKinematicCollisionBodies` | `AcquireEnemyKinematicBody` 按占用情况分配 |

隐藏一个对象的统一手法：把节点平移到 `HiddenPosition`（`(0,-100,0)`，定义在 `Brotato3DCommon.hpp`）+ `SetVisible(false)` + 物理体 `SetBodyActive(false)`。

> **僵尸 rig 是场景节点**：换竞技场时 `OnSceneUnloaded` 会清空 `enemies_`（连同 rig），下一局重新实例化。
> rig 的 part model / 材质在 `BeforeSceneRebuild` 注入（`FZombieRigResource::Inject`），实例在运行时 `SpawnEnemy` 里按需创建。

> **改动提示**：要加新的高频视觉实体（如"弹壳"），照搬碎块池的模式（在 `BeforeSceneRebuild` 预建 N 个节点入池，运行时复用），不要在 `OnTick` 里 `CreateRenderNode`。

### 5.2 场景重建生命周期（`BeforeSceneRebuild`）

引擎在加载/重建场景前回调 `BeforeSceneRebuild(nodes, models, materials, lights, tracks)`，这是**唯一可以批量塞入程序化几何体的窗口**（实现在 `Brotato3DEffectSystem.cpp`，体量大是因为它一口气建了：灯光池（含巴士车灯）→ 主角 rig 与武器目录注入 → 撤离巴士 → 每种敌人的推挤代理盒 + 僵尸 rig 注入 → 子弹池 → 碎块池 → 血泊池 → kinematic body 池）。

切换竞技场时加载 `arenas.json` 中配置的固定 SCAD 场景并触发重建。`sceneReady_` 标志保证波次只在场景与运行时对象池准备完毕后才启动。

### 5.3 敌人移动的统一落点解析

敌人每条移动路径（普通追击 / 迫击炮走位 / 长枪冲锋待机 / 冲刺 / 击退）都要做同一串处理：**夹到竞技场内 → 推出障碍（撤离车+道具）→ 再夹一次 → 吸附到地形高度**。这串逻辑已抽成单一方法：

```cpp
// Brotato3DEffectSystem.cpp
glm::vec3 Brotato3DGameInstance::ResolveEnemyGroundedPosition(
    const Brotato3D::FEnemyRuntime& enemy, glm::vec3 candidate) const;
```

调用方只需：`enemy.worldPos = ResolveEnemyGroundedPosition(enemy, 期望位置); enemy.node->SetTranslation(enemy.worldPos);`。**新增敌人 AI 移动时请走这个 helper**，不要再手写那串 clamp/resolve/clamp/setY。

### 5.4 物理 kinematic body 的 hack（务必先读注释）

为了让玩家/敌人能"推开"满地的物理碎块，又不被引擎的 mesh 自动晋升机制坑到，`BuildKinematicCollisionBodies` 用了一个 workaround：给每个 kinematic body 挂一个隐藏的、带 `PhysicsComponent`（标 `Dynamic` 以跳过 mesh 晋升）的代理节点来"认领"该 body。详见该函数上方注释（含 `TODO: PhysicsComponent::IsManuallyBound()` 的引擎层修复方向）。改物理相关代码前先读那段注释。

### 5.5 灯光即材质

没有传统点光源——临时光效（开火、爆炸）是**一块发光材质的 area light**（`SpawnTempLight`）。`EnsureLightMaterial(color)` 按量化后的颜色 key 缓存材质 id，避免每次新建材质。


### 5.6 角色、僵尸与断肢（ScadRig）

角色全部是刚体骨骼 ScadRig（[ScadRig.md](ScadRig.md)），由 NextDayz 的资产与动画管线改造而来：

- **主角** `brotato_survivor.scad`：NextDayz 幸存者的 20 骨骼骨架、持枪 IK 骨骼与 `rig_humanoid` 生成的步态，
  穿 `nextdayz_body.scad` 的服装外壳（夹克是 `ROLECOLOR`，跟随所选角色颜色）+ 背包睡袋（俯视最强的识别特征）。
  `FPlayerRigVisual` 的层：locomotion（idle + run F/B/L/R，按**身体坐标系**下的实测速度混合、步频匹配）→
  hold / hold_body（ready ↔ aim）→ recoil（每发重触发的 Additive）→ action（冲刺 lunge）→ 加速度驱动的整体倾斜。
  身体朝向正在射击的目标，双腿按移动方向侧移/后退，所以"边退边打"是真的在倒退。
  武器模型来自 `brotato_weapons.scad`（`bone_wpn_<weaponId>`，与 `weapons.json` 对齐），挂在 `bone_weapon_socket`；
  第一把武器的枪口就是枪口火光的位置（`MuzzleWorldPosition`），子弹从玩家半径内出发，贴脸的僵尸也打得到。
- **移动**：速度朝输入积分（加速 70 m/s²、减速 55 m/s²），冲刺结束时直接带着跑速出来；相机朝跑动方向和射击方向前瞻。
  武器**集火**：当前目标没比最近的敌人远 2 m 以上就不换（`FocusFireSlackMeters`），身体也看着它。
- **僵尸** `brotato_zombie_<rig>.scad`：九个变体共用一套 17 骨骼骨架（与 `nextdayz_infected.scad` 同枢轴），
  只有 walker 带动作（NextDayz 感染者的步态 / 攻击 / 踉跄 / 三种死亡 + 新增的 `crawl`）；其余变体加载时按骨骼名
  借 walker 的动作（`FZombieRigResource::Load` 的 clipDonor）。外形全部来自 `kit_bzombie.scad` 的 `bz_spec(variant)`。
  `FZombieRigVisual` 的层：base（idle/walk/run 按模拟速度混合，或 crawl）→ action（攻击、远程/治疗/投掷的
  起手 alert、踉跄）→ death（按自己的时钟手动采样，停在最后一帧 = 尸体）+ 躯干 flinch 弹簧。
- **断肢**：断的是真实的骨骼子树——`Sever` 之后该骨骼不再跟随骨架，由 `UpdateLooseLimbs` 在世界空间做抛物线、
  弹跳、落地静止。kit 在每个关节里藏了伤口（`bz_part_stump`），肢体在时看不见，掉了才露出来。
  规则在 `Brotato3DGoreSystem.cpp`：每掉 1/3 血条断一肢（先手臂，`canCrawl` 的才会断腿）；断腿 → `crawling`
  （移速 ×0.42，趴地爬）；暴击击杀爆头；爆炸 / 单发过量 / Boss / 巴士碾压 → 全身解体（`BeginEnemyCorpse(gib)`）。
- **尸群**：`SeparateEnemies` 让僵尸互相挤开（按体型分质量，冲锋中的 Linebacker 质量极大），挤压量进入实测速度，
  所以被挤的僵尸是在挪步而不是滑动。尸体保留 3.6 s 后下沉消失，同屏尸体多于 70 具时缩短到 1.3 s。
- **撤离巴士** `brotato_vehicles.scad` 的 `bone_veh_bus`（`dw_veh_survivor_bus`）：只从北 / 西 / 东驶入（相机朝北俯视，
  玩家以南只有约 8 m 可见），停在玩家侧前方一个车道外、车门朝向玩家；撤离区在车门外（`GetExtractionZonePos`），
  停稳后才计时；驶入途中碾碎车道上的僵尸；车门上方一盏面光源保证黄昏下可读。

---

## 6. 约定速查

- **命名**：类型/函数 `PascalCase`，变量/参数 `camelCase`，私有成员 `camelCase_`（尾下划线），常量 `camelCase`，宏 `UPPER_CASE`。游戏内结构体加 `F` 前缀（`FEnemyDef`），枚举加 `E`（`EAppState`）。
- **计时单位**：运行时计时几乎全是 **毫秒 `float`**，字段名带 `Ms` 后缀（`cooldownMs`、`remainingMs`）。`OnTick` 拿到的是秒（`double deltaSeconds`），进函数后立刻 `deltaMs = deltaSeconds * 1000`。
- **坐标系**：俯视 3D，玩法在 **XZ 平面**，Y 是高度。所以到处是 `DistanceXZ()` 而非 `glm::distance`。
- **`using namespace Brotato3DUtil;`**：每个 `*System.cpp` 顶部都写，以直接用 `ClampToArena`/`DistanceXZ`/`HiddenPosition` 等共享工具。
- **音频单入口**：玩法代码**只调** `Brotato3DAudio.hpp` 里的 `PlayXxxSfx`，绝不直接碰 `NextAudio`。加武器/敌人音效就扩这个 header。
- **`MasterDifficulty`**（`Brotato3DAudio.hpp`，默认 1.0）：作用于敌人接触/迫击炮伤害的全局难度乘子，可做难度/作弊开关。
- **`DEV_MODE`**：编译期宏；当前 target 在所有构建配置都定义 `DEV_MODE=1`。`K` 召唤 rat、`1~6` 切单一武器等调试键在 `#if DEV_MODE` 中，但不要声称当前 Release 会自动移除它们；若要区分配置，应先修改 CMake 条件。
- **本地化**：UI 文本走 `Tr(gameInstance, key, fallback)` / `TrFormat(...)`，底层是引擎的 `NextLocalization`（在 `OnInit` 里 `LoadFromJson(i18n.json, "zh")`）。

---

## 6.5 竞技场地图（`assets/scad/source/brotato3d/*.scad`）

三张固定地图由 `projects/Brotato3D/Content/configs/arenas.json` 绑定（Brotato3D 游戏工程的内容，C++ 与
C# 两版共用；地图本身仍在 SCAD 资产树里，因为它们按相对路径 `use` kit 库），改地图前先记住这几条硬约束：

| 事实 | 出处 | 对地图的意义 |
|---|---|---|
| `halfExtent` = town [260,160] / outskirts [320,200] / desert [340,220] | arenas.json | SCAD 的 x/y 直接对应；边界外有隐形墙 |
| 相机 = 目标 + (0, 13, 10)，FOV 60°，target 被 clamp 到 `halfExtent - (32, 38)`，并朝跑动 / 射击方向前瞻 | `OverrideRenderCamera` / `UpdateCameraTracking` | **`halfExtent` 之外永远看不见，往外堆几何是纯浪费**；玩家以南只露约 8 m，以北约 22 m |
| 世界 `z = -scad_y` | ScadLoader 的 Z-up→Y-up | 相机从 SCAD 的 -y 往 +y 看，**kit 的 front = -y 正对镜头** |
| 单屏地面 ≈ 66 m (x) x 41 m (y) | 由上面两条推出 | 设计粒度：**每 66x41 m 至少要有一处结构** |
| 建筑不参与碰撞（只有撤离车是障碍） | `ResolvePlayerObstacleCollision` | 街区的价值是**可读的规划 + 方位地标**，不是阻挡 |

写地图的方法（三张图都按这个来的，改图/加图照抄）：

1. **先画格子再写坐标**：town 是 65x58 m 的方格街坊（一街坊≈一屏），outskirts 是一条分离式公路 + 农田网格，desert 是公路 + 铺砖式地表分区。
2. **每块空地都要有用途**：停车场 / 院子 / 球场 / 田块 / 碎石场，而不是裸草皮。`dd_ground_lot`、`dd_nature_field_big`、`dd_ground_gravel` 就是为此加的。
3. **线性件划红线**：绿篱 / 铁丝网 / 栅栏 / 护栏 / 隔离墩，它们是俯视下"被规划过"的最强信号。
4. **地标锚定方位**：水塔 / 教堂尖塔 / 信号塔 / 加油站雨棚 / 广告牌，跑图时每 1~2 屏能看到一个。
5. **边界要看得见**：town/outskirts 用林带，desert 用岩带 + 沙丘，让玩家理解隐形墙在哪。
6. **朝向**：面向镜头的一排用默认朝向（front = -y），背面排用 `rotate([0,0,180])`；注意 `lay_row` 永远沿 +x 生长，旋转 180° 的排必须从街坊**东端**起算。
7. **地面叠层**：地面件重叠时上层要抬 `dd_layer(1)`，理由见 [SCAD 资产实战手册](ScadAssetPlaybook.md) 的"地面叠层"。
8. **末日后那一层**：规划好的街区之上，用 `kit_deadly_wasteland.scad` 让街面"出过事"——`dw_ground_road_clutter(L, W, seed)`
   沿一段路确定性地铺血泊、拖痕、尸体、纸片、垃圾袋、行李、购物车、刹车痕、弃车、烧毁车（整段 `gk_flatten`，一段路一个
   Node）；已经摆了车阵的路段传 `cars = false`。主干道上再放几处 `dw_prop_roadblock`（警车 + 隔离墩 + 没守住的人）。
   场景里的是暗红的陈血，游戏运行时的血（`Brotato3DGoreSystem`）才是鲜红的。
   件库预览：`gnb shot --scene assets/scad/tests/deadly_wasteland_preview.scad`。

验收用 `gnb shot --scene assets/scad/source/brotato3d/<map>.scad`；注意全图 shot 的相机在 ~600 m 外，薄板堆叠处会出现光线精度造成的黑斑，**那是远距离截图的 artifact，游戏内 32 m 机位不会有**——怀疑时把该区域单独摆成一个小场景再截一张。

---

## 7. 改完怎么验证

```bash
# 构建
gnb.bat build Brotato3D      # Windows
./gnb.sh build Brotato3D     # macOS/Linux

# 运行：看到 "committed scene [...]" 即初始化通过
./gnb.sh run Brotato3D
```
- 配置缺字段会在 `OnInit()` 抛 `Brotato3D failed to load required data`。
- 用 `DEV_MODE` 的 `K`（喷怪）+ `1~6`（切武器）边玩边验数值。

自动化验证（隐藏窗口，几十秒一条，截图落在 `out/build/<preset>/validation_runs/<runId>/screenshots/`）：

```bash
gnb validate --script assets/agentscripts/brotato3d-zombies.agentscript.json     # 九种僵尸特写、断肢、爬行、战斗、三种死法
gnb validate --script assets/agentscripts/brotato3d-extraction.agentscript.json  # 黄昏、巴士碾压、停车、上车、进商店
gnb validate --script assets/agentscripts/brotato3d-arenas.agentscript.json      # 三张竞技场依次加载并开打
gkNextUnitTests "[Brotato3D]"                                                    # 角色资产契约（共用骨架、必需动作 / 骨骼）
```

`brotato3d.validation.*` cvar 能开局、刷九种僵尸一排 / 一圈尸群、断肢、全杀、清场、入夜、上车、切竞技场、
无敌 / 冻结僵尸 / 停火 / 停波次钟 / 自动选升级卡、把相机拉近；`game.*` 查询给出状态、波次、存活 / 尸体 / 爬行数、
断肢数、玩家位置与速度、巴士状态、收到的移动输入事件数。验证窗口是隐藏的，但**仍可能收到本机的键盘 / 手柄输入**——
脚本里出现玩家莫名位移时，先看 `game.moveInput` 的 `events=`。

---

## 8. 进一步阅读

- [`docs/projects/brotato-3d/developer-guide.md`](../projects/brotato-3d/developer-guide.md) —— 配置/数值/加内容向开发指南（本文的姊妹篇）
- [`docs/projects/brotato-3d/introduction.md`](../projects/brotato-3d/introduction.md) —— 项目定位
- [`AGENT_GUIDE/MagicaLego.md`](MagicaLego.md) —— 另一个 C++ 小游戏的同类梳理，可对照
- [`AGENTS.md`](../../AGENTS.md) —— 引擎全局规范、构建/命名/目录约定
