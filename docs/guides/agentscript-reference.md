---
title: "agentscript 脚本字段参考"
category: guide
status: 现行
owner: engine/tools
created: 2026-09-14
last_updated: 2026-11-25
---

# agentscript 脚本字段参考

`gnb validate --script <path>` 执行的 `.agentscript.json` 的完整字段表。架构、线程模型和协议边界见
[Agent 输入驱动验证架构](../designs/agent-validation-input-driver.md)；本页只回答"这个字段叫什么、
写错了会怎样"。

**脚本在启动目标之前先做检查。** gnb 把输入步骤整体转发给引擎，引擎用 `params.value(key, default)`
读取，拼错的字段在引擎那边只会静默变成默认值；所以 gnb 先按下面的字段表检查脚本：未知字段、未知
步骤类型、缺必填字段、取值不在枚举里、没有 viewport 却用 `norm` 坐标，都是错误，**什么都不会启动**。
以 `_` 开头的键（如 `"_comment"`）任何位置都允许，当注释用。只检查不运行：

```bash
gnb validate --lint                      # 检查 assets/agentscripts 下全部脚本（CI 也跑这一条）
gnb validate --lint path/to/x.agentscript.json
```

检查管不到的是**查询名**：查询表在引擎里（见"内建查询"），写错的查询名在运行时失败，报错里带引擎给的原因。

权威来源，本页与它们不一致时以代码为准：

- [`lint.go`](../../tools/gnb/internal/validate/lint.go) 的 `stepFields` — 每种步骤允许的字段
- [`validate.go`](../../tools/gnb/internal/validate/validate.go) 的 `execute` — 顶层字段、
  等待/断言/截图步骤、比较运算
- [`NextValidationModule.cpp`](../../src/Modules/NextValidation/NextValidationModule.cpp) 的
  `HandleCommand` / `Query` — 输入步骤字段、内建查询
- [`SyntheticInput.cpp`](../../src/Modules/NextValidation/SyntheticInput.cpp) — 键名、修饰键、鼠标键

## 顶层结构

```json
{
  "name": "my_check",
  "target": "gkNextRenderer",
  "scene": "assets/models/playground.glb",
  "args": ["--force-compatibility-renderer"],
  "defaults": { "waitFrames": 2, "stepTimeoutMs": 12000 },
  "viewport": { "width": 1280, "height": 720 },
  "steps": []
}
```

| 字段 | 类型 | 默认 | 说明 |
|---|---|---|---|
| `name` | string | 文件名去扩展名 | report 文件名与 `run.json` 里的名字 |
| `target` | string | `gkNextRenderer` | 要启动的可执行文件；`--target` 覆盖 |
| `scene` | string | 无 | 传给 `--load-scene`；`--scene` 覆盖。proc 场景直接写 `KilometerWorld.proc` |
| `args` | string[] | 空 | 追加到引擎命令行，用于只在某个 flag 下成立的场景。命令行 `--` 之后的参数排在其后 |
| `defaults.waitFrames` | int | 1 | `wait-frames` 省略 `n` 时的帧数 |
| `defaults.stepTimeoutMs` | int | 8000 | 每步超时；省略或 ≤0 时是 8000，**不是无限** |
| `viewport.width/height` | int | 引擎默认 | 窗口尺寸，也是 `norm` 坐标的换算基准；`--width/--height` 覆盖 |
| `waitReady` | bool | `true` | 第一步之前先等 `scene.ready` 再预热 3 帧（最长 300 秒）。只有测启动或加载过程本身的脚本才设成 `false` |
| `tags` | string[] | 空 | 批量运行的分组，见"批量运行"。`smoke` = 快速健康检查，CI 在 Lavapipe 上跑 |
| `steps` | array | — | 见下 |

每个步骤都可以带 `comment`（或 `description`，两者都在时 `description` 优先）做说明，会写进验证记录，
失败时也会出现在终端摘要里。

**脚本不必以 `quit` 结尾**：最后一步执行完，gnb 会自动让目标退出。

