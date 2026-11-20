# NextDayz Web W2 烘焙验收

打开[本机验收页](http://127.0.0.1:4178/)，切换训练场 v3 和洛克特·河湾 v8。可以拖动镜头、查看核心区或出生点，勾选右侧类型在地图上显示对应锚点。统计栏中的三角形、批次和边界误差由浏览器解码 GLB 后核对。传输体积为整张地图的 HTTP gzip 大小，包含碰撞、地形和锚点。全图镜头加载所有渲染分块，游戏内的分块流送在 W4 接入。

## 启动

在仓库根目录执行。

```sh
./gnb.sh build NextDayzBake
./gnb.sh web bake
./gnb.sh web bake --check
npm --prefix web/nextdayz/w0 ci
node web/nextdayz/w2/server.mjs
```

`--map training_ground` 可只烘焙一张已发布地图。`--watch` 监视源文件及已求值的地形数据依赖，验收页会读取新清单并刷新正在浏览的地图。发布清单只引用完整写入的内容哈希目录。托管文件时按 `Accept-Encoding` 提供同名的 `.gz` 文件，使用原文件的 MIME 类型和 `Content-Encoding: gzip`。

## Rig 和共享资源

首次烘焙共享资源前，生成默认音效，并从桌面版离屏渲染或有效的桌面缓存导出物品图标。图标视觉源变更后重新执行桌面脚本。音效脚本需要 NumPy，或使用 `uv run --with numpy python` 运行。

```sh
python3 tools/audio/nextdayz_sfx.py
./gnb.sh build NextDayz
NEXTDAYZ_ICON_EXPORT="$PWD/out/web/nextdayz/icons-desktop" ./gnb.sh validate --script assets/agentscripts/nextdayz-web-icon-export.agentscript.json
./gnb.sh web bake --rigs
./gnb.sh web bake --rigs --check
```

`--rigs` 为七个目录分别生成 GLB 和骨骼/动画二进制，并发布 WebP 图标、WAV 和翻译表。地图、rig 和共享资源独立跟踪输入。W2 清单中的 `kernelBuild` 为 `null`，由 W3 的内核构建流程填入。

## 验证

```sh
ND_W2_CHROME=1 node web/nextdayz/w2/verify.mjs
```

脚本会核对浏览器解码的三角形、包围盒和渲染批次、HTTP 编码与体积，完整读取碰撞和地形二进制，并对照同一 loader 核对 rig 的骨骼、剪辑时长与 GLB 部件索引。截图和结果写入 `out/web/nextdayz/w2-evidence/`。

实际 DS 的全量物资核对使用 W1 的真实协议。先按 [W1 说明](../w1/README.md) 构建协议，用以下命令为两张图各启动一个 DS。

```sh
./gnb.sh ds --web --clients 0 --scene assets/scad/proc/nextdayz/training_ground/published/v3/scene.scad --port 27082 --rtc-port 28082 --signal-port 7782
./gnb.sh ds --web --clients 0 --scene assets/scad/proc/nextdayz/cz_loket_riverbend/published/v8/scene.scad --port 27083 --rtc-port 28083 --signal-port 7783
```

```sh
ND_W2_CHROME=1 ND_W1_OUTPUT="$PWD/out/web/nextdayz/w1" \
  ND_W2_DS_TRAINING=http://127.0.0.1:7782 ND_W2_DS_LOKET=http://127.0.0.1:7783 \
  node web/nextdayz/w2/verify.mjs
```

## 产物格式

所有二进制使用小端。向量按分量写出，四元数顺序为 XYZW，矩阵按列存储。计数和字符串字节长度为 uint32。节点引用是同一烘焙场景中的索引，用于关联碰撞和玩法节点。

- `NDC1` 碰撞文件先写 mesh 和实例数。每个 mesh 包含顶点数、索引数、两段压缩数据的字节数和对应 payload，分别用 meshopt 解码为 float32 XYZ 和 uint32 三角形索引。实例记录包含 mesh、节点、靶子引用（缺省为 `UINT32_MAX`）、参与标记（1 为 CPU 射线，2 为静态体，4 为运动体）、射线矩阵和物理 TRS。
- `NDT1` 地形文件先写网格数。每个网格包含 X/Y 单元格数、float64 X/Y 范围、uint64 seed、双向 float64 变换、float64 XYZ 顶点。逐单元格数据分别为对角线、地表分区、地表标记三段 uint8 数组，以及 float64 水位数组。
- `NDR1` rig 文件先写骨骼和 clip 数。骨骼记录包含 UTF-8 名称、int32 父索引以及 float32 绑定 TRS。Clip 记录包含名称、float32 时长、uint8 循环标志、通道数。每个通道记录包含 int32 骨骼索引，依次记录位置、旋转和缩放的关键帧数组。每个关键帧是 float32 时间和对应向量。GLB 部件的 `extras` 记录骨骼和部件索引，可换色材质使用 `material.extras.tintable`。
