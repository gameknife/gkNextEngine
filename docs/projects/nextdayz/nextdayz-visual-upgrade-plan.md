---
title: "NextDayz 皮克斯感 Low-Poly 视觉升级实施计划"
category: plan
status: 未开始（V0–V5 待实施）
owner: NextDayz
created: 2026-11-26
last_updated: 2026-11-26
---

# NextDayz 皮克斯感 Low-Poly 视觉升级实施计划

配套设计：[NextDayz 皮克斯感 Low-Poly 视觉升级设计](nextdayz-visual-upgrade-design.md)。
本文按 `AGENTS.md` 工作流只排**未完成任务的执行顺序、文件边界与验收闸门**。每完成一步单独提交（提交信息带任务编号，如 `nextdayz: add lookdev diorama scene (V0-1)`）并更新本文进度；全部完成后将长期契约并入 `docs/AGENT_GUIDE/SCADLoader.md` 与 `nextdayz-visual-upgrade-design.md`，再删除本计划。

## 执行原则

- **样板先行，肉眼闭环**：所有底层几何、BSDF 与资产改动先在 `V0-1` 的微缩样板间用 `gnb.bat shot` 同机位截图对比 Before/After，确认质感达标后再铺开到全库与 1 km 地图。
- **最小化构建**：只改 SCAD 资产时运行 `gnb.bat build NextDayz`（或 `ScadCatalog`）同步运行目录；改到 `Modules/ScadLoader` 或 Engine 着色器时运行 `gnb.bat build gkNextRenderer gkNextUnitTests NextDayz ScadCatalog`；严禁并发启动多个 `gnb` 构建。
- **严守玩法与单测契约**：不得破坏建筑 `L × D` 占地、前门 `-Y` 轴线净空、每节点/每骨骼部件 16 材质槽上限、角色 `bone_*` 枢轴以及 `[Scad]` 调制纹理单测守卫（均值 > 160、标准差 < 40）。

## 进度总览

| 阶段 | 任务编号 | 状态 | 核心产出 |
| :--- | :--- | :--- | :--- |
| P0 视觉标杆切片 | `V0-1` | 未开始 | 40m×40m 全要素 Lookdev 微缩样板间与 4 机位基准图 |
| P1 SCAD 与着色底座 | `V1-1` ～ `V1-3` | 未开始 | 圆角/扫掠/法线原语、SSS/Sheen/Foliage 着色、烘焙顶点 AO 与宏尺度打散 |
| P2 电影光影与大厅 | `V2-1` ～ `V2-2` | 未开始 | 透光窗户点亮室内、户外冷暖色调调校、直升机机舱大厅电影级重塑 |
| P3 角色与武器 2.0 | `V3-1` ～ `V3-4` | 未开始 | 幸存者五官眼神光与 SSS 皮肤、圆角绒光服装、感染者放样重写、武器捕光边 |
| P4 植被与草海 2.0 | `V4-1` ～ `V4-3` | 未开始 | 蓬松透光地域树种、柔弧融合且随地域变色的运行时草地、画意过程纹理 |
| P5 建筑套件与地图 | `V5-1` ～ `V5-3` | 未开始 | 波希米亚/稀树草原/冷战/训练场建筑套件升级，三张地图重导出与回归验收 |

---

## P0：建立视觉标杆微缩样板间（Vertical Slice）

### `V0-1` 搭建 `nextdayz_pixar_lookdev.scad` 评测基准场
- **范围**：新建 `assets/scad/source/others/nextdayz_pixar_lookdev.scad`（只引用现有库，不改 C++）。
- **内容**：在 `40m × 40m` 场地上同框布置：1 名穿戴外套/背心/背包/持枪的幸存者静态展示架、1 只感染者、1 栋带室内的波希米亚农舍（`nd_bh_building`）、2 棵代表性树木（阔叶椴树 + 云杉）、一段碎石土路、弃车、木箱物资与武器陈列台；用 `gk_camera_lookat` 埋入 4 个固定机位：`portrait`（角色半身肖像）、`street`（街景建筑与树）、`interior`（室内门窗朝向）、`diorama`（45° 微缩全景）。
- **完成与闸门**：
  - `gnb.bat build ScadCatalog && gnb.bat scad catalog`（0 warning）；
  - 镜像文件至 `out/build/windows/assets/scad/` 后执行 `gnb.bat shot --scene assets/scad/source/others/nextdayz_pixar_lookdev.scad --frames 60`，确认 4 个机位均可无报错渲染，作为后续全部阶段的 Before 基准。

