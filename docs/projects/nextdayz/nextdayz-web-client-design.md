---
title: "NextDayz Web 客户端：同源内核 + three.js 表现层"
category: project
status: 现行（已实施；W8-3、W8-5 与 5.3 的收尾未完成）
owner: NextDayz
created: 2026-11-16
last_updated: 2026-11-23
---

# NextDayz Web 客户端：同源内核 + three.js 表现层

本文是 NextDayz 浏览器客户端的架构设计。目标是让玩家打开网页就能登录 Master、出击到某张地图的
DS，并与桌面客户端在同一个世界里游玩。剩余工作见
[开发计划](nextdayz-web-client-plan.md)；日常开发、烘焙、测试与部署见
[Web 客户端开发指南](../../guides/nextdayz-web-client.md)；联机现状见
[启动模式与多人联机](nextdayz-multiplayer-design.md)。

> D1–D8 已确认并实施。W0–W7 与 W8-0、W8-1、W8-2、W8-4 已验收，实测并入 5.4、7.1、8.1、9 与 10。
> 与设计仍有差距的地方就地标为“现状”：5.3 的联机客户端仍有两份，8.1 缺中档独显和 Firefox/Safari 的
> 完整实测，8.2 的界面文案未全部走翻译表。D2 的浏览器内求值仍在试验中。

## 0. 一页结论

Web 客户端由四块组成，前三块保证"对应"，最后一块是唯一允许与桌面不同的部分：

| 组成 | 是什么 | 保证了什么 |
| --- | --- | --- |
| **同源内核** `NextDayzCore` → `ndcore.wasm` | 桌面 NextDayz 的玩法 C++ 源码（协议、库存、物品/武器表、3C、武器、动画图、rig 运行时、物资、联机客户端状态机），剥离引擎依赖后**同一份源码**既链接进桌面程序，也用 Emscripten 编成 WASM | 玩法完全对应：不存在第二份玩法实现 |
| **同源烘焙** `NextDayzBake`（`gnb web bake`） | 用桌面**同一份 ScadLoader** 把已发布地图、角色 rig、物品/武器/服装目录求值，导出 GLB + 碰撞 + 锚点 + rig 数据 | SCAD 建模流程与角色流程完全对应：作者仍然只写 `.scad`，几何、骨骼、clip 由同一段代码产生 |
| **同一协议** NextNet WebRTC 传输 | DS 在 ENet 之外再监听 WebRTC DataChannel（libdatachannel，仓库已用于 Remote Play）；`NetProtocol` 的字节一个不改 | 与桌面客户端同服：DS 不区分对方是桌面还是浏览器 |
| **Web 表现层** `web/nextdayz/` | TypeScript + three.js 渲染、DOM HUD、WebAudio、输入、资源加载 | 不要求对标：画面、UI 技术、音频后端都按浏览器的成熟做法来 |

一句话：**玩法规则只存在于内核，SCAD 语义只存在于 ScadLoader，Web 侧只负责"把内核算出来的东西画
出来、把玩家的输入送进去"。**

## 1. 目标与边界

### 1.1 必须交付

1. 桌面浏览器（键鼠）打开页面 → 选择 Master → 昵称登录 → 大厅看仓库、选带出物品与地图 → 出击 →
   在 DS 上游玩 → 撤离入库 / 死亡 / 下线后回到野外。全程与桌面客户端走同一个 Master、同一个 DS。
2. 桌面玩家与 Web 玩家互相可见，动作、持枪、换弹、翻越、服装一致；共同打感染者；互伤开关生效；
   抢同一处物资只有一人得到；物品账本、身体结算、命中回溯校验对两种客户端一视同仁。
3. 玩法对应：移动速度与姿态、翻越/攀爬规则、武器数值与后坐力、命中分区、网格背包规则、搜刮流程、
   生存数值显示、撤离流程，与桌面版出自同一份代码。
4. SCAD 对应：新增或修改地图、kit、角色骨架、clip、服装、武器模型，只需重新烘焙即可出现在 Web
   版，不改 Web 代码。
5. 可自动验证：无浏览器的协议机器人（Node 里跑内核）、桌面 + Web 混合的端到端脚本、烘焙与协议的
   金标测试，全部能在 CI 里给出 pass/fail。

### 1.2 不做

- 画面对标（光追、GI、DLSS、体积雾等）。Web 版追求清晰、稳定 60 fps，不追求和桌面同一张图。
- 单机战役与离线训练场（它们依赖本地 `WorldSim`、存档与导航构建）。Web 首版只有联机；训练场作为
  Master 上的一张地图照常可进。
- 发布版不在浏览器内求值 SCAD（同一份 ScadLoader 编成 WASM 的试验见 D2）；ScadLibrary / 编辑器功能、
  F5 调试器面板。
- 触屏与手机浏览器、手柄。
- 无 GPU 的 DS。DS 仍按现状运行（需要 Vulkan 设备，软件 Vulkan 可用）；本方案只给它加一种传输。
- 账号体系升级。只加 D6 里的登录口令，除此之外昵称即账号的现状不变。

## 2. 现状中决定方案的事实

以下均来自当前代码，是后面每个决策的依据。

| 事实 | 位置 | 对 Web 版的含义 |
| --- | --- | --- |
| DS 传输是 ENet（UDP），两条通道：可靠有序、不可靠"新包覆盖旧包" | `src/Modules/NextNet/NetHost.*` | 浏览器不能发裸 UDP，DS 必须增加一种浏览器可用的传输，并保持这两种投递语义 |
| 协议版本 5，25 种消息；编码里多处把整个 C++ 结构体按内存布局写出（`FSurvivorPresence`、`FPlayerLocomotionState`、`TimeSystem::FState`、`FDamageEvent`、`FWeaponAttachments`、`FGridPlacement`） | `Net/NetProtocol.cpp` | 用另一种语言手写这些布局很脆弱，桌面每加一个字段就会悄悄错位；用同一份 C++ 编译则天然一致 |
| 移动由客户端决定、服务器只检测；命中由客户端发现、服务器按回溯位置校验；背包在客户端操作、服务器按账本核对守恒 | `NextDayzNetwork.cpp`、`Sim/ItemLedger`、`Sim/LagCompensation` | 客户端承担大量玩法逻辑（3C、开火、库存、动画驱动的命中盒）。这些逻辑必须与桌面一致，否则两类玩家玩的不是同一个游戏 |
| 感染者命中盒取自客户端本地动画后的骨架 | `Zombies/ZombieRigVisual.*`、`Combat/CombatSystem` | 动画图与 rig 运行时也属于"玩法"，不只是表现 |
| 协议、库存、物品表已能脱离地图和渲染单独编译 | `src/Application/Util/NextDayzBots/CMakeLists.txt` | 内核的最底层今天就是干净的，可以作为第一步 |
| 物理已在抽象接口之后（`NextPhysics`、`INextCharacterControllerBackend`），后端是 Jolt | `Engine/Runtime/Subsystems/NextPhysics.hpp`、`Modules/NextPhysics` | Jolt 本身可编成 WASM，角色控制器可以用同一个后端 |
| 分层动画器已区分 `Evaluate`（只算姿态）与 `Apply`（写场景节点）；姿态后处理 `RigPoseOps` 只操作 `FRigPose` | `src/Gameplay/Rig/` | rig 运行时离"不依赖引擎"只差资产结构里夹带的 `Model` |
| 引擎的 `Assets` / `Runtime` 层约 2.7 万行里有 29 个文件直接触及 Vulkan，`Scene`、`Model`、`Engine.hpp` 都在其中 | `src/Engine/**` | 把整个引擎无头化再编成 WASM 是引擎级改造，不走这条路 |
| 地图是运行时求值的 `.scad`（Manifold CSG + 地形），桌面提交一张 1 km 地图要十几秒 | `Modules/ScadLoader`、多人设计 §7 | 在浏览器里用单线程 WASM 现场求值会更慢，首屏不可接受 |
| 玩法对象来自具名 SCAD 模块节点及其 metadata：`nd_loot`、`nd_item`、`nd_zed`、`nd_spawn_*`、`nd_extraction`、`nd_poi`、`nd_target`、水井 | `World/WorldAnchorRegistry.cpp` | 这些可以在烘焙时一次扫描导出 |
| 角色、感染者、物品、武器、配件、服装、第一人称手臂都是 ScadRig 资产（`bone_` 模块 + `anim_` clip） | `assets/scad/characters/nextdayz_*.scad` | 一种烘焙格式覆盖全部角色类资产 |
| libdatachannel 与 cpp-httplib 已在 `vcpkg.json` 里，用于 Remote Play | `vcpkg.json`、`Modules/NextRemote` | WebRTC 传输不引入新依赖 |
| Master 是 Go 的 HTTP JSON 服务，已有公网部署包与 `--public-host` | `tools/gnb/internal/dzmaster`、`docs/guides/nextdayz-server-deployment.md` | 浏览器用 `fetch` 即可访问，但需要同源或 CORS，以及 HTTPS |

