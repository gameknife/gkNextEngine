# 运行时 biome 草地

`Runtime::FTerrainGrassSystem` 在场景提交后读取 `TerrainComponent`，为玩家附近的草原生成实体草叶。
SCAD 只定义地形及 biome；不写草叶、不做 SCAD scatter，也不为每株草复制几何。
NextDayz 默认启用，入口为 `NextDayzGameInstance` 的 Prepare / Bind / Tick / Clear 生命周期。

## 几何与风

- 初始化生成 **近/中/远三类共享草簇，每类 9 个风姿态，共 27 个 mesh / BLAS**，与覆盖地图大小无关。
  近景每簇 12 片不等高、不同朝向的实体 low-poly 草叶，合计 120 个三角形。
  每叶采用底部封口的三角截面，根部到腰部接 6 个面，腰部到尖端接 3 个面，
  加 1 个底面；平面法线保留清晰棱面，不再用零厚度双面薄片。草叶宽约 7–12 cm、深约 5–10 cm，
  再叠加草簇的随机缩放。
  从直立到最大弯曲按 9 档预采样；所有姿态的拓扑、根部和材质槽一致。不生成简化 LOD，避免额外
  BLAS、根部漂移和远处叶片消失。材质为不透明实体，无 alpha 测试。
- 中景大簇直径约 2.7 m，每簇 36 叶、144 个三角形；远景大簇直径约 5 m，每簇 64 叶、
  256 个三角形。中远景的每片草简化为 4 个三角形的封闭四面体，稍加宽而不拉高，保持低模实体
  草的颜色和体积。大簇的草根覆盖更宽区域，避免将近景的小簇直接放大成巨草。
- 草簇按稳定随机数改变宽高、朝向偏差与风相位。grass / grass_dark / dry_grass 各有一套根暗梢亮
  的四色材质，分别采用低饱和橄榄绿、灰绿和枯卡其色，贴合地面与场景的灰绿基调。
  根/尖明度倍率为 0.82/1.12，第二组变体乘 0.90，保留层次并减轻草簇的色块感。
  dry_grass 更矮、更稀疏，使用枯草颜色。
- 风场由缓慢转动的主风向、空间行进波、局部不同相位组成，NextDayz 天气增加阵风强度。20 Hz
  更新实例，带滞回地选择最近的弯曲档位；剩余弯曲量用小幅根部旋转近似。方向缓动与不同相位
  避免整片草同帧跳档。mesh 顶点与 BLAS 内容始终不变。
- `RenderComponent::SetModelId` 同时驱动普通可见性/CSM 和现有 GPU RT LOD-select，后者从 model
  取共享 BLAS 地址重建 TLAS。软件光栅路径也能看到同样的草。TLAS 更新与实例/光栅开销依然随
  草簇数量增长；共享 BLAS 节省几何和底层加速结构，不能消除 TLAS 与着色成本。

## 生长与驻留

- 世界空间 8×8 m 分块，近/中/远每块分别有 14×14、4×4、2×2 个抖动候选点，以 terrain seed
  和世界网格坐标散列。再以
  低频斑块和 biome 密度筛选。离开再返回、卸载再加载都得到同一组草。
  默认覆盖率 1.0，相比原来的 10×10 / 0.95 配置，单位面积草簇数量约为 2.06 倍。
- 只接受 grass / grass_dark / dry_grass，排除水域、道路、建筑 pad 和大于 30° 的坡。
  根部查询精确三角化地形高度，贴合法线；额外八向探测为近/中/远簇分别留出约 0.85 / 1.95 / 3.5 m
  的语义边缘。大簇另检查地形相对落点切平面的误差，中/远景最多容许 0.22 / 0.35 m，超出就不放置，
  避免在地形折面上悬空或穿入道路。
  使用严格地形边界，不沿 `ContainsWorld` 的 gameplay slack 在地形外长草。支持多块地形。
- **禁生长边缘过渡**：在道路、建筑 pad、水域、沙地/岩地等非草 biome 周围，计算到最近禁生长
  cell 矩形的世界空间距离。直接使用 cell 边界距离，跨 cell 连续，旋转/缩放地形下仍按米计量。
  扣除近/中/远簇各自的占地安全距离后，默认再用 4 m 的 smoothstep 过渡带，把生长概率从 0
  提升至完整密度，同时把草高从约 35% 提升至 100%。禁生长地表内部仍不生成草。
  筛疏复用原来的随机阈值，因此边缘草是原草场的稳定子集，重新加载不会换一批草；内侧密集草场
  保持原高度。距离搜索只在生长/分块重建时检查邻近 cell，风动不重新计算。
