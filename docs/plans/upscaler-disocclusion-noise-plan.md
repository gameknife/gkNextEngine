---
title: "升采样器 disocclusion 噪点治理开发计划"
category: plan
status: P0–P3 已实施（2026-11-14，P3 护栏变更已获用户同意）；P4 未做
owner: engine/rendering
created: 2026-11-14
last_updated: 2026-11-14
---

# 升采样器 disocclusion 噪点治理开发计划

目标：运动物体移开后，DLSS / DLAA、FidelityFX FSR、Native TAAU、SGSR2 下露出的区域不再出现强烈噪点，
观感接近 DLSS Ray Reconstruction。约束是不重建颜色历史链，不改 `IUpscaler` 契约，RR 路径保持原样。

本计划的根因分析来自 2026-11-14 的代码审读；P0 的运行时数据见 1.5 节，每条根因的验证状态标在条目后。

实施完成后，仍成立的契约并入 [直接样本后处理与 Upscaler 输入链](../designs/direct-sample-post-chain.md)
和 [Visibility Surface、G-buffer 与 Shading Scheduler](../designs/visibility-surface-gbuffer-shading-scheduler.md)，
本文删除。

## 1. 现象与根因

现象：只有 DLSS-RR 干净；其余升采样器在运动物体扫过的区域留下一片噪点，持续零点几秒。

### 1.1 升采样器的历史兼任了降噪器

`Process.Compose` 把当前帧的 `RT_SINGLE_DIFFUSE * RT_ALBEDO + RT_SINGLE_SPECULAR` 直接写成
`RT_SCENE_COLOR`，前面没有任何降噪。这是现行设计（见 `direct-sample-post-chain.md`），收敛完全依赖
升采样器的时域累积。于是任何"历史失效"的像素都会退回 1spp 原始噪声，再花几十帧重新收敛。

DLSS-RR 不受影响，因为它直接吃 noisy diffuse/specular 和引导缓冲，本身就是带空间先验的降噪器。

### 1.2 `RT_MOTIONMOMENT` 把物体离开后的拖尾整片作废

`Common.CalculateMotionVector`（`assets/shaders/common/SceneSampling.slang`）里有一个读改写副作用：
像素被运动物体覆盖时把计数器置为 `Camera.TemporalFrames`（`r.temporalFrames`，默认 16），被静态
几何覆盖时每帧减 1。所以物体离开后，它扫过的每个像素还要再"被标记" 16 帧。标记期间有三个后果：

1. **DLSS / DLAA 每帧丢历史。**（无法自动验证：`--agent-validation` 禁用 Streamline；P1 已删除掩码，待人工确认） `FUpscalerTypeInfo::requiresInvalidMotionMask` 只对 DLSS 为 true，
   此时运动矢量被写成 `FLT_MAX`（随后乘渲染分辨率，在 `R32G32_SFLOAT` 里是 inf）。DLSS 连续 16 帧
   输出原始样本，之后才开始累积。运动物体自身的像素同样如此。Streamline 头文件里
   `motionVectorsInvalidValue` 的说明只针对 `cameraMotionIncluded == false` 的情形，DLSS-SR 没有
   "无效运动矢量"的公开约定，这里是在依赖未定义行为。
2. **ReSTIR 放弃时域复用。**（代码路径已确认；`r.restir.enable` 默认关，P0 场景未覆盖，P2 已去掉门控） `Restir.slang` 的时域分支要求 `MotionMoment[pixel] == 0`。它后面紧跟的
   instance id 匹配已经能准确拒掉真正换了表面的像素，这个 16 帧门控只是让直接光输入更噪，
   对所有升采样器都生效。
3. **补偿不足。**（已证实加采样有可测作用，见 1.5） `Core.PathTracing` / `Core.SharcQuery` / `PathTracingLiteShading` 在标记像素上把
   采样数乘 4，抵不上几十帧累积。

这个计数器还有两个独立缺陷：它按屏幕像素存储、不随运动矢量重投影，相机一动标记就和场景错位；
`Core.SurfaceBuild` 对未命中像素提前返回，物体划过天空后计数器不会递减。

### 1.3 真实 disocclusion 没有空间滤波兜底

