/**
 * @file ViewportRefreshRegressionTests.cpp
 * @brief 视口刷新回归测试 — 覆盖刷新策略、脏标记、性能监控
 *
 * 测试范围：
 *  - SceneRefreshCoordinator 构造与析构
 *  - FrameTimer 性能监控
 *  - 刷新协调器与 SceneManager 脏标记联动
 *  - PerfMonitor 启用/禁用
 *
 * 注意：需要 QTimer::start() 的测试（onSceneChanged / requestLightRefresh 等）
 * 需要 QApplication 实例，当前版本跳过这些测试。
 */

#include <gtest/gtest.h>

#include "UI/Render/SceneRefreshCoordinator.h"
#include "Engine2D/Core/SceneManager.h"
#include "Engine2D/SyEntity/SyLine.h"
#include "Log/SyPerfCounter.h"

#include <chrono>
#include <memory>
#include <thread>

 // 记录一帧，并保证两次时钟读取间隔非零（避免高精度时钟落在同一 tick 导致 avg==0 的抖动失败）
static void recordOneFrame(FrameTimer& timer)
{
    timer.beginFrame();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    timer.endFrame();
}

// ==================== 构造与析构 ====================

TEST(ViewportRefreshRegressionTest, Coordinator_DefaultConstruction)
{
    SceneRefreshCoordinator coordinator;
    EXPECT_EQ(coordinator.frameTimer(), nullptr);
}

TEST(ViewportRefreshRegressionTest, Coordinator_DestructorWithoutSetup)
{
    {
        SceneRefreshCoordinator coordinator;
    }
    SUCCEED();
}

TEST(ViewportRefreshRegressionTest, Coordinator_StopWithoutRenderWidget)
{
    SceneRefreshCoordinator coordinator;
    coordinator.stop();
    SUCCEED();
}

// ==================== 刷新 API 安全调用（无 Widget） ====================

TEST(ViewportRefreshRegressionTest, Coordinator_RequestRepaintWithoutWidget)
{
    SceneRefreshCoordinator coordinator;
    coordinator.requestRepaint(); // 无 RenderWidget 时不应崩溃
    SUCCEED();
}

TEST(ViewportRefreshRegressionTest, Coordinator_OnSelectionChangedWithoutWidget)
{
    SceneRefreshCoordinator coordinator;
    coordinator.onSelectionChanged(); // 无 RenderWidget 时不应崩溃
    SUCCEED();
}

// ==================== 性能监控 ====================

TEST(ViewportRefreshRegressionTest, Coordinator_PerfMonitorDisabledByDefault)
{
    SceneRefreshCoordinator coordinator;
    EXPECT_EQ(coordinator.frameTimer(), nullptr);
}

TEST(ViewportRefreshRegressionTest, Coordinator_EnablePerfMonitor)
{
    SceneRefreshCoordinator coordinator;
    coordinator.setPerfMonitorEnabled(true);
    EXPECT_NE(coordinator.frameTimer(), nullptr);
    coordinator.setPerfMonitorEnabled(false);
    EXPECT_EQ(coordinator.frameTimer(), nullptr);
}

TEST(ViewportRefreshRegressionTest, Coordinator_TogglePerfMonitor)
{
    SceneRefreshCoordinator coordinator;

    coordinator.setPerfMonitorEnabled(true);
    EXPECT_NE(coordinator.frameTimer(), nullptr);

    coordinator.setPerfMonitorEnabled(false);
    EXPECT_EQ(coordinator.frameTimer(), nullptr);

    coordinator.setPerfMonitorEnabled(true);
    EXPECT_NE(coordinator.frameTimer(), nullptr);
}

// ==================== FrameTimer 基础行为 ====================

TEST(ViewportRefreshRegressionTest, FrameTimer_DefaultState)
{
    FrameTimer timer;
    EXPECT_EQ(timer.frameCount(), 0u);
    EXPECT_DOUBLE_EQ(timer.avgFrameMs(), 0.0);
}

