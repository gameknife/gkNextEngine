---
title: "GI 缓存与体素资源架构"
category: design
status: 现行
owner: engine/rendering
created: 2026-07-17
last_updated: 2026-11-14
---

# GI 缓存与体素资源架构

引擎当前有两套用途不同的 radiance cache：software paths 使用 VoxelData + AmbientCube，PathTracing 可使用 SHARC。它们共享部分 scene 准备，但不是一个“统一 GI cache”，也没有实现旧设计中的相机跟随统一 clipmap。

## 为什么使用 storage buffer + 稀疏 brick

AmbientCube 是运行时持续分帧写入的缓存。BC6H/ASTC 等硬件块压缩格式不能作为普通 compute 随机写目标；采用它们需要额外的未压缩中间层和实时编码 pass，既没有消除写入工作集，也会拖慢 bake，且 BC 在移动端并非通用方案。因此当前方向是保留 BDA/storage-buffer 路径，通过 right-size cascade、稀疏 cube brick 和 active-list 同时减少常驻容量与无效 dispatch。

RGB9E5 是单个 32-bit radiance 值的编码选择，不是块压缩。它保持 shader 可直接写，并改善相对 HDR 范围；不要把“已使用 RGB9E5”误解成 arena 已可采样 BC/ASTC texture。

## Renderer 使用关系

- SoftwareTracing、SoftwareModern 和 VoxelTracing 请求 Voxel + Ambient。
- PathTracing 的 contract 声明 Voxel、Ambient、TLAS、SHARC，并固定使用 SHARC；`ShouldSkipAmbientCubeUpdates()` 会跳过其 AmbientCube 更新。PathTracingLite 声明 Voxel、Ambient、TLAS，保留 AmbientCube 更新，且不创建 SHARC 或 ReSTIR 资源。
- SoftwareModernNoAmbient 不请求任何 scene GI resource；它使用直接/IBL/CSM 与屏幕空间 GTAO，不会因为 VoxelData 存在就自动获得体素天光遮蔽。

## Ambient arena

`Scene` 为实际分配的 cascade capacity 计算 ambient arena 布局，并通过一个 80-byte `AmbientResources` 地址表把各 region 暴露给 shader，从而保持 `GPUScene` 为 128 bytes。不要恢复依赖编译期固定 offset 的寻址。

当前 region 包括：

- `Cubes`：每 cascade 的稀疏 AmbientCube brick pool。
- `Voxels`：仍为每 cascade dense 的 VoxelData 数组。
- `CubesPong`：仅一条 cascade 大小的 bake ping-pong scratch。bake 第 c 级时反弹查询从 pong 读本级，dispatch 前
  pong 必须等于 ping 的第 c 级。上一次 dispatch 烘的是同一级、brick 表未重建（generation 相同）、arena 未换时，
  只拷上一批写过的 slot（`AmbientBake::PlanPongSync`，相邻 slot 合并成一个 copy region）；否则整级拷贝。
  任何不经 bake 写 cube 池的路径（清池、slot 清零、缓存恢复、几何变化的采样数下调）都必须让 generation 变化
  或令 `pong.valid = false`。
- `BrickTable`：逻辑 brick → pool slot。
- `ActiveBrickList`：每级一段，存本帧 bake batch 的 storage brick 号。

原先的 `Residency` 区（hit-driven residency）与 `Pages` 区（DDA 的 page index）已在
[大地图 GI 覆盖](../plans/ambientcube-clipmap-plan.md) 阶段 1、2 删除。

## 环形寻址

每级的 voxel 数组、brick 表和 cube-pool slot 都按 **storage brick** 索引。窗口内的 brick b（`[0, 24×6×24)`）
存在 `(b + wrap) mod (24, 6, 24)`；窗口平移 d 个 brick 时 wrap 同步加 d（`ShiftAmbientWindow`），留在窗口内的
brick 存储位置不变，只有进入的 brick 复用离开的 brick 的存储。环绕只在两处实现：shader 的
`AmbientBrickLinear`（经 `FAmbientCascadeAddress`）和 CPU 的 `GetVoxelAddress`；其余代码一律经过它们。
wrap 由 `FAmbientCascadeLayout::wraps` 记录，经 UBO 的 `AmbientCubeCascadeWrap[5]` 交给 shader。
active list 里存的是 storage brick，`DecodeAmbientActiveBrickProbe` 负责还原窗口坐标。