FSR、Native TAAU、SGSR2 不吃 1.2 的无效矢量，但物体移开露出的背景确实没有历史。以 Native TAAU 为例，
`Process.NativeTemporalReproject` 在历史失效时把混合权重归零，置信度每帧只加 0.12，前七八帧基本是
当前帧样本。显示分辨率的后置滤波 `Process.TemporalPostFilter` 默认关闭（`r.upscaler.postFilter`），
开了也是全屏统一强度，不知道哪里是 disocclusion；它作用在乘过 albedo 的颜色上，也帮不到 DLSS。

### 1.4 顺带发现，未验证

- `FidelityFXIntegration.cpp` 的 `EnsureUpscaleContext` 只在 HDR 显示输出时设
  `FFX_UPSCALE_ENABLE_HIGH_DYNAMIC_RANGE`，但送入的 scene color 始终是线性 HDR；同时无条件设了
  `FFX_UPSCALE_ENABLE_NON_LINEAR_COLORSPACE`，而 dispatch 在线性输入时传的 flags 是 0。SDR 显示器上
  FSR 可能把线性 HDR 当 LDR 累积，放大亮噪点。Streamline 路径用的是
  `inputColorIsLinear || hdrOutput`，可作对照。

### 1.5 P0 实测（2026-11-14）

脚本 `assets/agentscripts/upscaler-disocclusion.agentscript.json`（playground.glb，PathTracing，
1066x600 → 1600x900，相机静止，动画运行 150 帧后冻结），指标 `tools/upscaler_disocclusion_rmse.py`：
以冻结后第 64 帧为参考，"拖尾"是冻结前最后一张运动截图与参考差异明显的区域（约占画面 2–3%）。

| 拖尾 RMSE | f1 | f4 | f8 | f16 |
|---|---|---|---|---|
| 基线 TAAU（旧代码，`r.temporalFrames 16`） | 0.039 | 0.030 | 0.028 | 0.027 |
| 基线 TAAU（旧代码，`r.temporalFrames 0`） | 0.038 | 0.029 | 0.029 | 0.027 |
| 基线 SGSR2 | 0.049 | 0.036 | 0.028 | 0.021 |
| P2 后 TAAU（两次） | 0.033 / 0.034 | 0.026 / 0.028 | 0.025 | 0.023 |
| P2 后 TAAU，`r.temporalFrames 0`（两次） | 0.044 / 0.045 | 0.036 | 0.032 / 0.033 | 0.029 |
| P2 后 SGSR2（两次） | 0.050 / 0.051 | 0.036 / 0.037 | 0.028 | 0.021 |

全图 RMSE 在 0.011–0.015 之间，是单帧噪声底。结论：

- 这个场景以日光直射为主，disocclusion 噪声幅度不大，各组差异只有 0.005–0.01，冻结时刻的动画相位
  每次不同，单次对比有噪声，结论只看趋势。
- 新的历史年龄驱动的加采样对 TAAU 有效（同一代码下 0.044 → 0.033），比旧计数器略好；SGSR2 不敏感。
- 加采样之后，f1 拖尾误差仍是 f16 的 1.5–2.4 倍：真正的 disocclusion 仍要靠 P3 的空间滤波。
- 需要更有区分度的场景时，换间接光为主的室内场景（如 `conf_room.glb`）或把 `r.samples` 降到 1。

**测量环境注意（P3 期间发现）**：上表是在当时机器上的 `cvar_user.json` 下测的，帧时约 40 ms。之后跑的
烟测脚本（`checkerboard-renderers-smoke`、`surface-tracing-restir-upscaler`）改写了几个 Archive cvar
并持久化（其中把 `r.samples` 设成默认值会抹掉原有的非默认值），同一份旧代码随后变成约 5 ms/帧、全图噪声
高出 2–3 倍，上表因此**不能**与之后的数字直接比较。脚本现在在运行期间固定 `r.checkerboardRendering`、
`r.exposure.auto`、`r.samples 8`，并用 `accumulateFrames 256` 的渐进图作为参考（不再用 f64），结束时恢复。

## 2. 方案总览

