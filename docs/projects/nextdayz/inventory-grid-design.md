# NextDayz 网格背包

这是当前规则，物品尺寸以 `src/Application/Game/NextDayz/Data/ItemDefs.cpp` 为准。

## 放置规则

- 每个物品栈占一个矩形；堆叠数量改变时矩形不变，拆分必须另找位置。
- 容器有固定宽、高；空格总数足够也可能因碎片化装不下。
- 拖放到网格会按鼠标选中的格子摆放；绿色为可放置，红色为重叠、越界或类型不符。
- 拖动时按 **R** 旋转 90°，右键菜单也可原地旋转；非法旋转或放置保持原状。
- 背包内及地面同类物品均可拖到已有物品上合并；数量不超过该物品的 `maxStack`，超出的部分留在原位置或地上。满栈显示红色，不接受合并。
- 右键“合并同类物品”会将其他同类物品补入选中的栈，满栈后停止；合并完的空栈会移除，释放占格。
- 拾取按钮、E 和拖放到容器标题会优先补齐已有同类栈，再为余量寻找空位：优先物品定义的方向，再尝试旋转，不会自动移动已有物品。
- 网格内的地面物品拖放只拾取该物资堆第一种物品的一栈，剩余物品留在地上。
- 衣物穿戴不占格；脱下衣物或背包时，其内容先转移到其他容器，任何一步放不下则整笔事务失败。
- 折叠的空背包可作为物品摆放，但只有穿戴的背包提供储物网格，不能借嵌套扩容。

## 物品图标

- 背包格子、装备栏、武器栏、悬浮说明和拖拽预览共用运行时缩略图。卡片保留占格和堆叠数量，名称及详情在悬浮时显示。
- `ItemIconCache` 用现有物资目录的模型与分段材质建立独立 RenderView，以固定灯光绘制透明背景；相机自动取景，按物品的原始宽高裁切。取景不加侧向偏角，使物品主轴保持水平或垂直；对平放的枪械、配件和衣物正对其宽面，对瓶罐等竖放物品正对正面，两类仅偏离正视方向 6°，保留少量厚度与透视。摆放旋转时只旋转图标的 UV，不额外渲染。
- 每帧最多处理一张待生成或待上传的图标，完成后复用 GPU 图像，不持续渲染静态物品。
- 角色预览与物品图标共用 2× SSAA：按输出宽高各两倍渲染，以每个输出像素的 2×2 样本计算颜色和几何覆盖率。输出使用非预乘 alpha，避免透明轮廓混入背景或产生黑边，不依赖时域历史。图标仍按每格 64 像素缓存，取景裁切区按该尺寸的两倍生成。
- 磁盘缓存写在 `FileHelper::GetWritableFilePath("cache/item-icons/…")` 指向的应用用户目录；`.thumb` 保存带校验的 RGBA16F 像素，不在 `assets/` 生成图片或模型文件。
- 缓存键包含渲染版本、尺寸、几何和材质内容；模型或材质随场景重新加载时会重新校验。缺失、损坏的缓存自动重建，缓存不可写时继续使用内存中的图标。
- `gnb validate --script assets/agentscripts/nextdayz-inventory-icons.agentscript.json` 检查全部 91 种目录物品和旋转拖放。`game.inventoryIconsRendered` 与 `game.inventoryIconCacheHits` 可区分首次渲染和磁盘缓存命中。

## 堆叠上限

物品卡片显示“当前数量／上限”，每栈独立占一个矩形。合并不改变目标物品的位置和方向；即使没有空格，只要目标栈还有余量就能补入。

| 物品 | 每栈上限 |
| --- | --- |
| 5.45×39 子弹 | 30 发 |
| 7.62×54 子弹 | 20 发 |
| 12 号霰弹 | 12 发 |
| 9×18 子弹 | 24 发 |
| 绷带 `bandage` | 4 |
| 布条 `rag` | 6 |

其他可堆叠物品沿用各自的 `maxStack`，枪械和穿戴物保持单件。

## 固定容器

| 容器 | 网格 | 限制 |
| --- | --- | --- |
| 基础上衣口袋 | 6×1 | 细长物品需横放 |
| 基础裤袋 | 2×2 | 能容纳方形小件 |
| 主、副武器背带（各一个） | 4×2 | 仅枪械和近战武器 |

