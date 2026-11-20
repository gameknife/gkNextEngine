# NextDayz 最小服务器包

服务器包包含 Master（`bin/gnb`）、DS（`bin/NextDayz`）、实际链接的运行库、已发布地图与角色的
递归依赖、启动脚本、许可证、文件清单和 SHA-256 校验值。Linux 包还包含生产版 Web 客户端、WASM 和已发布地图的烘焙资产。
远端无需源码、Git、Go、Node.js、CMake 或 vcpkg。

DS 仍使用引擎的 Vulkan 场景/GPU 资源。`--dedicated` 不绘制、不播放声音，**尚不是纯 CPU 服务器**。
引擎初始化仍需要 shader、字体和公共 HDR，因此也保留在 `runtime.pak` 中。

## 本地打包

Windows x64：

```powershell
.\gnb.bat package nextdayz-server --version dev-001
```

Linux x86_64：

```sh
./gnb.sh package nextdayz-server --version dev-001
```

默认定向构建 `NextDayz`、`Packager` 并关闭 Tracy。Linux 还构建 `NextDayzBake`、烘焙地图、构建
WASM 和网页，再把这一整套产物打包。初次打包还会用 Vulkan 导出物品图标。构建机需 Go 和 Node.js 24，首次先执行 `gnb setup`。
缺少公共 HDR 时执行 `gnb paks fetch optional`。已有完整产物时可加 `--skip-build`；此时 Linux 打包
仍会校验地图资产清单及 WASM 构建标识。输出目录可用 `--output <dir>` 指定。

输出到 `out/dist/nextdayz/`：

- `NextDayz-server_win64_<version>.zip` 与 `.zip.sha256`。
- `NextDayz-server_linux64_<version>.tar.gz` 与 `.tar.gz.sha256`。

本地按宿主平台原生构建，不做 C++ 跨平台交叉编译。一次同时产出两个平台，请运行 GitHub Actions
中的 **NextDayz packages and release**，填写 `version`（同时用作 Release tag，例如 `nextdayz-v0.1.0`）。
Windows/Linux matrix 并行构建，每个平台的客户端、服务器与校验文件一起上传为 Actions artifact。
两个平台全部构建及验证成功后，会统一向同一个 GitHub Release 发布四个压缩包和四个 `.sha256` 文件。

手动触发时 `publish_release` 默认开启，关闭则只保存 Actions 构建产物。推送 `nextdayz-*` tag
会自动构建并发布，tag 名同时作为包版本与 Release tag；旧的 server/client 前缀同样受支持。
发布前检查完整性、校验和、平台与版本；现有 tag 必须与构建 commit 一致，同 commit 重跑更新同名构件。
此流程不部署远端服务器。
客户端命令、预置公网 Master 地址和玩家启动方式见 [客户端分发说明](nextdayz-client-package.md)。

## 运行条件

- Windows 10/11 或 Windows Server x64：安装支持引擎所需 Vulkan 功能的显卡驱动。
  Vulkan ICD/驱动由系统提供（构建目录中的 Vulkan loader 会随包复制）；包内收集厂商 DLL
  实际依赖的 Visual C++ runtime，无需开发机 MSVC 环境。
- Linux x86_64：CI 使用 Ubuntu 24.04，分发基线为 glibc 2.39+。其他本机构建以构建机 glibc 为基线。
  动态库的实际依赖闭包随包收集，glibc/动态加载器由操作系统提供，Vulkan ICD/驱动由服务器提供。
  原生 GPU 驱动或 Lavapipe 软件 Vulkan 均需满足引擎功能要求；无独显不代表无需 Vulkan。
- Linux 默认 `--headless`，通过 `VK_EXT_headless_surface` 启动，无需 X11/Wayland。
  CI 参照主发布流程安装更新的 Mesa/Lavapipe 并实际验证所有地图 DS 注册；旧发行版自带 Mesa
  可能不支持此路径。若驱动不支持 headless surface，可在 Xvfb/正常桌面中加 `--headless=false`。

例如 Ubuntu 的基础软件 Vulkan 环境为 `libvulkan1 mesa-vulkan-drivers vulkan-tools`；
请用 `vulkaninfo` 检查设备和 `VK_EXT_headless_surface`。需要更新 Mesa 时参考
`.github/workflows/nextdayz-server.yml` 的同名步骤。软件 Vulkan 会增加 CPU/内存开销。

## Linux 浏览器联机部署

先校验压缩包，完整解压，保持 `bin/` 与 `assets/` 相邻。无需进入特定工作目录。

在 Ubuntu 24.04 x86_64 主机准备上一节的 Vulkan 环境，并安装 Caddy 或配置现有 HTTPS 代理。
将域名指向服务器的公网 IP，开放 TCP 80/443 和 UDP 27200–27203。

```sh
sha256sum -c NextDayz-server_linux64_dev-001.tar.gz.sha256
mkdir -p /srv/nextdayz
tar -xzf NextDayz-server_linux64_dev-001.tar.gz -C /srv/nextdayz
/srv/nextdayz/start-master.sh --public-host game.example.com --max-servers 4 --trust-local-proxy
```

启动脚本默认监听本机 TCP 7780，自动托管包内 `web/`，启用账号口令和 WebRTC UDP 27200 起始端口。
将包内 `Caddyfile.nextdayz` 的 `game.example.com` 换成实际域名，合并到 Caddy 的配置并重载。示例
配置仅转发网页、资源和客户端 API，并覆盖 `X-Real-IP`，以便 Master 按玩家 IP 限速。使用其他
同机代理时也要覆盖该头，或去掉 `--trust-local-proxy`。保持 TCP 7780 仅本机可达。