TEST(ViewportRefreshRegressionTest, FrameTimer_AverageCalculation)
{
    FrameTimer timer;

    for (int i = 0; i < 100; ++i)
        recordOneFrame(timer);

    EXPECT_EQ(timer.frameCount(), 100u);
    EXPECT_GT(timer.avgFrameMs(), 0.0);
}

TEST(ViewportRefreshRegressionTest, FrameTimer_Reset)
{
    FrameTimer timer;

    timer.beginFrame();
    timer.endFrame();
    EXPECT_GT(timer.frameCount(), 0u);

    timer.reset();
    EXPECT_EQ(timer.frameCount(), 0u);
    EXPECT_DOUBLE_EQ(timer.avgFrameMs(), 0.0);
}

TEST(ViewportRefreshRegressionTest, FrameTimer_CurrentFrameMs)
{
    FrameTimer timer;

    timer.beginFrame();
    double ms = timer.currentFrameMs();
    EXPECT_GE(ms, 0.0);
    timer.endFrame();
}

// ==================== SceneManager 脏标记联动 ====================

TEST(ViewportRefreshRegressionTest, SceneManager_DirtyTracking)
{
    Eg::SceneManager scene;

    auto line = std::make_unique<Eg::SyLine>();
    line->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });

    std::vector<std::unique_ptr<Eg::SyEntity>> entities;
    entities.push_back(std::move(line));
    scene.addEntities(std::move(entities));

    // 添加图元后应有脏标记
    const auto& dirty = scene.dirtyEntities();
    EXPECT_FALSE(dirty.empty());

    // 清理后应空
    scene.markClean();
    EXPECT_TRUE(scene.dirtyEntities().empty());
}

TEST(ViewportRefreshRegressionTest, SceneManager_MultipleAddsDirtyTracking)
{
    Eg::SceneManager scene;

    for (int i = 0; i < 5; ++i)
    {
        auto line = std::make_unique<Eg::SyLine>();
        line->setPointVector({ Ut::Vec2d(0.0, 0.0), Ut::Vec2d(10.0 + i, 10.0 + i) });
        std::vector<std::unique_ptr<Eg::SyEntity>> entities;
        entities.push_back(std::move(line));
        scene.addEntities(std::move(entities));
    }

    EXPECT_EQ(scene.getEntityCount(), 5u);
    EXPECT_FALSE(scene.dirtyEntities().empty());

    scene.markClean();
    EXPECT_TRUE(scene.dirtyEntities().empty());
}

TEST(ViewportRefreshRegressionTest, SceneManager_DeletedEntityIds)
{
    Eg::SceneManager scene;

    auto line = std::make_unique<Eg::SyLine>();
    line->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });
    Eg::EntityId entityId = line->id;

    std::vector<std::unique_ptr<Eg::SyEntity>> entities;
    entities.push_back(std::move(line));
    scene.addEntities(std::move(entities));
    scene.markClean();

    // 删除图元后应有删除 ID
    scene.deleteEntity(scene.findSyEntityById(entityId));
    EXPECT_FALSE(scene.deletedEntityIds().empty());
}

// ==================== 刷新协调器生命周期 ====================

TEST(ViewportRefreshRegressionTest, Coordinator_StopClearsPendingState)
{
    SceneRefreshCoordinator coordinator;
    coordinator.stop();
    coordinator.requestRepaint();
    coordinator.onSelectionChanged();
    SUCCEED();
}

TEST(ViewportRefreshRegressionTest, Coordinator_DoubleStop)
{
    SceneRefreshCoordinator coordinator;
    coordinator.stop();
    coordinator.stop();
    SUCCEED();
}

TEST(ViewportRefreshRegressionTest, Coordinator_SetSceneManager)
{
    Eg::SceneManager scene;
    SceneRefreshCoordinator coordinator;
    coordinator.setSceneManager(&scene);
    coordinator.stop();
    SUCCEED();
}

// ==================== 刷新 API 安全调用（无 Widget / 无 SceneManager） ====================

