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
 * 依赖 QApplication 实例，缺少时跳过。
 */

#include <gtest/gtest.h>

#include "UI/Render/SceneRefreshCoordinator.h"
#include "UI/Render/RenderSubmitScheduler.h"
#include "Engine2D/Core/SceneManager.h"
#include "Engine2D/Core/SceneWriteGateway.h"
#include "Engine2D/AprEntity/AprCircle.h"
#include "Engine2D/AprEntity/AprLine.h"
#include "Engine2D/AprEntity/AprNurbs.h"
#include "Engine2D/Geometry/EntityGeometryEmitter.h"
#include "Engine/Render/TessParams.h"
#include "Log/AprPerfCounter.h"
#include "RenderWidget.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>

#include <algorithm>
#include <chrono>
#include <iostream>
#include <limits>
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

// ---- 构造与析构 ----

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

// ---- 刷新 API 安全调用（无 Widget） ----

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

// ---- 性能监控 ----

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

// ---- FrameTimer 基础行为 ----

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

// ---- SceneManager 脏标记联动 ----

TEST(ViewportRefreshRegressionTest, SceneManager_DirtyTracking)
{
    Eg::SceneManager scene;

    auto line = std::make_unique<Eg::AprLine>();
    line->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });

    std::vector<std::unique_ptr<Eg::AprEntity>> entities;
    entities.push_back(std::move(line));
    Eg::SceneWriteGateway::addEntities(scene, std::move(entities));

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
        auto line = std::make_unique<Eg::AprLine>();
        line->setPointVector({ Ut::Vec2d(0.0, 0.0), Ut::Vec2d(10.0 + i, 10.0 + i) });
        std::vector<std::unique_ptr<Eg::AprEntity>> entities;
        entities.push_back(std::move(line));
        Eg::SceneWriteGateway::addEntities(scene, std::move(entities));
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

    auto line = std::make_unique<Eg::AprLine>();
    line->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });
    Eg::EntityId entityId = line->id;

    std::vector<std::unique_ptr<Eg::AprEntity>> entities;
    entities.push_back(std::move(line));
    Eg::SceneWriteGateway::addEntities(scene, std::move(entities));

    // 记下当前变更游标，只关心此后的变更
    uint64_t cursor = 0;
    Eg::SceneChangeSet changes;
    ASSERT_TRUE(scene.readChanges(cursor, changes));
    cursor = changes.toRevision;

    // 删除图元后应有删除变更
    Eg::SceneWriteGateway::deleteEntity(scene, scene.findAprEntityById(entityId));
    ASSERT_TRUE(scene.readChanges(cursor, changes));
    const Eg::SceneChange* change = findChangeInRefresh(changes, entityId);
    ASSERT_NE(change, nullptr);
    EXPECT_TRUE(Eg::hasSceneChangeKind(change->kinds, Eg::SceneChangeKind::Removed));
}

// ---- 刷新协调器生命周期 ----

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

// ---- 刷新 API 安全调用（无 Widget / 无 SceneManager） ----

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

// ---- 批量写入的刷新抑制（导入专用） ----

TEST(RefreshSuppressionTest, Suppression_DefersSceneChangeUntilEnd)
{
    Eg::SceneManager scene;
    SceneRefreshCoordinator coordinator;
    coordinator.setSceneContext(&scene);

    coordinator.beginRefreshSuppression();

    // 抑制期间场景变更：只记待办，不调度（级别保持 None）
    auto line = std::make_unique<Eg::AprLine>();
    line->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });
    std::vector<std::unique_ptr<Eg::AprEntity>> entities;
    entities.push_back(std::move(line));
    Eg::SceneWriteGateway::addEntities(scene, std::move(entities));

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

    auto line = std::make_unique<Eg::AprLine>();
    line->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });
    std::vector<std::unique_ptr<Eg::AprEntity>> entities;
    entities.push_back(std::move(line));
    Eg::SceneWriteGateway::addEntities(scene, std::move(entities));

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
    std::vector<std::unique_ptr<Eg::AprEntity>> entities;
    entities.reserve(kBulkCount);
    for (size_t i = 0; i < kBulkCount; ++i)
    {
        auto line = std::make_unique<Eg::AprLine>();
        line->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });
        entities.push_back(std::move(line));
    }
    Eg::SceneWriteGateway::addEntities(scene, std::move(entities));

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


