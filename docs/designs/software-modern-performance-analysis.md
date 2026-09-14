---
title: "SoftwareModern / SoftwareTracing 性能分析与优化方案"
category: design
status: 待实施；代码审计完成，GPU 分项归因待验证
owner: engine/rendering
created: 2026-09-14
last_updated: 2026-09-14
---

# SoftwareModern / SoftwareTracing 性能分析与优化方案

## 1. 结论与证据边界

**目前 SoftwareModern 的实现不足以保证比 SoftwareTracing 快很多。两者已经共用光栅化首交点和大部分整帧流程；Modern 主要替换了间接光求值，却仍承担随机路径采样、昂贵的探针插值、软件光源阴影和完整的 tracing 输出链。**

优先关注以下五点：

1. **比较对象发生了收敛。** SoftwareTracing 不是逐像素追踪 primary ray 的传统软件路径追踪器：它同样从 Visibility/G-buffer 回放首交点，普通不透明材质在第一次反弹后就查询 AmbientCube 终止。
2. **Modern 的探针查询并不便宜。** 默认每个间接样本先查原点，再在命中时查切平面 jitter 点；高光直射估计还可能额外查一至两次。每次都是带体素防漏光、稀疏页表和驻留原子写的八角插值。
3. **有可定位的实现问题。** 驻留功能关闭时仍写原子标记；probe tracer 被用于要求真实几何交点的 glossy MIS；间接光强度控制在 Modern 路径未生效。前两项可能增加成本，后两项还影响画质和比较公平性。
4. **公共工作和调度会掩盖优势。** 烘焙器以目标 GPU 帧时间分配剩余预算；默认 60 FPS 对应 17.5 ms 预算，Modern 省下的时间可能变成更多烘焙工作。公共前后处理、单帧在途以及 present 等待也可能压低整帧收益。
5. **本机整帧 FPS 已出现测量上限。** 本轮隐藏窗口、720p、关闭 upscaler 的完整记录均接近 120 FPS；不能据此断言两种 shader 的 GPU 耗时相同。

审计起点为 `2974c913e866292a4125ae59dacc2aaa3f845691`。本文分为代码确定事实、基于实现的性能推断和本机观察，不把逻辑访存次数当作实际 DRAM 流量，不给出未经 GPU 计时支持的提速承诺。本次仅落文档，未修改 renderer/shader 实现。

## 2. 两种 renderer 实际差在哪里

主要入口：

- [SoftwareTracingRenderer.cpp](../../src/Engine/Rendering/SoftwareTracing/SoftwareTracingRenderer.cpp)：`Render`。
- [SoftwareModernRenderer.cpp](../../src/Engine/Rendering/SoftwareModern/SoftwareModernRenderer.cpp)：`Render`。
- [SwTracingShading.slang](../../assets/shaders/common/SwTracingShading.slang)：`ShadeSwTracingPixel`。
- [SwModernShading.slang](../../assets/shaders/common/SwModernShading.slang)：`ShadeSwModernPixel`。
- [PathTracingRenderer.slang](../../assets/shaders/common/PathTracingRenderer.slang)：`Render`、`ScatterAndTrace`。

| 工作 | SoftwareTracing | SoftwareModern |
| --- | --- | --- |
| Scene 资源需求 | Voxel + Ambient + LightGrid | 相同 |
| 主可见性 | GPU cull + raster visibility | 相同 |
| 首交点 | 全分辨率 SurfaceBuild，回放 G-buffer | 相同 |
| 着色调度 | classify + finalize + 三类 bucket dispatch | 相同 |
| 普通间接光 | 屏幕空间近场尝试 + 分层体素 DDA；命中后 AmbientCube terminal | 在当前表面附近查询 AmbientCube |
| 路径终止 | 普通材质首反弹终止；玻璃可继续 | `ForceExitAfterFirst=true`，玻璃也首反弹终止 |
| 每像素间接样本 | `max(1, r.samples / 2)` | 相同 |
| 太阳直射 | CSM + BSDF | 相同 |
| 有限光源阴影 | 软件 DDA segment | 相同 |
| 镜面面积光补偿 | 几何 glossy proposal | 额外启用 analytic delta 面积光循环，仍调用通用 glossy proposal |
| 输出/compose/upscale | tracing single diffuse/specular + 辅助 RT + 公共后处理 | 相同 |

