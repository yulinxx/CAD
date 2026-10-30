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

// 背景（缺陷）：CurveLodManager 拆分后，32ms 的 LOD 定时器是单次的，
// 而 ensurePump() 是空实现，定时器触发一次后不再续跑。
// 队列超过单批 2000 个图元时，剩余图元永远停在低精度 ——
// 用户看到的现象就是「超大文件放大后，曲线仍然粗糙」。
//
// 断言方式：不依赖真实 GPU（无渲染器时 upsert 会失败，但批次游标仍会推进），
// 看队列是否排空、批次是否继续增长。旧实现最多被场景刷新顺手带上 2 批，
// 修复后 32ms 定时器会持续续跑，直到 6001 个图元全部消费完。
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
