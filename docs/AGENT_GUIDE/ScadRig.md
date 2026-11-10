# ScadRig — SCAD 刚体骨骼角色

基于 OpenSCAD DSL 的角色建模 + 动作机制（Blockbench/Minecraft-entity 同语义：刚体部件挂骨骼层级，**无蒙皮**）。本文件是现行约定；运行时复用入口是 `NextGameplay::Sim::FScadRigVisual` / `FCharacterPool`。

## 资产格式速查（作者视角）

一个角色 = 一个合法 `.scad` 文件（原版 OpenSCAD / ScadStudio 可直接打开看绑定姿态）：

```scad
ROLECOLOR = [1, 0, 1];                      // 换色占位（纯品红），运行时按实例替换
module part_arm() { ... }                   // 非 bone_ 前缀 = helper，几何折叠进所属骨骼
module bone_arm_l() { part_arm(); }
module bone_arm_r() { mirror([1,0,0]) part_arm(); }   // mirror 必须在骨骼体内（烘进网格）
module bone_torso() {
    color(ROLECOLOR) cube(...);             // 体内直属几何 = 刚性绑定该骨骼
    translate([0,0,0.54]) bone_head();      // 调用点外层 translate/rotate = 子骨骼 pivot
}
module bone_root() { translate([0,0,0.84]) bone_torso(); ... }
bone_root();                                // 顶层恰好一个 bone_* 调用

anim_walk = [                               // clip = 顶层 anim_<name> 变量（纯数据 list）
    ["bone_leg_l", "rot", [[0,[35,0,0]], [0.4,[-35,0,0]], [0.8,[35,0,0]]]],
    ["bone_root",  "pos", [[0,[0,0,0]], [0.2,[0,0,0.03]], [0.4,[0,0,0]]]],
];
anim_sit = [ ["loop", false], ["bone_root","pos",[[0,[0,0,-0.42]]]] ];   // 单帧 = 姿态
```

规则（违反 → `SCADRIG:` 前缀 warning，加载不失败）：

1. 骨骼 = `bone_` 前缀 user module；骨骼名 = 完整模块名；每骨骼只调用一次（重复取首个）。
2. bone 调用外层只允许 `translate`/`rotate`（scale/mirror → warning）。
3. 顶层只有一个 `bone_*` 根调用；散落几何被忽略。
4. 通道 `rot`（度，OpenSCAD rotate 语义）/ `pos`（SCAD 单位）/ `scale`；key 时间单调递增，线性插值（rot 加载期转 quat 后 slerp）。`["loop", bool]` 元数据行，默认 true；duration = 最大 key 时间。
5. 合成语义：`L_final = L_bind · T(pos) · R(rot) · S(scale)`，空 clip = 绑定姿态。
6. 1 unit = 1 m，Z-up，根骨骼原点落地；引用未知骨骼的通道 → warning + 丢弃。
7. **轴对称的肢体，绕自身对称轴的旋转是看不见的。** 手臂/腿常常是一根沿局部 Z 轴的圆柱
   加一个球，那么 `rot` 的 Z 分量在画面上就是空操作：前后摆动要写 X，外张/上举要写 Y。
   这一条不报 warning（数据完全合法），症状是动作"播了但角色像站着不动"。
   `astro_bot.scad` 的 hover/win/wave/zip 初版就整批写在了 Z 上；
   `Test_NextAstrobot.cpp` 现在采样手部四元数把它钉住，新 rig 值得照抄这个断言。
8. 引擎面朝 +Z = SCAD 的 −Y（鼻子/鞋尖朝 −Y 建模）。

示例资产：`assets/scad/characters/agent_basic.scad`（7 骨骼、~250 tris、idle/walk/sit/work）。

## 角色件库 kit_char（造型组装，非只换色）

`assets/scad/lib/kit_char.scad`（prefix `ch_`，scaleClass human）把 rig 角色拆成可组合部件，
新角色 = 薄 `.scad` 文件选件拼装：