`SampleDownscale=2` **只是减少路径样本数，不是宽高减半**。默认 `r.samples=2` 时两者均为每个参与着色像素一个间接样本；`r.samples=1` 仍然是一个，调成 4 才变成两个。棋盘格是另外的开关，且 SurfaceBuild 仍按全分辨率执行。

两者使用的 `FNullRadianceCache` 不请求每次 bounce 的 direct lighting；不能看到通用 `RecordSurfaceHit` 调用，就认定它们每跳都重复算直射。普通表面的 primary direct 在样本循环之后计算一次。也不能把 Slang 的接口/泛型直接等同于运行时虚函数成本，需要看实际生成的 shader。

当前实时链路也**没有旧的 diffuse/specular 时域降噪 + 多轮 a-trous 引擎降噪器**。`SamplePostChain` 主要做 compose；progressive accumulation 只在显式 progressive 模式运行。Native TAAU/FSR 后面的可选 post-filter 是另一层工作。见[直接样本后处理契约](direct-sample-post-chain.md)。

因此预期收益应按下式理解：

```text
Tracing GPU = 公共前处理 + 公共直射 + DDA/terminal + 公共后处理 + 可选烘焙
Modern GPU  = 公共前处理 + 公共直射 + probe/jitter + 额外高光路径 + 公共后处理 + 可选烘焙
```

如果 70% 成本是公共部分，剩余 30% 即使减半，整帧也只快约 17.6%。这是解释收益上限的示例，不是本机测得的占比。

## 3. 可以从代码确认的问题

### 3.1 驻留未启用，shader 仍在写热点原子变量

证据链：

- [EngineCVars.cpp](../../src/Engine/Runtime/Config/EngineCVars.cpp)：`r.ambientCube.hitDrivenResidency` 和 `bounceHitAffectsResidency` 默认 false，`hitMarkTileRatio` 默认 0.25。
- [Engine.CameraUbo.cpp](../../src/Engine/Runtime/Engine.CameraUbo.cpp)：`AmbientCubeCascadeParams.z` 无条件填入 clamp 到 `[0.01, 1]` 的标记比例，没有根据驻留开关清零。
- [AmbientCube.slang](../../assets/shaders/common/AmbientCube.slang)：`gatherAmbientCubes` 调用 `MarkAmbientBrickHit`；后者只判断比例，最终对 `lastConsumerHitFrame` 或 `lastBounceHitFrame` 执行 `InterlockedMax`。
- [BrickPageTable.cpp](../../src/Engine/Assets/Acceleration/BrickPageTable.cpp)：`requested[b] = (!hitDriven || inGrace || recentlyHit)`。驻留关闭时命中时间不影响保留决策，但仍可服务诊断统计。

同一砖块、同一帧的 hash 相同：0.25 **不是每像素独立抽掉 75% 原子写**，而是抽到的砖块上所有查询都继续争用同一个地址。Modern 的原点/jitter/额外 glossy 查询会放大这一问题。当前已把每个角的标记合并为每次 gather 一次，不应把“八次改一次”再作为新优化。

**建议：** 只有驻留策略或显式诊断需要时才开启标记；consumer 和 bounce 分别控制。若保留默认统计，应改为低成本汇总或按需统计，避免诊断反过来主导 shading。启用驻留时再评估同 brick 的 subgroup 去重，并保持移动相机下的覆盖和淘汰语义。

这是确定的非必需工作；具体耗时要通过去除标记前后的 `shadingpass` 和 shader atomic/counter 观察确认。现有 CVar 最低为 0.01，**不能用设置 `hitMarkTileRatio=0` 作为当前版本的可靠消融实验**；需要增加调试开关或修正 UBO gating。

### 3.2 Probe tracer 不满足 glossy MIS 所需的交点契约

证据链：