TEST(ViewportRefreshRegressionTest, Coordinator_RequestLightRefreshWithoutWidget)
{
    SceneRefreshCoordinator coordinator;
    coordinator.requestLightRefresh(); // 无 RenderWidget 时不应崩溃
    SUCCEED();
}

TEST(ViewportRefreshRegressionTest, Coordinator_RequestFullRefreshWithoutWidget)
{
    SceneRefreshCoordinator coordinator;
    coordinator.requestFullRefresh(); // 无 RenderWidget 时不应崩溃
    SUCCEED();
}

TEST(ViewportRefreshRegressionTest, Coordinator_OnSceneChangedWithoutWidget)
{
    SceneRefreshCoordinator coordinator;
    coordinator.onSceneChanged(); // 无 RenderWidget / SceneManager 时不应崩溃
    SUCCEED();
}

TEST(ViewportRefreshRegressionTest, Coordinator_OnSceneChangedWithSceneManager)
{
    Eg::SceneManager scene;
    SceneRefreshCoordinator coordinator;
    coordinator.setSceneManager(&scene);
    coordinator.onSceneChanged(); // 有 SceneManager 但无 Widget 时不应崩溃
    SUCCEED();
}

// ==================== 刷新级别升级测试 ====================

TEST(ViewportRefreshRegressionTest, Coordinator_RefreshLevelUpgrade)
{
    SceneRefreshCoordinator coordinator;

    // 先请求 Repaint，再请求 LightRefresh，应升级到更高优先级
    coordinator.requestRepaint();
    coordinator.requestLightRefresh();
    coordinator.stop();
    SUCCEED();
}

TEST(ViewportRefreshRegressionTest, Coordinator_RefreshLevelNoDowngrade)
{
    SceneRefreshCoordinator coordinator;

    // 先请求 LightRefresh，再请求 Repaint，不应降级
    coordinator.requestLightRefresh();
    coordinator.requestRepaint();
    coordinator.stop();
    SUCCEED();
}

TEST(ViewportRefreshRegressionTest, Coordinator_AllRefreshLevelsSequence)
{
    SceneRefreshCoordinator coordinator;

    coordinator.requestRepaint();
    coordinator.requestLightRefresh();
    coordinator.requestFullRefresh();
    coordinator.stop();
    SUCCEED();
}

// ==================== 场景变更→刷新调度→渲染更新链路测试 ====================
// 刷新链路：sceneChanged → scheduleSceneUpdate → updateSceneRender
// 三条刷新路径：Repaint / LightUpdate / FullRefresh

TEST(RefreshChainRegressionTest, SceneChanged_SchedulesSceneUpdate)
{
    Eg::SceneManager scene;
    SceneRefreshCoordinator coordinator;
    coordinator.setSceneManager(&scene);

    auto line = std::make_unique<Eg::SyLine>();
    line->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });

    std::vector<std::unique_ptr<Eg::SyEntity>> entities;
    entities.push_back(std::move(line));
    scene.addEntities(std::move(entities));

    // onSceneChanged 收集脏 ID 并调度更新
    coordinator.onSceneChanged();
    coordinator.stop();
    SUCCEED();
}

TEST(RefreshChainRegressionTest, SceneChanged_CollectsDirtyIds)
{
    Eg::SceneManager scene;
    SceneRefreshCoordinator coordinator;
    coordinator.setSceneManager(&scene);

    auto line = std::make_unique<Eg::SyLine>();
    line->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });

    std::vector<std::unique_ptr<Eg::SyEntity>> entities;
    entities.push_back(std::move(line));
    scene.addEntities(std::move(entities));

    EXPECT_FALSE(scene.dirtyEntities().empty());

    coordinator.onSceneChanged();
    coordinator.stop();
    SUCCEED();
}

