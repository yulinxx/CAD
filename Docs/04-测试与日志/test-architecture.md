# 测试架构

## 概述

本文档描述项目的测试架构，包括测试类型、测试框架、测试组织和测试策略。

---

## 1. 测试类型

### 1.1 单元测试

**定义**：测试单个函数或类的行为

**范围**：
- 核心类型验证（RenderTypesTest）
- 工具方法验证（TransformParametersTest）
- 算法验证（几何计算）
- 数据结构验证

**特点**：
- 快速执行（< 1ms / 测试）
- 隔离性强
- 可重复

**关键文件**：
- `Renderx/Test/RenderTypesTests.cpp`
- `Utility/Utility/Test/VecTests.cpp`、`BBox2dTests.cpp`、`GeomMathTests.cpp`
- `Engine/2D/Test/Geo2DPrimitivesTests.cpp`、`Geo2DConstructAlgorithmsTests.cpp`
- `Engine/3D/Test/Geo3DPrimitivesExtendedTests.cpp`
- `UI/2D/Test/BaseToolTests.cpp`
- `UI/2D/Test/TransformParametersTests.cpp`
- `UI/Common/Test/CommandKernelTests.cpp`

### 1.2 集成测试

**定义**：测试多个组件协作的行为

**范围**：
- 命令生命周期测试（CommandConcurrentTests / UndoRedoTests）
- 渲染链路测试（RenderViewport2DRegressionTests / ViewportRefreshRegressionTests）
- 视口刷新测试
- 选择同步测试

**特点**：
- 验证组件间交互
- 覆盖完整流程
- 执行时间中等

**关键文件**：
- `Main/Src/UI/Test/RenderViewport2DRegressionTests.cpp`
- `Main/Src/UI/Test/ViewportRefreshRegressionTests.cpp`
- `Main/Src/UI/Test/ViewportInputRegressionTests.cpp`
- `Main/Src/UI/Test/Scene3DRegressionTests.cpp`
- `Main/Src/UI/Test/CommandUiWiringTests.cpp`（顶部工具栏 actionId 与命令目录的接线一致性；左侧绘图面板与中枢工具动作的接线；客户配置里全部 commandId 的可解析性与配置可信性自检）
- `UI/2D/Test/ToolsInteropTests.cpp`
- `UI/3D/Test/OperationBus3DTests.cpp`

### 1.3 回归测试

**定义**：验证修复的问题不再出现

**范围**：
- 变换链回归
- 渲染链回归
- 交互链回归
- 工艺链回归
- 旧问题回归

**特点**：
- 每个修复必须添加回归用例
- 防止问题重现
- 定期执行

### 1.4 E2E 测试

**定义**：测试完整的用户流程

**范围**：
- 完整绘图流程
- 文件导入导出流程
- 激光加工流程

**特点**：
- 模拟真实用户操作
- 执行时间较长
- 覆盖核心场景

---

## 2. 测试框架

### 2.1 Google Test

**用途**：核心测试框架

**特点**：
- 丰富的断言宏
- 参数化测试支持
- 测试夹具支持
- 生成 XML 报告

**使用方式**：
```cpp
TEST(RenderTypesTest, Size2D_Construction)
{
    Size2D size(100, 200);
    EXPECT_EQ(size.width, 100);
    EXPECT_EQ(size.height, 200);
}

TEST_F(BaseToolTest, LineTool_Creation)
{
    auto* tool = createTool<LineTool>();
    ASSERT_NE(tool, nullptr);
    EXPECT_EQ(tool->name(), "LineTool");
}
```

### 2.2 Qt Test

**用途**：UI 相关测试

**特点**：
- 支持 Qt 信号槽测试
- 支持事件模拟
- 集成 Qt 框架

### 2.3 Catch2

**用途**：部分旧测试

**特点**：
- 单头文件库
- 支持 BDD 风格
- 自动注册测试

---

## 3. 测试组织

### 3.1 测试项目结构

