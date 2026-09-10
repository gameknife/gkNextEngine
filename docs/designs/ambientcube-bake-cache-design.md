# AmbientCube Bake 磁盘缓存

已实施。场景结构不变时，启动后的 AmbientCube bake（CPU 体素化 + 距离场 + GPU 光照收敛）被一次磁盘
读取替代。本文记录现行契约。

## 1. 边界

- 缓存**只覆盖一次完整 bake 的产物**：voxels、brick table / active brick list、page index、
  ambient cube pool。
- 有效性只由**一个 64 位 key** 决定，key 只 hash 场景的初始结构（见 §3）。key 不同 = 未命中 = 走
  原来的完整 bake。
- 缓存永远只是加速：文件缺失、损坏、尺寸不符、key 不符，一律静默回退到完整 bake。
- 不做增量缓存、局部失效、缓存淘汰、跨机共享。运行时改场景仍走原有的增量重烘路径。

## 2. 数据流

单一触发点仍是 `Scene::RebuildMeshBuffer` → `FCPUAccelerationStructure::AsyncProcessFull`
（[Scene.Build.cpp](../../src/Engine/Assets/Core/Scene.Build.cpp)）。

```
AsyncProcessFull(scene, arenaMemory)
  ├── InitCascadeBakers(...)                     // 网格确定后才能算 key
  ├── key = ComputeAmbientBakeKey(scene, inputs)
  ├── 命中 → TryRestoreAmbientBakeCache()  → 直接进入“已烘完”状态，函数返回
  └── 未命中 → 原完整 bake，ambientCacheWriteArmed_ = true

AcknowledgeAmbientBake(revision)                 // GPU 32 轮收敛后由 BakeAmbientCubeCascade 调用
  ├── ambientCachePendingSave_ = ambientCacheWriteArmed_   // 只置标志，不在渲染路径上读显存
  └── ambientCacheSaveFrame_ = totalFrames + 3             // 收敛帧的 dispatch 必须先退休

FCPUAccelerationStructure::Tick(...)             // Scene 每 30 帧调一次
  └── FlushPendingAmbientBakeCacheSave()         // 主线程只做：帧号检查 + 尺寸校验 + 派发任务
        └── TaskCoordinator 后台任务（全部在子线程）：
              ├── 拷 voxels / brick table / page index
              ├── Map + memcpy 读回 cube pool     // BAR 读，秒级，绝不能放主线程
              └── lzav 压缩 + 写文件（.tmp → rename）
```

实现位置：

| 文件 | 职责 |
|------|------|
| [AmbientBakeCache.hpp](../../src/Engine/Assets/Acceleration/AmbientBakeCache.hpp) / [.cpp](../../src/Engine/Assets/Acceleration/AmbientBakeCache.cpp) | key 计算、文件格式、压缩读写 |
| [CPUAccelerationStructure.cpp](../../src/Engine/Assets/Acceleration/CPUAccelerationStructure.cpp) | `TryRestoreAmbientBakeCache` / `FlushPendingAmbientBakeCacheSave` 与三个状态成员 |
| [Scene.hpp](../../src/Engine/Assets/Core/Scene.hpp) | `MarkAmbientCacheRestored` / `ConsumeAmbientCacheRestored` |
| [VulkanBaseRenderer.cpp](../../src/Engine/Rendering/VulkanBaseRenderer.cpp) | `SetScene` 里的 clear-pass 抑制 |
| [DeveloperStatusBar.cpp](../../src/Modules/DevTools/UI/DeveloperStatusBar.cpp) | footer bake 指示器的 `Saving` 阶段 |

## 3. Cache key

`ComputeAmbientBakeKey` 用 XXH64 流式 hash 下列内容（顺序固定，实现见 AmbientBakeCache.cpp）：

1. `kCacheVersion` + `sizeof(VoxelData/AmbientCube/PageIndex)` + `CUBE_SIZE_XY/Z` +
   `GPU_SCENE_AMBIENT_BRICK_EDGE` + `BRICKS_PER_CASCADE` + `ACGI_PAGE_COUNT`
   —— 结构体或网格常量一改，全部缓存自动失效，不需要有人记得改版本号；
2. 网格与后端：`baseUnit`、`offsetBias`、`cascadeCount`、`cascadeRatio`、`poolBricksPerCascade`、
   `hardwareBake`（HW/SW bake 结果不同，必须分开）；
