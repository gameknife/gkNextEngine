---
title: "NextDayz 皮克斯感 Low-Poly 视觉升级设计"
category: design
status: 待实施（配套执行计划见 nextdayz-visual-upgrade-plan.md）
owner: NextDayz
created: 2026-11-26
last_updated: 2026-11-26
---

# NextDayz 皮克斯感 Low-Poly 视觉升级设计

配套执行计划：[NextDayz 视觉升级实施计划](nextdayz-visual-upgrade-plan.md)。
关联文档：[SCAD 加载器](../../AGENT_GUIDE/SCADLoader.md) · [ScadRig](../../AGENT_GUIDE/ScadRig.md) · [SCAD 资产手册](../../AGENT_GUIDE/ScadAssetPlaybook.md) · [世界派遣与地理关卡](nextdayz-world-map-design.md) · [运行时 biome 草地](../../designs/runtime-terrain-grass.md)。

## 1. 背景与问题诊断

NextDayz 的全部资产（地图、地形、建筑、角色、动作、服装、武器、物资）均基于 SCAD 构建。经过此前“引入 `gk_material` PBR 分层、`gk_texture` 过程纹理节点图、`rig_forms` 角色截面放样”几轮迭代后，画面整体美感依然不理想。结合实机渲染采样与源码审计，核心症结可归纳为**“CAD 硬棱几何 + 高频平铺写实噪点 + 二元极端着色”**的系统性错位：

### 1.1 SCAD 与渲染基础设施层的四个瓶颈

1. **全局固定 35° 法线折痕阈值与“45° 假倒角”死锁**
   - `FScadLoader.cpp`、`FScadRig.cpp` 与 `FScadShared.cpp` 对所有非地形几何统一使用 `smoothAngleDegrees = 35.0f`，SCAD 侧无法按模块控制法线平滑角。
   - `minkowski` 在当前求值器中仅近似为 `union`。资产大量使用三立方体 `hull()`（`rf_box` / `ndc_box`）制作倒角盒，但单段切角面与主平面的夹角恰好是 **45°（> 35°）**，导致所有倒角被渲染成硬折面，抓不住连续高光；同理，`$fn = 8` 或 `m = 8` 的肢体、圆柱与球体（相邻面 45° > 35°）全部退化为生硬的八棱柱与多面球。
2. **缺少高表现力的原生圆角与曲线扫掠原语**
   - `FScadGeometry` 仅有基础 `cube` / `sphere` / `cylinder` / `polyhedron` 与线性/旋转拉伸。在脚本中靠球体 `hull()` 拼圆角或弯管，不仅会产生扭曲碎面与法线伪影，还会带来不必要的 Manifold CSG 开销。
3. **着色模型二元化：缺失次表面包裹（Subsurface）、织物绒光（Sheen）与 SCAD 树冠透光**
   - `Assets::Material` 与 `BSDF.slang` 仅向 SCAD 暴露了纯漫反射 `Lambertian` 与固定 `IOR = 1.45`（约 3.4% 镜面反射）的 `Mixture`。
   - 由于 `Mixture` 会让布料、抹灰、皮肤像裹了一层塑料膜，现有资产库被迫将皮肤、织物、墙体、砖石、瓦片、树叶全部退回 `roughness = 1` 的纯粉笔 `Lambertian`，导致角色面部明暗交界线死灰、服装缺乏织物轮廓绒光。
   - 引擎虽已为运行时草地实现了 `Material::Foliage`（主表面直接光背面透射），但未桥接给 `ScadLoader`，导致所有 SCAD 树木完全不透光。
4. **纯色分桶缺少低频体积渐变与接触环境光遮蔽（AO）**
   - `FScadEvaluator` 按量化 RGBA 严格拆桶，`Vertex` 也未携带逐顶点遮蔽/色温调制；而 `ProceduralTexture` 生成的 256×256 无 Mipmap 盒投影贴图只能提供高频图案，在大立面与长土路上呈现每 2–3 米一次的网格重复感，缺乏墙根接地暗角、屋檐压暗与衣袋接缝遮蔽。

