# UiShell 框架迁移（SampleFrame 风格，低耦合 + 算法/UI 分离）

> **状态唯一事实源：`UiShell/MIGRATION.md`（本文只做摘要，不再维护另一套状态）。**
> M0–M6 已完成；M3 启动崩溃已修，但 M3 交互运行时与 M4 菜单点击/快捷键运行时仍待真实 GUI 验收。
> M5 数据化（brand/关于框/六套主题/默认语言/外置资源）已完成；M6 把 `April` 生产入口切到
> UiShell `AppBootstrap`，旧 UI 实现移到测试专用目录 `Main/Src/UI/Test/LegacyUI/`。
> Step 0 基线（2026-11-28）：`UI_CUSTOMER=April` 下 configure / build / 25 项
> `UiTst|UiLayerCheck` 全绿（含 `UiTstAprilMenuCatalog`、`UiTstProfileCommands`），`April` 与 `AprilShell`（最小 profile）均可进入
> 事件循环；一键门禁 `UiShell/ci/ui-shell-baseline.sh` 与 staged 打包冒烟均通过。
> 全量 `ctest`：4881/4891 通过；10 个失败位于未触碰的 SY/Raster/CAM/Hardware 路径，
> 与用户 WIP 子模块一致，需单独复位验证。
> 命令、产物与启动崩溃根因见 `UiShell/MIGRATION.md` §0.5。

## 1. 分层（已成立 + `UiLayerCheck` 守卫）

```
UiContract ← UiShell ← UiDialogs ← customers ← main
```

- **`UiShell` 核心**（`UiShell/include/UiShell`, `UiShell/src`, `UiShell/dialogs`, `UiShell/main`, `UiShell/cmake`）
  **不** include/引用 `Main/Src`, `Main/`, `Composition/`, `ApplicationCompositionRoot`, `AppBootstrapper`,
  `AprilCore`, `AprilUi`, `UiWorkbench`, `MainApp`。`UiShell` 不知道 customer；**无 `if(customer==...)`**。
  守卫：`UiShell/cmake/CheckLayers.cmake` + ctest `UiLayerCheck`。
- **`customers/April`（`UiApril`）** 是唯一允许耦合 CAD 算法/旧 UI 的层。

## 2. `AprilShell` / `April` / `AprilCore` / `AprilUi`

```
UiShell::AppBootstrap + UiShell::MainWindow + UiShell::ShellRuntime   ← AprilShell
customers/April (UiApril)
customers/April ──> UiContract + UiDialogs + UiShell
customers/April ──> AprilUi + AprilCore (+ Engine*)     // 仅 customer 层
AprilShell (薄 EXE) ──> UiShell + UiApril (+ UiDialogs)
April (Main EXE) ──> UiShell/main + AprilUi + AprilCore + AprilShellStartup  // M6 已切换
AprilCore ──x──> UiShell                                 // 算法不依赖 UI
```

- **`AprilCore`**：`Main/Src/**` 非 UI + 不含 `main.cpp`（`Composition/Import/Export/Persistence/Hardware/Runtime/Bootstrap/Common/Platform/CAM/License/Vision`）。**不链 `UiShell`**。
- **`AprilUi`**：`Main/Src/UI/**` 中的生产 UI 实现；复用 `WorkbenchWindow/Workbench2D/Workbench3D/RenderViewport2D/Viewport3D/...`。
  旧实现已移到 `Main/Src/UI/Test/LegacyUI/`，生产源与 qrc 过滤 `/Test/`，只有 `MainTests` 编译它们。
- **`AprilShell`**：`UiShell::AppBootstrap`（无产品专属钩子）。
- **`April`**：M6 起同样由 `UiShell/main` 的 `AppBootstrap` 启动，`APR_PRODUCT_STARTUP=1`
  时调用 `AprilShellStartup` 的产品前置逻辑；旧的 `AppBootstrapper`/`CADApplicationEntry`/`CADApplicationRuntime` 已删除。