- **部件**（分类 = 名字第二段，进 catalog / ScadLibrary 浏览器）：`ch_head_*` / `ch_hair_*` /
  `ch_hat_*` / `ch_torso_*` / `ch_arm_*` / `ch_leg_*` / `ch_acc_*`；整装预设 `ch_char_*`。
  部件原点 = 所属骨骼 pivot；手臂/腿以左侧建模，右侧骨骼体内 `mirror([1,0,0])`。
- **骨架标准**：`ch_pivot_torso/head/arm_l/arm_r/leg_l/leg_r()` 返回固定 pivot（与 agent_basic
  相同），因此 **clip 跨角色复用**：`anim_walk = ch_clip_walk();`（idle/walk/sit/work/wave）。
- **use 语义约束**：kit 顶层赋值会被丢弃，常量一律零参函数（`ch_TINT()` 品红换色占位、
  `ch_SKIN(i)` 等）。loader 侧零改动：`use <>` 闭包 + 函数导入本来就支持。
- **范例角色**：`characters/worker.scad`（安全帽+反光背心+背包+工具腰带+靴）、
  `characters/citizen.scad`(马尾+连衣裙)；单测 `gkNextUnitTests "[KitChar]"`。

**ScadLibrary 角色台**（顶栏应用模式，`CharacterDesigner.*`）：按分类选件 + 调
肤色/发色/主色 → 生成角色 scad → `FRigPreview` 实机预览（rig 加载 → `BeforeSceneRebuild`
注入部件模型/材质 → `OnSceneLoaded` RigInstance 实例化 + FRigAnimator 播 clip，可切动画/
绑定姿态）→ 导出 `assets/scad/characters/<名>.scad`（`use <../lib/kit_char.scad>`，游戏直接
加载）。**生命周期**：引擎顺序 BeforeSceneRebuild → OnSceneUnloaded → OnSceneLoaded，
`FRigPreview::OnSceneUnloaded` 只清 animator/节点指针，不可清注入产物（同 AirportSim 教训）。
回归脚本：`gnb validate --script assets/agentscripts/scadlibrary-designer.agentscript.json`。

## 放样造型库 rig_forms（让刚体角色不再是方块人）

`assets/scad/lib/rig_forms.scad`（前缀 `rf_`）给刚体部件提供"截面放样"，仍然是每骨骼一个刚体、无蒙皮：

- **一个形体 = 沿骨骼 Z 轴的几圈截面**。`rf_loft(rings, m = 8, n = 2.6)`，ring 是
  `[z, w, d(, x, y(, n))]`：高度、全宽、全深、截面中心偏移、可选的超椭圆指数（2 = 椭圆，
  ~3 = 肌肉感，6+ = 倒角盒）。自下而上列出，首尾自动封盖。输出是一个 `polyhedron`，
  8 边 6 圈约 100 个三角形。
- **盒子做不到的三件事**：锥度（手腕比前臂细）、偏离骨轴的体积（胸、小腿肚、三角肌）、
  **圆头并伸过枢轴**——上臂下端和前臂上端都收成圆顶并互相重叠，所以肘 / 膝 / 肩弯曲时看到的
  是一个关节而不是一道缝。这是刚体绑定里最值钱的一条。
- **着色**跟随加载器的 35° 折痕角：8 边保持硬朗的切面，12 边以上绕形体平滑。默认相位让
  ±X / ±Y 轴上是一个**平面**而不是一条棱，口袋、插板、织带才有面可贴。
- `rf_grow(rings, dw)` 把截面整体放大一圈（衣服套在身体上、绗缝层套在外套上）；
  `rf_span(rings, z0, z1)` 取其中一段并在切口处插值（袖口、反光条、护膝都是"沿着形体的一圈"，
  而不是套在外面的方盒）；`rf_box(size, bevel)` 是硬件细节用的倒角盒。

NextDayz 是完整用例：

- `characters/nextdayz_body.scad` 是幸存者的**身体形体**（`ndb_` 前缀）：躯干 / 骨盆 / 四肢的截面表
  （`ndb_torso()` 等函数）、带下颌-颧骨-眉弓-鼻梁的头、松握拳的手、赤脚。`nextdayz_survivor.scad`
  的 `part_*` 只是转调它们，骨架与全部 clip 不变。
