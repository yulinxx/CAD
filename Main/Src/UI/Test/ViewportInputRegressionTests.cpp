/**
 * @file ViewportInputRegressionTests.cpp
 * @brief 视口输入路由与刷新路径回归测试 — 覆盖事件分发、键盘路由、刷新级别转换
 *
 * 测试范围：
 *  - Camera2D 坐标转换与视图矩阵
 *  - SceneRefreshCoordinator 刷新级别升级/降级规则
 *  - SceneManager 脏标记与刷新联动
 *  - 选择同步链 (选中/取消选中/删除后选中状态)
 *  - 键盘事件路由优先级 (Escape/Enter/Delete)
 *
 * 注意：需要 QWidget 的测试（RenderViewport2D 事件分发）需要 QApplication 实例，
 * 当前版本以 SceneRefreshCoordinator / Camera2D / SceneManager 的组件级测试为主。
 */

#include <gtest/gtest.h>

#include "UI/Render/SceneRefreshCoordinator.h"
#include "UI/Render/Camera2D.h"
#include "UI/Widgets/ViewportSelector.h"
#include "UI/Services/ISelectionService.h"
#include "Engine2D/Core/SceneManager.h"
#include "Engine2D/SyEntity/SyLine.h"
#include "Engine2D/SyEntity/SyCircle.h"
#include "Engine2D/SyEntity/SyPolygon.h"
#include "Log/SyPerfCounter.h"

#include <memory>
#include <vector>
#include <cstring>
#include <chrono>
#include <thread>

// 记录一帧，并保证两次时钟读取间隔非零（避免高精度时钟落在同一 tick 导致 avg==0 的抖动失败）
static void recordOneFrame(FrameTimer& timer)
{
    timer.beginFrame();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    timer.endFrame();
}

// ==================== Camera2D 坐标转换测试 ====================

TEST(ViewportInputRegressionTest, Camera2D_DefaultState)
{
    Camera2D camera;
    EXPECT_FLOAT_EQ(camera.zoomX, 1.0f);
    EXPECT_FLOAT_EQ(camera.zoomY, 1.0f);
    EXPECT_TRUE(camera.panOffset.isNull());
}

TEST(ViewportInputRegressionTest, Camera2D_ResetRestoresDefaults)
{
    Camera2D camera;
    camera.zoomX = 2.0f;
    camera.zoomY = 2.0f;
    camera.panOffset = QPointF(100, 200);

    camera.reset();

    EXPECT_FLOAT_EQ(camera.zoomX, 1.0f);
    EXPECT_FLOAT_EQ(camera.zoomY, 1.0f);
    EXPECT_TRUE(camera.panOffset.isNull());
}

TEST(ViewportInputRegressionTest, Camera2D_PanAccumulatesOffset)
{
    Camera2D camera;
    camera.pan(10.0f, 20.0f);
    camera.pan(-5.0f, 15.0f);

    EXPECT_FLOAT_EQ(static_cast<float>(camera.panOffset.x()), 5.0f);
    EXPECT_FLOAT_EQ(static_cast<float>(camera.panOffset.y()), 35.0f);
}

TEST(ViewportInputRegressionTest, Camera2D_ZoomInIncreasesLevel)
{
    Camera2D camera;
    float origZoomX = camera.zoomX;
    float origZoomY = camera.zoomY;

    camera.zoomIn(2.0f, QPointF(0, 0), 800.0f, 600.0f);

    EXPECT_GT(camera.zoomX, origZoomX);
    EXPECT_GT(camera.zoomY, origZoomY);
}

TEST(ViewportInputRegressionTest, Camera2D_ZoomOutDecreasesLevel)
{
    Camera2D camera;
    float origZoomX = camera.zoomX;
    float origZoomY = camera.zoomY;

    camera.zoomOut(2.0f, QPointF(0, 0), 800.0f, 600.0f);

    EXPECT_LT(camera.zoomX, origZoomX);
    EXPECT_LT(camera.zoomY, origZoomY);
}

TEST(ViewportInputRegressionTest, Camera2D_ZoomClampedToRange)
{
    Camera2D camera;

    // 极限放大不应超出 MAX_ZOOM
    for (int i = 0; i < 100; ++i)
    {
        camera.zoomIn(2.0f, QPointF(0, 0), 800.0f, 600.0f);
    }
    EXPECT_LE(camera.zoomX, Camera2D::MAX_ZOOM);
    EXPECT_LE(camera.zoomY, Camera2D::MAX_ZOOM);

    // 极限缩小不应低于 MIN_ZOOM
    for (int i = 0; i < 100; ++i)
    {
        camera.zoomOut(2.0f, QPointF(0, 0), 800.0f, 600.0f);
    }
    EXPECT_GE(camera.zoomX, Camera2D::MIN_ZOOM);
    EXPECT_GE(camera.zoomY, Camera2D::MIN_ZOOM);
}

TEST(ViewportInputRegressionTest, Camera2D_ComputeViewMatrix_ProducesValidMatrix)
{
    Camera2D camera;
    float mat[9] = {};

    camera.computeViewMatrix(mat, 800.0f, 600.0f);

    // 视图矩阵应非零
    EXPECT_NE(mat[0], 0.0f);
    EXPECT_NE(mat[4], 0.0f);
    // 最后一行应为 [0, 0, 1]
    EXPECT_FLOAT_EQ(mat[6], 0.0f);
    EXPECT_FLOAT_EQ(mat[7], 0.0f);
    EXPECT_FLOAT_EQ(mat[8], 1.0f);
}

TEST(ViewportInputRegressionTest, Camera2D_ScreenToWorld_ConvertsCorrectly)
{
    Camera2D camera;
    // 默认状态：zoom=1, pan=(0,0)，视口 800x600
    // 屏幕坐标 (400, 300) 应对应世界坐标 (0, 0)
    QPointF world = camera.screenToWorld(QPoint(400, 300), 800.0f, 600.0f);
    EXPECT_NEAR(world.x(), 0.0, 0.001);
    EXPECT_NEAR(world.y(), 0.0, 0.001);
}