### 1.2 NextDayz 现有资产与光影层的五个短板

1. **角色与感染者缺乏皮克斯式亲和力（Appeal）**：`nextdayz_body.scad` 为兼容旧版方块服装的口袋坐标，`ndb_shell_coat`（`n = 10`）与 `ndb_shell_leg`（`m = 8`）仍保持僵直方盒与八棱柱廓形；面部用硬块堆出凸起眉弓，眼睛是无眼白、无角膜高光的纯黑窄缝，顶光下眼窝呈骷髅式死黑。`nextdayz_clothing.scad` 基础款仍以裸 `color()` 为主；`nextdayz_infected.scad` 更是 100% 停留在裸 `cube()` 方块阶段。
2. **建筑体块单薄、贴图平铺感重、室内全黑**：地域建筑考据扎实，但墙角、门窗套、屋面板多为 90° 直角薄片；`tex_plaster` 剥落坑对比过硬且周期重复；窗户玻璃（`bh_m_glass` / `cw_m_glass`）为不透明实体板，彻底阻断日照与天光，导致步入室内几乎全黑（RGB 接近 0）。
3. **树木与运行时草地割裂**：各 kit 的树木由 `$fn = 5..8` 的硬棱圆锥与球体拼插、用裸 `color()` 渲染，球体间自阴影生硬；`FTerrainGrassSystem` 草叶为笔直尖刺三棱锥，近景暗部噪点重，且全局固定中欧冷灰绿，与塔勒克金色稀树草原脱节。
4. **机舱大厅欠曝嘈杂，户外高光泛白**：主菜单机舱大厅整体欠曝发灰、皮肤降噪斑驳；户外晴天直射光过强，浅色墙面与树冠顶部易褪色发白。

---

## 2. 美术与技术总方向：雕塑感触觉微缩风（Sculpted Tactile Miniature）

目标是在**不改变 SCAD 程序化工作流、不拘泥于三角面数量限制**的前提下，把视觉语言从“CAD 工程块 + 写实脏迹”转向类似皮克斯动画电影（《Luca》《Up》《Toy Story 4》微缩景观）与《Overwatch》《Tunic》的**雕塑感触觉微缩风**：

1. **大面干净概括，转折奢侈捕光（70 / 25 / 5 法则）**：70% 面积保持干净利落的低模雕塑切面；25% 预算投入到 3–4 段平滑圆倒角（Catchlight Bevels）与富有张力的剪影曲线上，让光追 GI 在每条棱边滚出温润亮线；仅留 5% 给极低对比度的画意笔触肌理。
2. **体块节奏与曲直对比（Straight vs. Curve）**：杜绝纯正交盒子——屋脊带 2%–3% 自然垂弧、檐口加厚、烟囱与石墙微收分；角色采用 **1:6.8 硬朗动画英雄比例**（保留现有骨架枢轴，重塑阔肩收腰 A/V 剪影、裤脚堆叠与鲜活眼神光）。
3. **触感优先于噪点**：用皮肤暖色次表面包裹（SSS）、织物掠射绒光（Sheen）、陶土/木蜡低折射率微光泽（Soft IOR）和树冠逆光透射（Foliage）塑造“想伸手触摸的材质感”，用烘焙顶点腔体/接地 AO 取代高频破损噪点。
4. **寂静废土的通透电影光影（Color Script）**：保留撤离生存题材的克制叙事，拒绝糖果色卡通化；通过“暖金阳光 + 澄澈群青天光冷阴影 + 透光窗户斜射入室的暖光斑”建立戏剧化但通透干净的光影层次。

---

## 3. 核心架构与接口设计契约

### 3.1 `ScadLoader` 雕塑几何与法线扩展

在 `src/Modules/ScadLoader/` 中新增四个原生 `gk_` 语言扩展（桌面端默认高细节；通过求值器内置变量 `$gk_detail = 1.0` 支持 Web WASM 或低配烘焙时按比例缩减圆角与曲线段数）：