- 同一个文件还提供**服装外壳** `ndb_shell_coat / sleeve / leg / seat` 和靴子 `ndb_boot_*`。衣柜原先的
  分层包络（外套厚 .28、袖 .158×.168、裤腿 .19×.205…）是按盒子定的，口袋、拉链、织带全都贴在
  那些面上；外壳**在细节所在的高度保持同一包络**，只在盒子没有形状的地方做文章——收腰、斜肩到
  领口平台、收口的袖口、圆头盖住关节、裆部内收。所以 `nextdayz_clothing.scad`（基础款）和
  `nextdayz_clothing_heritage.scad`（史实款）换壳时细节坐标一行没动。
- 约束仍在：衣物隐藏的是身体的渲染部件而不是骨骼；裸体躯干要留在背心内壁以内
  （无外套时背心深度会缩到 0.78）；头要装得进原有帽盔（宽 ≤ .22、顶 ≤ .25）。
- 手的局部坐标约定：手指沿 −Z 并朝 −Y 卷曲（掌心朝 −Y，武器 socket 与 IK 握点都在那一侧），
  拇指在外侧。
- 预算：裸体 15 个部件合计约 2900 三角形（`Test_ScadRig.cpp` 守着 < 3200）。
- 快速看效果：`gnb validate --script assets/agentscripts/nextdayz-lobby-look.agentscript.json`
  （进机舱大厅、穿一套衣服、截一张图，约 7 秒）。只改了 `.scad` 时先 `gnb build NextDayz` 同步资产。

## ScadLibrary 角色工作室（动作 + 装备）

ScadLibrary 顶栏把作者工具组织成三个应用级模式，每个模式拥有独立布局：

- **场景组装（Ctrl+1）**：左侧浏览 `assets/scad` 组装场景与 Kit，中间预览，右侧提供
  对象化摆放和完整 SCAD 源码编辑；
- **角色台（Ctrl+2）**：隐藏通用 kit 浏览器，扩大角色预览，右侧集中造型与颜色；
- **角色工作室（Ctrl+3）**：最大化动作预览区，并提供更宽的动作、骨架与装备编辑器。

场景组装会递归发现所有引用 `kit_*.scad` 的场景。复杂手写场景通过 ScadLoader 求值
变量、循环、条件和 module 引用，将最终 Kit 实例展开成可编辑对象；展开编辑必须另存副本，
原文件仍保留完整源码。ScadLibrary 生成的固定变换链平铺场景可无损往返对象列表。未保存
源码通过工作区副本预览。新建对象场景写入 `assets/scad/evaluated/`，源码场景写入
`assets/scad/source/`，过程场景写入 `assets/scad/proc/`；
`gen/` 仍视为可被规格重新生成覆盖的产物。完整目录约定见 `assets/scad/README.md`，
回归脚本为 `assets/agentscripts/scadlibrary-assembly.agentscript.json`。

角色工作室面向已有 ScadRig 资产，默认打开
`assets/scad/characters/nextdayz_survivor.scad`，也可输入其他 `.scad` 路径：

- **动作预览**：动作选择、播放/暂停、正反向速度、时间拖动和绑定姿态预览；
- **角色工作室布局**：顶部以居中的大页签切换三个应用级模式；左侧是全高骨骼层级与
  骨骼信息，中间是带变换工具条的角色视口，右侧是动作/装备属性，动画时间轴独立贴在
  中央视口底边，切到装备页时自动收起；
- **贴底时间轴动作修改**：骨骼层级按 rig 父子关系显示并标注该骨骼的轨道数，时间轴只
  显示所选骨骼的 `pos` / `rot` / `scale` 轨道。关键帧显示为可拖动菱形；标尺和轨道空白可定位播放头，双击轨道按当前
  插值创建关键帧，选中后可精调时间与 XYZ 数值，也可删除关键帧或整条轨道。编辑值始终
  使用 SCAD 作者空间（Z-up、XYZ 角度），预览时转换为引擎空间。选择骨骼后，视口在该
  骨骼枢轴显示移动/旋转/缩放 Gizmo；拖动时暂停播放，并在当前时间自动创建或更新对应的
  `pos` / `rot` / `scale` 关键帧；