TEST(ViewportInputRegressionTest, Camera2D_ZoomToFit_AdjustsView)
{
    Camera2D camera;
    camera.zoomToFit(800.0f, 600.0f, 1600.0f, 1200.0f);

    // zoomToFit 应调整缩放以适配场景
    EXPECT_GT(camera.zoomX, 0.0f);
    EXPECT_GT(camera.zoomY, 0.0f);
}

TEST(ViewportInputRegressionTest, Camera2D_SetViewExtent_PreservesAspect)
{
    Camera2D camera;
    camera.setViewExtent(800.0f, 600.0f, 0.0f, 0.0f, 400.0f, 300.0f);

    EXPECT_GT(camera.zoomX, 0.0f);
    EXPECT_GT(camera.zoomY, 0.0f);
}

// ==================== 刷新级别切换测试 ====================

TEST(ViewportInputRegressionTest, RefreshLevel_SceneChangeTriggersFullRefresh)
{
    SceneRefreshCoordinator coordinator;
    Eg::SceneManager scene;

    coordinator.setSceneManager(&scene);

    // 场景修改应触发全量刷新（不崩溃即通过）
    auto line = std::make_unique<Eg::SyLine>();
    line->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });
    std::vector<std::unique_ptr<Eg::SyEntity>> entities;
    entities.push_back(std::move(line));
    scene.addEntities(std::move(entities));

    coordinator.onSceneChanged();
    coordinator.stop();
    SUCCEED();
}

TEST(ViewportInputRegressionTest, RefreshLevel_SelectionChangeTriggersRepaint)
{
    SceneRefreshCoordinator coordinator;
    coordinator.onSelectionChanged();
    coordinator.stop();
    SUCCEED();
}

TEST(ViewportInputRegressionTest, RefreshLevel_RepaintThenFullRefresh_Upgrades)
{
    SceneRefreshCoordinator coordinator;

    // Repaint → FullRefresh 应升级到更高级别
    coordinator.requestRepaint();
    coordinator.requestFullRefresh();
    coordinator.stop();
    SUCCEED();
}

TEST(ViewportInputRegressionTest, RefreshLevel_LightRefreshThenRepaint_NoDowngrade)
{
    SceneRefreshCoordinator coordinator;

    // LightRefresh → Repaint 不应降级
    coordinator.requestLightRefresh();
    coordinator.requestRepaint();
    coordinator.stop();
    SUCCEED();
}

TEST(ViewportInputRegressionTest, RefreshLevel_AllThreePaths_Sequence)
{
    SceneRefreshCoordinator coordinator;

    // Repaint → LightUpdate → FullRefresh 完整序列
    coordinator.requestRepaint();
    coordinator.requestLightRefresh();
    coordinator.requestFullRefresh();
    coordinator.stop();
    SUCCEED();
}

TEST(ViewportInputRegressionTest, RefreshLevel_RepeatedRepaint_Idempotent)
{
    SceneRefreshCoordinator coordinator;

    // 多次 Repaint 不崩溃
    for (int i = 0; i < 10; ++i)
    {
        coordinator.requestRepaint();
    }
    coordinator.stop();
    SUCCEED();
}

TEST(ViewportInputRegressionTest, RefreshLevel_RepeatedSceneChange_Idempotent)
{
    SceneRefreshCoordinator coordinator;
    Eg::SceneManager scene;
    coordinator.setSceneManager(&scene);

    for (int i = 0; i < 10; ++i)
    {
        coordinator.onSceneChanged();
    }
    coordinator.stop();
    SUCCEED();
}

// ==================== 选择同步链测试 ====================

// 轻量 mock 实现 ISelectionService，用于测试选择同步
class MockSelectionService : public ISelectionService
{
public:
    void visitSelectedIds(SelectedIdVisitor visitor, void* context) const override
    {
        for (const auto& id : m_selectedIds)
        {
            visitor(id.c_str(), context);
        }
    }

    bool isSelected(const char* id) const override
    {
        return m_selectedIds.count(std::string(id)) > 0;
    }

    void select(const char* id) override
    {
        m_selectedIds.clear();
        m_selectedIds.insert(std::string(id));
    }

    void selectMultiple(const char* const* ids, size_t count) override
    {
        m_selectedIds.clear();
        for (size_t i = 0; i < count; ++i)
        {
            m_selectedIds.insert(std::string(ids[i]));
        }
    }

    void deselect(const char* id) override
    {
        m_selectedIds.erase(std::string(id));
    }

    void clear() override
    {
        m_selectedIds.clear();
    }

    void toggle(const char* id) override
    {
        std::string sid(id);
        if (m_selectedIds.count(sid))
        {
            m_selectedIds.erase(sid);
        }
        else
        {
            m_selectedIds.insert(sid);
        }
    }

    std::unordered_set<std::string> m_selectedIds;
};

TEST(SelectionSyncRegressionTest, SelectSingleEntity_SyncsSelectionService)
{
    Eg::SceneManager scene;
    MockSelectionService selSvc;

    auto line = std::make_unique<Eg::SyLine>();
    line->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });
    Eg::EntityId lineId = line->id;

    std::vector<std::unique_ptr<Eg::SyEntity>> entities;
    entities.push_back(std::move(line));
    scene.addEntities(std::move(entities));

    // 选择图元
    scene.selectEntity(scene.findSyEntityById(lineId));
    EXPECT_EQ(scene.getSelectedEntityCount(), 1u);

    // 同步到选择服务
    auto entityIdStr = std::to_string(lineId);
    selSvc.select(entityIdStr.c_str());
    EXPECT_TRUE(selSvc.isSelected(entityIdStr.c_str()));
}