1. [RayTracers.slang](../../assets/shaders/common/RayTracers.slang) 的 `FAmbientCubeRayTracerIndirect.TraceRay` 只填入 probe 颜色和固定 `outHitDist=100`；不更新 `outVertex` 或 `outNode`。返回的 bool 来自天空可见度阈值，不是几何求交结果。
2. [PathTracingRenderer.slang](../../assets/shaders/common/PathTracingRenderer.slang) 在 Modern 的 `Render` 尾部仍调用 `EvaluateGlossyDirect(tracer, ...)`。
3. 该函数对 Mixture/Metallic/Dielectric 额外调用 `TraceRay`，然后用 `reflectedHit.Position/MaterialIndex` 判断是否命中注册面积光。由于 tracer 没有更新交点，这里拿到的仍是原始非发光表面。
4. [Lighting.slang](../../assets/shaders/common/Lighting.slang) 的 `EvaluateLightDirectSampleWithPdf` 却对非 delta 面积光镜面项应用 `PowerHeuristic(lightPdfOmega, glossyPdf)`，假设另一条 BSDF proposal 会补回互补贡献。

**确定的问题：** Modern 的这条 BSDF proposal 无法提供真实面积光命中，NEE 端却仍采用双策略 MIS。粗糙面积光高光存在缺失互补项的偏差；具体视觉严重程度需专用场景验证。`EnableAnalyticDeltaLightReflection=true` 只处理近 delta 材质，不能修复粗糙高光。

**性能代价：** 原有间接光查询之外，又执行一次完整 probe gather；理想玻璃还可能为透射执行一次。面积光命中分支没有得到它需要的数据。返回 miss 时仍会计算太阳盘，所以不能直接删除整个函数后就认定画质等价。

**建议：** 将 `SampleAmbientRadiance` 与真正的 `TraceGeometry` 接口分开，直接光策略显式声明是否支持 BSDF emitter proposal。

- 粗糙高光若仅采用 NEE，就令相应面积光镜面 MIS 权重为 1，不执行无效 probe glossy proposal。
- 若需要近镜面反射，使用有界的真实 screen-space/DDA reflection 或明确的 analytic emitter 策略；NEE 权重与实际参与策略配套。
- 太阳盘及 delta 透射要单独保留/校正，不能把“面积光无需双策略”推广成全局关闭所有 MIS。

这项应先修正确性，再衡量节省，不能以高光变暗换 FPS。

### 3.3 `r.gi.indirectIntensity` 在 Modern 的 probe 路径未生效

`IndirectIntensity` 的 shader 使用点是 SHARC query 与 `ApplyTerminalRadiance`。SoftwareTracing 命中后的 ambient terminal 会乘这个系数；Modern 通过 `FuzzyTracing` 直接乘 `tempColor`，随后 `ForceExitAfterFirst`，跳过 `ApplyAmbientTerminal`，probe sampler 本身也没有乘该系数。

这是两个 renderer 的调参契约不一致，会影响 GI 强度和画质比较；**不是主要性能根因**。

修复前先分开 bounce、天空和 emitter 通道，只对定义为间接反弹的能量应用系数。不能对 `FullAmbientCubeSampler` 整体简单乘一次，否则会同时改变其 sky/emissive direct 成分。

### 3.4 烘焙预算没有直接覆盖整池快照成本

[VulkanBaseRenderer.GiBake.cpp](../../src/Engine/Rendering/VulkanBaseRenderer.GiBake.cpp) 的 `BakeAmbientCubeCascade` 每次有效 dispatch 之前，都把所选 cascade 的**整个已分配 cube pool** 复制到 pong；并不是只复制本帧 dispatch 的 dirty probes。

默认 `192 × 192 × 48` 个 probe、pool ratio 0.5、每 cube 40 B，对应一次约 **33.75 MiB** 的复制，读写两端逻辑流量合计约 67.5 MiB。本机启动日志也报告 pong 33.8 MB。即使本帧只烘焙少量 group，仍付这笔固定成本；实际内存传输时间取决于缓存与驱动。

