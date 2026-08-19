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
├── SimpleRenderer3DTests.cpp        # 验证渲染器测试
├── UndoRedoRegressionTests.cpp      # 撤销重做回归
├── UndoRedoExtendedRegressionTests.cpp
├── SceneNotifierTests.cpp           # 场景通知测试
├── SelectionServiceTests.cpp        # 选择服务测试
├── SyEntitySerializerTests.cpp      # 图元序列化
├── FioEntityConverterTests.cpp      # IR 转换测试
├── ImportExportRegressionTests.cpp  # 导入导出回归
├── LayerPersistenceBridgeTests.cpp  # 图层持久化桥接
├── ClientConfigTests.cpp            # 客户配置测试
├── SceneTreeBuilder3DTests.cpp      # 3D 场景树构建
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
├── CommandKernelTests.cpp    # 命令内核测试
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
