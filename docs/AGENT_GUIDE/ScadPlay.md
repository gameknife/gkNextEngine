# ScadPlay —— 边玩边用自然语言搭关卡、改玩法

`ScadPlay` 是一个 C# 第三人称游戏（`projects/ScadPlay` + `src/Application/Game/ScadPlay`），
专门为"人在游戏里玩，agent 在旁边按自然语言改"设计：

- **关卡 = 一个 `.scad` 文件**（默认 `assets/scad/source/scadplay/playground.scad`）。游戏运行时
  保存它——或它 `use` 的任何 kit——1～2 秒内原地重建，玩家留在原处，已经打开的门、吃掉的金币都保留。
- **玩法 = C#**（`projects/ScadPlay/Scripts`）。kit 里每个玩法 module 对应一个同名 C# 类
  （`sp_door` → `Mechanisms/Door.cs`），改了规则或加了新机关，重新发布程序集，正在跑的游戏热重载，
  同样不丢进度。
- **agent 通道 = `out/scadplay/context.json`**：玩家在哪、朝哪、看着什么，上次构建的结果，
  C# 版本，每个机关的状态和最近的事件。坐标全部是 SCAD 空间（米，Z 朝上），直接写进 `translate`。

这套流程是从 UE 工程 TP_ThirdPerson（ScadRuntime / ScadShrine / ScadVerse）的经验搬回来的：
那边的 ScadCore 本来就移植自本仓库的 `Modules/ScadLoader`，"场景 = .scad、玩法类名 = kit 模块名、
运行中热重载、agent 读 context 再追加编辑"在那边跑通之后，这里用 C#（CoreCLR 热重载）代替 Verse。

Agent 的操作步骤在 skill 里：[`.agents/skills/scadplay/SKILL.md`](../../.agents/skills/scadplay/SKILL.md)
（Claude Code 入口 `.claude/skills/scadplay/SKILL.md`）。本文是协议与契约的完整说明。

## 运行

```bash
./gnb.sh build ScadPlay          # C++ 壳 + 发布 C#
./gnb.sh run ScadPlay            # 打开默认关卡；也能在 gkNextLauncher 里选
SCADPLAY_LEVEL=assets/scad/source/scadplay/my_level.scad ./gnb.sh run ScadPlay
```

关卡选择顺序：环境变量 `SCADPLAY_LEVEL` → `projects/ScadPlay/Content/configs/scadplay.json` 的 `level`
→ `playground.scad`。路径可以是资产路径、绝对路径；游戏会优先加载**源码树**里的那份
（`Paths.GetProjectRoot()` 下），所以编辑源文件立即生效。没有源码树（打包后）就加载运行时副本，
HUD 显示"只读"，不再热重建。

操作：WASD 移动（相对镜头）、空格跳（按住跳得更高）、Shift 冲刺、按住右键拖动转视角、E 互动、
R 重开本关、L 显示/隐藏机关标注（id、状态和接线）、Tab 锁定鼠标。F1～F10 是引擎调试快捷键，游戏不占用。

验证：`./gnb.sh validate --script assets/agentscripts/scadplay-smoke.agentscript.json`
（吃金币、踩踏板开门，三张截图）；单测 `gkNextUnitTests "[Live]"`。

## 改动怎么落地

| 改什么 | 怎么生效 | 失败时 |
| --- | --- | --- |
| 关卡 / kit 的 `.scad` | 游戏每 0.25 s 检查 `use/include` 闭包里每个文件的时间戳和大小，变了就先**只解析**（不求值）一遍，通过才请求重建 | 语法错误 / 缺文件：不重建，`build.ok = false` + 带文件和行号的 `error`，上一版关卡留在屏幕上 |
| C# 玩法 | `python3 .agents/skills/scadplay/scripts/publish_scripts.py`：`dotnet publish` 到游戏加载程序集的目录，引擎 0.5 s 内发现、卸载旧程序集、建新实例 | 编译错误：不发布，脚本打印错误，游戏继续跑旧代码；运行时异常：只停掉抛异常的那个机关，`fault` 里有异常和 `文件:行号` |