---

## P1：SCAD 雕塑几何与皮克斯触感着色底座

### `V1-1` 实现 `ScadLoader` 雕塑几何与法线控制原语
- **范围**：`src/Modules/ScadLoader/{FScadGeometry.*,FScadEvaluator.Detail.h,FScadEvaluator.Geometry.cpp,FScadLoader.cpp,FScadRig.cpp,FScadShared.*}`、`src/Tests/Test_Scad*.cpp`。
- **内容**：
  1. 实现内置变量 `$gk_detail = 1.0`；
  2. 实现 `gk_round_box(size, r, segs, power, center)` 原生网格生成（带连续圆角法线与半径安全钳制）；
  3. 实现 `gk_sweep(points, radii, fn, cap, twist)` Catmull-Rom + Parallel Transport 最小扭转标架扫掠；
  4. 将 `SceneMeshBucket::faceted` 扩展为作用域可传递的 `gk_crease(angleDeg)`（兼容 `FScadLoader` 与 `FScadRig`），并实现 `gk_puff_normals(strength, upBias)` 蓬松团块法线柔化。
- **完成与闸门**：
  - `gnb.bat build gkNextRenderer gkNextUnitTests`；
  - `out/build/windows/bin/gkNextUnitTests.exe "[Scad]"` 与 `"[ScadRig]"` 全部通过（补齐新原语水密性、法线连续性与退化参数单测）；
  - 在样板间局部替换测试块，`gnb.bat shot` 验证圆角高光连续无黑面。

### `V1-2` 扩展 `gk_material`（`subsurface` / `sheen` / `ior`）与 `gk_foliage`
- **范围**：`src/Engine/Assets/Data/Material.hpp`、`src/Modules/ScadLoader/{FScadEvaluator.*,FScadLoader.cpp,FScadRig.cpp}`、`src/Engine/Runtime/Scene/SceneBuilder.*`、`assets/shaders/common/{BSDF.slang,Lighting.slang,SwModernShading.slang,SwTracingShading.slang,NoAmbientShading.slang,PathTracingLiteShading.slang}`。
- **内容**：
  1. 严守 `sizeof(Assets::Material) == 64`，按设计 §3.2 接入 `gk_foliage(c, transmission, thickness)` 及 `gk_material` 的 `ior`、`subsurface`、`sheen` 参数（同时支持普通场景 Node 与 `ScadRig` 部件材质）；
  2. 在 `BSDF.slang` 与直接光/环境光路径统一实现能量守恒的次表面暖色包裹漫反射与掠射织物绒光。
- **完成与闸门**：
  - `gnb.bat build gkNextRenderer gkNextUnitTests NextDayz`；
  - `out/build/windows/bin/gkNextUnitTests.exe "[Scad]"` 通过；
  - `gnb.bat validate --all --tag smoke` 全部通过（确认 Lavapipe / 各渲染器路径着色编译与运行正常）；
  - 样板间验证皮肤暖色明暗交界线、布料轮廓绒光与树冠逆光透射生效。

### `V1-3` 实现烘焙顶点腔体/接地 AO 与世界坐标低频打散
- **范围**：`src/Engine/Assets/Data/Vertex.hpp`、`src/Modules/ScadLoader/{FScadShared.*,FScadLoader.cpp,FScadRig.cpp}`、`assets/shaders/common/{BasicTypes.slang,SceneSampling.slang,BSDF.slang}`。
- **内容**：
  1. 在 `GPUVertex::tangentw` 空闲高 6 位编码逐顶点遮蔽（`0` = 无遮蔽默认 `1.0`，保证非 SCAD 模型零影响），在 `ScadLoader` 增加快速腔体凹陷度 + 局部接地暗角计算及 `gk_ao(strength, ground)` 控制；
  2. 对带过程纹理的非角色静态表面叠加世界空间双频微色温/明度打散，消除大平面 2–3m 周期重复感。
- **完成与闸门**：
  - `gnb.bat build gkNextRenderer gkNextUnitTests NextDayz`；
  - `out/build/windows/bin/gkNextUnitTests.exe "[Scad]"` 与 `gnb.bat validate --all --tag smoke` 通过；
  - `gnb.bat shot --scene assets/scad/source/others/nextdayz_pixar_lookdev.scad` 确认墙根、窗套凹槽与衣袋边缘出现柔和体积暗角，且 glTF 场景 `gnb.bat shot --scene assets/models/playground.glb` 外观无退化。

---

## P2：电影级光影、透光窗户与机舱大厅重塑

