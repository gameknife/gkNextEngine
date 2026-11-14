---
title: "大地图 GI 覆盖：Voxel / AmbientCube clipmap 开发计划"
category: plan
status: 阶段 1 已实施，阶段 2–3 待做
owner: engine/rendering
created: 2026-11-14
last_updated: 2026-11-14
---

# 大地图 GI 覆盖：Voxel / AmbientCube clipmap 开发计划

目标：让 SoftwareTracing / SoftwareModern 依赖的 voxel + ambient cube 机制覆盖 1–2 km² 的地图。视点附近
保持现在的精度，远处用更粗的精度，整张图任何位置都有遮蔽与反弹。约束是保持现有机制的鲁棒性和精简：
voxel 格式、ambient cube 格式、DDA、烘焙着色器都不换，只改"寻址"和"更新粒度"。

本计划按 2026-11-14 的代码重新立项，不沿用更早的 clipmap / 驻留 / tile 缓存设计。实施完成后，仍成立的
契约并入 [GI 缓存与体素资源](../designs/gi-cache-architecture.md)，本文删除。

## 1. 基线（2026-11-14 实测）

默认 3 级，网格 `192×192×48`，全部以世界原点为中心，Y 从 0 往上：

| 级 | 体素 | 水平范围 | 垂直范围（世界 y） |
|---|---|---|---|
| 0 | 0.25 m | 48 m | -0.3 ~ 11.7 |
| 1 | 0.5 m | 96 m | -0.7 ~ 23.3 |
| 2 | 1.0 m | 192 m | -1.4 ~ 46.6 |

`assets/scad/proc/nextdayz/training_ground` 是 800 m 见方，地表高度 6.5 ~ 28.7 m，营地平地在 y = 10 m。
在 SoftwareModern 下用 `gnb validate` 加 `r.ambientCube.residencyDebug 2` 实拍：

- 全图只有中心 192 m 在网格内，占面积 5.9%。网格外的查询返回"无间接光、天空全可见"。
- 三级 brick 池全部打满（`5184 active bricks` = 3 × 1728），中心地表所在的层反而没有 cube。

问题拆开是六条，后面的设计逐条对应：

1. 锚点固定在原点，Y 从 0 起，地形抬高就出界。
2. brick 激活规则是"实体 brick 向外膨胀 3 个 brick"。网格只有 6 层 brick 高，这等于整列全要；分配又按
   y 从低到高先到先得，名额被地下的空 brick 占掉。
3. 管线只有"整套重建"一种粒度：体素化、距离场、上传、烘焙、磁盘缓存都以全部 cascade 为单位，
   `FCPUBrickTable::MarkDirtyBounds` 没有调用方。不能只更新一部分，也就不能移动。
4. `PageIndex` 是世界固定的 ±512 m，超过 1024 m 的图长射线会漏。
5. 烘焙射线固定 20 m，对粗级只有几格，远场遮蔽测不到。
6. 未烘焙的 probe 以黑色全权重参与插值，而不是被跳过。

本机软件追踪的吞吐（除烘焙外几乎空载，是上限）：playground 冷烘焙 2991 个 brick × 32 遍用 2.7 s，约
1800 万 probe 更新/秒；NextDayz 场景 3 级共 530 万体素的体素化、距离场和上传在 1.9 s 内完成。

## 2. 已确认的决定

1. 删除 `PageIndex` 和 hit-driven residency。
2. 大地图模式（贴合场景 + 跟随视点）只给大地图开：`sys.ambientCubeFollow` 默认 0，NextDayz 在启动时把
   默认值设为 1。其余程序保持固定锚点，走同一套代码，只是焦点为常量。MagicaLego 依赖晶格相位对齐
   积木，不受影响。
3. 级数上限从 4 升到 5。大地图模式下比例按地图尺寸自适应，让最粗一级刚好盖住全图。
4. 磁盘缓存先做"快照 + 各级原点"；brick 级持久化放到阶段 4。

