# Main → UiShell 迁移计划

> 状态：M0 已完成（2026-11-27）。目标是把现有 `Main` 的 UI 外壳改成
> SampleFrame 的架构：`UiContract` 契约层 + `UiShell` 运行时 + `UiDialogs`
> 共享对话框 + `customers/<产品>` 客户库 + 薄 EXE。

## 0. 策略：绞杀者，不推倒重来

现有 `UI + Main` 约 16.5 万行，直接删除重写的风险不可控。采用并行迁移：

1. 新外壳 `UiShell/` 先作为根工程子目录存在，旧 `April` 应用保持可运行；
2. 每迁移一块，就在新外壳里替换掉一块旧实现；
3. 全部功能对齐后，`April` 入口改为链接新外壳，最后删除 `Main/Src/UI`。

回滚方式：`-DBUILD_UI_SHELL=OFF` 即回到迁移前状态，旧 `April` 不受影响。

## 1. M0 已完成的内容

| 项 | 结果 |
| --- | --- |
| 新子系统 | `UiShell/`（源码来自 SampleFrame，构建脚本已适配根工程） |
| 根工程接入 | `CMakeLists.txt` 新增 `BUILD_UI_SHELL`，默认 `ON` |
| 产物 | `build/bin_Qt6/shell/Release/MantiSoft`、`libUiShell.dylib`、`libUiMantiSoft.dylib` |
| 测试 | 22 个 Qt Test + `UiLayerCheck`，共 23 项全部通过 |
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
4. `ctest -R "UiTst|UiLayerCheck"` **23/23**；
5. `April` 可启动（offscreen，正常初始化 DB / 语言 / 许可 / UI）；
   `nm -C April` 能看到 `WorkbenchWindow::*` 等 `AprilUi` 符号；
6. `Engine` 子模块 WIP 已 `stash pop` 恢复原样。

**注**：`MainTests` 仍编译全部源码，未切到链接 `AprilUi`（见“本轮未做”）。


### M2：建立 `customers/April` 客户模块（已完成，2026-11-27）

**做法**：

- `UiShell/customers/April/`：
  - `CustomerModule.cpp`：`createCustomerModule()` + `customerBuildFingerprint()`；
    `strId=april`、`strDisplayName=April`、`strProfilePath=:/UiApril/profile/ui.json`；
    `Q_INIT_RESOURCE(UiAprilProfile)`（静态链接兜底）；`centerView` 指向本客户实现。
  - `panels/PanelFactory.*`：`CenterView : RD::ICenterViewProvider`，M2 返回空 `QWidget`；
    **M3** 改为返回现有 CAD 工作台/视口宿主。
  - `ui.json`：version 2 最小集（titlebar / menubar / CenterView / StatusBar）。
- `UiProjectDefaults.cmake`：`UI_CUSTOMER` 增加 `April`；`UI_CUSTOMER_TARGET=UiApril`；
  customer 目录 `customers/April`；宿主 EXE 用 **`AprilShell`**（避免与 `Main` 的 `April` 目标冲突；M6 切换入口前不替换现有 `April`）。
- `UiApril`（`msUiAddCustomerLibrary`）链接 `AprilUi`（`if(TARGET AprilUi)`）。

**暂未做（后续里程碑）**：

- `commands/Commands.*`：`ICommandCatalog` / `ICommandInvoker` → 现有命令表（**M4**）；`cmdCatalog/cmdInvoker/cmdState` 留空。
- `ui.json` 由 `configs/base.json` + `april.json` 转换（**M4**）；`panels`/`menus` 目前空。
- `assets/`、`translations/`（**M5**）。
- 完整资源注册：`AprilUi` 里的 `:/configs/...` 等 CAD 资源目前只在 `April` 可执行目标里注册；
  `AprilShell` 的 CAD 资源注册在 **M5/M6** 处理（M2 空中央视图不依赖它们，已可构建/启动）。
- 完整 exe 替换（**M6**）。

**验收（临时 stash `Engine` WIP，用已提交 Engine 状态；验证后已 `stash pop` 恢复 50 文件）**：

1. `cmake -S . -B build -DUI_CUSTOMER=April` 配置成功，`customer : April`；
2. `ninja UiApril` **exit 0** → `libUiApril.dylib`（链接 `AprilUi`）；
3. `ninja AprilShell` **exit 0** → `AprilShell`（新外壳 + 空中央视图，profile 外置到 `<exe>/profile/ui.json`）；
4. `AprilShell` offscreen 启动，加载 `UiAprilProfile`；
5. `Engine` 子模块 WIP 已恢复。

**注**：M1 里 `AprilUi` 已链接；M2 只验证 `UiApril` 可链接 `AprilUi`，CAD 视口/命令/资源在 M3/M4/M5 接入。

### M3：视口接入（框架已移植；本轮不保证编译，待处理 `Engine` 子模块后验证）

**做法**（`UiShell/customers/April`）：

- 新增 `panels/AprilWorkbench.{h,cpp}`：
  - `AprilWorkbench : RD::ICenterViewProvider`；
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
  - 与 `UiShell` 的 `RD::ShellServices` 互转：`populateShellServices()` / `applyShellServices()`；
  - 不重写工作台，复用 `AprilUi` 现有实现。