| 阶段 | 内容 | 主要受益 | 规模 |
|---|---|---|---|
| P0 | 复现脚本、基线数据、证实根因 | 全部 | 小 |
| P1 | 删除 DLSS 无效运动矢量掩码（**已完成**） | DLSS / DLAA | 小 |
| P2 | `RT_MOTIONMOMENT` 改为重投影的历史年龄；ReSTIR 与加采样改用它（**已完成**） | 全部（输入噪声） | 中 |
| P3 | 渲染分辨率、按历史年龄驱动的纯空间滤波（**已完成**，定量效果待合适场景验证） | 全部非 RR（根治） | 中 |
| P4 | FSR 标志核实、文档并入、删除本计划 | — | 小 |

P1 和 P2 互相独立，可以分别提交。P3 依赖 P2 的历史年龄。

## 3. 需要用户确认的决定

**P3 与现行设计护栏有一处冲突。** `direct-sample-post-chain.md` 写明"实时路径不做颜色 history
reprojection，也不运行引擎内 spatial denoiser"。P3 的取舍如下：

- 仍然遵守：不引入颜色历史，scene color 仍只来自当前帧样本；不叠第二层 TAA。
- 需要改写：增加一个**无历史的空间滤波**，只在历史年龄不足的像素上生效，收敛后的像素逐位不变。
- 新增的跨帧状态只有"历史年龄"这一张几何计数（P2），性质等同于现有的 `RT_OBJECTID_1` 和
  ReSTIR reservoir，不是颜色。

接手的 agent 可以直接做 P0–P2。**开始 P3 之前，先向用户确认是否接受这处护栏变更**；若不接受，
P3 的退路见第 8 节。

## 4. 分阶段任务

### P0 复现与基线

1. 找或做一个有刚体运动物体的可复现场景。`sys.tickAnimation` 控制动画推进；若现有 `assets/models`
   里没有合适的动画场景，用一个简单的程序化场景或 SCAD 场景加一个往复平移的物体。
2. 写 `assets/agentscripts/upscaler-disocclusion.agentscript.json`，协议固定为：
   动画运行若干帧 → `sys.tickAnimation false` 冻结 → 在冻结后第 1、4、8、16、64 帧各截一张图。
   相机全程不动。可参考 `native-taau-motion.agentscript.json` 的写法，步骤字段见
   `docs/guides/agentscript-reference.md`。
3. 指标：以第 64 帧为参考，算前几帧相对它的 RMSE（全图和拖尾区域各一个）。放一个小脚本在
   `tools/` 下即可，不必进 gnb。
4. 对 Native TAAU（`r.upscaler.type 4`）和 SGSR2（5）跑基线并留档。FSR（3）先确认它在
   `--agent-validation` 下是否可用，不可用就归入人工验证。
5. 证实 1.2：把 `r.temporalFrames` 设为 0 重跑。预期 ReSTIR 门控和 4 倍采样同时消失，拖尾区的
   RMSE 曲线形状发生变化。把结论写进本文第 1 节，替换"没有做过运行时 A/B"的声明。
6. DLSS 的基线只能人工看，方法见第 5 节。请用户在 `r.temporalFrames 0` 和 16 之间对比一次并记录。

完成标准：有脚本、有基线数字、第 1 节的每条根因都标注了"已证实 / 已证伪 / 无法自动验证"。

**状态（2026-11-14）**：1–5 完成，数据见 1.5。FSR 未测；6（DLSS 人工对比）待用户。

### P1 删除 DLSS 无效运动矢量掩码

1. `UpscalerTypes.hpp`：删掉 `FUpscalerTypeInfo::requiresInvalidMotionMask` 及 `kTypes` 里对应的列。
   注意这是按位置初始化的聚合，删列时逐行核对。
2. 删掉随之变成死代码的链路：`VulkanBaseRenderer::RequiresInvalidMotionMask`、
   `Engine.CameraUbo.cpp` 的赋值、`BasicTypes.slang` 的 `UniformBufferObject.UpscalerInvalidMotionMask`
   及其 C++ 镜像、`SceneSampling.slang` 里返回 `FLT_MAX` 的分支。UBO 两侧布局必须同步改，
   拿不准就保留为 padding。
3. 构建 `gkNextRenderer` 和 `gkNextUnitTests`，跑 `[Unit]` 里和 upscaler 相关的测试。

