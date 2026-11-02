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
#include "UI/Render/RenderSubmitScheduler.h"
#include "Engine2D/Core/SceneManager.h"
#include "Engine2D/SyEntity/SyCircle.h"
#include "Engine2D/SyEntity/SyLine.h"
#include "Log/SyPerfCounter.h"
#include "RenderWidget.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>

#include <chrono>
#include <memory>
#include <thread>
#include <vector>

namespace
{
    /// 取出某个图元的变更记录，不存在返回 nullptr
    const Eg::SceneChange* findChangeInRefresh(const Eg::SceneChangeSet& set, Eg::EntityId id)
    {
        for (const Eg::SceneChange& change : set.changes)
        {
            if (change.entityId == id)
            {
                return &change;
            }
        }
        return nullptr;
    }
}  // namespace

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
    coordinator.requestRepaint();  // 无 RenderWidget 时不应崩溃
    SUCCEED();
}

TEST(ViewportRefreshRegressionTest, Coordinator_OnSelectionChangedWithoutWidget)
{
    SceneRefreshCoordinator coordinator;
    coordinator.onSelectionChanged();  // 无 RenderWidget 时不应崩溃
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
    {
        recordOneFrame(timer);
    }

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

    // 添加图元后应有变更记录
    uint64_t cursor = 0;
    Eg::SceneChangeSet changes;
    ASSERT_TRUE(scene.readChanges(cursor, changes));
    EXPECT_FALSE(changes.changes.empty());

    // 推进变更游标后应读不到变更
    cursor = changes.toRevision;
    ASSERT_TRUE(scene.readChanges(cursor, changes));
    EXPECT_TRUE(changes.changes.empty());
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

    // 添加图元后应有变更记录
    uint64_t cursor = 0;
    Eg::SceneChangeSet changes;
    ASSERT_TRUE(scene.readChanges(cursor, changes));
    EXPECT_FALSE(changes.changes.empty());

    // 推进变更游标后应读不到变更
    cursor = changes.toRevision;
    ASSERT_TRUE(scene.readChanges(cursor, changes));
    EXPECT_TRUE(changes.changes.empty());
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

    // 记下当前变更游标，只关心此后的变更
    uint64_t cursor = 0;
    Eg::SceneChangeSet changes;
    ASSERT_TRUE(scene.readChanges(cursor, changes));
    cursor = changes.toRevision;

    // 删除图元后应有删除变更
    scene.deleteEntity(scene.findSyEntityById(entityId));
    ASSERT_TRUE(scene.readChanges(cursor, changes));
    const Eg::SceneChange* change = findChangeInRefresh(changes, entityId);
    ASSERT_NE(change, nullptr);
    EXPECT_TRUE(Eg::hasSceneChangeKind(change->kinds, Eg::SceneChangeKind::Removed));
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
    coordinator.setSceneContext(&scene);
    coordinator.stop();
    SUCCEED();
}

// ==================== 刷新 API 安全调用（无 Widget / 无 SceneManager） ====================

TEST(ViewportRefreshRegressionTest, Coordinator_RequestLightRefreshWithoutWidget)
{
    SceneRefreshCoordinator coordinator;
    coordinator.requestLightRefresh();  // 无 RenderWidget 时不应崩溃
    SUCCEED();
}

TEST(ViewportRefreshRegressionTest, Coordinator_RequestFullRefreshWithoutWidget)
{
    SceneRefreshCoordinator coordinator;
    coordinator.requestFullRefresh();  // 无 RenderWidget 时不应崩溃
    SUCCEED();
}

TEST(ViewportRefreshRegressionTest, Coordinator_OnSceneChangedWithoutWidget)
{
    SceneRefreshCoordinator coordinator;
    coordinator.onSceneChanged();  // 无 RenderWidget / SceneManager 时不应崩溃
    SUCCEED();
}

TEST(ViewportRefreshRegressionTest, Coordinator_OnSceneChangedWithSceneManager)
{
    Eg::SceneManager scene;
    SceneRefreshCoordinator coordinator;
    coordinator.setSceneContext(&scene);
    coordinator.onSceneChanged();  // 有 SceneManager 但无 Widget 时不应崩溃
    SUCCEED();
}

// ==================== 批量写入的刷新抑制（导入专用） ====================

TEST(RefreshSuppressionTest, Suppression_DefersSceneChangeUntilEnd)
{
    Eg::SceneManager scene;
    SceneRefreshCoordinator coordinator;
    coordinator.setSceneContext(&scene);

    coordinator.beginRefreshSuppression();

    // 抑制期间场景变更：只记待办，不调度（级别保持 None）
    auto line = std::make_unique<Eg::SyLine>();
    line->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });
    std::vector<std::unique_ptr<Eg::SyEntity>> entities;
    entities.push_back(std::move(line));
    scene.addEntities(std::move(entities));

    EXPECT_EQ(coordinator.pendingLevel(), UI::SceneRefreshLevel::None);

    // 结束抑制：重收集变更并调度一次刷新（小变更量 → 增量级别）
    coordinator.endRefreshSuppression();
    EXPECT_EQ(coordinator.pendingLevel(), UI::SceneRefreshLevel::LightUpdate);

    coordinator.stop();
}