- `PanelFactory.h`：`CenterView : public AprilWorkbench`（M2 空 `QWidget` 已替换为 M3 视口宿主）。
- `CustomerModule.cpp`：`module.centerView` 指向 `CenterView`（即 `AprilWorkbench`）。
- `Main/CMakeLists.txt`：`AprilUi` 的 include 目录与 `UICommon/UI2D/Engine*/Engine3D` 链接改 `PUBLIC`，
  供 `customers/April` `#include "UI/..."`。

**真实 API 名（不重新发明）**：`RenderViewport2D`（`QWidget` + `UI::IViewportHost`）、`Viewport3D`、
`RenderWidget3DAdapter`、`UiServices`、`WorkbenchServices`、`SceneDocument2D`、`UiStateCenter`、`OperationBus`、`ISelectionService`。

**暂未做 / 本轮不保证**：

- **编译**：用户明确“编译先不管；框架移植成功后，再考虑这个子模块处理”。本轮只移植框架，**未**在 `Engine` 子模块修复后实际 `ninja` 验证。
- **组合根**：`ApplicationCompositionRoot` 在 `Main/Src/Composition`（不在 `AprilUi` 内），现在填充 `UiServices`/`SceneDocument2D`/`OperationBus`；
  如何为 `customers/April` 提供已装配好的实例，留到处理 `Engine` 子模块时一并接上。
- **资源注册**：`AprilUi` 的 `:/configs/...` 等仍在 `April` 可执行目标注册；`AprilShell` 的 CAD 资源在 **M5/M6**。
- **验收（待 `Engine` 可编译后）**：2D 绘图、选择、变换、撤销重做在新外壳内可用。

**注**：原 M3 文本写 `UiViewport3D`，实际类为 `Viewport3D`（头文件 `UiViewport3D.h`）。

### M4：菜单、工具栏、命令、快捷键（命令适配框架已移植；`ui.json` 菜单转换未做）

**做法**（`UiShell/customers/April`）：

- 新增 `commands/Commands.{h,cpp}`：`Apr::Commands : RD::ICommandCatalog + RD::ICommandInvoker + RD::ICommandState`
  - `find()`；`invoke()` → `IUiCommandDispatcher::dispatch(commandId, {})`；
    `invokeWithArgs()` → `dispatch(commandId, QJsonObject→QVariantMap)`（透传/补齐，不整体覆盖）；
  - `queryState()` → `isCommandRegistered` → `bEnabled`；`addCommand(RD::CommandView)`；`setDispatcher()`；
  - **不重写命令系统**，复用 `AprilUi` 的 `IUiCommandDispatcher`（`UiWorkbench` 实现）/ `OperationBus` / `OperationRegistry`。
- `CustomerModule.cpp`：`ModuleParts` 持有 `Commands commands`；`module.cmdCatalog/cmdInvoker/cmdState = &parts->commands`。
- `AprilWorkbench::applyShellServices` / `populateShellServices` 已转发 `cmdCatalog/cmdInvoker/cmdState`。

**暂未做**：

- `IUiCommandDispatcher`（现有 `UiWorkbench`）的 `setDispatcher()` 注入：`customers/April` 装配完工作台后接上；
  `UiApril` 单独 dylib 链接仍受“`AprilUi` 引用 `Main` 非 UI 符号 / 组合根”缺口影响（见 M3 注）。
- **`configs/base.json` 的 176 个 action 映射到新 `ui.json` 的 `menus`**（需要 `UiShell` menu 转换，未做）；
  菜单点击/工具栏/面板按钮/快捷键“走同一条命令路径”的**运行时验收**与 `tstInteraction` 在新客户上通过，待 `AprilShell` 完整链接后验证。
- `ui.json` 的 `menus`/`panels` 目前空。

### M5：品牌、主题、i18n、部署

- 品牌数据全部进 `ui.json` / `assets/`，删除 `WorkbenchWindow.cpp` 里的硬编码。
- 主题走 `UiShell` 的 `ThemeController`，保留现有 6 套 QSS。
- 翻译沿用 `TranslationService`，`--lang` 在客户模块加载前生效。
- 验收：改外部 `ui.json` + `assets/` 即可换客户，不重编。

### M6：切换入口，删除旧 Main

- `April` 入口改为 `UiShell/main` 的 `AppBootstrap` 流程。
- 删除 `Main/Src/UI/ClientConfig`、`Workbench*` 等已被替代的实现。
- 保留 `Main/Src/Composition` 等仍在使用的服务，直到完全迁移。
- 验收：全量回归 + 打包脚本产出可运行目录。

## 4. 验收标准（每个里程碑都要满足）

1. `cmake -S . -B build -DBUILD_UI_SHELL=ON` 配置成功；
2. `ctest --test-dir build -R "UiTst|UiLayerCheck"` 全绿；
3. 旧 `April` 仍可构建、启动（M6 之前）；
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

## 6. 本轮未做的事

- 没有删除或修改 `Main/` 的任何源码；
- 没有把 CAD 视口接进新外壳（M3）；
- 没有把 `configs/base.json` 转成 `customers/April/ui.json`（M2/M4）。

这些是后续里程碑的工作，需要按 M1 → M6 逐项推进。