风险：这个掩码当初可能是为了压住"运动物体的影子在背景上滞后"之类的拖影。删除后请用户在 DLSS 下
专门看移动物体的接触阴影和物体自身的边缘。如果确实出现拖影，不要恢复无效矢量，改用 Streamline 的
`kBufferTypeBiasCurrentColorHint`（FSR 对应 `dispatch.reactive`）给一张渐变的提示图，只标光照真正
变化的像素。

完成标准：DLSS 下运动物体本身和它的拖尾都能累积；没有新增可见拖影（人工确认）。

**状态（2026-11-14）**：代码已删除，`UniformBufferObject` 中原 `UpscalerInvalidMotionMask` 的 4 字节
改为 P2 的 `LowHistorySampleScale`，布局不变。人工 DLSS 确认未做。

### P2 历史年龄取代 `RT_MOTIONMOMENT`

把"被运动物体碰过之后倒数 16 帧"换成"这个像素的历史已经连续有效了多少帧"。

1. 语义：`age = 重投影有效 ? min(上一帧 age(重投影位置) + 1, 上限) : 0`。
   重投影有效的判据先用现成数据：落在屏幕内，且 `RT_OBJECTID_1`（上一帧 object id）在重投影位置的
   instance 与当前一致。重投影位置取 2×2 邻域里的最小 age，抖动下偏保守。未命中（天空）像素写上限。
2. 存储：读上一帧、写当前帧，不能原位读改写，否则不同线程互相读到写了一半的邻居。建议
   `RT_MOTIONMOMENT` 保留为当前帧 age，另加一个上一帧槽位，和 `CopyObjectIdHistory` 一起拷贝；
   也可以用 ping-pong。新槽位要同步 `BindlessTexture.slang`、`SurfaceBufferLayout.hpp`、
   `VulkanBaseRenderer.cpp` 的 `CREATE_STORAGE_IMAGE` 和各 renderer 的资源声明。
3. 计算位置：归 `Core.SurfaceBuild`，从 `Common.CalculateMotionVector` 里拿掉这个副作用，让它变回
   纯函数。`PathTracingRenderer.slang` 里还有一处调用，一并处理。
4. 失效：`RenderView::historyGeneration` 变化、camera cut、reset 时全屏归零，和 ReSTIR 的
   `RESTIR_FRAME_TEMPORAL_VALID_BIT` 走同一个触发条件。
5. 消费方：
   - `Restir.slang`：去掉 `MotionMoment[pixel] == 0` 门控，只留 instance id 匹配。
   - `Core.PathTracing` / `Core.SharcQuery` / `PathTracingLiteShading`：加采样条件改为
     `age < 阈值`。倍数和阈值做成 CVar。
   - `Util.VisualDebugger`：该格改为显示 age。
6. `r.temporalFrames` 的含义随之变化，更新 CVar 描述和设置面板文案。MagicaLego 有几处直接写
   `TemporalFrames`（8 / 16 / 512），那是旧累积时代的用法，逐处确认意图后再改。

已知局限：只用 instance id 判不出同一实例内部的自遮挡（比如整块地形是一个实例）。升采样器自己的深度
检测仍会处理那些像素，只是 P3 的滤波帮不上。`RT_PREV_DEPTHBUFFER` 名字带 PREV 但存的是当前帧深度，
不能拿来比。如果 P0 的数据显示这类情况很重要，再加一张上一帧线性深度，作为本阶段的可选扩展。

完成标准：Visual Debugger 里 age 随相机和物体运动正确跟随；P0 脚本的拖尾 RMSE 相对基线下降；
移动物体的阴影没有因为放开 ReSTIR 时域复用而出现滞后。

**状态（2026-11-14）**：已实施，与上面的设计有三处出入：

- **重投影位置取匹配 tap 里权重最大的那个的 age，不取 2×2 最小值。** 最小值是每帧一次的最小值滤波：
  任何亚像素运动下，轮廓处的 0 每帧向内腐蚀一像素，Visual Debugger 里移动物体自身和平移中的整片地面
  都被拉成"刚 disocclusion"。有效性仍然保守：匹配同一 instance 的双线性权重之和须 ≥ 0.5。