- **安全回写**：保存时把加载器求值后的全部 clip 展开到原文件末尾
  `SCADLIBRARY_RIG_EDITOR_BEGIN/END` 标记区。后续保存只替换该区域，不改骨架、几何和
  helper function；因此 `rh_gait_clip()` 一类函数生成的动作也可编辑；
- **装备预览/修改**：任意 catalog kit 模块可挂到任意骨架，支持模块参数、启用状态及
  SCAD 本地位置/旋转/缩放的实时调整。预览仍走真实的 model/material 注入和 bone parent，
  不是独立摆在角色旁边的静态模型；
- **装备数据**：保存到角色同名 `.equipment.json`。`kit` 写相对路径，记录稳定的 `id`、
  显示名、目标骨架、模块、参数和 TRS；这是后续换装系统可直接消费/扩展的作者数据，
  当前游戏运行时是否读取它仍由各产品决定。

NextDayz 初始配置为 `characters/nextdayz_survivor.equipment.json`（军帽、背包、主武器）。
交互回归：

```bash
gnb validate --script assets/agentscripts/scadlibrary-workbench.agentscript.json
```

## 运行时管线

```
FScadRigLoader::LoadRig(.scad)            // ScadLoader 模块（src/Modules/ScadLoader/FScadRig.*）
   → Assets::FRigAsset                    // Engine 层纯数据（src/Engine/Assets/Data/RigAsset.hpp）
      bones[]（父先序 bind TRS，引擎空间） parts[]（每骨骼分 section 的 Model + tint 标记） clips[]
FRigInstance::Instantiate(scene, asset, desc)   // NextGameplay（src/Gameplay/Rig/RigInstance.*）
   → 每骨骼一个 Node（bind TRS）+ 每 part 一个 RenderNode 挂骨骼下；返回根骨骼 Node
FRigAnimator                              // 每实例：Bind / Play(clip, fade) / SetPlaySpeed
   → Update(dt) 采样 →（可选 crossfade）→ 写骨骼 TRS（可变引用）→ root 一次 RecalcTransform(true)
```

复杂角色可选择 `src/Gameplay/Rig/RigLayeredAnimator.*` 的 `FRigLayeredAnimator`；旧消费端无需迁移：

- `FRigBoneMask::FullBody` / `FromSubtree` 创建逐骨骼权重遮罩，`SetBoneWeight` 可微调 torso/head 等过渡。
- layer 按创建顺序求值，支持 `Override` 与相对绑定姿态的 `Additive`。
- `SetLoopBlend` 接收多个 `FRigClipBlendSample`；同一 `syncGroup` 在方向或 gait clip 集切换时保留
  normalized phase。
- `SetStaticBlend` 用于 aim pose，`SetManualBlend` 用权威 gameplay 时间采样交互动作。
- `PlayOneShot(..., restartIfSame=true)` 用于逐发 recoil；`IsOneShotComplete` 可查询收尾。
- 采样只覆盖 clip 实际 authored 的 position/rotation/scale 分量；未 authored 分量不会错误覆盖底层 pose。
- `Update` = `Evaluate` + `Apply`。需要在 clip 混合之后再做程序化处理时拆开调用：`Evaluate(dt)` 只把各层
  混进 `MutablePose()`，不写 Node；处理完再 `Apply()`（一次 root `RecalcTransform`）。

**姿态后处理**（`src/Gameplay/Rig/RigPoseOps.*`，作用于 `FRigPose`，与格式无关）：

- `ComputeRigComponentTransforms` / `RefreshRigComponentTransforms`：组件空间（rig root 的父空间，即
  角色世界节点）的正向运动学；骨骼按父先序存储，所以刷新"从某骨骼起的所有后续骨骼"即可覆盖其子树。
- `SetRigBoneComponentRotation` / `RotateRigBoneInComponentSpace` / `SetRigBoneComponentPosition`：
  用组件空间的目标改写 bind 相对的 pose 分量（带权重），用于瞄准俯仰分摊、整体倾斜等。
- `SolveRigTwoBoneIK(upper, lower, end, target, poleTarget, weight)`：两骨 IK，关节朝极点弯曲；
  只改 upper/lower 的旋转，末端朝向需要时再用 `SetRigBoneComponentRotation` 设置。