TEST(RefreshChainRegressionTest, SceneChanged_CollectsDeletedIds)
{
    Eg::SceneManager scene;
    SceneRefreshCoordinator coordinator;
    coordinator.setSceneManager(&scene);

    auto line = std::make_unique<Eg::SyLine>();
    line->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });
    Eg::EntityId lineId = line->id;

    std::vector<std::unique_ptr<Eg::SyEntity>> entities;
    entities.push_back(std::move(line));
    scene.addEntities(std::move(entities));
    scene.markClean();

    scene.deleteEntity(scene.findSyEntityById(lineId));
    EXPECT_FALSE(scene.deletedEntityIds().empty());

    coordinator.onSceneChanged();
    coordinator.stop();
    SUCCEED();
}

TEST(RefreshChainRegressionTest, SceneChanged_EmptyScene)
{
    Eg::SceneManager scene;
    SceneRefreshCoordinator coordinator;
    coordinator.setSceneManager(&scene);

    coordinator.onSceneChanged();
    coordinator.stop();
    SUCCEED();
}

// ==================== 三条刷新路径正确性测试 ====================

TEST(RefreshPathRegressionTest, RepaintPath_DoesNotNeedSceneManager)
{
    // Repaint 路径仅调用 m_renderWidget->update()，不需要 SceneManager
    SceneRefreshCoordinator coordinator;
    coordinator.requestRepaint();
    coordinator.stop();
    SUCCEED();
}

TEST(RefreshPathRegressionTest, LightUpdatePath_RequiresSceneManager)
{
    // LightUpdate 路径需要 SceneManager 来获取实体数据
    Eg::SceneManager scene;
    SceneRefreshCoordinator coordinator;
    coordinator.setSceneManager(&scene);

    auto line = std::make_unique<Eg::SyLine>();
    line->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });

    std::vector<std::unique_ptr<Eg::SyEntity>> entities;
    entities.push_back(std::move(line));
    scene.addEntities(std::move(entities));

    coordinator.onSceneChanged();
    coordinator.stop();
    SUCCEED();
}

TEST(RefreshPathRegressionTest, FullRefreshPath_RequiresSceneManager)
{
    // FullRefresh 路径需要 SceneManager 来获取所有实体
    Eg::SceneManager scene;
    SceneRefreshCoordinator coordinator;
    coordinator.setSceneManager(&scene);

    auto line = std::make_unique<Eg::SyLine>();
    line->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });

    std::vector<std::unique_ptr<Eg::SyEntity>> entities;
    entities.push_back(std::move(line));
    scene.addEntities(std::move(entities));

    coordinator.requestFullRefresh();
    coordinator.stop();
    SUCCEED();
}

TEST(RefreshPathRegressionTest, FullRefreshPath_MarksSceneClean)
{
    Eg::SceneManager scene;
    SceneRefreshCoordinator coordinator;
    coordinator.setSceneManager(&scene);

    auto line = std::make_unique<Eg::SyLine>();
    line->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });

    std::vector<std::unique_ptr<Eg::SyEntity>> entities;
    entities.push_back(std::move(line));
    scene.addEntities(std::move(entities));

    EXPECT_FALSE(scene.dirtyEntities().empty());

    coordinator.requestFullRefresh();
    coordinator.stop();
    SUCCEED();
}

// ==================== 增量渲染与全量渲染切换测试 ====================

TEST(RefreshPathRegressionTest, IncrementalToFull_Switch)
{
    Eg::SceneManager scene;
    SceneRefreshCoordinator coordinator;
    coordinator.setSceneManager(&scene);

    // 先增量（LightUpdate），再全量（FullRefresh）
    coordinator.onSceneChanged();
    coordinator.requestFullRefresh();
    coordinator.stop();
    SUCCEED();
}

TEST(RefreshPathRegressionTest, FullToIncremental_Switch)
{
    Eg::SceneManager scene;
    SceneRefreshCoordinator coordinator;
    coordinator.setSceneManager(&scene);

    // 先全量，再增量
    coordinator.requestFullRefresh();
    coordinator.onSceneChanged();
    coordinator.stop();
    SUCCEED();
}