## 3. 设计

### 3.1 布局

每级网格尺寸不变（`192×192×48`，brick 为 `8³`，每级 `24×6×24` 个 brick）。大地图模式下：

- 最粗一级的体素尺寸取 `场景水平尺寸 / 188`，居中盖住全图，不滚动。
- 0 级保持 `sys.ambientCubeUnit`（默认 0.25 m），中间各级等比分布，比例夹在 1.5 ~ 4 之间。比例被 4
  夹住时最粗一级盖不住全图，它就和其他级一样滚动，机制不变。
- 其余各级以主相机位置为焦点，XZ 和 Y 都跟随。

5 级时的布局：

| 级 | 800 m 图 | 1 km² | 2 km²（1414 m 见方） | 行为 |
|---|---|---|---|---|
| 0 | 0.25 m / 48 m | 0.25 m / 48 m | 0.25 m / 48 m | 跟随 |
| 1 | 0.51 m / 97 m | 0.54 m / 103 m | 0.59 m / 112 m | 跟随 |
| 2 | 1.03 m / 198 m | 1.15 m / 221 m | 1.37 m / 263 m | 跟随 |
| 3 | 2.10 m / 402 m | 2.48 m / 476 m | 3.21 m / 617 m | 跟随 |
| 4 | 4.26 m / 817 m | 5.32 m / 1021 m | 7.52 m / 1444 m | 盖住全图，静止 |

格式是"体素尺寸 / 水平范围"，垂直范围是水平的四分之一。坡地上近处出了 0 级高度的地表会落到更高的
粗一级里，这是嵌套窗口自带的退化方式。最粗一级静止且盖住全图，保证任何时刻任何位置都有一层可用。

### 3.2 寻址

- 每级有一个世界晶格，相位沿用今天的 `CalculateAmbientCubeOffset(unit, offsetBias)`，所以固定锚点
  模式下各级网格与今天逐格一致。
- 窗口原点吸附到该级 brick（8 格）的整数倍。存储位置 = 世界 brick 坐标对 `(24, 6, 24)` 取模。窗口移动
  时留在窗口内的 brick 原地不动，只有新进入的 brick 复用离开的 brick 的存储。
- 每级向着色器提供两项：窗口最小角的世界坐标加体素尺寸，以及 brick 环绕偏移。放在 UBO 的每级数组里，
  取代现在由"基础 unit × 比例"推导的同心公式。
- 环绕只发生在两处：着色器的 `AmbientBrickLinear`，CPU 的 `GetVoxelAddress` / `DecodeVoxelAddress`。
  voxel 数组、brick 表、脏标记都经由这一个规则寻址。窗口内坐标（`[0, 192)` 等）的含义和越界判断不变。

### 3.3 更新管线

管线的粒度从"整套 cascade"降到"一组 brick"，各级独立：

1. 体素化：对指定 brick 集合投射，结果写入任务自己的缓冲。
2. 距离场：只重算受影响的子盒。距离场是 L∞ 且封顶 15 格，一个格子的值只取决于 14 格内的占据，所以
   "变化区域外扩 2 个 brick"的子盒在"再外扩 2 个 brick"的定义域上重算，结果与整级重算逐格相同。离开
   一侧的格子保留旧值，旧值只会偏小，是保守的。
3. 上传：按 brick 上传。voxel 存储是 brick-swizzled 的，每个 brick 是连续的 4 KB。
4. 分配：进入的 brick 按 3.6 的规则拿 cube 槽位，槽位换主人时清零。
5. 烘焙：进入烘焙队列，每个 brick 记已完成遍数。

加载时的整图烘焙就是"所有 brick 进入"，瞬移同理，只有这一条路径。现在的 revision / acknowledge 协议
和 `Scene::Tick` 里 30 帧一次的调用都去掉，改为每帧检查，空闲时不做事。

### 3.4 滚动的提交与发布