衣物提供的额外网格见下表。容量数字仅表示已用/总格数，不参与形状判断。

## 物品与穿戴容器尺寸

| ID | 物品 | 物品占位 | 穿戴后提供网格 |
| --- | --- | --- | --- |
| `signal_flare` | 信号枪 | 2×1 | — |
| `optic_red_dot` | 红点瞄具 | 1×1 | — |
| `optic_holographic` | 全息瞄具 | 2×1 | — |
| `optic_4x` | 4 倍镜 | 3×1 | — |
| `grip_vertical` | 垂直握把 | 1×2 | — |
| `mag_quick` | 快速弹匣 | 1×2 | — |
| `mag_extended` | 扩容弹匣 | 1×2 | — |
| `muzzle_suppressor` | 消音器 | 2×1 | — |
| `tactical_flashlight` | 战术手电 | 1×2 | — |
| `ak` | AK-74 | 4×1 | — |
| `svd` | SVD | 4×1 | — |
| `mosin` | 莫辛步枪 | 4×1 | — |
| `shotgun` | 霰弹枪 | 4×1 | — |
| `pistol` | 马卡洛夫手枪 | 2×1 | — |
| `aks74u` | AKS-74U 短突击步枪 | 3×1 | — |
| `bizon` | PP-19 野牛冲锋枪 | 3×1 | — |
| `aps` | APS 斯捷奇金冲锋手枪 | 2×1 | — |
| `svt40` | SVT-40 半自动步枪 | 4×1 | — |
| `toz34` | TOZ-34 上下双管猎枪 | 4×1 | — |
| `knife` | 猎刀 | 2×1 | — |
| `crowbar` | 撬棍 | 4×1 | — |
| `axe` | 消防斧 | 3×2 | — |
| `ammo_545` | 5.45x39 | 2×1 | — |
| `ammo_762` | 7.62x54 | 2×1 | — |
| `ammo_12g` | 12 号霰弹 | 2×1 | — |
| `ammo_9mm` | 9x18 | 1×1 | — |
| `helmet` | 钢盔 | 2×2 | — |
| `jacket` | 野战夹克 | 2×2 | 6×2 |
| `hoodie` | 连帽衫 | 2×2 | 4×2 |
| `vest_military` | 军用背心 | 2×2 | 3×2 |
| `work_pants` | 工装裤 | 2×2 | 5×2 |
| `cargo_pants` | 作战裤 | 2×2 | 6×2 |
| `backpack_small` | 小背包 | 2×2 | 4×4 |
| `backpack` | 背包 | 2×2 | 6×6 |
| `backpack_military` | 突击背包 | 3×2 | 6×8 |
| `hat_beanie` | 针织帽 | 1×1 | — |
| `cap_baseball` | 棒球帽 | 2×1 | — |
| `hat_boonie` | 丛林帽 | 2×1 | — |
| `helmet_tactical` | 战术头盔 | 2×2 | — |
| `hat_ushanka` | 护耳皮帽 | 2×2 | — |
| `jacket_puffer` | 羽绒服 | 3×2 | 5×2 |
| `jacket_rain` | 防雨外套 | 2×2 | 4×2 |
| `jacket_denim` | 牛仔夹克 | 2×2 | 5×2 |
| `jacket_rescue` | 救援夹克 | 2×2 | 7×2 |
| `vest_police` | 警用防弹衣 | 2×2 | 2×2 |
| `vest_chest_rig` | 胸挂 | 2×2 | 6×2 |
| `vest_hunting` | 狩猎背心 | 2×2 | 5×2 |
| `vest_utility` | 工具背心 | 2×2 | 7×2 |
| `vest_rescue` | 反光背心 | 2×1 | 3×2 |
| `jeans` | 蓝色牛仔裤 | 2×2 | 4×2 |
| `pants_tactical` | 战术长裤 | 2×2 | 7×2 |
| `pants_rain` | 防雨长裤 | 2×2 | 4×2 |
| `pants_snow` | 防寒长裤 | 3×2 | 6×2 |
| `backpack_messenger` | 邮差包 | 3×2 | 6×3 |
| `backpack_medical` | 医疗背包 | 2×2 | 6×4 |
| `backpack_rolltop` | 卷口背包 | 2×3 | 4×8 |
| `hat_panama` | 巴拿马帽 | 2×1 | — |
| `helmet_ssh68` | SSh-68 钢盔 | 2×2 | — |
| `helmet_m56` | M56 钢盔 | 2×2 | — |
| `shirt_telnyashka` | 海魂衫 | 2×1 | — |
| `jacket_afganka` | 阿富汗卡上衣 | 2×2 | 6×2 |
| `jacket_gorka` | 戈尔卡山地罩衫 | 2×2 | 6×2 |
| `jacket_strichtarn` | 雨点迷彩上衣 | 2×2 | 5×2 |
| `vest_type56` | 56 式胸挂 | 2×2 | 7×2 |
| `vest_6b3` | 6B3 防弹衣 | 2×2 | 4×2 |
| `pants_afganka` | 阿富汗卡长裤 | 2×2 | 5×2 |
| `pants_gorka` | 戈尔卡山地长裤 | 2×2 | 5×2 |
| `pants_strichtarn` | 雨点迷彩长裤 | 2×2 | 4×2 |
| `backpack_rd54` | RD-54 伞兵背包 | 2×2 | 5×5 |
| `backpack_abalakov` | 阿巴拉科夫背包 | 3×2 | 7×5 |
| `food_can` | 豆子罐头 | 1×2 | — |
| `food_tuna` | 金枪鱼罐头 | 1×1 | — |
| `food_crackers` | 饼干 | 2×1 | — |
| `food_apple` | 苹果 | 1×1 | — |
| `food_chocolate` | 巧克力棒 | 1×1 | — |
| `water_bottle` | 瓶装水 | 1×2 | — |
| `water_bottle_empty` | 空水瓶 | 1×2 | — |
| `drink_soda` | 汽水 | 1×1 | — |
| `bandage` | 绷带 | 1×1 | — |
| `rag` | 布条 | 1×1 | — |
| `medkit` | 急救包 | 3×1 | — |
| `painkillers` | 止痛药 | 1×1 | — |
| `can_opener` | 开罐器 | 1×1 | — |
| `compass` | 指南针 | 1×1 | — |
| `map` | 战术地图 | 2×1 | — |
| `radio` | 野战收音机 | 2×1 | — |
| `battery` | 电池 | 1×1 | — |
| `lantern` | 煤油提灯 | 2×2 | — |
| `fuel` | 燃料罐 | 2×2 | — |
| `bedroll` | 铺盖卷 | 3×2 | — |
| `firecrackers` | 鞭炮 | 1×1 | — |

