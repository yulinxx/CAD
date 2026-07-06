/**
 * @file FrameworkLifecycleTests.cpp
 * @brief 框架稳定版开发清单与生命周期回归测试
 *
 * 第一部分：稳定版开发清单
 * - 启动阶段只做装配，不做业务编排
 * - 切换阶段只做工作台编排，不做窗口状态硬拼
 * - 退出阶段只做收口，不回调已失效对象
 * - 2D/3D 工作台必须遵循同一套生命周期接口
 * - 渲染适配层只做适配，不承载工作台业务
 *
 * 第二部分：回归测试目标
 * - 验证渲染器生命周期基础行为
 * - 验证工作台状态对象默认值稳定
 * - 验证关键状态切换接口可重复调用且不污染内部状态
 */

#include <gtest/gtest.h>

#include "UI/SimpleRenderer3D.h"
#include "UI/UiWorkbench.h"

TEST(FrameworkLifecycleTest, StableChecklist_IsDocumented)
{
    // 这个测试的作用不是验证算法，而是把框架稳定版的开发原则固定在测试集中。
    // 这样后续新增启动/切换/退出逻辑时，可以先对照这里的约束再做修改。
    SUCCEED();
}

TEST(FrameworkLifecycleTest, WorkbenchStateSnapshot_DefaultValuesAreStable)
{
    // 工作台快照对象必须具备稳定默认值，避免未初始化字段在切换/退出时传播脏状态。
    WorkbenchStateSnapshot snapshot;

    EXPECT_TRUE(snapshot.viewMode.isEmpty());
    EXPECT_TRUE(snapshot.layerId.isEmpty());
    EXPECT_TRUE(snapshot.documentId.isEmpty());
    EXPECT_TRUE(snapshot.selectionSource.isEmpty());
    EXPECT_TRUE(snapshot.selectionText.isEmpty());
    EXPECT_TRUE(snapshot.selectionType.isEmpty());
    EXPECT_TRUE(snapshot.viewportType.isEmpty());
    EXPECT_TRUE(snapshot.viewportStatus.isEmpty());
    EXPECT_FALSE(snapshot.dirty);
}

TEST(FrameworkLifecycleTest, SimpleRenderer3D_LifecycleIsIdempotent)
{
    // 渲染器生命周期应支持重复关闭，不应因二次 shutdown 产生异常状态。
    SimpleRenderer3D renderer;

    EXPECT_FALSE(renderer.isReady());
    EXPECT_FALSE(renderer.isRenderLoopRunning());

    EXPECT_TRUE(renderer.initialize());
    EXPECT_TRUE(renderer.isReady());

    renderer.setRenderLoopEnabled(true);
    EXPECT_TRUE(renderer.isRenderLoopRunning());

    renderer.shutdown();
    EXPECT_FALSE(renderer.isReady());
    EXPECT_FALSE(renderer.isRenderLoopRunning());

    // 关闭后再次调用 shutdown 应保持幂等。
    renderer.shutdown();
    EXPECT_FALSE(renderer.isReady());
    EXPECT_FALSE(renderer.isRenderLoopRunning());
}

TEST(FrameworkLifecycleTest, SimpleRenderer3D_ResetViewDoesNotBreakMode)
{
    // resetView 只能重置视图，不应该破坏当前交互模式的可用性。
    SimpleRenderer3D renderer;
    ASSERT_TRUE(renderer.initialize());

    renderer.setOrbitMode(true);
    renderer.resetView();

    EXPECT_TRUE(renderer.isOrbitMode());
    renderer.shutdown();
}