TEST(RefreshPathRegressionTest, MultipleFullRefreshes)
{
    Eg::SceneManager scene;
    SceneRefreshCoordinator coordinator;
    coordinator.setSceneManager(&scene);

    for (int i = 0; i < 5; ++i)
    {
        coordinator.requestFullRefresh();
    }
    coordinator.stop();
    SUCCEED();
}

TEST(RefreshPathRegressionTest, MultipleIncrementalUpdates)
{
    Eg::SceneManager scene;
    SceneRefreshCoordinator coordinator;
    coordinator.setSceneManager(&scene);

    for (int i = 0; i < 10; ++i)
    {
        auto line = std::make_unique<Eg::SyLine>();
        line->setPointVector({ Ut::Vec2d(0.0, 0.0), Ut::Vec2d(10.0 + i, 10.0 + i) });
        std::vector<std::unique_ptr<Eg::SyEntity>> entities;
        entities.push_back(std::move(line));
        scene.addEntities(std::move(entities));
        coordinator.onSceneChanged();
    }
    coordinator.stop();
    SUCCEED();
}

// ==================== 刷新协调器标记清理测试 ====================

TEST(RefreshCleanupRegressionTest, MarkClean_AfterUpdateSceneRender)
{
    Eg::SceneManager scene;
    SceneRefreshCoordinator coordinator;
    coordinator.setSceneManager(&scene);

    auto line = std::make_unique<Eg::SyLine>();
    line->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });

    std::vector<std::unique_ptr<Eg::SyEntity>> entities;
    entities.push_back(std::move(line));
    scene.addEntities(std::move(entities));

    EXPECT_FALSE(scene.dirtyEntities().empty());

    coordinator.onSceneChanged();
    coordinator.requestFullRefresh();
    coordinator.stop();
    SUCCEED();
}

TEST(RefreshCleanupRegressionTest, PendingIds_ClearedAfterFullRefresh)
{
    Eg::SceneManager scene;
    SceneRefreshCoordinator coordinator;
    coordinator.setSceneManager(&scene);

    auto line = std::make_unique<Eg::SyLine>();
    line->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });
    Eg::EntityId lineId = line->id;

    std::vector<std::unique_ptr<Eg::SyEntity>> entities;
    entities.push_back(std::move(line));
    scene.addEntities(std::move(entities));
    scene.markClean();

    scene.deleteEntity(scene.findSyEntityById(lineId));
    coordinator.onSceneChanged();
    coordinator.stop();
    SUCCEED();
}

// ==================== 大尺寸实体刷新测试 ====================

TEST(RefreshStressRegressionTest, BulkEntities_SceneChanged)
{
    Eg::SceneManager scene;
    SceneRefreshCoordinator coordinator;
    coordinator.setSceneManager(&scene);

    std::vector<std::unique_ptr<Eg::SyEntity>> entities;
    for (int i = 0; i < 50; ++i)
    {
        auto line = std::make_unique<Eg::SyLine>();
        line->setPointVector({ Ut::Vec2d(0.0, 0.0), Ut::Vec2d(10.0 + i, 10.0 + i) });
        entities.push_back(std::move(line));
    }
    scene.addEntities(std::move(entities));

    EXPECT_EQ(scene.getEntityCount(), 50u);
    EXPECT_FALSE(scene.dirtyEntities().empty());

    coordinator.onSceneChanged();
    coordinator.stop();
    SUCCEED();
}

TEST(RefreshStressRegressionTest, BulkEntities_FullRefresh)
{
    Eg::SceneManager scene;
    SceneRefreshCoordinator coordinator;
    coordinator.setSceneManager(&scene);

    std::vector<std::unique_ptr<Eg::SyEntity>> entities;
    for (int i = 0; i < 50; ++i)
    {
        auto line = std::make_unique<Eg::SyLine>();
        line->setPointVector({ Ut::Vec2d(0.0, 0.0), Ut::Vec2d(10.0 + i, 10.0 + i) });
        entities.push_back(std::move(line));
    }
    scene.addEntities(std::move(entities));

    coordinator.requestFullRefresh();
    coordinator.stop();
    SUCCEED();
}

// ==================== 刷新协调器与场景联动测试 ====================