3. 参与 GI 的实例：`modelId` + 世界矩阵 + `matIdxs`；
4. 每个 `Model` 的顶点位置与索引数组；
5. 每个材质的 `gpuMaterial_` 原始字节；
6. `scene.Lights()` 原始字节；
7. 太阳与天光：`HasSun/HasSky/SunRotation/SunElevation/SunIntensity/SunColor/SkyIdx/SkyIntensity/
   SkyColor/SkyRotation`。

两条必须守住的规则：

- **第 3 项的实例筛选必须与 `CaptureBuildInput` 逐字一致**（`GetModelId() != -1`、`GetVisible()`、
  `participation & (giBake|gpuAs)`）。否则 key 覆盖的和实际烘的不是同一批几何。
- **顶点按位置逐个拷贝后再 hash，不能直接 hash `Vertex` 数组**：`Vertex` 是 ALIGN_16，尾部 padding
  是未初始化字节，直接 hash 会让 key 在同一份场景上抖动。

顺序依赖：实例顺序来自 `scene.Components<RenderComponent>()`。不稳定的后果只是不命中，不会错误恢复。

**shader 改动不在 key 里**：改 `Bake.SwAmbientCube.comp.slang` / `Bake.HwAmbientCube.comp.slang` /
`AmbientCubeBaker.slang` / 距离场算法 / `convergencePasses` 之后，必须手动 `kCacheVersion += 1`。
开发 bake shader 期间用 `r.ambientCube.diskCache 0` 关掉缓存。

## 4. 文件

路径复用 cook 缓存机制：`Utilities::CookHelper::GetCookedFileName("{:016x}", "ambient")` →
`<writable root>/cooked/ambient<key>.gncook`，和 `cpubvh` / `tangent` 并排。

```
header: magic 'GKAB' | version | key | cascadeCount | cascadeCapacity
        | voxelCountPerCascade | poolBricksPerCascade
section × 6: rawSize(u64) | compressedSize(u64) | lzav bytes
  voxels                  cascadeCount   * voxelCountPerCascade
  brickTable              cascadeCapacity * BRICKS_PER_CASCADE
  activeBrickList         cascadeCapacity * poolBricksPerCascade
  activeBricksPerCascade  cascadeCapacity
  pages                   ACGI_PAGE_COUNT^2
  cubes                   cascadeCapacity * poolBricksPerCascade * BRICK_VOLUME
```

- `cascadeCount`（CPU baker 数）与 `cascadeCapacity`（arena 分配的 cascade 数）分开存：两者通常相等，
  但分开存才能保证 cube pool 的 brick→slot 映射不被重塑。
- 压缩用 `lzav_compress_default`（**不是 `_hi`**，后者在 100MB 量级太慢）。
- 写入先落 `.tmp` 再 rename，崩在半路不会在真实 key 下留残文件。
- 不存的两项：`distanceToSolidSeeds`（恢复时按 `matId > 0 ? 0 : kMaxDistanceFieldSeed` 一趟重建）、
  `residency`（arena 创建时已清零，与冷启动一致）。

## 5. 恢复后的状态

`TryRestoreAmbientBakeCache` 让引擎落在**与冷启动收敛后完全相同**的状态：

- `cpuBrickTable` 的 brickTable / activeBrickList / activeBricksPerCascade 按缓存原样恢复
  （**必须原样**：cube pool 是通过它索引的，重算 slot 分配会把恢复的光照打散），
  dirtyBricks 全零，`dirtyRevision = 0` —— 于是 `BakeAmbientCubeCascade` 第一行就返回；
- `fullProbeBakePending_ = false`、`ambientBakeIdle_ = true`、`needFlush = false`、
  `totalVoxelGroups_ = 0`，进度 UI 显示 Idle 而不是闪一个假进度条；
- `scene.MarkAmbientCacheRestored()`。

**clear-pass 抑制是唯一的正确性陷阱**：`CommitSceneToRenderer` 的顺序是 `RebuildMeshBuffer()`
（恢复点）→ `SetScene()`，而 `SetScene` 原本无条件 `RequestClearAmbientCubeCache()`，会在下一帧把
恢复好的 cube pool 清零。现在 `SetScene` 改成消费 `ConsumeAmbientCacheRestored()`：恢复过就不清。

## 6. 写入：武装条件与线程

`ambientCacheWriteArmed_` 只在**未命中的完整 bake**里置位，`AsyncProcessFull` / `ClearAllTasks`
会重置这几个成员。命中的那次不 arm（不会把刚读出来的东西再写一遍）。

**整个保存过程都在子线程**，主线程只做帧号检查、尺寸校验和一次任务派发。两个前提让它成立：

