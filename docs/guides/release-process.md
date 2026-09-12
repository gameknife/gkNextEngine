# 发布流程（Release Process）

本文描述 gkNextEngine 的完整发布流程。同一个 `v*` Release 发布标准 `default` preset
（**gkNextRenderer / gkNextEditor / ScadLibrary / NextAstrobot / Brotato3D**），平台优先级 Windows > Linux > macOS。

本次只准备草稿包。**Brotato 商业参考素材替换完成前，不得将草稿转成正式 Release。**
仓库 `tools/brotato3d-pak/README.md` 记录当前音效 / 图标尚不可分发。
替换为自制或具备分发许可的内容后，重建 `assets/paks/brotato3d.pak`、更新第三方声明，
再从新提交生成全部平台归档与 `runtime.pak`，完成验收后人工发布。

本次 `nextbrotato3d` 对应实际 CMake target `Brotato3D`。`gkNextMotionBenchmark` 保留源码目标，
不再进入默认发布包。

---

## 1. 发布前检查（Release Checklist）

在打 tag 之前，逐条确认：

- [ ] Brotato 音效 / 图标已替换并重建 pak、来源与许可已记录（未完成只允许 draft）
- [ ] `gnb test` 全绿（本机跑全量；CI 只跑 `~[GPU]` 子集）
- [ ] `gnb visual` 无视觉回归
- [ ] 五个 target 在本机能正常启动、切换全部渲染器、截图、正常退出
- [ ] 100 / 125 / 150 / 200% DPI 下编辑器与游戏 UI 无文字裁切
- [ ] 无硬件光追的设备上（或 `--forcenort`）回退链路正常
- [ ] `README.md` / `README.en.md` / `AGENTS.md` / `docs/README.md` 与代码一致
- [ ] `THIRD-PARTY-NOTICES.md` 覆盖所有随包分发的第三方组件
- [ ] 仓库内无个人推广链接、无个人签名身份
- [ ] `src/build.version` 会由 CI 覆盖，不需要手工改
- [ ] 发布构建显式使用 `-DGK_ENABLE_TRACY=OFF`，并确认产物不监听 Tracy 端口

### 本地预演打包

CI 出问题时排查成本高，建议先在本机跑一遍完整链路。以下为 shell 构建入口；
Windows 用 `gnb.bat`，macOS 打包参数换成 `macos`、归档名换成 `gknextrenderer_macos_<tag>.7z`。
`verify-package.py` 需要 Python 3.11+。每次使用新的或已清空的 `release-smoke` 目录：

```bash
./gnb.sh build --tracy=off gkNextRenderer gkNextEditor ScadLibrary NextAstrobot Brotato3D Packager
```

```bash
gnb package windows --package-preset default --trace-assets --version v0.1.2.0
```

```bash
gnb smoke gknextrenderer_win64_v0.1.2.0.7z --launch --keep --staging release-smoke
python tools/release/verify-package.py release-smoke
```

`gnb smoke` 会把 7z 解压到一个干净目录并验证：包内没有 `.pdb` / `.ilk` 等构建产物、
必需资产齐全、package manifest 中声明的可执行文件都能启动（`--help`）。加 `--launch` 时还会真实运行每个 target
并等待引擎日志里的 `committed scene`（需要可用的 Vulkan 设备）。

---

## 2. 打 tag 触发 CI

发布由 tag 触发（`.github/workflows/release.yml`，匹配 `v*`）：

```bash
git tag v0.1.2.0 && git push origin v0.1.2.0
```

CI 会依次执行：

1. `make-release` — 用 `docs/releases/next-release.md` 创建 **draft** GitHub Release；不会自动正式发布
2. `linux-build` — 写入版本、构建桌面目标，并在 `Xvfb + Lavapipe` 软件 Vulkan 环境中对
   `default` 运行 `--trace-assets`。它把 Linux 的 `default` 包上传至草稿，并上传平台无关的完整精确资产包
   （`runtime.pak`、资产列表、manifest 与本轮 `brotato3d.pak`）供后续 job 使用，
   三平台沿用同一份 Brotato 包，避免不同时间下载产生差异。
3. `windows-build` / `macos-build` — 在 Linux 资产追踪完成后构建各自的可执行文件，下载对应
   preset 的精确资产包并通过 `--runtime-pak` 组装归档；不在无 GPU runner 上运行 Vulkan 追踪：
   - 写入 `src/build.version`
   - `gnb setup`
   - `gnb build` 所有该归档需要的 target
   - Windows 与 macOS 均打包 `default`
   - `gnb smoke <7z> --keep --staging release-smoke`（结构冒烟；不带 `--launch`）
   - `python tools/release/verify-package.py release-smoke`：核对五个 target，以及仓库中全部
     SCAD / catalog / 游戏配置 / 额外资产与解包后的明文文件逐字节一致
   - 上传产物到 **draft** Release