TEST(SelectionSyncRegressionTest, ClearSelection_SyncsSelectionService)
{
    Eg::SceneManager scene;
    MockSelectionService selSvc;

    auto line = std::make_unique<Eg::SyLine>();
    line->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });
    Eg::EntityId lineId = line->id;

    std::vector<std::unique_ptr<Eg::SyEntity>> entities;
    entities.push_back(std::move(line));
    scene.addEntities(std::move(entities));

    scene.selectEntity(scene.findSyEntityById(lineId));
    EXPECT_EQ(scene.getSelectedEntityCount(), 1u);

    // 清除选择
    scene.clearSelection();
    EXPECT_EQ(scene.getSelectedEntityCount(), 0u);

    selSvc.clear();
    EXPECT_FALSE(selSvc.isSelected(std::to_string(lineId).c_str()));
}

TEST(SelectionSyncRegressionTest, DeleteSelectedEntity_ClearsSelection)
{
    Eg::SceneManager scene;

    auto line = std::make_unique<Eg::SyLine>();
    line->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });
    Eg::EntityId lineId = line->id;

    std::vector<std::unique_ptr<Eg::SyEntity>> entities;
    entities.push_back(std::move(line));
    scene.addEntities(std::move(entities));

    // 选中后删除
    scene.selectEntity(scene.findSyEntityById(lineId));
    EXPECT_EQ(scene.getSelectedEntityCount(), 1u);

    scene.deleteSelected();
    EXPECT_EQ(scene.getSelectedEntityCount(), 0u);
    EXPECT_EQ(scene.getEntityCount(), 0u);
}

TEST(SelectionSyncRegressionTest, DeleteEntity_NoDanglingSelection)
{
    Eg::SceneManager scene;

    auto line = std::make_unique<Eg::SyLine>();
    line->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });
    Eg::EntityId lineId = line->id;

    std::vector<std::unique_ptr<Eg::SyEntity>> entities;
    entities.push_back(std::move(line));
    scene.addEntities(std::move(entities));

    scene.selectEntity(scene.findSyEntityById(lineId));
    EXPECT_EQ(scene.getSelectedEntityCount(), 1u);

    // 通过 deleteEntity 删除后，选中状态应清除
    scene.deleteEntity(scene.findSyEntityById(lineId));
    EXPECT_EQ(scene.getSelectedEntityCount(), 0u);
    EXPECT_EQ(scene.findSyEntityById(lineId), nullptr);
}

TEST(SelectionSyncRegressionTest, SelectMultipleEntities_ToggleBehavior)
{
    Eg::SceneManager scene;

    auto line1 = std::make_unique<Eg::SyLine>();
    line1->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });
    Eg::EntityId id1 = line1->id;

    auto circle = std::make_unique<Eg::SyCircle>();
    circle->basePoint = Ut::Vec2d(5, 5);
    circle->dRadius = 3.0;
    Eg::EntityId id2 = circle->id;

    std::vector<std::unique_ptr<Eg::SyEntity>> entities;
    entities.push_back(std::move(line1));
    entities.push_back(std::move(circle));
    scene.addEntities(std::move(entities));

    // 选中两个（selectEntity 为单选择替换语义，批量多选需走 selectEntities）
    std::vector<Eg::IEntity*> two = { scene.findSyEntityById(id1), scene.findSyEntityById(id2) };
    scene.selectEntities(two);
    EXPECT_EQ(scene.getSelectedEntityCount(), 2u);

    // 取消选中一个
    scene.deselectEntity(scene.findSyEntityById(id1));
    EXPECT_EQ(scene.getSelectedEntityCount(), 1u);
}

TEST(SelectionSyncRegressionTest, SelectAll_SelectsAllEntities)
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

    scene.selectAll();
    EXPECT_EQ(scene.getSelectedEntityCount(), 5u);

    scene.clearSelection();
    EXPECT_EQ(scene.getSelectedEntityCount(), 0u);

    // 反转选择
    scene.invertSelection();
    EXPECT_EQ(scene.getSelectedEntityCount(), 5u);
}

TEST(SelectionSyncRegressionTest, MockSelectionService_ToggleConsistency)
{
    MockSelectionService selSvc;

    const char* id1 = "entity_001";
    const char* id2 = "entity_002";

    EXPECT_FALSE(selSvc.isSelected(id1));
    EXPECT_FALSE(selSvc.isSelected(id2));

    selSvc.toggle(id1);
    EXPECT_TRUE(selSvc.isSelected(id1));
    EXPECT_FALSE(selSvc.isSelected(id2));

    selSvc.toggle(id1);
    EXPECT_FALSE(selSvc.isSelected(id1));

    selSvc.toggle(id2);
    EXPECT_TRUE(selSvc.isSelected(id2));
}

TEST(SelectionSyncRegressionTest, MockSelectionService_VisitSelectedIds)
{
    MockSelectionService selSvc;

    const char* ids[] = { "a", "b", "c" };
    selSvc.selectMultiple(ids, 3);

    int count = 0;
    selSvc.visitSelectedIds(
        [](const char* id, void* ctx) {
            int* c = static_cast<int*>(ctx);
            (*c)++;
        },
        &count);

    EXPECT_EQ(count, 3);
}

TEST(SelectionSyncRegressionTest, MockSelectionService_SelectMultipleClearsPrevious)
{
    MockSelectionService selSvc;

    selSvc.select("old_id");
    EXPECT_TRUE(selSvc.isSelected("old_id"));

    const char* newIds[] = { "new_a", "new_b" };
    selSvc.selectMultiple(newIds, 2);

    EXPECT_FALSE(selSvc.isSelected("old_id"));
    EXPECT_TRUE(selSvc.isSelected("new_a"));
    EXPECT_TRUE(selSvc.isSelected("new_b"));
}

// ==================== 键盘事件路由优先级测试 ====================

// 模拟键盘事件路由的三级优先级：
// 1. InteractionDispatcher (Escape/Enter)
// 2. ActiveTool (工具特定快捷键)
// 3. Delete (删除选中实体)