## 3. 决策（已核对）

D1–D8 已于 2026-11-16 全部按推荐确认，下面保留每项的结论与被否决的备选，供执行者理解边界。
**D1 是整个方案的分水岭。** 执行中若有证据表明某项决策站不住，停下交回核对，不自行改道。

### D1 玩法逻辑如何与桌面保持一致 —— 已确认：同源内核编成 WASM

| 方案 | 做法 | 评价 |
| --- | --- | --- |
| **A. 同源内核（已确认）** | 把桌面 NextDayz 的客户端玩法代码整理成不依赖引擎的 `NextDayzCore`，桌面链接它，Web 把它编成 WASM | 玩法只有一份；桌面以后加玩法，Web 重新编译就有。代价前置在桌面代码上：要把混着场景/渲染调用的类拆成"逻辑"与"应用到引擎"两半 |
| B. TypeScript 重写 | 约 2.2 万行客户端玩法 C++ 移植到 TS，靠导出的数据表和金标测试约束 | 起步快、不动桌面代码；但此后每个玩法改动都要做两遍，NextDayz 目前几乎每天都在改，漂移不可避免。协议里按内存布局写出的结构体要手工对齐 |
| C. 整个引擎编成 WASM | 引擎加无 Vulkan 的构建形态，游戏代码原样编译，ImGui HUD 也原样复用 | 游戏代码零改动，但引擎核心要长期维护第二种形态；运行时现场求值 SCAD、内存与包体都不适合浏览器；也与"引擎不必对标"的前提相悖 |

选 A 的理由：需求里唯一的硬约束是"完全对应"，而能长期守住它的只有"只有一份"。A 的改造方向与
多人设计里尚未完成的 M1（`Sim/` 与 `Client/` 拆分、`PlayerController` 拆运动体与相机、
`WeaponSystem` 拆开火判定与视图模型）是同一件事，等于借 Web 版把它做完。

A 的风险是桌面回归。对策写进计划：按子系统逐个迁移，每一步桌面都保持可发布，以现有
`[NextDayz]` 单测和 `nextdayz-net-*` agentscript 作为闸门。

B 不再是并行选项，只作为记录保留：若 W0-1 的实验否定了 A（例如内核无法在不带引擎的前提下编译，
或 wasm32 下协议字节无法与桌面一致），执行者**不得自行改走 B**，应停下并把证据交回核对。B 与 A 共用
同一套 Web 侧接口（第 5.3 节），届时传输、烘焙、Web 表现层、部署四条线不需要重做。

### D2 SCAD 如何进入浏览器 —— 已确认：离线烘焙；浏览器内同源求值在试验中

- **结论（现行发布）**：新增桌面工具 `NextDayzBake`，链接真正的引擎与 ScadLoader，把内容求值后导出为 Web 资产。
  "流程完全对应"由三点保证：作者的输入不变（仍是 `.scad`、同一批 kit、同样的 `bone_` / `anim_`
  约定）；求值代码不变（就是桌面那份）；产物带内容哈希并与地图版本绑定。`gnb web bake --watch`
  让"保存 `.scad` → 浏览器里刷新出来"保持在秒到十几秒级。发布包和正式站点只使用烘焙资产。
