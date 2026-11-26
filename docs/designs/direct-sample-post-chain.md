---
title: "直接样本后处理与 Upscaler 输入链"
category: design
status: 现行
owner: engine/rendering
created: 2026-07-20
last_updated: 2026-11-26
---

# 直接样本后处理与 Upscaler 输入链

PathTracing、SoftwareTracing 和 SoftwareModern 共用
`PipelineCommon::SamplePostChain`。实时路径不做颜色 history reprojection；renderer 生成的
当前帧 diffuse/specular 样本直接 compose 成 scene color，随后交给 DLSS、FSR 或 native resolve。
唯一的引擎内空间滤波是只作用于"升采样器还没有历史"的像素的 disocclusion filter（见下文），
收敛后的像素逐位是 renderer 的原始样本。

## 当前链路

1. renderer 写 `RT_SINGLE_DIFFUSE`、`RT_SINGLE_SPECULAR`、`RT_ALBEDO` 和 upscaler 所需
   G-buffer/motion/hit-distance。
2. PathTracing 开启 ReSTIR 时，由 `Core.RestirSpatialShade` 先把面光直接项加入
   `RT_SINGLE_DIFFUSE`（SoftwareTracing 的 ReSTIR 支持已移除）。它共享 reservoir
   和算法主体；ReSTIR 时域复用是光照采样器自身状态，不是颜色历史。
3. `Process.DisocclusionFilter.comp.slang`（`r.disocclusionFilter`）只在 `RT_HISTORY_AGE` 小于
   `r.disocclusionFilter.frames`（默认 20）的像素上，对 `RT_SINGLE_DIFFUSE` 做边缘停止 a-trous
   （同 instance、切平面、法线、SVGF 式亮度停止），强度 `w = 1 - age / frames`，`w == 0` 的像素
   不写。第二轮（`r.disocclusionFilter.specular`）滤 `RT_SINGLE_SPECULAR`：只处理粗糙度 > 0.15、
   至多 3 趟，额外有粗糙度、albedo（specular 未解调）停止，接触反射（反射段 < 0.5 m）收窄核。
   progressive、非主视图、无 object id 历史、Ray Reconstruction、稀疏棋盘格时整个 pass 不跑。
4. `Process.Compose.comp.slang` 读取 single diffuse/specular 与当前 albedo，叠加编辑器
   outline，输出线性 HDR 的 `RT_SCENE_COLOR`。可选的 firefly 抑制（`r.compose.fireflyClamp`，
   阈值 `r.upscaler.fireflySigma`，默认 2.5）在**解调域**、只对同 instance 的 3×3 邻居比较：
   上限取 `max(mean + kσ, 2 × 次亮邻居 + 5% 显示白)`，只压 1–2 spp 估计器的十倍级孤立亮点；
   specular 只在粗糙度 > 0.15 时处理（锐利镜面里的点光是真实的）。它不写回
   `RT_SINGLE_*`，progressive、offline 参考、RR、稀疏棋盘格时关闭。Compose 同时写
   `RT_UPSCALER_REACTIVE`（slot 22）：漫反射主导为 0，粗糙度 < 0.25 且 specular 主导的像素
   0.25–0.55，编辑器 outline 0.85；目前只有 FSR 消费。
5. DLSS、FidelityFX FSR 3.1、Native TAAU、SGSR2 或 native presentation 统一消费
   `RT_SCENE_COLOR`。DLSS Ray Reconstruction 的
   noisy diffuse/specular resource 直接绑定 `RT_SINGLE_DIFFUSE/SPECULAR`，不经过引擎滤波。

Tracing renderer 的 direct-lighting 通道语义是强契约：`RT_SINGLE_DIFFUSE` 保存
albedo-demodulated diffuse lighting，compose 时才乘 `RT_ALBEDO`；`RT_SINGLE_SPECULAR` 保存已经
包含 Fresnel、metal tint、BSDF 与 emitter radiance 的完整 specular radiance，compose 不再乘
base color。ReSTIR 第二阶段只能累加前者，不能覆盖主 pass 已写入的 glossy/delta direct。

