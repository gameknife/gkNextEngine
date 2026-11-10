---
title: "Soft Mesh Shader GPU-Driven 提交路径"
category: guide
status: 现行
owner: engine
created: 2026-06-03
last_updated: 2026-11-03
---

# Soft Mesh Shader GPU-Driven 提交路径

当前 raster visibility、wireframe 和 GPU CSM 都使用 Soft Mesh Shader single-draw 路径。它不是硬件 mesh shader：compute 把可见 model section 压实并展开成 primitive stream，graphics pass 再用一次 `vkCmdDrawIndirect(count=1)` 消费该 stream。

## 资源与 slot

`SoftMeshShaderResources` 定义在 `assets/shaders/common/BasicTypes.slang`，保存 soft-mesh 的六个 device address（主 primitive、shadow primitive、visible items、draw args、dispatch args、counters），以及挂在同一张表上的 Lights、LightGrid 和逐 proxy 的 `RayTracingLods`。C++ 对应资源由 `src/Engine/Assets/Core/Scene.Build.cpp` 创建。

| Slot | 用途 |
| ---: | --- |
| 0 | 主 visibility 与 wireframe |
| 1..4 | CSM cascade 0..3 |

primitive buffer 按“实例展开后的三角形需求”定容，不是只看唯一模型 index 数；运行时节点增长超过容量时，Scene 会按需扩容。Visible item 每 slot 上限为 `Scene::kMaxIndirectDrawCount`。

## 每帧流程

实现入口是 `src/Engine/Rendering/VulkanBaseRenderer.GpuDriven.cpp` 的 `DispatchGpuCulling` 与 `DispatchSunShadow`：

```text
clear counters / relevant draw args
  → Compact（section cull + block/wave 聚合 + visible item）
  → Finalize（counter → draw/dispatch indirect args）
  → Expand（visible item → uint primitive stream）
  → vkCmdDrawIndirect(count=1)
```

Compact 有两套等价 shader：

- `Task.SoftMeshShaderGpuCullCompact.comp.slang` / shadow 版本：64-thread workgroup 用 LDS prefix scan，每组少量全局 atomic。
- `*CompactWave.comp.slang`：设备支持 compute subgroup arithmetic + ballot 时使用 wave intrinsics；否则回退 LDS 版本。

不要根据旧 atomic-contention 计划恢复“每个可见 thread CAS 预留区间”的实现。当前 block/wave 聚合保持 gapless layout，并聚合 GPU-driven stats。

Finalize 把每 slot 的 triangle/item counter 转成 `VkDrawIndirectCommand` 和 `VkDispatchIndirectCommand`，并按实际 capacity 钳制。Expand 每个 workgroup 处理一个 visible item，写出 32-bit primitive：

```text
bits 31..17: non-zero instance index（15 bit）
bits 16..0 : section-local triangle index（17 bit）
```

因此单 section 仍不得超过 131072 个三角形，可编码 non-zero instance 为 1..32767。突破该边界必须改编码或拆分数据，不能静默依赖 mask 截断。

Vertex shader 用 `SV_VertexID / 3` 取得 primitive，`% 3` 取得角点，再查 `scene.Nodes`、model offsets、index 与 static/skinned vertex。相关入口：

- `Rast.VisibilityPassSoftMeshShader.vert.slang`
- `Rast.WireframeSoftMeshShader.vert.slang`
- `Rast.ShadowMapSoftMeshShader.vert.slang`

## Shadow 与多视图

Shadow 使用独立 primitive 区间和 slot 1..4。`sunShadowCascadeUpdateMask` 只更新需要的 cascade；同一帧、相同 camera family 的 reference view 可复用已生成的 shadow set，不同 camera family 必须刷新完整集合。

Soft-mesh scratch 当前属于 Scene，全 scheduled view 复用。多视图之间的 transfer/compute/indirect/vertex barrier 是正确性要求；不要把它误当成每个 RenderView 私有资源。画面偶发缺面、shadow 闪烁或次视图污染时，先核对：