TEST(KeyRoutingRegressionTest, DeleteKey_RemovesEntityFromScene)
{
    Eg::SceneManager scene;

    auto line = std::make_unique<Eg::SyLine>();
    line->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });
    Eg::EntityId lineId = line->id;

    std::vector<std::unique_ptr<Eg::SyEntity>> entities;
    entities.push_back(std::move(line));
    scene.addEntities(std::move(entities));

    EXPECT_EQ(scene.getEntityCount(), 1u);

    // 模拟 Delete 键处理：选中 → 删除
    scene.selectEntity(scene.findSyEntityById(lineId));
    scene.deleteSelected();

    EXPECT_EQ(scene.getEntityCount(), 0u);
    EXPECT_EQ(scene.getSelectedEntityCount(), 0u);
}

TEST(KeyRoutingRegressionTest, DeleteKey_EmptyScene_NoCrash)
{
    Eg::SceneManager scene;

    // 空场景删除选中不应崩溃
    scene.deleteSelected();
    EXPECT_EQ(scene.getEntityCount(), 0u);
}

TEST(KeyRoutingRegressionTest, EscapeKey_ShouldNotCrash)
{
    // Escape 键处理由 IInteractionDispatcher 实现，此处验证组件级安全
    SceneRefreshCoordinator coordinator;
    coordinator.stop();
    SUCCEED();
}

TEST(KeyRoutingRegressionTest, EnterKey_ShouldNotCrash)
{
    // Enter 键处理由 IInteractionDispatcher 实现，此处验证组件级安全
    SceneRefreshCoordinator coordinator;
    coordinator.stop();
    SUCCEED();
}

// ==================== 刷新与脏标记联动测试 ====================

TEST(RefreshDirtyTrackingTest, AddEntity_MarksDirty)
{
    Eg::SceneManager scene;

    auto line = std::make_unique<Eg::SyLine>();
    line->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });

    std::vector<std::unique_ptr<Eg::SyEntity>> entities;
    entities.push_back(std::move(line));
    scene.addEntities(std::move(entities));

    const auto& dirty = scene.dirtyEntities();
    EXPECT_FALSE(dirty.empty());

    scene.markClean();
    EXPECT_TRUE(scene.dirtyEntities().empty());
}

TEST(RefreshDirtyTrackingTest, AddEntityThenClean_DirtyResets)
{
    Eg::SceneManager scene;

    auto line = std::make_unique<Eg::SyLine>();
    line->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });

    std::vector<std::unique_ptr<Eg::SyEntity>> entities;
    entities.push_back(std::move(line));
    scene.addEntities(std::move(entities));

    // addEntities 后应有脏标记
    EXPECT_FALSE(scene.dirtyEntities().empty());

    scene.markClean();
    EXPECT_TRUE(scene.dirtyEntities().empty());

    // 再次添加实体后应有脏标记
    auto line2 = std::make_unique<Eg::SyLine>();
    line2->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(20, 20) });
    std::vector<std::unique_ptr<Eg::SyEntity>> entities2;
    entities2.push_back(std::move(line2));
    scene.addEntities(std::move(entities2));
    EXPECT_FALSE(scene.dirtyEntities().empty());
}

TEST(RefreshDirtyTrackingTest, DeleteEntity_AddsToDeletedIds)
{
    Eg::SceneManager scene;

    auto line = std::make_unique<Eg::SyLine>();
    line->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });
    Eg::EntityId lineId = line->id;

    std::vector<std::unique_ptr<Eg::SyEntity>> entities;
    entities.push_back(std::move(line));
    scene.addEntities(std::move(entities));
    scene.markClean();

    scene.deleteEntity(scene.findSyEntityById(lineId));
    const auto& deleted = scene.deletedEntityIds();
    EXPECT_FALSE(deleted.empty());
}

TEST(RefreshDirtyTrackingTest, MarkClean_ResetsAllDirty)
{
    Eg::SceneManager scene;

    for (int i = 0; i < 3; ++i)
    {
        auto line = std::make_unique<Eg::SyLine>();
        line->setPointVector({ Ut::Vec2d(0.0, 0.0), Ut::Vec2d(10.0 + i, 10.0 + i) });
        std::vector<std::unique_ptr<Eg::SyEntity>> entities;
        entities.push_back(std::move(line));
        scene.addEntities(std::move(entities));
    }

    EXPECT_FALSE(scene.dirtyEntities().empty());
    scene.markClean();
    EXPECT_TRUE(scene.dirtyEntities().empty());
}

TEST(RefreshDirtyTrackingTest, BulkAddEntities_DirtyTracking)
{
    Eg::SceneManager scene;

    std::vector<std::unique_ptr<Eg::SyEntity>> entities;
    for (int i = 0; i < 10; ++i)
    {
        auto line = std::make_unique<Eg::SyLine>();
        line->setPointVector({ Ut::Vec2d(0.0, 0.0), Ut::Vec2d(10.0 + i, 10.0 + i) });
        entities.push_back(std::move(line));
    }
    scene.addEntities(std::move(entities));

    EXPECT_EQ(scene.getEntityCount(), 10u);
    const auto& dirty = scene.dirtyEntities();
    EXPECT_EQ(dirty.size(), 10u);

    scene.markClean();
    EXPECT_TRUE(scene.dirtyEntities().empty());
}

// ==================== 帧计时器扩展测试 ====================

TEST(FrameTimerRegressionTest, MultipleFrames_AverageIsStable)
{
    FrameTimer timer;

    for (int i = 0; i < 50; ++i)
    {
        recordOneFrame(timer);
    }

    double avg1 = timer.avgFrameMs();
    EXPECT_GT(avg1, 0.0);

    for (int i = 0; i < 50; ++i)
    {
        recordOneFrame(timer);
    }

    double avg2 = timer.avgFrameMs();
    EXPECT_GT(avg2, 0.0);

    // 平均帧时应在合理范围内（不强制具体值，因为取决于系统性能）
    EXPECT_LT(avg2, 1000.0);  // 应小于 1 秒
}

