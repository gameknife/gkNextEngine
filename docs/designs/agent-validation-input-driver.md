---
title: "Agent 输入驱动验证架构"
category: design
status: 现行
owner: engine/tools
created: 2026-06-09
last_updated: 2026-09-14
---

# Agent 输入驱动验证架构

当前实现采用“gnb 编排、Engine 提供原子能力”的边界。旧的 Engine 内 `AgentDriver` / `ScriptPlayer` 已删除；不要在引擎里恢复第二套脚本解释器。

## 组成

- `tools/gnb/internal/validate/validate.go`：解析 `.agentscript.json`，启动目标，执行等待/断言，写 JSON report，并监管进程退出。
- `tools/gnb/internal/validationstore/`：独立持久化验证运行、脚本快照、步骤、心跳、runner 日志、截图索引和 `review.json`。
- `src/Modules/NextValidation/AgentControlServer.*`：只监听 gnb 分配的 loopback TCP 端点，按行处理带一次性 token 的 JSON 请求。
- `src/Modules/NextValidation/NextValidationModule.cpp` 的 `FAgentControlService::HandleCommand`：提供
  input、query、cvar、exec、screenshot、quit 原语；同类的 `Query` 是内建查询表。
- `src/Modules/NextValidation/SyntheticInput.*`：向 SDL 事件队列注入键鼠事件；鼠标移动不会移动系统光标。
- `src/Engine/Runtime/Interface/AgentQueries.*` 与 `GameInstance::RegisterAgentQueries`：注册 `game.*` 业务查询。
- `src/Engine/Runtime/Interface/AgentControl.hpp`：引擎侧的服务接口，模块通过
  `NextEngine::SetAgentControlService` 安装——整套验证能力是 `NextValidation` 模块，不在 Engine 核心内。

`--agent-validation` 会启用隐藏窗口、确定性 pacing、Immediate present，并禁用 Streamline。`gnb validate --visible` 只改变窗口可见性，不改变脚本语义。

`--agent-watch`（`gnb validate --watch`）是给人看的变体：隐含 `--agent-validation` 与可见窗口，改用 FIFO
present；`NextValidation` 注册一个 `IUiLayer`（[`WatchOverlay`](../../src/Modules/NextValidation/WatchOverlay.cpp)，
在所有应用/开发者图层之上）画底部字幕、合成光标、按键提示和断言结果；`IAgentControlService::ShouldDropEvent`
让 `NextEngine::HandleEvent` 丢掉真实键鼠事件——合成事件带专用设备 id（`syntheticMouseId` /
`syntheticKeyboardId`），文本事件按推送计数认领。节奏、光标平滑移动、跳过 `quit` 和结束后保留窗口都在 gnb
（[`watch.go`](../../tools/gnb/internal/validate/watch.go)），引擎只多一个 `annotate` 原语。

## 脚本与查询

**逐字段的完整表在 [agentscript 脚本字段参考](../guides/agentscript-reference.md)**，写脚本查那一页；
本节只给分类，不重复字段清单——清单散成两份必然有一份先过期。

支持的步骤以 `validate.go` 的 `execute` switch 为准：

- 输入：`key`、`text`、`mouse-move`、`mouse-button`、`click`、`drag`、`scroll`
- 等待/判定：`wait-frames`、`wait-ms`、`wait-until`、`assert`
- 控制：`cvar`、`exec`、`screenshot`、`log`、`quit`

查询表在 `NextValidationModule.cpp` 的 `Query`，分 `engine.*`、`scene.*`（含 GPU-driven 统计）、
`cvar.<name>` 和 `game.<name>` 四类；游戏查询注册时不带 `game.` 前缀。

比较操作为 `eq/ne/gt/ge/lt/le/contains`。坐标可写窗口坐标，也可在 `norm` 中写 0..1 归一化坐标。gnb 握手后
查询 `engine.windowWidth/Height`，按真实窗口坐标尺寸换算 `norm`；Windows 显示缩放不是 100% 时它大于脚本
viewport，老程序不报告时才退回 viewport。

