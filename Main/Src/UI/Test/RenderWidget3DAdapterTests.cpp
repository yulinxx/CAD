/**
 * @file RenderWidget3DAdapterTests.cpp
 * @brief RenderWidget3DAdapter 回归测试 — 覆盖生命周期、回调、状态查询、接口兼容
 *
 * P5 测试扩展 (2026-07-30): 补 RenderWidget3DAdapter 的最小闭环测试
 *
 * 注意：需要 QWidget parent 的测试（initialize/setScene/setCamera）需要 QApplication 实例，
 * 当前版本以组件级测试为主。
 */

#include <gtest/gtest.h>
#include "UI/Render/RenderWidget3DAdapter.h"

// ==================== 生命周期测试 ====================

TEST(RenderWidget3DAdapterTest, Construction)
{
    RenderWidget3DAdapter adapter;
    EXPECT_FALSE(adapter.isReady());
    EXPECT_FALSE(adapter.isRenderLoopRunning());
    EXPECT_TRUE(adapter.isOpenGL());
}

TEST(RenderWidget3DAdapterTest, DestructorSafeWithoutInit)
{
    {
        RenderWidget3DAdapter adapter;
    }
    SUCCEED();
}

TEST(RenderWidget3DAdapterTest, ShutdownWithoutInit)
{
    RenderWidget3DAdapter adapter;
    adapter.shutdown();
    EXPECT_FALSE(adapter.isReady());
}

// ==================== 状态查询测试 ====================

TEST(RenderWidget3DAdapterTest, IsOpenGLReturnsTrue)
{
    RenderWidget3DAdapter adapter;
    EXPECT_TRUE(adapter.isOpenGL());
}

TEST(RenderWidget3DAdapterTest, RenderLoopNotRunningWithoutInit)
{
    RenderWidget3DAdapter adapter;
    EXPECT_FALSE(adapter.isRenderLoopRunning());

    adapter.setRenderLoopEnabled(true);
    EXPECT_FALSE(adapter.isRenderLoopRunning());  // 未初始化，渲染循环不运行
}

TEST(RenderWidget3DAdapterTest, SelectedNodeId_DefaultEmpty)
{
    RenderWidget3DAdapter adapter;
    EXPECT_EQ(adapter.selectedNodeId(), QString());
    EXPECT_TRUE(adapter.selectedPathNames().isEmpty());
}

// ==================== 场景与相机绑定测试 ====================

TEST(RenderWidget3DAdapterTest, SetScene_NullIsSafe)
{
    RenderWidget3DAdapter adapter;
    adapter.setScene(nullptr);
    SUCCEED();
}

TEST(RenderWidget3DAdapterTest, SetCamera_NullIsSafe)
{
    RenderWidget3DAdapter adapter;
    adapter.setCamera(nullptr);
    SUCCEED();
}

// ==================== 视图控制测试 ====================

TEST(RenderWidget3DAdapterTest, ResetView_WithoutInit)
{
    RenderWidget3DAdapter adapter;
    adapter.resetView();
    SUCCEED();
}

TEST(RenderWidget3DAdapterTest, Resize_WithoutInit)
{
    RenderWidget3DAdapter adapter;
    adapter.resize(800, 600);
    adapter.resize(1024, 768);
    SUCCEED();
}

// ==================== 轨道/测量模式测试 ====================

TEST(RenderWidget3DAdapterTest, OrbitMode_Default)
{
    RenderWidget3DAdapter adapter;
    // 适配器 isOrbitMode 始终返回 true（由内部 RenderWidget3D 管理）
    EXPECT_TRUE(adapter.isOrbitMode());
}

TEST(RenderWidget3DAdapterTest, SetOrbitMode)
{
    RenderWidget3DAdapter adapter;
    // setOrbitMode 是空操作，接口保留兼容性
    adapter.setOrbitMode(false);
    EXPECT_TRUE(adapter.isOrbitMode());

    adapter.setOrbitMode(true);
    EXPECT_TRUE(adapter.isOrbitMode());
}

TEST(RenderWidget3DAdapterTest, SetMeasureMode_WithoutInit)
{
    RenderWidget3DAdapter adapter;
    adapter.setMeasureMode(true);
    adapter.setMeasureMode(false);
    SUCCEED();
}