- 焦点偏离窗口中心超过 1.5 个 brick 才滚动（迟滞），然后重新对中到最近的 brick。窗口被夹在场景包围盒
  内；某轴上窗口不小于场景时，该轴居中不动。
- 先准备后提交：进入的 brick 在后台完成体素化和距离场后，由游戏线程在一帧内一起提交 voxel、brick 表
  和窗口原点。每级同时只有一个滚动任务；任务期间焦点继续移动，提交后按最新焦点再起下一个。
- 发布规则：probe 的累计采样遍数不足 K（初定 4）时，插值跳过它。跳过后当前级没有有效权重就落到更粗
  一级，全部没有才是天空兜底。烘焙跟不上时的表现是"暂时变粗"，不会出现黑块或噪点。
- 这条规则同样用于烘焙自己的反弹查询，所以新 brick 的第一遍反弹直接取自已收敛的粗一级。

### 3.5 DDA 多级行走

射线在当前点所在的最细一级里走；走出该级窗口，或进入更细一级的窗口时，换到新的一级继续，直到命中、
走完 `maxDistance` 或离开最粗一级。远距离落在粗级上，格子大，加上现有的距离场跳跃，步数自然有界，
不再需要 `PageIndex`。`TraceRayDDA` 本身只多一个"在哪一级走"的参数。

### 3.6 brick 激活与分配

- 激活条件改为"brick 内存在离表面不超过 k 格的体素"，直接读已有的距离场。k 按级取
  `clamp(ceil(2 m / unit), 2, 8)`，细级留出角色高度的空气层，粗级只保留贴表面的一层。
- 池不够时按离焦点的距离排序，近的先拿。拿不到的 brick 保持 invalid，查询落到更粗一级。
- 仍在窗口内且仍激活的 brick 保留原槽位，这一条不变。

### 3.7 烘焙调度

- 每帧选一级，从该级队列取一批 brick 写入 active brick list 并 dispatch，CPU 给这批 brick 的遍数加一。
  着色器的解码方式不变。
- 顺序：未发布的优先，细级优先，近的优先；已发布的在后台补到 32 遍后出队。
- 帧预算仍由 `AmbientBake::PlanNextDispatchGroups` 按目标帧率分配。
- 烘焙射线长度随级别缩放：`clamp(80 × unit, 20 m, 200 m)`，按格数封顶，各级单条射线的代价相当。
- pong 拷贝在阶段 1–3 保持"整池拷贝、每帧一级"的现状，优化见阶段 4。

### 3.8 磁盘缓存

沿用现有的整份快照，多存各级窗口原点，去掉 page index。恢复时把原点设为快照里的值，然后交给滚动
逻辑：与当前焦点重叠的 brick 直接可用，其余的正常进入。最粗一级静止，总能完整复用；近场各级在同一
出生点附近启动时也基本复用。保存时机不变，仍是加载后烘焙队列第一次清空时。

### 3.9 配置与调试

- `sys.ambientCubeFollow`：0 为固定锚点（默认），1 为大地图模式。
- `sys.ambientCubeCascadeCount`：上限 5。NextDayz 桌面端默认 5。
- `sys.ambientCubeCascadeRatio`：固定锚点模式下照旧；大地图模式下由地图尺寸自适应。
- `r.ambientCube.debugView`：取代 `residencyDebug`。1 按级别着色，2 按状态着色（不在任何窗口内 / 没有
  cube brick / 未发布 / 已发布）。
- DevTools 面板显示各级原点、激活与溢出的 brick 数、待烘焙数、最近一次滚动的体素化耗时。同一组数字
  注册为 agent 查询（`ambient.pendingBricks` 等），供 `gnb validate` 断言。

### 3.10 删除项

- `PageIndex`：`FCPUPageIndex`、arena 的 Pages 区、着色器里的 `PAGE_*` 常量和 page 步进、缓存 payload
  里的 pages。
