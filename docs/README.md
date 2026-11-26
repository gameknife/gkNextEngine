# gkNextRenderer 文档索引

本目录只保留能解释当前代码、当前操作方式或明确剩余工作的文档。已完成的一次性实施计划、被取代的
方案和重复审计已退出文档面；历史过程通过 Git 追溯。

## 权威性与生命周期

发生冲突时按以下顺序判断：

1. 当前代码、CMake、配置与 `AGENTS.md`；
2. 本索引中的 guide/design 与 `AGENT_GUIDE/`；
3. Git 历史只作历史证据。

`guide` 说明现在怎么做，`design` 记录仍成立的边界与契约，`plan` 只保留尚未完成且范围明确的执行
顺序。完成 plan 中仍有价值的内容应并入 design/guide，然后删除 plan；不要长期保留旧文件清单、行号、
进度表和一次性验收日志。

工作方式（对话驱动、design + plan、一步一提交）见 `AGENTS.md` 的“工作流”一节。下方“剩余计划”是
可发现的积压，用户在对话里要求时才开始做。

## 操作指南

- 构建与工具：[CMake 结构](guides/cmake-structure.md) · [gnb CLI](guides/gnb-cli.md) ·
  [gnb 架构](guides/gnb-architecture.md) · [发布流程](guides/release-process.md) · [下一版 Release 草稿](releases/next-release.md)
- 平台与显示：[SDR/HDR10/EDR](guides/display-output-modes.md) ·
  [macOS/MoltenVK FIFO 排障](guides/macos-moltenvk-fifo-present-troubleshooting.md) ·
  [Windows Dozen 排障](guides/dozen-vulkan-backend-troubleshooting.md) ·
  [Steam Deck 部署](notes/steamdeck-deployment-notes.md)
- 渲染扩展：[DLSS/Streamline](guides/dlss-streamline.md) ·
  [FidelityFX FSR](guides/fidelityfx-fsr.md) · [Tracy](guides/tracy-profiling.md) ·
  [Soft Mesh Shader](guides/soft-mesh-shader-gpu-driven-submit.md)
- 内容与交互：[SCAD 场景创作](guides/scad-scene-authoring-guide.md) ·
  [TUI 模式](guides/tui-mode.md) · [VITURE Carina AR](guides/viture-ar.md)
- 验证：[agentscript 脚本字段参考](guides/agentscript-reference.md)（写 `gnb validate` 脚本前先查这页）

## 当前架构

### Runtime、渲染与平台

- [渲染运行时架构与契约](designs/rendering-runtime-architecture.md)
- [双平面 Visibility Buffer](designs/massive-visibility-buffer-design.md)
- [Visibility Surface、G-buffer 与 Shading Scheduler](designs/visibility-surface-gbuffer-shading-scheduler.md)
- [直接样本后处理与 Upscaler 输入链](designs/direct-sample-post-chain.md)（含历史年龄、disocclusion filter、首跳 Sobol 采样与升采样器噪声度量）
- [Tracing Direct Lighting 与 ReSTIR DI](designs/pathtracing-restir-design.md)
- [SoftwareModernNoAmbient 与 GTAO](designs/software-modern-noambient-rendering.md)
- [SoftwareModern / SoftwareTracing 性能分析与优化方案](designs/software-modern-performance-analysis.md)（代码审计完成；优化待实施）
- [大气散射与高度雾](designs/atmosphere-and-height-fog-design.md)
- [GI 缓存与体素资源](designs/gi-cache-architecture.md)
- [AmbientCube Bake 磁盘缓存](designs/ambientcube-bake-cache-design.md)
- [CPU TLAS 快照与后台重建](designs/cpu-tlas-snapshot-architecture.md)
- [RenderView 多视图](designs/multi-viewport-renderview-design.md)
- [Gaussian Splat / SOG v2](designs/gaussian-splatting-sog-design.md)
- [iOS A12X Compatibility Minimal Render](designs/ios-a12x-compatibility-minimal-render-mvp.md)
- [移动端 application 目标](designs/mobile-application-targets.md)
- [Agent 输入驱动验证](designs/agent-validation-input-driver.md)
- [WebRTC Remote Play](designs/webrtc-remoteplay-design.md)