// ---- 刷新级别升级测试 ----

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

// ---- 场景变更→刷新调度→渲染更新链路测试 ----
// 刷新链路：sceneChanged → scheduleSceneUpdate → updateSceneRender
// 三条刷新路径：Repaint / LightUpdate / FullRefresh

TEST(RefreshChainRegressionTest, SceneChanged_SchedulesSceneUpdate)
{
    Eg::SceneManager scene;
    SceneRefreshCoordinator coordinator;
    coordinator.setSceneContext(&scene);

    auto line = std::make_unique<Eg::AprLine>();
    line->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });

    std::vector<std::unique_ptr<Eg::AprEntity>> entities;
    entities.push_back(std::move(line));
    Eg::SceneWriteGateway::addEntities(scene, std::move(entities));

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

    auto line = std::make_unique<Eg::AprLine>();
    line->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });

    std::vector<std::unique_ptr<Eg::AprEntity>> entities;
    entities.push_back(std::move(line));
    Eg::SceneWriteGateway::addEntities(scene, std::move(entities));

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

    auto line = std::make_unique<Eg::AprLine>();
    line->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });
    Eg::EntityId lineId = line->id;

    std::vector<std::unique_ptr<Eg::AprEntity>> entities;
    entities.push_back(std::move(line));
    Eg::SceneWriteGateway::addEntities(scene, std::move(entities));

    // 记下当前变更游标，只关心此后的变更
    uint64_t cursor = 0;
    Eg::SceneChangeSet changes;
    ASSERT_TRUE(scene.readChanges(cursor, changes));
    cursor = changes.toRevision;

    Eg::SceneWriteGateway::deleteEntity(scene, scene.findAprEntityById(lineId));
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

// ---- 三条刷新路径正确性测试 ----

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

    auto line = std::make_unique<Eg::AprLine>();
    line->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });

    std::vector<std::unique_ptr<Eg::AprEntity>> entities;
    entities.push_back(std::move(line));
    Eg::SceneWriteGateway::addEntities(scene, std::move(entities));

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

    auto line = std::make_unique<Eg::AprLine>();
    line->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });

    std::vector<std::unique_ptr<Eg::AprEntity>> entities;
    entities.push_back(std::move(line));
    Eg::SceneWriteGateway::addEntities(scene, std::move(entities));

    coordinator.requestFullRefresh();
    coordinator.stop();
    SUCCEED();
}

TEST(RefreshPathRegressionTest, FullRefreshPath_MarksSceneClean)
{
    Eg::SceneManager scene;
    SceneRefreshCoordinator coordinator;
    coordinator.setSceneContext(&scene);

    auto line = std::make_unique<Eg::AprLine>();
    line->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });

    std::vector<std::unique_ptr<Eg::AprEntity>> entities;
    entities.push_back(std::move(line));
    Eg::SceneWriteGateway::addEntities(scene, std::move(entities));

    Eg::SceneChangeSet changes;
    ASSERT_TRUE(scene.readChanges(0, changes));
    EXPECT_FALSE(changes.changes.empty());

    coordinator.requestFullRefresh();
    coordinator.stop();
    SUCCEED();
}

// ---- 增量渲染与全量渲染切换测试 ----

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
        auto line = std::make_unique<Eg::AprLine>();
        line->setPointVector({ Ut::Vec2d(0.0, 0.0), Ut::Vec2d(10.0 + i, 10.0 + i) });
        std::vector<std::unique_ptr<Eg::AprEntity>> entities;
        entities.push_back(std::move(line));
        Eg::SceneWriteGateway::addEntities(scene, std::move(entities));
        coordinator.onSceneChanged();
    }
    coordinator.stop();
    SUCCEED();
}

// ---- 刷新协调器标记清理测试 ----