FidelityFX temporal FSR 与 Native TAAU 可在 evaluate 后运行同一层显示分辨率的可选后处理
（`r.upscaler.postFilter`，默认关）：upscaler 先写入每个 swapchain image 独立的 ping/pong
intermediate，随后 firefly neighborhood clamp 与以当前帧 albedo/normal 引导的多 pass a-trous
滤波写回 swapchain。强度按 `RT_HISTORY_AGE` 调制：刚 disocclusion 为全强度，age 达到
`r.disocclusionFilter.frames` 时降到四分之一，所以已收敛的纹理不发糊；这个调制来自几何，
三种 upscaler 共用，不要求 upscaler 报告自己的置信度。该阶段不参与 renderer 样本生成、
历史重投影或 DLSS 路径。

## 历史年龄（RT_HISTORY_AGE）

`RT_HISTORY_AGE` 记录像素下的表面已经连续多少帧可被重投影（0 = 刚 disocclusion，饱和于 255），
是几何账本，性质同 `RT_OBJECTID_1`，不是颜色。写入方是 `Core.SurfaceBuild`，主 PathTracing
渲染器自己解析 primary hit，用同一个 `Surface.ReprojectHistoryAge` 写。判据：

- 当前命中点经 instance 的 `combinedPrevTS` 和上一帧**带抖动**的 `Camera.PrevViewProjection`
  投影，落在上一帧光栅的精确亚像素位置；静止画面下轮廓不会因抖动相位变化而反复清零
  （未消抖的 motion vector 差一个 jitter 增量，最多一像素）。
- 2×2 双线性 tap 中，要求 `RT_OBJECTID_1` 同 instance，且 `RT_DEPTH_HISTORY_PREV`（slot 21，
  上一帧 `RT_PREV_DEPTHBUFFER` 的拷贝）的线性深度与该点上一帧应有的深度一致（相对 3% + 按掠射
  角放宽的一像素深度斜率 + 2 cm），这样同一实例内部的自遮挡（地形、整栋建筑、单 mesh 角色）
  也会清零。匹配权重和 ≥ 0.5 才算有效，age 继承权重最大的匹配 tap（不取最小值，否则每帧
  腐蚀一圈）。
- `historyGeneration` 变化时宿主把 `RT_HISTORY_AGE_PREV` 清成哨兵 0xFFFF，所有表面下一帧从 0 开始。
  age、object id、depth 三张上一帧平面在 `CopyObjectIdHistory` 里一起拷贝。

消费方：disocclusion 加采样（`r.temporalFrames` 内乘 `r.lowHistorySampleScale`，PathTracing、
PathTracingLite、SoftwareTracing、SoftwareModern 都适用）、disocclusion filter、temporal post filter
的强度调制。

## 首跳采样序列

`r.sampling.sobol`（默认开）让 `FPathTracingRenderer::Render` 在每个样本的首跳（lobe、方向、
fuzzy jitter、首个命中点的光源样本）和每帧一次的 primary direct 上打开一个采样窗口：窗口内
`RandomFloat*` 的前若干维来自按像素随机旋转的 shuffled Owen-scrambled 2D Sobol（Burley 2020），
样本序号是 `TotalFrames × spp + i`，超出维度预算和窗口外仍是 PCG。实现全在 `ConstFunc.slang`
的 per-invocation 静态状态里，采样代码的接口不变；2D 请求总是占整对维度。

屏幕空间结构化偏移（8×8 Bayer rank 经 R2，即零资产"蓝噪声"）做过并实测：它让所有非 RR
upscaler 的帧间闪烁升高 9–16%，因为 clip box 与 history lock 把这种结构当成了信号。不要在
没有对比数据时把空间偏移改回结构化图案。

## 噪声度量

- `assets/agentscripts/upscaler-flicker.agentscript.json` + `tools/upscaler_flicker_rmse.py`：
  冻结场景、2 spp，每组 renderer/upscaler 连拍 8 帧，输出无参考的帧间闪烁和对 256 帧
  progressive 参考的误差。单张 256 帧参考本身仍有约 0.02–0.04 RMSE 的噪声，比较不同构建时用
  `--reference-run` 固定同一份参考（或对多份参考取平均）。
