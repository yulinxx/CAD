# Main → UiShell 迁移计划

> **状态唯一事实源：本文。** `Docs/01-当前架构/UiShell框架迁移.md` 只做摘要并回指本文。
>
> 当前状态（2026-11-28）：M0–M6 已完成。M0–M4 的框架/脚手架（契约层、shell 运行时、
> `UiDialogs`、客户库、ports & adapters、命令适配）已落地；M3 修掉了栈上 adapter 悬空导致的
> 启动崩溃；M5 数据化已完成（profile 驱动品牌/关于框/六套主题/默认语言/外置资源）；
> M6 已把 `April` 生产入口切到 UiShell `AppBootstrap`，旧 UI 实现移到测试专用目录。
> **M3 2D 交互语义与 M4 菜单点击/快捷键的真实 GUI 运行时验收仍待补**（offscreen 环境限制，见 §0.5）。
> `SampleFrame/` 是只读参考工程，任何改动都不落到该目录。
>
> 目标是把现有 `Main` 的 UI 外壳改成 SampleFrame 的架构：`UiContract` 契约层 +
> `UiShell` 运行时 + `UiDialogs` 共享对话框 + `customers/<产品>` 客户库 + 薄 EXE。

## 0. 策略：绞杀者，不推倒重来

现有 `UI + Main` 约 16.5 万行，直接删除重写的风险不可控。采用并行迁移：

1. 新外壳 `UiShell/` 先作为根工程子目录存在，旧 `April` 应用保持可运行；
2. 每迁移一块，就在新外壳里替换掉一块旧实现；
3. 全部功能对齐后，`April` 入口改为链接新外壳（M6 已完成）；旧 UI 生产副本移到
   `Main/Src/UI/Test/LegacyUI/`，只由 `MainTests` 编译，视为生产侧删除。

回滚方式：`-DBUILD_UI_SHELL=OFF` 回到未启用新外壳的构建形态；M6 后的完整生产回退需要恢复
   旧入口源文件（已删除）与旧 UI 生产路径（现位于测试专用目录）。

## 0.5 Step 0 基线（已验证，2026-11-27）

本节的命令是后续每个里程碑的**固定验收底线**；任何提交都必须保持它们全绿。
`UI_CUSTOMER=April` 是必须显式指定的：`UiShell` 默认客户是 `MantiSoft`，
不指定时根本不会生成 `AprilShell` / `UiApril` 目标。

一键脚本（已固化进 `.github/workflows/ui-shell-baseline.yml`；脚本放在
`UiShell/ci/`，因为仓库的 `scripts/` 被 `.gitignore` 排除，放那里 CI 取不到）：

```bash
bash UiShell/ci/ui-shell-baseline.sh       # BUILD_DIR / BUILD_TYPE 可覆盖
```

等价的展开步骤：

```bash
cd <repo>
cmake -S . -B build -G Ninja -DBUILD_UI_SHELL=ON -DUI_CUSTOMER=April -DBUILD_TESTS=ON
cmake --build build --target AprilShell UiApril AprilUi AprilCore April
cmake --build build               # 测试目标也要跟上配置变化，否则指纹用例会读到旧宿主值
ctest --test-dir build -R "UiTst|UiLayerCheck" --output-on-failure
python3 UiShell/ci/ui-shell-smoke.py
```

本机（macOS / Qt 6.12.0 / AppleClang 21 / arm64 / Release）实测结果：