- **不需要 `vkDeviceWaitIdle`**：`kFramesInFlight == 1`，每帧开始都会等上一次提交的 fence，所以
  收敛帧之后再过 2 帧，那一帧的 bake dispatch 必然已经退休。`ambientCacheSaveFrame_ =
  totalFrames + 3` 就是这个门槛（`Tick` 30 帧一次，实际从不因此多等）。
- **读 bake 状态不需要加锁**：收敛把 `ambientBakeIdle_` 置位，体素化队列和 residency 重建都不会再
  碰 `cascadeBakers` / `cpuBrickTable` / `cpuPageIndex`；唯一可能回来的写者是新的完整 bake，而通往
  它的两条路（`Scene::CleanUp` 和 `~Scene`）都先调 `ClearAllTasks()`，那里会等这个任务结束，之后
  才动这些状态和 arena 本身。

代价：冷缓存那一次，如果在写盘完成前就切场景，`ClearAllTasks()` 会等它（见 §8 的秒级读回耗时）。
换来的是 bake 结束时**游戏线程一帧都不卡**。

因为这段时间不再是"什么都没发生"，`EProbeBakeStage::CacheSave` 把它暴露给 UI：
`GetProbeBakeProgress()` 在 `ambientCachePendingSave_ || ambientCacheSaveInFlight_` 时返回这个阶段，
footer 的 bake 指示器显示 `Bake — Saving`（不定进度），落盘后才变 `Complete`。少了这一段，用户会在
"已完成"的显示下遇到一次切场景阻塞。

## 7. 开关与清理

- CVar `r.ambientCube.diskCache`（默认 `true`，Archive）：关掉则既不读也不写。
- 清缓存 = 删 `<writable root>/cooked/ambient*.gncook`。没有自动淘汰。
- 磁盘上遗留的 `<writable root>/gicache/` 目录来自更早一次尝试，当前代码无任何引用，可直接删除。

## 8. 实测（playground.glb，RTX 5070 Ti）

| 项 | 值 |
|----|----|
| arena（3 cascade，pool 1728/3456） | 175.8 MB（cubes 101.2 / voxels 40.5 / pong 33.8） |
| 缓存文件（1 cascade） | 16.5 MB（原始约 50 MB，lzav ≈ 3×） |
| 冷启动：读回 + 压缩写盘（**全在子线程**） | readback 1–3 s，compress+write ≈ 80–100 ms |
| 冷启动：游戏线程增加的开销 | 一次尺寸校验 + 任务派发，量级 μs |
| 热启动：恢复（主线程，在场景提交里） | **45–80 ms**，之后完全没有 bake |

热启动省掉的是整段 CPU 体素化 + 距离场 + 32 轮 GPU 收敛。

readback 的绝对耗时随负载浮动很大（BAR 内存的主机读本来就慢，子线程还要和满速渲染的主线程抢
PCIe），但它不再落在游戏线程上。真要把它压下去，正路是 `vkCmdCopyBuffer` 到
`HOST_VISIBLE|HOST_CACHED` staging buffer 再读——那是优化，不是现在的实现。

## 9. 验证

- `gkNextUnitTests "[AmbientBake]"`：
  - `[Unit]` 文件往返 + key/截断/损坏头一律判定为未命中；
  - `[GPU][Integration]` key 随实例位移、太阳角度、网格参数变化，同场景稳定；
  - `[GPU][Integration][Slow]` 端到端：冷启动烘完并写盘（同时断言写出的 cube 不是全零、voxel 有实体），
    第二次加载命中缓存且 600 帧内不出现任何 bake 工作。该用例**会清空** `cooked/ambient*.gncook`
    （前后各一次），并在结束时把它改过的 archived cvar 还原——否则测试会顺手改写开发者自己的
    renderer / GI 设置。
- 手工肉眼验证：`gnb shot --scene assets/models/playground.glb`（需要一个请求 ambient cube 的
  renderer，如 `r.rendererType 1`）跑两次，冷/热两张截图的 GI 一致。

**注意 bake 节奏**：`AmbientBake::PlanNextDispatchGroups` 用帧时间对比 `r.ambientCube.bakeTargetFps`
决定每帧 dispatch 多少组。当帧时间本来就长于目标帧时（例如 `EngineTestFixture` 强制 1/30 s delta，
或隐藏窗口下 bake 自身把帧拖长），控制器会一直判定“已经太慢”而停在每帧 1 组，收敛要几十万帧。
测试与离线验证把 `r.ambientCube.bakeTargetFps` 调到 1 让它放开跑；这是现有 bake 调度的性质，
与缓存无关。