- 这些编辑假设链路无缩放（bind scale 只参与正向运动学）。单测：`gkNextUnitTests "[PoseOps]"`。

**IK 目标骨骼约定**：空的 `bone_ik_*` 骨骼（无 part）把"手该在哪里"作为普通 clip 数据表达——clip 动画
目标帧，运行时把手臂解算上去。目标帧之间的混合是位置插值 + 旋转 slerp，远比混合关节角稳定（混合关节角
会让手脱离武器）。NextDayz 士兵：`bone_ik_weapon`（武器帧，躯干子骨骼）→ `bone_ik_hand_r/l`（握把 /
护木上的腕部帧）；`bone_weapon_socket` 的 bind 是右手握把帧的逆，所以右手解到 `bone_ik_hand_r` 时武器
正好落在 `bone_ik_weapon` 上。

NextDayz 是完整参考：18-clip locomotion 混合空间（姿态 × 速度 × 方向，单一 sync group）→ hold
（手臂 + IK 骨骼 Override）+ hold_body（躯干/头 Additive：侧身与转头）→ weapon_action 同样拆两层 →
recoil Additive → action FullBody，之后是程序化阶段（倾斜、瞄准分摊、武器晃动、双臂 IK）。资产和运行时
契约见 `docs/projects/nextdayz/nextdayz-3c-scadrig-design.md`。

NextDayz 感染者（`Zombies/ZombieRigVisual.*`）是"不拿武器"的参考：base（idle / walk / run 或跛行
那一对，按模拟速度混合、按步频匹配播放速率）→ reach（只罩双臂，追近时抬手）→ action（alert / 两种
攻击 / stagger，用**模拟自己的计时器**手动采样，所以出手帧就是结算伤害的那一帧）→ death（按致命一击
选择倒地方式，停在最后一帧）。程序化阶段叠加每只个体的驼背/歪头、头和胸口追踪玩家、转弯倾斜，以及
受击 flinch：每次命中按方向、命中点和冲击量踢一下阻尼弹簧，所以中弹可叠加、可按伤害缩放、并且不打断
腿部动作。它同时按骨骼输出胶囊命中盒，子弹打的是动画后的骨架而不是渲染网格。

## 程序化 clip 作者库 rig_humanoid（推荐给复杂人形）

`assets/scad/lib/rig_humanoid.scad`（前缀 `rh_`）在 SCAD 里从"意图"生成 clip，输出仍是普通 `anim_*`
数据，loader、ScadLibrary 时间轴编辑器和所有运行时都不需要改：

- **步态** `rh_gait_clip(skel, gait, dir)`：步频 / 速度 / 支撑比例 / 步幅后偏 / 抬脚 / 起伏 / 重心
  左右转移 / 髋部扭转与侧倾 / 躯干前倾与反向扭转 / 手臂摆动。靴子用两骨腿 IK 钉在 z=0，支撑相做
  脚跟着地 → 平放 → 脚尖蹬离的滚动；后退是时间反转的前进步态；侧移时髋部转向移动方向、躯干反向扭回。
  所有方向和步态都以"相位 0 = 左脚跟着地"对齐，可以在同一个 normalized phase 上混合。
- **持枪** `rh_upper_clip(skel, rig, keys, loop)`：每个 key 给出武器帧（躯干空间）和可选的左手目标，
  双臂用两骨臂 IK 解算到握把/护木，同时写出 `bone_ik_*` 通道。
- **全身** `rh_body_clip(...)`：跳跃、翻越、拾取这类 key 用骨盆 / 双脚接地点 / 武器帧 / 左手（可用
  世界空间 `hand_l_root`）描述。
- 骨架与步态参数以字典书写，入口处用 `rh_skeleton([...])` / `rh_gait_params([...])` 解析为定长
  向量（字典查找是线性扫描，生成器会调用数千次）。
- 约定：角色面朝 −Y、左侧 +X、**右侧 −X**（`_r` 骨骼放 −X）。刚体靴子的滚动支点 `ball` 要放在靴尖附近，
  否则脚尖蹬地时靴尖会扎进地面。
