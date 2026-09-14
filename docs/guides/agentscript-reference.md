---
title: "agentscript 脚本字段参考"
category: guide
status: 现行
owner: engine/tools
created: 2026-09-14
last_updated: 2026-09-14
---

# agentscript 脚本字段参考

`gnb validate --script <path>` 执行的 `.agentscript.json` 的完整字段表。架构、线程模型和协议边界见
[Agent 输入驱动验证架构](../designs/agent-validation-input-driver.md)；本页只回答"这个字段叫什么、
写错了会怎样"。

**没有 schema 校验。** gnb 把输入步骤整体转发给引擎，引擎用 `params.value(key, default)` 读取，
因此**拼错的可选字段既不报错也不生效，只是静默变成默认值**。这是本页存在的唯一理由——先查表，
不要照着记忆写。

权威来源共三处，本页与它们不一致时以代码为准：

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
| `steps` | array | — | 见下 |

每个步骤都可以带 `comment`（或 `description`，两者都在时 `description` 优先）做说明，会写进验证记录。

`timeoutMs` 覆盖该步的 `defaults.stepTimeoutMs`，但**只对会轮询的步骤有意义**：`wait-frames`、
`wait-ms`、`wait-until` 和等待文件落盘的 `screenshot`。`assert` 只判定一次，输入步骤是一次性 RPC，
给它们写 `timeoutMs` 不报错也不起作用。

## 等待与断言

| type | 字段 | 说明 |
|---|---|---|
| `wait-frames` | `n` (int) | 等到 `engine.totalFrames` 增长 n。场景解析在主线程，帧计数在那期间会停住，这正是它能等住加载的原因 |
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
| `cvar` | `name`, `set`（可选） | 带 `set` 是写入，省略是读取。写入走 `ECVarSetBy::Console`。cvar 不存在或值非法会抛错。**带 `Archive` 标志的 cvar 会被持久化**，见下面的警告 |
| `exec` | `line` | 执行一条控制台命令行 |
| `log` | `message`（或 `text`） | 只写进 report，不与引擎交互 |
| `quit` | — | 请求引擎退出，exitCode 恒为 0 |
| `screenshot` | 见下 | |

> **脚本改过的 `Archive` cvar 会留在你的机器上。** 它们写进
> `~/Library/Application Support/gkNext/<target>/assets/configs/cvar_user.json`
> （Windows 在 `%LOCALAPPDATA%`），**进程退出后依然生效**，于是污染之后每一次 `gnb shot` /
> `gnb validate` / 手动运行。`r.rendererType` 是最典型的一个：一个切渲染器的脚本跑完，
> 后续所有运行都还停在它最后设的那个渲染器上。
>
> 2026-09-14 这件事真实发生过：一个验证脚本把 `r.rendererType` 留在 3（VoxelTracing），
> 导致之后的"改动前后对比"实际上是在拿两个不同渲染器的画面比，白查了一轮。
>
> 想避免就在脚本末尾把改过的 `Archive` cvar 设回默认值（默认值见
> `assets/configs/cvar_default.json`）。排查"画面莫名其妙变了"时，先看那个文件。

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
{ "type": "click", "at": { "norm": [0.5, 0.3] } }   // 归一化，按 viewport 换算，改分辨率不失效
{ "type": "click", "at": { "px": [640, 360] } }     // 像素
{ "type": "click", "at": [640, 360] }               // 像素，简写
```

### 三个真实踩过的坑

1. **坐标字段不是 `x`/`y`。** 写 `{"type":"click","x":100,"y":200}` **不会报错**——`at` 缺失时引擎
   回退到"当前光标位置"（`params.contains("at") ? ... : current`），于是点在了别处。
   而 `mouse-move` / `drag` 用 `params.at()` 读 `to`/`from`，缺了会抛
   `key 'to' not found`，反而是能发现的。唯一真正用 `x`/`y` 的是 `scroll`，且语义是滚动量。

2. **`key` 的字段是 `code` 不是 `key`。** 写成 `key` 会得到 `unknown key`——因为
   `params.value("code","")` 读到空串。

3. **`mouse-move` 的 `durationFrames` 是空操作。** 有三个现存脚本在写它，但代码里根本没有这个字段，
   转发过去被忽略。要分帧平滑移动就写多个 `mouse-move` 步骤。

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
  `engine.checkerboardActive`、`engine.sparseCheckerboardActive`
- **场景**：`scene.nodeCount`、`scene.selectedId`、`scene.selectedCount`、`scene.sunElevation`、
  `scene.atmosphereEnabled`、`scene.aerialPerspectiveEnabled`、`scene.heightFogEnabled`
- **GPU-driven 统计**：`scene.renderProxyCount`、`scene.maxVisibleProxyIndex`、
  `scene.drawnTriangleCount`、`scene.drawnProxyCount`、`scene.lod0TriangleCount`、
  `scene.culledTriangleCount`、`scene.culledProxyCount`、`scene.shadowVisibleTriangleCount`、
  `scene.drawnTrianglesPerProxy`
- **CVar**：`cvar.<名字>`，例如 `cvar.r.rendererType`。**返回字符串**，见上面的比较说明
- **游戏**：`game.<名字>`，由 `GameInstance::RegisterAgentQueries` 注册，注册时不带 `game.` 前缀

`scene.drawnTrianglesPerProxy` 是在引擎里算好再返回的，不要自己用两个查询相除——统计每帧刷新，
两次查询可能落在不同帧上，比值没有意义。

## 惯用写法

**等场景真正就绪**（只等 `Running` 不够，启动时先有一个空场景）：

```json
{ "type": "wait-until", "query": "engine.status", "op": "eq", "value": "Running", "timeoutMs": 30000 },
{ "type": "wait-until", "query": "scene.nodeCount", "op": "gt", "value": 0 },
{ "type": "wait-frames", "n": 20 }
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

## 调试脚本本身

- report 在 `out/build/<preset>/agent_reports/<name>.json`，每步都有 `passed` 和失败时的 `message`。
- 完整证据在 `out/build/<preset>/validation_runs/<runId>/`：`script.json`（本次解析到的输入）、
  `runner.log`（目标 stdout/stderr）、`screenshots/`。
- 脚本路径不必在 `assets/agentscripts/` 下，临时脚本写 `/tmp` 里跑完即弃是可以的；要长期保留的
  才放进仓库。
- 断言失败会让 `gnb validate` 返回非零退出码，CI 可直接据此判定。
- **一步失败不会中断脚本**，后续步骤照常执行，report 里逐步标 `passed`。所以一次运行就能看到
  所有问题，不用逐个修、逐次重跑。

本页的字段、默认值和上面三个坑的报错文案，都在 2026-09-14 用一份把每种写法各跑一遍的脚本
实测过（包括故意写错的那几条）。改动 `execute`、`HandleCommand` 或 `Query` 时请同步这里。