| 项 | 结果 |
| --- | --- |
| configure（依赖方向审计 + UI→Engine include ratchet） | 通过 |
| `ninja AprilShell UiApril AprilUi AprilCore April` | 通过；产物 `build/bin_Qt6/shell/Release/AprilShell`、`libUiShell.dylib`、`libUiApril.dylib` |
| `ctest -R "UiTst|UiLayerCheck"` | **25/25 通过**（24 个 `UiTst*` 单测，含 `UiTstAprilMenuCatalog`、`UiTstProfileCommands` + `UiTstInteraction` + `UiLayerCheck`） |
| 旧 `April`（`QT_QPA_PLATFORM=offscreen`） | 进入事件循环，未退出 |
| `AprilShell` 最小 profile 冒烟（`--profile=`，无 CenterView） | **通过**：进入事件循环；同时验证 `--profile` 命令行优先级 |
| `AprilShell` 完整 `ui.json`（offscreen） | 仍会崩在 `MetalSurface` 创建：offscreen 平台没有原生窗口，属环境限制，不是 UiShell 代码缺陷；真实 macOS 窗口下另行验收 |
| `SampleFrame/` 只读 | 无 tracked 文件，且全目录 SHA-256 快照前后一致（456 个文件） |

Step 0 为使 April 满足共享集成测试契约，在 `customers/April/CustomerModule.cpp`
注册了共享默认 `dialog.about` / `dialog.sample.params` 工厂，并登记 `sample.run`
命令元数据；**不新增 CAD 功能、不改 shell 核心**。M3 运行时修复见 §3。

`AprilShell` 原先“profile 加载后立即崩溃”的**根因已定位并修复**：
`wireCompositionRoot()` 曾在栈上构造 `CompositionRootAdapter` 并把它（连同内部的
`DefaultViewportFactory`）注入 `AprilWorkbench`；函数返回后这些指针悬空，第一次
创建视口即跳到非法地址。现在 adapter 由 `ModuleParts.wiring` 长期持有，
`AprilWorkbench` 只经 `IViewportServices` / `ICadViewport::bindServices` 使用抽象视图，
不接触 `ApplicationCompositionRoot` 或具体算法对象。

全新的空 build 目录当前会卡在 vcpkg baseline（`versions/baseline.json` 在本地 vcpkg
提交中缺失），因此基线验收使用已配置好的 `build/`；该环境问题单独处理，不要误判为框架回归。

## 1. M0 已完成的内容

| 项 | 结果 |
| --- | --- |
| 新子系统 | `UiShell/`（源码来自 SampleFrame，构建脚本已适配根工程） |
| 根工程接入 | `CMakeLists.txt` 新增 `BUILD_UI_SHELL`，默认 `ON` |
| 产物 | `build/bin_Qt6/shell/Release/MantiSoft`、`libUiShell.dylib`、`libUiMantiSoft.dylib` |
| 测试 | 24 个 Qt Test + `UiLayerCheck`，共 25 项全部通过 |
| 旧应用 | `April` 目标未改动；configure 的依赖方向审计仍通过 |

### 移植时修掉的三个问题

1. **CMake 子目录化**：不再调用 `project()`、不再加载 `../configure.cmake`，
   Qt/编译器/vcpkg 全部继承根工程；输出落到 `bin_Qt6/shell`。
2. **生成文件不在源码树**：`UiVsSourceGroup` 先过滤，再 `source_group(TREE)`，
   避免 `UiCustomerProfile` 生成的 `.qrc` 触发 configure 失败。
3. **跨 dylib RTTI**：根工程全局 `-fvisibility=hidden` 会让纯头文件接口的
   `dynamic_cast<IFoo*>` 在 macOS/Linux 上静默失败。外壳子目录改为
   `default` 可见性，导出面仍由 `Export.h` 控制。

## 2. 目标架构映射

| 旧实现 | 新实现 | 备注 |
| --- | --- | --- |
| `Main/main.cpp` + `Runtime/CADApplicationEntry` | `UiShell/main/main.cpp` + `AppBootstrap` | 薄入口 |
| `Main/Src/UI/Workbench/WorkbenchWindow` | `customers/April` 的 `ICenterViewProvider` | 中央视图 |
| `Main/Src/UI/Workbench/Workbench2D/3D` | `customers/April` 的中央视图实现 | 逐步搬迁 |
| `Main/Src/UI/Services/UiServices` | `UiShell` 的 `ShellServices` + 客户持有 | 服务注入 |
| `Main/Src/UI/ClientConfig/*.json` | `customers/April/ui.json` | profile 流水线 |
| `Main/Src/UI/ClientConfig/UiBrandManager` | `ui.json` 的 `titleBar`/主题/资源 | 品牌数据化 |
| `Main/Src/Composition/*Registry` | 客户 `ICommandCatalog` / `ICommandInvoker` | 命令表 |
| `Main/Src/UI/Workbench/WorkbenchMenuManager` | `UiShell` 的 `MenuController` | 菜单 |
| `UI/Common/Src/Dlg/*` | `UiDialogs` 或客户对话框工厂 | 对话框 |
| `UI/Common/Src/ThemeManager` | `UiShell` 的 `ThemeController` + QSS | 主题 |
| `Main/Src/Runtime/LicensingBootstrap` | 客户模块 `createCustomerModule()` 前 | 授权 |