调度器的 bake timestamp 从复制之后、`vkCmdDispatch` 之前开始。整帧计时仍包含复制，所以不能称为“完全漏计”，但这笔固定烘焙成本被归到了 `nonBakeMilliseconds`，不能由减少 group 数有效缩小。在前台已超预算时至少仍烘焙一个 group，固定复制负担也仍然存在。

**建议：** 分别记录 bake snapshot/copy 和 trace 时间，让预算感知固定成本；再按依赖边界设计快照复用、dirty brick 范围复制或分批更新。不能直接移除 pong 或仅复制当前写入 brick：`interpolateAmbientCubesStable` 可能读邻居，必须保留稳定读视图和防止同一 dispatch 读写竞态。

这是烘焙活跃期的确定成本，两者都会承担；**烘焙收敛后不再 dispatch，不能用它解释所有稳定场景的性能差距**。

## 4. 设计成本与需验证的风险

### 4.1 Modern 通常每个间接样本做两次八角插值

`AmbientCubePositionJitter=2.0` 使 `ScatterAndTrace` 执行：

```text
原始表面附近的 probe 查询
  ├─ miss：使用天空，结束
  └─ hit：切平面 jitter 点再查一次
           命中则平均两个颜色，否则保留原始颜色
```

每次 `gatherAmbientCubes` 包含：cascade 搜索、最多八个角的体素 clearance 检查、防漏光计算、稀疏 brick 查表、cube fetch、RGB9E5 解码、方向权重和归一化。有效角还要执行 `sqrt/rsqrt` 等运算；没有有效权重时可能继续查更粗 cascade。

仅按 cube 数据计算，两个八角查询最多触及 640 B 的逻辑 payload，尚未计入体素/页表、失败 cascade 和高光额外查询。**这是源码层面的访问量估算，不是每像素实际读取 640 B DRAM 的测量。**

防漏光查询有明确画质目的，不应直接删掉。值得测量的改进是：

- 以原点查询为稳健基线，让 jitter 按时域或局部不连续度有条件发生，避免每帧固定两次。
- 多样本时原始位置不变，尝试复用八角可见性、索引/权重，或先插值方向基系数再求多个方向；注意额外寄存器的代价。
- 把防漏光 validity 与 radiance 查询拆开，判断原点不可用时不要读取完整颜色；有命中再决定是否需要第二份完整辐射度。
- 为低频 diffuse GI 建立独立半分辨率路径，用 full-resolution depth/normal/object-id 做重建。保持直接光全分辨率。

### 4.2 Modern 并未去掉直接光的软件追踪

`FSoftwareTracingDirectIlluminator` 对太阳使用 CSM，对点光/面积光使用软件有限线段 DDA。Modern 仅换了 indirect tracer，并未换这个 direct illuminator。

因此有较多近场光源、长阴影射线或复杂体素遮挡时，减少间接光 DDA 只能省去一部分。两者默认并不运行软件 ReSTIR 调度；不要把 `r.restir.enable=true` 误读为两者都执行了 ReSTIR 的时空复用 pass。

Modern 还在近 delta 材质上执行 `AnalyticDeltaLightIlluminate`：遍历 `GetGlobalLightCell()` 中的光源，逐个做面积光筛选和反射方向测试，候选命中后再追踪遮挡。复杂度随全局光源表增长，而非仅随一个随机 NEE 样本增长。低粗糙度像素很多时，它可能抵消省下的 indirect tracing。

优化时要用有保守覆盖保证的候选 light list/方向范围筛选或面光层级结构，不能盲目换成过小的局部列表而漏掉远处可见镜面光源。

### 4.3 “Indirect” 命名并不等于只包含间接反弹

`FAmbientCubeRayTracerIndirect` 当前实际用 `FullAmbientCubeSampler`。但这**不等于太阳直射必然重复计算**：`sampleAmbientCubeDirectSurface` 当前读取 `SkyEmissiveDirect`，不含 `SunDirect`，因此看到 Full 就改成 ID sampler 会错误地删掉天空贡献。

