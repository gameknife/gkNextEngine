# NextDayz Web 客户端开发指南

本页讲怎样开发、烘焙、测试和部署 NextDayz 的浏览器客户端。为什么这样设计，见
[Web 客户端架构设计](../projects/nextdayz/nextdayz-web-client-design.md)；还没做完的工作见
[剩余计划](../projects/nextdayz/nextdayz-web-client-plan.md)；具体的人工验收步骤和快捷入口见
[web/nextdayz/README.md](../../web/nextdayz/README.md)。

命令以 `./gnb.sh` 为例，Windows 换成 `gnb.bat`。构建串行执行，不要同时跑两个 `gnb build` 或 `gnb web build`。

## 组成

| 位置 | 内容 |
| --- | --- |
| `src/Application/Game/NextDayz/Core/CMakeLists.txt` | `NextDayzCore` 的文件清单：桌面 NextDayz 和 WASM 共用的玩法代码 |
| `src/Application/Web/NextDayzWebCore/` | WASM 入口 `WebCore.cpp`，C 导出函数都以 `Nd` 开头；Jolt 物理与角色适配；三个探针（协议、规则、rig 姿态）；`protocol-golden.txt`；CMake 预设 `nextdayz-wasm` |
| `src/Application/Util/NextDayzBake/` | 离线烘焙工具：用桌面的 ScadLoader 求值地图和 rig，导出 Web 资产 |
| `web/nextdayz/src/` | TypeScript 表现层：three.js 场景、角色、音频、DOM 界面 |
| `web/nextdayz/shared/` | `client.mjs` 把 WASM 包成 `Client`，浏览器和 Node 机器人共用；`bots.mjs` 是 Web 机器人 |
| `web/nextdayz/test/` | `*.test.mjs` 是 `gnb web test` 运行的单测；其余脚本是要连真实服务的 Playwright 验收 |
| `out/web/nextdayz/` | 产物：`core/`（`ndcore.mjs`、`ndcore.wasm`、`build.json`）、`assets/`（烘焙资产和 `manifest.json`）、`site/`（`vite build` 的输出）、`emsdk/` |

`web/nextdayz/w0`–`w2` 是一次性原型。`shared/client.mjs` 仍从 `w1/transport.mjs` 取 WebRTC 传输，
所以这几个目录暂时不能直接删除，清理见剩余计划 R6。

## 一次性准备

需要 Go（gnb 自身）和 Node.js 24。

```sh
./gnb.sh build NextDayz NextDayzBake
./gnb.sh web setup
./gnb.sh web icons
./gnb.sh web bake --rigs
./gnb.sh web build
```

1. `build`：在 `out/build/<preset>/vcpkg_installed` 里装好 vcpkg 头文件（glm、fmt、spdlog、nlohmann），
   并产出烘焙工具和图标导出要用的 NextDayz。
2. `web setup`：把固定版本的 Emscripten 4.0.23 装到 `out/web/nextdayz/emsdk`。已经装过的可以设
   `ND_EMSDK` 指过去。
3. `web icons`：用 Vulkan 启动 NextDayz，离屏导出物品图标。Linux 上使用 headless surface。
4. `web bake --rigs`：烘焙已发布地图、角色 rig 和共享资产。
5. `web build`：构建 WASM 内核并做三项检查，然后构建网页。

Jolt 源码的来源依次是：环境变量 `ND_JOLT_SOURCE`；本机 vcpkg 构建缓存里的 v5.4.0；都没有时，由 CMake
下载同一版本并核对校验和。

## 日常开发

先启动一个带 WebRTC 端口段的本机 Master，再启动开发服务器：

```sh
./gnb.sh master serve --listen 127.0.0.1:7784 --rtc-port-base 28700 --data out/web/nextdayz/dev-master --cheats
./gnb.sh web dev
```

`gnb web dev` 依次构建并检查内核、安装 npm 依赖、核对烘焙资产，然后在 <http://127.0.0.1:4179/> 启动 Vite。
它会一直监视已发布地图的源文件：有变化就重烘，清单更新后浏览器自动刷新（`--watch=false` 关闭监视）。

| 环境变量 | 作用 | 默认 |
| --- | --- | --- |
| `ND_WEB_PORT` | 开发服务器端口 | 4179 |
| `ND_MASTER_URL` | `/api/` 代理到的 Master | `http://127.0.0.1:7784` |
| `ND_DIRECT_URL` | 「开发直连」的 DS 信令地址，配合 `gnb ds --web` | `http://127.0.0.1:7781` |
| `VITE_ND_REVIEW=1` | 打开验收快捷入口（传送到练习区等），只在开发模式生效；Master 需带 `--cheats` | 关 |