- 默认半径 **192 m**，是原先 48 m 的四倍距离、约十六倍圆形面积。近景在 36–48 m 与中景交叠，
  中景在 80–96 m 与远景交叠；交界处两档的覆盖权重之和始终为 1。最后 16 m 缩短至隐藏。
  光栅/光追/阴影共用相同变换。覆盖半径可调低，不会在设定半径之外继续保留可见草。
  跨块时回收实例槽，保留一个块量级的驻留滞回，**最多 42,000 个实例**；远处依靠原地形颜色衔接。
  玩家移动超过 1 m 或配置变化才检查驻留，风动不重新生成地面点。
- 草不创建物理体，不参与 CPU 射线、寻路或静态 GI 烘焙。CPU TLAS 捕获和 bake key 同时排除
  `rayCastVisible=false && receiveGI=false` 的 GPU 装饰物。风动只调用 `MarkTransformDirty`。

## 复用与调节

其他程序可以持有 `FTerrainGrassSystem`：在 `BeforeSceneRebuild` 调用 `Prepare(nodes, models,
materials)`，提交后 `Bind(scene)`，每帧 `Tick(scene, dt, focus, weather)`，卸载前 `Clear()`。
Prepare 只为含地形的场景注入资源。运行时开关保留这 27 个共享 mesh，重新启用无需重建场景。
引擎实际顺序是 Prepare → OnSceneUnloaded → commit → OnSceneLoaded；因此 Clear 只释放驻留
与旧节点引用，保留刚暂存的 asset ID，下一次 Prepare 才重置资源声明。

NextDayz 的控制台变量（不写入个人 Archive 配置）：

| 变量 | 默认 | 范围 / 含义 |
|---|---|---|
| `nextdayz.grass.enabled` | true | 实时开关 |
| `nextdayz.grass.radius` | 192 | 8–192 m，近中远分档覆盖 |
| `nextdayz.grass.density` | 1.0 | 0–1，稳定子集 |
| `nextdayz.grass.edgeWidth` | 4 | 0–12 m，边缘密度/高度过渡宽度；0 恢复硬剔除 |
| `nextdayz.grass.transmission` | 1.0 | 0–1，主表面直接光透光强度；0 恢复原不透明光照 |
| `nextdayz.grass.windStrength` | 0.65 | 0–1，叠加天气影响 |
| `nextdayz.grass.windYaw` | 0.7 | 弧度，+X 朝 -Z |
| `nextdayz.grass.windSpeed` | 1 | 0–4，0 冻结风场时间 |
| `nextdayz.grass.interaction` | true | 角色接触与回弹总开关 |
| `nextdayz.grass.interactionStrength` | 1 | 0–2，角色拨草/踩伏强度 |
| `nextdayz.grass.recoverySeconds` | 1.25 | 0.3–4，离开后的回弹时间尺度 |
| `nextdayz.grass.playerInteraction` | true | 玩家接触开关 |
| `nextdayz.grass.zombieInteraction` | true | 活跃且存活的感染者接触开关 |

查询：`game.grassInstances`、`grassPoolSize`、`grassMeshes`、`grassPoseChanges`、`grassTileBuilds`。
分档与距离查询：`game.grassNearInstances`、`grassMidInstances`、`grassFarInstances`、`grassFarthestDistance`。
边缘缩矮的可见实例数：`game.grassEdgeInstances`。
验证：`gkNextUnitTests "[TerrainGrass]"`；`gnb validate --script assets/agentscripts/nextdayz-grass.agentscript.json`。
后者等场景真正 ready 后取景，检查草量、风姿态变化、开关恢复和远距离流送预算，并留存开/关截图。
同时验证最远可见草根超过 185 m、远景档确实产生实例，以及 48 m / 192 m 的同机位截图。
最后在主公路拍摄 `edgeWidth=0` 和 `edgeWidth=4` 的同机位对照，检查稀疏、缩矮和无草路面的过渡。

## 草叶透光

`Material::Foliage(color, transmission, thickness, textureId)` 仍是 Lambertian，复用材质的
`Reserved2`（透光强度）与 `Reserved3`（有效厚度，米），不增加 GPU 数据布局或材质枚举。
编辑器 Material 面板的 Lambertian 属性下提供 `Foliage Translucency`、`Transmission`、
`Thickness (m)`，支持现有撤销流程。运行时草根强度为草尖的 40%，根/尖厚度为 0.14/0.08 m，
枯草强度再乘 0.65；修改控制台强度只上传 12 个共享材质，不改 mesh/BLAS。

