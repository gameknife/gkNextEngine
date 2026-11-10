# iOS NativeAOT C# 真机开发计划

状态：P0–P2 已实施（2026-09-11）。`TestFPS` 已完成 arm64 NativeAOT 静态链接、签名打包；
P3 的物理设备安装、交互、生命周期与跨平台回归仍需在指定设备上执行，不能以本机 bundle 构建代替。

## 1. 目标与范围

沿用 Android 的使用方式，通过 `gnb ios build --app <target>` 将 C# 游戏编译、链接、签名到
iOS application，再通过 `gnb ios run --device <ID>` 安装到 iPhone/iPad 真机运行。
复用现有 C# 工程、生成绑定、`ManagedGameHostInstance` 和 NativeAOT host，游戏无需维护 iOS 分支。

首批支持 `DotNetSandbox`、`FlappyCSharp`、`Brotato3DCSharp`，按此顺序验证。
构建宿主首先限定 macOS arm64 + Xcode，目标为 `ios-arm64` / `iphoneos`。
用户自建 C# 工程沿用现有 application target + 移动注册表流程；本期不新增直接传任意 csproj 的 CLI。

本期不包含模拟器、Mac Catalyst、App Store 发布、CoreCLR/JIT、热重载、Launcher 动态换游戏。
每个 app 静态包含一个游戏和一份 NativeAOT runtime；更新 C# 后重新 build、签名、安装。
macOS 上运行 iOS wrapper 可用于辅助排障，但不能替代真机验收。

## 2. 当前基础与缺口

| 环节 | 当前代码事实 | 计划改动 |
| --- | --- | --- |
| .NET 配置 | `cmake/SetupDotNet.cmake` 在 `IOS` 分支直接 return；Android 强制 AOT、使用 `linux-bionic-arm64` | iOS 强制 AOT，分离宿主 RID 与 `ios-arm64` 目标 RID |
| 托管发布 | `assets/csharp/GkNext.Bootstrap/GkNext.Bootstrap.csproj` 已预留 `GkNativeLib=Static`；iOS runtime pack 尚未接入 | 增加 iOS 条件属性，验证静态产物及完整链接依赖 |
| 原生链接 | `gk_dotnet_managed_game` 只处理 DLL、dylib、SO；iOS 会落入通用 APPLE 动态库分支 | 在 APPLE 之前处理 IOS，建立静态库依赖和链接规则 |
| Host / ABI | `AotHost.cpp` 已直接调用 `GkNext_Bootstrap`，使用 `FEngineApi` / `FManagedApi` 函数表 | 原则上保持 ABI；验证初始化、反向回调、GC 和生命周期 |
| 应用注册 | `MobileApplications.json` 中三个 C# target 都只有 android，已有 iOS bundle id | 验证通过后加入 ios；保留 `requiresDotNet` 检查 |
| 注册表测试 | `TestManagedGamesAreAndroidOnly` 明确禁止 C# 出现在 iOS 列表 | 改为验证两平台可见性与托管依赖声明 |
| iOS 编排 | `tools/gnb/internal/ios/ios.go` 已支持选 app、Xcode build、签名、devicectl 安装启动 | 补齐工具链诊断与托管应用回归，复用已有流程 |
| 资源 | `TargetHelpers.cmake` 将构建树 assets 拷入 bundle | 确认项目 manifest / Content 已在拷贝前完成 staging，C# 从 bundle 读取 |

当前托管层使用 `net10.0`，`gnb.toml` 的 SDK 版本下限为 `10.0.300`，允许使用更高版本，
不是严格锁版。现有 `ios-device` preset 最低系统版本为 iOS 15.0；NativeAOT 是否能保持此下限，
必须在第一阶段确认，不能仅凭 preset 宣称兼容。

## 3. 技术路线与先决验证

采用“C# → NativeAOT 静态库 → 现有 C++ iOS executable → 签名 .app”的路线。
这是本项目的工程选择：复用现有直接调用入口及 Xcode bundle 构建，减少额外运行时文件。