shader 热路径（DDA 每步、gather 每个角）不能逐次从 UBO 和资源表重读 wrap、voxel 基址、pool 基址：
`GetAmbientCascadeAddress` 每级解析一次，再往下传。逐次重读在本机让 VoxelTracing 慢了约 7%。

## 窗口滚动

`FCPUAccelerationStructure::PlanAmbientScroll` 在 voxel 管线空闲时为每级求焦点对应的吸附原点，偏差达到
`kAmbientScrollHysteresisBricks`（2）就滚动：CPU 侧 baker 立即换到新窗口（`ShiftAmbientWindow`），进入的
brick 走 voxel 管线（按轴分片的距离场子盒），`FinishVoxelJob` 在一帧内把 voxel、brick 表和窗口一起提交，
shading 不会看到超前于数据的窗口。进入的 brick 不继承原存储位置的 slot（`UpdateData` 的 enteredBricks），
其 cube 清零、遍数归零。缓存保存读体素期间不滚动；滚动本身不阻塞 bake。瞬移是同一路径，只是所有 brick 进入。

有未发布 brick 时 bake 至少占帧时间的 10%（`AmbientBake::PublishFloorGroups`），否则超预算的帧只给 1 个
group，滚进来的区域会长期停在粗级。`ambient.*` agent 查询和 DevTools 图形面板给出队列、滚动次数与耗时。

## Voxel 管线与 bake 队列

更新粒度是"一组 brick"，各级独立（`FCPUAccelerationStructure::Tick` 每帧推进，空闲时不做事）：

1. 体素化：待处理 brick 按每任务 6 个 brick 分到并行任务（`FCPUProbeBaker::VoxelizeBrick`）。
2. 距离场：只重算受影响的子盒。L∞、封顶 15 格，所以"变化区域外扩 2 个 brick"的输出盒在"再外扩 2 个
   brick"的定义域上重算，与整级重算逐格相同（单测覆盖）。
3. 上传：只上传输出盒覆盖的 brick，每个是连续的 4 KB。
4. 分配：`FCPUBrickTable::UpdateData` 重新分类，换主人的 slot 清零。

加载时的整图烘焙就是"所有 brick 进入"这一条路径。

几何变化走同一条管线：CPU 快照重建时（`Scene::MarkDirty` 之后，最多每 30 帧一次；逐帧移动只调
`MarkTransformDirty`，不触发）`BuildSnapshot` 比较前后快照，把新增、删除、移动的实例的新旧包围盒记进
`ambientDirtyBounds`，发布时并入待处理列表（超过 64 个合成一个并集）。voxel 管线空闲时
`PlanAmbientDirtyRegions` 把它们换算成各级窗口内的 brick（外扩一格、按整 brick 取整）送去体素化；
`FinishVoxelJob` 对距离场输出盒覆盖的已分配 brick 调 `FCPUBrickTable::RequeueBricks`：遍数降到发布阈值、
GPU 上 probe 的采样数也降到发布阈值，于是保持已发布、旧光照还在，新样本以 1/(n+1) 的权重快速覆盖。
整图烘焙与缓存恢复会丢弃待处理的盒子。`sys.ambientCubeDynamicUpdate=false` 关闭这条路径。材质换绑不改变
包围盒，不会触发。

brick 表同时是 bake 队列：每个已分配 brick 记已烘遍数（`bakePasses`），新主人从 0 开始，到
`kAmbientBakeConvergencePasses`（32）出队。每帧 `AcquireAmbientBakeBatch` 选一级、取一批 brick 写进该级的
active list 段并 dispatch，录完后 `CompleteAmbientBakeBatch` 给它们的遍数加一。顺序：未发布的优先（低于
`r.ambientCube.publishPasses`），细级优先，同级里遍数少的、离相机近的优先。帧预算不足一个 brick（8 个
dispatch group）时，一批跨多帧派发完，超预算的帧仍只付一个 group 的代价。清空 cube 池（场景切换时的
clear pass）会把所有遍数归零。原来的 dirty revision / acknowledge 协议已删除。