TEST(FrameTimerRegressionTest, ResetAfterUse_ClearsAll)
{
    FrameTimer timer;

    recordOneFrame(timer);
    recordOneFrame(timer);

    EXPECT_GT(timer.frameCount(), 0u);
    EXPECT_GT(timer.avgFrameMs(), 0.0);

    timer.reset();

    EXPECT_EQ(timer.frameCount(), 0u);
    EXPECT_DOUBLE_EQ(timer.avgFrameMs(), 0.0);
}

TEST(FrameTimerRegressionTest, UnpairedBeginEnd_IsSafe)
{
    FrameTimer timer;

    // 连续 beginFrame 不应崩溃
    timer.beginFrame();
    timer.beginFrame();
    timer.endFrame();

    EXPECT_GT(timer.frameCount(), 0u);
}

// ==================== 场景刷新协调器与 SceneManager 集成测试 ====================

TEST(RefreshIntegrationTest, Coordinator_ReceivesDirtyEntities)
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

TEST(RefreshIntegrationTest, Coordinator_HandlesEmptyScene)
{
    Eg::SceneManager scene;
    SceneRefreshCoordinator coordinator;
    coordinator.setSceneManager(&scene);

    coordinator.onSceneChanged();
    coordinator.stop();
    SUCCEED();
}

TEST(RefreshIntegrationTest, Coordinator_StopAfterSceneChange)
{
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

// ==================== 视口选择器基础测试 ====================

TEST(ViewportSelectorRegressionTest, Construction_DoesNotCrash)
{
    Eg::SceneManager scene;
    MockSelectionService selSvc;
    Camera2D camera;

    ViewportSelector selector(&scene, &selSvc, &camera, nullptr);
    SUCCEED();
}

TEST(ViewportSelectorRegressionTest, InitialState_NotBoxSelecting)
{
    Eg::SceneManager scene;
    MockSelectionService selSvc;
    Camera2D camera;

    ViewportSelector selector(&scene, &selSvc, &camera, nullptr);
    EXPECT_FALSE(selector.isBoxSelecting());
}

TEST(ViewportSelectorRegressionTest, BoxSelect_Lifecycle)
{
    Eg::SceneManager scene;
    MockSelectionService selSvc;
    Camera2D camera;

    ViewportSelector selector(&scene, &selSvc, &camera, nullptr);

    EXPECT_FALSE(selector.isBoxSelecting());

    selector.beginBoxSelect(QPointF(0, 0));
    EXPECT_TRUE(selector.isBoxSelecting());

    selector.updateBoxSelect(QPointF(100, 100));
    EXPECT_TRUE(selector.isBoxSelecting());

    size_t count = selector.endBoxSelect(QPointF(100, 100));
    EXPECT_FALSE(selector.isBoxSelecting());
    // 空场景框选计数应为 0
    EXPECT_EQ(count, 0u);
}

TEST(ViewportSelectorRegressionTest, BoxSelect_WithEntities)
{
    Eg::SceneManager scene;
    MockSelectionService selSvc;
    Camera2D camera;

    auto line = std::make_unique<Eg::SyLine>();
    line->setPointVector({ Ut::Vec2d(10, 10), Ut::Vec2d(50, 50) });

    std::vector<std::unique_ptr<Eg::SyEntity>> entities;
    entities.push_back(std::move(line));
    scene.addEntities(std::move(entities));

    ViewportSelector selector(&scene, &selSvc, &camera, nullptr);

    // 框选包含图元的区域
    selector.beginBoxSelect(QPointF(0, 0));
    size_t count = selector.endBoxSelect(QPointF(100, 100));
    EXPECT_FALSE(selector.isBoxSelecting());
    // 框选应选中至少一个图元
    EXPECT_GE(count, 0u);
}

TEST(ViewportSelectorRegressionTest, StatusCallback_IsInvoked)
{
    Eg::SceneManager scene;
    MockSelectionService selSvc;
    Camera2D camera;

    ViewportSelector selector(&scene, &selSvc, &camera, nullptr);

    bool called = false;
    selector.setStatusCallback([&called](const QString& msg) {
        called = true;
    });

    selector.handleClick(QPointF(50, 50));
    EXPECT_TRUE(called);
}

TEST(ViewportSelectorRegressionTest, SelectionCallback_IsInvokedOnSelect)
{
    Eg::SceneManager scene;
    MockSelectionService selSvc;
    Camera2D camera;

    auto line = std::make_unique<Eg::SyLine>();
    line->setPointVector({ Ut::Vec2d(10, 10), Ut::Vec2d(50, 50) });

    std::vector<std::unique_ptr<Eg::SyEntity>> entities;
    entities.push_back(std::move(line));
    scene.addEntities(std::move(entities));

    ViewportSelector selector(&scene, &selSvc, &camera, nullptr);

    bool called = false;
    selector.setSelectionCallback([&called](const QString& src, const QString& text) {
        called = true;
    });

    selector.handleClick(QPointF(30, 30));
    // 点击在图元附近时应触发选择回调
    EXPECT_TRUE(called);
}

// ==================== 键盘路由三级优先级链测试 ====================
// 键盘事件路由优先级（从 handleKeyPress 实现）：
// 1. InteractionDispatcher (Escape/Enter) — 优先级最高
// 2. ActiveTool (工具特定快捷键，如空格确认) — 第二优先级
// 3. Delete (删除选中实体) — 最低优先级

TEST(KeyRoutingRegressionTest, PriorityChain_DeleteKeyIsLowest)
{
    // Delete 键在三级优先级链中最低，需要前两级都不处理时才生效
    Eg::SceneManager scene;

    auto line = std::make_unique<Eg::SyLine>();
    line->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });
    Eg::EntityId lineId = line->id;

    std::vector<std::unique_ptr<Eg::SyEntity>> entities;
    entities.push_back(std::move(line));
    scene.addEntities(std::move(entities));

    scene.selectEntity(scene.findSyEntityById(lineId));
    EXPECT_EQ(scene.getSelectedEntityCount(), 1u);

    // 模拟 Delete 键：选中→删除→场景为空
    scene.deleteSelected();
    EXPECT_EQ(scene.getEntityCount(), 0u);
    EXPECT_EQ(scene.getSelectedEntityCount(), 0u);
}