- **跛行** gait 参数 `limp`（0..1，右腿）：拖行的脚抬得低、脚尖下垂擦地，承重时骨盆下沉、躯干向伤腿
  侧倾。只改抬脚与姿态，不改步幅，所以两只脚的支撑速度仍一致、不会滑步。
- **徒手上身** `rh_free_upper_clip` / `rh_free_body_clip`：没有武器的角色每只手臂要么写 FK 角度
  （`arm_l: [upperarm, forearm(, hand)]`），要么写躯干空间腕部目标 `wrist_l: [pos, euler]` 由两骨 IK
  解向 `pole_l`；全身版本同样用 `pelvis` / `foot_l/r` 描述下半身。
- **稠密 key** `rh_dense_keys(keys, step)`：关节角在两个 IK 解之间插值时，接地的脚和够取的手会漂走
  （摔倒、扑咬时脚会穿地）；它按 `step` 秒把"意图"（目标位置、角度、接地点）线性插值后逐帧求解。
  同时存在于两个 key 的值线性混合，其余（例如 FK 手臂 ↔ 腕部目标）在中点切换。
- 回归：`gkNextUnitTests "NextDayz survivor gaits plant the boots and holds keep both hands on the rifle"`
  采样每个步态检查"不穿地、每只靴子按 duty 比例贴地"，并检查持枪 clip 里武器在右手、左手在护木上；
  `"NextDayz infected gaits plant the feet and actions stay on the ground"` 对感染者做同样的步态检查，
  另外检查全身动作不穿地、三种死亡最终躺在地上、两种攻击出手帧的手在胸口高度够到前方。
- 代价：生成在加载时运行，`nextdayz_survivor.scad` 约 0.7 s。
- 依赖 OpenSCAD 语义的两处 evaluator 行为（2026-11 修正前都是错的，库里的写法依赖修正后的行为）：
  嵌套向量的 `+ - * /` 与一元负号逐元素递归（`[pos, euler]` 帧可以直接插值），`undef == undef` 为真、
  向量逐元素比较相等。回归在 `Test_ScadLoader.cpp` 的
  `"Scad vector arithmetic and equality follow OpenSCAD for nested vectors and undef"`。

- 关键帧容器复用 `Model.hpp` 的 `AnimationChannel<T>`（单 key 直接返回该 key；`Sample` 为 const）。
- evaluator 侧唯一改动：`SceneEvalResult.topLevelVariables` 顶层变量快照（`anim_*` 由此读取）。
- 坐标转换与场景加载共用 `FScadShared.h`：`ScadToWorldBasis/ScadLocalToEngineTRS/ScadToWorldPos/ScadRotateXYZ`。
- 多实例去同步：`SetPhaseOffset`；行走速度匹配：`SetPlaySpeed(speed / baseSpeed)`。
- 单测：`gkNextUnitTests "[ScadRig]"`（loader）、`"[Rig]"`（animator/实例化链路，含真实资产站姿数值验证）。

## 从 C# 驱动：Rig 绑定

C# 游戏没有 gameplay 层的类型，所以 rig 通过一个引擎子系统暴露：`NextRig`
（`src/Engine/Runtime/Subsystems/NextRig.hpp`）声明接口，`NextGameplay::Rig::Install(engine)`
（`src/Gameplay/Rig/RigSubsystem.*`）装上实现，`Modules/NextDotNet` 的 `Rig.*` 绑定调用它。理由与
`NextPhysics` 完全相同：引擎拥有帧和场景，领域实现在别处，而 C# 侧只能看到句柄。

实现刻意**不是** `FCharacterPool`：那是 sim 池，带导航网格与路径跟随，是它大部分的开销、也是自己驱动
角色的游戏不需要的部分。两者共享的是 `FScadRigVisual`（世界节点 + rig 子树 + animator）。

生命周期是引擎的，不是选择：