```
Main/Src/UI/Test/
├── FrameworkLifecycleTests.cpp      # 框架生命周期
├── RenderViewport2DRegressionTests.cpp # 2D 视口回归
├── ViewportRefreshRegressionTests.cpp  # 视口刷新回归
├── ViewportInputRegressionTests.cpp    # 视口输入回归
├── Scene3DRegressionTests.cpp       # 3D 场景回归
├── RenderWidget3DAdapterTests.cpp   # 3D 适配层测试
├── UndoRedoRegressionTests.cpp      # 撤销重做回归
├── UndoRedoExtendedRegressionTests.cpp
├── SceneNotifierTests.cpp           # 场景通知测试
├── SelectionServiceTests.cpp        # 选择服务测试
├── SyEntitySerializerTests.cpp      # 图元序列化
├── FioEntityConverterTests.cpp      # IR 转换测试
├── ImportExportRegressionTests.cpp  # 导入导出回归
├── LayerPersistenceBridgeTests.cpp  # 图层持久化桥接
├── ClientConfigTests.cpp            # 客户配置测试（含工具栏命令绑定回归：无分发器时推迟构建 / 空分发器会永久禁用全部按钮 / 菜单与工具栏共用同一分发器实例）
├── SceneTreeBuilder3DTests.cpp      # 3D 场景树构建
├── CommandUiWiringTests.cpp         # 命令 UI 接线回归（26 例：actionId 解析 / 启用规则 / 菜单栏 / 外部 QAction 树应用 / 2D-3D 切换防串台（5 例 BUILD_UI3D 条件编译）/ 配置命令契约 / 配置可信性自检 3 例 / 左侧绘图面板与中枢工具动作 4 例）
└── ToolSelectionSyncRegressionTests.cpp

UI/2D/Test/
├── TestMain.cpp              # 测试入口
├── BaseToolTests.cpp         # 基础工具测试
├── ToolManagerTests.cpp      # 工具管理器测试
├── TransformParametersTests.cpp # 变换参数测试
├── ToolsInteropTests.cpp     # 工具交互测试
├── TextEditToolFlowTests.cpp # 文本编辑工具流
├── TextEditServiceTests.cpp  # 文本编辑服务
├── ToolShortcutTests.cpp     # 工具快捷键
├── ParameterFactoryTests.cpp # 参数工厂
├── ComplexToolsTests.cpp     # 复杂工具
└── ComplexToolsTestImpl.cpp

UI/Common/Test/
├── CommandKernelTests.cpp    # 命令内核测试（含启用规则组合：多选 / 多选+未锁定）
├── CommandConcurrentTests.cpp # 命令并发测试
├── UndoRedoTests.cpp         # 撤销重做测试
└── SettingsTableTests.cpp    # 设置表测试

UI/3D/Test/
├── CommandCatalog3DTests.cpp # 3D 命令目录测试
├── OperationBus3DTests.cpp   # 3D 操作总线测试
├── ToolManager3DTests.cpp    # 3D 工具管理器测试
├── Lighting3DTests.cpp       # 光照测试
└── SceneDocumentIO3DTest.cpp # 3D 场景文档 IO

Renderx/Test/
├── RenderTypesTests.cpp      # 渲染类型测试
├── BatchQueueTests.cpp       # 批次队列测试
├── MeshManagerTests.cpp      # 网格管理测试
├── ArenaTests.cpp            # Arena 分配器测试
├── SlotMapTests.cpp          # SlotMap 测试
├── NullBackendTests.cpp      # Null 后端测试
└── TransientBufferPoolTests.cpp # 暂存缓冲池测试

Engine/2D/Test/
├── Geo2D*Tests.cpp          # 2D 几何算法
├── GeometryComputationTests.cpp
├── SceneManagerTests.cpp    # 场景管理测试
├── SelectionSemanticsTests.cpp
├── TessellatorTests.cpp     # 细分器测试
├── PathOptimizerTests.cpp   # 路径优化测试
└── RegressionTests.cpp      # 2D 回归

Engine/3D/Test/
├── Geo3D*Tests.cpp          # 3D 几何算法
├── GeometryContext3DTests.cpp
└── TransformerTests.cpp

FileIO/FileIO/Test/
├── FileImportTests.cpp
├── SySerializerTests.cpp
├── FioTypesTests.cpp
├── FileIOUtilityTests.cpp
└── FileIORegressionTests.cpp

Utility/Utility/Test/
├── VecTests.cpp
├── BBox2dTests.cpp
└── GeomMathTests.cpp
```

### 3.2 测试命名规范

| 元素 | 规范 | 示例 |
|------|------|------|
| 测试套件 | `[模块][组件]Test` | `RenderTypesTest` |
| 测试用例 | `[组件]_[场景]` | `Size2D_Construction` |
| 测试夹具 | `[组件]Test` | `BaseToolTest` |