// ==================== 回调测试 ====================

TEST(RenderWidget3DAdapterTest, StatusCallback_NullIsSafe)
{
    RenderWidget3DAdapter adapter;
    adapter.setStatusCallback(nullptr);
    SUCCEED();
}

TEST(RenderWidget3DAdapterTest, SelectionCallback_NullIsSafe)
{
    RenderWidget3DAdapter adapter;
    adapter.setSelectionCallback(nullptr);
    SUCCEED();
}

TEST(RenderWidget3DAdapterTest, PathCallback_NullIsSafe)
{
    RenderWidget3DAdapter adapter;
    adapter.setPathCallback(nullptr);
    SUCCEED();
}

TEST(RenderWidget3DAdapterTest, StatusCallback_IsStored)
{
    RenderWidget3DAdapter adapter;
    bool called = false;
    adapter.setStatusCallback([&called](const QString&) {
        called = true;
    });
    // 回调已存储，但无 widget 时不会触发
    SUCCEED();
}

// ==================== 选中节点测试 ====================

TEST(RenderWidget3DAdapterTest, SelectNodeById_WithoutInit)
{
    RenderWidget3DAdapter adapter;
    adapter.selectNodeById("test_node");
    SUCCEED();
}

// ==================== Widget 访问测试 ====================

TEST(RenderWidget3DAdapterTest, Widget_ReturnsNullWithoutInit)
{
    RenderWidget3DAdapter adapter;
    EXPECT_EQ(adapter.widget(), nullptr);
}

// ==================== 渲染循环控制测试 ====================

TEST(RenderWidget3DAdapterTest, RenderLoop_ToggleWithoutInit)
{
    RenderWidget3DAdapter adapter;
    adapter.setRenderLoopEnabled(true);
    EXPECT_FALSE(adapter.isRenderLoopRunning());
    adapter.setRenderLoopEnabled(false);
    EXPECT_FALSE(adapter.isRenderLoopRunning());
}

// ==================== 多次 shutdown 测试 ====================

TEST(RenderWidget3DAdapterTest, DoubleShutdown)
{
    RenderWidget3DAdapter adapter;
    adapter.shutdown();
    adapter.shutdown();
    EXPECT_FALSE(adapter.isReady());
}

// ==================== 空路径选择测试 ====================

TEST(RenderWidget3DAdapterTest, SelectedPathNames_DefaultEmpty)
{
    RenderWidget3DAdapter adapter;
    EXPECT_TRUE(adapter.selectedPathNames().isEmpty());
}

// ==================== 回调生命周期测试 ====================

TEST(RenderWidget3DAdapterTest, Callbacks_AllThreeCanBeSet)
{
    RenderWidget3DAdapter adapter;

    int statusCalls = 0;
    int selectionCalls = 0;
    int pathCalls = 0;

    adapter.setStatusCallback([&](const QString&) {
        statusCalls++;
    });
    adapter.setSelectionCallback([&](const QString&) {
        selectionCalls++;
    });
    adapter.setPathCallback([&](const QStringList&) {
        pathCalls++;
    });

    // 回调已存储，无 widget 时不会触发
    SUCCEED();
}

TEST(RenderWidget3DAdapterTest, Callbacks_ResetWithNull)
{
    RenderWidget3DAdapter adapter;

    adapter.setStatusCallback([](const QString&) {});
    adapter.setStatusCallback(nullptr);  // 重置为 nullptr 不崩溃

    adapter.setSelectionCallback([](const QString&) {});
    adapter.setSelectionCallback(nullptr);

    adapter.setPathCallback([](const QStringList&) {});
    adapter.setPathCallback(nullptr);

    SUCCEED();
}

// ==================== 视图模式组合测试 ====================

TEST(RenderWidget3DAdapterTest, MeasureAndOrbitMode_Combination)
{
    RenderWidget3DAdapter adapter;

    adapter.setMeasureMode(true);
    EXPECT_TRUE(adapter.isOrbitMode());

    adapter.setMeasureMode(false);
    EXPECT_TRUE(adapter.isOrbitMode());

    adapter.setOrbitMode(false);
    EXPECT_TRUE(adapter.isOrbitMode());  // 始终返回 true
}