`timeoutMs` 覆盖该步的 `defaults.stepTimeoutMs`，但**只对会轮询的步骤有意义**：`wait-frames`、
`wait-ms`、`wait-until` 和等待文件落盘的 `screenshot`。`assert` 只判定一次，输入步骤是一次性 RPC，
给它们写 `timeoutMs` 不报错也不起作用。

## 等待与断言

| type | 字段 | 说明 |
|---|---|---|
| `wait-frames` | `n` (int)，可选 `readyQuery` (string) | 等到 `engine.totalFrames` 增长 n；指定 `readyQuery` 时仅在查询返回 `true` 后计帧，变回 `false` 或帧计数重置时重新计帧。异步加载期间仍会增长帧数，因此截图前应使用 `scene.ready` |
| `wait-ms` | `ms` (int) | 墙钟等待。**不推荐**，帧率不同结果不同；能用 `wait-frames` 就用它 |
| `wait-until` | `query`, `op`, `value` | 轮询到条件成立；超时算失败 |
| `assert` | `query`, `op`, `value` | 立即判定一次，不成立则**立刻失败并让进程返回非零退出码** |

`op` 取 `eq` / `ne` / `gt` / `ge` / `lt` / `le` / `contains`。

> **`eq` / `ne` / `contains` 是字符串比较**（`fmt.Sprint(a) == fmt.Sprint(b)`），`gt/ge/lt/le` 才转数值。
> 所以 bool 型 cvar 必须写 `"value": true`，写 `"value": 1` 会失败——实际返回的是字符串 `"false"`/`"true"`，
> 而 `fmt.Sprint(1)` 是 `"1"`。同理 `engine.status` 只能和 `"Running"` 这类字符串比。

## 控制

| type | 字段 | 说明 |
|---|---|---|
| `cvar` | `name`, `set`（可选） | 带 `set` 是写入，省略是读取。写入走 `ECVarSetBy::Console`。cvar 不存在或值非法会抛错 |
| `exec` | `line` | 执行一条控制台命令行 |
| `log` | `message`（或 `text`） | 只写进 report，不与引擎交互 |
| `quit` | — | 请求引擎退出，exitCode 恒为 0 |
| `screenshot` | 见下 | |

验证运行（`--agent-validation`）**不读也不写**用户配置 `cvar_user.json`：每次都从
`assets/configs/cvar_default.json` 的默认值开始，脚本改过的 `Archive` cvar 也不会留到之后的运行或
你手动启动的程序里。脚本末尾不需要再把 cvar 设回去。

`screenshot` 字段：

| 字段 | 类型 | 默认 | 说明 |
|---|---|---|---|
| `out` | string | `screenshots/<name>_step` | 相对可写目录；实际落盘路径由 gnb 改写为 `<证据目录>/<步号>_<名字>`，report 里 `out` 是最终路径、`requestedOut` 是你写的那个 |
| `ui` | bool | `false` | **默认不含 ImGui**。要验证 HUD / 面板必须显式写 `true` |
| `accumulateFrames` | int | 0 | >0 时进入离线 progressive，累计 N 帧再存，用于随机估计器的收敛对照 |
| `quitAfterCapture` | bool | `false` | 截图与退出合并成一次引擎操作，避免 headless 下 quit 与 server 拆除竞争 |

## 输入步骤

这一组是最容易写错的：**gnb 不解析它们**，`clone(step)` 去掉 `type` 后原样转发，所以字段名就是
引擎 `HandleCommand` 里读的名字。

| type | 必填 | 可选 | 说明 |
|---|---|---|---|
| `key` | `code` | `action`, `mods` | `action` 取 `press`(默认) / `down` / `up` |
| `text` | `value` | — | 注入 `SDL_EVENT_TEXT_INPUT`，不是逐键敲击 |
| `mouse-move` | `to` | `relative` | `relative: true` 走 `InjectRelativeMouse`，`to` 当增量用 |
| `click` | — | `at`, `button`, `count` | 一次完整按下+抬起 |
| `mouse-button` | — | `at`, `button`, `count`, `action` | `action` 取 `press`(默认) / `down` / `up`，用于按住不放 |
| `drag` | `from`, `to` | `button` | 移动→按下→移动→抬起 |
| `scroll` | — | `x`, `y` | **这里的 `x`/`y` 是滚动量，不是坐标**；滚动位置取当前光标 |