### `V2-1` 实现透光窗户玻璃与昼夜冷暖色彩脚本调校
- **范围**：`src/Modules/ScadLoader/{FScadEvaluator.*,FScadLoader.cpp}`、`assets/scad/lib/kit_{bohemia,coldwar,savanna,training}.scad`（玻璃材质包装）、`src/Application/Game/NextDayz/{NextDayzConfig.hpp,World/TimeSystem.cpp}`。
- **内容**：
  1. 实现 `gk_material(..., shadow = false)`（拆入 `__glass` 子节点，关闭阴影投射与 GI 遮挡但保留主视线高光反射），应用到各 kit 窗户玻璃；
  2. 调优 `TimeSystem` 与曝光响应：优化晴天暖金阳光与清透群青天光阴影的色温与强度比，抑制正午浅色墙面死白过曝，并确保推门进入带窗建筑时室内家具、墙裙与地面斜射光斑清晰通透。
- **完成与闸门**：
  - `gnb.bat build NextDayz gkNextUnitTests`；
  - `gnb.bat validate --script assets/agentscripts/nextdayz-loket-entrance.agentscript.json` 与 `nextdayz-weather.agentscript.json` 通过；检查生成的 `loket-palace-interior` 与 `loket-castle-courtyard` 截图，确认室内告别死黑、立面高光柔和不刺眼。

### `V2-2` 重塑直升机机舱大厅（几何圆角化 + 电影三点布光）
- **范围**：`assets/scad/scenes/nextdayz_helicopter_lobby.scad`、`src/Application/Game/NextDayz/NextDayzLobby.cpp`（默认 `cabinLights_` / `cabinEnv_`）。
- **内容**：
  1. 将机舱隔音软包、座椅帆布与管架、舱门框、灭火器与电台箱全面升级为 `gk_round_box` / `gk_sweep` 圆角构件与 `sheen` 帆布材质；
  2. 重新设计默认机舱三点电影布光（敞门暖阳主光 + 舷窗冷天光轮廓光 + 舱顶柔光补光 + 眼部反光板），消除角色面部欠曝与暗部噪点。
- **完成与闸门**：
  - `gnb.bat build NextDayz`；
  - `gnb.bat validate --script assets/agentscripts/nextdayz-lobby-look.agentscript.json` 与 `nextdayz-helicopter-lobby.agentscript.json` 通过，肉眼检查大厅截图达到主视觉品质。

---

## P3：角色、服装、感染者与武器 2.0

### `V3-1` 重塑幸存者身体与五官（`nextdayz_body.scad` 2.0）
- **范围**：`assets/scad/lib/rig_forms.scad`、`assets/scad/characters/{nextdayz_body.scad,nextdayz_survivor.scad,nextdayz_fps_hands.scad}`、`src/Tests/Test_ScadRig.cpp`（如裸体三角面预算阈值需随高细节放宽，同步更新单测契约注释）。
- **内容**：
  1. 升级头颅与五官：采用 `m = 16`、`gk_crease(55)` 柔和头型，制作微凹暖白眼白 + 深色虹膜 + 高光角膜三层眼睛（捕捉眼神光），柔和化眉弓、鼻头与耳廓，皮肤接入 `subsurface`；
  2. 肢体与手部放样升级至 `m = 12..14` 圆滑截面，第一人称手臂同步受益；保持骨架枢轴与手掌武器握持坐标系不变。
- **完成与闸门**：
  - `gnb.bat build NextDayz gkNextUnitTests`；
  - `out/build/windows/bin/gkNextUnitTests.exe "[ScadRig]"`、`"NextDayz survivor gaits plant the boots and holds keep both hands on the rifle"` 通过；
  - `gnb.bat validate --script assets/agentscripts/nextdayz-lobby-look.agentscript.json` 与 `nextdayz-ads-transition.agentscript.json` 截图验收面部亲和力与手部握持。

### `V3-2` 重塑基础与史实服装库（`nextdayz_clothing*.scad` 2.0）
- **范围**：`assets/scad/characters/{nextdayz_body.scad,nextdayz_clothing.scad,nextdayz_clothing_heritage.scad,nextdayz_loot_items.scad}`、`src/Application/Game/NextDayz/Player/PlayerRigVisual.cpp`（必要时微调包络间隙）。
- **内容**：
  1. 将 `ndb_shell_coat / sleeve / leg / seat` 升级为富有曲线张力的阔肩收腰下摆、圆滑袖裤（`m = 14`）与靴口堆叠轮廓；
  2. 为 `nextdayz_clothing.scad` 全部基础款补齐 `gk_material`（织物 `sheen`、防雨布微光泽、皮革柔光、反光条与金属扣件），并将两套服装库及地面 loot 的硬切角口袋、防弹衣、头盔、护耳帽、背包全部升级为 `gk_round_box` / `gk_sweep` 圆角造型（每部件严守 ≤ 16 材质段）。
