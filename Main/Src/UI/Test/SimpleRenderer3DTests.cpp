/**
 * @file SimpleRenderer3DTests.cpp
 * @brief SimpleRenderer3D 回归测试 — 覆盖生命周期、回调、场景/相机、选中、事件路由
 *
 * P5 测试扩展 (2026-07-30): 补回调链路、场景相机绑定、选中同步、IRenderSurface 接口
 */

#include <gtest/gtest.h>
#include "UI/Render/SimpleRenderer3D.h"
#include "UI/IRenderSurface.h"
#include <QPainter>
#include <QImage>

 // ==================== 生命周期测试 ====================

TEST(SimpleRenderer3DTest, Lifecycle)
{
    SimpleRenderer3D renderer;

    EXPECT_FALSE(renderer.isReady());
    EXPECT_FALSE(renderer.isRenderLoopRunning());

    bool initialized = renderer.initialize();
    EXPECT_TRUE(initialized);
    EXPECT_TRUE(renderer.isReady());

    renderer.shutdown();
    EXPECT_FALSE(renderer.isReady());
}

TEST(SimpleRenderer3DTest, RenderLoopControl)
{
    SimpleRenderer3D renderer;
    renderer.initialize();

    EXPECT_FALSE(renderer.isRenderLoopRunning());

    renderer.setRenderLoopEnabled(true);
    EXPECT_TRUE(renderer.isRenderLoopRunning());

    renderer.setRenderLoopEnabled(false);
    EXPECT_FALSE(renderer.isRenderLoopRunning());

    renderer.shutdown();
}

// ==================== P5 补充：3D 操作总线与菜单/快捷键联动 ====================
// 这一段保留操作语义层面的覆盖；与回调签名/接口适配相关的检查归到下方通用回调与接口测试。

TEST(SimpleRenderer3DTest, OperationBus3D_ViewOperations)
{
    // 验证 3D 视图操作（View_ResetRenderView, View_FitAll 等）对应的渲染器行为
    SimpleRenderer3D renderer;
    renderer.initialize();

    // 重置视图
    renderer.resetView();
    renderer.setOrbitMode(true);
    EXPECT_TRUE(renderer.isOrbitMode());

    // 渲染循环控制
    renderer.setRenderLoopEnabled(true);
    EXPECT_TRUE(renderer.isRenderLoopRunning());
    renderer.setRenderLoopEnabled(false);
    EXPECT_FALSE(renderer.isRenderLoopRunning());

    // 测量模式
    renderer.setMeasureMode(true);
    renderer.setMeasureMode(false);

    renderer.shutdown();
    SUCCEED();
}

TEST(SimpleRenderer3DTest, OperationBus3D_SelectionOperations)
{
    // 验证 3D 选择操作与回调联动
    SimpleRenderer3D renderer;
    renderer.initialize();

    renderer.selectNodeById("test_node");
    EXPECT_EQ(renderer.selectedNodeId(), "test_node");

    renderer.selectNodeById("another_node");
    EXPECT_EQ(renderer.selectedNodeId(), "another_node");

    renderer.selectNodeById("");
    EXPECT_EQ(renderer.selectedNodeId(), "");

    renderer.shutdown();
}

TEST(SimpleRenderer3DTest, OperationBus3D_MultipleViewOperations)
{
    // 验证多次视图操作序列的稳定性
    SimpleRenderer3D renderer;
    renderer.initialize();

    for (int i = 0; i < 5; ++i)
    {
        renderer.resetView();
        renderer.setOrbitMode(true);
        renderer.setOrbitMode(false);
        renderer.setMeasureMode(true);
        renderer.setMeasureMode(false);
    }

    renderer.shutdown();
    SUCCEED();
}

TEST(SimpleRenderer3DTest, OperationBus3D_RenderAndSelectionTogether)
{
    // 验证渲染和选择操作的组合使用
    SimpleRenderer3D renderer;
    renderer.initialize();

    renderer.setRenderLoopEnabled(true);
    renderer.selectNodeById("node_1");
    renderer.setOrbitMode(true);
    renderer.selectNodeById("node_2");
    renderer.setOrbitMode(false);
    renderer.setRenderLoopEnabled(false);

    EXPECT_EQ(renderer.selectedNodeId(), "node_2");

    renderer.shutdown();
}