TEST(RefreshCleanupRegressionTest, MarkClean_AfterUpdateSceneRender)
{
    Eg::SceneManager scene;
    SceneRefreshCoordinator coordinator;
    coordinator.setSceneContext(&scene);

    auto line = std::make_unique<Eg::AprLine>();
    line->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });

    std::vector<std::unique_ptr<Eg::AprEntity>> entities;
    entities.push_back(std::move(line));
    Eg::SceneWriteGateway::addEntities(scene, std::move(entities));

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

    auto line = std::make_unique<Eg::AprLine>();
    line->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });
    Eg::EntityId lineId = line->id;

    std::vector<std::unique_ptr<Eg::AprEntity>> entities;
    entities.push_back(std::move(line));
    Eg::SceneWriteGateway::addEntities(scene, std::move(entities));

    Eg::SceneWriteGateway::deleteEntity(scene, scene.findAprEntityById(lineId));
    coordinator.onSceneChanged();
    coordinator.stop();
    SUCCEED();
}

// ---- 大尺寸图元刷新测试 ----

TEST(RefreshStressRegressionTest, BulkEntities_SceneChanged)
{
    Eg::SceneManager scene;
    SceneRefreshCoordinator coordinator;
    coordinator.setSceneContext(&scene);

    std::vector<std::unique_ptr<Eg::AprEntity>> entities;
    for (int i = 0; i < 50; ++i)
    {
        auto line = std::make_unique<Eg::AprLine>();
        line->setPointVector({ Ut::Vec2d(0.0, 0.0), Ut::Vec2d(10.0 + i, 10.0 + i) });
        entities.push_back(std::move(line));
    }
    Eg::SceneWriteGateway::addEntities(scene, std::move(entities));

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

    std::vector<std::unique_ptr<Eg::AprEntity>> entities;
    for (int i = 0; i < 50; ++i)
    {
        auto line = std::make_unique<Eg::AprLine>();
        line->setPointVector({ Ut::Vec2d(0.0, 0.0), Ut::Vec2d(10.0 + i, 10.0 + i) });
        entities.push_back(std::move(line));
    }
    Eg::SceneWriteGateway::addEntities(scene, std::move(entities));

    coordinator.requestFullRefresh();
    coordinator.stop();
    SUCCEED();
}

// ---- 刷新协调器与场景联动测试 ----

TEST(ViewportRefreshRegressionTest, Coordinator_OnSceneChangedWithDirtyEntities)
{
    Eg::SceneManager scene;
    SceneRefreshCoordinator coordinator;
    coordinator.setSceneContext(&scene);

    auto line = std::make_unique<Eg::AprLine>();
    line->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });
    std::vector<std::unique_ptr<Eg::AprEntity>> entities;
    entities.push_back(std::move(line));
    Eg::SceneWriteGateway::addEntities(scene, std::move(entities));

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

    auto line = std::make_unique<Eg::AprLine>();
    line->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });
    Eg::EntityId lineId = line->id;

    std::vector<std::unique_ptr<Eg::AprEntity>> entities;
    entities.push_back(std::move(line));
    Eg::SceneWriteGateway::addEntities(scene, std::move(entities));

    // 记下当前变更游标，只关心此后的变更
    uint64_t cursor = 0;
    Eg::SceneChangeSet changes;
    ASSERT_TRUE(scene.readChanges(cursor, changes));
    cursor = changes.toRevision;

    auto* entity = scene.findAprEntityById(lineId);
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

// ---- FrameTimer 扩展测试 ----

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

// ---- 增量/全量渲染切换稳定性 ----