## 3. 里程碑

### M1：把现有 CAD UI 提取成库（已完成，2026-11-27）

目标：让新外壳能链接现有工作台，而不是重写。

**做法**（`Main/CMakeLists.txt`）：

- 从收集到的 `MAIN_APP_SOURCES`（已做 Test/Raster/CAM 过滤）里把
  `Main/Src/UI/**` 切出 `APRIL_UI_SOURCES`，其余保留 `MAIN_APP_SOURCES`；
  切分前的完整列表保存为 `MAIN_ALL_SOURCES`，供 `MainTests` 仍然整体编译。
- 新增 `AprilUi` **STATIC** 库，源码为 `Main/Src/UI/**`（已排除 `Test/`）。
- `April` 可执行目标改为：非 UI 源码 + 链接 `AprilUi`；`main.cpp` 不变。
- `April` 与 `AprilUi` 共用 `april_apply_app_target_settings()`，include、宏、
  链接依赖完全一致，避免编出 ABI 不一致的实现。
- `MainTests` 维持原样（编译全部源码，不链接 `AprilUi`），避免 UI 符号两份。

**已处理的坑**：

- **Q_OBJECT 头只能属于一个目标**：`ExampleCustomPanel.h` / `Workbench3D.h`
  从 `April` 移到 `AprilUi` 的源码，否则 moc 产物重复定义。
- **`Q_INIT_RESOURCE` / qrc**：qrc 仍只编进 `April` 与 `MainTests`；
  `AprilUi` 关闭 `AUTORCC`，资源在进程内共享，不需要重复注册。
- **Unity build**：`AprilUi` 与 `April` 分成两个 target 会重新分批；
  已确认 `-Werror` 下干净编译。
- **RTTI / 可见性**：`AprilUi` 设 `CXX_VISIBILITY_PRESET default`，与 `April` 一致。
- **`MainTests` 源集合**改用拆分前的 `MAIN_ALL_SOURCES`。

**验收（在临时 stash 掉无关的 `Engine` 子模块 WIP 后，用已提交的 Engine 状态验证）**：

1. `cmake -S . -B build` 配置成功；
2. `ninja April` / `cmake --build build --target April` **exit 0**，`AprilUi` 产出 `libAprilUi.a`；
3. `ninja MainTests` **exit 0**；`MainTests --gtest_filter='ViewportInputRegressionTest.*'` **28/28**；
4. `ctest -R "UiTst|UiLayerCheck"` **25/25**；
5. `April` 可启动（offscreen，正常初始化 DB / 语言 / 许可 / UI）；
   `nm -C April` 能看到 `WorkbenchWindow::*` 等 `AprilUi` 符号；
6. `Engine` 子模块 WIP 已 `stash pop` 恢复原样。

**注**：`MainTests` 仍编译全部源码（含 M6 起位于测试专用目录的旧 UI），未切到链接 `AprilUi`（见 §3 M6）。


### M2：建立 `customers/April` 客户模块（已完成，2026-11-27）

**做法**：