两种都会让关卡重建（C# 热重载后新实例需要重新声明角色 rig 池），玩家位置/速度/朝向、镜头、
每个机关的状态、检查点、死亡数和计时都会被带过去。只有 R 重开、传送门和通关跳关是"从头开始"。

## context.json

`out/scadplay/context.json`（仓库根下），按重命名替换写入，读到的一定是完整文件；有变化时每秒最多写 4 次，
没变化 5 秒心跳一次。同一时间只会有一个 ScadPlay 进程在写（`pid` 字段），脚本用它判断游戏是否还活着。

```json
{
  "version": 1, "game": "scadplay", "pid": 55080, "updated": "2026-11-02T04:07:59Z",
  "scene":  { "source": "/…/assets/scad/source/scadplay/playground.scad", "level": "playground", "live": true,
              "files": ["/…/playground.scad", "/…/assets/scad/lib/kit_scadplay.scad"] },
  "player": { "spawned": true, "pos": [0, 0, 0.05], "ground_z": 0.08, "forward": [1, 0], "yaw_deg": 0,
              "on_ground": true, "standing_on": "sp_spawn(\"start\") @ line 16" },
  "view":   { "forward": [0.93, 0], "yaw_deg": 0, "pitch_deg": -21.8, "hit": [3.63, 0, 0],
              "hit_node": "sp_ground#0 @ line 15" },
  "build":  { "serial": 2, "building": false, "ok": true, "error": "", "warnings": [], "warning_count": 0,
              "nodes": 54, "triangles": 4980, "ms": 77.1, "eval_ms": 2.8, "trigger": "file:playground.scad",
              "scene_shown": true },
  "scripts": { "serial": 1, "loaded_at": "…", "project": "projects/ScadPlay/Scripts/ScadPlay.csproj",
               "publish_dir": "/…/out/build/macos-arm64/bin/csharp/scadplay", "mechanism_types": ["sp_spawn", "…"] },
  "gameplay": { "title": "试玩场", "goal": "吃齐金币，打开终点", "state": "playing", "time": 12.3,
                "coins": 5, "coins_total": 8, "deaths": 0, "checkpoint": "",
                "mechanisms": [ { "key": "door_a", "module": "sp_door", "active": true, "status": "open 100 %",
                                  "pos": [12, 0, 0], "line": 23, "on": ["plate_a"] } ],
                "unbound": [], "warnings": [], "events": [ { "t": 4.1, "text": "门 door_a 打开" } ] }
}
```

| 字段 | 含义 |
| --- | --- |
| `scene.source` | 要编辑的文件（绝对路径）。`live: false` 时改了也不会重建 |
| `scene.files` | 被监视的全部文件：关卡 + 它 use/include 的 kit。改其中任何一个都会重建 |
| `player.pos` | 玩家**脚底**，SCAD 坐标 |
| `player.ground_z` | 脚下地面高度（跳在空中时也给），在这个高度上放东西 |
| `player.forward` / `yaw_deg` | 角色朝向。`rotate([0, 0, yaw_deg])` 把物体的 +X 转到玩家面朝的方向 |
| `player.standing_on` | 脚下是谁：机关按 id、其他按 `module#序号`，带关卡文件里的行号 |
| `view.*` | 镜头朝向；`hit` / `hit_node` 是屏幕中心看着的点和物体，用于"放到那边" |
| `build.serial` | 完成的构建次数（失败也算）。改完等它变大且 `building` 为 false |
| `build.ok` / `error` / `warnings` | 构建结果。`warnings` 是 SCAD 求值警告原文（"unknown module 'x'"） |
| `build.ms` / `eval_ms` | 从保存到可玩的总耗时 / 其中 SCAD 求值耗时 |
| `scripts.serial` | C# 热重载次数（0 = 启动时那份）。发布后等它变大 |
| `scripts.publish_dir` | 运行中的游戏从哪加载程序集——发布必须写到这里才会热重载 |
| `gameplay.mechanisms[]` | 每个玩法件：`key`（id 或 `module#序号`）、`active`、`status`、位置、**行号**、接线、`fault` |
| `gameplay.unbound` | 带 `id` 参数却没有 C# 类的 module 名——该写类了 |
| `gameplay.warnings` | 接线问题（听了不存在的 id、id 重名）、机关异常 |
| `gameplay.events` | 最近 40 条玩法事件（踩下、开门、吃金币、死亡、通关），按关卡时间 |