## 有史可考的物品档案

`FItemDef::lore` 是可选的“物品档案”文本（英文为翻译键，中文在 `UI/Translations.inl`），在背包悬浮提示的描述下方显示。上表 `aks74u` 到 `toz34` 的五把枪与 `hat_panama` 到 `backpack_abalakov` 的十四件服装取材自 [innawoods.net](https://innawoods.net/) 的手绘装备图鉴，每件都写了真实来历；枪械只用世界里已有的四种弹药。模型在 `assets/scad/lib/kit_nd_arsenal.scad`（武器）和 `assets/scad/characters/nextdayz_clothing_heritage.scad`（服装），验收展台是 `assets/scad/source/others/nextdayz_heritage_showroom.scad`。

## 存档契约

版本 5 保存每个实例的 `gridX`、`gridY`、`rotated` 和容器宽高。读取时拒绝重叠、越界及不合法容器归属。
版本 1～4 会迁移并重新摆放；无法装入的物品转移到安全屋仓库，保留数量、枪械装弹和配件，避免升级丢失物品。

## 验证入口

- `gnb.bat build NextDayz gkNextUnitTests`
- `out/build/windows/bin/gkNextUnitTests "[NextDayz]"`
- `gnb.bat validate --script assets/agentscripts/nextdayz-inventory-grid.agentscript.json`

交互回放覆盖精确落格、旋转、越界拒绝和地面拾取；单元测试覆盖碎片空间、拆分/合并、换装事务及存档迁移。