要让桌面客户端进同一个世界，用另一个昵称登录同一个 Master：

```sh
./gnb.sh run NextDayz -- --cvar "nextdayz.net.mode client" --cvar "nextdayz.net.master http://127.0.0.1:7784" \
  --cvar "nextdayz.net.name Desktop" --cvar "nextdayz.net.autoDeploy true" --cvar "nextdayz.net.autoMap training_ground"
```

### 改玩法

1. 玩法只写在 `NextDayzCore` 里。TS 里出现速度、伤害、能否放入、是否命中这类常量或判定，就是缺陷：
   缺什么数值，就让内核输出。
2. 给内核加 `.cpp` 要改两处：`Core/CMakeLists.txt` 的显式清单，以及
   `src/Application/Game/NextDayz/CMakeLists.txt` 里排除内核文件的正则。漏了后者，桌面会把同一个文件编译两次。
3. 内核只能依赖标准库、上面那几个 vcpkg 头文件，以及 `tools/gnb/cmd/gnb/web_core.go` 里
   `checkWebCoreIncludes` 白名单上的少数引擎文件。要扩大白名单，等于修改设计 5.4，先交回核对。
4. 浏览器需要的新数据，从 `WebCore.cpp` 的导出函数出去，经 `shared/client.mjs` 和 `client.d.mts` 交给表现层。
   每帧都要读的数据放进共享内存视图，不要再加 JSON。
5. 剩余计划 R1 完成之前，DS 消息的处理和客户端帧顺序有两份：桌面在 `NextDayzNetwork.cpp` 与
   `NextDayzGameInstance.cpp`，Web 在 `WebCore.cpp`。改其中一份时，另一份也要同步，并对照设计 5.3 列出的分歧。

### 改协议

不为了 Web 改线上字节。确实要升级协议时，按现有规矩提升 `kProtocolVersion`，桌面和 Web 一起改，并重新生成金标：

```sh
./gnb.sh build NextDayzProtocolGolden
out/build/<preset>/bin/NextDayzProtocolGolden > src/Application/Web/NextDayzWebCore/protocol-golden.txt
./gnb.sh web build
```

`gnb web build` 会确认 WASM 编出的字节和新金标一致。金标文件保持 LF 换行，和协议改动放在同一个提交里。

### 改地图和角色

`assets/configs/nextdayz_maps.json` 里 `status` 为 `published` 的地图才会烘焙，产物按 `mapVersion` 放在
`out/web/nextdayz/assets/maps/<id>/v<版本>/`。

```sh
./gnb.sh web bake --map training_ground   # 只烘一张图
./gnb.sh web bake --rigs                  # 地图、角色 rig 和共享资产
./gnb.sh web bake --rigs --check          # 只核对，不写文件
./gnb.sh web bake --watch                 # 源文件变化后只重烘受影响的部分
```

清单记录了每份产物的源文件哈希和输出哈希。如果源文件改了没重烘、清单与地图注册表不符，或输出文件缺失、
被改动，`--check` 都会失败。