### "前面"在哪

默认以**角色**为准，不用镜头；只有请求指着某个东西（"那边""那座山上"）才用 `view.hit`。
`p = player.pos`，`f = player.forward`，距离 `d`：

| 请求 | x, y |
| --- | --- |
| 前面 | `p + f * d` |
| 后面 | `p - f * d` |
| 右边 | `p + [f.y, -f.x] * d` |
| 左边 | `p + [-f.y, f.x] * d` |

没说距离就取 5 m 加物体半个进深，免得放在玩家身上。高度用 `player.ground_z`。
尺度：玩家高 1.6 m、宽 0.7 m，能直接迈上 0.5 m 的台阶，单跳 ≈ 2 m 高，助跑跳远 ≈ 5 m。

## 写关卡的规则

- **追加到文件末尾**（`// ==== Agent additions` 那行之后）。机关的状态按 key 保留，匿名件的 key 是
  `module#序号`（同名 module 在场景里的出现次序）——在中间插入会让后面同名件的序号全部错位，状态就对不上了。
  要保留状态的机关**一律给 `id`**。
- **一个物体一个 module，调用一次**，写一行注释说明是什么请求：

  ```openscad
  // agent: "在前面加一座瞭望塔"
  module agent_watchtower_1()
  {
      sp_block(w = 2.4, d = 2.4, h = 0.4, c = sp_STONED());
      for (sx = [-1, 1], sy = [-1, 1]) translate([sx * 0.9, sy * 0.9, 0.4]) sp_pillar(r = 0.15, h = 3);
      translate([0, 0, 3.4]) sp_block(w = 3, d = 3, h = 0.3, c = sp_WOOD());
  }
  translate([6, 0, 0]) agent_watchtower_1();
  ```

  物体围绕自己的原点建、底面 z = 0，位置和朝向写在调用上，以后挪动只改一行。
- 先用 kit：关卡已经 `use` 的 `kit_scadplay.scad`（`sp_*`），再看 `assets/scad/lib/catalog.json`
  （很大，按名字 / 类别 grep，不要整份读）。要用别的 kit 就在文件头加一行 `use <../../lib/kit_x.scad>`。
- `collision = "none"`：不挡路、射线也穿过（装饰、触发区）。默认参与碰撞。
- 要改 / 挪 / 删之前加的东西：先重读文件，原地改那条语句，不要再追加一条。
- 文件保持 UTF-8，自己没加的字节一个不动。语言面见 [SCADLoader.md](SCADLoader.md)：OpenSCAD +
  `gk_*` 扩展；`$fn` 写成 module 里的局部赋值（`$fn = 16;`），**作为调用参数（`cylinder(..., $fn = 16)`）
  目前不生效**。

## 玩法件（kit_scadplay 契约）

`assets/scad/lib/kit_scadplay.scad` 文件头是权威版本，这里是摘要。**带 `id` 参数的 module 就是玩法件**，
每个都有同名 C# 类；激活（active）是它对外的信号。

接线：`on` = 全部激活才启用（字符串或列表：`on = "plate_a"`、`on = ["plate_a", "lever_b"]`），
`any` = 任一激活即可；`id` 末尾 `*` 是前缀通配（`on = "coin_*"`）。向量参数（`travel`）是**该件自己的
局部 SCAD 坐标**，随 `rotate()` 转。字符串参数里不要写 `;` `=`，列表元素里不要写 `,`。