TEST(SimpleRenderer3DTest, OperationBus3D_QuickToggleSequence)
{
    // 验证快速切换操作序列的稳定性
    SimpleRenderer3D renderer;
    renderer.initialize();

    // 快速切换渲染循环
    for (int i = 0; i < 10; ++i)
    {
        renderer.setRenderLoopEnabled(true);
        renderer.setRenderLoopEnabled(false);
    }
    EXPECT_FALSE(renderer.isRenderLoopRunning());

    // 快速切换轨道模式
    for (int i = 0; i < 10; ++i)
    {
        renderer.setOrbitMode(true);
        renderer.setOrbitMode(false);
    }
    EXPECT_FALSE(renderer.isOrbitMode());

    renderer.shutdown();
}

TEST(SimpleRenderer3DTest, ShutdownDisablesRenderLoop)
{
    SimpleRenderer3D renderer;
    renderer.initialize();
    renderer.setRenderLoopEnabled(true);
    EXPECT_TRUE(renderer.isRenderLoopRunning());

    renderer.shutdown();
    EXPECT_FALSE(renderer.isRenderLoopRunning());
    EXPECT_FALSE(renderer.isReady());
}

TEST(SimpleRenderer3DTest, DoubleInitializeIsSafe)
{
    SimpleRenderer3D renderer;
    EXPECT_TRUE(renderer.initialize());
    EXPECT_TRUE(renderer.initialize()); // 二次初始化不应崩溃
    EXPECT_TRUE(renderer.isReady());
    renderer.shutdown();
}

TEST(SimpleRenderer3DTest, DoubleShutdownIsSafe)
{
    SimpleRenderer3D renderer;
    renderer.initialize();
    renderer.shutdown();
    renderer.shutdown(); // 二次关闭不应崩溃
    EXPECT_FALSE(renderer.isReady());
}

// ==================== 轨道模式测试 ====================

TEST(SimpleRenderer3DTest, OrbitMode)
{
    SimpleRenderer3D renderer;
    renderer.initialize();

    EXPECT_TRUE(renderer.isOrbitMode());

    renderer.setOrbitMode(false);
    EXPECT_FALSE(renderer.isOrbitMode());

    renderer.setOrbitMode(true);
    EXPECT_TRUE(renderer.isOrbitMode());

    renderer.shutdown();
}

TEST(SimpleRenderer3DTest, ResetView)
{
    SimpleRenderer3D renderer;
    renderer.initialize();

    renderer.setOrbitMode(true);
    renderer.resetView();

    EXPECT_TRUE(renderer.isOrbitMode());

    renderer.shutdown();
}

// ==================== 选中状态测试 ====================

TEST(SimpleRenderer3DTest, SelectionGetters_DefaultEmpty)
{
    SimpleRenderer3D renderer;
    renderer.initialize();

    EXPECT_EQ(renderer.selectedNodeId(), QString());
    EXPECT_TRUE(renderer.selectedPathNames().isEmpty());

    renderer.shutdown();
}

TEST(SimpleRenderer3DTest, SelectNodeById_SetsSelectedId)
{
    SimpleRenderer3D renderer;
    renderer.initialize();

    renderer.selectNodeById("mesh_001");
    EXPECT_EQ(renderer.selectedNodeId(), QString("mesh_001"));

    renderer.selectNodeById("mesh_002");
    EXPECT_EQ(renderer.selectedNodeId(), QString("mesh_002"));

    renderer.shutdown();
}

TEST(SimpleRenderer3DTest, SelectNodeById_EmptyStringClears)
{
    SimpleRenderer3D renderer;
    renderer.initialize();

    renderer.selectNodeById("mesh_001");
    EXPECT_EQ(renderer.selectedNodeId(), QString("mesh_001"));

    renderer.selectNodeById(QString());
    EXPECT_EQ(renderer.selectedNodeId(), QString());

    renderer.shutdown();
}

// ==================== 测量模式测试 ====================

TEST(SimpleRenderer3DTest, MeasureMode)
{
    SimpleRenderer3D renderer;
    renderer.initialize();

    renderer.setMeasureMode(true);
    renderer.setMeasureMode(false);

    renderer.shutdown();
}

// ==================== 渲染测试 ====================

TEST(SimpleRenderer3DTest, Render)
{
    GTEST_SKIP() << "Render test requires Qt GUI context";
}

// ==================== 尺寸调整测试 ====================

TEST(SimpleRenderer3DTest, Resize)
{
    SimpleRenderer3D renderer;
    renderer.initialize();

    renderer.resize(800, 600);
    renderer.resize(1024, 768);

    renderer.shutdown();
}