4. `draft-ready` — 三平台全部成功后输出交接摘要；草稿仍不转正

三个桌面 Release job 与 `desktop.yml` 使用相同 runner：`ubuntu-24.04`、`windows-latest`、
`macos-latest`。它们也使用相同的 `gnb info --bincache-key` 和 `.vcpkg` / `.vcpkg_bincache`
缓存路径，因此 Release 可以直接复用日常 Desktop CI 已生成的 vcpkg 二进制缓存。
Android 当前不属于发布支持平台，也不会产生 Release 产物。

### 产物命名

| 平台 | 文件名 |
|---|---|
| Windows | `gknextrenderer_win64_<tag>.7z` |
| Linux | `gknextrenderer_linux64_<tag>.7z` |
| macOS | `gknextrenderer_macos_<tag>.7z` |

### 包内结构

```
bin/          五个程序 + 运行时 DLL/共享库 + 厂商 license 文本
assets/paks/  runtime.pak + 可审计的运行时资产清单 + brotato3d.pak（正式发布前须替换内容）
assets/scad/  完整明文源树：lib/catalog.json、kit、characters、source、proc、evaluated 等
assets/configs/nextastrobot/       全部关卡与玩法配置
assets/sounds/astrobot/            完整音效（不能只依赖启动采样）
assets/projects/Brotato3D/Content/ 全部 Brotato3D 游戏配置
package.manifest.json  preset、平台、版本和目标清单
README.txt    启动方式 / 系统要求 / 已知问题 / 反馈入口
LICENSE
THIRD-PARTY-NOTICES.md
```

默认发布包不携带 `gnb` 命令行工具。仅在需要随包提供 AI agent sidecar 时显式传入
`--include-gnb`；打包器届时会加入 `bin/gnb[.exe]` 和 `bin/gnb-agent-manifest.json`。

打包目标由 `gnb.toml` 的 `[package.presets.<name>]` 配置；`default` 是标准五程序发布，
仓库也可以保留供本地或按需交付使用的其他 preset，但 tag 触发的 Release 只发布 `default`。
通过 `--package-preset <name>` 选择，省略时使用 `[package].default_preset`。所有 preset 共用同一套 trace、runtime.pak、sidecar、文档和 7z 流程。

推荐的精确模式 `--trace-assets` 会依次以隐藏 Agent Validation 模式运行
`gkNextRenderer`、`gkNextEditor`、`ScadLibrary`、`NextAstrobot`、`Brotato3D`，合并 `FileHelper` 成功解析的磁盘文件与
实际命中的 Pak 条目，排序去重后生成单一 `assets/paks/runtime.pak`。发布 7z 同时携带
`runtime-assets.txt` 和 `runtime.manifest.json`，便于审计文件名与压缩后大小。已有的多轮覆盖清单
可用 `--asset-trace <path>` 直接复用；这适合把代表性场景和交互脚本的结果合并后交给无 GPU 的 CI。
完整的 `runtime.pak`、资产列表与 manifest 也可通过 `--runtime-pak <目录>` 原样复用；Release CI
用 Linux 的 Lavapipe 生成它们，因此 Windows/macOS 只需组装，不依赖各自 runner 的 Vulkan 设备。
Packager 会从原始资产目录或已有 Pak 中提取每个命中项，不会把整个可选 Pak 嵌套进新 Pak。
场景列表、内容浏览器、`IsAssetAvailable` 和 Pak 条目枚举只属于发现/存在性探测，不计入覆盖；
只有具体磁盘文件路径被解析或 `LoadFile` / `LoadMountedFile` 成功读取时才记录。
编译后的 `assets/shaders/**/*.spv` 是例外：渲染器可在运行时切换，采样期间未启用的渲染器仍是
发布功能，因此精确打包会无条件合并全部 SPIR-V 文件。场景扫描还会用当前安装目录和已挂载 Pak
再次校验逻辑资产路径，避免旧的按需解包缓存让未随当前版本发布的场景重新出现在选择器中。
其他必须进入精确包、但不保证在采样流程中加载的资产，分别配置在各 preset 的
`always_include_assets`。当前 `default` 固定包含 `conf_room.glb`、`pbr.glb` 和 `playground.glb`。

`extra_files` 在 trace、复用 runtime.pak 和目录白名单三种模式下均生效。`assets/scad/`
整树以普通文件提供，不能只留下启动时读取过的 SCAD 或仅封进 pak；相对路径保留以保证
`use` / `include`、Kit 索引、角色与跨关卡引用可用。同名但不同目录的文件分别保留。
额外的 `assets/` 路径缺失即打包失败。游戏配置和 Astrobot 全部音效也按此方式固定携带；
Brotato 的完整音效 / 图标包单独保留，覆盖战斗、升级与商店等启动采样未走到的功能。