- `UiShell/customers/April/`：
  - `CustomerModule.cpp`：`createCustomerModule()` + `customerBuildFingerprint()`；
    `strId=april`、`strDisplayName=April`、`strProfilePath=:/UiApril/profile/ui.json`；
    `Q_INIT_RESOURCE(UiAprilProfile)`（静态链接兜底）；`centerView` 指向本客户实现。
  - `panels/PanelFactory.*`：`CenterView : AA::ICenterViewProvider`，M2 返回空 `QWidget`；
    **M3** 改为返回现有 CAD 工作台/视口宿主。
  - `ui.json`：version 2 最小集（titlebar / menubar / CenterView / StatusBar）。
- `UiProjectDefaults.cmake`：`UI_CUSTOMER` 增加 `April`；`UI_CUSTOMER_TARGET=UiApril`；
  customer 目录 `customers/April`；宿主 EXE 用 **`AprilShell`**（避免与 `Main` 的 `April` 目标冲突；M6 已同时把 `April` 生产入口切到同一 `AppBootstrap` 流程）。
- `UiApril`（`msUiAddCustomerLibrary`）链接 `AprilUi`（`if(TARGET AprilUi)`）。

**暂未做（后续里程碑）**：

- `commands/Commands.*`：`ICommandCatalog` / `ICommandInvoker` → 现有命令表（**M4 已完成**，见 §3）。
- `ui.json` 由 `configs/base.json` + `april.json` 转换（**M4 已完成**：8 菜单覆盖 base.json 全部 id；`panels` 仍为空，属 M5）。
- `assets/`、`translations/`（**M5**）。
- 完整资源注册：`AprilUi` 里的 `:/configs/...` 等 CAD 资源在 `April` 可执行目标注册；
  `AprilShell` 的 profile/translations/运行时资源在 **M6** 由 `UiShell/CMakeLists.txt` 的
  `AprilRuntimeDeploy ALL` 统一部署（M2 空中央视图不依赖它们，当时已可构建/启动）。
- 完整 exe 替换（**M6 已完成**）。

**验收（临时 stash `Engine` WIP，用已提交 Engine 状态；验证后已 `stash pop` 恢复 50 文件）**：

1. `cmake -S . -B build -DUI_CUSTOMER=April` 配置成功，`customer : April`；
2. `ninja UiApril` **exit 0** → `libUiApril.dylib`（链接 `AprilUi`）；
3. `ninja AprilShell` **exit 0** → `AprilShell`（新外壳 + 空中央视图，profile 外置到 `<exe>/profile/ui.json`）；
4. `AprilShell` offscreen 启动，加载 `UiAprilProfile`；
5. `Engine` 子模块 WIP 已恢复。

**注**：M1 里 `AprilUi` 已链接；M2 只验证 `UiApril` 可链接 `AprilUi`，CAD 视口/命令/资源在 M3/M4/M5 接入。

### M3：视口接入（脚手架已移植；启动崩溃已修，交互运行时仍未验收）

**做法**（`UiShell/customers/April`）：

- 新增 `panels/AprilWorkbench.{h,cpp}`：
  - `AprilWorkbench : AA::ICenterViewProvider`；
  - `createCenterView()` → `createCenterView2D` / `createCenterView3D`：
    - **2D**：返回现有 `RenderViewport2D`（`Main/Src/UI/Render/RenderViewport2D.h`，`AprilUi`），
      注入 `setDocument` / `setSelectionService` / `setOperationBus` / `setLayerManager`，
      再 `initializeTools()`；`objectName=center.view`；
    - **3D**：返回现有 `Viewport3D`（`Main/Src/UI/Render/UiViewport3D.h`，**类名是 `Viewport3D`，不是 `UiViewport3D`**），
      `setRenderer(std::make_unique<RenderWidget3DAdapter>())`、`setSceneDocument`、`setCameraController`、`initialize()`；
      未开 `BUILD_UI3D` 时保留调用点、退回空中央视图；
  - `ViewportKind { TwoD, ThreeD }`；
  - 视口所需服务由 `customers/April` 持有并注入：
    `UiServices`（七分组装配容器，`setUiServices` 同步 `workbench()`）、`WorkbenchServices`、
    `SceneDocument2D`、`SceneDocument3D`、`UiStateCenter`、`OperationBus`、`ISelectionService`、`LayerManager`、`CameraController3D`；
  - 与 `UiShell` 的 `AA::ShellServices` 互转：`populateShellServices()` / `applyShellServices()`；
  - 不重写工作台，复用 `AprilUi` 现有实现。
