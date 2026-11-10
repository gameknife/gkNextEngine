# 桌面五程序发布包（草稿）

本次将 ScadLibrary、NextAstrobot、Brotato3D、gkNextRenderer 和 gkNextEditor 放入同一个桌面包，提供 Windows x64、Linux x86_64 与 macOS arm64 版本。

- **ScadLibrary**：零件库、场景拼装、地形与角色动画编辑；完整 `assets/scad/` 以明文提供，包含目录索引、库、角色和全部仓库内 SCAD 场景，可直接编辑。
- **NextAstrobot**：SCAD 关卡驱动的 3D 平台跳跃原型。
- **Brotato3D**：俯视角生存射击原型，附完整游戏配置。
- **gkNextRenderer / gkNextEditor**：渲染浏览、多管线对比、场景与材质编辑。

完整解压归档，保持 `bin/` 与 `assets/` 并列，启动 `bin/<target>`（Windows 加 `.exe`）。`gkNextMotionBenchmark` 仍可从源码构建，本次不随包发布。

## 正式发布前必须完成

**当前仅生成 draft，不公开发布。** `tools/brotato3d-pak/README.md` 标明现有 Brotato 音效和图标来自不可分发的商业参考素材。必须先替换素材、重建 `brotato3d.pak`、更新来源与许可说明，并更新 `gnb setup` 下载的 pak 发布源，再从替换后的提交重新生成三平台全部归档。已有草稿包及其精确运行资源包不能直接转正。

干净机器验收：五个程序启动；ScadLibrary 打开、修改、保存并重开 SCAD；NextAstrobot 切换关卡与机关交互；Brotato3D 开始战斗、升级、商店及音效；renderer/editor 切换渲染器、加载场景并正常退出。Windows NVIDIA 设备另验真实 DLSS。

AI 生成依赖另行配置的服务；包内不提供 gnb sidecar、本地模型或 .NET 开发工具链。有关 C# 开发工作流请使用源码构建环境。

完整步骤见 [发布流程](../guides/release-process.md)。