### Editor、脚本与内容管线

- [Desktop UI Foundation](designs/desktop-ui-foundation.md)
- [Editor 材质创作](designs/editor-material-authoring.md) ·
  [Settings/CVar](designs/editor-settings-and-cvars.md) · [Sequencer](designs/editor-sequencer.md)
- [.NET 脚本运行时](designs/dotnet-scripting-design.md) ·
  [托管游戏 Launcher 与 PIE](designs/managed-game-launcher-design.md)
- [NextAI 产品化边界](designs/nextai-product-focused-architecture.md) ·
  [gnb AI Bridge v2](designs/gnb-ai-bridge-protocol-v2.md)
- [SCAD Scene Compose](designs/scad-scene-compose-design.md) ·
  [统一场景文档](designs/scad-unified-scene-document.md) ·
  [Terrain](designs/scad-terrain-design.md) ·
  [ScadLibrary AI 创作实现](projects/scadlibrary/ai-authoring.md)
- [真实地理数据生成城市关卡](designs/geo-city-generation-design.md)
- [运行时 biome 草地与共享 BLAS 风动](designs/runtime-terrain-grass.md)
- [场景导出 glTF/GLB 契约](designs/scene-export-gltf-contract.md)

## AGENT 专题指南

- C#：[开发应用](AGENT_GUIDE/CSharpGameDevelopment.md) ·
  [.NET Bindings](AGENT_GUIDE/DotNetBindings.md) · [反射](AGENT_GUIDE/ReflectionSystem.md) ·
  [Hot Reload](AGENT_GUIDE/HotReload.md)
- SCAD：[Loader](AGENT_GUIDE/SCADLoader.md) · [资产 Playbook](AGENT_GUIDE/ScadAssetPlaybook.md) ·
  [Terrain](AGENT_GUIDE/ScadTerrain.md) · [ScadRig](AGENT_GUIDE/ScadRig.md)
- Loader/渲染：[LDraw](AGENT_GUIDE/LDrawLoader.md) · [Gaussian Splat](AGENT_GUIDE/GaussianSplat.md) ·
  [Native Temporal Upscaler](AGENT_GUIDE/NativeTemporalUpscaler.md)
- Gameplay：[CharacterDemo](AGENT_GUIDE/CharacterDemo.md) · [SimKit](AGENT_GUIDE/SimKit.md) ·
  [Brotato3D](AGENT_GUIDE/Brotato3D.md) · [MagicaLego](AGENT_GUIDE/MagicaLego.md) ·
  [NextTotalwar](AGENT_GUIDE/NextTotalwar.md) · [NextWorldTravel](AGENT_GUIDE/NextWorldTravel.md) ·
  [NextAstrobot](AGENT_GUIDE/NextAstrobot.md) · [ScadPlay（边玩边改 + agent 协议）](AGENT_GUIDE/ScadPlay.md)
- 通用代码审查：[编码规范](AGENT_GUIDE/coding-standards.md)

## 项目文档

- [AirportSim](projects/airport-sim/architecture.md)
- Brotato3D：[项目介绍](projects/brotato-3d/introduction.md) ·
  [配置与玩法开发](projects/brotato-3d/developer-guide.md)
- CitySolSim：[经营循环设计](projects/citysolsim/management-loop-design.md) ·
  [剩余计划](projects/citysolsim/management-loop-plan.md)