- `PanelFactory.h`：`CenterView : public AprilWorkbench`（M2 空 `QWidget` 已替换为 M3 视口宿主）。
- `CustomerModule.cpp`：`module.centerView` 指向 `CenterView`（即 `AprilWorkbench`）。
- `Main/CMakeLists.txt`：`AprilUi` 的 include 目录与 `UICommon/UI2D/Engine*/Engine3D` 链接改 `PUBLIC`，
  供 `customers/April` `#include "UI/..."`。

**真实 API 名（不重新发明）**：`RenderViewport2D`（`QWidget` + `UI::IViewportHost`）、`Viewport3D`、
`RenderWidget3DAdapter`、`UiServices`、`WorkbenchServices`、`SceneDocument2D`、`UiStateCenter`、`OperationBus`、`ISelectionService`。

**暂未做 / 本轮不保证**：

- **编译**：Step 0 已在当前 `Engine` 工作区状态下实测 `ninja AprilShell UiApril AprilUi AprilCore April` 通过；此项从“未验证”关闭。
- **启动崩溃**：已修复（adapter 生命周期，见 §0.5）；最小 profile 冒烟进入事件循环。
- **组合根**：`ApplicationCompositionRoot` 在 `Main/Src/Composition`（不在 `AprilUi` 内）。
  `attachCompositionRoot()` 现在把 adapter / viewportFactory 交给 `ModuleParts.wiring` 持有，
  `wireCompositionRoot()` 经 `IViewportServices` 真注入，并把 `SceneDocument2D` /
  `OperationBus` / `ISelectionService` / `LayerManager` 转发给 `DefaultViewportFactory`；
  不再有栈上悬空指针。
- **资源注册**：`AprilUi` 的 `:/configs/...` 等在 `April` 可执行目标注册；`AprilShell` 的
  profile/translations/运行时资源由 M6 的 `AprilRuntimeDeploy ALL` 部署。
- **验收（待真实窗口环境）**：2D 绘图、选择、变换、撤销重做在新外壳内可用；offscreen 无法创建 `MetalSurface`，不能作为此项验收环境。
- **`--profile`**：`StartupSession` 现支持 `--profile <path>` / `--profile=<path>`，显式路径优先且不存在即失败，冒烟脚本依赖该约定。

**注**：原 M3 文本写 `UiViewport3D`，实际类为 `Viewport3D`（头文件 `UiViewport3D.h`）。

### M4：菜单、工具栏、命令、快捷键（菜单/目录/外置 profile 同步已完成；运行时待验收）

**做法**（`UiShell/customers/April`）：

- 新增 `commands/Commands.{h,cpp}`：`Apr::Commands : AA::ICommandCatalog + AA::ICommandInvoker + AA::ICommandState`
  - `find()`；`invoke()` → `ICommandSink::dispatch(commandId, {})`；
    `invokeWithArgs()` → `dispatch(commandId, QJsonObject→QVariantMap)`（透传/补齐，不整体覆盖）；
  - `queryState()` → `isCommandRegistered` → `bEnabled`；`addCommand(AA::CommandView)`；`setSink()`；
  - **不重写命令系统**，复用 `CommandCatalog` / `OperationRouting` / `OperationBus` / `OperationRegistry`。
    M6 后生产路径不再经过旧 `IUiCommandDispatcher`。
- `CustomerModule.cpp`：`ModuleParts` 持有 `Commands commands`；`module.cmdCatalog/cmdInvoker/cmdState = &parts->commands`。
- `AprilWorkbench::applyShellServices` / `populateShellServices` 已转发 `cmdCatalog/cmdInvoker/cmdState`。
- **菜单转换已完成**：`customers/April/ui.json` 的 8 个菜单覆盖 `configs/base.json` 的全部 184 个 id
  （`action→command`，`command→commandId`，`label→titleKey`，`icon→iconKey`，保留 `separator`/`submenu`）。