- **失效不走 Build 的标志位，而是宿主清零。** `VulkanBaseRenderer::ResetHistoryAgeIfInvalidated` 在
  `historyGeneration` 变化时把 `RT_HISTORY_AGE_PREV` 清成"无历史"哨兵（0xFFFF），对所有写 age 的 pass
  都生效。
- **主 PathTracing 渲染器不跑 `Core.SurfaceBuild`**，它在自己的 primary hit 里解析表面，所以 age 也在
  那里用同一个 `Surface.ReprojectHistoryAge` 计算，并作为渲染器状态交给加采样（不回读本次写的贴图）。

槽位：`RT_MOTIONMOMENT`(14) 改名 `RT_HISTORY_AGE`，上一帧放在原先空着的 slot 11
（`RT_HISTORY_AGE_PREV`），由 `CopyObjectIdHistory` 与 object id 一起拷贝。`r.temporalFrames` 现在是
"age 低于多少帧算 disocclusion 并加采样"（0 关闭），倍数是新 CVar `r.lowHistorySampleScale`（默认 4）；
progressive、缩略图和离屏参考视图一律不加采样。Visual Debugger 右下格显示 age（红 0 → 黄 16 → 绿 32+）。
静止画面下轮廓像素因抖动光栅化在两个 instance 间翻转，age 恒为 0，这是 P3 需要知道的已知行为。
移动物体阴影是否因 ReSTIR 放开出现滞后，未做人工确认。

### P3 按历史年龄驱动的空间滤波（开工前先过第 3 节）

1. 位置：`SamplePostChain::Run` 里，compose 之前。只在实时路径运行；progressive、RR、非主视图跳过。
   PathTracing、PathTracingLite、SoftwareTracing、SoftwareModern 都经过这条链，按 renderer contract
   决定是否启用。
2. 信号：滤 `RT_SINGLE_DIFFUSE`。它是除过 albedo 的光照，滤完再乘 albedo，不糊贴图。
   `RT_SINGLE_SPECULAR` 是完整高光辐射度，只对粗糙度（`RT_NORMAL.a`）高于阈值的像素做较窄的滤波，
   镜面和低粗糙度一律不碰。建议先只做 diffuse，看数据再决定 specular。
3. 滤波器：边缘停止的 à-trous，权重用深度、法线、instance id。强度 `w = saturate(1 - age / N)`：
   age 为 0 时走满核（4–5 趟，步长 1、2、4、8、16），随 age 增长线性收窄，到 N 帧后 `w = 0`、
   输出与输入逐位相同。N 做成 CVar，起点取 16–24，要不短于升采样器自身的收敛时间。
4. 成本：`w == 0` 的像素直接拷贝。画面静止时这个 pass 应接近免费，用 `SCOPED_GPU_TIMER` 量。
   预算：1080p 渲染分辨率、无 disocclusion 时不超过 0.3 ms。
5. 稀疏棋盘格：`IsSparseCheckerboardLightingActive()` 时一半像素没有着色。第一版在该模式下
   直接关闭本 pass，并在文档里写明；之后要支持的话，每个 tap 都得过 `Scheduler.ShouldSkipPixel`。
6. 总开关 CVar，默认开；关闭时整条链路与现在逐位一致，便于 A/B。
7. `gkNextVisualTest` 的 baseline 在静止画面下不应变化（静止时 `w` 全为 0）。如果变了，说明 age 的
   上限或 reset 有问题。

完成标准：P0 脚本里冻结后第 1 帧的拖尾 RMSE 降到接近第 16 帧基线的水平（TAAU ≤ 0.025，
SGSR2 ≤ 0.025，按 1.5 节的数据）；DLSS、FSR 人工确认拖尾不再有明显噪点；静止画面 baseline 无差异；达到成本预算。

**状态（2026-11-14）**：已实施，`Process.DisocclusionFilter.comp.slang` + `SamplePostChain::RunDisocclusionFilter`。

- 实现：3×3 à-trous，步长 1、2、4、8、16（`r.disocclusionFilter.passes`，2–5，默认 5）；强度
  `w = saturate(1 - age / r.disocclusionFilter.frames)`（默认 20）；像素跑 `ceil(w × passes)` 趟、最后按 w
  混合，`w == 0` 的像素一次都不写。边缘停止：同 instance、切平面距离、法线（32 次幂）、亮度（SVGF 式：
  第 0 趟用 3×3 空间方差估噪，σ = 4·stddev，方差放在 ping/pong 的 alpha 里逐趟按权重平方收缩）。
  亮度项是后加的：只有几何停止时，刚 disocclusion 区域里的硬阴影边会被 ±31 像素的核抹掉。
