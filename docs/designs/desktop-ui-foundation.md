---
title: "Desktop UI Foundation"
category: design
status: 现行
owner: desktop-ui
created: 2026-08-09
last_updated: 2026-11-24
---

# Desktop UI Foundation & Unified UI Architecture

`gkNextEngine` 的 UI 框架采用“核心零 ImGui 依赖 + 可插拔分层宿主 + 统一现代 GPU 渲染后端”架构，依赖严格单向向下：

- **Engine Core (`src/Engine/`)**：零 ImGui 依赖，仅定义纯粹宿主契约 `Runtime::IUiHost` 与可插拔图层契约 `Runtime::IUiLayer`（位于 `src/Engine/Runtime/Interface/UiHost.hpp`）。
- **NextUI (`src/Modules/NextUI/`)**：实现 `NextUI::FUiSystem`（`IUiHost`）与统一 GPU 渲染器 `NextUI::FUiRenderer`，对外仅暴露 `NextUI`（桌面工作台/通用控件）和 `NextUI::Hud`（2D/3D 游戏 HUD）两个命名空间。
- **NextRmlUi (`src/Modules/NextRmlUi/`)**：以可插拔 `Runtime::IUiLayer`（`EUiLayerOrder::Document`）接入 `IUiHost`，保留编译几何并直接向 `FUiRenderer` 提交 `FUiCustomDrawStream`，完全不经过 ImGui DrawList。
- **DevTools / Application / C# (`UiCanvas`)**：在 `NextUI` 之上构建开发者诊断面板、产品编辑器与 C# 托管游戏 HUD。

## 单一物理模块与 API 入口 (`src/Modules/NextUI/`)

- `NextUI.hpp`：桌面与通用 UI 唯一聚合头（包含 `FontAtlas`、`Theme`、`Scopes`、`Widgets`、`Containers`）。
- `Theme.{hpp,cpp}`：语义颜色 Token（`NextUI::EColor`）、`ApplyTheme()`、DPI 与逻辑坐标换算。C# 侧 `UiPalette` 通过 `UI_GetThemeColor` 实时同步同一份色板。
- `Scopes.hpp`：`FScopedStyle`、`FScopedId`、`FScopedDisabled`、`FScopedWindow`、`FScopedChild` 零堆分配 RAII 栈保护。
- `Widgets.{hpp,cpp}`：语义 `Button`、`GhostButton`、`ToolbarButton`、`IconButton`、`ModeRailButton`、`ComboOption`、`SegmentedPills`、`PresetPills`、`LabeledRow`、`Tooltip` / `ShowHoveredTooltip`。
- `Containers.{hpp,cpp}`：`DrawAppTitleBar`、`DrawBottomBar`、`FScopedOverlayPanel`、`FScopedInsetPanel`、`FScopedCard`、`FScopedSection`、`BeginDetailPanel`、`BeginCollapsibleSidePanel`、`BeginModal`。
- `Hud.{hpp,cpp}`：`NextUI::Hud` 世界坐标投影（`TryProjectWorldToScreen`）、状态条、图像自适应填充与 `FNotificationCenter`。
- `Platform/UiMultiViewport.{hpp,cpp}`：独立多视口（SDL3 + Vulkan SwapChain）平台后端，供 `gkNextEditor` 等桌面多窗口宿主按需开启。

## 统一现代 GPU 渲染器 (`FUiRenderer` & `UI.Composite`)

所有 UI 表面（主窗口 SwapChain、RmlUi Retained Geometry、编辑器独立浮动视口、RemotePlay 无头流式画面、C# `UiCanvas` 批处理列表）统一由 `FUiRenderer` 与 `UI.Composite.{vert,frag}.slang` 绘制：

1. **零 CPU 顶点改写**：直接 `memcpy` 20B 紧凑顶点流与 16-bit 索引流至 Host-Visible Ring Buffer。
2. **双顶点流绑定（兼容 iOS A12X `ERT_Compatibility`）**：Binding 0 为 Per-Vertex（20B），Binding 1 为 Per-Instance（8B `{textureIndex, flags}`），通过 `vkCmdDrawIndexed(..., firstInstance = batchIndex)` 传递批次材质与混合标记，无需 `VK_KHR_buffer_device_address` 或逐 Draw Push Constant。
3. **硬件裁剪与分支消除**：片段着色器零 `discard`；矩形裁剪全部交由 `vkCmdSetScissor` 光栅化硬件裁剪完成，跨 DrawList 相同 `(scissor, textureIndex, flags, vertexOffset)` 自动合批。
4. **Premultiplied Alpha 与 HDR 合成**：管线固定预乘混合 `(ONE, ONE_MINUS_SRC_ALPHA)`，统一支持 SDR sRGB、HDR10 ST.2084 PQ 与 Linear scRGB 输出。

## 图层栈与输入路由

- **绘制顺序（正向 Z-order）**：主 ImGui 应用层 → 已注册 `IUiLayer`（如 `NextRmlUi`）→ 开发者诊断层（按 `FUiFrameResult` 与 `FUiFramePolicy` 过滤）→ 系统加载/管线预热遮罩。
- **事件路由（逆向 Z-order）**：按图层逆序派发 `SDL_Event`，顶层消费即短路拦截；C# `UiCanvas` 通过 `UI_ClaimInputCapture` 将 `HotId`/`ActiveId` 与模态面板捕获状态同步回 `IUiHost`，阻断底层场景相机误触。

## 稳定交互与验证基线

- 主 DockSpace ID `MyDockSpace`，以及窗口/popup 的 `###`/`##` 后缀，都是持久化协议；显示文案变化不能顺手改 stable ID。
- `F2` 切换 Graphics Debug，面板内 `Q` 循环 Renderer、`1..9` 切换 View Mode；grave 键切换 Console。ImGui ini key、Settings manifest id 与 CVar 名同样视为持久化协议。
- Overlay 默认无边框；Section indent/unindent 必须对称；disabled 控件仍可显示原因；任何 collapsed、close 或 early-return 路径都要保持 Window/Style 栈平衡。
- MainWindow 可执行本地主窗口命令；RemoteView 与 capture surface 禁止额外窗口副作用。hidden-UI capture 不绘制 UI，include-UI capture 按 `FUiFrameResult` 绘制。

自动化入口：

- `assets/agentscripts/ui-foundation-renderer-{normal,compact}.agentscript.json`
- `assets/agentscripts/ui-foundation-editor-{normal,compact}.agentscript.json`

开发环境可用 `ui.catalog true` 打开 UI Catalog。