微软文档说明，无 iOS workload 依赖的普通类库可以使用 `PublishAotUsingRuntimePack` 发布到
iOS，并可通过 `NativeLib=Static` 生成 `.a`；但文档没有覆盖静态库消费端链接配置，且提示静态库
存在限制。因此“产出一个 .a”不能作为可行性结论，必须完成实际链接和真机调用。
参见 [微软 iOS NativeAOT 类库文档](https://learn.microsoft.com/en-us/dotnet/core/deploying/native-aot/ios-like-platforms/creating-and-consuming-custom-frameworks)。

首先验证以下发布参数，作为探针起点而非已验证命令：

```text
dotnet publish assets/csharp/GkNext.Bootstrap/GkNext.Bootstrap.csproj
  -c Release -r ios-arm64
  -p:GkAot=true -p:GkNativeLib=Static
  -p:PublishAotUsingRuntimePack=true
  -p:GkGameProject=<游戏 csproj 的绝对路径>
  -p:GkNativeName=GkNext.Bootstrap.<target>
  -o <当前构建树中的独立 staging 目录>
```

保留 `net10.0`，不为游戏整体切换到 MAUI 或 `net10.0-ios`。
宿主 ILCompiler 与目标 runtime pack 必须来自兼容的同一版本；是否需要显式宿主包引用、
额外 opt-in 属性及具体 SDK 参数，以选定 SDK 的 MSBuild targets 和探针结果为准。
Android 的 linker / SONAME 参数继续局限于 Android 条件，不直接复制到 iOS。

## 4. 分阶段实施

### P0：静态链接与真机可行性门槛

- 记录实际 .NET SDK/runtime pack、Xcode、iphoneos SDK、设备系统版本；检查宿主架构与工具链可用性。
- 用最小托管导出完成加法、对象分配、显式 GC、托管调用原生函数指针，再验证现有 Bootstrap ABI。
  可复用 `src/Modules/NextDotNet/Probe/` 的断言，但其桌面 executable 不能直接当 iOS app 安装；
  需要带签名的最小 iOS 宿主承载探针。
- 检查 `.a` 的实际文件名、archive 成员、arm64 和 iOS 平台标记；确认是否包含完整运行时，
  列出最终 executable 所需的补充 archive、系统库、framework、初始化与符号保留规则。
- 使用实际 Xcode 链接设置验证 dead stripping 后入口和 runtime 初始化仍有效；确有需要时仅对
  相关 archive 使用 force-load，不全局开启 all-load。检查与现有 C++ 静态库并用时的重复符号。
- 统一 NativeAOT 和 CMake 的最低系统版本、sysroot；若 runtime 要求高于 iOS 15，明确支持下限，
  并在配置阶段拒绝不兼容组合。

验收：签名 app 在物理设备启动，双向调用及 GC 断言通过，保存可复现命令与链接依赖清单。
P0 未过不开放注册表。若静态路线有无法解决的上游阻塞，先记录复现与版本范围，修订方案评估
NativeAOT framework，再进入后续阶段；不能把普通 macOS arm64 库当作 iOS 库替用。

### P1：CMake / MSBuild 正式接入

- 修改 `cmake/SetupDotNet.cmake`：移除 iOS 的无条件禁用，验证宿主、目标架构、sysroot 与 SDK，
  强制 AOT、设置 `ios-arm64`；AOT 不查 hostfxr 头文件。
- 缺 .NET 时保持纯 C++ 应用可构建；`requiresDotNet=true` 应用配置失败并给出修复命令，
  复用 `src/Application/CMakeLists.txt` 已有 fail-fast，不生成无托管层的空壳 app。
- Bootstrap csproj 增加 iOS 专属发布属性；将 P0 的依赖封装在一个 CMake helper/imported target，
  避免每个游戏重复维护系统链接参数。
- `gk_dotnet_managed_game` 增加静态库路径，声明准确 OUTPUT / BYPRODUCTS 与 target 依赖。
  staging/stamp 按 target、RID、配置隔离，发布完成后才链接，链接与资源拷贝完成后才签名。
- 保留托管 publish 串行链；检查共享 `obj` 在 RID、游戏、后端切换时的缓存污染，必要时统一隔离
  中间目录。发布参数、csproj/props/targets、C# 源文件变化必须触发重建。
- 核查 `src/Modules/CMakeLists.txt` 的 NextDotNet 配置及原生 host 源码：移动端不能走启动 dotnet
  子进程、动态程序集加载或桌面热重载路径。不承载游戏但链接模块的目标继续使用 AOT stub。

验收：真实 iOS application 能完成托管发布与原生链接；重复 build 不重复 publish；修改 C# 会
重新发布并 relink；切换 target、配置和桌面/iOS 构建不会复用错误产物。无需把 `.a` 放进 app 资源。

### P2：应用、资源与 gnb 闭环

- 将 `DotNetSandbox`、`FlappyCSharp`、`Brotato3DCSharp` 加入 iOS 注册；更新注册表注释及测试。
- 复用三个 target 的 `gk_dotnet_managed_game` 和 `ManagedGameHostInstance`；检查叶子 CMake
  在只配置一个移动 application 时自洽。
- 核实 `projects/<Game>/<id>.game.json`、Content/configs/scenes/sounds 到 bundle 的完整链路，
  确保 manifest 的项目路径在 AOT 下不触发 DLL 或 csproj 加载；写入需求使用可写沙盒目录。
- gnb 输出当前 app、后端、目标 RID 与 SDK；失败明确区分发布、链接、签名、安装、启动阶段。
  如需扩展 `dotnet status`，只增加诊断，不维护第二套发布实现。
- 保持 `gnb ios run` 使用构建产物 manifest 选择 app；测试换 app 后安装的是新 bundle。
  真机运行显式指定物理设备，避免默认选择本机 wrapper。

验收目标操作（实施完成后可用）：

```bash
./gnb.sh dotnet setup
./gnb.sh ios build --app FlappyCSharp --team-id <TEAM_ID>
./gnb.sh ios device
./gnb.sh ios run --device <PHYSICAL_DEVICE_ID>
```

### P3：真机功能与回归

| 验证项 | 通过条件 |
| --- | --- |
| DotNetSandbox | 日志确认 NativeAOT、ABI 初始化、托管 Tick 和回调；对象分配/GC 后仍可调用 |
| FlappyCSharp | 出现 `committed scene [...]`，角色持续运动；验证现有触控/输入链、开始、碰撞、重开 |
| Brotato3DCSharp | 场景和 HUD 正常、输入可驱动玩法，连续运行至少 10 分钟无崩溃或持续异常增长 |
| 生命周期 | 前后台切换、锁屏恢复、停止后冷启动正常，恢复后 Tick 与输入继续工作 |
| 内容与更新 | 从 bundle 读取配置/声音等；修改 C# 可观察行为变化，修改 Content 后重新安装可见更新 |
| AOT 约束 | 热重载明确不可用；不依赖 hostfxr、外部托管 DLL 或构建机路径运行 |
| 故障路径 | 缺 SDK/pack、发布失败、无效签名、错误设备都有明确错误且不被当作成功 |
| 非托管 iOS | `gkNextRenderer` 仍可构建；链接 NextDotNet 的非游戏目标检查 stub 无重复入口 |
| Android / 桌面 | FlappyCSharp Android NativeAOT 和桌面 CoreCLR/AOT 的已有探针保持通过 |

保存设备型号/系统、工具链版本、启动与探针日志、截图、运行时长和内存观察结果。
仅 `committed scene` 不足以证明 C# 已工作，必须同时有托管行为或断言证据。
真机不直接套用桌面的隐藏窗口 `gnb shot` 验收；利用设备截图与日志完成可视验证。

验证命令按影响范围串行执行：gnb 的 Go 改动运行相关 `internal/ios`、`internal/mobileapps` 测试；
触及 host 或托管层按仓库要求运行 `./gnb.sh dotnet ci`，涉及模板依赖时运行
`./gnb.sh dotnet templates`。iOS 用上述目标逐个构建；Android 做对应目标回归。
不得同时启动多个 gnb/CMake/Ninja build。硬件不足的检查记录为未验证，不能算真机验收通过。

### P4：文档收口

- 更新 `docs/designs/mobile-application-targets.md` 中“iOS 没有 C#”的边界。
- 更新 `.NET` 设计、C# 开发指南、gnb CLI 指南与 AGENTS.md，说明环境准备、注册新工程、真机
  build/run、资源路径、最低系统版本及 AOT 更新方式。
- 将 P0 确认的静态链接契约并入长期设计；全部验收后按 docs 生命周期移除此计划和索引入口。

## 5. 依赖顺序与完成定义

执行顺序为 P0 → P1 → P2 → P3 → P4。最高风险是静态 runtime 的链接与初始化，以及 SDK 和
iOS 最低版本的兼容性；先解决这些，再扩展应用列表。资源 staging 与签名的执行顺序也是必须
验证的构建契约，不能靠签名后补拷文件解决。

完成需同时满足：三个首批 C# 应用可通过现有 gnb 命令构建和安装；物理 iOS 设备证明托管代码与
交互正常；C# 和内容更新可重复部署；Android 与桌面回归通过；长期文档准确描述已验证的支持范围。
