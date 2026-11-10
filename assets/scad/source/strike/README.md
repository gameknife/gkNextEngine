# Strike 港区地图资产

本目录是 CS 风格地图的第一版 SCAD 作者资产，不是一个新的游戏 executable。
布局按 Dust2 的主要路线关系组织，建筑、美术、角色为新的 lowpoly 港区风格。
没有导入 Valve 模型、贴图、BSP 或 navmesh；未做原版逐尺寸测量，因此不承诺原版的
跑点时间、投掷物点位或竞技平衡。后续校准集中修改 `dust2_layout.scad`，不需要改 kit。

## 打开

```powershell
.\gnb.bat run ScadLibrary --scene assets/scad/source/strike/dust2_port.scad
.\gnb.bat run gkNextRenderer --scene assets/scad/source/strike/dust2_port.scad
```

ScadLibrary 场景列表自动发现 `strike` 分组。角色在角色工作室中分别打开
`assets/scad/characters/strike_police.scad`、`strike_raider.scad`。

| 文件 | 用途 |
| --- | --- |
| `dust2_port.scad` | 主场景，有屋顶、掩体、街道细节和 7 个相机 |
| `dust2_layout.scad` | 区域地板、结构、高差、出生点和包点数据的单一作者入口 |
| `dust2_overview.scad` | 去隧道屋顶的俯视检查版，不应作为正式玩法场景 |
| `dust2_long.scad` / `dust2_b.scad` | A 大道和 B 洞出口的第一人称视角验收 |
| `kit_showcase.scad` | 建筑、门洞、坡道、阶梯、掩体、车辆、街具总览 |
| `characters.scad` | 警匪静态持枪姿态对照，不是 Rig 播放器 |
| `equipment.scad` | 两种步枪外观和头部装备近景 |

## 布局与尺度

- 1 SCAD unit = 1 m；SCAD Z-up；北为 +Y；引擎换算为 `(x,z,-y)`。
- 主区域约 94 × 106 m；T 在南，B 在西北，A 在东北，CT 在两点之间偏北。
- T 出生区、上层隧道和 B 点地面为 2 m；中门、CT 和 A 大道为 0 m；A 点为 4 m；大坑为 -2 m。
- 保留 T→B 洞→B、T→中路→中门→CT、下层隧道→中路、猫道→A 小道→A、
  T→错位双门→A 大道→A，以及 CT→B 门、CT→A 坡道的路线。
- A 小道北端使用薄桥面，CT 可以在下面通过。上层隧道有实体屋顶，下层通过楼梯接中路。
- B 门和 B 窗分开；窗边有跳跃/垫脚块。窗线路没有列入普通步行路线验收。
- 建筑块是不可进入的实体遮挡物；装饰卷帘门不是可交互门。双门保持打开。
- `ds_spawns_t()` / `ds_spawns_ct()` 各提供 5 个 SCAD 空间出生点；`ds_sites()` 提供包点中心与尺寸；
  `ds_callouts()` 提供主要区域名字和位置。目前它们是作者数据，不会自动生成 gameplay 实体。

## Kit 契约

- `assets/scad/lib/kit_strike.scad`：`sk_` 前缀，建筑/地面/掩体/街具/车辆/植物。
- `assets/scad/lib/kit_strike_char.scad`：`so_` 前缀，警匪共用可选件与步枪。
- 两套库只有 module/function，常量为零参函数，`use` 后可直接调用；catalog 由工具生成。
- 落地件底面 z=0；建筑门面 -Y；汽车朝 +X。坡道/楼梯从原点向 +Y 上升。
- 角色件以骨骼 pivot 为原点，所以手臂/腿的 `zMin < 0` 是预期；电缆按端点坐标放置，
  `sk_box` 是居中几何 helper，不应按落地件使用。
- `so_wpn_carbine` 是竖直持枪部件，枪口 -Y、原点在机匣，不是平躺 loot。
- 装饰贴在墙面或行走带边缘；场景层没有整图 difference；建筑件内按需 `gk_flatten`。
- 不以面数预算限制细节。保持几何平面化、造型简明，面数不等同于 lowpoly 风格。

## 警匪 Rig

`strike_police.scad` 为头盔/护目镜/蓝色防弹装备；`strike_raider.scad` 为帽子/面巾/红色夹克。
共享 `characters/strike_rig.scad` 的 17 骨骼层级，各 16 个刚体部件、9 个动画，无蒙皮。

- 公共动作：`idle`、`walk`、`run`、`crouch`、`crouch_walk`、`aim`、`fire`、`reload`。
- 警方另有 `defuse`，匪方另有 `plant`。这是姿态动画，不会执行拆包/下包逻辑。
- 根节点在脚底，角色约 1.86 m；朝 SCAD -Y / 引擎 +Z；右侧镜像只在骨骼体内进行。
- `bone_weapon_socket` 当前挂在 torso 下并带默认步枪几何，持枪双臂用双连杆解算。
  该挂点不是 NextDayz 的右手挂点契约；需要可换枪时应替换此骨骼直属几何，不能叠加第二把枪。
- 步行等动作原地播放，root motion 由未来角色控制器负责。动作在关键帧处解算足底高度；
  已验证支撑脚落地，但没有声称任意插值时刻的精确 IK 或最终控制器速度匹配。
- `fire` / `reload` / `plant` / `defuse` 非循环；`aim`、`crouch` 为静态姿态。
- 配色是显式色，不使用运行时品红 tint 占位。

## 验证

```powershell
.\gnb.bat scad catalog
.\gnb.bat build gkNextUnitTests
.\out\build\windows\bin\gkNextUnitTests.exe "[StrikeAssets]" --reporter compact
.\gnb.bat shot --scene assets/scad/source/strike/dust2_port.scad --frames 160
.\gnb.bat shot --scene assets/scad/source/strike/characters.scad --frames 160
```

2026-09-17 验收：新增 kit 共 43 条 catalog 记录，0 bad / 0 warning；主场景及
kit、装备、角色、俯视、长道、B 洞视角均成功提交场景，SCAD 0 warning。
截图与日志保存在 `out/build/windows/screenshots/strike/`，不提交构建产物。

`Test_StrikeAssets.cpp` 使用真实 evaluator 输出的三角网检查八条路线：每 0.4 m 采样地面、
0.27 m 横向余量、站立净空，并在脚上/腰/头三个高度做沿路三角形相交检查。
这不是完整物理角色胶囊 sweep，也没有测试每一个边界、跳跃、B 窗和投掷物。
Rig 测试通过真实 `FScadRigLoader` 检查骨骼、部件、动作、警告和步态关键帧足底高度。

`gkNextRenderer` 截图右下的白色长方体是现有渲染器的测试枪口 `viewMuzzle`，不在这些
SCAD 资产里。需要无测试枪口画面可用 `--target gkNextEditor` 截图。