- 缓冲：`RT_DISOCCLUSION_PING/PONG`（slot 19/20，RGBA16F，每个 RT bank 一对）。最后一趟原地写回
  `RT_SINGLE_DIFFUSE`，只在活动像素写、只在非活动像素读，所以至少两趟。
- 跳过：progressive、非主视图、视图不保留 object id 历史、没有时域升采样、Ray Reconstruction、稀疏棋盘格。
  specular 未处理。总开关 `r.disocclusionFilter`（默认开）。
- 功能验证：把 `frames` 设成 60000（所有像素都按刚 disocclusion 处理）对比，diffuse 光照从 1spp 级噪声
  变干净，物体边界、人物脚下和立柱的接触阴影都保留；正常设置下只有拖尾和轮廓像素被写。
- **P0 指标在 playground 上没有区分度**：拖尾只占画面 1–3%，8 spp 下 TAAU 拖尾 f1 RMSE 开/关滤波为
  0.044–0.047 / 0.045–0.051，差异落在运行间波动内；上面的阈值是旧环境的数字，未能验证。要做定量对比，
  需要一个大面积 disocclusion、间接光为主的场景。
- 成本（2400×1350 原生渲染，帧率差估算，GPU 计时只在 Tracy 里有）：静止画面开/关差异在测量噪声内
  （< 约 0.4 ms）；所有像素都活动时约 +1.3–1.6 ms（5 趟）。
- 静止画面并非逐位不变：抖动光栅化让轮廓像素的 object id 在相邻两帧之间翻转，这些像素 age 恒为 0，
  每帧都会被滤一次（只在同 instance 内）。`gkNextVisualTest` baseline 预计在轮廓处有细微差异，未跑。
  一个可选改进是用带抖动的矩阵重投影做 age 匹配，减少轮廓翻转。
- 第一版 shader 跑 P0 时出现过一次 `ERROR_DEVICE_LOST`，之后十余次运行未复现，原因未定位。
- DLSS / FSR 人工确认未做。

### P4 收尾

1. 核实 1.4 的 FSR 标志：对照 FidelityFX SDK 文档确认两个 create flag 在线性 HDR 输入下的正确取值，
   SDR 显示器上做一次前后对比。这是独立的小修，和前面阶段无依赖，也可以提前做。
2. Native TAAU 的可选改进：后置滤波读取 history alpha 里已有的置信度，按 `1 - confidence` 调强度。
   只有 P3 之后 TAAU 仍明显落后于其它升采样器才做。
3. 把仍成立的契约写进 `direct-sample-post-chain.md`（新的滤波 pass 和护栏措辞）与
   `visibility-surface-gbuffer-shading-scheduler.md`（`RT_MOTIONMOMENT` 那一行的新语义）；
   `pathtracing-restir-design.md` 里提到 MotionMoment 的地方同步改。然后删除本文并更新 `docs/README.md`。

## 5. 验证方法与限制

- **Native TAAU / SGSR2**：`gnb validate --script assets/agentscripts/upscaler-disocclusion.agentscript.json`，
  自动化、可回归。
- **DLSS / DLAA**：`gnb shot` 和 `gnb validate` 都带 `--agent-validation`，会强制禁用 Streamline，
  **不能证明 DLSS 生效**。必须在 Windows NVIDIA 环境用正常可见窗口运行（会弹窗，先告知用户），
  日志确认 `DLSS Super Resolution active for <renderer>` 且没有 `slEvaluateFeature` 失败，然后人工观察。
  细节见 `AGENTS.md` 的"DLSS / Streamline 验证限制"。
- **FSR**：先确认 agent-validation 下是否可用，不可用则同 DLSS 走人工。
- **RR 回归**：每个阶段结束都切到 `r.upscaler.type 2` 看一眼，确认没有被波及。
- 构建范围：全部是 Engine 层和 shader 改动，`gnb build`（`gkNextRenderer` + `gkNextUnitTests`）即可。
  改了 UBO 布局或 bindless 槽位后跑一遍单元测试。