在浏览器打开 `https://game.example.com/`，输入昵称和账号口令，选地图并出击。这个 Master 上的
新昵称首次登录设置口令。原有无口令账号会在首次设置时认领，旧登录凭据随即失效。正式迁移前应
先让旧玩家在可信环境完成认领，再对外开放入口。开发机可通过 SSH 转发 TCP 7780 查看网页，本机浏览器
要实际连入远程地图时仍须能直达 DS 的 WebRTC UDP 端口。

`--public-host` 填客户端能直达的 IP 或 DNS 名，WebRTC 媒体流量直接连接 DS。
有 NAT 时把 WebRTC UDP 段原样转发到主机。可用 `--rtc-port-base` 调整起始端口，范围是
`rtc-port-base .. rtc-port-base + max-servers - 1`。如同时提供桌面联机，还需开放 ENet UDP
27100–27103（同样随 `--port-base` 和 `--max-servers` 变化），桌面客户端也使用 HTTPS Master 地址。

本机部署可以直接打开 `http://127.0.0.1:7780/`。其他机器访问时用 HTTPS，浏览器需要安全上下文
才能校验烘焙资源。受限网络若封锁 UDP，当前 WebRTC 连接可能无法建立。

## Windows 桌面联机部署

```powershell
Get-FileHash .\NextDayz-server_win64_dev-001.zip -Algorithm SHA256
Expand-Archive .\NextDayz-server_win64_dev-001.zip C:\NextDayz
C:\NextDayz\start-master.bat --listen 0.0.0.0:7780 --public-host game.example.com --max-servers 4
```

`--public-host` 填客户端可达的公网 IP 或域名，不能填 `0.0.0.0`。Master 与 DS 位于同机，
DS 回连 Master 使用本机地址，客户端收到的 DS 地址使用 `--public-host`。
默认仅监听 `127.0.0.1:7780`。

防火墙/云安全组开放 TCP 7780，以及 UDP `port-base .. port-base + max-servers - 1`。
例如 `--port-base 27100 --max-servers 4` 对应 UDP 27100–27103；默认上限为 32 台 DS。
有 NAT 时须映射同样的端口。客户端在服务器列表中配置 `http://game.example.com:7780`，
登录并派遣时 Master 自动选择地图 DS；容量不足时拒绝分配，空服默认 300 秒后回收。
`--max-players` 默认 16，`--pvp` 开启友伤；生产运行不要传 `--cheats`。

## 管理、持久化与更新

包内 `bin/gnb master` 不依赖源码仓库，会从自身所在目录识别包根目录。
也可显式传 `master serve --runtime-root <package-root>`。

```sh
./bin/gnb master status
./bin/gnb master stop ds1
./bin/gnb master pvp ds1 --on
./bin/gnb master kick 123
```

管理 API `/admin/*` 只接受本机 loopback 请求，远程管理请通过 SSH 在服务器上执行。
不要将公网反向代理直接转发到全部 Master 路径：代理只发布客户端需要的 `/api/*`，
不要转发 `/admin/*`；DS 的 `/ds/*` 留给同机进程。
`--require-password` 可以启用带口令的登录；公网使用 HTTPS 代理终结 TLS。

默认持久化到包根目录 `data/`：`master.json` 保存账户/仓库/世界检查点，`logs/` 保存 DS 日志。
可用 `--data /srv/nextdayz-data` 将状态移到独立目录。引擎自身日志/缓存仍在系统的每用户应用数据目录。
更新前停止 Master 并备份持久化目录；替换程序和资源时保留 `data/`。
Ctrl+C 或 Linux SIGTERM 会请求 DS 保存状态并退出；systemd 请设置 `TimeoutStopSec=45` 或更长。

Linux systemd 示例（按实际目录和非 root 服务用户调整）：

```ini
[Unit]
Description=NextDayz Master and DS fleet
After=network-online.target
Wants=network-online.target

[Service]
Type=simple
User=nextdayz
WorkingDirectory=/srv/nextdayz
ExecStart=/srv/nextdayz/start-master.sh --public-host game.example.com --max-servers 4 --trust-local-proxy --data /srv/nextdayz-data
Restart=on-failure
KillMode=mixed
TimeoutStopSec=45

[Install]
WantedBy=multi-user.target
```

`nextdayz` 用户需对数据目录和自己的缓存目录有写权限，并可访问 GPU（使用硬件 Vulkan 时）。
Windows 可以先用前台脚本运行，通过 Ctrl+C 停机；脚本本身不注册 Windows Service。

## 验收

```sh
python tools/nextdayz/smoke_server_package.py out/dist/nextdayz/NextDayz-server_linux64_dev-001.tar.gz --launch-ds --web
```

脚本校验 SHA-256 和文件清单，在**仓库之外**解压并启动包内 Master，验证登录、地图列表、默认
数据目录；`--launch-ds` 逐张派遣全部已发布地图，等待真实 DS 心跳进入 `ready`，验证管理员停服及
Master 收到停机信号后回收最后一台 DS；随后重启 Master 验证账号持久化。
证据目录打印为 `nextdayz-server-smoke-*`，包含 Master 和 DS 日志；不使用桌面截图或假 DS 代替注册验证。
`--web` 还会从解压后的包打开正式页面，用真实 Chromium 验证账号口令、旧版本拒绝出击、WebRTC
进服、拾取、返回大厅后回归、撤离和仓库持久化。执行该测试前，在 `web/nextdayz` 运行
`npm ci` 和 `npx playwright install --with-deps chromium`。Windows CI 验证 Master 与包结构，Linux
Lavapipe CI 执行地图与浏览器测试。验收公网部署时，用另一台机器打开 HTTPS 域名并实际出击。