| 时机 | 能做什么 | 为什么 |
|---|---|---|
| `BeforeSceneRebuild` | `Rig.DeclarePool(path, capacity)` | 只有此刻 part model 能注入；GPU-driven primitive buffer 定容一次，之后无法凭空多出实例 |
| `OnSceneLoaded` | `Rig.Acquire(pool, pos, yaw, tint)` | 节点此时才存在 |
| 每帧 | `Rig.SetTransform` / `Rig.PlayClip` / `Rig.SetPlaySpeed` / `Rig.Release` | 普通 gameplay |

```csharp
protected override void BeforeSceneRebuild()
    => enemyPool = Rig.DeclarePool("assets/scad/characters/nextdayz_infected.scad", 20);

protected override void OnSceneLoaded()
    => enemy = Rig.Acquire(enemyPool, in spawn, 0.0f, in tint);

protected override void OnTick(double dt)
{
    Rig.SetTransform(enemy, in position, MathF.Atan2(dx, dz));   // 引擎朝向 +Z，所以是 atan2(x, z)
    Rig.PlayClip(enemy, "walk");                                  // 播当前 clip 是 no-op，可每帧调
}
```

要点：

- **动画由引擎推进**。子系统每帧 tick 所有活着的 animator，并自己 `MarkTransformDirty()`；驱动
  rig 的 C# 游戏**不需要**为角色调 `Scene.MarkTransformDirty()`（移动普通 render node 仍然需要）。
- **clip 名写错不报错**，角色会退回绑定姿态；名字来自数据时先问 `Rig.HasClip(pool, name)`。
  回归测试 `Test_TemplateRigClips.cpp` 用这条契约守住 `tps` 模板与两个资产的耦合。
- **句柄不复用**：released 实例的 id 永远读作 dead，不会变成接手该槽位的那个角色。
- `tint` 只影响资产标了可染色（`ROLECOLOR` / `ch_TINT()` 占位）的 section；`nextdayz_survivor` 是
  显式配色的，传什么颜色都不变；`nextdayz_infected` 的衬衫（含袖子和下摆碎布）是 `ROLECOLOR`，tint 就是
  衬衫颜色。
- **宿主必须链接** `NextGameplay` + `ScadLoader` 并调用 `Install`。`gkNextLauncher` 和
  `gkNextEditor` 都做了，所以 manifest 里写 `"requiredModules": ["ScadLoader", "NextGameplay"]`
  的游戏在两边都能跑；别的宿主会在菜单里标灰。`Rig.IsAvailable()` 是运行时的同一个问题。
- 上手最快的路径是 launcher / 编辑器的 **New Game Project → Third-Person Shooter** 模板，它是这套
  API 的完整用例（玩家 + 敌人池 + 状态驱动的 clip 切换）。

需要分层混合（locomotion + aim + recoil 叠加）时，C# 侧目前没有对应绑定；那条路是 native 的
`FRigLayeredAnimator`，NextDayz 是参考实现。

## Sim Kit 与 AirportSim 接入结论

- `src/Gameplay/Sim/ScadRigVisual.*` 实现 `FScadRigVisual`：世界 Node（位置/yaw/体型微缩放/PhysicsComponent）+ rig 子树 + animator；`FCharacterPool` 统一负责注入、实例化、动画提示与 box fallback。AirportSim、StudioSim 和 CitySolSim 都走这条共享路径。
- 非 tint section 材质全池共享，tint section 每池位一份；为满足 GPU-driven primitive buffer 的定容语义，每池位注入独立 part model 拷贝。容量和开销必须以当前消费端配置及 profiler 为准。
- 生命周期顺序仍是 `BeforeSceneRebuild` 注入 → `OnSceneUnloaded` 清运行时指针 → `OnSceneLoaded` 实例化。清理时不能提前销毁已注入的 model/material 数据；`FCharacterPool::Clear()` 只重置 visual、导航、scene 指针和重新注入标志。
- AirportSim 开关是 `AirportSimConfig.hpp` 的 `kUseScadRigVisual`（默认 true），rig 路径为 `kAgentRigPath`；当前池容量由 18 名员工 + 24 名旅客组成，共 42 个 slot。
- 验收使用 `./gnb.sh shot --target AirportSim --frames 300` 检查多色角色排队、行走与在岗；性能结论必须重新采样，不沿用旧日志中的固定毫秒数。