## 6. 风险

| 风险 | 应对 |
|---|---|
| 根因分析有误（未经运行时验证） | P0 先证实；不符就改计划 |
| P1 删掉掩码后出现阴影或光照拖影 | 用 bias-current-color / reactive 提示图，不恢复无效矢量 |
| 仅靠 instance id 的 age 漏掉同实例自遮挡 | 可选的上一帧线性深度 |
| P3 滤波在 age 较低时过糊，物体边缘出现"涂抹带" | N 和满核趟数可调；边缘停止权重收紧；先 diffuse 后 specular |
| 滤波与升采样器的邻域裁剪相互作用，收敛变慢或闪烁 | P0 脚本的 RMSE 曲线能直接看出来；必要时缩短 N |
| 移动端成本 | 总开关 + `w == 0` 早退；SGSR2 目标平台上实测 |

## 7. 不做的事

- **恢复完整的 SVGF / 颜色时域累积**：与升采样器的累积叠成两层，带来延迟和拖影，也正是
  `direct-sample-post-chain.md` 明确禁止的。
- **集成 NRD（ReLAX / ReBLUR）**：输入现成（就是给 RR 准备的那些缓冲），效果有保证，但对移动端
  太重，还多一个二进制依赖。如果 P3 的自写滤波质量不够，这是下一步的候选，届时单独立项。
- **只加强显示分辨率后置滤波**：作用在乘过 albedo 的颜色上，会糊贴图；在升采样之后，帮不到 DLSS。

## 8. P3 不被接受时的退路

只做 P1 + P2，再把加采样做得更激进（age 低的像素 8–16spp，按 age 递减）。噪点会明显减轻但不会
消失，成本集中在 disocclusion 发生的那几帧。

## 9. 关键文件

- 合成与后处理链：`assets/shaders/Process.Compose.comp.slang`、
  `src/Engine/Rendering/PipelineCommon/SamplePostChain.{hpp,cpp}`
- 运动矢量与计数器：`assets/shaders/common/SceneSampling.slang`（`CalculateMotionVector`）、
  `assets/shaders/Core.SurfaceBuild.comp.slang`、`assets/shaders/Shader/SurfaceBuffer.slang`、
  `src/Engine/Rendering/PipelineCommon/SurfaceBufferLayout.hpp`、`SurfaceBuildPass.cpp`
- 槽位与资源：`assets/shaders/common/BindlessTexture.slang`、
  `src/Engine/Rendering/VulkanBaseRenderer.cpp`（`CREATE_STORAGE_IMAGE`、`BuildUpscalerFrameInputs`、
  `ResolvePrimaryViewToSwapchain`）、`VulkanBaseRenderer.GpuDriven.cpp`（`CopyObjectIdHistory`）
- 升采样器类型表：`src/Engine/Rendering/Upscaler/UpscalerTypes.hpp`
- 各 provider：`src/Modules/NextStreamline/StreamlineIntegration.cpp`、
  `src/Modules/NextFidelityFX/FidelityFXIntegration.cpp`、`src/Modules/NextTemporalUpscaler/`、
  `assets/shaders/Process.NativeTemporalReproject.comp.slang`、
  `assets/shaders/Process.TemporalPostFilter.comp.slang`
- ReSTIR 与加采样：`assets/shaders/common/Restir.slang`、`assets/shaders/Core.PathTracing.comp.slang`、
  `assets/shaders/Core.SharcQuery.comp.slang`、`assets/shaders/common/PathTracingLiteShading.slang`
- UBO 与 CVar：`assets/shaders/common/BasicTypes.slang`、`src/Engine/Runtime/Engine.CameraUbo.cpp`、
  `src/Engine/Runtime/Config/EngineCVars.cpp`、`UserSettings.hpp`
- 相关文档：`docs/designs/direct-sample-post-chain.md`、
  `docs/designs/visibility-surface-gbuffer-shading-scheduler.md`、
  `docs/designs/pathtracing-restir-design.md`、`docs/AGENT_GUIDE/NativeTemporalUpscaler.md`、
  `docs/guides/dlss-streamline.md`、`docs/guides/fidelityfx-fsr.md`