1. **`gk_round_box(size, r = 0.02, segs = 3, power = 2.0, center = true)`**
   - 在 `FScadGeometry` 直接生成圆角盒/超椭球封闭三角网（无需经过 Manifold `hull()`）：平坦主面与 `segs` 段圆弧过渡带精确共顶点，法线在圆角带连续插值、与主平面相切。
   - 半径自动钳制 `r <= 0.49 * min(size.x, size.y, size.z)`；`segs` 随 `$gk_detail` 缩放（最低 `1`）。
2. **`gk_sweep(points, radii, fn = 12, cap = "round", twist = 0)`**
   - 输入 3D 控制点列 `points`（≥ 2 点）与标量或等长半径列 `radii`，内部按弦长参数化采样 Catmull-Rom 样条，并使用 **Parallel Transport（最小扭转 Bishop 标架）** 沿曲线推进截面（严禁用会在拐点翻转 180° 的朴素 Frenet 标架）。
   - `cap` 支持 `"none"` / `"flat"` / `"round"`（半球封顶），用于有机弯曲树干、树根外扩、下垂屋脊、织带管线与衣领卷边。
3. **`gk_crease(angleDeg) { ... }`**
   - 作用域修饰器：将 `SceneMeshBucket::faceted` 泛化为 `float smoothAngleDegrees = -1.0f`（`< 0` 沿用加载器默认 35°，`0` 为纯硬边切面，`> 0` 覆盖平滑阈值），在 `FScadLoader` 与 `FScadRig` 两端一致生效。
4. **`gk_puff_normals(strength = 0.65, upBias = 0.25) { ... }`**
   - 作用域修饰器：在该子树生成的网格合并后，计算局部包围盒中心 `C` 与半径 `R`，将顶点渲染法线按 `normalize(mix(N, normalize((P - C) + vec3(0, upBias * R, 0)), strength))` 柔化，使多球/多面体组合的树冠或灌木呈现浑然一体的蓬松受光体积，消除内部球体穿插的黑缝。

### 3.2 零布局膨胀的触感材质与 BSDF 扩展

严守 `GPU_SCENE_MATERIAL_SIZE = 64`（`Material.hpp` 与 `BasicTypes.slang` 布局不变），扩展 SCAD 材质语法与着色器：

| SCAD 接口与参数 | `Assets::Material` 映射字段 | 渲染器语义（PathTracing / SwTracing / SwModern 全路径兼容） |
| :--- | :--- | :--- |
| `gk_foliage(c, transmission = 0.65, thickness = 0.10)` | `Material::Foliage`（`Lambertian` + `Reserved2 = transmission`, `Reserved3 = thickness`）+ `NormalTextureId` | 复用引擎已落地的 `DirectIlluminatePrimary` 叶片背面直接光漫透射，使所有 SCAD 树木、灌丛、树篱具备逆光通透感。 |
| `gk_material(..., ior = 1.45)` | `Mixture` 的 `RefractionIndex = clamp(ior, 1.0, 2.0)` | 控制非金属菲涅尔 `F0 = ((ior-1)/(ior+1))^2`。抹灰/陶土/风化木用 `ior = 1.18~1.25`（`F0 ≈ 0.7%~1.2%`）获得温润微光泽而不泛塑料白光；`ior = 1.0` 时关闭镜面反射，仅启用下述漫反射扩展。 |
| `gk_material(..., subsurface = 0..1)` | `Mixture` 的 `Reserved2 = clamp(subsurface, 0, 1)` | 能量守恒的次表面包裹漫反射：`NoL_wrap = saturate((NoL + w) / ((1 + w) * (1 + w)))`（`w = 0.45 * subsurface`），并在明暗交界带叠加与反照率相乘的暖红血色偏移，用于角色皮肤、耳鼻、蜡质与温润木件。 |
| `gk_material(..., sheen = 0..1)` | `Mixture` 的 `Reserved3 = clamp(sheen, 0, 1)` | 掠射角织物微纤绒光：叠加 `sheen * 0.25 * mix(vec3(1), baseColor, 0.5) * pow(1 - NoV, 4) * saturate(NoL + 0.3)`，正面保持纯亚光，轮廓边缘泛起柔和布料绒光，用于外套、裤装、背包与帆布座椅。 |
| `gk_material(..., shadow = false)` | 拆分为独立 `__glass` 子桶，设 `RenderComponent::SetCastShadows(false)` 并跳过 GI 遮挡 | 用于建筑窗扇玻璃（高光深色反射板）：室外依然能看到窗户的天空反射与碎裂细节，但太阳直射光与天光可无阻挡穿过窗洞照亮室内地面与家具。 |