- **目录登记**：`Commands::addCommandsFromProfile()` 从客户 profile 的 `menus` 递归登记所有
  `type=command` 节点；`createCustomerModule()` 读取内嵌 ui.json 调用它。否则 `AssemblyPlanner`
  会把菜单命令当“未知命令”剔除。新增 `UiTstAprilMenuCatalog` 单测校验嵌套菜单、元数据、
  去重，以及**随附 ui.json 的每个菜单命令都能在目录里找到**；实测完整 profile 启动
  的 unknown-command 警告为 0。
- **外置 profile 同步**：契约新增可选 `AA::ICommandProfileRegistrar`，`ProfilePipeline::loadFromJson()`
  在规划前调用它；四套客户的 `Commands` 均已实现（April 复用 `addCommandsFromProfile()`，
  其余继承 `CommandTable` 的默认实现）。因此 `--profile` 指向的站点 ui.json 即使引入新命令 id，
  目录也会在加载时补齐，无需重编或改客户代码。新增 `UiTstProfileCommands` 验证“有/无登记钩子”
  的装配诊断差异、元数据与幂等性；端到端用外置 profile 启动 `AprilShell`（offscreen + 最小 profile）
  实测 unknown-command 警告为 0。

**暂未做**：

- M6 后命令出口改为 `ICommandSink` / `OperationCommandSink`：主题、语言、`help.about`、
  `view.switch_to_*` 走 `IShellRuntime`，2D 命令走 `CommandCatalog` / `OperationRouting` /
  `OperationBus`；不再依赖 `UiShellHost` / `UiWorkbench`，`UiApril` 可单独构建与链接。
- 菜单点击/工具栏/面板按钮/快捷键“走同一条命令路径”的**运行时验收**，待真实窗口环境验证
  （offscreen 下完整 profile 会崩在 `MetalSurface`，见 §0.5）。

### M5：品牌、主题、i18n、部署（数据化完成）

- **品牌元数据**：profile 新增可选 `brand` 段（`product` / `organization` / `icon` / `theme` /
  `language` / `copyright` / `website` / `email` / `license` / `logo`）；`ProfileParser` 校验类型
  并忽略未知字段，`ProfilePipeline::brand()` 暴露给运行时。`ShellRuntime` 在加载后应用：
  `product` 覆盖窗口标题、`organization` 写 `QCoreApplication`、`icon` 设置窗口图标；
  品牌字段同时转成 `ShellServices::brandParams`，`DialogController` 用它填充关于框等共享对话框
  （显式 dialog binding 优先）。未声明 `brand` 时完全沿用客户模块与命令行设置。
- **相对资源**：`titleBar.icon` / `brand.icon` / `brand.logo` 的相对路径按 `ui.json` 所在目录解析
  （`:/...` 与绝对路径原样保留）；自绘标题栏与顶层窗口用同一解析结果。
- **主题**：`ThemeController::apply()` 叠加三层：框架 `:/UiShell/styles/`、客户 qrc、
  `<profileDir>/styles/`；`ShellServices::strStyleDir` 由 `loadFromFile()` 填充。六套皮肤
  `light` / `dark` / `blue` / `slate` / `high_contrast` / `system` 全部可用：后四套是薄差异层，
  经 `ThemeCatalog::baseId()` 先套 `light`/`dark` 基础 QSS，保留 `msuiRole`/`msuiSeries` 契约；
  三处内置主题菜单统一遍历 `ThemeCatalog::entries()`。
- **i18n**：显式 `--lang` 仍优先（`AppBootstrap` 在客户模块加载前安装）；未指定 `--lang` 时
  用 `brand.language` 走 `ShellRuntime::switchLanguage()`，并可读取 `<profileDir>/translations/`。
  六个主题菜单文案用 `QT_TRANSLATE_NOOP` 登记并补入 6 种语言的 `.ts`。