`VoxelData` 当前为 8 bytes：完整 `matId` 加一个打包的 8×4-bit distance/inside word。`AmbientCube` 为 40 bytes：六个方向色、sun direct、sky/emissive direct 以 RGB9E5 保存，另有六方向 8-bit sky visibility。不要引用旧文档中的 16-byte VoxelData、56-byte AmbientCube 或 RGBA8 face 布局。

## Cascade 布局

每级都是 `192×192×48` 的网格，落在自己的世界晶格上，晶格相位是 `CalculateAmbientCubeOffset(unit, offsetBias)`。
网格在世界中的位置只由 `FAmbientCascadeLayout`（`Engine/Assets/Acceleration/AmbientCascadeLayout.hpp`）
描述：每级一个"格子 (0,0,0) 的世界坐标 + 体素尺寸"。CPU 体素化按它写 voxel，camera UBO 的
`AmbientCubeCascadeGrid[5]` 把同一组数交给 shading、bake 和 Splat；shader 不再由"基础 unit × 比例"推导同心网格。

- `sys.ambientCubeFollow = 0`（默认）：固定锚点，第 i 级体素为 `unit × ratio^i`，窗口就是晶格原点，与改造前逐格一致。
- `sys.ambientCubeFollow = 1`：贴合场景。比例按场景水平尺寸自适应（夹在 1.5 ~ 4），使最粗一级以
  180 格（两侧各留吸附误差 4 格 + 插值余量 2 格）盖住全图；各级 XZ 居中、Y 底面落在场景最低点下方一格，
  原点吸附到该级 brick 的整数倍。不能盖住整个场景的级别跟随焦点滚动：焦点 XZ 为当前渲染相机，Y 为相机
  正下方的地面；窗口夹在场景包围盒内，偏差达到 2 个 brick 才滚动（见下方"窗口滚动"）。NextDayz 桌面端
  默认开启并用 5 级。

shading 必须读 bakers 实际采用的布局（`TryGetAmbientGridConfig`），不能读待生效的设置，否则在重烘焙
完成前会用新网格去采旧数据。级数上限 `CUBE_CASCADE_MAX` 为 5，CPU 常量、`GPU_SCENE_AMBIENT_CASCADE_MAX`、
`AmbientCube.slang` 与 Splat 的上限需同步。

Scene 构造时按当时的 `sys.ambientCubeCascadeCount` 固定本 scene 的 cascade capacity，但不超过显存预算能容纳的
级数（`Engine/Assets/Core/AmbientArenaBudget.hpp`）：预算是最大 device-local heap 乘以 `kAmbientArenaHeapShare`
（桌面 1，iOS/Android 1/8，手机的 heap 是共享内存），`FitAmbientCascadeCapacity` 从配置级数往下找第一个放得下的。
只有 1 级也放不下时，设备探测（Engine 与 VulkanBaseRenderer 用同一套计算）才把 ambient 渲染器换成 NoAmbient；
降级会打 `[AmbientArena] N cascades requested, M fit` 警告。运行中把 CVar 调大不会越界，但新增 capacity 要到 scene reload 才分配。cube pool 大小由 `sys.ambientCubePoolBrickRatio` 转换成每 cascade 固定 slot 上限，因此同一配置下“更稀疏的场景”主要减少 active/bake 工作，不会继续缩小已经创建的 Vulkan allocation。评估显存必须看启动时 `[AmbientArena]` 日志，不能只看 active brick 数。

## 稀疏 brick 分配

逻辑网格仍是 `192×192×48`，以 `8×8×8` probe 组成 brick。候选集直接读距离场：brick 内存在离实体不超过
k 格的**空**体素即激活，k = `clamp(ceil(2 m / unit), 2, 8)`（`ResolveAmbientBrickShellCells`），细级留出
角色高度的空气层，粗级只保留贴表面的一层；全实心的 brick 不激活。`FCPUBrickTable` 保留仍被需要的 brick
的旧 slot，其余按到焦点（当前为渲染相机）的距离由近到远分配空 slot。slot owner 改变时必须清零对应 cube，
防止新 brick 读到旧 owner 的 radiance；清零后采样遍数为 0，在重新烘焙到发布阈值前不会被插值使用。

