---
title: "NextDayz 世界派遣与地理关卡"
category: design
status: 现行（三地图）
owner: NextDayz
created: 2026-11-05
last_updated: 2026-11-07
---

# NextDayz 世界派遣与地理关卡

**安全屋整备 → 接受目的地保密的回收委托 → 随机地区部署 → 搜刮／生存／撤离 → 返回安全屋查看世界足迹。**

撤离、仓库与出售机制见[持久世界与直升机撤离](nextdayz-extraction-design.md)。本层增加真实地理关卡、分图持久化、离线矢量世界地图，以及草稿试玩到正式化的制作流程。

## 当前地图与风格

| 地图 | 生成中心（WGS84 纬度，经度） | 内容与体验 |
| --- | --- | --- |
| 洛克特·河湾 / Loket — Riverbend | 捷克洛克特，`50.1860, 12.7540` | 奥赫热河曲流环抱岩岬上的老城：广场连续市民房立面、鼠疫柱与洋葱顶教堂，下城沿街台地，城堡守住北坡。北岗是围绕村绿和池塘的农庄村，东岸是温泉别墅与瓷器厂，东南是战后小住宅区，南部台地是集体农场、营地与条田；西北云杉林里藏着周末小屋，西部有小蠹虫枯死林。强调过河、林间潜行与分散搜刮。 |
| 小樽·北运河 / Otaru — North Canal | 日本北海道小樽，`43.2025, 141.0005` | 北运河、色内码头、仓储街区及邻近住宅。盐蚀石仓、锈损金属屋顶、废弃日式街屋和遗留货物；强调仓库穿行、沿岸暴露与集中补给。 |
| 青龙湖·荒径 / Qinglong Lake — Lost Greenway | 中国成都青龙湖湿地公园中心附近，`30.6346, 104.1868` | 湖汊、绿道、缓坡与林地提供骨架。废弃游客中心、灰瓦茶舍、旧村院落和公园养护场构成搜刮据点；阔叶树、垂柳与竹丛提供遮蔽，步桥和林间绕行形成不同风险路线。 |