- clear → compact 的 counter 可见性；
- compact → finalize/expand 的 counter 与 visible item；
- finalize → indirect read 的 dispatch/draw args；
- expand → vertex read 的 primitive stream；
- 前一个 view 消费完成后 scratch 才能被下一个 view 覆盖。

## Discrete LOD 与硬件光追

`Model::BuildLods` 用 meshoptimizer 为单 section、非蒙皮模型生成最多 `MAX_MODEL_LOD_COUNT` 级索引流，`ModelData` 记录每级的 `lodIndexOffset/lodIndexCount` 和相对 LOD0 的物体空间误差 `lodError`。cull pass 用 `SoftMeshShaderCull.slang` 的 `ComputeScreenExtent` + `SelectLod` 逐 proxy 选级，级别编码进 visibility id 的高位，重建（`Common.get_material_data`）按该级的索引区间取三角形。

硬件光追必须追踪同一个表面，否则从 visibility buffer 重建的主命中点（LOD k 表面）会和 TLAS 里的 LOD0 表面自相交，次级光线（阳光阴影、NEE、反弹）出现黑斑/漏光。因此：

- **BLAS 按级构建**：`CreateBottomLevelStructures` 为每个有简化级的模型每级建一个 BLAS，索引区间直接取光栅 LOD 表；蒙皮模型只有 LOD0（每帧 refit）。
- **TLAS 逐实例选级**：`Task.RayTracingLodSelect.comp.slang` 在 TLAS 重建前运行，用与主视图 cull **完全相同**的矩阵（`PrevViewProjectionUnJit`）、bounds、viewport、`LodBaseThreshold` 和 `SelectLod` 选级，把 CPU 写的实例模板（全 LOD0）复制到 device-local 实例缓冲并改指对应 BLAS，同时写逐 proxy 的 `RayTracingLods`。完全在相机背后的 proxy 不会被主视图画到，用镜像 w 的投影尺寸选级，而不是一律 LOD0。相机移动、节点变化或 LOD 设置变化时重建 TLAS；静止时不重建。
- **命中解码**：`FHardwareRayTracer.TraceRay` 用 `RayTracingLods[instance]` 选 `lodIndexOffset`，`CommittedPrimitiveIndex` 是该级 BLAS 内的序号。实例下标必须等于 NodeProxy 下标，失效模型的槽位写空实例而不是跳过。
- **主命中点对齐**：`r.lod.rayTracingBias > 0`（RT proxy：光追比光栅粗 N 级）、次视图（自己的光栅级）或阈值边界的浮点差都会让两边级别不同。`IRayTracer.ResolvePrimaryOrigin` 在硬件 tracer 里比较 visibility id 的级别和 `RayTracingLods`，不同则沿视线、只在该 proxy 自身的三角形里、在两级 `lodError` 之和允许的窗口内找到被追踪的表面，把起点移过去并保持调用方原有的 standoff。`FPathTracingRenderer.Render` 与 ReSTIR spatial pass 在发出任何光线前调用它；软件 tracer 原样返回。

| CVar | 默认 | 作用 |
| --- | --- | --- |
| `r.lod.rayTracing` | 1 | 光追跟随主视图逐实例选级；0 = 全部追踪 LOD0（对照基线） |
| `r.lod.rayTracingBias` | 0 | 光追 proxy 在上述选择上再粗 N 级；0 = 主视图里光追与光栅完全一致 |

两个开关组合 `r.lod.rayTracing 0` + `r.lod.rayTracingBias N` 即固定级 RT proxy。

## 与旧 MDI 的边界

当前路径不创建 `VkDrawIndexedIndirectCommand` 列表，不查询 `multiDrawIndirect` / `drawIndirectFirstInstance`，不绑定旧 indexed-MDI submit，也没有 `r.drawSubmitMode` 分支。不要为单个平台重新引入第二条常驻提交路径。

## 验证

```bash
./gnb.sh build gkNextRenderer gkNextUnitTests
./gnb.sh shot --scene assets/models/playground.glb
./out/build/<preset>/bin/gkNextUnitTests
```

渲染修改还应检查 visibility、wireframe、四级 CSM、selected/hovered object id，以及至少一个 secondary RenderView；需要多场景 baseline 时运行 `gkNextVisualTest`。