SoftwareModern、SoftwareTracing、SoftwareModernNoAmbient、PathTracing、PathTracingLite
只在首命中表面的直接光中补充背面漫透射：
`transmission * exp(-8 * thickness) * max(-N·L, 0) / pi`，再乘草色、光强和可见度。
软件路径的太阳沿用 CSM，有限灯沿用相应渲染器的屏幕空间或体素遮挡；硬件光追路径的太阳与
有限灯均使用原有硬件阴影射线。正面光照、后续反弹、反射、天光、间接光和 GI 缓存均保持原处理。
透光补光只从 `DirectIlluminatePrimary` 进入，不改变次级命中的 `DirectIlluminate` 或 BSDF，
不写入 ReSTIR reservoir，并使用独立随机数副本以保留原有反射和间接光采样序列。

封闭 low-poly 草叶会遮挡自身的背面入射光，因此透光阴影查询沿入射方向跨过局部草叶厚度，
最多偏移 0.35 m。草仍投射普通阴影，建筑和地形仍能阻挡透光；这是复用现有阴影资源的局部近似，
无法准确表达逐叶厚度或多层散射，在紧邻薄遮挡物处可能有少量漏光。没有新增阴影贴图或 pass。
本次不扩展 glTF 导出格式；这些预留参数不会随现有 glTF 导出保存，运行时草由工厂重新创建。

同机位光照对照：`gnb validate --script assets/agentscripts/nextdayz-grass-lighting.agentscript.json`。

## 角色交互

`Tick(..., weather, interactors)` 接收稳定 ID、脚部世界位置、作用半径和强度。NextDayz 在角色运动及
感染者贴地更新后收集交互源；玩家离地或翻越时不提交，死亡感染者不再持续施压。引擎保存上一次
20 Hz 更新的位置，用扫掠胶囊覆盖快速移动路径；新 ID 从当前位置开始，单次位移超过 4 m 视为
传送，不沿途压草。离开输入列表或卸载时清除历史；实例槽回收时清除压痕，避免跨块串扰。

接触源按现有 8 m 网格分桶，草簇只检查自己块内的扫掠段；接触处的切平面高度差超过 0.45 m 则
忽略，避免桥上、楼上或跳跃中的角色影响下方草地。作用方向混合向身体外侧拨开与顺移动方向拖拽，
速度越快压力越强；静止保留较轻压力。多个角色的弯曲叠加后限幅，压力取最大值。

每簇只保存弯曲/压力与回弹速度，使用解析阻尼弹簧，接触约 0.1–0.2 秒响应、离开后约 1–2 秒回弹。
受压时减弱风摆。只在近景 LOD 启用，24–36 m 平滑减弱，在中远景大簇出现前归零。

实例使用 `Node::SetShearedTransform`，局部 `x'=x+shearX*y`、`z'=z+shearZ*y`，配合保持正值的
高度缩放表现踩伏。所有根部位于局部 y=0，因此接触和风姿态残差都保留草根位置。原 27 个共享
mesh/BLAS 不变，不更新顶点、不重建 BLAS；完整仿射矩阵驱动光栅、CSM、TLAS 和上一帧运动矩阵。
可见性重建、法线贴图与硬件射线命中共用逆转置法线变换，包围盒包含剪切后的几何。
此接口用于没有物理体的运行时装饰，不对碰撞形状执行剪切。

诊断查询：`game.grassInteractionCount`（正在变形或回弹的可见草簇）、`grassContactCount`（当前接触）、
`grassMaxPressure`、`grassContactChecks`（本次更新实际执行的接触检查）。
验证：`gnb validate --script assets/agentscripts/nextdayz-grass-interaction.agentscript.json`，覆盖玩家站立/
奔跑、离开后的恢复、感染者独立接触、硬件光追/光栅截图，以及共享 mesh 与实例池预算。

## 当前边界

这是量化 mesh 动画，残差旋转只是近似；切换模型的非刚性顶点位移尚无精确逐顶点 motion vector。
该限制可能在强时域降噪/上采样下产生残影；不要把静态截图当成时域画质保证。
草簇沿地形平面贴地，不能精确跟随草簇尺度内的凸凹折面；微量埋根避免一般坡面浮根。
角色交互是每簇的仿射近似，同簇各片草共享弯曲方向，不做逐叶碰撞，也不留下永久踩踏地表。
建筑占地应标为 terrain pad；本系统不对任意场景物件另做遮挡/占地射线。
地形运行时改变网格或变换时需重新 Prepare/Bind，和场景重建生命周期一致。