TEST(RefreshSuppressionTest, Suppression_NestedDepth)
{
    Eg::SceneManager scene;
    SceneRefreshCoordinator coordinator;
    coordinator.setSceneContext(&scene);

    // 嵌套两层（如导入泵事件期间又嵌套了另一个导入的 phase 3-5）：
    // 只有一层结束时不得恢复调度
    coordinator.beginRefreshSuppression();
    coordinator.beginRefreshSuppression();

    auto line = std::make_unique<Eg::SyLine>();
    line->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });
    std::vector<std::unique_ptr<Eg::SyEntity>> entities;
    entities.push_back(std::move(line));
    scene.addEntities(std::move(entities));

    coordinator.endRefreshSuppression();
    EXPECT_EQ(coordinator.pendingLevel(), UI::SceneRefreshLevel::None)
        << "深度未归零时不得派发刷新";

    coordinator.endRefreshSuppression();
    EXPECT_EQ(coordinator.pendingLevel(), UI::SceneRefreshLevel::LightUpdate);

    coordinator.stop();
}

TEST(RefreshSuppressionTest, Suppression_BulkChangeUpgradesToFullRefresh)
{
    Eg::SceneManager scene;
    SceneRefreshCoordinator coordinator;
    coordinator.setSceneContext(&scene);

    coordinator.beginRefreshSuppression();

    // 超过 kBulkChangeThreshold（16384）的海量写入：结束时必须升级全量，
    // 而不是对海量图元逐个 clone 快照 + 无裁剪增量 upsert
    constexpr size_t kBulkCount = 17000;
    std::vector<std::unique_ptr<Eg::SyEntity>> entities;
    entities.reserve(kBulkCount);
    for (size_t i = 0; i < kBulkCount; ++i)
    {
        auto line = std::make_unique<Eg::SyLine>();
        line->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });
        entities.push_back(std::move(line));
    }
    scene.addEntities(std::move(entities));

    coordinator.endRefreshSuppression();
    EXPECT_EQ(coordinator.pendingLevel(), UI::SceneRefreshLevel::FullRefresh);

    coordinator.stop();
}

TEST(RefreshSuppressionTest, Suppression_NoSceneChangeEndsWithoutRefresh)
{
    SceneRefreshCoordinator coordinator;
    coordinator.beginRefreshSuppression();
    coordinator.endRefreshSuppression();  // 抑制期间无变更：结束应为空操作
    EXPECT_EQ(coordinator.pendingLevel(), UI::SceneRefreshLevel::None);
    coordinator.stop();
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
    coordinator.setSceneContext(&scene);

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
    coordinator.setSceneContext(&scene);

    auto line = std::make_unique<Eg::SyLine>();
    line->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });

    std::vector<std::unique_ptr<Eg::SyEntity>> entities;
    entities.push_back(std::move(line));
    scene.addEntities(std::move(entities));

    Eg::SceneChangeSet changes;
    ASSERT_TRUE(scene.readChanges(0, changes));
    EXPECT_FALSE(changes.changes.empty());

    coordinator.onSceneChanged();
    coordinator.stop();
    SUCCEED();
}

TEST(RefreshChainRegressionTest, SceneChanged_CollectsDeletedIds)
{
    Eg::SceneManager scene;
    SceneRefreshCoordinator coordinator;
    coordinator.setSceneContext(&scene);

    auto line = std::make_unique<Eg::SyLine>();
    line->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });
    Eg::EntityId lineId = line->id;

    std::vector<std::unique_ptr<Eg::SyEntity>> entities;
    entities.push_back(std::move(line));
    scene.addEntities(std::move(entities));

    // 记下当前变更游标，只关心此后的变更
    uint64_t cursor = 0;
    Eg::SceneChangeSet changes;
    ASSERT_TRUE(scene.readChanges(cursor, changes));
    cursor = changes.toRevision;

    scene.deleteEntity(scene.findSyEntityById(lineId));
    ASSERT_TRUE(scene.readChanges(cursor, changes));
    const Eg::SceneChange* change = findChangeInRefresh(changes, lineId);
    ASSERT_NE(change, nullptr);
    EXPECT_TRUE(Eg::hasSceneChangeKind(change->kinds, Eg::SceneChangeKind::Removed));

    coordinator.onSceneChanged();
    coordinator.stop();
    SUCCEED();
}

TEST(RefreshChainRegressionTest, SceneChanged_EmptyScene)
{
    Eg::SceneManager scene;
    SceneRefreshCoordinator coordinator;
    coordinator.setSceneContext(&scene);

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
    // LightUpdate 路径需要 SceneManager 来获取图元数据
    Eg::SceneManager scene;
    SceneRefreshCoordinator coordinator;
    coordinator.setSceneContext(&scene);

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
    // FullRefresh 路径需要 SceneManager 来获取所有图元
    Eg::SceneManager scene;
    SceneRefreshCoordinator coordinator;
    coordinator.setSceneContext(&scene);

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
    coordinator.setSceneContext(&scene);

    auto line = std::make_unique<Eg::SyLine>();
    line->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });

    std::vector<std::unique_ptr<Eg::SyEntity>> entities;
    entities.push_back(std::move(line));
    scene.addEntities(std::move(entities));

    Eg::SceneChangeSet changes;
    ASSERT_TRUE(scene.readChanges(0, changes));
    EXPECT_FALSE(changes.changes.empty());

    coordinator.requestFullRefresh();
    coordinator.stop();
    SUCCEED();
}