### 坐标写法

`to` / `at` / `from` 三种写法，**推荐 `norm`**（现有脚本里 68 处用 `norm`、12 处用裸数组）：

```json
{ "type": "click", "at": { "norm": [0.5, 0.3] } }   // 归一化，按真实窗口尺寸换算，改分辨率/缩放不失效
{ "type": "click", "at": { "px": [640, 360] } }     // 窗口坐标
{ "type": "click", "at": [640, 360] }               // 窗口坐标，简写
```

`norm` 按目标报告的**窗口坐标尺寸**（`engine.windowWidth/Height`）换算，而不是脚本的 `viewport`：
Windows 上显示缩放不是 100% 时两者不同（125% 下 1280×720 的窗口坐标是 1600×900），以前按 viewport
换算，`norm [0.5, 0.5]` 实际点在 40% 处。不报告窗口尺寸的老程序退回用 `viewport`；两者都没有时该步
直接报错。`px` 和裸数组是窗口坐标，不随缩放调整，所以优先用 `norm`。

### 检查会拦下的老坑

下面几种写法过去不报错、静默出错，现在启动前就会被检查拦下，报错里带正确写法：

1. **点击坐标写成 `x`/`y`。** 坐标在 `at` 里；`at` 缺失时引擎回退到"当前光标位置"，于是点在了别处。
   唯一真正用 `x`/`y` 的是 `scroll`，且语义是滚动量。
2. **`key` 的字段写成 `key`。** 键名在 `code` 里。
3. **`mouse-move` 写 `durationFrames`。** 没有这个字段；要分帧平滑移动就写多个 `mouse-move` 步骤。

### 键名

按顺序解析：别名表 → `F1`..`F24` → SDL 官方键名 → 单字符退化为小写字母。

别名（大小写不敏感）：`RETURN`/`ENTER`、`KP_ENTER`、`ESC`/`ESCAPE`、`SPACE`、`TAB`、`BACKSPACE`、
`GRAVE`/`BACKQUOTE`、`DELETE`/`DEL`、`LEFT`、`RIGHT`、`UP`、`DOWN`、`LSHIFT`、`RSHIFT`、`LCTRL`、
`RCTRL`、`LALT`、`RALT`。

`mods` 取 `CTRL`/`CONTROL`、`SHIFT`、`ALT`、`GUI`/`CMD`/`COMMAND`/`META`。
`button` 取 `left`(默认)、`right`、`middle`、`x1`、`x2`。

> 带修饰键的快捷键未必能驱动到 ImGui 应用：实测 gkNextEditor 的 `Ctrl+,`（Preferences）用合成事件
> 打不开，直接点工具栏按钮可以。原因没有深究。驱动 ImGui 界面时优先用坐标点击，并且**先
> `mouse-move` 再 `click`**——ImGui 需要一帧 hover 才认得该控件。

## 内建查询

`query` 可用的名字（[`NextValidationModule.cpp`](../../src/Modules/NextValidation/NextValidationModule.cpp)
的 `Query`），查不到的名字会让该步失败：

- **引擎**：`engine.totalFrames`、`engine.frameRate`、`engine.time`、`engine.status`
  （`Starting`/`Running`/`Loading`/`AsyncPreparing`）、`engine.rendererType`、
  `engine.checkerboardActive`、`engine.sparseCheckerboardActive`、`engine.windowWidth`、`engine.windowHeight`
  （窗口坐标尺寸，`norm` 换算用的就是它）
- **场景**：`scene.ready`、`scene.nodeCount`、`scene.selectedId`、`scene.selectedCount`、`scene.sunElevation`、
  `scene.atmosphereEnabled`、`scene.aerialPerspectiveEnabled`、`scene.heightFogEnabled`