### 3.3 Mock/Stub 组织

当前主要使用 GTest 的 gmock 进行 mock，mock 定义与测试用例同文件（如 `FrameworkLifecycleTests.cpp` 中的 MockSceneManager），不单独维护 Stub 目录。

---

## 4. 测试策略

### 4.1 覆盖率目标

| 模块 | 目标覆盖率 | 当前覆盖率 |
|------|-----------|-----------|
| Renderx 核心类型 | 90% | 85% |
| 2D 几何算法 | 80% | 75% |
| UI2D 工具 | 80% | 70% |
| 命令系统 | 85% | 80% |

命令系统内的「命令 UI 启用态」子层由两处覆盖：启用规则求值在 `UI/Common/Test/CommandKernelTests.cpp`（含多选、多选+未锁定组合），UI 接线一致性在 `Main/Src/UI/Test/CommandUiWiringTests.cpp` —— 前 8 例是数据契约（工具栏 actionId 全部可解析、菜单栏 Edit 命令规则符合预期且不为 `Always`、Menu surface 已声明），中间 5 例是 2D 行为验证：直接构造带 `property("commandId")` 的 QMenu 跑 `CommandActionHub::applySnapshotToMenu()`，断言空选禁用编辑命令、剪贴板只放行 Paste、对齐需两个未锁定、`commandUnavailable` 项保持禁用、无关动作不被误改。

最后 5 例（`Switch_*` / `Switch3D_*`，`#if BUILD_UI3D`）覆盖 2D↔3D 切换：3D 侧 `CommandActionHub3D::applySnapshotToMenu()` 的选择/锁定门禁，以及「防串台」数据契约 —— 2D 独有 id 在 3D 目录解析为 `OperationId3D::None`、3D 独有 id 在 2D 目录解析为 `0`、跨侧快照对预置 `false` 的动作不产生改动、两侧同名命令（undo/redo/delete）的 `enableRule` 取值一致。

切换缺陷里的 P0（`WorkbenchMenuManager::m_workbench` 未随切换同步）没有单测覆盖：它需要完整 `WorkbenchWindow` + 两个真实工作台，属集成层，当前靠手工冒烟验证（3D 下点菜单项确认走 3D 操作总线、3D 独有项可见可用）。

同文件末尾的 `DrawToolBarWidgetTest`（4 例，2026-08-26 新增）锁定左侧绘图面板与命令中枢的接线：
按钮数量与顺序等于目录里 `LeftToolbar` 面的工具且每个按钮的 `defaultAction` 就是中枢那个 QAction、
点击按钮必须触发中枢 QAction 且 `detectOperationSource()` 报 `LeftToolbar`、
连续 `setActiveToolAction()` 后始终恰好一个 checked（`QActionGroup` 互斥不被破坏）、
每个工具动作的 `commandId` 属性等于目录 `shortcutId`。

同文件里的 `UiConfigSelfCheckTest`（3 例）覆盖配置可信性启动自检
（`Main/Src/UI/ClientConfig/UiConfigSelfCheck.{h,cpp}`，交叉核对 CMake 开关 / JSON 配置 / License 授权）：

- `UnknownCommandIdIsReportedAsUnresolved`：手工构造一份只含一个不存在 commandId 的配置，
  断言它进 `unresolvedCommands`、`hasBlockingIssue()` 为真，且报告字符串里**带定位路径**
  （`menus/edit/edit.does_not_exist`）。只报命令名的话还得人工翻 JSON 找它在哪，
  报告就失去了大半价值，所以路径本身也是被断言的契约。
- `ShippedConfigsPassTheSelfCheck`：对随包发布的 4 份配置（`base` / `san_yi` /
  `client_a` / `client_b`）跑自检，`unresolvedCommands` 与 `licensedButNotCompiled`
  都必须为空。后者是最严重的一类 —— 授权放行却没编译进来等于卖了不存在的功能。
- `EveryFeatureUsedInConfigsHasABuildSwitchMapping`：配置里用到的每个 `feature` 都必须在
  `UiConfigSelfCheck::isFeatureCompiledIn()` 的对应表里登记。没登记的 feature 自检对它
  完全无感（查不到就跳过，不报错也不告警），这条守卫就是防止新增授权功能时漏改那张表。