输入步骤由 gnb 整体透传给引擎，引擎按 `params.value(key, default)` 取值，拼错的字段在引擎侧只会
静默变默认值。所以校验放在 gnb：启动目标之前，[`lint.go`](../../tools/gnb/internal/validate/lint.go)
按每种步骤允许的字段表检查脚本，有错误就不启动（`gnb validate --lint` 只检查，CI 对全部脚本跑这一条）。
字段表是 gnb 一侧的约定，不是引擎协议的版本承诺；查询名不在表里，写错时由引擎在运行时报出原因。

## 控制协议与线程模型

gnb 先占用一个随机 `127.0.0.1` 端口并生成一次性随机 token，再用 `--agent-control=<host:port>` 和 `--agent-control-token=<token>` 启动目标。协议是单连接、逐行 JSON request/response；request 包含 `id/token/method/params`，握手返回 protocol version 与 capabilities。它不是 JSON-RPC，也没有稳定的跨版本远程 API 承诺。

`AgentControlServer` 的 socket 线程只收发、校验 token，并把请求放入队列后等待 promise。`NextEngine::Tick()` 调用 `Pump()`，在主线程执行 query、CVar、截图、退出和输入命令，再把结果交还 socket 线程。不得在 IO 线程直接读 Scene 或创建 Vulkan/ImGui 资源；也不要让主线程等待另一个需要主线程推进的异步步骤，否则会形成死锁。

输入命令只把合成 SDL event 放入事件队列；绝对鼠标 motion 不调用 `SDL_WarpMouseInWindow`，因此不会移动用户的系统光标。相对 motion 走 `InjectRelativeMouse`。若被测代码绕过事件、直接轮询 OS 鼠标状态，它不会自动看到合成位置，应改测试接缝或业务输入路径，而不是让隐藏验证抢占真实光标。

等待、比较、fatal 处理和 report 汇总属于 gnb。Engine 的原语不会解释脚本，也不知道某个 assert 是否应继续；截图命令先返回目标路径，gnb 再轮询文件出现。保持这个分层，才能让 `shot` 与 `validate` 共享通道而不把测试框架绑进运行时。

## 使用方式

```bash
./gnb.sh shot --scene assets/models/playground.glb
./gnb.sh shot --target ScadStudio --scene assets/scad/source/island_showcase.scad --frames 60 --ui
./gnb.sh validate --script assets/agentscripts/smoke.agentscript.json
./gnb.sh validate --script assets/agentscripts/smoke.agentscript.json --visible
```

`shot` 复用同一控制通道，固定执行 wait scene.ready → wait-frames → screenshot（截完即退出）。默认截图为 `out/build/<preset>/screenshots/agent_validation.jpg`；`validate` 默认报告在 `out/build/<preset>/agent_reports/`。

每次 `shot`/`validate` 还会创建 `out/build/<preset>/validation_runs/<runId>/`。`run.json` 的机器状态使用 `running/passed/failed/canceled/interrupted`；有截图的 `passed` 运行在 Dashboard 中显示为“采集完成、待审阅”，不等同于画面正确。`script.json` 保存本次解析输入，`runner.log` 保存目标 stdout/stderr 与执行器诊断，截图存放在该运行的 `screenshots/` 下。`review.json` 只保存最后一次 `pending/accepted/issue` 结论，CLI 可用 `gnb validation note <runId>` 写入。Dashboard 关闭期间 CLI 仍照常运行，重新打开后从目录轮询发现记录。

`screenshot` 步骤可选 `"accumulateFrames": N`。大于零时 Engine 暂时进入显式 offline progressive rendering，累计 N 帧后保存，并恢复此前 progressive 状态；这适合用两个独立进程做随机估计器的收敛/偏差对照。普通截图省略该字段，保持单帧当前样本语义。

## 约束

- 控制端点必须保持 loopback + 一次性 token；它不是远程管理 API。
- 新步骤优先在 gnb 侧组合已有原语。只有确实无法组合时才扩 Engine 协议。
- 断言失败必须保留非零退出码；不要把 report 仅当日志。
- 图像回归仍用 `gkNextVisualTest`；`gnb shot`/`validate` 面向快速肉眼验证和交互状态断言。