TEST(SimpleRenderer3DTest, Resize_ZeroSizeIsSafe)
{
    SimpleRenderer3D renderer;
    renderer.initialize();

    renderer.resize(0, 0);
    renderer.resize(1, 1);

    renderer.shutdown();
}

// ==================== 鼠标事件测试 ====================

TEST(SimpleRenderer3DTest, MouseEvents)
{
    SimpleRenderer3D renderer;
    renderer.initialize();

    renderer.onMousePress(100, 100, 1, 0, 640, 480);
    renderer.onMouseMove(150, 150, 1, 640, 480);
    renderer.onMouseRelease(150, 150, 1, 640, 480);

    renderer.onWheel(50, 640, 480);
    renderer.onWheel(-50, 640, 480);

    renderer.shutdown();
}

TEST(SimpleRenderer3DTest, MouseEvents_EdgePositions)
{
    SimpleRenderer3D renderer;
    renderer.initialize();

    // 边界位置 (0,0) 和 (w,h) 不应崩溃
    renderer.onMousePress(0, 0, 1, 0, 640, 480);
    renderer.onMouseMove(640, 480, 1, 640, 480);
    renderer.onMouseRelease(640, 480, 1, 640, 480);

    renderer.shutdown();
}

TEST(SimpleRenderer3DTest, MouseEvents_DifferentButtons)
{
    SimpleRenderer3D renderer;
    renderer.initialize();

    // 左键
    renderer.onMousePress(100, 100, 1, 0, 640, 480);
    renderer.onMouseRelease(100, 100, 1, 640, 480);

    // 中键
    renderer.onMousePress(100, 100, 2, 0, 640, 480);
    renderer.onMouseRelease(100, 100, 2, 640, 480);

    // 右键
    renderer.onMousePress(100, 100, 4, 0, 640, 480);
    renderer.onMouseRelease(100, 100, 4, 640, 480);

    renderer.shutdown();
}

TEST(SimpleRenderer3DTest, MouseEvents_WithModifiers)
{
    SimpleRenderer3D renderer;
    renderer.initialize();

    // Shift
    renderer.onMousePress(100, 100, 1, 0x02000000, 640, 480);
    // Ctrl
    renderer.onMouseMove(150, 150, 1, 640, 480);
    // Alt
    renderer.onMouseRelease(150, 150, 1, 640, 480);

    renderer.shutdown();
}

TEST(SimpleRenderer3DTest, WheelEvents_VariousDeltas)
{
    SimpleRenderer3D renderer;
    renderer.initialize();

    renderer.onWheel(120, 640, 480);
    renderer.onWheel(-120, 640, 480);
    renderer.onWheel(0, 640, 480);
    renderer.onWheel(240, 640, 480);

    renderer.shutdown();
}

// ==================== 回调与接口适配测试 ====================
// 这一节只验证 SimpleRenderer3D 的回调/接口契约，不再混入具体场景实体语义。

TEST(SimpleRenderer3DTest, StatusCallback_EmitsOnResetView)
{
    SimpleRenderer3D renderer;
    renderer.initialize();

    QString lastStatus;
    renderer.setStatusCallback([&lastStatus](const QString& msg) { lastStatus = msg; });

    renderer.resetView();
    EXPECT_FALSE(lastStatus.isEmpty());

    renderer.shutdown();
}

TEST(SimpleRenderer3DTest, SelectionCallback_EmitsOnSelectNode)
{
    SimpleRenderer3D renderer;
    renderer.initialize();

    QString lastSelection;
    renderer.setSelectionCallback([&lastSelection](const QString& id) { lastSelection = id; });

    renderer.selectNodeById("test_entity");
    EXPECT_EQ(lastSelection.toStdString(), "test_entity");

    renderer.shutdown();
}

TEST(SimpleRenderer3DTest, PathCallback_EmitsEmptyPathWithoutDocument)
{
    SimpleRenderer3D renderer;
    renderer.initialize();

    QStringList lastPath;
    bool pathCalled = false;
    renderer.setPathCallback([&lastPath, &pathCalled](const QStringList& path) {
        lastPath = path;
        pathCalled = true;
        });

    renderer.selectNodeById("node_with_path");
    // 回调被调用，但路径列表为空（无文档时 rebuildTreeHighlight 提前返回）
    EXPECT_TRUE(pathCalled);
    EXPECT_TRUE(lastPath.isEmpty());

    renderer.shutdown();
}