- `assets/agentscripts/upscaler-disocclusion.agentscript.json` +
  `tools/upscaler_disocclusion_rmse.py`：物体移开后的拖尾区域随帧数的误差曲线。
- 两者都在 `--agent-validation` 下运行，DLSS 与 FSR 被禁用，只覆盖 Native TAAU / SGSR2。

`SoftwareModernNoAmbient` 和 `VoxelTracing` 有自己的 compose shader，但输出契约同样是
`RT_SCENE_COLOR`，因此后续 presentation 不需要知道 renderer 类型。

PathTracing、SoftwareTracing、SoftwareModern 和 SoftwareModernNoAmbient 都声明通用 upscale
能力。只有 PathTracing 声明 Ray Reconstruction contract；在其他 renderer 选择 type 2 时，
该类型不可用并回到 native rendering，不会隐式改成另一种 upscaler。

## 唯一保留的颜色累积

显式 offline progressive rendering 仍需要多帧收敛。该模式只运行
`Process.ProgressiveAccumulate.comp.slang`，先将当前帧的 diffuse/specular 按 direct-light
契约合成为线性 scene radiance，再在单个 `RT_PROGRESSIVE_SCENE_COLOR` 中做原位 running
average，然后由同一个 compose pass 读取。该 history 在 primary RenderView 第一次确实
需要 progressive rendering 时按需创建，并保持到本次 swapchain 资源销毁；实时路径不分配它。
它不读取 motion、ObjectId 或上一帧投影，不是 reprojection，也不参与实时路径。
offline progressive 保留主相机 jitter 以获取不同子像素样本，但不调用 temporal upscaler 或 Frame Generation；
最终 `RT_SCENE_COLOR` 通过普通线性 blit 映射到 swapchain，避免再次按 motion vector 重投影已经累积的结果。

## 已删除资源与接口

每个 RenderView 不再拥有 `TemporalResolve` 或 `AtrousDenoiser`。下列全分辨率 image slot
及其生命周期已经删除：三张 diffuse/specular/albedo color history、六张 a-trous
ping/pong/output，以及 accumulate albedo。相关 denoiser/reprojection CVar 和 UI 也不存在。

`RenderView::historyGeneration` 仍保留，服务 camera cut、renderer/scene/extent 切换时的
previous UBO、ObjectId/depth 与 ReSTIR reservoir 失效；它不代表存在颜色 history。

## 修改护栏

- 新 renderer 应输出 single lighting + current G-buffer，再复用 `SamplePostChain`；不要在
  renderer 内重新建立一条颜色历史链。
- 引擎内空间滤波只允许是 disocclusion filter 这种形态：只混合当前帧样本、只作用于历史年龄
  不足的像素、收敛后逐位不变。跨帧状态只允许几何量（age、object id、深度）和 ReSTIR
  reservoir。
- DLSS Ray Reconstruction 必须拿到未滤波、未 firefly 抑制的 `RT_SINGLE_DIFFUSE/SPECULAR`；
  任何新的 pre-upscale 处理都要在 RR 激活时跳过。
- upscaler 的 reset、jitter、motion-vector 和 exposure 契约由 upscaler integration 管理；
  不要为了“加强 DLSS”再叠一层引擎 TAA/reprojection。
- FSR 输出滤波必须保持无历史、只读本帧 FSR intermediate，并在显示分辨率执行；不要将它
  扩展成另一条 temporal accumulation 链。
- realtime scene color 必须来自当前样本。只有用户明确进入 offline progressive 模式时才
  允许读取 `RT_PROGRESSIVE_SCENE_COLOR`。
- temporal upscaler provider 必须实现现有 `IUpscaler`/scene-color 契约，不应要求 renderer
  恢复旧 storage。当前 `NextStreamline` 与 `NextFidelityFX` 互斥注册同一个 provider factory。
- `RT_MOTIONVECTOR` 的全引擎契约是 render-pixel 单位；Streamline 与 FidelityFX 都提交
  1:1 motion-vector scale。renderer 不得单独写 normalized UV motion。