| module | 关键参数 | 激活 = | 行为 |
| --- | --- | --- | --- |
| `sp_spawn` | `id = "start"` | 恒真 | 出生点，面朝 +x，不挡路 |
| `sp_level` | `title`, `goal` | 恒真 | HUD 标题与目标（不可见） |
| `sp_checkpoint` | `r` | 已到达 | 碰到后成为复活点，旗子升起 |
| `sp_coin` / `sp_coin_row` | `value`, `hover`; `n`, `dx`, `prefix` | 已吃掉 | 吃掉计分；row 的 id = prefix + 序号 |
| `sp_goal` | `on`/`any`, `next` | 已到达 | 输入满足才开放；碰到通关，`next` = 下一关文件名 |
| `sp_plate` | `latch`, `w` | 被踩住 | `latch` 踩一次永久激活 |
| `sp_lever` | `toggle` | 已扳下 | 走近按 E |
| `sp_door` | `on`/`any`, `near`, `latch`, `w`, `h`, `travel`, `speed` | 打开 | 默认门板沉进地面 |
| `sp_mover` | `on`/`any`, `travel`, `speed`, `wait`, `mode`("loop"/"toggle"), `w`, `d`, `t` | 启用 | 移动平台，站上去会被带走 |
| `sp_spinner` | `on`/`any`, `speed`(°/s), `r` | 启用 | 旋转平台 |
| `sp_bridge` | `on`/`any`, `len`, `w`, `speed` | 已放下 | 吊桥，铰链在原点沿 +x 伸出 |
| `sp_jumppad` | `on`/`any`, `launch`(m), `r` | 启用 | 站上去弹起 |
| `sp_hazard` | `w`, `d`, `kind`("lava"/"spikes"/"water") | 恒真 | 碰到即死，回检查点 |
| `sp_sweeper` | `on`/`any`, `len`, `height`, `speed` | 启用 | 旋转扫杆，碰到即死 |
| `sp_enemy` | `travel`, `speed`, `stompable` | 已消灭 | 巡逻；碰到即死，从上方踩可消灭 |
| `sp_sign` | `text`, `label`, `r` | 玩家在旁 | 走近显示文字 |
| `sp_portal` | `on`/`any`, `to`, `spawn` | 启用 | 传送到同目录另一关的某个出生点 |
| `sp_counter` | `from`, `count` | 达成 | `from` 匹配到的件（module 名或 id 前缀）激活数 ≥ `count`（0 = 全部） |
| `sp_logic` | `on`/`any`, `invert`, `delay` | 条件成立 | 逻辑门 + 延时 |
| `sp_crumble` | `w`, `d`, `warn`, `respawn` | 已塌落 | 易碎平台 |

纯几何件（无玩法）：`sp_ground`（**顶面** z = 0）、`sp_block`、`sp_ramp`、`sp_stairs`、`sp_wall`、
`sp_pillar`、`sp_tree`、`sp_crate`。

**活动件**：会动的部分是 `sp_part_<件>()`，必须是玩法件 module 的**直接**子调用，不能放进 `gk_flatten`；
它的原点是运动基准。运行时只改它相对绑定姿态的局部变换，它的碰撞被换成同形状的运动学刚体，玩家站在上面
会被带着走。静态外壳包进 `gk_flatten()`。

## 写玩法（C#）

```text
projects/ScadPlay/Scripts/
├── ScadPlayGame.cs            生命周期：加载关卡、重建、热重载衔接、输入、镜头
├── Gameplay/Mechanism.cs      玩法件基类 + MovingPart（活动件 + 运动学碰撞）
├── Gameplay/MechanismTypes.cs module 名 → C# 类 的登记表（一行一个）
├── Gameplay/World.cs          信号（id/on/any）、出生 / 检查点 / 死亡 / 通关 / 金币、事件与警告
├── Gameplay/StateBag.cs       机关状态（跨重建、跨热重载）
├── Mechanisms/*.cs            每个 kit module 一个类
├── Level/                     节点索引（Scene.DescribeNodes）、参数解析、SCAD↔引擎坐标
├── Live/                      LiveLevel（监视/预检/重建）、AgentContext、Session（热重载存档）
├── Player/                    角色控制器 + astro_bot rig、跟随镜头
└── UI/Hud.cs                  HUD、构建状态、L 键标注
```

加一种机关 = 三步：kit（或关卡文件）里写 module（带 `id` 参数、活动件用 `sp_part_*`）→
`Mechanisms/<Name>.cs` 写一个 `Mechanism` 子类 → `MechanismTypes.cs` 登记一行。然后 `publish_scripts.py`。