候选数超过固定 pool 时，未分配 brick 保持 invalid 并落到更粗 cascade 取样；不能让 pool index wrap、
覆盖已有 owner 或把缺页默认为一块随机旧 radiance。降低 pool ratio 是画质/显存取舍，不是无损压缩开关。
每次分类在日志打印 `[AmbientBricks]` 各级的 wanted / overflow 数。当前没有 GPU free-list，也没有把 dense
VoxelData 一起稀疏化。

## Bake 与消费

camera-independent scene prepare 每帧只运行一次。AmbientCube bake 每帧从队列取一级的一批 brick，只 dispatch 这些 brick 的 probes；hardware/soft bake 共用 active-list 解码。bake 射线长度随级别缩放为 `clamp(80 × unit, 20 m, 200 m)`，各级单条射线跨过的格数相当。消费 shader 先由 cascade/逻辑 probe 找 `BrickTable`，缺页时使用明确 fallback，不能直接把逻辑 voxel index 当 pool index。

发布规则：probe 的累计 bake 遍数（`SkyVisibilityPXNXS0S1` 的第 2 字节）不足 `r.ambientCube.publishPasses`
（默认 4）时，插值把它当作缺失跳过；当前级没有有效权重就落到更粗一级，全部没有才是天空兜底。bake 自己的
反弹查询走同一规则，所以新 brick 的第一遍反弹取自已收敛的粗一级。Splat 的天空可见度采样同样遵守。
`r.ambientCube.debugView` 1 按最细命中级别着色（过渡带按权重混合两级颜色），2 按状态着色（品红=不在任何
窗口内，红=无 cube brick，黄=未发布，蓝=已发布）。

级间过渡：每级窗口在"背后还有更粗一级"的面（`AmbientCascadeOpenFaces`：面在场景包围盒内一个 brick 以上，且不是
最粗一级）前留 `r.ambientCube.blendCells`（默认 8 格，Y 取一半）的过渡带。带内的权重从 1 平滑降到 0，gather
以 1 − 权重的概率把这次采样交给下一粗级（随机数取样本位置与帧号的 hash），由时域累积（bake 里是滑动平均）
变成渐变。开放面与带宽打包在 UBO 的 `AmbientCubeCascadeWrap[i].w`。逐像素同时取两级再 lerp 的做法在本机
每帧多 1.0–1.5 ms，现在是 0.4–0.6 ms（同一 warp 内选不同级的分歧）。8 角 gather 在 `gatherAmbientCubes`
里只能写一份：bake 的每次反弹命中都调用它，抽成带 out 参数的辅助函数或内联两份都让 bake kernel 慢了约 85%。
池溢出（远角 brick 拿不到 slot）造成的边界不在过渡带里，仍是硬边。

AmbientCube 同时服务间接光、sky visibility、Splat 的可选场景光照以及 software tracing。修改 packed layout、brick edge 或 cascade 参数时必须同步 CPU static_assert、arena sizing、shader 寻址、readback 和 Splat consumer。

CPU voxelization、brick classification、flush 与首轮 bake 可能远晚于应用启动。默认短帧 `gnb shot` 可能只看到 sky IBL，却被误判为 AmbientCube 正常；验证此路径要把 `--frames` 提高到足以出现分类/bake 日志（复杂场景历史上常用约 3000 帧），并确认 `[AmbientArena]`、brick classification/overflow 与对应 GPU timer，而不是只凭最终截图。

## HDR 环境贴图驻留

`sys.hdrTextureStreaming=true` 时，HDR 环境贴图以 lowest mip 起步；当前 active `SkyIdx` 连续需求 8 帧后异步升为 full mip，离开 active 状态 180 帧后降回 lowest mip。bindless texture id 保持不变，payload/SH 在完成回调中刷新。关闭 streaming 会请求所有 HDR texture 升为 full mip。

这套策略减少未使用 sky 的显存，与 AmbientCube 的 brick 分配无关。修改阈值或异步替换时要验证当前 sky 不闪黑、SH 与 texture 同步、scene 切换和 shutdown。

## SHARC 边界

