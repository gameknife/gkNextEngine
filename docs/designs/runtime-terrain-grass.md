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
  的四色材质。dry_grass 更矮、更稀疏，使用枯黄颜色。
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
| `nextdayz.grass.windStrength` | 0.65 | 0–1，叠加天气影响 |
| `nextdayz.grass.windYaw` | 0.7 | 弧度，+X 朝 -Z |
| `nextdayz.grass.windSpeed` | 1 | 0–4，0 冻结风场时间 |

查询：`game.grassInstances`、`grassPoolSize`、`grassMeshes`、`grassPoseChanges`、`grassTileBuilds`。
分档与距离查询：`game.grassNearInstances`、`grassMidInstances`、`grassFarInstances`、`grassFarthestDistance`。
边缘缩矮的可见实例数：`game.grassEdgeInstances`。
验证：`gkNextUnitTests "[TerrainGrass]"`；`gnb validate --script assets/agentscripts/nextdayz-grass.agentscript.json`。
后者等场景真正 ready 后取景，检查草量、风姿态变化、开关恢复和远距离流送预算，并留存开/关截图。
同时验证最远可见草根超过 185 m、远景档确实产生实例，以及 48 m / 192 m 的同机位截图。
最后在主公路拍摄 `edgeWidth=0` 和 `edgeWidth=4` 的同机位对照，检查稀疏、缩矮和无草路面的过渡。

## 当前边界

这是量化 mesh 动画，残差旋转只是近似；切换模型的非刚性顶点位移尚无精确逐顶点 motion vector。
该限制可能在强时域降噪/上采样下产生残影；不要把静态截图当成时域画质保证。
草簇沿地形平面贴地，不能精确跟随草簇尺度内的凸凹折面；微量埋根避免一般坡面浮根。
建筑占地应标为 terrain pad；本系统不对任意场景物件另做遮挡/占地射线。
地形运行时改变网格或变换时需重新 Prepare/Bind，和场景重建生命周期一致。