- hit-driven residency：`AmbientBrickResidency` 及 arena 区、着色器里的 `MarkAmbientBrickHit*` 和原子写、
  CPU 回读、grace / evict 逻辑、五个相关 CVar、设置面板里的对应项、
  [驻留设计文档](../designs/ambientcube-hit-driven-residency-design.md)。
- 固定 3-brick 膨胀、revision / acknowledge 协议、`EBakerType` 与 `FarGPUMemory` 之类的空壳参数。

## 4. 预算

- 显存：5 级约 270 MB（cube 池 169、voxel 68、pong 34），今天 3 级是 176 MB。
- 滚动需求（估算，6 m/s 冲刺，约三分之一的进入 brick 被激活）：每秒约 270 个 brick、14 万个 probe。
  发布需要约 55 万 probe 更新/秒，补满 32 遍需要约 440 万/秒；对照基线的 1800 万/秒，余量分别约 30 倍
  和 4 倍。体素化约 41 万体素/秒，对照基线的 280 万/秒。
- 这些余量是本机数字。发布规则保证吞吐不足时只是变粗，所以较弱的 GPU 不需要另一套逻辑。

## 5. 实施计划

每一步单独可提交。带"画面不变"的步骤用 `gkNextVisualTest` 的 baseline 对比验收。

### 阶段 1：全图底图

不引入滚动，各级静止，交付后 1024 m 以内的图整张有 GI。

1. 删除 hit-driven residency，调试视图换成 `r.ambientCube.debugView`。画面不变。
2. 级数上限升到 5：`CUBE_CASCADE_MAX`、arena 布局与 static_assert、烘焙调度的每级数组、CVar 范围、
   显存预算检查。画面不变。
3. `sys.ambientCubeFollow`：锚点贴合场景包围盒（XZ 居中，底部对齐场景最低处），比例自适应。NextDayz
   开启并用 5 级。此阶段的近场各级静止在地图中心，是过渡状态。
4. 新的 brick 激活与分配规则（3.6）。
5. probe 发布规则（3.4）。
6. 烘焙射线随级别缩放；CPU 体素化射线从 64 格截到 1 格，后者不改变输出。

验收：`training_ground` 的状态调试截图里没有"窗口外"和"无 cube"的地表；地图各处有遮蔽与反弹；
`follow=0` 的现有场景只允许第 4、5 步带来的差异，逐项审阅后更新 baseline。

说明：`PageIndex` 仍在，超过 1024 m 的图要到阶段 2 才完整。

实施记录（2026-11-14）：六步已落地，与上文的差异如下，后续阶段以此为准。

- 每级网格参数提前到本阶段：`FAmbientCascadeLayout`（`AmbientCascadeLayout.hpp`）给出每级"格子 (0,0,0)
  世界坐标 + 体素尺寸"，经 UBO 的 `AmbientCubeCascadeGrid[5]` 交给所有 shader 消费者；同心公式已删除。
  阶段 2 第 2 步只剩环绕偏移。
- 覆盖格数用 180 而不是 188：原点吸附到 brick 后最多偏离中心 4 格，两侧再各留 2 格插值余量。
  800 m 图最粗一级因此是 4.44 m 而不是 4.26 m。
- 激活只看空体素：brick 内存在距实体不超过 k 格的**空**体素才激活，全实心 brick 不占池。
- 焦点暂取渲染相机位置，只用于池不足时的分配顺序。
- 发布阈值做成 CVar `r.ambientCube.publishPasses`（默认 4），设为 0 即回到改造前"全权重参与"的行为，
  便于逐项审阅 baseline 差异；Splat 的天空可见度采样同样遵守。
- bake 缓存 `kCacheVersion` 升到 2，key 增加每级体素尺寸与窗口原点。
- 近场各级的 Y 不再"底部对齐场景最低处"：training_ground 的地块底座到 y = −33.5，那样 0、1 级整体埋在
  地下。改为 3.4 的放置规则提前生效：窗口不小于场景的轴盖住全场景（Y 底部对齐），否则以焦点为中心并夹在
  包围盒内，Y 让焦点落在窗口下 1/4 处。阶段 1 的焦点是地图中心向下射线命中的地表；包围盒取 CPU 快照里
  参与 GI 的实例。