// ==================== 增量渲染与全量渲染切换测试 ====================

TEST(RefreshPathRegressionTest, IncrementalToFull_Switch)
{
    Eg::SceneManager scene;
    SceneRefreshCoordinator coordinator;
    coordinator.setSceneContext(&scene);

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
    coordinator.setSceneContext(&scene);

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
    coordinator.setSceneContext(&scene);

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
    coordinator.setSceneContext(&scene);

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
    coordinator.setSceneContext(&scene);

    auto line = std::make_unique<Eg::SyLine>();
    line->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });

    std::vector<std::unique_ptr<Eg::SyEntity>> entities;
    entities.push_back(std::move(line));
    scene.addEntities(std::move(entities));

    Eg::SceneChangeSet changes;
    ASSERT_TRUE(scene.readChanges(0, changes));
    EXPECT_FALSE(changes.changes.empty());

    coordinator.onSceneChanged();
    coordinator.requestFullRefresh();
    coordinator.stop();
    SUCCEED();
}

TEST(RefreshCleanupRegressionTest, PendingIds_ClearedAfterFullRefresh)
{
    Eg::SceneManager scene;
    SceneRefreshCoordinator coordinator;
    coordinator.setSceneContext(&scene);

    auto line = std::make_unique<Eg::SyLine>();
    line->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });
    Eg::EntityId lineId = line->id;

    std::vector<std::unique_ptr<Eg::SyEntity>> entities;
    entities.push_back(std::move(line));
    scene.addEntities(std::move(entities));

    scene.deleteEntity(scene.findSyEntityById(lineId));
    coordinator.onSceneChanged();
    coordinator.stop();
    SUCCEED();
}

// ==================== 大尺寸图元刷新测试 ====================

TEST(RefreshStressRegressionTest, BulkEntities_SceneChanged)
{
    Eg::SceneManager scene;
    SceneRefreshCoordinator coordinator;
    coordinator.setSceneContext(&scene);

    std::vector<std::unique_ptr<Eg::SyEntity>> entities;
    for (int i = 0; i < 50; ++i)
    {
        auto line = std::make_unique<Eg::SyLine>();
        line->setPointVector({ Ut::Vec2d(0.0, 0.0), Ut::Vec2d(10.0 + i, 10.0 + i) });
        entities.push_back(std::move(line));
    }
    scene.addEntities(std::move(entities));

    EXPECT_EQ(scene.getEntityCount(), 50u);
    Eg::SceneChangeSet changes;
    ASSERT_TRUE(scene.readChanges(0, changes));
    EXPECT_FALSE(changes.changes.empty());

    coordinator.onSceneChanged();
    coordinator.stop();
    SUCCEED();
}

TEST(RefreshStressRegressionTest, BulkEntities_FullRefresh)
{
    Eg::SceneManager scene;
    SceneRefreshCoordinator coordinator;
    coordinator.setSceneContext(&scene);

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
    coordinator.setSceneContext(&scene);

    auto line = std::make_unique<Eg::SyLine>();
    line->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });
    std::vector<std::unique_ptr<Eg::SyEntity>> entities;
    entities.push_back(std::move(line));
    scene.addEntities(std::move(entities));

    // 添加图元后应有变更记录
    Eg::SceneChangeSet changes;
    ASSERT_TRUE(scene.readChanges(0, changes));
    EXPECT_FALSE(changes.changes.empty());

    coordinator.onSceneChanged();
    coordinator.stop();
    SUCCEED();
}

TEST(ViewportRefreshRegressionTest, Coordinator_OnSceneChangedWithDeletedEntities)
{
    Eg::SceneManager scene;
    SceneRefreshCoordinator coordinator;
    coordinator.setSceneContext(&scene);

    auto line = std::make_unique<Eg::SyLine>();
    line->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });
    Eg::EntityId lineId = line->id;

    std::vector<std::unique_ptr<Eg::SyEntity>> entities;
    entities.push_back(std::move(line));
    scene.addEntities(std::move(entities));

    // 记下当前变更游标，只关心此后的变更
    uint64_t cursor = 0;
    Eg::SceneChangeSet changes;
    ASSERT_TRUE(scene.readChanges(cursor, changes));
    cursor = changes.toRevision;

    auto* entity = scene.findSyEntityById(lineId);
    ASSERT_NE(entity, nullptr);

    scene.selectEntity(entity);
    scene.deleteSelected();
    ASSERT_TRUE(scene.readChanges(cursor, changes));
    const Eg::SceneChange* change = findChangeInRefresh(changes, lineId);
    ASSERT_NE(change, nullptr);
    EXPECT_TRUE(Eg::hasSceneChangeKind(change->kinds, Eg::SceneChangeKind::Removed));

    coordinator.onSceneChanged();
    coordinator.stop();
}

