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
- `UI/2D/Test/BaseToolTests.cpp`
- `UI/2D/Test/TransformParametersTests.cpp`
- `UI/Common/Test/CommandKernelTests.cpp`

### 1.2 集成测试

**定义**：测试多个组件协作的行为

**范围**：
- 命令生命周期测试（CommandLifecycleTests）
- 渲染管线测试（RenderPipelineTest）
- 视口刷新测试
- 选择同步测试

**特点**：
- 验证组件间交互
- 覆盖完整流程
- 执行时间中等（< 100ms / 测试）

**关键文件**：
- `Main/Tests/RenderPipelineTest.cpp`
- `UI/2D/Test/ToolsInteropTests.cpp`

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
Main/Tests/
├── RenderCoreTests.cpp       # 渲染核心测试
├── RenderPipelineTest.cpp    # 渲染管线测试
├── CommandLifecycleTests.cpp # 命令生命周期测试

UI/2D/Test/
├── TestMain.cpp              # 测试入口
├── BaseToolTests.cpp         # 基础工具测试
├── ToolManagerTests.cpp      # 工具管理器测试
├── TransformParametersTests.cpp # 变换参数测试
└── ToolsInteropTests.cpp     # 工具交互测试

UI/Common/Test/
└── CommandKernelTests.cpp    # 命令内核测试

UI/3D/Test/
└── OperationId3DMappingTest.cpp # 3D操作ID映射测试
```

### 3.2 测试命名规范

| 元素 | 规范 | 示例 |
|------|------|------|
| 测试套件 | `[模块][组件]Test` | `RenderTypesTest` |
| 测试用例 | `[组件]_[场景]` | `Size2D_Construction` |
| 测试夹具 | `[组件]Test` | `BaseToolTest` |

### 3.3 Mock/Stub 组织

```
UI/2D/Test/Stub/
├── Engine2D/                 # Engine2D 模拟
│   ├── Core/
│   │   └── SceneManager.h
│   ├── SyEntity/
│   │   ├── SyLine.h
│   │   ├── SyCircle.h
│   │   └── SyArc.h
│   └── Edit/
│       └── SceneEditService.h
└── Ui/
    └── ViewWidget/
        └── ViewRenderCoordinator.h
```

---

## 4. 测试策略

### 4.1 覆盖率目标

| 模块 | 目标覆盖率 | 当前覆盖率 |
|------|-----------|-----------|
| 核心渲染类型 | 90% | 85% |
| 渲染管线 | 80% | 75% |
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

## 7. 变更记录

| 日期 | 版本 | 变更内容 | 作者 |
|------|------|----------|------|
| 2026-07-10 | 1.0.0 | 初版：基于当前测试结构编写 | 架构组 |