## 3. 算法/UI 分离 + 低耦合（ports & adapters）

**UI（`UiShell`/`customers/April`）不直接持有/改算法对象。**

- `Apr::ICadViewport` + `Apr::IViewportFactory`
  - `Apr::RenderViewport2DAdapter`：`AprilUi::RenderViewport2D` 藏后，只在 `.cpp` include；UI 意图 `resetView/zoomToSelection/setDrawingEnabled/setMeasureMode/setActiveTool`；算法注入留 adapter。
  - `Apr::Viewport3DAdapter`：`Viewport3D + RenderWidget3DAdapter` 藏后（`BUILD_UI3D`）。
  - `Apr::AprilWorkbench::createCenterView2D/3D` 走 `IViewportFactory` + `widget()`；**不** `new RenderViewport2D/Viewport3D`；旧视口藏在 adapter 中，不再持有 raw 指针。
- `Apr::IViewportServices` + `Apr::CompositionRootAdapter`
  - `hasSceneDocument2D/hasSceneDocument3D/hasOperationBus/hasSelectionService/hasLayerManager/hasUiStateCenter/hasCameraController3D/hasUiServices`；
  - 从 `AprilCore::ApplicationCompositionRoot` 适配；`Apr::AprilWorkbench::bindViewportServices(IViewportServices&)`；`UiShell` 不见 `ApplicationCompositionRoot`/`SceneDocument2D`/`OperationBus`。
- **命令**：`Apr::Commands : RD::ICommandCatalog + RD::ICommandInvoker + RD::ICommandState`
  - `setSink(Apr::ICommandSink*)`；生产实现为 `OperationCommandSink`（主题/语言/关于框/工作台切换走 `IShellRuntime`，2D 命令走 `CommandCatalog`/`OperationRouting`/`OperationBus`）；
  - `invoke/invokeWithArgs -> ICommandSink::dispatch -> OperationBus`；`UiShell` 只见 `RD::ICommand*`。
  - `Apr::attachCommandSink(Commands&, ICommandSink*)`；`Apr::attachCompositionRoot(...)` 可选 `ApplicationCompositionRoot::instance()`。
  - `AppBootstrap` 通过 `IHostWindow` 在创建客户模块前发布宿主 `QMainWindow`；`ShellRuntime` 通过 `IActiveRuntime` 发布活动运行时。
- `AprilWorkbench`（`customers/April`，`RD::ICenterViewProvider`）仍持有服务用于适配，但**`UiShell` core 不见**。

## 4. M3/M4 视口与旧 UI 界面

- **2D** `RenderViewport2D`（`QWidget`+`UI::IViewportHost`）；`ViewportKind::TwoD`。
- **3D** `Viewport3D`（头文件 `UiViewport3D.h`，类 `Viewport3D`，**非 `UiViewport3D`**）+ `RenderWidget3DAdapter`；`ViewportKind::ThreeD`；`BUILD_UI3D`。
- **旧界面部件进 `UiShell`**：`Apr::PanelFactory : RD::IWidgetFactory` `factory="april.panel"`：
  `drawTools/tools→DrawToolBarWidget`（左侧绘图工具栏，绑定外壳 QAction），
  `editToolbar→TopToolBar`（顶部编辑工具栏，命令顺序来自 `props.commands`），
  `scene→SceneTreePanel`, `inspector→PropertiesPanelWidget`, `status→StatusBar`, 未识别→空 `QWidget`。
  `ui.json.window.nativeMenuBar=true` 让 macOS 使用屏幕顶部原生菜单栏；`props.kind` 可显式指定面板类型。
  macOS 27+ 由 `aprilShellAfterBootstrap()` 调 `ensureMacMenuImagesVisible()` 显式打开
  `NSMenuItem.preferredImageVisibility`，补回原生菜单项图标。