- [Flappy C++/C# parity](projects/flappy-bird-parity/introduction.md)
- NextAstrobot：[3D 平台跳跃设计](projects/nextastrobot/nextastrobot-design.md)（已实施；
  现行契约见 [AGENT_GUIDE/NextAstrobot.md](AGENT_GUIDE/NextAstrobot.md)，设计文末 §11 列剩余打磨项）
- NextDayz：[3C 与 ScadRig](projects/nextdayz/nextdayz-3c-scadrig-design.md) ·
  [PVE 生存循环](projects/nextdayz/nextdayz-productization-design.md) ·
  [网格背包与物品尺寸](projects/nextdayz/inventory-grid-design.md) ·
  [持久世界与直升机撤离](projects/nextdayz/nextdayz-extraction-design.md) ·
  [局外经济与合同任务](projects/nextdayz/nextdayz-economy-contracts-design.md)（实施中：E1～E4 已完成，E5 度量与调参未开始） ·
  [世界派遣与地理关卡](projects/nextdayz/nextdayz-world-map-design.md) ·
  [游戏内调试器](projects/nextdayz/nextdayz-debugger.md) ·
  [启动模式与多人联机](projects/nextdayz/nextdayz-multiplayer-design.md) ·
  [Web 客户端架构设计](projects/nextdayz/nextdayz-web-client-design.md)（已实施，文中“现状”标出差距） ·
  [Web 客户端剩余计划](projects/nextdayz/nextdayz-web-client-plan.md)（中档独显与 Firefox/Safari 实测、浏览器内 SCAD 定论、e2e 接入 CI） ·
  [Web 客户端开发指南](guides/nextdayz-web-client.md) ·
  [服务器打包与部署](guides/nextdayz-server-deployment.md) ·
  [客户端打包与分发](guides/nextdayz-client-package.md)
- NextRaiden：[竖版街机射击 MVP 设计](projects/nextraiden/nextraiden-mvp-design.md) ·
  [开发计划](projects/nextraiden/nextraiden-mvp-plan.md)（实施中：P0–P2 已完成，关卡 1 可通关）
- NextRA：[架构不变量](projects/nextra/architecture.md) ·
  [经济与建造设计](projects/nextra/economy-build-design.md) ·
  [剩余计划](projects/nextra/economy-build-plan.md)
- NextTotalwar：[行军/地图约束](projects/nexttotalwar/nexttotalwar-mvp-design.md) ·
  [基础战斗循环](projects/nexttotalwar/nexttotalwar-productization-design.md)
- [ScadStudio](projects/scad-studio/architecture.md) ·
  [ScadLibrary AI 实现](projects/scadlibrary/ai-authoring.md) ·
  [StudioSim](projects/studio-sim/architecture.md) ·
  [TruckerDemo](projects/trucker-demo/architecture.md)

## 剩余计划

- [大地图 GI 覆盖：Voxel / AmbientCube clipmap](plans/ambientcube-clipmap-plan.md)（阶段 1–4 已实施：
  5 级贴合场景布局、probe 发布规则、DDA 多级行走、环形寻址、brick 粒度管线、近场跟随滚动、级间过渡、pong
  增量同步、几何变化局部重体素化、按显存降级数、内存 brick 缓存；brick 缓存落盘待定）
- [NextRaiden MVP](projects/nextraiden/nextraiden-mvp-plan.md)（实施中；P0–P2 已完成，P3/P4 待做）
- [验证中心 MVP](plans/validation-center-mvp-plan.md)（待实施；CLI 自动留档、Dashboard 查看问题与可见重跑）
- [iOS NativeAOT C# 真机支持](plans/ios-nativeaot-plan.md)（P0–P2 已完成；待真机验收）
- [PathTracing 材质模型统一](plans/shader-material-model-unification-plan.md)（待审阅）
- [地理城市生成剩余项](plans/geo-city-generation-plan.md)（P0–P5 已完成；只保留未完成项）
- [ScadLibrary AI 创作收口](plans/scadlibrary-ai-authoring-plan.md)（M0–M4 已完成；只剩 ScadStudio 退役门槛）

未在本页重复列出的具体 loader、游戏代码入口和验证命令，以对应 `AGENT_GUIDE/`、代码旁注释与
`gnb <command> --help` 为准。