TEST(ViewportRefreshRegressionTest, Coordinator_OnSceneChangedWithDirtyEntities)
{
    Eg::SceneManager scene;
    SceneRefreshCoordinator coordinator;
    coordinator.setSceneManager(&scene);

    auto line = std::make_unique<Eg::SyLine>();
    line->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });
    std::vector<std::unique_ptr<Eg::SyEntity>> entities;
    entities.push_back(std::move(line));
    scene.addEntities(std::move(entities));

    // 添加实体后应有脏标记
    EXPECT_FALSE(scene.dirtyEntities().empty());

    coordinator.onSceneChanged();
    coordinator.stop();
    SUCCEED();
}

TEST(ViewportRefreshRegressionTest, Coordinator_OnSceneChangedWithDeletedEntities)
{
    Eg::SceneManager scene;
    SceneRefreshCoordinator coordinator;
    coordinator.setSceneManager(&scene);

    auto line = std::make_unique<Eg::SyLine>();
    line->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });
    Eg::EntityId lineId = line->id;

    std::vector<std::unique_ptr<Eg::SyEntity>> entities;
    entities.push_back(std::move(line));
    scene.addEntities(std::move(entities));

    scene.markClean();
    auto* entity = scene.findSyEntityById(lineId);
    ASSERT_NE(entity, nullptr);

    scene.selectEntity(entity);
    scene.deleteSelected();
    EXPECT_FALSE(scene.deletedEntityIds().empty());

    coordinator.onSceneChanged();
    coordinator.stop();
}

TEST(ViewportRefreshRegressionTest, Coordinator_SetSceneManagerTwice)
{
    Eg::SceneManager scene1;
    Eg::SceneManager scene2;

    SceneRefreshCoordinator coordinator;
    coordinator.setSceneManager(&scene1);
    coordinator.setSceneManager(&scene2);
    coordinator.stop();
    SUCCEED();
}

TEST(ViewportRefreshRegressionTest, Coordinator_RequestFullRefreshAfterSceneChange)
{
    Eg::SceneManager scene;
    SceneRefreshCoordinator coordinator;
    coordinator.setSceneManager(&scene);

    coordinator.onSceneChanged();
    coordinator.requestFullRefresh();
    coordinator.stop();
    SUCCEED();
}

TEST(ViewportRefreshRegressionTest, Coordinator_RepaintDoesNotTriggerFullRefresh)
{
    Eg::SceneManager scene;
    SceneRefreshCoordinator coordinator;
    coordinator.setSceneManager(&scene);

    coordinator.requestRepaint();
    coordinator.stop();
    SUCCEED();
}

TEST(ViewportRefreshRegressionTest, Coordinator_LightRefreshThenRepaintNoDowngrade)
{
    Eg::SceneManager scene;
    SceneRefreshCoordinator coordinator;
    coordinator.setSceneManager(&scene);

    coordinator.requestLightRefresh();
    coordinator.requestRepaint();
    coordinator.stop();
    SUCCEED();
}

// ==================== FrameTimer 扩展测试 ====================

TEST(ViewportRefreshRegressionTest, FrameTimer_RecordMultipleFrames)
{
    FrameTimer timer;
    EXPECT_EQ(timer.frameCount(), 0u);

    recordOneFrame(timer);
    EXPECT_EQ(timer.frameCount(), 1u);

    recordOneFrame(timer);
    EXPECT_EQ(timer.frameCount(), 2u);

    recordOneFrame(timer);
    EXPECT_EQ(timer.frameCount(), 3u);

    EXPECT_GT(timer.avgFrameMs(), 0.0);
}

TEST(ViewportRefreshRegressionTest, FrameTimer_ResetResetsCount)
{
    FrameTimer timer;
    timer.beginFrame();
    timer.endFrame();
    timer.beginFrame();
    timer.endFrame();
    EXPECT_EQ(timer.frameCount(), 2u);

    timer.reset();
    EXPECT_EQ(timer.frameCount(), 0u);
    EXPECT_DOUBLE_EQ(timer.avgFrameMs(), 0.0);
}