```csharp
/// <summary>sp_bell: rings (active) while the player is within r metres.</summary>
public sealed class Bell : Mechanism
{
    public override void OnTick(float deltaSeconds)
    {
        bool near = PlayerDistanceXZ(Position) < Params.Num("r", 2.0f);
        if (near && !Active)
        {
            World.Log($"铃铛 {Key} 响了");
        }
        Active = near;
    }
}
```

`Mechanism` 给的东西：`Params`（`Num/Flag/Str/Vector/Ids`，默认值写成和 kit 一致）、`Position` /
`Rotation` / `WorldBounds`（引擎空间）、`LocalToWorldDirection(scadVector)`、`InputsSatisfied()`、
`Part()`（活动件：`SetPose(偏移, 轴, 角度, dt)`、`SetVisible`）、`PlayerStandsOn` / `PlayerTouches` /
`PlayerDistanceXZ`、`World`（`Signal`、`Kill`、`CompleteLevel`、`Travel`、`Toast`、`Log`、
`OfferInteraction`、`Player.Launch`）。状态要跨重建保留就覆盖 `Save` / `Load`（基类已存 `active`）。

AOT 规则照常（[CSharpGameDevelopment.md](CSharpGameDevelopment.md) §10）：不用反射、不用
`JsonSerializer.Deserialize<T>`，所以登记表是显式的一行一个。

## 架构与不变量

**每次加载的顺序**（引擎决定）：`BeforeSceneRebuild`（旧世界还能读：存下机关状态和玩家；声明 rig 池）
→ 引擎丢弃旧场景、求值 SCAD、上传 → `OnSceneLoaded`（`Scene.DescribeNodes` 建索引，按 module 名实例化
C# 机关，`Load` 状态，玩家放回原处）。

为此加到引擎里的东西（都不是 ScadPlay 专用）：

- `FLoaderRegistry` 的**源文件预检**（`RegisterSceneSourceCheck`）和**加载报告**（`PublishLoadReport`，
  带递增 `serial`，加载失败时游戏收不到 scene-loaded 回调，靠它判断"这次加载结束了"）。ScadLoader 注册了
  `.scad` 的预检（只解析闭包），每次加载发布 警告原文 / 依赖文件 / 节点数 / 三角数。
- ScadLoader：扁平向量参数写进 metadata（`travel=0,0,3`、`on=a,b`）；`collision = "none"`；
  求值器记录警告原文；节点 instance id 改为输出序号（原来并行求值时 64 位 id 截断成 32 位会**重复**，
  所有按 id 查节点的地方都可能拿错节点）。
- C# 绑定：`Scene.DescribeNodes / GetNodeWorldTransform / GetNodeBounds / RayCast /
  MakeNodeKinematic / SetNodeRayCastVisibleRecursive`、`Assets.CheckSceneSource / GetSceneCheckReport /
  GetSceneLoadReport`、`Character.Create / Move / SetPosition / GetState / Destroy`（ABI 8）。
  角色控制器属于游戏：卸载或热重载时引擎会销毁所有遗留的角色。
- rig 部件不再拿到隐式静态碰撞体（以前留在部件创建的位置——通常是原点——把在那里出生的角色卡住）。
- `gk_add_managed_game_application(... INSTALL_RIG)`：独立 C# 游戏也能用 ScadRig。

踩过的坑：

- 引擎**先丢弃当前场景再解析新文件**——没有预检，一个语法错误就让玩家掉进空世界。
- 活动件的 CPU 射线结构不随运动更新：`standing_on` 先查角色脚下刚体属于哪个机关，射线只作后备。
- 引擎在关卡包围盒底部放了一块看不见的地板：掉下去的玩家会站在"什么都没有"的地方，
  `World` 把"站在底板上、脚下没有关卡几何"当作掉出世界。
- 角色自己的 rig 对射线可见时，镜头吊臂会打到玩家的头：生成后把 rig 子树设为射线不可见。
- 自动跟随镜头只在"往前跑"时跟：跟随横移会让"右"跟着转，玩家原地画圈。
- 截图 / validate 用 `--agent-validation` 隐藏窗口启动，同样会写 `context.json`——跑验证时别和自己正在玩的那局混淆
  （看 `pid`）。