- **完成与闸门**：
  - `gnb.bat build NextDayz gkNextUnitTests`；
  - `gnb.bat validate --script assets/agentscripts/nextdayz-clothing-variety.agentscript.json`（193 步全服装穿戴切换）、`nextdayz-heritage-kit.agentscript.json`、`nextdayz-inventory-icons.agentscript.json` 全部通过并抽查截图无穿模。

### `V3-3` 放样化重写感染者（`nextdayz_infected.scad` 2.0）
- **范围**：`assets/scad/characters/nextdayz_infected.scad`。
- **内容**：保持骨架枢轴与全部动作生成代码不变，用 `rig_forms` + `gk_round_box` + `gk_material`（病态冷肤色微 SSS、织物 `sheen`、`ROLECOLOR` 染色破损衬衫）彻底替换全身裸 `cube()`，强化不对称佝偻剪影、肋骨伤痕与利爪手型。
- **完成与闸门**：
  - `gnb.bat build NextDayz gkNextUnitTests`；
  - `out/build/windows/bin/gkNextUnitTests.exe "NextDayz infected gaits plant the feet and actions stay on the ground"` 通过；
  - `gnb.bat validate --script assets/agentscripts/nextdayz-zombie-combat.agentscript.json` 通过并检查截图。

### `V3-4` 武器与配件圆角捕光打磨
- **范围**：`assets/scad/lib/{kit_nd_arsenal.scad,kit_coldwar.scad}`（武器段）、`assets/scad/characters/nextdayz_attachments.scad`。
- **内容**：将机匣、护木、弹匣、瞄具外壳的关键硬棱升级为 `gk_round_box` 捕光圆边，微调发蓝/磷化钢的粗糙度与基础明度（防止背光死黑），强化胡桃木/层压木护木的温润清漆层次，严守 `WeaponDefs.hpp` 的枪口/瞄准线坐标与 16 材质槽上限。
- **完成与闸门**：
  - `gnb.bat build NextDayz`；
  - `gnb.bat shot --scene assets/scad/source/others/nextdayz_heritage_showroom.scad`、`gnb.bat shot --scene assets/scad/source/others/nextdayz_weapons_showroom.scad` 与 `gnb.bat validate --script assets/agentscripts/nextdayz-ads-sight.agentscript.json` 通过。

---

## P4：自然生态、运行时草海与画意纹理 2.0

### `V4-1` 重塑各地域树木与灌木（蓬松透光树冠 + 扫掠树干）
- **范围**：`assets/scad/lib/kit_{bohemia,savanna,coldwar,hokkaido,training}.scad` 的 `*_nature_*` 模块。
- **内容**：用 `gk_sweep` 制作根部抓地外扩、自然弯曲分枝的树干；树冠改用多层级超椭球团块组合，套用 `gk_puff_normals` + `gk_foliage` 透光与“底冷深 → 顶暖亮”色阶，彻底消除多面球穿插黑缝与纸片圆锥感。
- **完成与闸门**：
  - `gnb.bat build ScadCatalog NextDayz && gnb.bat scad catalog`（0 warning，检查树木 `zMin` 与 `footprint` 合理）；
  - 在 `nextdayz_pixar_lookdev.scad`、`savanna_showcase.scad`、`coldwar_showcase.scad` 中 `gnb.bat shot` 验收顺光与逆光树冠质感。

### `V4-2` 升级运行时草地造型、法线融合与地域色板联动
- **范围**：`src/Engine/Runtime/Systems/`（`FTerrainGrassSystem` 实现文件）、`src/Engine/Runtime/Components/TerrainComponent.hpp`、`src/Modules/ScadLoader/FScadTerrain.*`。
- **内容**：
  1. 将草叶网格从笔直三棱刺升级为微弯柔弧叶型，并将草叶顶点法线向地表法线混合约 `0.55`，消除近景草丛黑斑噪点，实现皮克斯/塞尔达式的绒毯接地融合；
  2. 将地形 `palette` 类型暴露给 `FTerrainGrassSystem`，为 `continental`（温润中欧草绿）、`savanna`（暖金草原色）、`temperate` 自动匹配对应草色。