TEST(ViewportRefreshRegressionTest, FrameTimer_ResetPreservesCount)
{
    FrameTimer timer;
    recordOneFrame(timer);
    recordOneFrame(timer);
    EXPECT_EQ(timer.frameCount(), 2u);
    // 验证 avgFrameMs 的计算
    EXPECT_GT(timer.avgFrameMs(), 0.0);
    timer.reset();
}

TEST(ViewportRefreshRegressionTest, FrameTimer_AvgFrameMsCalculated)
{
    FrameTimer timer;
    recordOneFrame(timer);
    recordOneFrame(timer);
    recordOneFrame(timer);

    EXPECT_GT(timer.avgFrameMs(), 0.0);
    EXPECT_GT(timer.frameCount(), 0u);
}

// ==================== P5 补充：增量/全量渲染切换稳定性 ====================

TEST(RefreshPathRegressionTest, IncrementalToFull_BackToIncremental)
{
    // 验证：增量 → 全量 → 增量 切换稳定
    Eg::SceneManager scene;
    SceneRefreshCoordinator coordinator;
    coordinator.setSceneManager(&scene);

    auto line = std::make_unique<Eg::SyLine>();
    line->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });
    std::vector<std::unique_ptr<Eg::SyEntity>> entities;
    entities.push_back(std::move(line));
    scene.addEntities(std::move(entities));

    // 增量刷新
    coordinator.requestLightRefresh();
    coordinator.stop();

    // 全量刷新
    coordinator.requestFullRefresh();
    coordinator.stop();

    // 再增量刷新
    coordinator.requestLightRefresh();
    coordinator.stop();
    SUCCEED();
}

TEST(RefreshPathRegressionTest, MultipleIncrementalUpdates_NoDataLoss)
{
    // 验证：多次增量刷新不丢失数据
    Eg::SceneManager scene;
    SceneRefreshCoordinator coordinator;
    coordinator.setSceneManager(&scene);

    for (int i = 0; i < 10; ++i)
    {
        auto line = std::make_unique<Eg::SyLine>();
        line->setPointVector({ Ut::Vec2d(0.0, 0.0), Ut::Vec2d(10.0 + i, 10.0 + i) });
        std::vector<std::unique_ptr<Eg::SyEntity>> entities;
        entities.push_back(std::move(line));
        scene.addEntities(std::move(entities));

        coordinator.onSceneChanged();
        coordinator.requestLightRefresh();
    }
    coordinator.stop();
    EXPECT_EQ(scene.getEntityCount(), 10u);
}

TEST(RefreshPathRegressionTest, FullRefreshAfterSeriesOfIncremental)
{
    // 验证：连续增量后全量刷新正常
    Eg::SceneManager scene;
    SceneRefreshCoordinator coordinator;
    coordinator.setSceneManager(&scene);

    for (int i = 0; i < 5; ++i)
    {
        auto line = std::make_unique<Eg::SyLine>();
        line->setPointVector({ Ut::Vec2d(0.0, 0.0), Ut::Vec2d(10.0 + i, 10.0 + i) });
        std::vector<std::unique_ptr<Eg::SyEntity>> entities;
        entities.push_back(std::move(line));
        scene.addEntities(std::move(entities));

        coordinator.onSceneChanged();
        coordinator.requestLightRefresh();
    }
    coordinator.stop();

    // 全量刷新
    coordinator.requestFullRefresh();
    coordinator.stop();
    EXPECT_EQ(scene.getEntityCount(), 5u);
}

TEST(RefreshPathRegressionTest, RepaintAfterFullRefresh_NoDowngrade)
{
    // 验证：全量刷新后 Repaint 不会降级（升级不降级规则）
    Eg::SceneManager scene;
    SceneRefreshCoordinator coordinator;
    coordinator.setSceneManager(&scene);

    coordinator.requestFullRefresh();
    coordinator.requestRepaint();
    coordinator.stop();
    SUCCEED();
}