- **部署**：`UiDeployProfile()` 除 `ui.json` + `assets/` 外，客户目录自带的 `styles/`、
  `translations/` 也复制到 `<exe>/profile/`。
- **验收**：`UiTstProfileLoader` 覆盖 brand 解析（含非法类型），`UiTstInteraction` 覆盖外置
  标题/图标/皮肤、关于框品牌参数覆盖优先级、六套皮肤全部可应用；端到端用临时 profile
  （含 `assets/` + `styles/`）启动 `AprilShell`（offscreen + 最小 profile），事件循环正常、
  unknown-command 告警 0。
- **旧 UI 硬编码**：`WorkbenchWindow.cpp` 等无需再抄进 UiShell；M6 已把它们移出生产路径，
  仅保留在测试专用副本中。旧 `UICommon` 的六套 QSS 未逐字搬入，UiShell 用的是等价的薄差异层。

### M6：切换入口，旧 UI 生产路径删除（已完成，2026-11-28）

- **生产入口**：`April` 目标改用 `UiShell/main/main.cpp` 的 `AppBootstrap`，定义
  `APR_PRODUCT_STARTUP=1` 以调用 `AprilShellStartup` 中的产品专属前置逻辑（OpenGL/macOS、CLI、
  初始化、资源、崩溃处理、授权、退出保存、关停）；`AprilShell` 仍是不带产品钩子的薄 EXE。
  旧的 `main.cpp`、`AppBootstrapper.*`、`CADApplicationEntry.cpp`、`CADApplicationRuntime.*` 已删除。
- **宿主注入**：`AppBootstrap` 在创建客户模块前通过 `IHostWindow` 发布宿主 `QMainWindow`；
  `ShellRuntime` 通过 `IActiveRuntime` 发布当前运行时。`ApplicationCompositionRoot` 去掉
  `UiShellHost` 成员，改为消费主窗口；`OperationRegistrar` 已从 `m_root.mainWindow()` 取窗口。
- **命令出口**：`OperationCommandSink`（轻量接口 `ICommandSink`）替代 `IUiCommandDispatcher`，
  使独立菜单目录测试仍只需编译 `Commands.cpp`。`CompositionRootAdapter::dispatchOperation()`
  也委托给该 sink。
- **测试专用旧 UI**：`UI/ClientConfig`、`UI/Workbench`、`UiShellHost`、`DevicePanel` 等的
  实现/头文件移到 `Main/Src/UI/Test/LegacyUI/`；生产源集合和 qrc 过滤 `/Test/`，`MainTests`
  显式加入这些源码与 include 目录，原有覆盖继续运行。`Workbench3D.cpp` 里用户 WIP 的
  `setCompletionCallback` → `OperationBus::notifyOperationCompleted` 回调随重命名保留。
- **服务边界**：生产 `UiFeatureGate` 移到 `Main/Src/UI/Services/`，测试侧保留兼容头；
  `LicensingBootstrap.cpp` 改用新 include。
- **部署**：根 CMake 的部署调用因目标目录限制改由 `UiShell/CMakeLists.txt` 的
  `AprilRuntimeDeploy ALL` 承接，随 `April` 部署 profile/translations/运行时资源；安装规则
  同步安装 `UiShell`/`UiApril` 动态库与 `profile/`，避免装出来的 `April` 缺库或缺 ui.json。
- **April 工具栏/菜单栏回归修复**：`ui.json` 显式声明 `drawTools`（`left.main`）与
  `editToolbar`（`top.toolbar`），并开启 `window.nativeMenuBar` 让 macOS 使用屏幕顶部原生
  菜单栏。`Apr::PanelFactory` 从 2D 命令目录与 `props.commands` 取外壳 QAction，装配
  `DrawToolBarWidget` / `TopToolBar`；Select/Pan 切换经 `IShellHost::invoke` 回派。
  macOS 27+ 的 AppKit 默认隐藏 `NSMenuItem.image`，生产 April 在 `aprilShellAfterBootstrap()`
  调用 `ensureMacMenuImagesVisible()` 恢复原生菜单图标（旧路径原在测试专用的
  `WorkbenchMenuManager` 中调用）。`UiTstCustomerProfile` 覆盖该 profile 配置，
  `SampleFrame/` 仍未修改。