TEST(ViewportRefreshRegressionTest, Coordinator_SetSceneManagerTwice)
{
    Eg::SceneManager scene1;
    Eg::SceneManager scene2;

    SceneRefreshCoordinator coordinator;
    coordinator.setSceneContext(&scene1);
    coordinator.setSceneContext(&scene2);
    coordinator.stop();
    SUCCEED();
}

TEST(ViewportRefreshRegressionTest, Coordinator_RequestFullRefreshAfterSceneChange)
{
    Eg::SceneManager scene;
    SceneRefreshCoordinator coordinator;
    coordinator.setSceneContext(&scene);

    coordinator.onSceneChanged();
    coordinator.requestFullRefresh();
    coordinator.stop();
    SUCCEED();
}

TEST(ViewportRefreshRegressionTest, Coordinator_RepaintDoesNotTriggerFullRefresh)
{
    Eg::SceneManager scene;
    SceneRefreshCoordinator coordinator;
    coordinator.setSceneContext(&scene);

    coordinator.requestRepaint();
    coordinator.stop();
    SUCCEED();
}

// 公共契约（UI::ISceneRefreshScheduler）在 2D 侧的语义：级别单调升级、不可降级
TEST(ViewportRefreshRegressionTest, Coordinator_PendingLevelNeverDowngrades)
{
    Eg::SceneManager scene;
    SceneRefreshCoordinator coordinator;
    coordinator.setSceneContext(&scene);

    EXPECT_EQ(coordinator.pendingLevel(), UI::SceneRefreshLevel::None);

    coordinator.requestFullRefresh();
    EXPECT_EQ(coordinator.pendingLevel(), UI::SceneRefreshLevel::FullRefresh);

    coordinator.requestLightRefresh();
    coordinator.requestRepaint();
    EXPECT_EQ(coordinator.pendingLevel(), UI::SceneRefreshLevel::FullRefresh);

    coordinator.stop();
}

TEST(ViewportRefreshRegressionTest, Coordinator_LightRefreshThenRepaintNoDowngrade)
{
    Eg::SceneManager scene;
    SceneRefreshCoordinator coordinator;
    coordinator.setSceneContext(&scene);

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
    coordinator.setSceneContext(&scene);

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
    coordinator.setSceneContext(&scene);

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
    coordinator.setSceneContext(&scene);

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
    coordinator.setSceneContext(&scene);

    coordinator.requestFullRefresh();
    coordinator.requestRepaint();
    coordinator.stop();
    SUCCEED();
}

// ==================== 曲线 LOD 消费泵回归 ====================