浏览器内现场求值 SCAD 还在试验阶段（设计 D2），入口见 web README 的
[浏览器现场生成验收](../../web/nextdayz/README.md#浏览器现场生成验收)。发布包仍然只用烘焙资产。

## 测试

| 命令 | 测什么 | 需要什么 |
| --- | --- | --- |
| `./gnb.sh web build` | 内核包含边界、WASM 协议金标、规则探针（Node 里运行）、`tsc` 类型检查、`vite build` | Emscripten、vcpkg 头文件 |
| `./gnb.sh web test` | `web/nextdayz/test/*.test.mjs`：登录、档案、出击重试等客户端流程 | 已构建的 `ndcore.mjs` |
| `./gnb.sh build NextDayzProtocolGolden NextDayzCoreRules`，再运行这两个程序 | 原生协议字节与金标逐字比对、原生规则探针 | 桌面构建 |
| `./gnb.sh web test --bots 16 --seconds 1800 --latency 80 --loss 2 --master <url>` | Node 里的 Web 机器人经 WebRTC 完成登录、出击、拾取、丢弃和撤离 | 一个带 `--rtc-port-base` 的 Master；口令用 `ND_BOT_PASSWORD` 传 |
| `node web/nextdayz/test/w7*.mjs` 等脚本 | 真实浏览器：界面、混合客户端、抢物资、死亡后重新出击 | 手动启动的 Master 和开发服务器；本机装有 Chrome |
| `node web/nextdayz/test/character.mjs` | WASM 角色控制器在障碍夹具上的表现 | 先用 `NextDayzBake map` 导出夹具，见 web README |

Playwright 脚本通过 `ND_TEST_SITE`、`ND_TEST_MASTER` 找服务，用 `ND_TEST_PRESET` 找桌面构建，默认是
`macos-arm64`，其他平台要设成自己的 preset。截图和结果写到 `out/web/nextdayz/` 下各脚本对应的目录。

### 提交前检查

| 改了什么 | 至少要跑 |
| --- | --- |
| `NextDayzCore`、`NextDayzWebCore`、`src/Gameplay/Rig`、`src/Gameplay/Character`、`NextNet`、`JoltCharacter` | `gnb web build`、`gnb web test`、原生金标与规则探针；外加桌面闸门：`gnb build NextDayz gkNextUnitTests NextDayzBots`、`[NextDayz]` 单测、`gnb ds --bots 8 --minutes 3` |
| 只改 `web/nextdayz/` | `gnb web build`、`gnb web test` |
| 已发布地图、kit、角色 rig | `gnb web bake --rigs`，然后 `gnb web bake --rigs --check` |
| Master、信令、打包 | 在 `tools/gnb` 下运行 `go test ./internal/dzmaster ./internal/packager ./cmd/gnb` |
| 发布前 | 16 个 Web 机器人跑 30 分钟（80 ms 延迟、2% 丢包）、`w7*.mjs`，以及下文的服务器包浏览器冒烟 |

### CI

- **`NextDayz Web`**（`.github/workflows/nextdayz-web.yml`）：提交改到内核、WASM 入口、Web 工程或相关模块时运行，
  覆盖指向 `main` 的 PR，以及推到 `main`、`dev` 的提交。在 Linux 上跑原生金标与规则探针、`gnb web build`、
  `gnb web test`，以及 Master 和 gnb 的 Go 单测。
- **`Desktop CI`**：在 Windows、Linux 和 macOS arm64 上跑原生协议金标与规则探针。
- **`NextDayz packages and release`**：打 tag 或手动触发。烘焙、跑 `bake --check`、打包，然后把包解到仓库外，
  用 Chromium 跑浏览器冒烟。
- 还没进 CI：Web 机器人、混合客户端（剩余计划 W8-3）、rig 姿态探针。

## 部署

正式站点由 Master 同源托管。本机预览发布模式：

```sh
./gnb.sh web stage
./gnb.sh master serve --web <上一步打印的目录> --rtc-port-base 27200 --require-password
```

`gnb web stage` 重新核对烘焙资产，然后组装生产目录。`/api/web/manifest` 返回的构建标识和页面不一致时，
Master 拒绝出击并提示刷新。

对外部署使用 Linux 服务器包（`./gnb.sh package nextdayz-server`）。包里带着网页、WASM 和烘焙资产，
启动脚本默认开启口令和 WebRTC。HTTPS 反向代理、UDP 端口段和 `--public-host` 的配置见
[服务器包部署指南](nextdayz-server-deployment.md)。验证解包后的包：

```sh
python tools/nextdayz/smoke_server_package.py out/dist/nextdayz/NextDayz-server_linux64_<version>.tar.gz --launch-ds --web
```

## 常见问题

| 现象 | 处理 |
| --- | --- |
| `build NextDayz for the native header dependencies first` | 还没有 vcpkg 头文件。先构建任意一个桌面目标，或者设 `ND_CORE_HEADERS` 指向一个含这四个库的 include 目录 |
| `WASM dependency crosses the core boundary: …` | 内核间接包含了引擎、Gameplay 或模块的头文件。顺着报出的路径找到是哪个 include 引进来的，把需要的类型挪进纯数据头 |
| `WASM protocol bytes differ from protocol-golden.txt` | 协议字节变了。不是有意为之，就是有结构体布局或填充变了；有意的话，按上文“改协议”的步骤重新生成金标 |
| 出击时提示刷新页面 | 页面的构建标识和 Master 托管的不一致。重新构建，或刷新到 Master 托管的那一版 |
| 浏览器连不上 DS | Master 没配 `--rtc-port-base`，或者 UDP 端口段没有放通。直连调试时用 `gnb ds --web`，再用「开发直连」 |