> **与 `shader-material-model-unification-plan.md` 的边界协调**：`subsurface` 与 `sheen` 仅作为 `Mixture` 漫反射项的轻量化解析修饰（`EvaluateSurfaceBSDF` 与各 forward/tracing 直接光共用同一辅助函数），不新增 `Material::Enum` 枚举值，不破坏未来 `SampleSurfaceBSDF` 的 MIS 统一。

### 3.3 烘焙顶点 AO / 接地渐变与画意纹理打散

1. **零带宽开销的逐顶点遮蔽打包（`GPUVertex::tangentw`）**
   - `Vertex.hpp` 中 `GPUVertex::tangentw`（`uint16_t`）当前低 8 位存 `MaterialIndex`（0..15），高 8 位仅用最低 2 位存切线手性（`0` 或 `2`），高 6 位（`bits 10..15`，64 级 `0..63`）完全空闲。
   - 默认值编码为 `0`（对应 `ao = 1.0`，保证 glTF / LDraw / 地形草等非 SCAD 资产零变化，打包为 `aoBits = round((1.0 - ao) * 63)`，因此 `0` 即无遮蔽）。
   - `ScadLoader` 在生成静态建筑、道具与角色服装网格时，基于顶点邻接二面角凹陷度（Cavity）与局部距底面 `z=0` 高度计算柔和遮蔽因子 `ao ∈ [0.55, 1.0]`（可用 `gk_ao(strength = 1.0, ground = 0.35)` 调整或置 `0` 关闭），在 `SceneSampling.FetchGBuffer` 与 `BuildBSDFContext` 中对 `BaseColor` 施加柔和暗角（暗部略偏冷色温，避免死黑脏污）。
2. **过程纹理（`gk_texgraph.scad`）画意化与宏尺度打散**
   - 在着色器采样过程纹理时，对建筑/地表材质叠加一层基于世界坐标 `worldPos.xz` 的双频平滑微扰（波长约 `13m` 与 `37m`，幅度 `±6%` 明度、`±3%` 冷暖），消除大面积墙体与长道路上的平铺网格感。
   - 调整 `gk_texgraph.scad` 中的 `tex_plaster`、`tex_brick`、`tex_tiles`、`tex_dirt`、`tex_planks`：压低尖锐凹坑的 `tilt` 与对比度，提高砌块/瓦片逐单元的柔和色相微差（暖赭、陶红、青灰微跳色），**继续满足 `[Scad]` 单测的调制纹理守卫（均值 > 160、标准差 < 40、无缝平铺）**。

### 3.4 角色、生态、建筑与光影重塑规范

1. **幸存者、服装与感染者 2.0**
   - **骨架与动作零破坏**：严格保留 `nextdayz_survivor.scad` 与 `nextdayz_infected.scad` 的全部 `bone_*` 名称、父子层级、枢轴坐标、`bone_ik_*` 与 `bone_weapon_socket`，确保 18-clip 混合空间、持枪双臂 IK、近战与攀爬翻越零回归。
   - **幸存者五官与皮肤**：头颅放样提至 `m = 16`、`gk_crease(55)`；皮肤使用 `gk_material(ndb_skin(), roughness = 0.78, ior = 1.26, subsurface = 0.55)`；眼睛改为三层立体结构（微凹暖白巩膜 `[0.82, 0.80, 0.76]` + 深棕/灰蓝虹膜 + 略微凸出的 `roughness = 0.03` 高光角膜），赋予鲜活眼神光。
   - **服装廓形升级**：解除旧方盒包络束缚，躯干外套改为阔肩收腰微张下摆（`m = 14, n = 3.5`），袖管与裤腿提至 `m = 14`（相邻角 25.7° < 35°，自然平滑无八棱柱折痕）并做出肘部微鼓与靴口堆叠；口袋、防弹衣插板、头盔护耳、背包主体全部换用 `gk_round_box` 圆角件并接入织物 `sheen = 0.45`、皮革 `roughness = 0.50`、金属扣件高光；同步检查 `PlayerRigVisual` 的穿戴包络避让与每部件 ≤ 16 材质槽上限。
   - **感染者重写**：用 `rf_loft` + `gk_round_box` 重写 `nextdayz_infected.scad`，做出夸张的佝偻高低肩、干瘪收窄腰腹、多层破布垂片与浑浊微反光眼球，保留衬衫 `ROLECOLOR` 染色契约。
