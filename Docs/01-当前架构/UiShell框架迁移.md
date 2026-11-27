# UiShell 框架迁移（SampleFrame 风格，低耦合 + 算法/UI 分离）

> **状态唯一事实源：`UiShell/MIGRATION.md`（本文只做摘要，不再维护另一套状态）。**
> M0–M4 的框架脚手架 + 低耦合 + 数据化已落地；M3 启动崩溃已修、交互运行时与
> M4 菜单运行时未验收；M5 数据化（brand/关于框/六套主题/默认语言/外置资源）已完成，M6 未完成。
> Step 0 基线（2026-11-27）：`UI_CUSTOMER=April` 下 configure / build / 25 项
> `UiTst|UiLayerCheck` 全绿（含 `UiTstAprilMenuCatalog`、`UiTstProfileCommands`），旧 `April` 与 `AprilShell`（最小 profile）均可进入
> 事件循环；一键门禁 `UiShell/ci/ui-shell-baseline.sh` 已固化进 CI。
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
April (Main EXE) ──> main.cpp + AprilUi + AprilCore      // 旧应用仍在
AprilCore ──x──> UiShell                                 // 算法不依赖 UI
```

- **`AprilCore`**：`Main/Src/**` 非 UI + 不含 `main.cpp`（`Composition/Import/Export/Persistence/Hardware/Runtime/Bootstrap/Common/Platform/CAM/License/Vision`）。**不链 `UiShell`**。
- **`AprilUi`**：`Main/Src/UI/**`。复用旧 `WorkbenchWindow/Workbench2D/Workbench3D/RenderViewport2D/Viewport3D/...`。include/链接 `PUBLIC` 给 `UiApril`。
- **`AprilShell`**：`UiShell::AppBootstrap`（不 `Main::AppBootstrapper`）。
- **`April`** 旧 `Main::AppBootstrapper` 仍在 `AprilCore`，**M6 未切 `UiShell`、未删**。

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
  - `setDispatcher(::IUiCommandDispatcher*)`；`UiWorkbench : public IUiCommandDispatcher`；
  - `invoke/invokeWithArgs -> IUiCommandDispatcher::dispatch -> OperationBus`；`UiShell` 只见 `RD::ICommand*`。
  - `Apr::attachCommandDispatcher(Commands&, IUiCommandDispatcher*)`；`Apr::attachCompositionRoot(...)` 可选 `ApplicationCompositionRoot::instance()`。
- `AprilWorkbench`（`customers/April`，`RD::ICenterViewProvider`）仍持有服务用于适配，但**`UiShell` core 不见**。

## 4. M3/M4 视口与旧 UI 界面

- **2D** `RenderViewport2D`（`QWidget`+`UI::IViewportHost`）；`ViewportKind::TwoD`。
- **3D** `Viewport3D`（头文件 `UiViewport3D.h`，类 `Viewport3D`，**非 `UiViewport3D`**）+ `RenderWidget3DAdapter`；`ViewportKind::ThreeD`；`BUILD_UI3D`。
- **旧界面部件进 `UiShell`**：`Apr::PanelFactory : RD::IWidgetFactory` `factory="april.panel"`：
  `tools→DrawToolBarWidget`, `scene→SceneTreePanel`, `inspector→PropertiesPanelWidget`, `status→StatusBar`, 未识别→空 `QWidget`。
- `ui.json` `regions` + `panels` + `menus`；`configs/base.json` `menus`(8)→`UiShell MenuNodeSpec` **8 menus / 184 个 id**（`type action→command`, `command→commandId`, `label→titleKey`, `icon→iconKey`, `separator`, `submenu`），已覆盖 `base.json` 全部 id。
- `Apr::Commands::addCommandsFromProfile()` 从 ui.json 递归登记 `type=command` 节点到目录，`AssemblyPlanner` 才不会把菜单当未知命令剔除；`UiTstAprilMenuCatalog` 校验随附 profile 的每个菜单命令都可被 `find()`。
- 外置 `--profile` 走契约 `RD::ICommandProfileRegistrar`：`ProfilePipeline::loadFromJson()` 在规划前调用，四套客户 `Commands` 均实现（April 复用上述函数，其余继承 `CommandTable` 默认实现）；新菜单命令无需重编即可入目录，`UiTstProfileCommands` 校验有/无钩子的诊断差异与幂等性。
- `PanelCreateRequest`/`ShellServices`/`UiTranslations`/`UiDeploy`；`UiCustomerProfile` `ui.json`+`assets` 外置。

## 5. 当前不成立（未完成）

- **`AprilShell` 完整 profile + offscreen** 仍会崩在 `MetalSurface`：offscreen 无原生窗口，属环境限制；最小 profile 冒烟已通过。原“profile 加载后立即崩溃”的真因（栈上 adapter 悬空）已修，见 `MIGRATION.md` §0.5。
- **M3 运行时** 2D 绘图/选择/变换/撤销未验证；需在真实窗口环境验收；`UiWorkbench` 可能未 `setWorkbench`。
- `Apr::IViewportServices` 现在经 `ICadViewport::bindServices` 真注入（不再空实现），服务转发到 `DefaultViewportFactory`；2D 交互语义仍待验证。
- `AprilWorkbench` 只保存 `IViewportServices`/`IViewportFactory`/`ICadViewport` 等 port；组合根与 adapter 由 `ModuleParts.wiring` 持有；不返回 raw 旧视口。
- **M4** 菜单转换、目录登记与外置 profile 同步已完成（unknown-command 警告为 0），但点击/快捷键同一命令路径的运行时未验证；`setDispatcher` 已在 `wireCompositionRoot()` 接线，但 `UiWorkbench` 可能未设置。
- **M5** 数据化已做：`ui.json` 可选 `brand`（产品名/组织/图标/默认皮肤/默认语言/关于框字段）、
  外置 `styles/` 与 `translations/` 叠加、相对资源按 profile 目录解析、`UiDeployProfile` 一并部署；
  六套主题由 `ThemeCatalog` + 薄差异 QSS 提供。旧 `Main` 品牌硬编码与 `AprilShell` CAD `:/configs`
  资源注册未做。
- **M6** `April`→`UiShell` 未切；`Main/Src/UI` 未删；全量回归未跑。
- `AprilUi`↔`AprilCore` 互相引用；旧 `Main::AppBootstrapper` 未删；`April` 仍可运行。
- `Engine` 子模块 WIP + GeometryContext 补丁未提交。

## 6. `UiShell` 之外的旧文档

`UI分层与职责边界.md`、`UI-Engine解耦迁移方案.md`、`UI定制.md`、`耦合性分析.md`、`DLL制作标准与现状.md`、`白标与多客户插件化方案.md`
描述 `Main/Src/UI` + `UI/2D/3D/Common` 现状。**`UiShell` 是新增并行外壳，不替代它们；`UiShell` core 不耦合 `Main`。**