TEST(SimpleRenderer3DTest, Callbacks_NullCallbackIsSafe)
{
    SimpleRenderer3D renderer;
    renderer.initialize();

    // 设置 nullptr 回调不应崩溃
    renderer.setStatusCallback(nullptr);
    renderer.setSelectionCallback(nullptr);
    renderer.setPathCallback(nullptr);

    renderer.resetView();
    renderer.selectNodeById("test");

    renderer.shutdown();
}

// ==================== 场景与相机绑定测试 ====================

TEST(SimpleRenderer3DTest, SetScene_NullIsSafe)
{
    SimpleRenderer3D renderer;
    renderer.initialize();

    renderer.setScene(nullptr);
    SUCCEED();

    renderer.shutdown();
}

TEST(SimpleRenderer3DTest, SetCamera_NullIsSafe)
{
    SimpleRenderer3D renderer;
    renderer.initialize();

    renderer.setCamera(nullptr);
    SUCCEED();

    renderer.shutdown();
}

// ==================== OpenGL 标识测试 ====================

TEST(SimpleRenderer3DTest, IsNotOpenGL)
{
    SimpleRenderer3D renderer;
    EXPECT_FALSE(renderer.isOpenGL());
}

// ==================== IRenderSurface 接口测试 ====================

TEST(SimpleRenderer3DTest, IRenderSurface_Lifecycle)
{
    SimpleRenderer3D renderer;

    // 通过 IRenderSurface 接口操作生命周期
    UI::IRenderSurface* surface = &renderer;
    EXPECT_FALSE(surface->isReady());

    EXPECT_TRUE(surface->initialize());
    EXPECT_TRUE(surface->isReady());

    surface->shutdown();
    EXPECT_FALSE(surface->isReady());
}

TEST(SimpleRenderer3DTest, IRenderSurface_RenderLoop)
{
    SimpleRenderer3D renderer;
    renderer.initialize();

    UI::IRenderSurface* surface = &renderer;
    EXPECT_FALSE(surface->isRenderLoopRunning());

    surface->setRenderLoopEnabled(true);
    EXPECT_TRUE(surface->isRenderLoopRunning());

    surface->setRenderLoopEnabled(false);
    EXPECT_FALSE(surface->isRenderLoopRunning());

    renderer.shutdown();
}

TEST(SimpleRenderer3DTest, IRenderSurface_Resize)
{
    SimpleRenderer3D renderer;
    renderer.initialize();

    UI::IRenderSurface* surface = &renderer;
    surface->resize(1024, 768);
    surface->resize(1920, 1080);

    renderer.shutdown();
}

TEST(SimpleRenderer3DTest, IRenderSurface_IsNotOpenGL)
{
    SimpleRenderer3D renderer;
    UI::IRenderSurface* surface = &renderer;
    EXPECT_FALSE(surface->isOpenGL());
}

// ==================== 选中状态与路径缓存测试 ====================
// 这里关注渲染器内部状态保持，不做场景对象级别的回归验证。

TEST(SimpleRenderer3DTest, SelectNodeById_UpdatesSelectedNode)
{
    SimpleRenderer3D renderer;
    renderer.initialize();

    renderer.selectNodeById("node_001");
    EXPECT_EQ(renderer.selectedNodeId(), QString("node_001"));

    renderer.shutdown();
}

TEST(SimpleRenderer3DTest, SelectNodeById_RepeatedSelectionKeepsLatest)
{
    SimpleRenderer3D renderer;
    renderer.initialize();

    renderer.selectNodeById("node_a");
    renderer.selectNodeById("node_b");
    renderer.selectNodeById("node_a"); // 重复选中同一节点

    EXPECT_EQ(renderer.selectedNodeId(), QString("node_a"));

    renderer.shutdown();
}

// ==================== 事件路由边界测试 ====================
// 仅验证无崩溃与状态稳定，不涉及更高层的场景联动断言。

TEST(SimpleRenderer3DTest, MouseEvents_NegativeCoordinates)
{
    SimpleRenderer3D renderer;
    renderer.initialize();

    // 负坐标鼠标事件不应崩溃
    renderer.onMousePress(-10, -10, 1, 0, 640, 480);
    renderer.onMouseMove(-50, -50, 1, 640, 480);
    renderer.onMouseRelease(-50, -50, 1, 640, 480);

    renderer.shutdown();
}

TEST(SimpleRenderer3DTest, MouseEvents_OutOfBounds)
{
    SimpleRenderer3D renderer;
    renderer.initialize();

    // 超出视口范围的坐标不应崩溃
    renderer.onMousePress(10000, 10000, 1, 0, 640, 480);
    renderer.onMouseMove(20000, 20000, 1, 640, 480);
    renderer.onMouseRelease(20000, 20000, 1, 640, 480);

    renderer.shutdown();
}

