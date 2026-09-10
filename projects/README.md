# projects/ — 游戏工程

每个托管（C#）游戏是这里的一个目录，游戏**自己拥有**的一切都在里面：清单、内容、C# 代码。
引擎自己的东西（`assets/`、`assets/csharp/GkNext.*`、`src/`）不在这里。

```
projects/
├── Directory.Build.props      # 把下面每个 Scripts/*.csproj 接到引擎的 assets/csharp/Directory.Build.props
├── <Game>/
│   ├── <id>.game.json         # 清单：窗口、程序集、依赖模块、初始场景、热重载（设计见 §3）
│   ├── Content/               # 这个游戏的配置 / 音效 / 贴图 / 场景
│   └── Scripts/               # C# 工程：<Name>.csproj + *.cs
```

现有工程：

| 目录 | id | 说明 |
|---|---|---|
| `Flappy/` | `flappy` | Flappy 对照组。`Content/` 同时被 C++ 版 `FlappyCpp` 读取——replay parity 靠的就是两端读同一份配置 |
| `Brotato3D/` | `brotato3d` | Brotato3D 的 C# 版。`Content/configs` 同时被 C++ 版 `Brotato3D` 读取 |
| `Sandbox/` | `sandbox` | 绑定层的最小可运行样本。唯一没有 `Scripts/` 的工程：它的代码是引擎的 `GkNext.Game` 探针，留在 `assets/csharp` |

新工程由 launcher 的 **New Project** 或编辑器的 **File > New Game Project** 从
`assets/templates/games/` 下的模板生成，落在 `projects/<Name>/`。

## 运行时

构建时 CMake（`assets/cmake/RuntimeAssets.cmake`）把每个工程的**清单和 `Content/`** 拷到运行时资产树的
`assets/projects/<Game>/`；`Scripts/` 永远不拷——跑的是发布到 `<bin>/csharp/<id>/` 的程序集。
这样工程内容就是资产命名空间里普通的一棵子树，pak、asset trace、Android APK、iOS bundle 都不需要知道它的存在。

launcher 与编辑器扫描 `assets/projects/*/*.game.json` 列出游戏；per-game exe 直接指向自己那份清单。

## 在代码里引用内容

C# 用 `GameContent`，路径相对工程的 `Content/`：

```csharp
byte[] json = GameContent.ReadFile("configs/gameplay.json");
string flap = GameContent.Path("sounds/flap.wav");   // "assets/projects/Flappy/Content/sounds/flap.wav"
Audio.PlaySfx(flap);
```

引擎资产（`assets/scad/...`、`assets/models/...`）照旧写完整路径。清单里的 `icon` / `initialScene` 以
`Content/` 开头时解析到本工程内容，否则原样当作引擎资产路径或内置场景名（`Empty.proc`）。

`GameContent.Path` 每次都新建字符串：在 `OnInit` 或字段初始化里解析一次缓存起来，不要每帧调用。

## 改了之后

- 改 C# 或 `Content/`：launcher / 编辑器里点 **Rebuild C#**——重新发布程序集，并把 `Content/` 同步到运行时副本。
  不需要 C++ 构建。
- 新增一个 C# 工程后跑 `gnb dotnet sln`，让它进 `assets/csharp/GkNextManaged.sln`（IDE 从解决方案打开）。
- 需要独立 exe 时再加 `src/Application/Game/<Name>/`，`gk_dotnet_managed_game(... PROJECT "${GK_GAME_PROJECTS_ROOT}/<Game>/Scripts/<Name>.csproj" ...)`。

完整说明：[docs/AGENT_GUIDE/CSharpGameDevelopment.md](../docs/AGENT_GUIDE/CSharpGameDevelopment.md)、
[docs/designs/managed-game-launcher-design.md](../docs/designs/managed-game-launcher-design.md)。
