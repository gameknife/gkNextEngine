# NextRaiden 后续关卡资产储备

入口：`../../lib/kit_raiden_frontier.scad`，独立前缀 `rf_`。
共 36 个模块：沙漠能源、铁路工业、极地科研各 12 个。
这是静态布景库，不是第三关，也没有注册新的敌人、波次或可破坏目标。

## 预览

| 文件 | 内容 |
| --- | --- |
| `frontier_showcase.scad` | 36 个模块默认形态总览，按沙漠、铁路、极地从北到南排列 |
| `frontier_desert.scad` | 开采、输油、加工、太阳能和盐滩组合试摆 |
| `frontier_rail.scad` | 双线铁路、编组列车、货运与维修厂区试摆 |
| `frontier_polar.scad` | 科研站、冻土道路、雷达阵列和冰岸试摆 |
| `frontier_details.scad` | 抽油机、矿车、货车厢、线缆盘、补给站、雪车的三种形态对照 |

三个主题场景默认相机均为高度 75m、俯斜 15 度、FOV 50，另有全景和近景机位。
ScadLibrary 中在 `next_raiden` 场景分组打开；零件从 `kit_raiden_frontier` 选择。

```powershell
.\gnb.bat run ScadLibrary --scene assets/scad/source/next_raiden/frontier_showcase.scad
.\gnb.bat shot --scene assets/scad/source/next_raiden/frontier_polar.scad --frames 120
```

## 放置约定

- 米制、Z-up；入口和车辆车头统一朝 **-Y**，与 NextRaiden 现有资产一致。
- 所有非地砖模块底部 `z=0`，原尺寸最高不足 6m，保留 7m 飞行层的净空。
- 地砖均为 16x16m，基底 `z=-0.4..0`；车辙、轨道、路肩等有少量正向高度。
- 普通场地试摆设施用 `z=0.02`，地表石块、路肩不应置于设施脚下。
- 铁路沿 Y 接续，轨距 1.8m，轨面 `z=0.18`；列车和止挡整体放到 `z=0.18`。
- 双线铁路轴心为 `x=-3/+3`。机车中心到首节货车中心相隔 8.32m，货车中心间隔 7.92m，车钩恰好接触。
- 机车库自带基础平台和局部入库轨道，不是无缝铁路地砖；接轨时需另设计引线或转盘。
- 冻土道路中央路面 `z=0.06`，直行土路车辙顶 `z=0.075`，土路横向路口顶 `z=0.10`。
- 输油管中心高 1.25m，直管接口 `(0,-8)` / `(0,8)`，弯管 `(0,-8)` / `(8,0)`，三通合并三口；旋转和移动后可按 16m 网格连接。
- `variant=0/1/2` 是结构模式，越界整数会循环归一化。`seed` 决定斑块、杂物、岩体朝向或植被高度，不改变地砖的基底配色和边界。
- 每件静态模块内部 `gk_flatten()`；场景应继续由行或景观带管理生命周期，不能把整关合成一个巨型节点。
- 冰、玻璃、光伏板均为不透明材质，避免默认暗天空下透明介质发黑。没有纹理依赖。

## 沙漠能源

| 模块 | 0 / 1 / 2 模式 |
| --- | --- |
| `rf_ground_desert` | 风蚀砂地 / 碎石地 / 沙脊，另有 seed |
| `rf_ground_saltflat` | 盐滩斑块，仅 seed |
| `rf_ground_serviceway` | 直行土路 / 十字路 / 向 +X 的丁字路，另有 seed |
| `rf_bldg_refinery` | 双蒸馏柱 / 三柱 / 双柱加储罐 |
| `rf_bldg_coolingplant` | 单大冷却塔 / 双小塔 / 双塔加机房 |
| `rf_bldg_drillsite` | 钻井架和管材 / 增加储罐 / 增加设备柜 |
| `rf_prop_pumpjack` | 三种连杆与游梁静态姿态，不含动画 |
| `rf_prop_solararray` | 单排三板 / 双排六板 / 四板加逆变柜 |
| `rf_prop_pipeline` | 直管 / 直角弯管 / 三通 |
| `rf_bldg_desertcompound` | L 形院落 / U 形院落 / 院落加遮棚 |
| `rf_nature_mesa` | 层状台地 / 侧峰 / 高顶，另有 seed |
| `rf_veh_haultruck` | 空斗 / 矿石载荷 / 封盖货斗 |