2. **树木与运行时草地 2.0**
   - **地域树种**：用 `gk_sweep` 制作根部外扩、主干微弯并带 2–3 根分枝的树干；树冠由 5–9 个主次分明的超椭球团块（`$fn = 14`）构成，外包 `gk_puff_normals(0.7, 0.3)` + `gk_foliage(..., transmission = 0.65)`，按高度赋予“下层冷深绿 → 上层暖亮绿”的明度色温阶梯。
   - **运行时草地（`FTerrainGrassSystem`）**：近景草叶增加自然弯弧（2–3 段微拱），将草叶顶点法线向地形法线混合 `0.55`（消除近景黑刺阴影噪点，让草根柔和融进地面），并按 `TerrainComponent` 报告的调色板（`continental` / `savanna` / `temperate`）动态切换草叶色板。
3. **地域建筑、室内采光与机舱大厅**
   - **建筑构件**：屋顶 `bh_part_roof` / `cw_part_roof` / `sv_part_roof_*` 增加轻微脊线垂弧（或厚实圆角封檐板与分段立体脊瓦），门窗框线、窗台、烟囱压顶、墙角隅石全面改用 `gk_round_box`；**严格保持建筑外轮廓 `L × D`、正面 `-Y` 轴线门洞净空、室内地面装修层厚度以及 `nd_*` 家具/物资锚点坐标不变**。
   - **室内与大厅布光**：建筑玻璃改用 `shadow = false` 透光窗扇，配合适度放宽的室内自动曝光上限（使门窗射入的斜阳与天光反弹自然点亮房间）；重写 `nextdayz_helicopter_lobby.scad` 与 `NextDayzLobby.cpp` 的默认 `cabinLights_` / `cabinEnv_`，构建“右侧敞门暖阳主光 + 左侧舷窗冷蓝轮廓光 + 舱顶柔光补光 + 角色眼神光”的电影肖像构图。

---

## 4. 待确认的产品与美术微调项（供推进时向用户核对）

以下四项已在设计中给出安全默认值，后续 agent 推进到对应阶段前若用户另有偏好可随时调整，不影响底层架构：

1. **角色头身比**：默认采用 **1:6.8 硬朗动画比例**（直接兼容现有 `SKEL` 骨骼长度与全部 IK/步态）；若希望改为 1:5.5 夸张大头卡通比例，需在 V3 阶段同步调整 `SKEL` 与持枪握点。
2. **世界色调饱和度**：默认采用**“温润通透的寂静废土电影风”**（暖金阳光 + 清透蓝灰冷阴影，反照率上限 `< 0.45`）；若偏好《Luca》式高饱和明快度假风，可在 `TimeSystem` 与各 kit 色板函数统一上调饱和度。
3. **Web 客户端细节档位**：桌面端默认 `$gk_detail = 1.0`；Web 烘焙（`NextDayzBake`）与浏览器内 SCAD 求值默认传 `$gk_detail = 0.5` 控制三角面与 WASM 内存峰值。
4. **已发布地图版本策略**：升级共享 kit 后，按地理关卡规范递增 `cz_loket_riverbend` 与 `training_ground` 的 recipe `version` 发布新冻结快照，不覆盖历史版本。
