---
title: "NextDayz 持久世界与直升机撤离"
category: project
status: 现行（MVP）
owner: NextDayz
created: 2026-11-05
last_updated: 2026-11-05
---

# NextDayz 持久世界与直升机撤离

**安全屋备装 → 随机 POI 部署 → 搜刮生存 → 找到信号枪 → 停机坪呼叫直升机 → 登机撤离 → 入库／出售换金 → 再次出击。**

这是 [PVE 生存循环](nextdayz-productization-design.md) 上的单人本地长线机制。基础循环最初以 Riverland 验证；当前默认接入[两张真实地理关卡与世界派遣](nextdayz-world-map-design.md)。世界物资跨死亡、撤离与游戏重启保留，Riverland 继续作为原有回归场景。

## 玩法规则

| 环节 | 当前行为 |
| --- | --- |
| 安全屋 | 首次进入打开仓库与出击准备界面；仓库不限容量。可存取单件／整栈、穿戴衣物、选择手持武器或一键存入全部装备。 |
| 新出击 | 可携装或空手出击，不赠送消耗品；从地图标记的五个 POI 外围安全落点中随机部署。基础衣物口袋始终存在。 |
| 中途退出 | 原地续接，恢复随身装备、生命／饥渴／伤口、视角、快捷栏、提灯、世界时间和当前撤离航班。未提交的使用／拾取动作取消。 |
| 地图物资 | 手摆物资、随机刷新点的内容和已拾取状态、补货计时、地面掉落均保存。沿用离开区域且不在视野内才补货的规则。 |
| 信号枪 | 军事、警察与野外物资表低权重刷新，一次性使用。到有效停机坪按 **B** 或在背包中使用；无效位置、已有航班时不消耗。 |
| 撤离点 | 每张地理关卡提供两处撤离区。Riverland 回归场景仍使用军事基地地面停机坪与戈尔卡医院屋顶，医院东侧外楼梯可直接步行上顶。地图物品打开的地图标记两处 `[H]` 撤离点；携带信号枪时目标栏显示最近撤离点距离和方向。 |
| 直升机 | 固定航线进场 → 降落 → 限时停留 → 起飞。等待期间保持原有感染者威胁。到黄色舱门踏板附近按 **E** 登机，锁定操作并播放起飞镜头，起飞完成才将随身物资入库。 |
| 错过航班 | 空机起飞，玩家继续留在地图；需另一把信号枪再次呼叫。 |
| 死亡 | 随身物资在原地形成可回收掉落堆，玩家自动返回安全屋。仓库、黄金和世界保留，下次出击可找回装备。 |
| 出售 | 安全屋按固定售价出售单件或整栈物资，腾空仓库并增加黄金。黄金只累计、保存和展示；本期没有市场或购买功能。 |
| 时间 | 安全屋与离线期间暂停世界；感染者沿用运行时刷新，不逐个跨次保存。 |

信号枪稀有度由 `Data/LootTables.cpp` 的权重决定。`assets/configs/nextdayz_extraction.json` 配置到达／降落／停留／起飞时长、呼叫／登机距离、自动存档间隔，以及特定物品售价；其余物品使用按种类固定的售价。

## 持久化与物资归属

- `World/Campaign` 负责仓库、黄金、出击记录与撤离状态机；`NextDayzExpedition.cpp` 连接游戏生命周期、存档、界面与输入。
- 世界和玩家作为同一份版本化 JSON 快照提交，物资在地图、随身库存和仓库之间只保留一个归属。保存数量、枪械装弹、穿戴状态和衣物提供的容器关系。
- 地图物资键由来源／物资类型、毫米精度的位置与重叠序号构成，跨同一版地图加载保持稳定；不使用运行时节点编号。改变地图布局时需要另行处理存档迁移。
- 地面掉落使用可扩展显示节点，不再因原先的 32 堆池上限覆盖旧物资。
- 正式存档由平台用户数据目录下的 `NextDayz/campaign/save.json` 管理。默认每 10 秒自动保存，部署、拾取／使用提交、丢弃、死亡、航班阶段切换、撤离入库和出售等关键变更也保存；正常退出保存最新位置与航班计时。
- 写入先完成 `.tmp`，再轮换主档与 `.bak`；读档失败尝试备份，两个都无效则显示错误并暂停进程内玩法，保留原档。写入失败也暂停玩法，允许重试保存。
- 重开生命不再调用全图物资重置。`LootSystem::ResetSession` 仅保留给隔离的旧版测试与 agent 测试初始化。

## 验证与复现

按顺序运行，第二条持久化脚本会启动独立进程读取第一条留下的存档：

```powershell
gnb.bat build NextDayz gkNextUnitTests
out/build/windows/bin/gkNextUnitTests.exe "[NextDayz]"
gnb.bat validate --script assets/agentscripts/nextdayz-extraction.agentscript.json
gnb.bat validate --script assets/agentscripts/nextdayz-persistence-prepare.agentscript.json
gnb.bat validate --script assets/agentscripts/nextdayz-persistence-resume.agentscript.json
gnb.bat validate --script assets/agentscripts/nextdayz-extraction-traversal.agentscript.json
gnb.bat validate --script assets/agentscripts/nextdayz-smoke.agentscript.json
```

Agent validation 默认保留旧版无持久化启动语义，避免已有 3C／战斗测试依赖玩家正式存档。新脚本通过 `nextdayz.validation.campaign` 显式启用，使用进程工作目录下 `out/nextdayz-validation/` 的独立测试文件，不读写正式存档。

规则测试覆盖容器恢复、非法存档拒绝、装弹保留、出售守恒、呼叫有效性、登机条件、错过航班、只结算一次和备份恢复；交互脚本覆盖连续出击、死亡回收、40 堆掉落保留、跨进程恢复降落中的航班，以及从地面走上医院屋顶。截图与断言证据由 gnb 自动留在 `validation_runs`。

后续扩展边界：联机、可建造安全屋、仓库升级与自由驾驶直升机。多地图派遣和分图存档的现行契约见[世界派遣与地理关卡](nextdayz-world-map-design.md)。