需要进一步验证的是面积光：probe baker 把直接命中发光几何的能量写入 `SkyEmissiveDirect`，Modern 在当前表面附近读取它，同时又执行 finite-light NEE。这里存在注册 emitter 直接项重叠的风险。SoftwareTracing 的 probe terminal 位于真实次级交点，积分语义不同，不能简单统一替换 sampler。

建议把 `Bounce`、`SkyDirect`、`RegisteredEmitterDirect` 和必要的未注册 emissive 成分分清，再决定每个估计器负责哪一项。用“只开一个面积光，关闭天空和太阳”的隔离场景做能量验证。

### 4.4 全分辨率公共成本仍然很大

[SurfaceBufferLayout.hpp](../../src/Engine/Rendering/PipelineCommon/SurfaceBufferLayout.hpp) 定义七个 surface plane，逻辑写入量合计 42 B/pixel；两种 renderer 还请求 8 B/pixel 的 specular albedo。仅这部分约为 **50 B/pixel 的逻辑输出**，另有 motion moment 读改写、visibility 输入、纹理解码及后续重新读取。

此外还包括：

- `ShadingSchedulerPass` 的计数器重置、classify、finalize、三次 indirect dispatch 及同步。
- diffuse/specular/hit-distance 输出，compose、firefly clamp、outline 等像素处理。
- 根据配置启用的 checkerboard resolve、upscaler、post-filter，以及大气/最终输出和 UI。
- 两者相同的 LightGrid、visibility 与 CSM 需求。CSM 已有 cascade update mask 和共享复用，不能把四级每帧全量重画当作既定事实。

分桶对重 tracing kernel 有利，但对未来足够轻的 Modern kernel，分类及同步可能不划算。应比较“含分类的总 GPU 时间”，而不是只看 bucket kernel 自身变快。

也应按消费者审计 hit-distance/specular-albedo 输出。当前 native temporal 实现没有显式消费这几个输入字段；调试视图、其他 upscaler 和 checkerboard 契约仍要保留。通过需求位控制生成更合理，不能仅凭“在 macOS 上”永久删除资源。

### 4.5 M3 Max 上重点观察访存与占用率，不能只数射线