- **GPU-driven 统计**：`scene.renderProxyCount`、`scene.maxVisibleProxyIndex`、
  `scene.drawnTriangleCount`、`scene.drawnProxyCount`、`scene.lod0TriangleCount`、
  `scene.culledTriangleCount`、`scene.culledProxyCount`、`scene.shadowVisibleTriangleCount`、
  `scene.drawnTrianglesPerProxy`
- **AmbientCube GI 管线**：`ambient.allocatedBricks`、`ambient.pendingBricks`、`ambient.unpublishedBricks`、
  `ambient.scrollCount`、`ambient.lastScrollMs`、`ambient.voxelJobActive`、`ambient.dirtyRevoxelCount`、
  `ambient.brickCacheEntries`、`ambient.brickCacheHits`、`ambient.lastScrollRestoredBricks`
- **CVar**：`cvar.<名字>`，例如 `cvar.r.rendererType`。**返回字符串**，见上面的比较说明
- **游戏**：`game.<名字>`，由 `GameInstance::RegisterAgentQueries` 注册，注册时不带 `game.` 前缀

`scene.drawnTrianglesPerProxy` 是在引擎里算好再返回的，不要自己用两个查询相除——统计每帧刷新，
两次查询可能落在不同帧上，比值没有意义。

`scene.ready` 等待排队中的场景加载、GPU 场景提交、管线预热，以及纹理/HDR 上传的主线程完成回调。
场景加载失败时查询返回错误；合法空场景也可以就绪，节点数不能代替这个信号。

## 惯用写法

**等场景就绪**：启动时的那一次 gnb 已经替你等了（`waitReady`），不用再写 `engine.status == Running`
加固定帧数——那两样都不能证明加载完成。只有脚本中途又加载了新场景（切关卡、打开文件）时，才需要自己等：

```json
{ "type": "wait-until", "query": "scene.ready", "value": true, "timeoutMs": 300000, "fatal": true },
{ "type": "wait-frames", "n": 20, "readyQuery": "scene.ready", "fatal": true }
```

**在一个进程里扫多个渲染器**，比每个渲染器开一次进程快得多：

```json
{ "type": "cvar", "name": "r.rendererType", "set": 4 },
{ "type": "wait-frames", "n": 40 },
{ "type": "screenshot", "out": "screenshots/r4_noambient" }
```

渲染器编号见 `r.rendererType` 的 cvar 描述：0=PathTracing、1=SoftwareTracing、2=SoftwareModern、
3=VoxelTracing、4=SoftwareModernNoAmbient、5=PathTracingLite。

**断言 bool 型 cvar**（注意 `true` 不是 `1`）：

```json
{ "type": "assert", "query": "cvar.r.atmosphere.enable", "op": "eq", "value": true }
```

## 批量运行（`--all`）

```bash
gnb validate --all --tag smoke                       # 一组脚本，逐个跑，最后一张汇总表
gnb validate --all --target NextDayz --build         # 某个程序的全部脚本，先增量构建
gnb validate --all --tag smoke --list                # 只列出会跑哪些
gnb validate --all path/a.agentscript.json path/b.agentscript.json -- --forcenort   # -- 之后的参数传给每个目标
```

- 选择：位置参数是脚本文件或目录（默认 `assets/agentscripts`，不含子目录）；`--tag` 可重复，
  脚本要带齐所有给出的标签；`--target` 只留指定程序的脚本。
- 一个脚本失败不会中断批次。没构建的目标标为 SKIP（不算失败）；加 `--build` 会先把用到的目标全部增量构建。
- 汇总表每行一个脚本：PASS/FAIL/SKIP、通过步数、耗时、第一个失败原因，失败的附证据目录。
  汇总同时写进 `out/build/<preset>/agent_reports/batch.json`（`--report` 可改路径），每个脚本的报告照常写。
- 有失败时退出码非零，CI 直接据此判定。
- `--timeout-scale N` 把所有步骤超时和场景就绪等待乘以 N。软件 Vulkan（Lavapipe）比 GPU 慢一到两个
  数量级，CI 用 10。

