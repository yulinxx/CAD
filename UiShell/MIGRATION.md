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

### M1：把现有 CAD UI 提取成库

目标：让新外壳能链接现有工作台，而不是重写。

- 在 `Main/CMakeLists.txt` 增加 `AprilUi`（STATIC 或 SHARED）目标，
  源文件为 `Main/Src/UI/**`（排除 `Test/`），必要时带上
  `Main/Src/Composition/**`、`Main/Src/Import/**`、`Main/Src/Export/**`。
- `April` 可执行目标改为：`main.cpp` + `Runtime/**` + 链接 `AprilUi`。
- 处理 `Q_INIT_RESOURCE`、静态初始化与重复符号；先保证 `April` 行为不变。
- 验收：`April` 可启动，现有测试通过；`AprilUi` 可被其他目标链接。

### M2：建立 `customers/April` 客户模块

- 在 `UiShell/customers/April/` 提供：
  - `CustomerModule.cpp`：`createCustomerModule()`；
  - `panels/PanelFactory.*`：场景树、属性、图层、状态栏；
  - `commands/Commands.*`：包装现有命令表；
  - `ui.json`：由 `configs/base.json` + `april.json` 转换而来；
  - `assets/`、`translations/`。
- `UiProjectDefaults.cmake` 的 `UI_CUSTOMER` 增加 `April`。
- 验收：`-DUI_CUSTOMER=April` 构建出的 `April.exe` 显示新外壳 + 空白中央视图。

### M3：视口接入

- 实现 `ICenterViewProvider::createCenterView()`，返回现有
  `RenderViewport2D` / `UiViewport3D` 的宿主 QWidget。
- 视口所需的服务（`UiServices`、`SceneDocument2D`、`UiStateCenter`）
  由 `customers/April` 持有，经 `ShellServices` 注入。
- 验收：2D 绘图、选择、变换、撤销重做在新外壳内可用。

### M4：菜单、工具栏、命令、快捷键

- 把 `configs/base.json` 的 176 个 action 映射到新 `ui.json` 的 `menus`。
- `ICommandCatalog` / `ICommandInvoker` 包装现有 `OperationRegistry`。
- 验收：菜单点击、工具栏、面板按钮、快捷键走同一条命令路径；
  `tstInteraction` 等集成测试在新客户上通过。

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
