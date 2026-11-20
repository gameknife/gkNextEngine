# NextDayz Web W0 验收原型

用浏览器打开 [本机验收页](http://127.0.0.1:4176/)。训练场加载后，点击画面，用 WASD 移动黄色胶囊，用 Q/E
转向。验证训练场大门的碰撞时，回到起点后按 D 约 1.6 秒，再按住 W 前进，胶囊会在大门旁的混凝土墙前停下。
可切换洛克特、俯瞰和复位。页面也可展开查看协议对照，或运行两种 WebRTC 回显实验。实验数据写到
`out/web/nextdayz/w0/`。

## macOS 复现

在仓库根目录执行。使用已有的 gnb/vcpkg 环境、Node 和 Emscripten SDK 4.0.23。
安装 Emscripten 的方式见 [emsdk](https://github.com/emscripten-core/emsdk)。

```sh
./gnb.sh build NextDayzBake NextDayz gkNextUnitTests NextDayzBots --reconfigure
out/build/macos-arm64/bin/gkNextUnitTests '[NextDayz]'
./gnb.sh ds --bots 8 --minutes 3

w0="$PWD/out/web/nextdayz/w0"
mkdir -p "$w0"
out/build/macos-arm64/bin/NextDayzBake \
  assets/scad/proc/nextdayz/training_ground/published/v3/scene.scad "$w0/maps/training_ground"
out/build/macos-arm64/bin/NextDayzBake \
  assets/scad/proc/nextdayz/cz_loket_riverbend/published/v8/scene.scad "$w0/maps/cz_loket_riverbend"

# 从已安装并激活 4.0.23 的 emsdk 读取环境。
source /path/to/emsdk/emsdk_env.sh
includes="$PWD/out/build/macos-arm64/vcpkg_installed/arm64-osx/include"
bash web/nextdayz/w0/protocol.sh "$includes"
# Apple Silicon 上通过 Rosetta 跑第二种桌面 ABI。
bash web/nextdayz/w0/protocol.sh "$includes" x86_64

# Jolt 5.4.0 的源码可从 vcpkg source 缓存或 JoltPhysics v5.4.0 官方源码获得。
emcmake cmake -S web/nextdayz/w0 -B "$w0/jolt-build" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DJOLT_SOURCE=/path/to/JoltPhysics
cmake --build "$w0/jolt-build" --target collision

# 使用仓库已有的 0.24.2 overlay 给独立探针安装 libdatachannel。
.vcpkg/vcpkg install 'libdatachannel[core]' nlohmann-json --classic --triplet arm64-osx \
  --overlay-ports="$PWD/cmake/vcpkg-overlays" --x-install-root="$w0/rtc-deps" --disable-metrics
cmake -S web/nextdayz/w0 -B "$w0/rtc-build" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$w0/rtc-deps/arm64-osx"
cmake --build "$w0/rtc-build" --target rtc-probe

npm --prefix web/nextdayz/w0 ci
ND_W0_STUN=stun:stun.l.google.com:19302 npm --prefix web/nextdayz/w0 run dev
```

在另一终端执行自动验证。设置 `ND_W0_CHROME=1` 使用本机 Chrome。开启 `ND_W0_CHECK_STUN=1` 会检查
answer 中的 STUN 映射候选。用 `ND_W0_BROWSERS=firefox` 单独跑 Firefox，用 `ND_W0_STUN` 给 Firefox 探针
提供客户端 STUN。Chrome 会保存两张地图的俯瞰截图和结果 JSON 到 `evidence/`。Safari 可直接用验收页按钮
测试。

```sh
npm --prefix web/nextdayz/w0 exec -- playwright install chromium firefox
ND_W0_CHROME=1 ND_W0_CHECK_STUN=1 npm --prefix web/nextdayz/w0 run check
```