- **验收**：`BUILD_DIR=build bash UiShell/ci/ui-shell-baseline.sh` 通过（25/25 层检/单测 +
  `AprilShell smoke OK`）；`cmake --build build` 全目标构建通过；`Tools/package_smoke_test.py`
  的 staged 安装/运行/升级/卸载冒烟通过。默认 `-DUI_CUSTOMER=MantiSoft` 不生成
  `AprilShell` / `UiApril`。全量 `ctest` 为 4881/4891 通过；10 个失败集中在 SY 格式注册、
  Raster/CAM 运动规划与 Hardware 加工链路径（M6 未触碰，且与用户 WIP 子模块改动一致），
  需在这些 WIP 合入前单独复位验证。

## 4. 验收标准（每个里程碑都要满足）

1. `cmake -S . -B build -DBUILD_UI_SHELL=ON` 配置成功；
2. `ctest --test-dir build -R "UiTst|UiLayerCheck"` 全绿；
3. `April` 仍可构建、启动（M6 后与新外壳共用 `AppBootstrap`）；
4. 新外壳的 `UiLayerCheck` 不出现越界 include；
5. 不引入客户分支（`if (customer == ...)`）。

## 5. 风险与对策

| 风险 | 对策 |
| --- | --- |
| 提取 `AprilUi` 引发重复符号 / 静态初始化顺序 | 先只提取 UI 子集，逐目录加入；用 `nm` 检查重复符号 |
| 视口依赖 `UiServices` 的 16 个指针 | 先注入最小集合，其余按接口拆分（与 `UiServices收口计划` 联动） |
| 跨 dylib 类型擦除（`dynamic_cast`） | 契约接口保持纯头文件；非 Windows 用 default 可见性 |
| 新旧两套菜单/命令短期并存 | 以 `ui.json` 为唯一事实源，旧 `configs/*.json` 只读兼容 |
| 一次改动过大无法回滚 | 每个里程碑独立可构建、可测试；`BUILD_UI_SHELL=OFF` 即回滚 |

## 5.1 低耦合 + 算法/UI 分离（当前状态）

- `UiShell` core + `UiLayerCheck`：不依赖 `Main/April*`；customer 唯一差异 `customers/<X>`。
- `UiContract` 纯接口；`UiShell` 不知道 customer。
- `AprilCore`(算法) 不链 `UiShell`；UI 经 `AA::ICommand*` + `Apr::ICadViewport` + `Apr::IViewportServices` + `OperationBus`/`SceneDocument2D` 契约，不直接改 `SceneManager`。
- `Apr::IViewportFactory` + `Apr::RenderViewport2DAdapter`(2D) + `Apr::Viewport3DAdapter`(3D) + `Apr::CompositionRootAdapter`；旧 `RenderViewport2D`/`Viewport3D`/`ApplicationCompositionRoot` 关进 adapter。
- `customers/April` 唯一耦合 `AprilUi`/`AprilCore`；`UiShell` 不见。
- `AprilShell` 与 M6 后的 `April` 都不依赖 `Main::AppBootstrapper`（已删除）。
- 仍：`AprilWorkbench` customer 层持有服务用于适配；M3 交互运行时 / M4 菜单点击运行时待真实 GUI 验收。

同步 `Docs/01-当前架构/UiShell框架迁移.md`。

## 6. 本轮未做的事

- M3 的 2D 交互与 M4 的菜单点击/快捷键运行时验收，仍待真实 GUI 会话（offscreen 限制见 §0.5）；
- 生产路径未搬入旧 `UICommon` 的 QSS 原文；UiShell 使用数据化 profile 与等价的薄差异层；
- 未修改 `SampleFrame/`，未触碰用户 WIP（`.gitignore`、`Tools/`、小写子模块、`Docs/00-*`、`Docs/06-*`）。

后续在真实 GUI 会话补 M3/M4 运行时验收。