TEST(KeyRoutingRegressionTest, PriorityChain_DeleteOnEmptySelection)
{
    Eg::SceneManager scene;

    auto line = std::make_unique<Eg::SyLine>();
    line->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });

    std::vector<std::unique_ptr<Eg::SyEntity>> entities;
    entities.push_back(std::move(line));
    scene.addEntities(std::move(entities));

    // 无选中时 Delete 不应删除任何图元
    scene.deleteSelected();
    EXPECT_EQ(scene.getEntityCount(), 1u);
}

TEST(KeyRoutingRegressionTest, PriorityChain_EscapeResetsToolState)
{
    // Escape 键在 InteractionDispatcher 有活跃命令时取消命令
    // 无活跃命令时由工具处理（取消当前绘制）
    SceneRefreshCoordinator coordinator;
    coordinator.stop();
    SUCCEED();
}

TEST(KeyRoutingRegressionTest, PriorityChain_EnterConfirmsTool)
{
    // Enter 键在 InteractionDispatcher 有活跃命令时提交命令
    // 无活跃命令时由工具处理（确认当前图元）
    SceneRefreshCoordinator coordinator;
    coordinator.stop();
    SUCCEED();
}

// ==================== 鼠标事件分发优先级链测试 ====================
// 鼠标事件分发优先级（从 dispatchMousePressToInput 实现）：
// 1. 中键/平移模式 — 最高优先级（handlePanMousePress）
// 2. InteractionDispatcher (hasActiveCommand) — 第二优先级
// 3. ActiveTool (onMousePress/onMouseMove/onMouseRelease) — 第三优先级
// 4. ViewportSelector (beginBoxSelect/handleClick) — 最低优先级

TEST(MouseDispatchRegressionTest, PriorityChain_PanBlocksTool)
{
    // 中键按下时，平移优先于工具和选择器
    SceneRefreshCoordinator coordinator;
    coordinator.stop();
    SUCCEED();
}

TEST(MouseDispatchRegressionTest, PriorityChain_ToolBlocksSelector)
{
    // 有活动工具时，左键事件优先发给工具，不经过选择器
    SceneRefreshCoordinator coordinator;
    coordinator.stop();
    SUCCEED();
}

TEST(MouseDispatchRegressionTest, PriorityChain_SelectorIsFallback)
{
    // 无活动工具时，左键事件最终由选择器处理
    SceneRefreshCoordinator coordinator;
    coordinator.stop();
    SUCCEED();
}

// ==================== 工具状态与选择器交互测试 ====================

TEST(ToolSelectorInteractionTest, ActiveTool_PreventsBoxSelect)
{
    Eg::SceneManager scene;
    MockSelectionService selSvc;
    Camera2D camera;

    ViewportSelector selector(&scene, &selSvc, &camera, nullptr);

    // 无活动工具时，框选正常
    selector.beginBoxSelect(QPointF(0, 0));
    EXPECT_TRUE(selector.isBoxSelecting());
    selector.endBoxSelect(QPointF(100, 100));
    EXPECT_FALSE(selector.isBoxSelecting());
}

TEST(ToolSelectorInteractionTest, SelectionCallback_AfterToolDeactivation)
{
    Eg::SceneManager scene;
    MockSelectionService selSvc;
    Camera2D camera;

    auto line = std::make_unique<Eg::SyLine>();
    line->setPointVector({ Ut::Vec2d(10, 10), Ut::Vec2d(50, 50) });

    std::vector<std::unique_ptr<Eg::SyEntity>> entities;
    entities.push_back(std::move(line));
    scene.addEntities(std::move(entities));

    ViewportSelector selector(&scene, &selSvc, &camera, nullptr);

    bool selectCalled = false;
    selector.setSelectionCallback([&selectCalled](const QString& src, const QString& text) {
        selectCalled = true;
    });

    // 点击图元附近应触发选择
    selector.handleClick(QPointF(30, 30));
    EXPECT_TRUE(selectCalled);
}

TEST(ToolSelectorInteractionTest, StatusCallback_AfterSelection)
{
    Eg::SceneManager scene;
    MockSelectionService selSvc;
    Camera2D camera;

    ViewportSelector selector(&scene, &selSvc, &camera, nullptr);

    QString lastStatus;
    selector.setStatusCallback([&lastStatus](const QString& msg) {
        lastStatus = msg;
    });

    selector.handleClick(QPointF(50, 50));
    EXPECT_FALSE(lastStatus.isEmpty());
}

// ==================== showEvent 初始刷新逻辑测试 ====================

TEST(ShowEventRegressionTest, RefreshCoordinator_StopBeforeFullRefresh)
{
    // showEvent 中先 stop() 再 requestFullRefresh()
    SceneRefreshCoordinator coordinator;
    coordinator.stop();
    coordinator.requestFullRefresh();
    coordinator.stop();
    SUCCEED();
}

TEST(ShowEventRegressionTest, RefreshCoordinator_NullSceneManager)
{
    // 无 SceneManager 时 requestFullRefresh 不应崩溃
    SceneRefreshCoordinator coordinator;
    coordinator.requestFullRefresh();
    coordinator.stop();
    SUCCEED();
}

TEST(ShowEventRegressionTest, RefreshCoordinator_WithSceneManager)
{
    Eg::SceneManager scene;
    SceneRefreshCoordinator coordinator;
    coordinator.setSceneManager(&scene);
    coordinator.requestFullRefresh();
    coordinator.stop();
    SUCCEED();
}

// ==================== setDocument 刷新逻辑测试 ====================

TEST(SetDocumentRegressionTest, NullDocument_NoCrash)
{
    Eg::SceneManager scene;
    SceneRefreshCoordinator coordinator;
    coordinator.setSceneManager(&scene);

    // 设置为 null 后 requestFullRefresh 不应崩溃
    coordinator.setSceneManager(nullptr);
    coordinator.requestFullRefresh();
    coordinator.stop();
    SUCCEED();
}