三图均为 **1 km × 1 km**，各有五个安全落点、两处撤离区。真实地理只提供环境骨架；荒废状态、敌人与物资分布属于游戏虚构。地理参考：[捷克国家旅游局](https://www.visitczechia.com/en-us/things-to-do/places/landmarks/cities/t-loket)、[北海道官方旅游网站](https://www.visit-hokkaido.jp/en/spot/detail_10040.html)、[青龙湖公园地理参考](https://sichuan.scol.com.cn/ggxw/202204/58506692.html)、[OSM 公园边界](https://www.openstreetmap.org/relation/13882630)。

**生产原则：地理提供骨架，地域知识决定长相，Coldwar 玩法决定节奏。** 高程、河流、主路和建成区来自真实地点；每个建成区按当地真实聚落形态（村绿村、广场、街道、独立农庄、周末小屋区、集体农场……）展开，建筑用该地域专属 kit，植被按当地真实的森林／田地／河岸格局绘制土地覆盖。建筑数量、入口、物资、感染者和风险收益由游戏设计决定；允许压缩高差、简化道路、移动据点，以保证可玩性。

洛克特 v4 使用 **15 个据点、108 栋建筑、5 个安全落点、2 个撤离区**：老城广场（22，含市政厅与教堂）、下城街（17）、城堡（3）、北岗村绿村（13，含小教堂、消防站、酒馆）、温泉别墅（6）、瓷器厂（4）、东南小住宅区（10）、两处独立农庄（3+3）、集体农场 JZD（6）、周末小屋区（8），以及桥头关卡、卫生站、林中电台和南部营地。19 块土地覆盖（云杉林、混交林、山毛榉林、松林、小蠹虫枯死林、三条河岸带、条田、草甸、果园）和 7 条行道树／树篱。OSM 原有多少栋建筑，不改变这份预算。v3 的散点场院布局保留在冻结快照中。

小樽 v4 使用 **14 个据点、120 栋建筑、5 个安全落点、2 个撤离区**：三片木板住宅街区（16+16+14）、三条商街（16+12+12）、沿运河单侧排列的石仓（6）、北码头货运场（6）、南部修理场（4）、航运办公区（4）、诊所（4）、派出所（4）、检疫指挥点（4）和坡地神社（2）。北海道专属的 `hokkaido_lane`／`shotengai`／`canal`／`portyard`／`shrine` 形态按地块找平，台地间保留花园和横巷；12 块土地覆盖和 5 条行道树绘制西北栎榆白桦混交林、针叶林口袋、后院、公园和滨岸荒草，港池及装卸通道保持开敞。五个原 OSM 跨水桥口保留，北侧落点移出新办公区的警戒范围。详见[小樽地域调研与关卡规划](otaru-v4-authoring.md)。v3 的四排大场院布局保留在冻结快照中。

青龙湖 v1 使用 **12 个据点、48 栋建筑、5 个安全落点、2 个撤离区**；地形保留 85% 高差。OSM 只记录了本块少量建筑，游戏里的茶舍、游客中心、村居、医疗和补给布置由预算与 PCG 控制点决定。保留一座 OSM 过水桥，并用两个 PCG 桥梁控制点补充战术连通；不要求复刻现实的公园设施清单。植被使用四川园林主题的阔叶树、垂柳和竹丛。

地域外壳按地域分 kit：波希米亚走 `kit_bohemia.scad`（`bh_`：民间巴洛克、埃格兰半木构、连续立面市民房、洋葱顶教堂、JZD 与城堡，以及中欧小品和林木）；北海道走 `kit_hokkaido.scad`（`hk_`：木骨石仓、金属顶木板住宅、两层店铺、石造航运办公室、平顶公寓和诊所、派出所、神社、鸟居、石灯笼、电杆、自动售货机与栎／榆／白桦／针叶树），全部北海道建筑优先分派到本地外壳；四川仍走 `kit_abandoned.scad`（`az_`）。`nextdayz_settlement.scad` 与 `nextdayz_hokkaido.scad` 把聚落据点展开成地块、街道和小品；`nextdayz_coldwar.scad` 提供家具和分类物资组合（按占地缩放的 `nd_shop`／`nd_pub`／`nd_office`／`nd_depot`／`nd_chapel`），`nextdayz_pcg.scad` 装配并散布土地覆盖。几何独立合并，物资／感染者语义锚点与水泵（水源锚点）留在合并之外；建模不以压低三角数量为目标。

## 派遣、安全屋与存档

- 从 `assets/configs/nextdayz_maps.json` 中的 `published` 地图等概率随机派遣，再随机选该图的安全落点。世界地图用于浏览，不指定下次任务地点。
- 安全屋分为世界／任务页与仓库／携装页。世界地图用 Natural Earth 陆地矢量直接绘制，支持缩放、拖动、点击地点，显示简介和各图出击次数；完全离线。
- 临时测试入口：在左侧列表或世界地图选中地区后，点击 `ENTER SELECTED MAP (TEST)` 直接部署到该图。沿用正式出击、携装与存档规则；原有 `ACCEPT & DEPLOY` 继续随机派遣。
- 成功进入地图才计一次新出击，死亡也计入；加载失败、续接、热重建和重复结算不重复累计。返程显示地区和结果。
- 仓库与黄金共享；物资、掉落、补货计时和世界时间按稳定 `mapId` 保存。只推进正在游玩的地图，局外、离线和其他地图暂停。死亡装备留在原图，之后再次派到那里才能回收。
- 存档 v2 在同一快照里保存各图状态、地图版本、当前地图、玩家、航班与唯一出击序号；兼容原单图 v1 存档。已发布地区升级时，保留迁移前快照及 `mapId@v旧版本` 世界记录，将玩家带回安全屋；携装、仓库、金币、历史次数与其他地区保留，升级地区使用新的物资／地形布局。拒绝向旧版本降级。
- 普通启动优先进入已发布地图。旧 Riverland 存档自动迁入世界安全屋，保留随身装备、仓库、金币与历史次数，不新增撤离记录；旧世界状态保存在存档中，迁移前的完整快照留在 `save.json.riverland-backup`。显式加载 Riverland 的开发测试仍可使用旧场景。

## 生成与人工调整

复用 [gnb geo](../../designs/geo-city-generation-design.md) 的 SRTM、OSM、缓存、投影、高程处理与道路网络。新增 `geo expedition` 导出游戏关卡，原始数据和归一化 IR 仍留在 `external/geocache/`。

```powershell
# 首次抓取并处理地理骨架；换图时替换名称和中心坐标。
gnb.bat geo fetch --name cz_loket_riverbend --at 50.1860,12.7540 --size 1000 --profile europe
gnb.bat geo build --name cz_loket_riverbend --at 50.1860,12.7540 --size 1000 --profile europe

# 从缓存离线重生成游戏关卡。
gnb.bat geo expedition --recipe assets/scad/specs/nextdayz/cz_loket_riverbend.json
gnb.bat geo expedition --recipe assets/scad/specs/nextdayz/jp_otaru_north_canal.json
gnb.bat geo expedition --recipe assets/scad/specs/nextdayz/cn_chengdu_qinglong_lake.json
```

配方在 `assets/scad/specs/nextdayz/`，声明地域、种子、发布版本和 `gameplay`：控制点模板路径、建筑预算、地形高差比例、参考道路数量；可选 `roadClasses` 选择参考路类，可选 `palette` 覆盖地域默认地表色板（波希米亚默认 `continental`，其余 `temperate`）。公园应纳入 `cycleway`／`footway`／`path`；城镇不要带 `service`／`footway`，否则拆掉建筑后只剩满地土路斑块。洛克特使用 `reliefScale=0.4`、`roadLimit=32` 和主次干路路类。含聚落据点的地图地形网格自动用 256×256（3.9 m 格）。

### 新地点初始地图的强制规则

逐步流程见 skill [`nextdayz-geomap`](../../../.agents/skills/nextdayz-geomap/SKILL.md)。下表是验收底线；洛克特 v3 正是逐条违反了"地域 kit／聚落形态／土地覆盖"三行才显得千篇一律。

| 层 | 制作规则 | 验收重点 |
| --- | --- | --- |
| 地域调研 | 动手前写下本地域的建筑家族（屋顶形式与材料、山墙处理、墙面色系、地标类型）、聚落形态（每个真实建成区属于哪种）和土地覆盖（优势树种与林相、干扰如小蠹虫、田地制度、河岸带、行道树）。 | 调研结论要能在截图里逐条找到对应。 |
| 地理骨架 | DEM 注入 TERR；按地点选 `roadClasses`，只保留主次道路映射为 `road` 算子。建筑轮廓聚合为街区覆盖率，用于判断建成区位置与形态。 | 普通道路没有实体路板；跨水保留桥面，不能填坝；没有拆房后残留的土路斑块。 |
| 聚落规划 | 每个真实建成区一个聚落据点，选用匹配的形态（`naves` 村绿村／`square` 广场／`street` 街道／`suburb` 小住宅／`villas` 别墅／`homestead` 独立农庄／`chaty` 周末小屋／`jzd` 集体农场／`factory` 工厂／`hillcastle` 城堡；北海道另用 `hokkaido_lane` 住宅巷／`shotengai` 商街／`canal` 单侧石仓／`portyard` 货运场／`shrine` 神社），旋转对齐真实街道或村绿轴线。`military`／`checkpoint`／`medical` 等旧类型只用于游戏性营地。 | 鸟瞰：建筑沿街、沿村绿排布，有前院、后院、花园间隔与地标（教堂、小教堂、池塘、纪念碑、消防站）；不得出现"灰色大方块上两排房子"。 |
| 地块找平 | 聚落按地块找平：每个地块一个带地表与缓坡参数的 pad（plot），在 3.9 m 网格上平到边缘、且不被后续地块改动；不同高度的地块之间至少留 12 m（`nd_gap`），放花园、小路或村绿。单块 pad 只用于广场、JZD、工厂和城堡。 | 全占地和入口采样高度一致（`[Landscape]` 0.16 m 内）；禁止浮空、地形穿地板、入口高台；台地之间有可走的过渡。 |
| 建筑与物资 | 每个地域有自己的 kit，覆盖聚落形态会生成的全部建筑种类，并在 `nd_pcg_building` 里按地域分派；不得借用别的地域外壳。室内用按占地缩放的家具组合与类型化 loot。 | 街景能认出地域：屋顶、山墙、色彩、门窗；物资落在家具或可搜寻地面，门洞与通道不被装饰横穿。 |
| 土地覆盖与植被 | 用 `PCG_LANDCOVER` 多边形画出森林、条田、草甸、果园和河岸带（河岸带只在水边 15 m 内长树）；`PCG_ALLEYS` 画行道树与树篱；`PCG_VEGETATION` 只留稀疏荒草灌木。地表颜色来自土地覆盖的 `paint`，不靠海拔。 | 总览能分出深色林地、带条纹的田、绿色草甸、沿河树带；没有全图均匀撒树。自动排除水域、道路、地块、出生点、撤离区和视线走廊。 |
| 评审截图 | `geomap_preview.py` 出规划图；`geomap_shots.py` 出总览、每个聚落的鸟瞰与街景，逐张对照上面各行的验收重点迭代。 | 草稿解析时间保持在 10 s 内（运行日志 `parsed scene ... on cpu`）。 |
| 试玩发布 | 重生成保留人工控制点，完成鸟瞰、地面进屋、跨桥及撤离流程检查后冻结新版本。 | kit 无新警告，正式快照包含全部 PCG 控制点与库；不可覆盖已发布版本。 |

Go 验证建筑预算、各聚落形态的最小尺寸、水域、过大挖填（单块 12 m、分地块 20 m）、据点重叠、安全落点、土地覆盖与行道树参数和重生成保留。C++ `[NextDayz][Landscape]` 求值实际 SCAD，检查洛克特 108 栋、小樽 120 栋、青龙湖 48 栋建筑的全占地／入口贴地、多种建筑与 loot 类型、植被避让及道路语义；输入脚本另检查实际进屋、跨桥。自动检查不能替代风险收益和节奏的实玩调优。

### 可编辑层

每图位于 `assets/scad/proc/nextdayz/<mapId>/`：

| 文件 | 职责 |
| --- | --- |
| `terrain.hmap`、`base.scad` | 重新生成的高程／道路骨架与 PCG 调用入口 |
| `reference.json` | OSM 建成区覆盖率；仅参考，不是生成清单 |
| `layout.scad` | **关卡设计主入口**：据点、路线、植被、净空、安全落点和撤离区 |
| `authoring.scad`、`adjustments.scad` | 保留的手工参数、特殊掩体与例外布局；可用 `nd_ground(x,y)` 贴地 |
| `scene.scad` | 先加载控制点，再组合基础层与人工增补 |
| `generation.json`、`ATTRIBUTION.md` | 玩法预算、地理比例、生成报告与署名 |

`layout.scad` 首次从配方模板复制；**之后重生成保留现有文件**。修改据点以地图目录下的文件为准，模板用于新建地图。控制数组使用 JSON 兼容的 SCAD 字面量，方便生成前验证：

```scad
// [id,name,type,center,padSize,rotation,buildingCount,risk(1..3)]
PCG_SITES = [["village","Old quarter","village",[50,40],[100,68],0,8,1]];
// 聚落：kind 为 naves/square/street/suburb/villas/homestead/chaty/jzd/factory/hillcastle
// 或 hokkaido_lane/shotengai/canal/portyard/shrine 时
// 由 nextdayz_settlement.scad 展开成地块；size 至少满足 gnb 报出的最小尺寸。
// 其他控制层：PCG_PATHS / PCG_SPAWNS / PCG_EXTRACTIONS /
// PCG_VEGETATION / PCG_CLEAR。
// 可选 PCG_BRIDGES = [[[岸点Ax,岸点Ay],[岸点Bx,岸点By],桥宽], ...];
// 可选 PCG_LANDCOVER = [[id,"spruce|mixed|beech|pine|beetle|riparian|orchard|field|meadow|clearing|hokkaido_mixed|coastal_grass",[[x,y],...]], ...];
// 可选 PCG_ALLEYS = [[[[x,y],...],"fruit|linden|hedge|...",间距,路肩偏移], ...];
```

调整建筑数量时同步更新配方 `buildingBudget`；更复杂的自定义组合可放在 `adjustments.scad`，保持 kit 底面 z=0、语义锚点在 `gk_flatten` 外的契约。场景热重载会重新求值 PCG、地形和植被排除规则。可选 `PCG_BRIDGES` 要求桥宽 3–12 m、跨度至少 12 m，两端落在地图内的干岸上；桥面连接最终 TERR 高度，桥下保留水域，`PCG_CLEAR` 为桥头留出植被净空。

## 在游戏中试玩与正式化

正常启动 NextDayz 后，用反引号打开控制台：

```text
set nextdayz.authoring true
set nextdayz.authoring.map cz_loket_riverbend
```

切换编辑模式会保存并离开原进度，使用独立的 `authoring/<mapId>.json` 测试存档。局外可点击 `PLAYTEST BROWSED MAP` 试玩草稿。编辑当前地图及其 SCAD 依赖后，连续两个稳定轮询通过语法预检，再镜像到运行目录并重建；保留玩家、物资和航班状态。编辑造成原位置被挡时尝试移到安全落点。语法错误保留当前可玩的场景；用 `set nextdayz.authoring false` 返回正式存档。

完成通路、入口、桥梁、物资／感染者分布和撤离路线验收后：

```powershell
gnb.bat geo expedition --recipe assets/scad/specs/nextdayz/cn_chengdu_qinglong_lake.json --publish

# 将新场景、地图注册表与依赖同步到运行目录。
gnb.bat build NextDayz
```

正式化把场景、人工调整、高度场和 kit 依赖复制到 `published/v<版本>/`，将该入口登记为 `published`，同时保留 `draftScene`。同版本拒绝覆盖；草稿及共享 kit 后续修改不会影响正式关卡。成品随游戏资产分发，玩家机器不需要开发缓存或在线地理服务。

洛克特 `v4`、小樽 `v4` 及青龙湖 `v1` 都使用以上玩法优先流程，旧版保留在各自冻结快照中。洛克特 v4 首个采用聚落形态、地域 kit 与土地覆盖；小樽 v4 将同一流程用于北海道木板街区、金属屋顶、石仓、航运办公楼与港岸荒草。青龙湖偏湖岸步道、茶舍院落与密林遮蔽，后续重做时补齐四川聚落形态与独立 kit。共享 kit 的后续改动不会影响已冻结地图。

## 验证入口与边界

```powershell
gnb.bat build NextDayz gkNextUnitTests
out/build/windows/bin/gkNextUnitTests.exe "[NextDayz]"
out/build/windows/bin/gkNextUnitTests.exe "[NextDayz][Landscape]"
python tools/nextdayz/check_legacy_startup.py
python tools/nextdayz/check_region_upgrade.py
python tools/nextdayz/check_region_upgrade.py --map jp_otaru_north_canal
gnb.bat validate --script assets/agentscripts/nextdayz-world-maps.agentscript.json
gnb.bat validate --script assets/agentscripts/nextdayz-world-resume.agentscript.json
gnb.bat validate --script assets/agentscripts/nextdayz-published-dispatch.agentscript.json
gnb.bat validate --script assets/agentscripts/nextdayz-extraction.agentscript.json
gnb.bat validate --script assets/agentscripts/nextdayz-otaru-rescue.agentscript.json
gnb.bat validate --script assets/agentscripts/nextdayz-loket-entrance.agentscript.json
gnb.bat validate --script assets/agentscripts/nextdayz-loket-bridge.agentscript.json
gnb.bat validate --script assets/agentscripts/nextdayz-otaru-bridge.agentscript.json
gnb.bat validate --script assets/agentscripts/nextdayz-otaru-entrances.agentscript.json
gnb.bat validate --script assets/agentscripts/nextdayz-otaru-streets.agentscript.json
gnb.bat validate --script assets/agentscripts/nextdayz-qinglong-bridges.agentscript.json
gnb.bat validate --script assets/agentscripts/nextdayz-qinglong-entrances.agentscript.json
gnb.bat validate --script assets/agentscripts/nextdayz-qinglong-rescue.agentscript.json
```

前两份脚本顺序执行，分别覆盖多图、场景重建、撤离／死亡与跨进程恢复；`published-dispatch` 检查冻结场景的普通随机派遣，`extraction` 保留原撤离循环回归。桥梁脚本使用实际移动输入检查跨水通路；`qinglong-rescue` 覆盖第三张图的派遣、实际按键登机、撤离和读档，并确认旧地区记录与仓库保留。隔离测试不改正式存档；首份与正式派遣脚本会重建各自的测试进度，可重复执行。地形全景检查使用 `nextdayz-geography-overview.agentscript.json`，可通过 `--scene` 覆盖地图。

规划与评审工具：`tools/nextdayz/geomap_preview.py`（俯视规划图：地形、坡度、水、OSM 与全部控制层）、`tools/nextdayz/geomap_shots.py`（直接渲染源码草稿的总览／鸟瞰／街景截图）。世界地图矢量数据更新工具为 `tools/nextdayz/fetch_world_atlas.py`。Natural Earth 为公有领域；OSM 与 SRTM 署名随关卡保留。

当前边界：地形继承源高程误差，局部需要人工调整。首版提供可游玩的物资／威胁布局，建筑预算、据点节奏和风险收益仍需持续实玩调优；不包含全球流式地图、联机和复杂任务链。
