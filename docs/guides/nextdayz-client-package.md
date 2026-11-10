# NextDayz 客户端包

客户端包供玩家直接解压运行，支持 Windows x64 与 Linux x86_64。包含 NextDayz、已发布地图及其
依赖、直升机大厅、人物/武器/服装、界面与字体、完整 NextDayz 音效、shader 和所需运行库。
Windows 包还带渲染器动态加载的 Streamline/DLSS 插件及许可证。包内不含 Master CLI、其他游戏、
源码、地图草稿、历史地图版本或调试符号。

## 打包

```powershell
.\gnb.bat package nextdayz-client --version dev-001 --master-url http://game.example.com:7780
```

```sh
./gnb.sh package nextdayz-client --version dev-001 --master-url http://game.example.com:7780
```

默认构建 `NextDayz`、`Packager` 后出包；已有构建可用 `--skip-build`。首次使用按仓库常规流程
执行 `gnb setup`，缺少公共 HDR 时执行 `gnb paks fetch optional`。`--output` 可指定输出目录。

默认输出 `out/dist/nextdayz/NextDayz-client_win64_<version>.zip` 或
`NextDayz-client_linux64_<version>.tar.gz`，以及对应 `.sha256` 文件。本地按宿主平台原生构建。
GitHub Actions 的 **NextDayz packages and release** 一次同时产出 Win64/Linux 的客户端与服务器四种包。
每个平台的 server/client 与校验文件放在同一份 Actions artifact 中；全部构建及验证成功后，统一发布
到同一个 GitHub Release，包含四个压缩包与四个 `.sha256` 文件。

可手动填写 `version`（同时用作 Release tag，例如 `nextdayz-v0.1.0`）和可选 `master_url`，
`publish_release` 默认开启；关闭后只保留 Actions 构建产物。也可推送 `nextdayz-*` tag 自动构建并发布，
如 `git tag nextdayz-v0.1.0` 后执行 `git push origin nextdayz-v0.1.0`；tag 名就是包版本和 Release tag。
旧的 `nextdayz-client-*` / `nextdayz-server-*` tag 仍会同时构建和发布两种包。

发布前会校验四种包齐全、SHA-256 和包内平台/版本一致。手动运行时，如果 Release tag 已存在，
必须在该 tag 指向的同一个 commit 上运行；否则请换新版本，避免旧 tag 对应新代码。
同一 tag、同一 commit 的重跑会更新同名构件。NextDayz Release 不设置为仓库的默认 Latest Release。

`--master-url` 只写入包里的服务器列表，不连接或部署该服务器。不传时保留源码中的默认服务器
列表，缺省为本地 `http://127.0.0.1:7780`。当前游戏仅接受 `http://host:port`，不接受 URL 路径、
查询参数、凭据或 HTTPS；这与现有 Master/客户端协议一致。

## 玩家运行

完整解压后，Windows 双击 `start-client.bat`（也可直接启动 `bin/NextDayz.exe`）；
Linux 执行 `./start-client.sh`，脚本会设置随包动态库路径。启动脚本也接受引擎原有命令行参数。
保留 `bin/` 与 `assets/` 的相邻关系。

启动后在游戏的联机菜单选择 Master、填写幸存者名称并登录，再选择地图派遣；也可进入离线训练。
发行者或玩家可编辑 `assets/configs/nextdayz_servers.json`，无需重新构建或修改 pak，例如：

```json
{
  "version": 1,
  "servers": [
    {"name": "Public Master", "url": "http://game.example.com:7780"}
  ]
}
```

服务器列表是唯一随包保留的可编辑客户端配置；个人设置、收藏服务器、离线存档、缓存与日志写到
系统每用户应用数据目录。分发包不会收集开发机的账号、存档或用户设置。

Windows 需可用的 Vulkan 显卡驱动；Linux CI 的 glibc 基线是 Ubuntu 24.04（2.39+），玩家需安装
Vulkan ICD/显卡驱动并在正常桌面中运行。Linux 包附带 ELF 动态依赖，系统提供 glibc 与驱动。
DLSS 功能取决于 GPU/驱动，资源随包提供不代表所有设备都支持。无需 Go、CMake、.NET 或源码。

服务器部署和端口配置见 [服务器部署说明](nextdayz-server-deployment.md)。

## 包验证

```sh
python tools/nextdayz/smoke_client_package.py out/dist/nextdayz/NextDayz-client_linux64_dev-001.tar.gz --launch --gnb ./gnb
```

脚本在仓库之外解压，检查压缩包与逐文件 SHA-256、启动脚本、可编辑服务器列表、客户端资源及
`NextDayz --help`。加 `--launch` 时使用源码构建出的 `gnb validate` 驱动**解压后的客户端**，
验证大厅 UI、进入离线训练场并截图，最后退出；隐藏窗口不抢焦点。证据目录包含报告、截图和日志。
Windows Actions runner 无 GPU，仅做包检查；Linux Lavapipe job 做实际客户端渲染和交互验证。
该确定性验证会禁用 Streamline，不能证明真实 DLSS 生效。