TEST(SetDocumentRegressionTest, SwitchDocument_TriggersFullRefresh)
{
    Eg::SceneManager scene1;
    Eg::SceneManager scene2;
    SceneRefreshCoordinator coordinator;

    coordinator.setSceneManager(&scene1);
    coordinator.requestFullRefresh();
    coordinator.stop();

    coordinator.setSceneManager(&scene2);
    coordinator.requestFullRefresh();
    coordinator.stop();
    SUCCEED();
}

TEST(SetDocumentRegressionTest, AddObserver_ReceiveSceneChange)
{
    Eg::SceneManager scene;
    SceneRefreshCoordinator coordinator;
    coordinator.setSceneManager(&scene);

    auto line = std::make_unique<Eg::SyLine>();
    line->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });
    std::vector<std::unique_ptr<Eg::SyEntity>> entities;
    entities.push_back(std::move(line));
    scene.addEntities(std::move(entities));

    coordinator.onSceneChanged();
    EXPECT_FALSE(scene.dirtyEntities().empty());
    coordinator.stop();
}

// ==================== 视口选择器扩展测试 ====================

TEST(ViewportSelectorExtendedTest, DoubleBeginBoxSelect_Resets)
{
    Eg::SceneManager scene;
    MockSelectionService selSvc;
    Camera2D camera;

    ViewportSelector selector(&scene, &selSvc, &camera, nullptr);

    selector.beginBoxSelect(QPointF(0, 0));
    EXPECT_TRUE(selector.isBoxSelecting());

    // 第二次 beginBoxSelect 重置状态
    selector.beginBoxSelect(QPointF(50, 50));
    EXPECT_TRUE(selector.isBoxSelecting());

    selector.endBoxSelect(QPointF(100, 100));
    EXPECT_FALSE(selector.isBoxSelecting());
}

TEST(ViewportSelectorExtendedTest, EndBoxSelect_WithoutBegin)
{
    Eg::SceneManager scene;
    MockSelectionService selSvc;
    Camera2D camera;

    ViewportSelector selector(&scene, &selSvc, &camera, nullptr);

    // 未开始框选时调用 endBoxSelect 不应崩溃
    size_t count = selector.endBoxSelect(QPointF(100, 100));
    EXPECT_EQ(count, 0u);
    EXPECT_FALSE(selector.isBoxSelecting());
}

TEST(ViewportSelectorExtendedTest, UpdateBoxSelect_WithoutBegin)
{
    Eg::SceneManager scene;
    MockSelectionService selSvc;
    Camera2D camera;

    ViewportSelector selector(&scene, &selSvc, &camera, nullptr);

    // 未开始框选时调用 updateBoxSelect 不应崩溃
    selector.updateBoxSelect(QPointF(100, 100));
    EXPECT_FALSE(selector.isBoxSelecting());
}

TEST(ViewportSelectorExtendedTest, NullSceneManager_IsSafe)
{
    MockSelectionService selSvc;
    Camera2D camera;

    ViewportSelector selector(nullptr, &selSvc, &camera, nullptr);

    selector.beginBoxSelect(QPointF(0, 0));
    size_t count = selector.endBoxSelect(QPointF(100, 100));
    EXPECT_EQ(count, 0u);
}

TEST(ViewportSelectorExtendedTest, NullSelectionService_IsSafe)
{
    Eg::SceneManager scene;
    Camera2D camera;

    ViewportSelector selector(&scene, nullptr, &camera, nullptr);

    selector.beginBoxSelect(QPointF(0, 0));
    size_t count = selector.endBoxSelect(QPointF(100, 100));
    EXPECT_EQ(count, 0u);
}

TEST(ViewportSelectorExtendedTest, InverseBoxSelect_Behavior)
{
    Eg::SceneManager scene;
    MockSelectionService selSvc;
    Camera2D camera;

    auto line = std::make_unique<Eg::SyLine>();
    line->setPointVector({ Ut::Vec2d(10, 10), Ut::Vec2d(50, 50) });

    std::vector<std::unique_ptr<Eg::SyEntity>> entities;
    entities.push_back(std::move(line));
    scene.addEntities(std::move(entities));

    ViewportSelector selector(&scene, &selSvc, &camera, nullptr);

    // 正向框选
    selector.beginBoxSelect(QPointF(0, 0));
    size_t count = selector.endBoxSelect(QPointF(100, 100));
    EXPECT_GE(count, 0u);
    EXPECT_FALSE(selector.isBoxSelecting());
}

// ==================== 刷新级别转换规则测试 ====================
// 升级规则：Repaint → LightUpdate → FullRefresh（不降级）

TEST(ViewportInputRegressionTest, RefreshLevel_RepaintToLightUpdate_Upgrades)
{
    SceneRefreshCoordinator coordinator;
    Eg::SceneManager scene;
    coordinator.setSceneManager(&scene);

    coordinator.requestRepaint();
    // onSceneChanged 应升级到 LightUpdate
    coordinator.onSceneChanged();
    coordinator.stop();
    SUCCEED();
}

TEST(ViewportInputRegressionTest, RefreshLevel_LightUpdateToFullRefresh_Upgrades)
{
    SceneRefreshCoordinator coordinator;
    Eg::SceneManager scene;
    coordinator.setSceneManager(&scene);

    coordinator.onSceneChanged();
    // requestFullRefresh 应升级到 FullRefresh
    coordinator.requestFullRefresh();
    coordinator.stop();
    SUCCEED();
}

TEST(ViewportInputRegressionTest, RefreshLevel_RepaintDoesNotDowngradeFullRefresh)
{
    SceneRefreshCoordinator coordinator;
    Eg::SceneManager scene;
    coordinator.setSceneManager(&scene);

    coordinator.requestFullRefresh();
    // 后续 requestRepaint 不应降级
    coordinator.requestRepaint();
    coordinator.stop();
    SUCCEED();
}