TEST(RefreshPathRegressionTest, IncrementalToFull_BackToIncremental)
{
    // 验证：增量 → 全量 → 增量 切换稳定
    Eg::SceneManager scene;
    SceneRefreshCoordinator coordinator;
    coordinator.setSceneContext(&scene);

    auto line = std::make_unique<Eg::AprLine>();
    line->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });
    std::vector<std::unique_ptr<Eg::AprEntity>> entities;
    entities.push_back(std::move(line));
    Eg::SceneWriteGateway::addEntities(scene, std::move(entities));

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
        auto line = std::make_unique<Eg::AprLine>();
        line->setPointVector({ Ut::Vec2d(0.0, 0.0), Ut::Vec2d(10.0 + i, 10.0 + i) });
        std::vector<std::unique_ptr<Eg::AprEntity>> entities;
        entities.push_back(std::move(line));
        Eg::SceneWriteGateway::addEntities(scene, std::move(entities));

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
        auto line = std::make_unique<Eg::AprLine>();
        line->setPointVector({ Ut::Vec2d(0.0, 0.0), Ut::Vec2d(10.0 + i, 10.0 + i) });
        std::vector<std::unique_ptr<Eg::AprEntity>> entities;
        entities.push_back(std::move(line));
        Eg::SceneWriteGateway::addEntities(scene, std::move(entities));

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

// ---- 曲线 LOD 消费泵回归 ----