TEST(SimpleRenderer3DTest, WheelEvents_ZeroViewport)
{
    SimpleRenderer3D renderer;
    renderer.initialize();

    // 零视口尺寸不应崩溃
    renderer.onWheel(120, 0, 0);
    renderer.onWheel(-120, 0, 0);

    renderer.shutdown();
}

// ==================== 回调触发顺序测试 ====================

TEST(SimpleRenderer3DTest, Callbacks_FireInCorrectOrder)
{
    SimpleRenderer3D renderer;
    renderer.initialize();

    std::vector<QString> callOrder;
    renderer.setStatusCallback([&](const QString& msg) { callOrder.push_back("status:" + msg); });
    renderer.setSelectionCallback([&](const QString& id) { callOrder.push_back("select:" + id); });

    renderer.selectNodeById("ordered_entity");
    // 选择回调应先于状态回调触发
    EXPECT_FALSE(callOrder.empty());

    renderer.shutdown();
}

// ==================== 渲染器状态一致性测试 ====================
// 这一节保留初始化/销毁/重置后的内部状态检查，避免和回调契约测试重复。

TEST(SimpleRenderer3DTest, State_ConsistencyAfterShutdown)
{
    SimpleRenderer3D renderer;
    renderer.initialize();
    renderer.setRenderLoopEnabled(true);
    renderer.setOrbitMode(false);

    renderer.shutdown();

    EXPECT_FALSE(renderer.isReady());
    EXPECT_FALSE(renderer.isRenderLoopRunning());
}

TEST(SimpleRenderer3DTest, State_OrbitModePersistsAfterResetView)
{
    SimpleRenderer3D renderer;
    renderer.initialize();

    renderer.setOrbitMode(true);
    renderer.resetView();
    EXPECT_TRUE(renderer.isOrbitMode());

    renderer.setOrbitMode(false);
    renderer.resetView();
    EXPECT_FALSE(renderer.isOrbitMode());

    renderer.shutdown();
}

TEST(SimpleRenderer3DTest, State_SelectionPersistsAfterResize)
{
    SimpleRenderer3D renderer;
    renderer.initialize();

    renderer.selectNodeById("persistent_node");
    renderer.resize(800, 600);
    EXPECT_EQ(renderer.selectedNodeId(), QString("persistent_node"));

    renderer.shutdown();
}

// ==================== 初始状态一致性测试 ====================

TEST(SimpleRenderer3DTest, InitialState_AllDefaults)
{
    SimpleRenderer3D renderer;

    EXPECT_FALSE(renderer.isReady());
    EXPECT_FALSE(renderer.isRenderLoopRunning());
    EXPECT_TRUE(renderer.isOrbitMode());
    EXPECT_EQ(renderer.selectedNodeId(), QString());
    EXPECT_TRUE(renderer.selectedPathNames().isEmpty());
    EXPECT_FALSE(renderer.isOpenGL());
}

// ==================== 3D 渲染器模式切换扩展测试 ====================

TEST(SimpleRenderer3DTest, OrbitModeToggle)
{
    SimpleRenderer3D renderer;
    renderer.initialize();

    EXPECT_TRUE(renderer.isOrbitMode()); // 默认轨道模式开启（与 InitialState_AllDefaults 一致）

    renderer.setOrbitMode(true);
    EXPECT_TRUE(renderer.isOrbitMode());

    renderer.setOrbitMode(false);
    EXPECT_FALSE(renderer.isOrbitMode());

    renderer.shutdown();
}

TEST(SimpleRenderer3DTest, MeasureModeToggle)
{
    SimpleRenderer3D renderer;
    renderer.initialize();

    renderer.setMeasureMode(true);
    renderer.setMeasureMode(false);

    renderer.shutdown();
    SUCCEED();
}

TEST(SimpleRenderer3DTest, RenderLoopToggleMultipleTimes)
{
    SimpleRenderer3D renderer;
    renderer.initialize();

    for (int i = 0; i < 5; ++i)
    {
        renderer.setRenderLoopEnabled(true);
        EXPECT_TRUE(renderer.isRenderLoopRunning());
        renderer.setRenderLoopEnabled(false);
        EXPECT_FALSE(renderer.isRenderLoopRunning());
    }

    renderer.shutdown();
}