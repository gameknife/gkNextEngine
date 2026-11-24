---
title: "NextDayz Web 客户端开发计划：剩余工作"
category: project
status: 本机能做的都已完成；剩余项需要中档独显笔记本、macOS 和能跑 DS 的 CI runner
owner: NextDayz
created: 2026-11-16
last_updated: 2026-11-24
---

# NextDayz Web 客户端开发计划：剩余工作

本文只保留 [Web 客户端架构设计](nextdayz-web-client-design.md) 中尚未完成的部分。已完成的阶段与收尾项
（W0–W8 在本机能做的部分，R1–R6）的结论与实测已并入设计文档；日常开发、烘焙、测试与部署见
[Web 客户端开发指南](../../guides/nextdayz-web-client.md)；过程记录查 Git 历史。保留原计划的任务编号。

仓库的任务调度以 `.spec/TODO.md` 为准。下面的任务录入之前不视为已授权执行。

## 1. 剩余任务

剩下的三项都卡在硬件或基础设施上，代码与测试脚本已经就绪。

| 任务 | 内容 | 完成标准 |
| --- | --- | --- |
| W8-5 余项：其他硬件与浏览器 | 在设计 8.1 指定的中档独显笔记本上测 1080p（`gnb web test --e2e --scenario perf`，`ND_PERF_BROWSERS` 选浏览器）；Firefox 与 macOS 上的 Safari 各完整走一遍。开发机上 Playwright 自带的 Firefox 起不来，Windows 版 WebKit 没有 WebRTC，都测不了 | 结果写入设计 8.1；不达标项修复，或记为已知限制 |
| R5 余项：D2 定论 | 在同一台中档独显笔记本上跑 `web/nextdayz/test/scad-level.mjs`，记录求值耗时、WASM 内存峰值和进图时间（开发机的数据见设计 D2） | 数据写入 D2，决定是否转为默认路径 |
| W8-3 余项：接入 CI | `gnb web test --e2e` 已能自行编排全部场景；还需要一台能跑 DS（Vulkan 或 Lavapipe）和 Chrome 的 runner，并决定触发方式（手动、每日或发布前） | e2e 在 CI 上跑通 |

## 2. 顺序与依赖

三项互不依赖。W8-5 与 R5 用同一台笔记本，可以一起做。

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