- `debugView` 报告 gather 实际取用的级别：池溢出回落到粗一级时不再标红。调试色按 `PaperWhiteNit` 归一化。
- 验收（`assets/agentscripts/ambientcube-clipmap-coverage.agentscript.json`，SoftwareModern）：training_ground
  全图"已发布"，无"窗口外"与"无 cube"；5 级 arena 270.2 MB，冷烘焙 2.7 s 收敛；3 级池溢出 97 个 brick
  （多为底座下表面，回落 4 级）。`follow=0` 场景的 baseline 对比尚未做（本机无 `visual_test_baselines`）。

### 阶段 2：寻址与增量管线

纯重构，窗口仍不移动，画面应与阶段 1 一致。

1. DDA 多级行走，删除 `PageIndex`。这一步会改变长射线的行走方式，用 `gkNextStillBenchmark` 对比性能，
   审阅后更新 baseline。
2. 每级原点数组与环形寻址。原点不动时环绕偏移为 0，画面不变。
3. brick 粒度管线与烘焙队列，整图烘焙改走"所有 brick 进入"。画面不变。

单元测试：窗口平移后重叠 brick 的存储位置不变；进入 brick 集合的计算；子盒距离场在变化区域内与整级
重算逐格相等、其余格子不大于整级重算值；烘焙队列的遍数统计与出队。

### 阶段 3：近场跟随

1. 焦点跟踪、迟滞、场景夹取，滚动任务的准备与提交；瞬移走同一条路径。
2. 烘焙优先级。
3. 磁盘缓存存取各级原点。
4. DevTools 统计与 agent 查询；NextDayz 调参。

验收：agent 脚本在地图上多点瞬移再连续移动，断言待烘焙队列有界；状态调试截图逐点审阅，地表不出现
"窗口外"状态，视点周围在停下后回到"已发布"；冲刺时的帧时间与阶段 2 相比没有可见退步。

### 阶段 4：可选打磨

- 级间过渡带混合，消除随相机移动的精度分界线，并让新进入的 brick 落在权重为 0 的外圈。
- pong 拷贝改为只同步上一批写过的 brick 和被清零的槽位。pong 仍是完整的稳定视图，这需要每级一份
  pong，是显存换带宽。
- brick 级持久化缓存。brick 与世界晶格对齐，内容只取决于场景、级别和世界 brick 坐标，可以按坐标存取。
  立项时重新评估，不沿用 [旧的 tile 级缓存计划](gi-bake-disk-cache-plan.md)。
- 动态几何的局部重体素化：把变化的包围盒换算成 brick 集合送进同一条管线。
- 移动端按显存降级数。

### 文档

各阶段完成时同步更新 [GI 缓存与体素资源](../designs/gi-cache-architecture.md)（arena 的 region 列表、
"明确未实现"一节）、[Bake 磁盘缓存](../designs/ambientcube-bake-cache-design.md)（payload 与恢复流程）
和 [SoftwareModern 性能分析](../designs/software-modern-performance-analysis.md) 里涉及驻留标记的条目。

## 6. 风险

- 粗级对小物体和薄墙会漏或变胖，这是远场低精度的固有代价。
- 级边界处的精度跳变会随相机移动，阶段 4 的过渡带混合处理它；阶段 3 交付时可能可见。
- 着色器热路径多一次环绕，同时少了驻留的原子写，净效果以 benchmark 为准。
- 修改寻址要同步所有消费者：software tracing、VoxelTracing、PathTracing 的 ambient terminal、Splat 的
  场景光照、CPU static_assert、arena sizing 和缓存读回。
- 5 级的显存在移动端可能超预算，现有的预算检查会回退到 NoAmbient，移动端需要显式配置更少的级数。