SHARC 使用 vendored NVIDIA header，经 `assets/shaders/common/Sharc.slang` 适配 BDA/Slang。通用 path loop 只依赖 `IRadianceCache`：

- `FNullRadianceCache`：普通 path，无 cache 行为，编译期内联消失。
- `FSharcUpdateCache`：稀疏选像素，记录 surface/direct/throughput/miss。
- `FSharcQueryCache`：在 bounce、材质 roughness 和 segment length 条件满足时用缓存 radiance 终止路径。

不要把 SHARC 接口塞回 `FPathTracingRenderer` 的通用状态，也不要让 software renderer 假装使用 SHARC。SHARC 更新、resolve、query 的资源由 PathTracing 路径拥有；AmbientCube 仍是 software paths 的独立方案。

## 明确未实现

- VoxelData 内 sun visibility/emissive 的额外 GI-lite 字段。
- NoAmbient 的 voxel sky-visibility soft trace。

这些旧设计可以作为历史研究，但不是当前 roadmap。若重新立项，应先用现有 arena/brick/effective renderer contract 建立基线，不能从旧结构尺寸或旧阶段继续施工。

原列表里的"相机 recenter 的 ring-addressed clipmap"与"BVH dirty-region 驱动的局部增量"已按当前代码在
[大地图 GI 覆盖开发计划](../plans/ambientcube-clipmap-plan.md) 中实施（阶段 1–3 与阶段 4 的几何变化重体素化），
本文已同步。

## DDA 多级行走

`FHiVoxelDDARayTracer::TraceRayDDA` 总在"包含当前点的最细一级"里走：一段在射线离开该级窗口或进入更细一级
窗口时结束，然后从那里换级继续；不在任何窗口内时跳到射线下一次进入的窗口。远处的行走落在粗级上，格子大，
加上距离场跳跃，步数自然有界，不再需要 page index，任意大的地图都适用。`TraceRayDDAInCascade` 是单级行走。

## Brick 缓存（内存）

`FAmbientBrickCache`（`Engine/Assets/Acceleration/AmbientBrickCache.hpp`）按"级 + 世界 brick 坐标"保存烘好的
brick（512 个 AmbientCube 加遍数），LRU，预算 `sys.ambientCubeBrickCacheMB`（桌面 256、移动端 64，0 关闭）。
brick 与各级世界晶格对齐，所以内容只取决于场景、级别和位置，与烘它时窗口在哪无关。

- 存：滚动任务开始时（`StartAmbientBrickCapture`），离开窗口且已发布的 brick 仍占着原 slot，由 worker 与体素化
  并行读到系统内存；提交时（`CommitAmbientBrickCapture`）放进缓存。回读必须在 worker 上、排在体素化任务前、
  拆块、用流式加载：arena 是 write-combined 显存，游戏线程上读一次传送的量会卡几秒。
- 取：同一次提交里 `UpdateData` 分完 slot 后，进入窗口且命中的 brick 直接写回 cube 并恢复遍数
  （`RestoreEnteringAmbientBricks`），已发布的不再进入未发布队列。
- 淘汰：几何变化按脏盒外扩 2 个 brick（`EvictAmbientBrickCache`，在快照发布时做，不受
  `sys.ambientCubeDynamicUpdate` 影响）；清池（光照变化）、整图烘焙、缓存恢复和换场景整体清空。
- 统计：DevTools 图形面板的 AC Brick Cache 行，agent 查询 `ambient.brickCacheEntries`、`ambient.brickCacheHits`、
  `ambient.lastScrollRestoredBricks`；每次滚动提交打一行 `[AmbientBricks] scroll:` 日志。

## 磁盘缓存（计划中）

voxelize / distance field / ambient cube 目前每次加载都完整重算，虽然在同一场景与同一网格配置下结果稳定。
把这条链路做成 tile 级本地磁盘缓存的执行方案见 [GI Bake 磁盘缓存开发计划](../plans/gi-bake-disk-cache-plan.md)。
该计划成文早于现在的结构：distance field 现在可以按子盒重算，brick 也已与各级世界晶格对齐，所以按世界 brick
坐标存取的缓存可行，内存层已经是这样做的（见上节）；落盘时以它为基础，不沿用旧计划的约束。
