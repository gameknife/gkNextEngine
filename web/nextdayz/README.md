# NextDayz Web 联机验收

打开 [W7 训练场验收页](http://127.0.0.1:4182/)，用任意新昵称进入大厅。选择「训练场」并点击「出击」。
第一次进入会下载并校验地图、碰撞和角色资源，随后通过 WebRTC 加入地图服务器。

在本机验收模式下，可用「前往靶场」「前往障碍场」「前往感染者围栏」跳到三个练习区。
靶场入口旁能拾取武器和弹药。围栏入口先到外围，等服务器生成感染者，再次点击可走到近期目标前方。
也可登录已经带枪的验收账号 `W7 Mixed Browser 1794996018121` 并恢复出击。

| 操作 | 默认键位 |
| --- | --- |
| 移动、慢走、冲刺 | WASD、X、Shift |
| 蹲伏、跳跃和翻越、切换视角、自由观察 | C、空格、V、Alt |
| 开火或近战、瞄准、换弹 | 左键、右键、R |
| 主武器、副武器、手枪、上一把武器 | 1、2、3、Q |
| 拾取、喝井水、开启或取消撤离 | E、E、G |
| 随身装备、行动地图、菜单 | Tab、M、Esc |

背包里可以拖动或选中物品再点击空格。按 R 或点击「旋转」改变放置方向，拖到对应装备槽穿戴，
拖到武器上安装配件。动作由内核校验，放置失败时物品留在原处。设置支持中英切换，鼠标灵敏度、音量、
键位和画质保存在当前浏览器。选中装备可以使用、拆分或丢弃。抵达行动地图上的绿色撤离区后按 G，
等待服务器的倒计时结束，物资会进入仓库。

## 部署与正式站点预览

Linux x86_64 服务器包自带 Web 客户端，解压后由包内 Master 托管。HTTPS、Vulkan 和 WebRTC UDP
配置见 [服务器包部署指南](../../docs/guides/nextdayz-server-deployment.md)。
在开发机完成烘焙和 `gnb web build` 后，可执行 `gnb web stage` 组装同样的生产站点目录，将打印的
路径传给 `gnb master serve --web <dir> --rtc-port-base 27200 --require-password`，访问 Master 的
监听地址即可验证发布模式。

## 启动本机环境

从仓库根目录串行构建。Jolt 使用本机构建缓存里的 5.4.0 源码，缺少缓存时 CMake 下载同版本并核对校验和。

```sh
./gnb.sh build NextDayz NextDayzBake
./gnb.sh web setup
./gnb.sh web icons
./gnb.sh web bake --rigs
./gnb.sh web build
```

在两个终端分别启动 Master 和开发服务器。验收快捷入口由 `VITE_ND_REVIEW` 启用。

```sh
./gnb.sh master serve --listen 127.0.0.1:7797 --port-base 27700 --rtc-port-base 28700 \
  --data out/web/nextdayz/w7-master --cheats
VITE_ND_REVIEW=1 ND_WEB_PORT=4182 ND_MASTER_URL=http://127.0.0.1:7797 ./gnb.sh web dev
```

桌面端用另一个昵称登录同一 Master。双方在同一张地图上靠近对方，就能看到姿态、服装和武器。

```sh
./gnb.sh run NextDayz -- \
  --cvar "nextdayz.net.mode client" \
  --cvar "nextdayz.net.master http://127.0.0.1:7797" \
  --cvar "nextdayz.net.name W7 Desktop" \
  --cvar "nextdayz.net.autoDeploy true" \
  --cvar "nextdayz.net.autoMap training_ground"
```

浏览器的「开发直连」走 `ND_DIRECT_URL`，默认信令地址为 `http://127.0.0.1:7781`。
`gnb web dev` 监视地图烘焙输入，并在清单更新后刷新浏览器。

## 自动验证

下列命令在原生版的障碍夹具上检查 WASM 的角色控制器。第一条命令使用 macOS 构建目录，其他平台可替换为对应的 preset。

```sh
./out/build/macos-arm64/bin/NextDayzBake map assets/agentscripts/fixtures/nextdayz-traversal.scad out/web/nextdayz/w5-evidence/traversal
node web/nextdayz/test/character.mjs
./gnb.sh web test
node web/nextdayz/test/w7.mjs
node web/nextdayz/test/w7-mixed.mjs
node web/nextdayz/test/w7-loot.mjs
node web/nextdayz/test/w7-respawn.mjs
```

W7 自动化使用真实地图服务器。界面脚本检查装备拖放、配件、武器状态和设置持久化，混合客户端脚本检查
友伤和库存纠正，另外两项检查抢物资、撤离入库和死亡后重新出击。截图与结果写入 `out/web/nextdayz/w7-evidence/`。
测试账号和地上物资会留在该服务器的数据目录中，重复验证拾取时可使用独立的 Master 数据目录和端口。
W5 的 `NextDayzRigPoseProbe` 可读取同一个 `.rig.bin` 并输出骨骼矩阵供逐元素对比。

W3 机器人的 30 分钟联机测试使用独立 Master。机器人用脚本路线验证联机、账本、身体状态和撤离，烘焙碰撞测试使用上面的障碍夹具。

```sh
./gnb.sh master serve --listen 127.0.0.1:7796 --port-base 27600 --rtc-port-base 28600 \
  --data out/web/nextdayz/w5-stress/master
./gnb.sh web test --bots 16 --seconds 1800 --latency 80 --loss 2 --master http://127.0.0.1:7796
```