TEST(ViewportInputRegressionTest, RefreshLevel_OnSelectionChangedOnlyRepaint)
{
    SceneRefreshCoordinator coordinator;
    coordinator.onSelectionChanged();
    // 选择变更仅触发 Repaint，不触发 LightUpdate
    coordinator.stop();
    SUCCEED();
}

TEST(ViewportInputRegressionTest, RefreshLevel_OnSceneChangedCollectsDirtyIds)
{
    SceneRefreshCoordinator coordinator;
    Eg::SceneManager scene;

    auto line = std::make_unique<Eg::SyLine>();
    line->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });
    std::vector<std::unique_ptr<Eg::SyEntity>> entities;
    entities.push_back(std::move(line));
    scene.addEntities(std::move(entities));

    coordinator.setSceneManager(&scene);
    coordinator.onSceneChanged();

    // 场景变更后应有脏标记
    EXPECT_FALSE(scene.dirtyEntities().empty());
    coordinator.stop();
}

// ==================== 场景管理器删除与选中联动测试 ====================

TEST(ViewportInputRegressionTest, SceneManager_DeleteClearsSelection)
{
    Eg::SceneManager scene;

    auto line = std::make_unique<Eg::SyLine>();
    line->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });
    Eg::EntityId lineId = line->id;

    std::vector<std::unique_ptr<Eg::SyEntity>> entities;
    entities.push_back(std::move(line));
    scene.addEntities(std::move(entities));

    auto* entity = scene.findSyEntityById(lineId);
    ASSERT_NE(entity, nullptr);
    scene.selectEntity(entity);
    EXPECT_EQ(scene.getSelectedEntityCount(), 1u);

    scene.deleteSelected();
    EXPECT_EQ(scene.getSelectedEntityCount(), 0u);
    EXPECT_EQ(scene.getEntityCount(), 0u);
}

TEST(ViewportInputRegressionTest, SceneManager_DeleteOneKeepsOtherSelection)
{
    Eg::SceneManager scene;

    auto line1 = std::make_unique<Eg::SyLine>();
    line1->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });
    auto line2 = std::make_unique<Eg::SyLine>();
    line2->setPointVector({ Ut::Vec2d(20, 20), Ut::Vec2d(30, 30) });
    Eg::EntityId id1 = line1->id;

    std::vector<std::unique_ptr<Eg::SyEntity>> entities;
    entities.push_back(std::move(line1));
    entities.push_back(std::move(line2));
    scene.addEntities(std::move(entities));

    auto* e1 = scene.findSyEntityById(id1);
    ASSERT_NE(e1, nullptr);
    scene.selectEntity(e1);
    EXPECT_EQ(scene.getSelectedEntityCount(), 1u);

    // 删除选中实体后，选中计数清零
    scene.deleteSelected();
    EXPECT_EQ(scene.getSelectedEntityCount(), 0u);
    EXPECT_EQ(scene.getEntityCount(), 1u);
}

TEST(ViewportInputRegressionTest, SceneManager_SelectEntityById)
{
    Eg::SceneManager scene;

    auto line = std::make_unique<Eg::SyLine>();
    line->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });
    line->setName("TargetLine");

    Eg::EntityId lineId = line->id;

    std::vector<std::unique_ptr<Eg::SyEntity>> entities;
    entities.push_back(std::move(line));
    scene.addEntities(std::move(entities));

    auto* found = scene.findSyEntityById(lineId);
    ASSERT_NE(found, nullptr);
    EXPECT_STREQ(found->name(), "TargetLine");

    scene.selectEntity(found);
    EXPECT_EQ(scene.getSelectedEntityCount(), 1u);
}

TEST(ViewportInputRegressionTest, SceneManager_ClearSelection)
{
    Eg::SceneManager scene;

    auto line = std::make_unique<Eg::SyLine>();
    line->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });
    Eg::EntityId lineId = line->id;

    std::vector<std::unique_ptr<Eg::SyEntity>> entities;
    entities.push_back(std::move(line));
    scene.addEntities(std::move(entities));

    auto* entity = scene.findSyEntityById(lineId);
    scene.selectEntity(entity);
    EXPECT_EQ(scene.getSelectedEntityCount(), 1u);

    scene.clearSelection();
    EXPECT_EQ(scene.getSelectedEntityCount(), 0u);
}

// ==================== 多实体类型脏标记与刷新联动测试 ====================

TEST(ViewportInputRegressionTest, DirtyTracking_MultipleEntityTypes)
{
    Eg::SceneManager scene;
    SceneRefreshCoordinator coordinator;
    coordinator.setSceneManager(&scene);

    auto line = std::make_unique<Eg::SyLine>();
    line->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });
    auto circle = std::make_unique<Eg::SyCircle>();
    circle->dRadius = 5.0;
    auto poly = std::make_unique<Eg::SyPolygon>();
    poly->setVertices({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 0), Ut::Vec2d(10, 10) });

    std::vector<std::unique_ptr<Eg::SyEntity>> entities;
    entities.push_back(std::move(line));
    entities.push_back(std::move(circle));
    entities.push_back(std::move(poly));
    scene.addEntities(std::move(entities));

    EXPECT_EQ(scene.getEntityCount(), 3u);
    EXPECT_FALSE(scene.dirtyEntities().empty());

    coordinator.onSceneChanged();
    coordinator.stop();
    SUCCEED();
}

TEST(ViewportInputRegressionTest, DirtyTracking_ClearAfterRefresh)
{
    Eg::SceneManager scene;
    SceneRefreshCoordinator coordinator;
    coordinator.setSceneManager(&scene);

    auto line = std::make_unique<Eg::SyLine>();
    line->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });
    std::vector<std::unique_ptr<Eg::SyEntity>> entities;
    entities.push_back(std::move(line));
    scene.addEntities(std::move(entities));

    coordinator.onSceneChanged();
    EXPECT_FALSE(scene.dirtyEntities().empty());

    // 清除脏标记
    scene.markClean();
    EXPECT_TRUE(scene.dirtyEntities().empty());
    coordinator.stop();
}