- `ui.json` `regions` + `panels` + `menus`；`configs/base.json` `menus`(8)→`UiShell MenuNodeSpec` **8 menus / 184 个 id**（`type action→command`, `command→commandId`, `label→titleKey`, `icon→iconKey`, `separator`, `submenu`），已覆盖 `base.json` 全部 id。
- `Apr::Commands::addCommandsFromProfile()` 从 ui.json 递归登记 `type=command` 节点到目录，`AssemblyPlanner` 才不会把菜单当未知命令剔除；`UiTstAprilMenuCatalog` 校验随附 profile 的每个菜单命令都可被 `find()`。
- 外置 `--profile` 走契约 `RD::ICommandProfileRegistrar`：`ProfilePipeline::loadFromJson()` 在规划前调用，四套客户 `Commands` 均实现（April 复用上述函数，其余继承 `CommandTable` 默认实现）；新菜单命令无需重编即可入目录，`UiTstProfileCommands` 校验有/无钩子的诊断差异与幂等性。
- `PanelCreateRequest`/`ShellServices`/`UiTranslations`/`UiDeploy`；`UiCustomerProfile` `ui.json`+`assets` 外置。

## 5. 当前不成立（未完成）

- **`AprilShell` 完整 profile + offscreen** 仍会崩在 `MetalSurface`：offscreen 无原生窗口，属环境限制；最小 profile 冒烟已通过。原“profile 加载后立即崩溃”的真因（栈上 adapter 悬空）已修，见 `MIGRATION.md` §0.5。
- **M3 运行时** 2D 绘图/选择/变换/撤销未验证；需在真实窗口环境验收；`UiWorkbench` 可能未 `setWorkbench`。
- `Apr::IViewportServices` 现在经 `ICadViewport::bindServices` 真注入（不再空实现），服务转发到 `DefaultViewportFactory`；2D 交互语义仍待验证。
- `AprilWorkbench` 只保存 `IViewportServices`/`IViewportFactory`/`ICadViewport` 等 port；组合根与 adapter 由 `ModuleParts.wiring` 持有；不返回 raw 旧视口。
- **M4** 菜单转换、目录登记与外置 profile 同步已完成（unknown-command 警告为 0），但点击/快捷键同一命令路径的运行时未验证；生产命令出口已改为 `OperationCommandSink`，不再依赖 `UiWorkbench`。
- **M5** 数据化已做：`ui.json` 可选 `brand`（产品名/组织/图标/默认皮肤/默认语言/关于框字段）、
  外置 `styles/` 与 `translations/` 叠加、相对资源按 profile 目录解析、`UiDeployProfile` 一并部署；
  六套主题由 `ThemeCatalog` + 薄差异 QSS 提供。旧生产路径的品牌硬编码随 M6 移出。
- **M6** 已切：`April`→`UiShell/main` + `AprilShellStartup`；旧 UI 实现移到 `Main/Src/UI/Test/LegacyUI/`
  并由 `MainTests` 保留覆盖；生产 April 不编译该目录。`UiFeatureGate` 生产头在 `UI/Services/`，
  测试侧保留兼容头。部署由 `UiShell/CMakeLists.txt` 的 `AprilRuntimeDeploy ALL` 承接，
  安装树同时带上 `UiShell`/`UiApril` 动态库与 `profile/`。
- `AprilUi`↔`AprilCore` 的边界仍由客户层 port/adapter 管理；旧 `Main::AppBootstrapper` 已删；`April` 仍可运行。
- `Engine` 子模块 WIP + GeometryContext 补丁未提交。

## 6. `UiShell` 之外的旧文档

`UI分层与职责边界.md`、`UI-Engine解耦迁移方案.md`、`UI定制.md`、`耦合性分析.md`、`DLL制作标准与现状.md`、`白标与多客户插件化方案.md`
描述 `Main/Src/UI` + `UI/2D/3D/Common` 现状。**`UiShell` 是新增并行外壳，不替代它们；`UiShell` core 不耦合 `Main`。**