- **试验中：浏览器内同源求值**（2026-11-23 起）。整张洛克特的烘焙资产压缩后为 57.62 MB，离 60 MB 预算
  只差一点。为降低下载量，试验把桌面同一份 C++ ScadLoader 编成 WASM，在浏览器 Worker 里求值训练场源文件，
  现场生成地图网格、程序化贴图、Jolt 碰撞和锚点，再由同一个内核联机。SCAD 语义仍只在 ScadLoader 里
  实现一次，所以它不违背"完全对应"。它要检验的是原先否决这条路的两个代价：进图前的求值时间，以及
  Manifold、FreeType 带来的包体。入口见 [训练场联机验收](../../../web/nextdayz/README.md#完整训练场联机)。
  要转为默认路径，以下数据须先记在本节：
  1. 同一地图版本下，浏览器求值与离线烘焙在渲染三角形数、碰撞三角形数、锚点表、物资持久 id 上逐项一致。
     离线烘焙始终是对照基准，发布流程里的 `gnb web bake --check` 继续运行。
  2. 求值器（含 Manifold、FreeType）加源文件的压缩下载量，再加角色与共享资产，明显低于烘焙整张图。
  3. 在中档独显笔记本上，求值耗时和 WASM 线性内存峰值有记录，从点击出击到可操作仍不超过 20 秒。
     求值失败时能回退到烘焙资产。
- 否决：TS 重写 SCAD 求值器。1 万行求值/CSG/地形/纹理图代码的第二实现，直接违反"完全对应"。

### D3 浏览器与 DS 之间的传输 —— 已确认：WebRTC DataChannel

- **结论**：DS 增加 WebRTC 传输，每个浏览器玩家一条 PeerConnection、两条 DataChannel，分别对应
  ENet 的两条通道（见第 7 节）。它是浏览器里唯一同时提供"可靠有序"和"不可靠"的通用方案，延迟
  特性与 ENet 接近；DTLS 自带加密。
- 否决（作为主路径）：WebSocket。实现最简单，但基于 TCP，一个丢包会卡住后面所有快照。保留为
  **回退传输**：UDP 被封的网络下使用，排在计划的后段，不进首版验收。
- 否决：WebTransport。需要 HTTP/3 服务端和证书管理，仓库没有现成依赖，浏览器覆盖也不如 WebRTC。

### D4 HUD 与界面 —— 已确认：DOM 重写

- **结论**：用 HTML/CSS 做 HUD、背包、大厅、设置，数据来自内核每帧给出的界面状态，操作以命令形式
  回到内核。网格背包的规则（能不能放、旋转、堆叠、装备槽）在内核的 `Inventory` 里，界面只是视图。
  浏览器里文字清晰、输入法与缩放天然可用。
- 否决：把 ImGui HUD 编进 WASM。界面能与桌面逐像素一致，但现有 HUD 与引擎的图标离屏渲染、字体
  加载、`NextDayzGameInstance` 耦合很深，拆出来的成本不低于重写，结果还是一个不像网页的界面。

代价要说清楚：**界面是本方案里唯一需要桌面与 Web 各维护一份的部分。** 桌面加新面板，Web 要跟着做。

### D5 物理 —— 已确认：Jolt 同源编进内核

角色控制器（胶囊、台阶、斜坡、动态高度）和翻越探测的手感来自 Jolt 与
`NextCharacterController`。把同一个 Jolt 后端编进 `ndcore.wasm`，碰撞体由烘焙产物构建。否决在 Web
侧换用别的物理库：移动是客户端说了算的，换库等于换手感。

### D6 首版范围与上线前置 —— 已确认

- 首版只做联机（Master 流程 + 开发用直连），Web 版没有单机战役。
- **公网上线前先给 Master 加登录口令**（已在 W8-0 实施，先于 W8-4 的对外部署）。原先昵称即身份、无密码，
  桌面版分发范围小时问题不大，网页一旦公开，任何人都能用别人的昵称登录并动用其仓库。范围只到
  "最基本的口令"，做法见 7.3；邮箱、找回、第三方登录都不做。

### D7 代码与工具的位置 —— 已确认

| 内容 | 位置 |
| --- | --- |
| 内核静态库与 WASM 目标 | 源文件留在 `src/Application/Game/NextDayz/` 原处，由显式文件清单组成 `NextDayzCore`；WASM 入口在 `src/Application/Web/NextDayzWebCore/` |
| 烘焙工具 | `src/Application/Util/NextDayzBake/`（与 `Packager`、`NextDayzBots` 并列） |
| Web 客户端 | 仓库根 `web/nextdayz/`（TypeScript、Vite；仓库已有 `website/` 这个 Node 工程可参照） |
| WebRTC 传输 | `src/Modules/NextNet/` 内，与 ENet 并列 |
| Master 的信令与静态托管 | `tools/gnb/internal/dzmaster/` |
| 命令 | `gnb web setup | bake | dev | build | test`，`gnb ds --web`，`gnb master serve --web <dir>` |

不搬动现有玩法文件的目录，是为了不和正在进行的 NextDayz 开发产生大面积冲突；混合了逻辑与引擎调用的
文件就地拆成两个（例如 `PlayerRigVisual` 拆出只算姿态的 `PlayerRigGraph`）。

### D8 目标浏览器 —— 已确认

桌面 Chromium 系、Firefox、Safari 的当前与上一个大版本；键盘鼠标；WebGL2 为验收基线。three.js 的
WebGPU 路径可以作为增强打开，但不进验收。

## 4. 总体架构

```
                         ┌───────────────────────── 浏览器页面 ─────────────────────────┐
                         │                                                              │
  键鼠 / Pointer Lock ──►│  Web 表现层（TypeScript）                                    │
                         │  ┌────────────┐ ┌──────────┐ ┌───────────┐ ┌──────────────┐ │
                         │  │ three.js   │ │ DOM HUD  │ │ WebAudio  │ │ 资源加载     │ │
                         │  │ 场景/角色  │ │ 背包/大厅│ │ 定位音效  │ │ GLB/碰撞/rig │ │
                         │  └─────▲──────┘ └────▲─────┘ └─────▲─────┘ └──────┬───────┘ │
                         │   帧状态│     界面状态│     表现事件│        烘焙产物│         │
                         │  ┌──────┴────────────┴─────────────┴───────────────▼───────┐ │
                         │  │ ndcore.wasm（同源内核，C++）                             │ │
                         │  │ 联机客户端状态机 · 协议编解码 · 库存/物品/武器表        │ │
                         │  │ 3C（Jolt 角色控制器）· 武器与命中 · 动画图 · rig 运行时 │ │
                         │  │ 物资 · 动作 · 生存显示 · 时间天气                        │ │
                         │  └───────▲─────────────────────────────────────▲───────────┘ │
                         │   HTTPS  │ fetch（JSON）            DataChannel│（协议字节） │
                         └──────────┼─────────────────────────────────────┼─────────────┘
                                    │                                     │
              ┌─────────────────────▼──────────┐         ┌────────────────▼───────────────┐
              │ Master（Go）                   │ 信令转发 │ 关卡 DS（NextDayz --dedicated）│
              │ 账号/档案/派遣/票据（不变）    │◄────────►│ WorldSim、账本、身体、回溯校验 │
              │ + 静态托管 Web 客户端与资产    │          │ NextNet：ENet 传输（桌面）     │
              │ + WebRTC 信令转发 + 版本清单   │          │        + WebRTC 传输（浏览器） │
              └────────────────────────────────┘          └────────────────▲───────────────┘
                                                                           │ ENet / UDP
                                                                  桌面客户端 NextDayz.exe

  离线：.scad / kit / 角色 ──► NextDayzBake（桌面引擎 + 同一份 ScadLoader）──► 带哈希的 Web 资产
```

权威范围不变：Master 管账号与档案，DS 管一张地图的世界，客户端管输入、相机、本地移动与全部表现。
Web 客户端就是"客户端"这一格的另一种实现，其中玩法部分与桌面共用。

## 5. 同源内核 `NextDayzCore`

### 5.1 内核里有什么

| 领域 | 现有代码 | 现状 | 进内核需要做的事 |
| --- | --- | --- | --- |
| 协议编解码 | `Net/NetProtocol.*` | 源文件干净，头文件为了几个结构体包含了 `WorldSim.hpp` 等 | 把 `FSurvivorPresence`、`FZombieRuntime` 等线上结构挪到独立的纯数据头 |
| 库存、物品、武器、配件、掉落表 | `Inventory/Inventory.*`、`Data/*`、`Weapons/*Defs.hpp` | 已干净 | 无 |
| 物品账本规则、出生套装 | `Sim/ItemLedger.*` | 干净 | 无（客户端用它预判服务器会不会接受） |
| 生存数值、时间天气 | `Player/SurvivalSystem.*`、`World/TimeSystem.*` | `TimeSystem::Tick` 直接写场景环境 | 拆出"状态推进"与"应用到场景" |
| 配置 | `NextDayzConfig.hpp` | 头文件 | 无 |
| rig 运行时 | `src/Gameplay/Rig/RigLayeredAnimator.*`、`RigPoseOps.*` | 只差 `FRigAsset` 里夹着 `Model` | `FRigAsset` 拆成骨架 + clip（纯数据）与部件网格两部分 |
| 角色动画图 | `Player/PlayerRigVisual.*`、`Zombies/ZombieRigVisual.*`、`Player/FirstPersonArms.*` | 逻辑与场景节点操作混在一起 | 拆成"算姿态、命中盒、挂件可见性"的图与"写节点"的适配层 |
| 3C | `Player/PlayerController.*`、`PlayerActionController.*`、`Gameplay/Character/NextCharacterController.*` | 通过 `NextEngine&` 取物理与射线 | 改为依赖 `NextPhysics` 抽象与世界查询端口（5.2） |
| 武器 | `Weapons/WeaponSystem.*` | 开火判定与第一人称模型节点混在一起 | 拆出武器状态/开火/后坐力/散布，与视图模型的节点操作分开 |
| 感染者客户端镜像与命中 | `Zombies/ZombieSystem.*`（`ApplyReplicas`）、`Combat/CombatSystem.*`、`Combat/NoiseSystem.*` | 基本干净，经函数钩子取射线 | 确认不经头文件带入引擎 |
| 物资客户端 | `Inventory/LootSystem.*` | 用场景节点表示掉落物 | 拆出"条目状态、悬停、预约/提交"与"摆模型" |
| 联机客户端状态机 | `NextDayzNetwork.cpp` 的客户端一半、`Net/MasterApi.*`、`NextDayzLobby.cpp` 的流程部分 | 写在 `NextDayzGameInstance` 的成员函数里 | 提取为 `FClientSession`（5.3） |
| Jolt 后端 | `Modules/NextPhysics/JoltPhysicsBackend.*` | 包含 `Engine.hpp`、任务系统、`Model.hpp` | 加一条不经引擎的构造路径：从顶点/索引数组建网格体，单线程推进 |

内核**不包含**：`WorldSim` 的服务器侧（刷怪、AI、导航、补货）、存档与单机战役、调试器、ImGui HUD、
图标离屏渲染、音频后端、任何 `Engine/Vulkan`、`Engine/Rendering`、`Assets::Scene`。

### 5.2 内核向外要什么（端口）

内核只通过少量抽象接口接触外部世界，桌面与 Web 各给一份实现：

| 端口 | 作用 | 桌面实现 | Web 实现 |
| --- | --- | --- | --- |
| 物理 `NextPhysics` | 角色控制器、静态碰撞 | 现有 Jolt 后端 | 同一个 Jolt 后端，编进 WASM，碰撞体来自烘焙产物 |
| 世界射线查询 | 子弹、视线、翻越探测、相机臂 | 引擎的 CPU 加速结构 | 内核里的 Jolt 射线，查询烘焙导出的"可被射线命中"几何 |
| rig 资产 | 骨骼、绑定姿态、clip | `FScadRigLoader` 现场加载 | 读烘焙出的 rig 数据 |
| 时钟与随机数 | 帧时间、确定性随机 | 引擎 | 浏览器时间；随机数用已有的 `DeterministicRng` |
| HTTP 与数据报 | Master 请求、DS 消息 | `FHttpWorker`、`FNetHost` | 字节交给 TS，由 `fetch` 与 DataChannel 收发 |

端口之外的一切输出都是**数据**，见下一节。

### 5.3 内核与表现层的接口

`FClientSession` 是内核对外的唯一对象，承担现在 `NextDayzGameInstance` 里"联机客户端"那部分的帧
顺序。它的输入输出是这样四类，Web 侧通过 C 导出函数加共享内存视图访问，热路径不经 JSON：

**输入（表现层 → 内核）**

- 输入意图：移动键、视角增量、跳跃、蹲、瞄准、开火、换弹、切枪、交互、撤离等，语义与桌面
  `GameInstance` 的输入处理函数一一对应。键位映射在表现层，内核只认意图。
- 界面命令：背包里的移动/旋转/装备/卸下/使用/丢弃/装卸配件，大厅里的登录/选图/带出/出击，重生等。
- 网络输入：DS 发来的消息字节（标明通道），Master 的 HTTP 回复。
- 世界与资产：地图碰撞、地形、锚点表、rig 数据。

**输出（内核 → 表现层）**

- **帧状态**：相机（位置、朝向、视野）；时间与天气；角色表——每个角色给出种类（本地第三人称身体、
  第一人称手臂、远端幸存者、感染者）、根变换、骨骼矩阵数组、当前可见的部件与挂件（武器、配件、
  服装）；掉落物表；直升机。表现层照着它摆 three.js 对象，不做任何判断。
- **表现事件**：开火、曳光、弹着、命中标记、受击、脚步、换弹、感染者叫声等一次性事件，带位置与
  参数。表现层据此播特效和声音。
- **界面状态**：生命/饥饿/口渴/寒冷/流血、弹药、交互提示、撤离倒计时、背包与容器的完整布局、
  物品显示名与说明、大厅的档案/仓库/地图列表、联机状态行。
- **网络输出**：要发给 DS 的消息字节（标明可靠或不可靠），要发给 Master 的 HTTP 请求。

桌面这边的终态是 `NextDayzGameInstance` 的联机客户端路径也委托给同一个 `FClientSession`，把输出
应用到 `Assets::Scene`、ImGui 和 NextAudio。过渡期允许桌面继续直接调用各子系统，但**每个子系统
只能有一份实现**。

**现状（2026-11-23）**。`FClientSession`（`Net/ClientSession.*`）目前只负责 Master 登录、档案、出击、
进服握手、在场与背包上报的节流、远端插值和时钟校正。DS 消息怎么应用、每帧按什么顺序推进各子系统，
仍然写了两份：桌面在 `NextDayzNetwork.cpp` 与 `NextDayzGameInstance.cpp`，Web 在
`src/Application/Web/NextDayzWebCore/WebCore.cpp`。两份之间已经出现分歧：

- 拾取到可持握的武器后，Web 会把它装进首选武器槽，桌面不会；
- 收到服务器的背包纠正（`InventoryFix`）时，桌面在角色死亡后忽略它，Web 不检查生死；
- 打开背包或地图、翻越中、死亡时，桌面会锁定移动，并取消瞄准、扳机和近战；Web 只在动作进行中锁定移动；
- 出生时怎么装备武器，两边规则不同。

这违反了"每个子系统只能有一份实现"，收尾任务见开发计划。另外，Web 每帧经 `NdRead` 以 JSON 读取帧状态，
只有骨骼矩阵走共享内存，还没达到"热路径不经 JSON"。

### 5.4 守住内核边界的三道闸

1. **包含检查**：`NextDayzCore` 的源文件不得包含 `Engine/Vulkan/**`、`Engine/Rendering/**`、
   `Engine/Assets/Core/**`、`Engine/Runtime/Engine.hpp`、`imgui.h`。CI 里一个脚本直接扫。
2. **双目标构建**：凡是改到 NextDayz 或 `src/Gameplay/Rig` 的提交，CI 同时构建桌面目标与
   `ndcore.wasm`。内核被引擎依赖污染时，WASM 构建当场失败。
3. **协议金标**：每个按内存布局写出的结构体加 `static_assert` 钉住大小与字段偏移；一组固定消息
   在 x86_64、arm64、wasm32 三处编码出的字节必须完全相同。

现行实现：`gnb web build` 在 WASM 构建之后，用 `ninja -t deps` 核对内核的全部依赖。规则比第 1 条更严：
除少数白名单文件外，`Engine/`、`Gameplay/`、`Modules/` 下的任何头文件都不得进入。接着它把 WASM 的
协议字节与 `protocol-golden.txt` 比对，并在 Node 里跑规则探针 `NextDayzCoreRules`。CI 的 `NextDayz Web`
工作流在改到内核、Web 或相关模块的提交上运行这一整套，并在 Linux x86_64 上对原生的
`NextDayzProtocolGolden` 和 `NextDayzCoreRules` 做同样检查。`Desktop CI` 在 Windows、Linux 和
macOS arm64 上也跑原生金标。2026-11-23 首次在 Windows MSVC x86_64 上核对，11 行金标逐字一致，规则探针通过。

**W0-1 实测（2026-11-18）**。macOS arm64、Rosetta x86_64 和 Node 26.13 的 wasm32 使用
`NetProtocol.cpp`、`Inventory.cpp`、`ItemDefs.cpp`，五条消息 `Hello`、`PlayerState`、`Snapshot`、
`DamageNotice`、`InventorySync` 的字节及解码后的消息边界一致。六种直接写入的类型大小、对齐及已测字段
偏移一致，大小依次为 48、96、32、56、5、12 字节（顺序同第 2 节）。用 emsdk 安装并激活 Emscripten
4.0.23 后，单线程产物为 84,997 B WASM 和 10,430 B Node 加载器，构建只使用仓库头与 vcpkg 的头文件。

W3 的包含整理从 `NetProtocol.hpp → WorldSim.hpp` 入手。把 `FSurvivorPresence` 移入纯数据头以切断
`UniformBuffer.hpp` 和 `NavGrid.h`；将 `TimeSystem::FState`、`LootSystem.hpp` 中的物资同步类型、
`ZombieSystem.hpp` 中的快照类型拆出，再把 `CoreMinimal.hpp` 换成直接使用的标准头及 GLM。当前
`FNetWriter` 会复制隐式填充，实验先将填充清零再赋字段以控制输入。W3 应断言大小及偏移，并在序列化时
把填充位置固定为零，保留原协议每个字段的字节偏移和总长。`FGridSize` 与 GLM 向量也应纳入 W3 的布局断言。

对 Web 侧的硬规则：**TS 代码里出现玩法常量或玩法判定（速度、伤害、可否放入、是否命中）即为缺陷。**

## 6. 同源烘焙 `NextDayzBake`

### 6.1 输入与产物

输入是桌面版运行时读取的同一批文件：`assets/configs/nextdayz_maps.json` 里的地图、
`assets/scad/characters/nextdayz_*.scad`、相关 kit、配置、音效、字体。

产物写到 `out/web/nextdayz/assets/`。清单为每个文件记录 SHA-256，整组文件放在带内容哈希的目录：

| 产物 | 内容 | 来源 |
| --- | --- | --- |
| `maps/<id>/v<N>/scene-*.glb` | 地图渲染几何，按网格分块；材质为基础色/粗糙度/金属度，`gk_texture` 印花烘成贴图 | ScadLoader 求值后的场景 |
| `maps/<id>/v<N>/collision.bin` | 桌面实际注册到物理的碰撞体与实际参与 CPU 射线的几何（以 `PhysicsComponent` 与射线可见标记为准），含地形 | 同一场景 |
| `maps/<id>/v<N>/terrain.bin` | 地形高度场与地表分区 | `TerrainComponent` |
| `maps/<id>/v<N>/anchors.json` | `WorldAnchorRegistry` 的扫描结果（安全落点、物资点及其固定物品、水井、POI、撤离点），靶子等带 metadata 的玩法节点，物资条目的持久 id | 具名节点 + metadata |
| `maps/<id>/v<N>/env.json` | 天空、太阳、雾的地域配置 | 场景环境与世界 profile |
| `rigs/<name>.rig.bin` | 骨骼、绑定姿态、全部 clip（已是引擎空间），即 `FRigAsset` 的纯数据部分 | `FScadRigLoader` |
| `rigs/<name>.glb` | 各部件网格，按骨骼与部件编号命名；每个分段的颜色、粗糙度/金属度、是否可换色、印花贴图 | 同上 |
| `icons/<itemId>.webp` | 物品图标 | 桌面现有的图标离屏渲染 |
| `audio/*` | 音效（首版直接用 WAV，浏览器都能解） | `assets/sounds/nextdayz/` |
| `i18n/*.json` | 界面文案表 | `UI/Translations.inl` |
| `manifest.json` | 全部文件的哈希与大小、协议版本、每张地图的版本号、内核构建标识 | 工具生成 |

物品、武器、配件的**数值**不导出：它们在内核里，界面要显示的名称与说明由内核给出。这样新增一件
物品不会出现"数据表忘了同步"。

### 6.2 "SCAD 流程对应"的具体含义

下列内容变更后只需 `gnb web bake`，**Web 代码零改动**：

- 新地图或地图新版本（`nextdayz-geomap` 流程照旧，发布后烘焙）；
- kit 模块的造型、材质、印花；
- 角色骨架、`rig_humanoid.scad` 生成的 clip、手工 clip、IK 目标骨骼；
- 服装外壳、武器与配件模型、物品模型、第一人称手臂。

需要重新编译内核（但仍不改 Web 代码）：物品表、武器数值、动画图逻辑、任何玩法规则。

需要 Web 侧开发：新的 HUD 面板、新种类的特效、内核新增的表现事件类型。

### 6.3 版本与一致性

- DS 的 `Welcome` 带地图场景路径（含 `published/v<N>`），Web 客户端据此在 `manifest.json` 里找对应
  烘焙目录；找不到就明确提示"这张地图的 Web 资产尚未发布"，不进入世界。
- 烘焙是发布流程的一步：地图发布新版本而没有重新烘焙时，`gnb web bake --check` 失败（CI 守卫）。
- 烘焙自检：导出的三角形数、包围盒、锚点数量与桌面加载同一场景的统计一致；rig 的骨骼数、clip 数、
  clip 时长一致。
- 作者迭代：`gnb web bake --watch` 监视 `.scad` 与 `terrain.hmap`，只重烘受影响的地图或 rig，并通知
  开发服务器刷新页面。

## 7. 网络

### 7.1 DS：一个 `FNetHost`，两种传输

`FNetHost` 对游戏代码的接口不变（`Send(peer, payload, delivery)`、`Poll` 返回连接/断开/消息事件）。
内部改为可以同时挂多个传输，ENet 与 WebRTC 产生的对端共用一套 `FPeerId`。链路模拟与流量统计放在
传输之上，两种客户端都受用。`NextDayzNetwork.cpp` 不需要知道对端是哪种。

WebRTC 传输的要点：

| 项 | 设计 |
| --- | --- |
| 通道映射 | 通道 `r`：有序可靠 ↔ `EDelivery::Reliable`；通道 `u`：无序、零重传 ↔ `EDelivery::Unreliable` |
| "新包覆盖旧包" | DataChannel 的不可靠通道不保证顺序。传输层给 `u` 通道的每条消息加 16 位序号前缀，收到比已见序号旧的就丢弃，再把前缀剥掉交给游戏。**协议字节不变** |
| 大消息 | 快照约 3 KB，超过一个 UDP 包，由 SCTP 分片（与 ENet 分片不可靠包的行为等价：丢一片整条作废）。可靠通道上的物资全量可能达到几十到上百 KB。W1 每帧最多 16 KiB，接收端最多重组 4 MiB 的同一条消息 |
| 往返时延 | 命中回溯要用到 `FPeerStats.roundTripMs`。WebRTC 对端由传输层每秒一次 ping/pong 测得，口径与 ENet 的一致 |
| 线程 | libdatachannel 的回调在它自己的线程上，只允许把事件放进队列；`Poll()` 在游戏线程取出。回调不得碰游戏状态（与 Remote Play 的既有约定相同） |
| 端口 | 每个 DS 两个 UDP 端口：现有 ENet 端口，以及一个 WebRTC 端口，所有浏览器玩家复用它（libjuice，开启 `enableIceUdpMux` 并把起止端口设为同一值）。Master 分配并在派遣结果里下发 |
| 断开 | 主动断开在可靠通道上发关闭请求，等待对端确认或短时超时。PeerConnection 失败或关闭向游戏投递 `Disconnected`，后续与 ENet 共用身体保留、存档和租约逻辑 |

**W0-2 实测（2026-11-18）**。独立 libdatachannel 0.24.2 / libjuice 1.7.1 服务使用完整 offer/answer
一次 HTTP 往返。Chrome 157 和 Safari 27.0.1 各有两条同时在线的 PeerConnection 复用同一服务端 UDP 端口。
两者协商上限均为 256 KiB，并完成同尺寸可靠消息的逐字节回显。Firefox 的本机 ICE 选择了 VPN 接口。
双方加 STUN 后连通，协商上限同为 256 KiB，3 KiB 双通道均收发成功。可靠通道 64 KiB 曾成功回显，
重复运行有超时，256 KiB 尝试超时。W1 应使用小分块并在模拟链路上验证大小和重组。外部 STUN 也验证了 answer 携带公网映射候选
（`srflx`）。实测从同一台机器发起，公网客户端到部署主机的验证见 W8。

用 UDP 代理以 2% 概率丢弃实际数据报（双向）；连接的 SCTP MTU 配为 1200，发送间隔约 16 ms。Chrome
两轮实测均丢弃约 1.8% 的 UDP 数据报，3 KiB 零重传消息上行 560–568/600，往返 534/600。Safari 丢弃 50/2739 个
数据报时，往返 156/180。应用消息有多个分片，往返成功率与单个 UDP 数据报到达率使用不同口径。
Node 26.13 上 `node-datachannel` 0.33.6（内置 libdatachannel 0.24.6）已与独立 C++ 服务完成可靠回显，可供
W3 机器人复用。

**W1 实测（2026-11-18，macOS arm64）**。8 个 ENet 和 8 个 WebRTC bot 同服运行 30 分钟，
发送侧模拟延迟 80 ms、不可靠消息丢失 2%，每次采样均为 16/16 在线，最终 0 拒绝、0 掉线。
16 个纯 ENet bot 用同样模拟参数跑了 3 分钟。每 10 秒记录的服务端平均 tick 耗时中位数，混合组为
2.64 ms（179 个窗口），纯 ENet 组为 1.52 ms（18 个窗口）。期间观测到的最大单 tick 分别为
7.09 ms 和 4.52 ms。

两种传输在共享模拟器的 200 ms / 1% 用例都按序交付了 128 KiB 可靠消息及前后的小消息。另用 UDP 代理对两种
传输各加单程 100 ms 延迟、按 1% 概率丢弃实际数据报。WebRTC 组丢弃 15/1723 个数据报，Chrome 重组了
34,952 字节的全量物资并收到 452 份有效快照。ENet 组丢弃 22/2370 个数据报，机器人完成 4 次拾取，
无拒绝、掉线。

### 7.2 信令：经 Master 转发，一次往返

浏览器页面走 HTTPS 时不能直接请求 DS 的明文 HTTP 端口，所以信令统一经 Master：

1. 浏览器拿到派遣结果（DS 标识、票据）后建立 PeerConnection 和两条 DataChannel，等本地候选收集
   完成，把完整的 offer 发给 Master：`POST /api/rtc/offer`（带登录 token、DS 标识和派遣票据）。
2. Master 把 offer 放进该 DS 的待处理队列；DS 用长轮询 `POST /ds/signal` 取走，生成 answer（带自己
   的公网候选）后回传。
3. Master 把 answer 作为第 1 步请求的回复返回。浏览器设置远端描述，连通性检查由浏览器向 DS 的公网
   地址发起，所以玩家在 NAT 后面也能连上，不需要 TURN。
4. DataChannel 打开后，内核发 `Hello`（协议版本、昵称、场景、票据），之后与桌面流程完全相同。

票据仍是进服的唯一凭证；信令请求只接受已登录账号、且只能指向 Master 刚派遣给它的 DS。

开发直连使用 `gnb ds --web`。DS 在回环地址监听信令 HTTP（默认 TCP 7781），WebRTC 使用单独的 UDP 端口
（默认 28015）。`gnb master serve --rtc-port-base <port>` 为每个 DS 分配 ENet 之外的 UDP 端口，
派遣结果通过 `rtcPort` 返回。DS 用自身 ID 和密钥轮询 `/ds/signal`，Master 校验账号、票据有效期和目标 DS。
信令交换期间票据仍由 DS 的游戏握手兑换。两条路径的本机复现见 [W1 联机验收页](../../../web/nextdayz/w1/README.md)。

### 7.3 Master 的改动

- 静态托管：`gnb master serve --web <dir>` 在同一个端口提供 Web 客户端与烘焙资产，页面与 API 同源，
  不需要跨域配置。资产文件名带哈希，可设长期缓存；也可以改由 CDN 提供，这时只给资产域开 CORS。
- 信令转发已在 W1 接入 `/api/rtc/offer` 与 `/ds/signal`，配置 WebRTC 端口段时启用。
- 版本清单：`/api/web/manifest` 返回当前协议版本、Web 发布构建标识与资产清单地址。Web 客户端启动时
  核对；派遣请求带上自己的构建标识，不匹配时 Master 拒绝并提示刷新页面。这同时补上多人设计 §4
  里"握手校验内容哈希"尚未落地的部分。
- 派遣结果增加 DS 的 WebRTC 端口；地图列表、登录、档案、出击接口不变。
- HTTPS：TLS 在 Master 前面的反向代理上终结。Master 自身与 DS → Master 的内部接口不变。
- 登录口令（D6）：`/api/login` 的请求增加 `password`。Master 以启动参数决定是否要求口令，**默认不
  要求**，本机开发、`gnb ds --master` 与机器人压测的行为不变；公网部署的启动脚本打开它。打开后：
  昵称第一次登录时设定口令（已有的无口令账号在下一次登录时认领），之后口令不符即拒绝；只存加盐的
  慢哈希，不存明文；同一来源连续失败要限速。桌面与 Web 的登录都经内核的联机状态机，所以口令输入
  只在两边界面各加一个输入框。认领无口令账号时撤销旧 token，设置口令后的普通登录沿用当前 token。

### 7.4 Web 客户端的联机流程

与桌面一致，先加载、后连接：

```
登录 → 档案/仓库 → 选图与带出 → 出击（地图 DS 启动中则每秒重问）
   → 就绪：DS 标识、WebRTC 端口、票据、场景
   → 按清单下载并装载该地图的烘焙资产（进度条；碰撞体在此阶段建好）
   → 信令（7.2）→ DataChannel 打开
   → Hello → Welcome → CharacterSpawn → LootSync → 进入世界
   → 30 Hz 上报自身状态，20 Hz 收快照，100 ms 插值
   → 撤离 / 死亡 / 返回大厅
```

这段流程在内核的 `FClientSession` 里，TS 只负责搬字节和显示进度。

### 7.5 浏览器特有的问题

| 问题 | 处理 |
| --- | --- |
| 标签页切到后台时渲染循环停止，客户端不再上报，DS 会判定掉线 | 页面不可见时用 Worker 里的定时器以低频继续驱动内核的联机部分并上报；角色表现为原地站立。超过一定时间不回来则主动离开并保存 |
| `Ctrl` 是默认的步行修饰键，`Ctrl+W` 会关闭标签页 | Web 版步行修饰键默认改到别的键（键位在表现层，可在设置里改）；游戏中关闭页面前弹确认；全屏时在支持的浏览器上锁定键盘 |
| `Esc` 会被浏览器用来退出指针锁定 | 把"指针锁定被解除"当作打开 Esc 菜单的信号，而不是去监听 `Esc` 键 |
| `Alt`（环视）、`Tab`、`F5` 等有浏览器默认行为 | 指针锁定期间拦截默认行为；拦不住的在 Web 版换默认键 |
| 鼠标加速 | 请求不带系统加速的原始移动量（浏览器不支持时退回普通移动量，灵敏度设置照常生效） |
| 音频需要用户手势才能启动 | 在"登录"或"出击"的点击里启动音频上下文 |

## 8. Web 表现层

### 8.1 渲染（three.js）

- **地图**：按块加载 GLB，同材质几何合并，重复件用实例化；视锥剔除按块。地形是烘焙出的网格。
- **光照与天空**：一盏方向光（太阳/月亮）加级联阴影，环境光与雾由内核给出的时间、天气状态和
  地图的 `env.json` 决定。昼夜与天气的**状态**来自服务器（与桌面一致），**长相**是 Web 自己的。
- **角色**：ScadRig 是刚体部件挂骨骼、没有蒙皮。每个部件是一个网格，其矩阵直接取自内核输出的骨骼
  矩阵数组，three.js 侧关闭自动矩阵更新。48 只感染者 × 十几个部件的量级，先用普通网格实现，
  性能不足时改为按部件种类实例化。
- **换色与服装**：可换色分段用每实例颜色；服装、武器、配件的显隐与挂点完全按内核给出的列表。
- **第一人称**：手臂与武器是单独的 rig，用独立视野渲染，避免穿进墙里。
- **特效**：曳光、枪口火光、弹着、血雾用简单的粒子与贴片，由表现事件驱动。

性能预算（验收值，在一台中档独显笔记本的 1080p 下）：稳定 60 fps；绘制调用不超过 1500；
首次进入一张 1 km 地图的下载量不超过 60 MB、从点击出击到可操作不超过 20 秒（宽带下）。W0 实测如下（macOS arm64，Chrome 157，本机无缓存 HTTP，1920×1080
浏览器视口，约 1248×648 绘图缓冲区）。就绪时间从地图资源请求开始，包含建体。帧间隔是
`requestAnimationFrame` 采样，随显示刷新率变化。

| 地图 | 渲染三角形 / 碰撞三角形 | GLB / 碰撞 | 就绪 / Jolt 建体 | 起点绘制 / 俯瞰绘制 | 起点帧间隔 / 俯瞰帧间隔 |
| --- | ---: | ---: | ---: | ---: | ---: |
| 训练场 v3 | 3,203,233 / 3,124,040 | 270.2 / 150.0 MB | 4.19 / 3.81 s | 1428 / 1680 | 17.3 / 16.7 ms |
| 洛克特 v8 | 4,481,804 / 4,162,240 | 379.0 / 199.8 MB | 4.46 / 3.84 s | 27 / 3699 | 16.4 / 16.7 ms |

**W0-3 实测（2026-11-18）**。桌面 `FScadLoader` 无窗口求值，导出渲染三角形数和求值统计一致，地图范围
分别为 800 m 和 1 km。Jolt 5.4.0 用 Emscripten 4.0.23 构建成单线程 WASM（约 1.08 MB），
`CharacterVirtual` 在两张图的烘焙网格上沿地面移动，自动化验证训练场大门旁的混凝土墙将其挡住。
本机 Homebrew Emscripten 7.0.0-git 的 libc++ 缺少该版 Jolt 用到的 `std::is_trivial`，所以原型固定到
4.0.23。

W0 把世界坐标顶点直接写入 GLB 和单个静态碰撞网格，洛克特约 579 MB，俯瞰约 3700 次绘制。
W2 使用共享碰撞网格、渲染实例、按材质批处理、meshopt 分块和 HTTP gzip。已发布地图实测如下。

| 地图 | 渲染三角形 / 碰撞三角形 | 地图文件 / 整张图 HTTP 传输 | 全图绘制 |
| --- | ---: | ---: | ---: |
| 训练场 v3 | 3,187,393 / 3,108,200 | 72.95 / 32.09 MB | 784 |
| 洛克特·河湾 v8 | 4,422,404 / 4,102,840 | 117.27 / 57.62 MB | 1,049 |

W2 采用桌面 `WorldAnchorRegistry` 的显隐结果，因此该表统计玩家可见和能参与查询的地图几何。
gzip 体积包含碰撞、地形和地图元数据。W4 的 20 秒首屏预算先预留 4 秒给碰撞构建，其余约 16 秒覆盖
下载、解析和首帧。

**W4–W5 实测（M3 Max，Chrome，1920×1080 视口）**。画质有两档："均衡"和"级联阴影"。

| 场景 | 均衡档绘制 | 级联阴影档绘制 | 帧间隔 |
| --- | ---: | ---: | --- |
| 洛克特全图 | 约 1,050 | — | — |
| 洛克特核心区 | 685 | 1,429 | 两档中位数约 16.7 ms |
| 训练场 48 只感染者固定队列（240 帧） | 434 | — | 中位数与 P95 均为 16.7 ms |

**现状**：级联阴影档已接近 1,500 次绘制的上限。预算规定的中档独显笔记本 1080p，以及 Firefox、Safari
的完整对局都还没测（W8-5），也还没有低画质档。内核以固定 1/60 s 步进，渲染没有插值，在 120/144 Hz
显示器上可能出现顿挫，需要实机确认。

### 8.2 界面（DOM）

用轻量的响应式界面库，状态完全来自内核的界面状态。需要的界面：

- 服务器选择与登录（Master 列表、昵称、开发用直连）；
- 大厅：档案、仓库、带出数量、地图选择、出击 / 回到野外、上次结果；
- HUD：身体状态、弹药、准星与命中标记、罗盘、交互提示、撤离倒计时、联机状态行；
- 背包：网格拖拽、旋转、装备槽、容器、配件、使用、丢弃、物品说明；
- 地图界面、死亡界面、Esc 菜单、设置（灵敏度、音量、键位、画质档位）。

文案来自烘焙导出的同一张翻译表。**现状**：只有物品名和内核给出的提示语查这张表。其余约 77 处界面文字仍是
`web/nextdayz/src` 里 `text("中文", "English")` 形式的写死字符串。

### 8.3 音频与输入

- 音频：WebAudio，定位声源用声像节点；播什么、在哪播由内核的表现事件决定，音效文件与桌面相同。
- 输入：指针锁定 + 键盘。表现层把按键翻译成内核的输入意图，默认键位与桌面一致，7.5 列出的冲突键除外。

## 9. 部署

```
公网
 ├─ 443/TCP  反向代理（TLS）──► Master :7780
 │                               ├─ /            Web 客户端静态文件
 │                               ├─ /assets/…    烘焙资产（或交给 CDN）
 │                               └─ /api/…       登录、档案、出击、信令、清单
 ├─ 27100+/UDP   各 DS 的 ENet 端口（桌面客户端）
 └─ 另一段 UDP   各 DS 的 WebRTC 端口（浏览器）
```

- 服务器包（`gnb package nextdayz-server`）增加 Web 客户端与烘焙资产，Master 启动脚本增加 `--web`。
- 内核按单线程构建，不使用共享内存线程，因此页面**不需要**跨源隔离响应头，静态托管没有特殊要求。
- DS 的运行条件不变（仍需 Vulkan 设备，软件 Vulkan 可用）。

**W8 部署验收**。Linux x86_64 服务器包内含静态站点、WASM 和已发布地图的烘焙资产。启动脚本默认开启
口令和 WebRTC。验收在一台干净的 Ubuntu 24.04 上进行：用 Lavapipe 启动包内的 Master 与 DS，宿主机
Chromium 经 HTTPS 代理登录、经 UDP 进入训练场，依次验证了口令、旧构建被拒、拾取、离场后回归、撤离入库。
本机的 ENet 桌面客户端与 Chromium WebRTC 客户端也进了同一个 Linux 训练场，双方能互相看见。后台标签页
保持 31 秒后仍在服；超过 120 秒，角色被保存并回到大厅。发布工作流会把包解到仓库外，再用浏览器复跑一遍。
操作见 [服务器包部署指南](../../guides/nextdayz-server-deployment.md)。

顺带的协同价值：内核与烘焙产物齐备之后，"无 GPU 的 DS"（多人设计里列为 MVP 之后的引擎级改造）
多了一条更短的路——用烘焙出的碰撞与锚点喂给不依赖引擎的模拟层。这不在本计划内，只在此记一笔。

## 10. 一致性验证

| 层次 | 验证手段 | 设计要求 | 现在在哪里运行 |
| --- | --- | --- | --- |
| 结构 | 内核包含检查；桌面 + WASM 双目标构建 | 每次提交 | `NextDayz Web` 工作流（`gnb web build`） |
| 协议 | 结构体布局断言；三平台编码字节金标 | 每次提交 | wasm32 与 Linux x86_64 在 `NextDayz Web`；Windows x86_64、macOS arm64 在 `Desktop CI` |
| 规则 | 同一组规则用例在原生与 WASM 各跑一遍（`NextDayzCoreRules`）；Web 客户端单测（`gnb web test`） | 每次提交 | `NextDayz Web` |
| 资产 | 烘焙统计与桌面加载统计比对；rig 金标姿态（同一 clip 同一时刻，桌面与 WASM 的骨骼矩阵误差在 1e-4 内）；地图版本与烘焙清单一致 | 每次提交 / 发布 | 清单一致性（`gnb web bake --check`）在发布工作流；rig 金标（`NextDayzRigPoseProbe`）手动 |
| 联机（无浏览器） | **Web 机器人**：在 Node 里运行 `ndcore.wasm`，经真实的 WebRTC 传输连 DS，行为与 `NextDayzBots` 相同，输出同样的 `RESULT ok|FAIL` | 每次提交跑短时；发布前 16 个 × 30 分钟，80 ms 延迟 + 2% 丢包 | 手动（`gnb web test --bots`）；需要带 Vulkan 的 DS，尚未进 CI |
| 联机（混合客户端） | 一个桌面客户端（`gnb validate`）加一个真实浏览器（自动化驱动页面，内核把与桌面相同的 `game.*` 查询暴露给页面）。复刻现有 `nextdayz-net-*` 场景：互见与互伤、抢物资、账本纠正、命中接受与拒绝、身体结算，以及 Master 的离开—回归—撤离 | 每日 / 发布前 | 手动脚本 `web/nextdayz/test/w7-*.mjs`；自动编排与每日任务是 W8-3 |
| 手感 | 人工对照清单：同一地点、同一装备，在桌面与 Web 各走一遍移动、翻越、开火、换弹、搜刮 | 里程碑验收 | 里程碑验收 |

**已有实测**。两个 rig 共 8,204 个骨骼矩阵，原生与 WASM 的最大误差约 4.2×10⁻⁷。16 个 Web 机器人在
80 ms 延迟、2% 丢包下跑满 30 分钟，全部撤离，共拾取 155 次、丢弃 18 次，协议拒绝 0 次。混合客户端用例
覆盖了：互伤关时桌面幸存者生命保持 100、打开后 Web 能击倒桌面幸存者、服务器收回客户端虚增的库存、
两个 Web 客户端抢同一件物品只有一人成功，以及撤离入库、死亡后重新出击。

## 11. 风险

| 风险 | 影响 | 对策 |
| --- | --- | --- |
| 内核拆分引入桌面回归 | 桌面版玩法或手感被改坏 | 逐子系统迁移；每步以现有单测与 agentscript 为闸；拆分期间 NextDayz 的 C++ 只允许一条工作线写入 |
| 拆分工作量被低估（`PlayerController`、`WeaponSystem`、`PlayerRigVisual` 与引擎调用缠绕较深） | 进度滑动 | W0 先拆协议与库存验证路线；计划里 3C 与战斗各自成段，可独立验收；个别子系统确实拆不动时停下交回核对，不自行改用 TS 重写 |
| WebRTC 库行为与预期不符（单端口复用、最大消息长度、受限网络） | 连接失败或需要更多端口 | W0 实测并回填；保留 WebSocket 回退传输的位置 |
| 浏览器内存与加载时间 | 低配机器进不了图 | 地图分块、几何与贴图压缩、进度反馈；W0 用实测校正预算 |
| 单线程内核占用主线程 | 掉帧 | 每帧只有一个本地角色的物理和几十个 rig 的姿态计算，预计几毫秒；碰撞体在加载阶段构建。实测超预算时再考虑把内核移入 Worker |
| 界面两份维护 | 桌面加了面板，Web 没有 | 界面状态由内核统一给出，Web 缺的面板在联调清单里可见；发布检查包含界面对照 |
| 昵称即身份在公网上被冒用 | 仓库被他人动用 | 登录口令（D6、7.3），已在 W8-0 实施，部署包默认开启 |
| 协议升级时桌面、Web、DS 版本错位 | 进服被拒或行为异常 | 版本清单与构建标识核对（7.3）；Web 客户端随服务器包一起发布，天然同版本 |

## 12. 给执行者的规则

1. 不修改 `NetProtocol` 的线上字节来迁就 Web。浏览器传输需要的额外信息（序号、ping、分块）一律
   放在 NextNet 传输层，游戏层看不见。确需升级协议时按现有规矩提升 `kProtocolVersion`，桌面与
   Web 同时升级。
2. 玩法规则只写在内核里。Web 侧发现缺一个数值或判定时，去内核加输出，不在 TS 里补。
3. SCAD 语义只在 ScadLoader 里。烘焙工具只做导出，不"修正"几何；TS 代码不解析 `.scad`，浏览器内求值
   （D2 的试验）只能调用编成 WASM 的同一份 ScadLoader。
4. 内核的每一步拆分都必须让桌面版保持可构建、可通过现有 NextDayz 单测与联机 agentscript。
5. 标注"需验证"的外部库行为，先做最小实验再写正式实现，并把结论回填本文。
6. 构建与验证遵守仓库既有约定：只构建受影响的目标；`gnb build` 串行；不改 `ThirdParty/`。
