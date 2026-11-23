---
title: "NextDayz Web 客户端开发计划：剩余工作"
category: project
status: W0–W7 与 W8-0/1/2/4/6 已完成；剩余 W8-3、W8-5 与收尾项
owner: NextDayz
created: 2026-11-16
last_updated: 2026-11-23
---

# NextDayz Web 客户端开发计划：剩余工作

本文只保留 [Web 客户端架构设计](nextdayz-web-client-design.md) 中尚未完成的部分。已完成阶段
（W0–W7，W8-0、W8-1、W8-2、W8-4、W8-6）的结论与实测已并入设计文档；日常开发、烘焙、测试与部署见
[Web 客户端开发指南](../../guides/nextdayz-web-client.md)；过程记录查 Git 历史。保留原计划的任务编号，
新增的收尾项编为 R1–R6。

仓库的任务调度以 `.spec/TODO.md` 为准。下面的任务录入之前不视为已授权执行。

## 1. 剩余任务

| 任务 | 内容 | 完成标准 |
| --- | --- | --- |
| R1 联机客户端只保留一份（W3-4 收尾） | 把 DS 消息的应用和客户端帧顺序移进 `FClientSession`。帧顺序包括移动锁定、取消瞄准与扳机、拾取后装备武器、背包纠正、出生装备。现在这部分在 `NextDayzNetwork.cpp`/`NextDayzGameInstance.cpp` 和 `WebCore.cpp` 各有一份，两端改完后只留适配层：桌面写场景与 ImGui，Web 填帧状态。先统一设计 5.3 列出的四处分歧，每处以桌面行为为准，除非证明桌面是错的 | 把一段录好的 DS 消息序列分别喂给原生和 WASM 的 `FClientSession`，库存、武器槽、身体状态、锁定状态逐项一致，这项比对接入 `NextDayz Web` 工作流。`nextdayz-net-*`、`nextdayz-master-*` 和 Web 机器人照常通过 |
| W8-3 混合客户端自动化 | 一个桌面客户端加一个自动化驱动的浏览器，复刻互见与互伤、抢物资、离开—回归—撤离。`gnb web test --e2e` 自行启动 Master、DS、桌面客户端和 Chromium，结束后清理。现有的 `test/w7-*.mjs` 要手动起服务，默认值还写死了 `macos-arm64` 构建目录和 Chrome 渠道 | 全部通过，并接入 CI（可为每日任务） |
| W8-5 性能与兼容 | Chrome、Firefox、Safari 各完整走一遍；在设计 8.1 指定的中档独显笔记本上测 1080p；把内存、加载时间、帧时间与预算对照；加低画质档（现在只有"均衡"和"级联阴影"）；本地角色加渲染插值，或在高刷新率屏上实测证明不需要 | 各浏览器的结果写入设计 8.1；不达标项有明确结论：修复，或记为已知限制 |
| R2 界面文案走翻译表 | 设计 8.2 要求界面文字全部来自烘焙的翻译表，现在只有物品名和提示语这样做 | `web/nextdayz/src` 里不再有 `text("中文", "English")` 形式的界面字符串；切换语言后界面全部更新 |
| R3 正式版去掉验收指令 | `reviewPlace`、`reviewCrowd` 编进了正式版 `ndcore.wasm`，只靠 TS 的 `VITE_ND_REVIEW` 开关挡住。改为编译期开关 | 正式构建的 WASM 不导出、不处理验收指令；开发构建的验收入口照常可用 |
| R4 遗留核对 | 第三人称程序化姿态与桌面的差异；旧的桌面背包坐标脚本 | 姿态差异修复或记为已知限制；坐标脚本通过，或按新背包布局更新 |
| R5 浏览器内 SCAD 评估 | 按设计 D2 的三条判据收集数据：与烘焙产物逐项一致、下载量、中档独显上的求值耗时与内存峰值 | 数据写入 D2，并据此决定是否转为默认路径 |
| R6 清理原型目录 | `shared/client.mjs` 仍从 `w1/transport.mjs` 取 WebRTC 传输。把它移进 `shared/`，再删除 `w0/`–`w2/` 一次性原型，并改掉设计 7.2 里指向 W1 页面的链接 | `web/nextdayz/` 下没有 `w0/`–`w2/`；`gnb web build`、`gnb web test` 与机器人照常通过 |

## 2. 顺序与依赖

- **R1 最先做。** 它改的是桌面与 Web 共用的联机代码，W8-3 的端到端用例也要求两端行为一致。R1 进行期间，
  `NextDayzNetwork.cpp`、`NextDayzGameInstance.cpp` 和 `WebCore.cpp` 不接受其他改动。
- **W8-3 排在 R1 之后。** R6 要在 W8-3 动测试脚本之前做完，免得同一批文件被改两次。
- **可同时进行：** W8-5、R2、R3 只改 `web/nextdayz/` 或 Web 构建，可以和 R1 并行。R4、R5 各自独立。

## 3. 闸门

每个任务合入前，先跑 [开发指南的"提交前检查"](../../guides/nextdayz-web-client.md#提交前检查)。
改到 NextDayz C++ 时还要过桌面闸门：`gnb build NextDayz gkNextUnitTests NextDayzBots`、`[NextDayz]` 单测、
`gnb ds --bots 8 --minutes 3`，以及与所改子系统对应的 `assets/agentscripts/nextdayz-*` 脚本。提交说明写明
跑过哪些检查、结果如何；内核任务还要写明拆了哪些文件、适配层留在哪里。

## 4. 之后（不在本计划内）

- WebSocket 回退传输
- 单机战役（需要把 `WorldSim` 纳入内核）
- 口令之外的账号体系（找回、第三方登录）
- 触屏与手柄
- three.js 的 WebGPU 路径
- 把内核移入 Worker
- 增量快照
- 利用内核与烘焙产物做无 GPU 的 DS