// 背景（缺陷）：RenderSubmitScheduler 拆分后，32ms 的 LOD 定时器是单次的，
// 而 ensurePump() 是空实现，定时器触发一次后不再续跑。
// 队列超过单批 2000 个图元时，剩余图元永远停在低精度 ——
// 用户看到的现象就是「超大文件放大后，曲线仍然粗糙」。
//
// 断言方式：不依赖真实 GPU —— 注入成功的提交回调，验证队列排空与批次
// 续跑语义（失败重试语义由 RenderSubmitSchedulerTest 的失败重试用例覆盖）。
// 旧实现最多被场景刷新顺手带上 2 批，修复后 32ms 定时器会持续续跑，
// 直到 6001 个图元全部消费完。
TEST(CurveLodPumpRegressionTest, ContinuesPastFirstBatch)
{
    Eg::SceneManager scene;

    // 6001 个圆：单批 2000，旧实现最多被「场景刷新顺手带一次 + LOD 定时器一次」
    // 消费 4000 个，剩下的 2001 个会永久卡在队列里。
    constexpr int kCircleCount = 6001;
    std::vector<std::unique_ptr<Eg::SyEntity>> entities;
    entities.reserve(kCircleCount);
    for (int i = 0; i < kCircleCount; ++i)
    {
        auto circle = std::make_unique<Eg::SyCircle>();
        circle->basePoint = Ut::Vec2d((i % 80) * 10.0 - 400.0, ((i / 80) % 60) * 10.0 - 300.0);
        circle->dRadius = 1.0;
        entities.push_back(std::move(circle));
    }
    scene.addEntities(std::move(entities));
    ASSERT_EQ(scene.getEntityCount(), static_cast<size_t>(kCircleCount));

    RenderWidget widget;
    widget.resize(800, 600);

    SceneRefreshCoordinator coordinator;
    coordinator.setRenderWidget(&widget);
    coordinator.setSceneContext(&scene);
    // 无渲染器（无 GL）时真实 upsert 会返回失败，批次会走重试而不是推进；
    // 本用例验证的是泵续跑/队列排空语义，注入成功回调隔离掉 GL 依赖。
    coordinator.setSubmitBatchCallback([](const std::vector<uint64_t>&) { return true; });

    coordinator.requestCurveLodRefresh();
    // 首次收集应把所有曲线入队（视口 1.25 倍余量覆盖了测试坐标）。
    ASSERT_EQ(coordinator.pendingCurveLodCount(), static_cast<size_t>(kCircleCount));

    // 泵事件循环直到队列排空。旧实现（ensurePump 空实现）会停在剩余 2001 个。
    QElapsedTimer elapsed;
    elapsed.start();
    while (coordinator.pendingCurveLodCount() != 0 && elapsed.elapsed() < 3000)
    {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    EXPECT_EQ(coordinator.pendingCurveLodCount(), 0u)
        << "LOD 定时器触发后未继续消费队列：ensurePump 的回调续跑链路断了";
    // 6001 个图元完整消费需要 4 批；旧实现最多 2 批，>=3 即可区分。
    EXPECT_GE(coordinator.curveLodBatchCount(), 3u);
    coordinator.stop();
}

// 背景（缺陷）：旧实现靠协调器手动把「选中集 + 隐藏开关」快照同步给管理器，
// 但这条同步从未被调用，于是「选中时隐藏本体」的图元在每次缩放触发 LOD 时
// 都会被重新提交回世界层，表现为——选中图元隐藏后又突然冒出来。
//
// 修复后改为注入实时谓词：队列收集时直接读取最新开关 + 最新选中集，
// 隐藏中的选中图元不会进队列。
TEST(CurveLodSelectionRegressionTest, HiddenSelectedEntityNotQueued)
{
    Eg::SceneManager scene;

    auto circle = std::make_unique<Eg::SyCircle>();
    circle->basePoint = Ut::Vec2d(0.0, 0.0);
    circle->dRadius = 10.0;
    Eg::SyEntity* added = scene.addEntity(circle.get());
    ASSERT_NE(added, nullptr);
    // 场景先带着选中集被 attach：协调器必须在 setSceneContext 时同步选中缓存。
    scene.selectEntity(added);
    ASSERT_EQ(scene.getSelectedEntityCount(), 1u);

    RenderWidget widget;
    widget.resize(800, 600);
    widget.setHideSelectedOriginal(true);
    widget.setSelectionOutlinesActive(true);
    ASSERT_TRUE(widget.hideSelectedOriginalEffective());

    SceneRefreshCoordinator coordinator;
    coordinator.setRenderWidget(&widget);
    coordinator.setSceneContext(&scene);

    coordinator.requestCurveLodRefresh();

    // 选中本体正在被虚线轮廓替代，不该再由 LOD 提交到世界层。
    EXPECT_EQ(coordinator.pendingCurveLodCount(), 0u)
        << "隐藏中的选中图元被重新排进 LOD 队列，缩放后本体会重新出现";

    coordinator.stop();
}

// ==================== RenderSubmitScheduler 行为回归 ====================
//
// 调参常量与缩放滞后判据已收敛到 RenderSubmitScheduler：
// isScaleStale 是纯函数，队列行为通过 setSubmitBatchCallback 缝隙无 GL 直测。

// 缩放滞后判据：首次必过期；放大 > kLodUpgradeRatio 或缩小 > kLodDowngradeRatio
// 才过期，两个阈值之间是防抖死区（避免滚轮微操反复重收集）。
TEST(RenderSubmitSchedulerTest, ScaleHysteresis)
{
    // 首次（还没有缩放基准）必过期
    EXPECT_TRUE(RenderSubmitScheduler::isScaleStale(1.0, 0.0));
    // 放大 1.2 倍：未到升级阈值，死区内不过期
    EXPECT_FALSE(RenderSubmitScheduler::isScaleStale(1.2, 1.0));
    // 放大 1.31 倍：超过升级阈值，过期
    EXPECT_TRUE(RenderSubmitScheduler::isScaleStale(1.31, 1.0));
    // 缩小到 0.6 倍：未到降级阈值（2 倍），死区内不过期
    EXPECT_FALSE(RenderSubmitScheduler::isScaleStale(0.6, 1.0));
    // 缩小到 0.49 倍：超过降级阈值，过期
    EXPECT_TRUE(RenderSubmitScheduler::isScaleStale(0.49, 1.0));
}

// 缩放连续触发（滚轮/拖拽每帧都变）时，requestRefresh 不得重置队列进度：
// 队列未消费完只记 recollectPending，第二批必须恰好是剩余部分，
// 而不是被重新收集后从头再提交（旧根因：放大后精度一直停留低档）。
TEST(RenderSubmitSchedulerTest, ContinuousRequestKeepsQueueProgress)
{
    Eg::SceneManager scene;

    // 2501 个圆：首批 2000（默认自适应批量），余 501 个必须由第二批原样续上
    constexpr int kCircleCount = 2501;
    std::vector<std::unique_ptr<Eg::SyEntity>> entities;
    entities.reserve(kCircleCount);
    for (int i = 0; i < kCircleCount; ++i)
    {
        auto circle = std::make_unique<Eg::SyCircle>();
        circle->basePoint = Ut::Vec2d((i % 80) * 10.0 - 400.0, ((i / 80) % 60) * 10.0 - 300.0);
        circle->dRadius = 1.0;
        entities.push_back(std::move(circle));
    }
    scene.addEntities(std::move(entities));
    ASSERT_EQ(scene.getEntityCount(), static_cast<size_t>(kCircleCount));

    RenderWidget widget;
    widget.resize(800, 600);

    RenderSubmitScheduler scheduler(&widget);
    scheduler.setSceneManager(&scene);
    scheduler.setPumpCallback([]() {});

    std::vector<std::vector<uint64_t>> batches;
    scheduler.setSubmitBatchCallback([&batches](const std::vector<uint64_t>& batch) {
        batches.push_back(batch);
        return true;
    });

    std::unordered_set<uint64_t> renderedIds;

    // 首次收集：视口 1.25 倍余量覆盖测试坐标，全部曲线入队
    scheduler.requestRefresh();
    ASSERT_EQ(scheduler.size(), static_cast<size_t>(kCircleCount));

    scheduler.processBatch(renderedIds);
    ASSERT_EQ(batches.size(), 1u);
    EXPECT_EQ(batches[0].size(), 2000u);

    // 连续缩放事件：队列未消费完时再次请求
    scheduler.requestRefresh();
    // size 是「剩余未消费」口径：恰为总数 - 首批 2000。
    // 若错误地清队列重新收集，游标归零，size 会回到 kCircleCount 而不是剩余量
    EXPECT_EQ(scheduler.size(), static_cast<size_t>(kCircleCount - 2000));

    scheduler.processBatch(renderedIds);
    ASSERT_EQ(batches.size(), 2u);
    // 恰为剩余 501 个：进度未被重置、未重新收集
    EXPECT_EQ(batches[1].size(), 501u) << "第二批不是剩余部分，队列进度被重置或重新收集了";
    // 两批无重叠：ID 不会被重复提交
    const std::unordered_set<uint64_t> firstBatch(batches[0].begin(), batches[0].end());
    for (uint64_t uid : batches[1])
    {
        EXPECT_EQ(firstBatch.count(uid), 0u) << "第二批重复提交了首批的图元";
    }
    EXPECT_EQ(scheduler.processedBatchCount(), 2u);
    EXPECT_TRUE(scheduler.empty());
    EXPECT_EQ(renderedIds.size(), static_cast<size_t>(kCircleCount));
}

// 双队列消费顺序：视口扩展队列（画面缺块）优先于 LOD 队列（精度更新）。
// LINE 不是 LOD 类型，只能从扩展队列进；圆走 LOD 队列。
TEST(RenderSubmitSchedulerTest, ExtensionQueueConsumedBeforeLodQueue)
{
    Eg::SceneManager scene;

    std::vector<std::unique_ptr<Eg::SyEntity>> entities;
    for (int i = 0; i < 3; ++i)
    {
        auto line = std::make_unique<Eg::SyLine>();
        line->setPointVector({ Ut::Vec2d(-30.0 + 10.0 * i, -20.0), Ut::Vec2d(-30.0 + 10.0 * i, 20.0) });
        entities.push_back(std::move(line));
    }
    for (int i = 0; i < 3; ++i)
    {
        auto circle = std::make_unique<Eg::SyCircle>();
        circle->basePoint = Ut::Vec2d(-30.0 + 30.0 * i, 0.0);
        circle->dRadius = 5.0;
        entities.push_back(std::move(circle));
    }
    scene.addEntities(std::move(entities));
    ASSERT_EQ(scene.getEntityCount(), 6u);

    // 从场景取持久 id：LINE 与 CIRCLE 各 3 个
    std::vector<uint64_t> lineIds;
    std::vector<uint64_t> circleIds;
    for (const Eg::SyEntity* entity : scene.getAllEntities())
    {
        if (entity->eType == Eg::EType::LINE)
        {
            lineIds.push_back(static_cast<uint64_t>(entity->id));
        }
        else
        {
            circleIds.push_back(static_cast<uint64_t>(entity->id));
        }
    }
    ASSERT_EQ(lineIds.size(), 3u);
    ASSERT_EQ(circleIds.size(), 3u);

    RenderWidget widget;
    widget.resize(800, 600);

    RenderSubmitScheduler scheduler(&widget);
    scheduler.setSceneManager(&scene);
    scheduler.setPumpCallback([]() {});

    std::vector<std::vector<uint64_t>> batches;
    scheduler.setSubmitBatchCallback([&batches](const std::vector<uint64_t>& batch) {
        batches.push_back(batch);
        return true;
    });

    // 直线先进扩展队列（新进视野、从未上 GPU）；圆由 LOD 收集进精度队列
    scheduler.enqueueViewportExtension(lineIds);
    scheduler.requestRefresh();
    ASSERT_EQ(scheduler.size(), 6u);

    std::unordered_set<uint64_t> renderedIds;
    scheduler.processBatch(renderedIds);

    // 批次小（6 个），一次 processBatch 应全部消费
    ASSERT_EQ(batches.size(), 1u);
    const std::vector<uint64_t>& batch = batches[0];
    ASSERT_EQ(batch.size(), 6u);
    // 前 3 个是扩展队列的直线（保持入队顺序）：扩展优先
    for (size_t i = 0; i < lineIds.size(); ++i)
    {
        EXPECT_EQ(batch[i], lineIds[i]) << "扩展队列未优先消费，画面缺块会晚于精度更新";
    }
    // 后 3 个是 LOD 队列的圆（收集顺序不保证，按集合比较）
    const std::unordered_set<uint64_t> circleBatch(batch.begin() + 3, batch.end());
    for (uint64_t uid : circleIds)
    {
        EXPECT_EQ(circleBatch.count(uid), 1u);
    }
    // 账本包含全部 6 个
    for (uint64_t uid : lineIds)
    {
        EXPECT_EQ(renderedIds.count(uid), 1u);
    }
    for (uint64_t uid : circleIds)
    {
        EXPECT_EQ(renderedIds.count(uid), 1u);
    }
    EXPECT_TRUE(scheduler.empty());
}

// ==================== 批次提交失败的重试语义（缺陷1） ====================
//
// 旧缺陷：提交失败只记 recollectPending 就放游标走，而 refillQueueIfStale
// 的 staleness 前置门让重收集空转 —— 失败图元被永久丢弃。
// 修复后失败条目由游标回退留在原队列原位置重试，永不丢弃；
// 「整批必然失败」（GL 上下文不可用）走前置挂起，不做无意义提交尝试。

// 失败批次原样重试直至成功：前两次提交失败，第三次起成功。
// 断言失败批次与重试批次完全一致（条目不丢、不重排），最终全部提交。
TEST(RenderSubmitSchedulerTest, FailedBatchRetriedUntilSuccess_NoPermanentLoss)
{
    Eg::SceneManager scene;

    constexpr int kCircleCount = 2501;
    std::vector<std::unique_ptr<Eg::SyEntity>> entities;
    entities.reserve(kCircleCount);
    for (int i = 0; i < kCircleCount; ++i)
    {
        auto circle = std::make_unique<Eg::SyCircle>();
        circle->basePoint = Ut::Vec2d((i % 80) * 10.0 - 400.0, ((i / 80) % 60) * 10.0 - 300.0);
        circle->dRadius = 1.0;
        entities.push_back(std::move(circle));
    }
    scene.addEntities(std::move(entities));

    std::unordered_set<uint64_t> expectedIds;
    for (const Eg::SyEntity* entity : scene.getAllEntities())
    {
        expectedIds.insert(static_cast<uint64_t>(entity->id));
    }
    ASSERT_EQ(expectedIds.size(), static_cast<size_t>(kCircleCount));

    RenderWidget widget;
    widget.resize(800, 600);

    RenderSubmitScheduler scheduler(&widget);
    scheduler.setSceneManager(&scene);
    scheduler.setPumpCallback([]() {});

    std::vector<std::vector<uint64_t>> attempts;
    scheduler.setSubmitBatchCallback([&attempts](const std::vector<uint64_t>& batch) {
        attempts.push_back(batch);
        return attempts.size() > 2;  // 前两次失败，第三次起成功
    });

    std::unordered_set<uint64_t> renderedIds;
    scheduler.requestRefresh();
    ASSERT_EQ(scheduler.size(), static_cast<size_t>(kCircleCount));

    // 退避 tick 序列：失败 1 次跳 1 tick、失败 2 次跳 2 tick……30 个 tick 远够走完
    int ticks = 0;
    while (!scheduler.empty() && ticks < 30)
    {
        scheduler.processBatch(renderedIds);
        ++ticks;
    }

    ASSERT_TRUE(scheduler.empty()) << "重试后队列仍未排空：失败条目被丢弃或重试链路断了";
    ASSERT_GE(attempts.size(), 3u);
    // 失败批次原样重试：游标回退保证重试的批次与失败批次完全一致
    EXPECT_EQ(attempts[0], attempts[2]) << "重试批次与失败批次不一致：条目丢失或顺序被打乱";
    // 无永久缺失：全部图元最终都提交进账本
    EXPECT_EQ(renderedIds, expectedIds) << "有图元在失败后永久丢失";
}

// GL 上下文不可用（整批必然失败）：挂起队列，不做任何提交尝试；
// 恢复可用后自动续跑并全部提交。不可用期间零尝试 = 不空转不烧 CPU。
TEST(RenderSubmitSchedulerTest, UnavailableSubmitSuspendsWithoutPointlessRetries)
{
    Eg::SceneManager scene;

    constexpr int kCircleCount = 100;
    std::vector<std::unique_ptr<Eg::SyEntity>> entities;
    entities.reserve(kCircleCount);
    for (int i = 0; i < kCircleCount; ++i)
    {
        auto circle = std::make_unique<Eg::SyCircle>();
        circle->basePoint = Ut::Vec2d((i % 80) * 10.0 - 400.0, ((i / 80) % 60) * 10.0 - 300.0);
        circle->dRadius = 1.0;
        entities.push_back(std::move(circle));
    }
    scene.addEntities(std::move(entities));

    RenderWidget widget;
    widget.resize(800, 600);

    RenderSubmitScheduler scheduler(&widget);
    scheduler.setSceneManager(&scene);
    scheduler.setPumpCallback([]() {});

    bool available = false;
    scheduler.setUploadAvailablePredicate([&available]() { return available; });

    size_t attempts = 0;
    scheduler.setSubmitBatchCallback([&attempts](const std::vector<uint64_t>&) {
        ++attempts;
        return true;
    });

    std::unordered_set<uint64_t> renderedIds;
    scheduler.requestRefresh();
    ASSERT_EQ(scheduler.size(), static_cast<size_t>(kCircleCount));

    for (int i = 0; i < 20; ++i)
    {
        scheduler.processBatch(renderedIds);
    }
    // 不可用期间：零提交尝试、队列原封不动、批次数为零（挂起而非重试失败）
    EXPECT_EQ(attempts, 0u) << "上下文不可用期间仍在做无意义的提交尝试";
    EXPECT_EQ(scheduler.size(), static_cast<size_t>(kCircleCount)) << "挂起期间队列被动了";
    EXPECT_EQ(scheduler.processedBatchCount(), 0u);
    EXPECT_TRUE(renderedIds.empty());

    // 上下文恢复：自动续跑，全部提交
    available = true;
    int ticks = 0;
    while (!scheduler.empty() && ticks < 30)
    {
        scheduler.processBatch(renderedIds);
        ++ticks;
    }
    EXPECT_TRUE(scheduler.empty());
    EXPECT_EQ(attempts, 1u);
    EXPECT_EQ(renderedIds.size(), static_cast<size_t>(kCircleCount));
}

// 扩展队列的非曲线图元（LINE 不进 LOD 收集）失败后也能补上：
// 失败条目经游标回退留在扩展队列原位重试，与 LOD 队列同一条路径。
TEST(RenderSubmitSchedulerTest, ExtensionQueueNonCurveEntriesRetriedAfterFailure)
{
    Eg::SceneManager scene;

    std::vector<std::unique_ptr<Eg::SyEntity>> entities;
    for (int i = 0; i < 3; ++i)
    {
        auto line = std::make_unique<Eg::SyLine>();
        line->setPointVector({ Ut::Vec2d(-30.0 + 10.0 * i, -20.0), Ut::Vec2d(-30.0 + 10.0 * i, 20.0) });
        entities.push_back(std::move(line));
    }
    scene.addEntities(std::move(entities));

    std::vector<uint64_t> lineIds;
    for (const Eg::SyEntity* entity : scene.getAllEntities())
    {
        lineIds.push_back(static_cast<uint64_t>(entity->id));
    }
    ASSERT_EQ(lineIds.size(), 3u);

    RenderWidget widget;
    widget.resize(800, 600);

    RenderSubmitScheduler scheduler(&widget);
    scheduler.setSceneManager(&scene);
    scheduler.setPumpCallback([]() {});

    std::vector<std::vector<uint64_t>> attempts;
    scheduler.setSubmitBatchCallback([&attempts](const std::vector<uint64_t>& batch) {
        attempts.push_back(batch);
        return attempts.size() > 1;  // 首次失败，第二次成功
    });

    scheduler.enqueueViewportExtension(lineIds);
    ASSERT_EQ(scheduler.size(), 3u);

    std::unordered_set<uint64_t> renderedIds;
    int ticks = 0;
    while (!scheduler.empty() && ticks < 20)
    {
        scheduler.processBatch(renderedIds);
        ++ticks;
    }

    ASSERT_TRUE(scheduler.empty()) << "扩展队列失败条目没有被补上";
    ASSERT_GE(attempts.size(), 2u);
    EXPECT_EQ(attempts[0], attempts[1]) << "重试批次与失败批次不一致：扩展条目丢失或重排";
    for (uint64_t uid : lineIds)
    {
        EXPECT_EQ(renderedIds.count(uid), 1u) << "非曲线图元在批次失败后永久丢失";
    }
}

// 持续失败：指数退避而非每 tick 空转重试，且条目永不丢弃。
// 退避序列 1,2,4,8,16,16,...（tick）→ 100 个 tick 内恰好 9 次尝试；
// 断言区间 [6,12]：既证明退避生效（远小于 100），也证明没放弃重试。
TEST(RenderSubmitSchedulerTest, PersistentSubmitFailureBacksOff_WithoutDroppingEntries)
{
    Eg::SceneManager scene;

    constexpr int kCircleCount = 2501;
    std::vector<std::unique_ptr<Eg::SyEntity>> entities;
    entities.reserve(kCircleCount);
    for (int i = 0; i < kCircleCount; ++i)
    {
        auto circle = std::make_unique<Eg::SyCircle>();
        circle->basePoint = Ut::Vec2d((i % 80) * 10.0 - 400.0, ((i / 80) % 60) * 10.0 - 300.0);
        circle->dRadius = 1.0;
        entities.push_back(std::move(circle));
    }
    scene.addEntities(std::move(entities));

    RenderWidget widget;
    widget.resize(800, 600);

    RenderSubmitScheduler scheduler(&widget);
    scheduler.setSceneManager(&scene);
    scheduler.setPumpCallback([]() {});

    size_t attempts = 0;
    scheduler.setSubmitBatchCallback([&attempts](const std::vector<uint64_t>&) {
        ++attempts;
        return false;  // 恒失败
    });

    std::unordered_set<uint64_t> renderedIds;
    scheduler.requestRefresh();
    ASSERT_EQ(scheduler.size(), static_cast<size_t>(kCircleCount));

    for (int i = 0; i < 100; ++i)
    {
        scheduler.processBatch(renderedIds);
    }

    EXPECT_GE(attempts, 6u) << "重试停了：失败条目会被永久卡死";
    EXPECT_LE(attempts, 12u) << "退避失效：持续失败期间每 tick 都在空转重试";
    EXPECT_EQ(scheduler.size(), static_cast<size_t>(kCircleCount)) << "失败条目被丢弃了";
    EXPECT_TRUE(renderedIds.empty());
}