M3 系列硬件本身支持加速光线追踪；这里讨论的是本引擎使用的软件 renderer 路径，不能把 M3 Max 称为没有 RT 硬件的 GPU。实际 Vulkan feature 可用性仍应由所选驱动与 `SupportsRayQuery` 检查决定。[Apple M3 说明](https://www.apple.com/newsroom/2023/10/apple-unveils-m3-m3-pro-and-m3-max-the-most-advanced-chips-for-a-personal-computer/)

对当前 Slang → SPIR-V → MoltenVK/Metal 路径，需优先检查：probe 非连续读取、同址原子竞争、通用材质 shader 的资源占用、全屏 RT 往返和 compute/transfer 同步。把局部数据放进 threadgroup memory 或一律改成 half，并不保证更快；Apple 对 M3/A17 Pro 特别指出应结合新缓存层级、数据类型和资源利用率选择优化。[Metal shader 性能建议](https://developer.apple.com/videos/play/tech-talks/111373/)

“Modern 被带宽/低 occupancy 限制”目前是合理假设，**尚无本轮 GPU counters 支持**。用 Xcode Metal debugger/Instruments 观察 limiter、occupancy、内存等待、spill 和 SIMD 利用率后再定性。[GPU occupancy 检查](https://developer.apple.com/documentation/xcode/finding-your-metal-apps-gpu-occupancy)

## 5. 本机观察：为何仅看 FPS 容易得出错误结论

### 5.1 环境与本轮取得的数据

2026-09-14，本机 `Apple M3 Max`，macOS 26.6.2。构建缓存为 RelWithDebInfo、Tracy 开启；已执行 `./gnb.sh build gkNextMotionBenchmark`，增量构建成功。所选 GPU 日志为 MoltenVK 1.4.1（CSV 的编码版本显示 `MoltenVK 0.2.2209`），实际 swapchain extent 1280×720，SDR、Immediate。

运行采用 `--agent-validation --forcesoftgen --forcenort --no-shader-hotreload`，禁用 upscaler、post-filter、checkerboard、引擎 frame pacing/burst limit，`r.samples=2`；关闭 disk bake cache，每个场景重新收敛。正式配置预热 20 s，计时 6 s。测试程序会自动转动相机，因此这是短时运动观测，不是固定视角的 GPU 微基准。

| 已完成记录 | 场景 | 帧时间 ms | FPS | 计时帧数 |
| --- | --- | ---: | ---: | ---: |
| SoftwareTracing | GIBootcamp | 8.335 | 119.98 | 720 |
| SoftwareModern，第 1 次 | GIBootcamp | 8.334 | 120.00 | 720 |
| SoftwareModern，第 2 次 | GIBootcamp | 8.345 | 119.83 | 719 |

本轮配置原计划为三个场景、各四次 ABBA；进程在第四个 run 的测量完成前结束，退出码为 0，日志没有说明退出原因。**只有上面三条完整记录，不构成完整 ABBA 或跨场景结论。** 各已完成 run 的 bake 均在测量开始前收敛；从 committed scene 到收敛约 10.4–11.8 s。预跑的 10 s warmup 曾不足，相关数据不纳入上表。

本地复核文件位于 `out/analysis/swmodern-m3max/baseline.json`、`baseline.csv`、`baseline.log`；这些是忽略的实验产物，不作为仓库长期依赖。工作期间还存在其他并行编辑，本文没有把这次短测宣称为锁定全部工作树内容后的正式性能基线。

### 5.2 这些数字证明什么、没有证明什么

[BenchMark.cpp](../../src/Application/Render/gkNextBenchmark/Common/BenchMark.cpp) 使用 `Window::GetTime()` 累加两次 tick 间的 wall-clock 时间。它测到的是整帧，包含 CPU、等待和提交，**不是 shading GPU timestamp**。

日志明确显示：`delta=wall clock burstLimit=off presentMode=unsynchronized display=120.00Hz`。结果仍贴近 120 Hz，说明这条路径存在强烈的显示/提交节奏限制嫌疑；不能简单归咎于引擎主动限帧，更不能认定 GPU shader 正好各需 8.33 ms。下一步应测 CPU 的 `fence`、`acquire-frame`、present 等待与 GPU `[gpu]`/`shadingpass`。

[FrameSubmission.cpp](../../src/Engine/Rendering/FrameSubmission.cpp) 每次会等前一次 submit fence，`kFramesInFlight=1`；submit 又以 `ALL_COMMANDS` 等待 image-available。这限制了录制/执行的重叠，也把独立 offscreen 工作挂在 drawable 可用性后面。但 CPU/GPU 主循环不是简单的全串行加法，仍需时间线确认实际重叠和空泡。

**不建议把 frames-in-flight 直接改成 2 当作首个修复。** 当前共享 scene buffer、ambient arena 和资源生命周期需要一起审计。更安全的顺序是先分离 GPU 执行耗时与 WSI 等待，再考虑延迟 acquire、独立 offscreen 提交以及资源分帧。

### 5.3 烘焙自适应预算是另一个比较陷阱

[AmbientBakeScheduler.hpp](../../src/Engine/Rendering/AmbientBakeScheduler.hpp) 使用：

```text
targetMs = 1000 / bakeTargetFps × 1.05
bakeBudgetMs = max(0, targetMs - nonBakeGpuMs)
nextGroups ≈ bakeBudgetMs / measuredMsPerGroup
```

默认 60 对应 17.5 ms；若两个 renderer 都有 dirty work，Modern 的更低前台成本可能换成更多 bake groups，整帧仍接近同一预算。收益应表现为更快收敛，不一定立即表现为更多 FPS。这是调度目标，不是硬性 60 FPS cap；没有 dirty brick 时 scheduler idle。

因此必须分别报告：冷启动收敛时间、dirty 更新期间前台帧时间、完全收敛后的稳定帧时间。

## 6. 优化顺序

| 优先级 | 改动 | 验证重点 | 主要边界 |
| --- | --- | --- | --- |
| P0 | 建立 GPU 分项基线、核实 WSI 等待、分离 bake 活跃/稳定状态 | 两 renderer 的 GPU ms、CPU wait 和实际 render extent | FPS 触顶的数据不能用来算 shader 加速比 |
| P1 | 驻留/诊断关闭时跳过 consumer/bounce 标记 | GPU 原子工作减少；驻留开启时不误淘汰 | 显式诊断仍应可用 |
| P1 | 修正 Modern glossy direct 的 tracer/MIS 契约 | 高光能量正确，移除无效 probe proposal | 太阳盘、delta 反射/透射分别验证 |
| P1 | 明确 probe 能量通道与 indirectIntensity 语义 | 天空/面光不重复计能量、间接系数一致 | 不能直接把 Full sampler 换成 ID |
| P1 | 将双位置 probe gather 改为可控策略，复用原点插值 | query 次数、shading ms、薄墙漏光和闪烁 | 优先减少冗余，不删除防漏光约束 |
| P2 | Modern diffuse GI 单独半分辨率/低频更新；直接光保留全分辨率 | 降低 GI 像素工作量，边缘/运动重建质量 | 属于画质预算策略，非无损优化 |
| P2 | 独立轻量 Modern shader，专用 diffuse 与 reflection 策略 | 寄存器/occupancy、编译产物及 kernel 时间 | 通用模板中已被编译器折叠的代码不算收益 |
| P2 | native 路径按需求写 RT；重评 classify 与 compose 成本 | SurfaceBuild + classify + shade + compose 合计时间 | 保留调试、多视图和 upscaler 输入契约 |
| P2 | bake 固定复制计入预算，优化快照/dirty 粒度 | copy ms、trace ms、总收敛时间、动态场景 p95 | 保留邻居读快照和读写同步 |
| P3 | 限定精确镜面像素的 DDA/SSR，优化 analytic 面光候选集 | 反射完整性、候选数和追踪耗时 | 不能因减少候选漏掉远方反射光源 |
| P3 | CPU 提交/WSI 重叠与多帧资源模型 | GPU 空泡、CPU 录制、延迟及数据竞争 | 不是简单增加 swapchain image 或 frame 数 |

### 推荐的 Modern 目标结构

```text
Visibility / 完整 Surface
    ├─ 全分辨率直接光：CSM + 受控的局部光可见性
    ├─ 低频 diffuse GI：probe irradiance + 深度/法线引导重建
    └─ 有需求的 glossy 像素：有限反射追踪或定义明确的近似
             ↓
         Compose → 统一 upscaler / 输出
```

核心是让 Modern 的主要成本与低频 GI、需要反射的像素比例相关，而不是每个普通表面都执行缩减版路径采样器。Diffuse 可考虑直接评价插值后的 irradiance；必须核对当前六面存储基函数和积分归一化，不能把沿法线取一次值当作自动无偏替代。

按宽高各减半计算，GI shading 像素理论上减少 75%，但 reconstruction 有成本，直射/前后处理不变，因此**不等于整帧提速四倍**。最终是否比先优化现有 gather 更好，由稳定场景 GPU 分项占比决定。

不推荐以关闭 GI、改用 SoftwareModernNoAmbient 来代表这项优化成功；NoAmbient 可作为公共 raster/post 成本的下界参照，其光照功能和画质并不等价。

## 7. 后续可执行验证方案

### 7.1 先做可归因的 baseline

1. 固定代码、编译配置、驱动、GPU、电源/热状态；确认没有其他 GPU 重负载。不要擅自关闭用户程序。记录 scene/camera、材质统计、光源数和渲染像素数。
2. 先用 None upscaler、相同 `r.samples=2`、checkerboard off、post-filter off、相同 CSM/大气/compose 设置。然后单独补测实际使用的 Native TAAU/质量档，核实 render extent，而不仅看窗口尺寸。
3. 静态对比需等待 CPU voxel 工作和 GPU bake 全部 idle；移动/动态场景另开一组。现有固定 warmup 不会自动等待 bake，必须根据日志或新增完成条件确认。
4. 每场景用 ABBA，至少三组；计时窗口建议不少于 10 s，保留每帧样本用于 median/p95。现有 CSV 只有平均值，不能从它推算 p95。
5. 720p、1440p 各一组；若整帧仍贴近显示刷新率，以 GPU 时间为准。`--agent-validation` 适合软件路径隔离，但结果不自动代表正常可见窗口的 present 性能。

场景至少包括：GIBootcamp（probe/漏光）、MaterialShowcase（粗糙度/金属/玻璃）、LightingShowcase（有限光源），加用户实际觉得“差距很小”的场景与视角。`gkNextMotionBenchmark` 当前只接受注册的 `.proc` DemoScene；用户 glTF/SCAD 场景应通过 `gnb validate/shot` 或普通 renderer + Tracy 测，不要直接塞进 benchmark 配置。

基准工具还需注意两个细节：`--benchmark-config` 应传绝对路径，避免运行时工作目录/资源根解析导致读取失败后悄悄运行默认场景；`ApplyCurrentRunSettings` 会为 SoftwareTracing + Native TAAU 自动开启 post-filter，时序又涉及当前 renderer 状态，测试该组合时应明确统一两边的 filter 配置并核实实际生效值。本轮 `upscaler.type=0` 不进入这条特殊分支。现有配置格式可参考 [motion_benchmark.example.json](../../assets/configs/motion_benchmark.example.json)，不要直接采用其三秒预热作为 bake 已完成的保证。

### 7.2 计时与消融

现有 Tracy 区域可以先看：`[gpu]`、`sw-lightbake`、`MShader cull`、`visibility pass`、`shadow pass`、`surface build`、`shading classify`、`shadingpass`、`sample compose`、`temporal upscaler resolve`、`Temporal a-trous filter`、`ui`。参考 [Tracy 使用指南](../guides/tracy-profiling.md)。

`shadingpass` 的范围目前略不一致：Tracing 将资源 transition 包在 timer 内，Modern 在 timer 前 transition。细粒度比较应统一包围范围，或对完整的 SurfaceBuild → Compose 区间计时；不能把差出的同步时间全归为 shader。

建议增加以下**尚未实现**的诊断能力，分别开关，不把它们写成当前已有 CVar：

- primary direct / indirect probe / glossy proposal 的分项 shader 或计数器；
- 原点/jitter query 数、平均有效角数、cascade fallback 数；
- DDA 步数、光源候选数、驻留标记次数；
- bake copy 与 bake trace 的独立 GPU 时间；
- 等待 bake idle 的测量起点，以及固定视角/固定相机时间段模式。

一次只改一项：标记 gating → jitter 策略 → glossy/MIS → 低频 GI → RT/调度。关闭直接光、GI 或防漏光的实验只用于成本归因，不是最终画质验收。`r.gi.indirectIntensity=0` 既不影响当前 Modern probe 路径，也不是“停止执行 GI”的开关。

### 7.3 画质与回归标准

| 项目 | 最小验证内容 |
| --- | --- |
| 正确性 | 单面积光、单太阳、仅天空分别测试；粗糙度扫描；间接系数 0/1/2；无不合理能量突变 |
| 防漏光 | 薄墙、墙角、室内外接缝、细柱、cascade 交界、缺失 brick |
| 动态质量 | 相机旋转/平移、反遮挡、移动光源、金属/玻璃、低分辨率 GI 的边缘与拖影 |
| 公平性 | 同 scene/camera、samples、实际分辨率、bake 状态和完整后处理配置 |
| 性能 | 稳态 GPU median/p95、前台 CPU 等待、dirty 更新 p95、收敛时长同时报告 |

先完成 P1，再决定是否推进 P2 架构改变。每项只有在重复测量中超出噪声区间且画质通过时才记为收益；不能用本轮 120 FPS 数据作为未来优化的精确基准。

实际修 shader/engine 时按仓库规则执行 targeted `./gnb.sh build` 和相关单元测试；视觉验证先用 `./gnb.sh shot --scene <scene>`，需要回归对比时再跑 `gkNextVisualTest`。本次文档分析未实施这些修复，也未宣称它们已通过视觉验证。