精确包只保证覆盖采样时实际走到的功能。发布前应以代表性的场景与交互流程扩充清单，并始终执行
`gnb smoke <7z> --launch`。不传上述两个参数时仍保留目录白名单模式，作为 CI 与问题排查的保守回退。

桌面发布归档使用 7z/LZMA2 最高压缩级别、128 MiB 字典和 solid 模式。gnb 依次查找
`GNB_7Z`、PATH 中的 `7zz` / `7z` / `7za`，Windows 还会查找标准的 `Program Files/7-Zip`。
`gnb smoke` 仍可读取旧 `.zip`，新的桌面发布物统一输出 `.7z`。

---

## 3. 发布后验收

在**干净机器**（不含仓库、不含开发工具）上，三平台各做一次：

1. 解压 → 双击 `bin/gkNextRenderer`
2. 依次切换全部渲染器
3. 截图一张，确认输出落在用户目录（不是安装目录）
4. 打开 `gkNextEditor`，确认首屏加载了默认场景
5. 打开 `ScadLibrary`，浏览 Kit、场景、地形、角色；编辑并保存 SCAD，重开确认修改保留
6. 运行 `NextAstrobot`，遍历配置中的关卡，确认跳跃、移动平台、收集与重生
7. 运行 `Brotato3D`，检查角色 / 武器选择、战斗、波次、升级、商店与音效
8. 全程无崩溃框、无资源缺失或红色 error 日志

验收记录（截图 + 日志）归档到该次 Release 的说明或 issue 里。

正式发布执行顺序：

1. 替换 Brotato 参考素材，重新生成 pak 并更新 `THIRD-PARTY-NOTICES.md` 和本次 Release Notes。
   将替换后的 pak 更新到 `gnb.toml` 所指的资产发布源；pak 本身被 gitignore，只有本机重建不能改变 CI 下载的内容。
2. 提交改动，选择新的 `v*` tag，推送并等待本工作流生成三平台草稿归档。
3. 从草稿下载归档，完成上述干净机器验收；Windows NVIDIA 上按 AGENTS.md 单独检查真实 DLSS。
4. 确认三个附件属于替换素材后的同一提交，删去草稿说明中的待办，补充版本亮点与已知问题。
5. 在 GitHub Release 页面人工点击 Publish release。本次改动不推 tag，也不执行公开发布。

失败处理：构建 / trace / 冒烟 / 明文比对任一步失败都不发布；修复后可重跑失败 job。
若源代码或素材已变化，使用新 tag 全部重建，不混用旧 Linux runtime.pak 与新平台二进制。

---

## 4. Release Notes 模板

```markdown
## gkNextRenderer <version>

### 亮点
- （3–5 条，面向用户，不写内部重构）

### 变更
- （按 renderer / editor / ScadLibrary / games / 工具链分组）

### 已知问题
- （已知但本次不修的问题，附 issue 链接）

### 系统要求
- Windows 10/11 x64 · Linux x86_64（使用 Ubuntu 24.04 runner 构建；需在目标发行版实测） · macOS arm64
- 支持 Vulkan 1.3 的 GPU；硬件光追（RTX 20 系 / RX 6000 及以上）可启用 PathTracing，
  其余设备自动回退到软件渲染器
- 请更新到显卡厂商的最新驱动

### 文件写入位置
日志、设置、截图与烘焙缓存写入用户数据目录（Windows: `%APPDATA%\gkNext\<app>`）。
需要绿色版时，在 exe 上级目录放一个 `portable.txt`，所有写入会回到安装目录。

### 反馈
https://github.com/gameknife/gkNextRenderer/issues
```

---

## 5. 回滚

Release 出现阻断问题时：

1. 在 GitHub 上把该 Release 标为 **pre-release** 或直接删除，避免继续被下载
2. **不要删除 tag**（已有人 clone/引用时会造成更混乱的状态）；在下一个 patch 版本修复
3. 修复后发新 tag（`v0.1.2.1`），在新 Release Notes 里说明上一版的问题
4. 把导致回滚的问题补成一条冒烟检查（优先加进 `gnb smoke`），避免同类问题再次流出

---

## 6. 相关文件

| 文件 | 作用 |
|---|---|
| `.github/workflows/release.yml` | tag 触发的三平台草稿流水线 |
| `gnb.toml` | 五个封包 target、固定资产与明文文件目录 |
| `tools/release/verify-package.py` | 解包后与仓库逐文件比对 |
| `docs/releases/next-release.md` | 本次草稿 Release Notes |
| `.github/workflows/desktop.yml` | PR / main 的构建 + 测试 |
| `tools/gnb/internal/packager/packager.go` | 打包清单、资产白名单、包内 README |
| `tools/gnb/internal/packager/smoke.go` | 解压即用冒烟校验 |
| `THIRD-PARTY-NOTICES.md` | 第三方 attribution，随包分发 |
