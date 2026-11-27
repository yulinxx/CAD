# UiShell 框架迁移（SampleFrame 风格，低耦合 + 算法/UI 分离）

> 状态：M0–M4 框架脚手架 + 低耦合 + 数据化完成；M3 运行时 / M5 / M6 未完成。
> 详细里程碑见 `UiShell/MIGRATION.md`。本文把现状同步进 `Docs/`。

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
  - `Apr::AprilWorkbench::createCenterView2D/3D` 走 `IViewportFactory` + `widget()`；**不** `new RenderViewport2D/Viewport3D`；不把 raw 旧视口暴露给 `UiShell`；`m_viewport2D/m_viewport3D` 置空。
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
- `ui.json` `regions` + `panels` + `menus`；`configs/base.json` `menus`(8)→`UiShell MenuNodeSpec` **8 menus / 162 commands**（`type action→command`, `command→commandId`, `label→titleKey`, `icon→iconKey`, `separator`, `submenu`）。
- `PanelCreateRequest`/`ShellServices`/`UiTranslations`/`UiDeploy`；`UiCustomerProfile` `ui.json`+`assets` 外置。

## 5. 当前不成立（未完成）

- **M3 运行时** 2D 绘图/选择/变换/撤销未验证；`ApplicationCompositionRoot::instance()->isBuilt()` + `currentWorkbench()` 不保证；`UiWorkbench` 可能未 `setWorkbench`。
- `Apr::IViewportServices` 只 `has*`，未传具体服务；`bindViewportServices` 未真正用。
- `AprilWorkbench` 仍持有 `SceneDocument2D/OperationBus/UiServices/ApplicationCompositionRoot`；`viewport2D/viewport3D` raw 返回；但不进 `UiShell`。
- **M4** 菜单运行时未验证；`setDispatcher` 接线不保证 `UiWorkbench` 非空。
- **M5** 品牌/主题/i18n/部署、`AprilShell` CAD `:/configs` 资源注册未做。
- **M6** `April`→`UiShell` 未切；`Main/Src/UI` 未删；全量回归未跑。
- `AprilUi`↔`AprilCore` 互相引用；旧 `Main::AppBootstrapper` 未删；`April` 仍可运行。
- `Engine` 子模块 WIP + GeometryContext 补丁未提交。

## 6. `UiShell` 之外的旧文档

`UI分层与职责边界.md`、`UI-Engine解耦迁移方案.md`、`UI定制.md`、`耦合性分析.md`、`DLL制作标准与现状.md`、`白标与多客户插件化方案.md`
描述 `Main/Src/UI` + `UI/2D/3D/Common` 现状。**`UiShell` 是新增并行外壳，不替代它们；`UiShell` core 不耦合 `Main`。**