// 32ms 的 LOD 定时器必须能持续续跑：ensurePump() 若为空实现，定时器触发一次
// 后不会继续触发，队列超过单批 2000 个图元时剩余图元会停在低精度，
// 现象是「超大文件放大后，曲线仍然粗糙」。
// 断言方式：不依赖真实 GPU —— 注入成功的提交回调，验证队列排空与批次
// 续跑语义（失败重试语义由 RenderSubmitSchedulerTest 的失败重试用例覆盖）；
// 6001 个图元需要 4 批才能全部消费完。
TEST(CurveLodPumpRegressionTest, ContinuesPastFirstBatch)
{
    Eg::SceneManager scene;

    // 6001 个圆：单批 2000，若无续跑最多消费「场景刷新顺手带一次 + LOD 定时器一次」
    // 共 4000 个，剩下的 2001 个会永久卡在队列里。
    constexpr int kCircleCount = 6001;
    std::vector<std::unique_ptr<Eg::AprEntity>> entities;
    entities.reserve(kCircleCount);
    for (int i = 0; i < kCircleCount; ++i)
    {
        auto circle = std::make_unique<Eg::AprCircle>();
        circle->basePoint = Ut::Vec2d((i % 80) * 10.0 - 400.0, ((i / 80) % 60) * 10.0 - 300.0);
        circle->dRadius = 1.0;
        entities.push_back(std::move(circle));
    }
    Eg::SceneWriteGateway::addEntities(scene, std::move(entities));
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

    // 泵事件循环直到队列排空。ensurePump 为空实现时会停在剩余 2001 个。
    QElapsedTimer elapsed;
    elapsed.start();
    while (coordinator.pendingCurveLodCount() != 0 && elapsed.elapsed() < 3000)
    {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    EXPECT_EQ(coordinator.pendingCurveLodCount(), 0u)
        << "LOD 定时器触发后未继续消费队列：ensurePump 的回调续跑链路断了";
    // 6001 个图元完整消费需要 4 批；中断的泵最多 2 批，>=3 即可区分。
    EXPECT_GE(coordinator.curveLodBatchCount(), 3u);
    coordinator.stop();
}

// 选中时隐藏本体的契约：快照式同步（协调器手动把「选中集 + 隐藏开关」发给管理器）
// 一旦漏调用，「选中时隐藏本体」的图元会在每次缩放触发 LOD 时被重新提交回世界层，
// 表现为选中图元隐藏后又冒出来。
// 因此队列收集必须注入实时谓词：直接读取最新开关 + 最新选中集，隐藏中的选中图元不入队。
TEST(CurveLodSelectionRegressionTest, HiddenSelectedEntityNotQueued)
{
    Eg::SceneManager scene;

    auto circle = std::make_unique<Eg::AprCircle>();
    circle->basePoint = Ut::Vec2d(0.0, 0.0);
    circle->dRadius = 10.0;
    Eg::AprEntity* added = Eg::SceneWriteGateway::addEntity(scene, circle.get());
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

// ---- RenderSubmitScheduler 行为回归 ----
// 调参常量与缩放滞后判据都在 RenderSubmitScheduler：
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
// 而不是被重新收集后从头再提交，否则放大后精度会一直停留低档。
TEST(RenderSubmitSchedulerTest, ContinuousRequestKeepsQueueProgress)
{
    Eg::SceneManager scene;

    // 2501 个圆：首批 2000（默认自适应批量），余 501 个必须由第二批原样续上
    constexpr int kCircleCount = 2501;
    std::vector<std::unique_ptr<Eg::AprEntity>> entities;
    entities.reserve(kCircleCount);
    for (int i = 0; i < kCircleCount; ++i)
    {
        auto circle = std::make_unique<Eg::AprCircle>();
        circle->basePoint = Ut::Vec2d((i % 80) * 10.0 - 400.0, ((i / 80) % 60) * 10.0 - 300.0);
        circle->dRadius = 1.0;
        entities.push_back(std::move(circle));
    }
    Eg::SceneWriteGateway::addEntities(scene, std::move(entities));
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

    std::vector<std::unique_ptr<Eg::AprEntity>> entities;
    for (int i = 0; i < 3; ++i)
    {
        auto line = std::make_unique<Eg::AprLine>();
        line->setPointVector({ Ut::Vec2d(-30.0 + 10.0 * i, -20.0), Ut::Vec2d(-30.0 + 10.0 * i, 20.0) });
        entities.push_back(std::move(line));
    }
    for (int i = 0; i < 3; ++i)
    {
        auto circle = std::make_unique<Eg::AprCircle>();
        circle->basePoint = Ut::Vec2d(-30.0 + 30.0 * i, 0.0);
        circle->dRadius = 5.0;
        entities.push_back(std::move(circle));
    }
    Eg::SceneWriteGateway::addEntities(scene, std::move(entities));
    ASSERT_EQ(scene.getEntityCount(), 6u);

    // 从场景取持久 id：LINE 与 CIRCLE 各 3 个
    std::vector<uint64_t> lineIds;
    std::vector<uint64_t> circleIds;
    for (const Eg::AprEntity* entity : scene.getAllEntities())
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

// ---- 批次提交失败的重试语义 ----
// 提交失败若只记 recollectPending 就放游标走，refillQueueIfStale 的 staleness
// 前置门会让重收集空转，失败图元被永久丢弃。
// 失败条目必须由游标回退留在原队列原位置重试，永不丢弃；
// 「整批必然失败」（GL 上下文不可用）走前置挂起，不做无意义提交尝试。

// 失败批次原样重试直至成功：前两次提交失败，第三次起成功。
// 断言失败批次与重试批次完全一致（条目不丢、不重排），最终全部提交。
TEST(RenderSubmitSchedulerTest, FailedBatchRetriedUntilSuccess_NoPermanentLoss)
{
    Eg::SceneManager scene;

    constexpr int kCircleCount = 2501;
    std::vector<std::unique_ptr<Eg::AprEntity>> entities;
    entities.reserve(kCircleCount);
    for (int i = 0; i < kCircleCount; ++i)
    {
        auto circle = std::make_unique<Eg::AprCircle>();
        circle->basePoint = Ut::Vec2d((i % 80) * 10.0 - 400.0, ((i / 80) % 60) * 10.0 - 300.0);
        circle->dRadius = 1.0;
        entities.push_back(std::move(circle));
    }
    Eg::SceneWriteGateway::addEntities(scene, std::move(entities));

    std::unordered_set<uint64_t> expectedIds;
    for (const Eg::AprEntity* entity : scene.getAllEntities())
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
    std::vector<std::unique_ptr<Eg::AprEntity>> entities;
    entities.reserve(kCircleCount);
    for (int i = 0; i < kCircleCount; ++i)
    {
        auto circle = std::make_unique<Eg::AprCircle>();
        circle->basePoint = Ut::Vec2d((i % 80) * 10.0 - 400.0, ((i / 80) % 60) * 10.0 - 300.0);
        circle->dRadius = 1.0;
        entities.push_back(std::move(circle));
    }
    Eg::SceneWriteGateway::addEntities(scene, std::move(entities));

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

    std::vector<std::unique_ptr<Eg::AprEntity>> entities;
    for (int i = 0; i < 3; ++i)
    {
        auto line = std::make_unique<Eg::AprLine>();
        line->setPointVector({ Ut::Vec2d(-30.0 + 10.0 * i, -20.0), Ut::Vec2d(-30.0 + 10.0 * i, 20.0) });
        entities.push_back(std::move(line));
    }
    Eg::SceneWriteGateway::addEntities(scene, std::move(entities));

    std::vector<uint64_t> lineIds;
    for (const Eg::AprEntity* entity : scene.getAllEntities())
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
    std::vector<std::unique_ptr<Eg::AprEntity>> entities;
    entities.reserve(kCircleCount);
    for (int i = 0; i < kCircleCount; ++i)
    {
        auto circle = std::make_unique<Eg::AprCircle>();
        circle->basePoint = Ut::Vec2d((i % 80) * 10.0 - 400.0, ((i / 80) % 60) * 10.0 - 300.0);
        circle->dRadius = 1.0;
        entities.push_back(std::move(circle));
    }
    Eg::SceneWriteGateway::addEntities(scene, std::move(entities));

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

// ---- 可选基准（DISABLED_：默认跳过，不进 CI） ----
// 「百万级图元 zoom out」重建成本基准 —— 对应 LOD 文档 §5 的验收口径
// （百万场景 zoom out 单帧 <10ms、误差 ≤1px）。文档把这两个数字标注为
// 「待基准验证」；本用例提供可重复的低成本测量入口，但不覆盖 GPU 全链路。
// 手动运行（默认测试集会跳过，不拖慢 CI）：
//   ViewportRefreshTests --gtest_also_run_disabled_tests \
//       --gtest_filter=RenderSubmitLodBenchmark.DISABLED_MillionEntityZoomOutRebuild
// 测量口径（数字只作相对参考，勿当验收结论）：
// 无 GL 环境：提交回调里用 Eg::emitEntityGeometry + 计数 sink 模拟「按当前
// 缩放重建几何」的 CPU 侧工作量（发射/离散化正是 LOD 调参瞄准的主成本）；
// 不含 GPU 上传（allocBlock/writeBlock）与圆/弧顶点展开（发生在装配层，
// 需要渲染上下文），这两块不在本基准口径内；
// 耗时不设阈值断言（阈值随机器漂移只会把 CI 变成抖动源），以 stdout 报告
// 收集耗时与逐批耗时；断言只锁正确性不变量（收集数/账本数/顶点上界/误差预算）。
namespace
{
    /// 计数/捕获 sink：统计发射原语次数与折线点数上界，可选捕获最后一条折线
    struct LodBenchmarkSink : Eg::ISceneGeometrySink
    {
        size_t polylineCount = 0;
        size_t maxPolylinePoints = 0;
        bool capture = false;
        std::vector<Ut::Vec2d> capturedPolyline;

        void emitPolyline(const Ut::Vec2d* points, size_t count, bool /*bClosed*/, const Ut::Color& /*color*/) override
        {
            ++polylineCount;
            maxPolylinePoints = std::max(maxPolylinePoints, count);
            if (capture)
            {
                capturedPolyline.assign(points, points + count);
            }
        }
        void emitCircle(const Ut::Vec2d&, double, const Ut::Color&) override {}
        void emitArc(const Ut::Vec2d&, double, double, double, const Ut::Color&) override {}
        void emitEllipse(const Ut::Vec2d&, double, double, double, double, double, bool, const Ut::Color&) override {}
        void emitText(const Ut::Vec2d&, const char*, const Ut::Color&) override {}
        void emitImagePlaceholder(
            const Ut::Vec2d&, const Ut::Vec2d&, const Ut::Vec2d&, const Ut::Vec2d&, const Ut::Color&) override
        {
        }
        void emitTriangleSoup(const Ut::Vec3f*, size_t, const Ut::Vec3f*, size_t, const Ut::Color&) override {}
    };

    double pointSegDist(const Ut::Vec2d& p, const Ut::Vec2d& a, const Ut::Vec2d& b)
    {
        const Ut::Vec2d ab = b - a;
        const double l2 = ab.lengthSquared();
        if (l2 <= 0.0)
        {
            return (p - a).length();
        }
        double t = (p - a).dot(ab) / l2;
        t = std::max(0.0, std::min(1.0, t));
        return (p - (a + ab * t)).length();
    }

    /// 参考采样点到折线的最大弦高（世界单位）—— 与 SplineAlgorithmsTests 同款测量
    double maxChordHeight(const std::vector<Ut::Vec2d>& ref, const std::vector<Ut::Vec2d>& poly)
    {
        double worst = 0.0;
        for (const Ut::Vec2d& p : ref)
        {
            double best = std::numeric_limits<double>::max();
            for (size_t i = 0; i + 1 < poly.size(); ++i)
            {
                best = std::min(best, pointSegDist(p, poly[i], poly[i + 1]));
            }
            worst = std::max(worst, best);
        }
        return worst;
    }
}  // namespace

TEST(RenderSubmitLodBenchmark, DISABLED_MillionEntityZoomOutRebuild)
{
    using Clock = std::chrono::steady_clock;
    const auto elapsedMs = [](Clock::time_point t0, Clock::time_point t1) {
        return std::chrono::duration<double, std::milli>(t1 - t0).count();
    };

    // 100 万条 4 控制点样条：1000×1000 网格铺进默认可视盒（±400×±300）。
    constexpr int kGridW = 1000;
    constexpr int kGridH = 1000;
    constexpr size_t kEntityCount = static_cast<size_t>(kGridW) * static_cast<size_t>(kGridH);
    // zoom out 档：整个场景缩到约 1/10 屏 —— 曲线离散走「激进简化」档。
    constexpr double kZoomOutScale = 0.1;
    // 误差预算取 2px 宽松界：严格 1px 是 §5 的待验证验收线，
    // 没有真机 GPU 全链路测量前不在用例里锁死。
    constexpr double kErrorBudgetPx = 2.0;

    Eg::SceneManager scene;

    const Clock::time_point tSetup0 = Clock::now();
    {
        std::vector<std::unique_ptr<Eg::AprEntity>> entities;
        entities.reserve(kEntityCount);
        for (int gy = 0; gy < kGridH; ++gy)
        {
            for (int gx = 0; gx < kGridW; ++gx)
            {
                auto spline = std::make_unique<Eg::AprNurbs>();
                spline->nDegree = 3;
                const double x0 = gx * 0.8 - 400.0;
                const double y0 = gy * 0.6 - 300.0;
                spline->setControlPointVector({ Ut::Vec2d(x0, y0),
                    Ut::Vec2d(x0 + 0.2, y0 + 0.3),
                    Ut::Vec2d(x0 + 0.5, y0 - 0.2),
                    Ut::Vec2d(x0 + 0.8, y0 + 0.1) });
                spline->updateKnots();
                // 单位权重必须显式设置：权重向量为空时 AprNurbs::value 落入控制点
                // 线性插值兜底，求值/离散全部失真（SplineAlgorithmsTests 同款教训）。
                spline->setControlPointWeights(std::vector<double>(4, 1.0));
                entities.push_back(std::move(spline));
            }
        }
        Eg::SceneWriteGateway::addEntities(scene, std::move(entities));
    }
    const double setupMs = elapsedMs(tSetup0, Clock::now());
    ASSERT_EQ(scene.getEntityCount(), kEntityCount);

    RenderWidget widget;
    widget.resize(800, 600);

    RenderSubmitScheduler scheduler(&widget);
    scheduler.setSceneManager(&scene);
    scheduler.setPumpCallback([]() {});

    LodBenchmarkSink sink;
    scheduler.setSubmitBatchCallback([&scene, &sink](const std::vector<uint64_t>& batch) {
        for (uint64_t uid : batch)
        {
            const Eg::AprEntity* entity = scene.findEntityById(static_cast<Eg::EntityId>(uid));
            if (entity)
            {
                Eg::emitEntityGeometry(*entity, sink, kZoomOutScale);
            }
        }
        return true;
    });

    // 收集耗时：RTree 范围查询 + 入队（视口 1.25 倍余量覆盖网格坐标）
    const Clock::time_point tCollect0 = Clock::now();
    scheduler.requestRefresh();
    const double collectMs = elapsedMs(tCollect0, Clock::now());
    ASSERT_EQ(scheduler.size(), kEntityCount) << "收集口径异常：不是全部曲线都入队了";

    // 逐批重建耗时 —— 「zoom out 后逐帧重建」的无 GPU 侧代理：
    // 每个 processBatch tick 消费一批（自适应批 128~8192），
    // 回调里按 kZoomOutScale 重新离散并发射。
    std::unordered_set<uint64_t> renderedIds;
    double submitTotalMs = 0.0;
    double submitMaxMs = 0.0;
    size_t batchCount = 0;
    while (!scheduler.empty() && batchCount < 10000)
    {
        const Clock::time_point tBatch0 = Clock::now();
        scheduler.processBatch(renderedIds);
        const double batchMs = elapsedMs(tBatch0, Clock::now());
        submitTotalMs += batchMs;
        submitMaxMs = std::max(submitMaxMs, batchMs);
        ++batchCount;
    }

    std::cout << "[LOD基准] 构造+入库 " << setupMs << " ms | 收集 " << collectMs << " ms | 批数 " << batchCount
              << " | 重建提交合计 " << submitTotalMs << " ms（avg "
              << (batchCount > 0 ? submitTotalMs / batchCount : 0.0) << " ms/批，max " << submitMaxMs << " ms/批）"
              << std::endl;

    // ---- 正确性不变量（与机器速度无关） ----
    EXPECT_TRUE(scheduler.empty()) << "队列未排空：有图元被丢弃或卡死";
    EXPECT_EQ(renderedIds.size(), kEntityCount) << "有图元未提交进账本";
    // 每个样条恰发射一条折线
    EXPECT_EQ(sink.polylineCount, kEntityCount);
    // 顶点上界（钳制）在整个百万批次上全程有效
    EXPECT_LE(sink.maxPolylinePoints, static_cast<size_t>(Eg::tess::kLodMaxCurvePoints));

    // ---- 误差抽检：代表性大波浪样条（与 SplineAlgorithmsTests 同形） ----
    // 误差 = 参考采样点到发射折线的最大弦高 × scale = 屏幕像素。
    // 参考曲线必须与生产同一求值器 AprNurbs::value 密采样。
    Eg::AprNurbs wavy;
    wavy.setControlPointVector({ Ut::Vec2d(0, 0),
        Ut::Vec2d(500, 2000),
        Ut::Vec2d(1500, -1500),
        Ut::Vec2d(2500, 2000),
        Ut::Vec2d(3500, 0),
        Ut::Vec2d(4500, -2000) });
    wavy.nDegree = 3;
    wavy.updateKnots();
    wavy.setControlPointWeights(std::vector<double>(6, 1.0));

    LodBenchmarkSink captureSink;
    captureSink.capture = true;
    ASSERT_TRUE(Eg::emitEntityGeometry(wavy, captureSink, kZoomOutScale));
    ASSERT_GE(captureSink.capturedPolyline.size(), 2u);

    std::vector<Ut::Vec2d> reference;
    constexpr int kRefSamples = 8192;
    reference.reserve(kRefSamples);
    for (int i = 0; i < kRefSamples; ++i)
    {
        reference.push_back(wavy.value(static_cast<double>(i) / (kRefSamples - 1)));
    }
    const double errorPx = maxChordHeight(reference, captureSink.capturedPolyline) * kZoomOutScale;
    std::cout << "[LOD基准] zoom out 档离散误差 " << errorPx << " px（预算 " << kErrorBudgetPx << " px）" << std::endl;
    EXPECT_LE(errorPx, kErrorBudgetPx) << "zoom out 档离散误差超过预算";

    // ---- 超放大档钳制抽查：点数上界在发射边界同样有效 ----
    LodBenchmarkSink zoomInSink;
    ASSERT_TRUE(Eg::emitEntityGeometry(wavy, zoomInSink, 1000.0));
    EXPECT_LE(zoomInSink.maxPolylinePoints, static_cast<size_t>(Eg::tess::kLodMaxCurvePoints));
    EXPECT_GE(zoomInSink.maxPolylinePoints, 16u) << "钳制是拉平而非截断，密度不该塌掉";
}