- **完成与闸门**：
  - `gnb.bat build gkNextRenderer gkNextUnitTests NextDayz`；
  - `out/build/windows/bin/gkNextUnitTests.exe "[TerrainGrass]"` 通过；
  - `gnb.bat validate --script assets/agentscripts/nextdayz-grass.agentscript.json` 与 `nextdayz-grass-lighting.agentscript.json` 通过，且 `nextdayz-talek-bridge` 截图确认稀树草原草色与金色地表和谐统一。

### `V4-3` 画意化重构 `gk_texgraph.scad` 建筑与地表印花
- **范围**：`assets/scad/lib/gk_texgraph.scad`、`assets/scad/source/others/gk_texture_patterns.scad`。
- **内容**：降低 `tex_plaster`、`tex_dirt`、`tex_asphalt` 的高频碎坑对比与法线倾角；提升 `tex_tiles`、`tex_brick`、`tex_stone`、`tex_planks` 的逐块手绘暖冷微色差与柔和边缘；为稀树草原泥墙补充专属柔和泥抹印花 `tex_mudplaster`。
- **完成与闸门**：
  - `gnb.bat build gkNextUnitTests NextDayz`；
  - `out/build/windows/bin/gkNextUnitTests.exe "[Scad]"` 全部通过（确保均值/标准差与无缝守卫不破）；
  - `gnb.bat shot --scene assets/scad/source/others/gk_texture_patterns.scad` 验收。

---

## P5：地域建筑套件升级与全地图重烘焙验收

### `V5-1` 升级 `kit_bohemia` 与 `kit_savanna` 地域建筑与小品
- **范围**：`assets/scad/lib/{kit_bohemia.scad,kit_savanna.scad}`。
- **内容**：加厚檐口与山墙封边、为屋脊引入柔和微垂与立体圆角脊瓦、将门窗套/窗台/烟囱/石拱门/围墙柱/街具小品换用 `gk_round_box` 圆角件并接入温和 `ior = 1.20` 陶土抹灰微光泽；严守建筑 `L × D` 与入口净空。
- **完成与闸门**：
  - `gnb.bat build ScadCatalog && gnb.bat scad catalog`（0 warning）；
  - `gnb.bat shot --scene assets/scad/source/savanna_showcase.scad` 与 `nextdayz_pixar_lookdev.scad` 验收。

### `V5-2` 升级 `kit_coldwar` 与 `kit_training` 建筑、家具与载具
- **范围**：`assets/scad/lib/{kit_coldwar.scad,kit_training.scad}`。
- **内容**：将室内外家具（桌椅、床铺、沙发、货架、铁柜）、路边弃车/卡车/BTR 外壳、训练场营房与掩体全面升级为 `gk_round_box` / `gk_sweep` 圆角饱满体块与柔和车漆/织物材质；严守 `nd_table_top` 等物资刷新点高度契约。
- **完成与闸门**：
  - `gnb.bat build ScadCatalog && gnb.bat scad catalog`（0 warning）；
  - `gnb.bat shot --scene assets/scad/source/coldwar_showcase.scad` 与 `training_showcase.scad` 验收。

### `V5-3` 重导出三张地图、全量回归与版本发布
- **范围**：`assets/scad/specs/nextdayz/{training_ground,cz_loket_riverbend,ke_talek_mara}.json`、`assets/scad/proc/nextdayz/**`、`assets/configs/nextdayz_maps.json`。
- **内容**：
  1. 用 `gnb.bat geo expedition` 重新导出 `training_ground`、`cz_loket_riverbend` 与 `ke_talek_mara` 草稿，检查解析耗时与三角面统计；
  2. 用 `python tools/nextdayz/geomap_shots.py` 导出总览、鸟瞰与街景截图逐张复核；
  3. 递增 `training_ground` 与 `cz_loket_riverbend` 的配方 `version` 并执行 `--publish` 冻结新快照；若 Web 预烘焙需要同步，运行对应烘焙检查。
- **完成与闸门**：
  - `gnb.bat build NextDayz gkNextUnitTests NextDayzBots`；
  - `out/build/windows/bin/gkNextUnitTests.exe "[NextDayz]"` 与 `"[NextDayz][Landscape]"` 全部通过；
  - `python tools/nextdayz/check_region_upgrade.py --map cz_loket_riverbend` 通过；
  - 跑通核心交互回归：`nextdayz-smoke`、`nextdayz-training-ground`、`nextdayz-loket-bridge`、`nextdayz-loket-entrance`、`nextdayz-talek-bridge`、`nextdayz-extraction`。