去重说明：`CommandUiWiringTests.cpp` 原先自带一份配置遍历实现
（`collectMenuCommands` / `workbenchScope` / `collectAllCommandRefs` / `ConfigCommandRef`），
现已删除，改为 `using ConfigCommandRef = UiConfigCommandRef;` + 调用
`UiConfigSelfCheck::collectCommandRefs`。启动自检与配置契约测试必须用**同一份遍历**，
否则会长出「测试过了但运行期自检漏报」这种最难查的偏差 —— 两边各写一份时，
任何一侧漏了工具栏或快捷键节都察觉不到。




### 4.2 测试执行策略

**本地开发**：
```bash
# 运行单个测试套件
./MainTests --gtest_filter=RenderTypesTest*

# 运行所有测试
./MainTests
```

**CI/CD**：
- 每次提交自动运行单元测试
- 每日构建运行集成测试
- 每周运行回归测试和 E2E 测试

### 4.3 失败分类

| 分类 | 定义 | 处理方式 |
|------|------|----------|
| **回归失败** | 之前通过的测试现在失败 | 立即修复 |
| **新功能失败** | 新添加的测试失败 | 在 PR 中修复 |
| **已知失败** | 已识别的旧架构遗留问题 | 标记并计划修复 |
| **环境失败** | 测试环境问题 | 修复环境 |

#### 4.3.1 无 QApplication 导致的"挂死"（2026-08-27 定位）

构造 `QWidget` 前必须存在 `QApplication`，否则 Qt 直接 `qFatal`，Debug 版走
`__debugbreak()` —— 进程停在断点上，既不退出也不打印 gtest 汇总，表现为：

- 测试进程挂死，`ctest` 只报超时，看不出是哪个用例；
- 退出码 `0x80000003`（STATUS_BREAKPOINT）；
- 挂死进程一直占着 Qt 与项目 DLL，后续构建报 `LNK1168: 无法打开 xxx.dll 进行写入`。

排查顺序：先 `Get-Process | Where Path -like "*build*"` 找残留测试进程，
再看该 exe 的 stderr 是否有 `Must construct a QApplication before a QWidget`。

约定：任何会构造 QWidget（含派生自 QWidget 的 Stub）的测试目标都必须建
`QApplication` —— `UI/2D/Test/TestMain.cpp` 在自定义 main 里建，
`MainTests` 用 `::testing::Environment` 在首个用例前建。
平台插件沿用默认 `windows`（`sanyi_deploy_qt_dlls` 只部署它，
强制 `QT_QPA_PLATFORM=offscreen` 会因插件缺失直接起不来）。


---

## 5. 测试辅助工具

### 5.1 Mock 对象

```cpp
// 模拟 SceneManager
class MockSceneManager : public Eg::SceneManager
{
public:
    MOCK_METHOD(std::vector<Eg::EntityId>, getAllEntityIds, (), (const override));
    MOCK_METHOD(void, addEntity, (std::unique_ptr<Eg::SyEntity>), (override));
    MOCK_METHOD(void, removeEntity, (Eg::EntityId), (override));
};
```

### 5.2 测试数据

```cpp
// 测试用几何数据
struct TestGeometry
{
    static std::unique_ptr<Eg::SyLine> createLine()
    {
        return std::make_unique<Eg::SyLine>(
            Eg::Point(0, 0),
            Eg::Point(100, 100)
        );
    }
};
```

### 5.3 断言扩展

```cpp
// 自定义断言
#define EXPECT_VEC_EQ(expected, actual) \
    EXPECT_EQ(expected.x, actual.x); \
    EXPECT_EQ(expected.y, actual.y); \
    EXPECT_EQ(expected.z, actual.z);
```

---

## 6. 边界检查清单

### 添加新测试时检查
- [ ] 是否遵循命名规范？
- [ ] 是否有清晰的测试目标？
- [ ] 是否使用了正确的断言？
- [ ] 是否需要添加回归用例？

### 修改代码时检查
- [ ] 是否破坏了现有测试？
- [ ] 是否需要添加新测试？
- [ ] 是否更新了相关测试？

### 修复问题时检查
- [ ] 是否添加了回归用例？
- [ ] 是否验证了修复？

---

## 7. 同步说明

本文的测试文件列表应与当前工作树保持一致。新增 / 删除测试文件时同步更新「3.1 测试项目结构」。