## 铁路工业

| 模块 | 0 / 1 / 2 模式 |
| --- | --- |
| `rf_ground_rail` | 单线 / 双线 / 单线平交口，另有 seed |
| `rf_ground_industrial` | 维修地坪 / 装卸位线 / 通行边线，另有 seed |
| `rf_bldg_roundhouse` | 双开口扇形库 / 三开口 / 三闭门 |
| `rf_bldg_freightterminal` | 空装卸台 / 矮货垛 / 高货垛 |
| `rf_bldg_machinehall` | 单排气筒 / 双排气筒 / 高排气筒，均为锯齿屋顶 |
| `rf_bldg_cityblock` | L 形楼群 / U 形楼群 / 闭合院落 |
| `rf_prop_signalbridge` | 单线桥 / 双线桥 / 双线警戒信号 |
| `rf_prop_railbuffer` | 斜撑止挡 / 混凝土止挡 / 长缓冲头 |
| `rf_veh_locomotive` | 双风扇机车 / 低罩单风扇调车机 / 加长顶部设备罩 |
| `rf_veh_freightcar` | 棚车 / 载货平车 / 罐车 |
| `rf_prop_cableyard` | 双盘 / 四盘 / 四只横向大盘 |
| `rf_prop_scrapyard` | 三档整理区堆放数量，散件由 seed 决定 |

## 极地科研

| 模块 | 0 / 1 / 2 模式 |
| --- | --- |
| `rf_ground_snow` | 风积雪 / 双车辙 / 露岩，另有 seed |
| `rf_ground_ice` | 冰面裂纹，仅 seed |
| `rf_ground_frozenroad` | 净路面 / 中线 / 履带痕，另有 seed |
| `rf_bldg_polarstation` | 紧凑双舱 / 分离双舱 / 双舱加补给设备 |
| `rf_bldg_radome` | 单大罩 / 双小罩 / 双罩加设备柜 |
| `rf_prop_disharray` | 单大天线 / 双倾斜天线 / 双向天线 |
| `rf_bldg_generatorbank` | 双发电机 / 三机 / 三机加高排气管 |
| `rf_bldg_weatherlab` | 气象站 / 屋顶罩 / 外置仪器箱 |
| `rf_prop_supplycache` | 露天补给 / 有支柱的雪棚 / 三面挡风墙 |
| `rf_veh_snowcat` | 矮货舱 / 高舱 / 前推雪铲 |
| `rf_nature_iceberg` | 双峰 / 三峰 / 高冰脊，另有 seed |
| `rf_nature_snowpines` | 三株 / 四株 / 三株高树，另有 seed |

## 验收与接入

离线使用真实 SCAD 求值器检查了 328 组结构和种子组合，覆盖默认值、全部模式、
负种子及 65521 周期边界：无空网格、无警告、无落地件穿地，地砖边界保持 16x16m。
这些组合最高 5.98m、最大 908 三角面；总览、主题试摆和机械近景另做实际渲染检查。
几何检查报告位于本地 `out/build/windows/scad_validation_frontier/catalog.json`，不进入版本库。

新增内容已进入 `assets/scad/lib/catalog.json`。修改后刷新 catalog，再将两个库文件和
五个预览 SCAD 同步到 `out/build/windows/assets/` 对应位置，避免预览读取旧副本。

正式接入新关卡时，再编写景观带、关卡配置和波次。需要可破坏设施时应另定义命中、
分块与运行时注册约定，不能仅靠将静态模块改名为现有目标来获得完整玩法。