### smoke 组与 CI

`"tags": ["smoke"]` 的脚本是改完引擎或渲染后先跑的那一组（本机约 1 分钟）。CI（`desktop.yml` 的
Linux 任务）在没有 GPU 和显示器的 runner 上用 Lavapipe 跑同一组：引擎自动改用 `VK_EXT_headless_surface`
并关闭硬件光追，失败时把 `agent_reports/` 和 `validation_runs/` 作为构建产物上传。所以 smoke 脚本：

- 不能依赖硬件光追（PathTracing）或外部升采样器（DLSS/FSR，验证模式下本来就关闭）；
- 加进 smoke 之前先在本机用 `gnb validate --all <脚本> -- --forcenort` 跑通（关闭硬件光追，和 CI 的渲染路径一致）；
- 要尽量快：smoke 是每个 PR 都跑的。

## 观看模式（`--watch`）

`gnb validate --script <path> --watch` 是给人看的回放，脚本不用改：

- 窗口可见，按显示器刷新率呈现（vsync），所以按帧数的等待也会花真实时间。
- 每个动作后停顿 `--pace`（默认 700ms）；`log` 步骤停两倍时间，等待类步骤不额外停。
- 画面底部有字幕：第几步、在做什么、步骤的 `comment`；断言通过闪绿框，失败闪红框。
- 光标从上一个位置平滑移到目标（合成光标用圆环标出，点击有波纹），按键在字幕上方显示。
- 你的键盘和鼠标输入会被忽略，不会干扰回放；关窗口不受影响。直接轮询 OS 键鼠状态的代码看不到这种过滤。
- 脚本里的 `quit` 和截图的 `quitAfterCapture` 在观看模式下跳过：跑完或失败后停在最后的画面，
  字幕显示结果，**关掉窗口才结束**。`ui: true` 的截图会临时隐藏叠加层，截不到字幕。

只想显示窗口、不要节奏和字幕时用 `--visible`。

## 调试脚本本身

- **终端只给摘要**：通过时一行 `N/N steps passed`；失败时列出每个失败步骤（步号、类型、原因、注释）、
  失败截图路径，以及 `runner.log` 里最后几行引擎错误。引擎日志只写进 `runner.log`，要同时打到终端加
  `--verbose`。
- **失败截图在出错的那一刻截**（`failure_<步号>`），不会等后面的步骤（通常是退出）把画面换掉。
- **目标进程崩溃或退出时立即停下**，报"目标进程在第 N 步意外退出"并附退出码，不再把剩下每一步都报成连接错误。
  启动阶段就失败（缺 Vulkan 扩展、找不到资源）时报 `target exited during startup`，并带上引擎的那一行报错。
  验证模式下启动失败不会弹出错误对话框，也不会卡住不退出。
- **程序比 shader 产物旧时会警告**：shader 编译在所有目标共用的目录里，构建别的目标后，这个目标可能
  和新 shader 不匹配（8 月的程序配 11 月的 shader 会直接丢设备）。加 `--build` 先增量构建目标再跑。
- report 在 `out/build/<preset>/agent_reports/<name>.json`，每步都有 `passed` 和失败时的 `message`。
- 完整证据在 `out/build/<preset>/validation_runs/<runId>/`：`script.json`（本次解析到的输入）、
  `runner.log`（目标 stdout/stderr）、`screenshots/`。
- 脚本路径不必在 `assets/agentscripts/` 下，临时脚本写在临时目录里跑完即弃是可以的；要长期保留的
  才放进仓库。
- 断言失败会让 `gnb validate` 返回非零退出码，CI 可直接据此判定。
- **一步失败不会中断脚本**（除非该步 `"fatal": true`），后续步骤照常执行，report 里逐步标 `passed`。
  所以一次运行就能看到所有问题，不用逐个修、逐次重跑。

改动 `lint.go` 的字段表、`execute`、`HandleCommand` 或 `Query` 时请同步这里。
