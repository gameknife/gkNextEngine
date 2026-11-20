# NextDayz Web W1 联机验收

本页连接真实的 NextDayz DS。昵称入服后，页面显示服务器分配的幸存者 ID、地图、持续增长的快照数与可靠通道
重组后的全量物资。另一个 ENet 幸存者开始上报状态后，其 ID 会出现在「同服幸存者 ID」。

## 启动验收

在仓库根目录执行以下命令。协议探针用与 W0 相同的 Emscripten SDK 4.0.23，编译同一份桌面协议源文件。
以下以 macOS arm64 为例。

```sh
./gnb.sh build NextDayz NextDayzBots gkNextUnitTests --reconfigure
source /path/to/emsdk/emsdk_env.sh
bash web/nextdayz/w1/build.sh "$PWD/out/build/macos-arm64/vcpkg_installed/arm64-osx/include"
./gnb.sh ds --web --clients 0 --port 27080 --rtc-port 28080
```

等 DS 就绪后，打开 [本机验收页](http://127.0.0.1:7781/) 并点击「加入服务器」。可在另一终端启动一个
ENet 机器人，或者让桌面客户端直连 UDP 27080。点击「离开」验证断开后可重新入服。

```sh
out/build/macos-arm64/bin/NextDayzBots --port 27080 --bots 1 --seconds 180
```

## 回归验证

```sh
out/build/macos-arm64/bin/gkNextUnitTests '[NextDayz],[NextNet]'
(cd tools/gnb && go test -race ./internal/dzmaster ./cmd/gnb)
./gnb.sh ds --bots 16 --web-bots 8 --minutes 30 --bot-latency 80 --bot-loss 2
```

`--bots` 是总数。`--web-bots` 是其中走 WebRTC 的数量，其余走 ENet。`--bot-latency` 与 `--bot-loss`
模拟机器人发送侧的延迟和不可靠消息丢失。

浏览器自动验证需要先启动上面的本机 DS 与 ENet 机器人。有 Chrome 时设置 `ND_W1_CHROME=1`，也可使用 W0
安装的 Playwright Chromium。Master 实测使用独立数据目录，在另一终端启动下列服务。

```sh
npm --prefix web/nextdayz/w0 ci
./gnb.sh master serve --listen 127.0.0.1:7780 --port-base 27160 --rtc-port-base 28160 \
  --data out/web/nextdayz/w1-master
```

```sh
ND_W1_CHROME=1 ND_W1_MASTER=http://127.0.0.1:7780 node web/nextdayz/w1/verify.mjs
```

脚本先通过直连页验证真实 DS 和 ENet 幸存者快照，再用 Master 派遣的票据转发一次 WebRTC offer。第二条
浏览器连接接收 `Welcome`、`CharacterSpawn`、快照和全量物资。截图和结果写入
`out/web/nextdayz/w1-evidence/`。
