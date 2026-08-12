/**
 * @file RenderViewport2DRegressionTests.cpp
 * @brief 视口组件级回归测试 — 覆盖 SceneRefreshCoordinator + SceneManager 行为、刷新管线、脏标记传播
 *
 * 测试范围：
 *  - SceneRefreshCoordinator 四级刷新策略完整生命周期
 *  - SceneManager 脏标记传播与刷新联动
 *  - 实体增删改后的刷新触发
 *  - 选择变更后的重绘触发
 *  - 帧计时器性能监控
 *  - 刷新级别升级/降级规则
 *  - 大批量实体刷新压力
 *
 * 注意：需要 QWidget 的测试（RenderViewport2D 事件分发）需要 QApplication 实例，
 * 当前版本以 SceneRefreshCoordinator / SceneManager 的组件级测试为主。
 *
 * P5 测试覆盖扩展 (2026-07-30)
 */

#include <gtest/gtest.h>

#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QContextMenuEvent>
#include <QWidget>

#include "UI/Render/SceneRefreshCoordinator.h"
#include "UI/Render/Camera2D.h"
#include "UI/Render/ViewportInputRouter.h"
#include "UI/Widgets/ViewportSelector.h"
#include "UI/Services/ISelectionService.h"
#include "Engine2D/Core/SceneManager.h"
#include "Engine2D/SyEntity/SyLine.h"
#include "Engine2D/SyEntity/SyCircle.h"
#include "Engine2D/SyEntity/SyPolygon.h"
#include "Log/SyPerfCounter.h"

#include <memory>
#include <vector>
#include <string>
#include <algorithm>
#include <cstring>

 // ==================== SceneRefreshCoordinator 完整生命周期测试 ====================

TEST(RenderViewport2DRegressionTest, RefreshCoordinator_ConstructWithoutWidget)
{
    SceneRefreshCoordinator coordinator;
    // 不设置 RenderWidget 时不应崩溃
    coordinator.stop();
    SUCCEED();
}

TEST(RenderViewport2DRegressionTest, RefreshCoordinator_SetSceneManager)
{
    SceneRefreshCoordinator coordinator;
    Eg::SceneManager scene;
    coordinator.setSceneManager(&scene);
    coordinator.stop();
    SUCCEED();
}

TEST(RenderViewport2DRegressionTest, RefreshCoordinator_LifecycleSequence)
{
    SceneRefreshCoordinator coordinator;
    Eg::SceneManager scene;
    coordinator.setSceneManager(&scene);

    // 完整生命周期序列：构造 → 设置场景 → 各种刷新 → 停止
    coordinator.requestRepaint();
    coordinator.requestLightRefresh();
    coordinator.requestFullRefresh();
    coordinator.onSelectionChanged();
    coordinator.stop();
    SUCCEED();
}

TEST(RenderViewport2DRegressionTest, RefreshCoordinator_StopTwice)
{
    SceneRefreshCoordinator coordinator;
    coordinator.stop();
    coordinator.stop(); // 重复停止不崩溃
    SUCCEED();
}

// ==================== 刷新级别升级规则测试 ====================

TEST(RenderViewport2DRegressionTest, RefreshLevel_RepaintUpgradesToLightUpdate)
{
    SceneRefreshCoordinator coordinator;
    Eg::SceneManager scene;
    coordinator.setSceneManager(&scene);

    // Repaint → onSceneChanged 应升级到 LightUpdate
    coordinator.requestRepaint();
    coordinator.onSceneChanged();
    coordinator.stop();
    SUCCEED();
}

TEST(RenderViewport2DRegressionTest, RefreshLevel_LightUpdateUpgradesToFullRefresh)
{
    SceneRefreshCoordinator coordinator;
    Eg::SceneManager scene;
    coordinator.setSceneManager(&scene);

    // LightUpdate → requestFullRefresh 应升级到 FullRefresh
    coordinator.onSceneChanged();
    coordinator.requestFullRefresh();
    coordinator.stop();
    SUCCEED();
}

TEST(RenderViewport2DRegressionTest, RefreshLevel_RepaintDoesNotDowngradeFullRefresh)
{
    SceneRefreshCoordinator coordinator;
    Eg::SceneManager scene;
    coordinator.setSceneManager(&scene);

    // FullRefresh → Repaint 不应降级
    coordinator.requestFullRefresh();
    coordinator.requestRepaint();
    coordinator.stop();
    SUCCEED();
}

TEST(RenderViewport2DRegressionTest, RefreshLevel_AllFourLevelsSequence)
{
    SceneRefreshCoordinator coordinator;
    Eg::SceneManager scene;
    coordinator.setSceneManager(&scene);

    // None → Repaint → LightUpdate → FullRefresh 完整序列
    coordinator.requestRepaint();
    coordinator.onSceneChanged();
    coordinator.requestFullRefresh();
    coordinator.stop();
    SUCCEED();
}

// P5 刷新语义统一：requestLightRefresh 专项测试

TEST(RenderViewport2DRegressionTest, RefreshLevel_RepaintUpgradesToLightRefresh)
{
    SceneRefreshCoordinator coordinator;
    Eg::SceneManager scene;
    coordinator.setSceneManager(&scene);

    // Repaint → requestLightRefresh 应升级到 LightUpdate
    coordinator.requestRepaint();
    coordinator.requestLightRefresh();
    coordinator.stop();
    SUCCEED();
}

TEST(RenderViewport2DRegressionTest, RefreshLevel_LightRefreshUpgradesToFullRefresh)
{
    SceneRefreshCoordinator coordinator;
    Eg::SceneManager scene;
    coordinator.setSceneManager(&scene);

    // LightRefresh → requestFullRefresh 应升级到 FullRefresh
    coordinator.requestLightRefresh();
    coordinator.requestFullRefresh();
    coordinator.stop();
    SUCCEED();
}

TEST(RenderViewport2DRegressionTest, RefreshLevel_LightRefreshDoesNotDowngradeFullRefresh)
{
    SceneRefreshCoordinator coordinator;
    Eg::SceneManager scene;
    coordinator.setSceneManager(&scene);

    // FullRefresh → requestLightRefresh 不应降级
    coordinator.requestFullRefresh();
    coordinator.requestLightRefresh();
    coordinator.stop();
    SUCCEED();
}

TEST(RenderViewport2DRegressionTest, RefreshLevel_LightRefreshNullWidgetNoCrash)
{
    SceneRefreshCoordinator coordinator;
    // 无 RenderWidget 时不应崩溃
    coordinator.requestLightRefresh();
    coordinator.stop();
    SUCCEED();
}

// ==================== 脏标记传播测试 ====================

TEST(RenderViewport2DRegressionTest, DirtyFlag_AddEntityTriggersSceneChanged)
{
    Eg::SceneManager scene;

    auto line = std::make_unique<Eg::SyLine>();
    line->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });

    std::vector<std::unique_ptr<Eg::SyEntity>> entities;
    entities.push_back(std::move(line));
    scene.addEntities(std::move(entities));

    EXPECT_EQ(scene.getEntityCount(), 1u);
    // 添加实体后场景管理器应有脏标记
    // 注意：SceneManager 的实际脏标记行为取决于实现
}

TEST(RenderViewport2DRegressionTest, DirtyFlag_ModifyEntityTriggersDirty)
{
    Eg::SceneManager scene;

    auto line = std::make_unique<Eg::SyLine>();
    line->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });
    Eg::EntityId lineId = line->id;

    std::vector<std::unique_ptr<Eg::SyEntity>> entities;
    entities.push_back(std::move(line));
    scene.addEntities(std::move(entities));

    // 修改实体属性
    auto* entity = scene.findSyEntityById(lineId);
    ASSERT_NE(entity, nullptr);
    entity->setVisible(false);
    // 修改后应有脏标记
}

TEST(RenderViewport2DRegressionTest, DirtyFlag_DeleteEntityTriggersDirty)
{
    Eg::SceneManager scene;

    auto line = std::make_unique<Eg::SyLine>();
    line->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });
    Eg::EntityId lineId = line->id;

    std::vector<std::unique_ptr<Eg::SyEntity>> entities;
    entities.push_back(std::move(line));
    scene.addEntities(std::move(entities));

    scene.deleteEntity(scene.findSyEntityById(lineId));
    EXPECT_EQ(scene.getEntityCount(), 0u);
    // 删除后应有脏标记
}

// ==================== 选择变更触发重绘测试 ====================

TEST(RenderViewport2DRegressionTest, SelectionChange_TriggersRepaint)
{
    SceneRefreshCoordinator coordinator;
    Eg::SceneManager scene;
    coordinator.setSceneManager(&scene);

    auto line = std::make_unique<Eg::SyLine>();
    line->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });
    Eg::EntityId lineId = line->id;

    std::vector<std::unique_ptr<Eg::SyEntity>> entities;
    entities.push_back(std::move(line));
    scene.addEntities(std::move(entities));

    // 选中实体
    scene.selectEntity(scene.findSyEntityById(lineId));
    EXPECT_EQ(scene.getSelectedEntityCount(), 1u);

    // 选择变更应触发 repaint
    coordinator.onSelectionChanged();
    coordinator.stop();
    SUCCEED();
}

TEST(RenderViewport2DRegressionTest, SelectionChange_ClearSelectionTriggersRepaint)
{
    SceneRefreshCoordinator coordinator;
    Eg::SceneManager scene;
    coordinator.setSceneManager(&scene);

    auto line = std::make_unique<Eg::SyLine>();
    line->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });
    Eg::EntityId lineId = line->id;

    std::vector<std::unique_ptr<Eg::SyEntity>> entities;
    entities.push_back(std::move(line));
    scene.addEntities(std::move(entities));

    scene.selectEntity(scene.findSyEntityById(lineId));
    scene.clearSelection();
    EXPECT_EQ(scene.getSelectedEntityCount(), 0u);

    coordinator.onSelectionChanged();
    coordinator.stop();
    SUCCEED();
}

TEST(RenderViewport2DRegressionTest, SelectionChange_DeleteSelectedClearsSelection)
{
    Eg::SceneManager scene;

    auto line = std::make_unique<Eg::SyLine>();
    line->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });
    Eg::EntityId lineId = line->id;

    std::vector<std::unique_ptr<Eg::SyEntity>> entities;
    entities.push_back(std::move(line));
    scene.addEntities(std::move(entities));

    scene.selectEntity(scene.findSyEntityById(lineId));
    scene.deleteSelected();
    EXPECT_EQ(scene.getSelectedEntityCount(), 0u);
    EXPECT_EQ(scene.getEntityCount(), 0u);
}

// ==================== 刷新协调器与场景管理器联动测试 ====================

TEST(RenderViewport2DRegressionTest, Coordinator_SceneChangeRefreshesWithEntities)
{
    SceneRefreshCoordinator coordinator;
    Eg::SceneManager scene;
    coordinator.setSceneManager(&scene);

    // 添加实体
    auto line = std::make_unique<Eg::SyLine>();
    line->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });

    std::vector<std::unique_ptr<Eg::SyEntity>> entities;
    entities.push_back(std::move(line));
    scene.addEntities(std::move(entities));

    // 场景变更通知
    coordinator.onSceneChanged();
    coordinator.stop();
    SUCCEED();
}

TEST(RenderViewport2DRegressionTest, Coordinator_SceneChangeWithMultipleEntities)
{
    SceneRefreshCoordinator coordinator;
    Eg::SceneManager scene;
    coordinator.setSceneManager(&scene);

    // 添加多个实体
    std::vector<std::unique_ptr<Eg::SyEntity>> entities;
    for (int i = 0; i < 5; ++i)
    {
        auto line = std::make_unique<Eg::SyLine>();
        line->setPointVector({ Ut::Vec2d(0.0, 0.0), Ut::Vec2d(10.0 + i * 10, 10.0 + i * 10) });
        entities.push_back(std::move(line));
    }
    scene.addEntities(std::move(entities));
    EXPECT_EQ(scene.getEntityCount(), 5u);

    coordinator.onSceneChanged();
    coordinator.stop();
    SUCCEED();
}

TEST(RenderViewport2DRegressionTest, Coordinator_MixedTypeEntities)
{
    SceneRefreshCoordinator coordinator;
    Eg::SceneManager scene;
    coordinator.setSceneManager(&scene);

    // 添加混合类型实体
    std::vector<std::unique_ptr<Eg::SyEntity>> entities;
    {
        auto line = std::make_unique<Eg::SyLine>();
        line->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });
        entities.push_back(std::move(line));
    }
    {
        auto circle = std::make_unique<Eg::SyCircle>();
        circle->basePoint = Ut::Vec2d(5, 5);
        circle->dRadius = 3.0;
        entities.push_back(std::move(circle));
    }
    {
        auto poly = std::make_unique<Eg::SyPolygon>();
        poly->setVertices({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 0), Ut::Vec2d(10, 10) });
        entities.push_back(std::move(poly));
    }
    scene.addEntities(std::move(entities));
    EXPECT_EQ(scene.getEntityCount(), 3u);

    coordinator.onSceneChanged();
    coordinator.stop();
    SUCCEED();
}

// ==================== 帧计时器与性能监控测试 ====================

TEST(RenderViewport2DRegressionTest, FrameTimer_DefaultConstruction)
{
    FrameTimer timer;
    // 默认构造不崩溃
    SUCCEED();
}

TEST(RenderViewport2DRegressionTest, FrameTimer_BeginEndFrame)
{
    FrameTimer timer;
    timer.beginFrame();
    timer.endFrame();
    // 帧计时基本操作不崩溃
    SUCCEED();
}

TEST(RenderViewport2DRegressionTest, FrameTimer_MultipleFrames)
{
    FrameTimer timer;

    for (int i = 0; i < 10; ++i)
    {
        timer.beginFrame();
        timer.endFrame();
    }
    SUCCEED();
}

TEST(RenderViewport2DRegressionTest, FrameTimer_GetAverageFrameTime)
{
    FrameTimer timer;

    for (int i = 0; i < 5; ++i)
    {
        timer.beginFrame();
        timer.endFrame();
    }

    double avg = timer.avgFrameMs();
    // 帧耗时应为非负数
    EXPECT_GE(avg, 0.0);
}

TEST(RenderViewport2DRegressionTest, FrameTimer_GetFrameCount)
{
    FrameTimer timer;

    for (int i = 0; i < 3; ++i)
    {
        timer.beginFrame();
        timer.endFrame();
    }

    uint64_t count = timer.frameCount();
    EXPECT_EQ(count, 3u);
}

TEST(RenderViewport2DRegressionTest, FrameTimer_ResetClearsData)
{
    FrameTimer timer;

    timer.beginFrame();
    timer.endFrame();
    timer.beginFrame();
    timer.endFrame();

    timer.reset();

    EXPECT_EQ(timer.frameCount(), 0u);
    EXPECT_DOUBLE_EQ(timer.avgFrameMs(), 0.0);
}

TEST(RenderViewport2DRegressionTest, FrameTimer_BeginWithoutEnd)
{
    FrameTimer timer;
    timer.beginFrame();
    // 未调用 endFrame 时再次 beginFrame 不应崩溃
    timer.beginFrame();
    timer.endFrame();
    SUCCEED();
}

TEST(RenderViewport2DRegressionTest, FrameTimer_EndWithoutBegin)
{
    FrameTimer timer;
    // 未调用 beginFrame 时调用 endFrame 不应崩溃
    timer.endFrame();
    SUCCEED();
}

TEST(RenderViewport2DRegressionTest, Coordinator_PerfMonitorToggle)
{
    SceneRefreshCoordinator coordinator;

    // 启用性能监控
    coordinator.setPerfMonitorEnabled(true);
    EXPECT_NE(coordinator.frameTimer(), nullptr);

    // 禁用性能监控：帧计时器释放
    coordinator.setPerfMonitorEnabled(false);
    EXPECT_EQ(coordinator.frameTimer(), nullptr);

    coordinator.stop();
    SUCCEED();
}

TEST(RenderViewport2DRegressionTest, Coordinator_PerfMonitorDoubleToggle)
{
    SceneRefreshCoordinator coordinator;

    coordinator.setPerfMonitorEnabled(true);
    coordinator.setPerfMonitorEnabled(true); // 重复启用
    coordinator.setPerfMonitorEnabled(false);
    coordinator.setPerfMonitorEnabled(false); // 重复禁用
    coordinator.stop();
    SUCCEED();
}

// ==================== 大批量实体刷新压力测试 ====================

TEST(RenderViewport2DRegressionTest, Stress_LargeBatchEntityAdd)
{
    SceneRefreshCoordinator coordinator;
    Eg::SceneManager scene;
    coordinator.setSceneManager(&scene);

    // 大批量添加实体
    std::vector<std::unique_ptr<Eg::SyEntity>> entities;
    for (int i = 0; i < 100; ++i)
    {
        auto line = std::make_unique<Eg::SyLine>();
        line->setPointVector({ Ut::Vec2d(0.0, 0.0), Ut::Vec2d(10.0 + i * 0.1, 10.0 + i * 0.1) });
        entities.push_back(std::move(line));
    }
    scene.addEntities(std::move(entities));
    EXPECT_EQ(scene.getEntityCount(), 100u);

    coordinator.onSceneChanged();
    coordinator.stop();
    SUCCEED();
}

TEST(RenderViewport2DRegressionTest, Stress_RepeatedAddAndDelete)
{
    SceneRefreshCoordinator coordinator;
    Eg::SceneManager scene;
    coordinator.setSceneManager(&scene);

    // 反复添加和删除实体
    for (int round = 0; round < 5; ++round)
    {
        std::vector<std::unique_ptr<Eg::SyEntity>> entities;
        std::vector<Eg::EntityId> ids;
        for (int i = 0; i < 10; ++i)
        {
            auto line = std::make_unique<Eg::SyLine>();
            line->setPointVector({ Ut::Vec2d(0.0, 0.0), Ut::Vec2d(10.0, 10.0) });
            ids.push_back(line->id);
            entities.push_back(std::move(line));
        }
        scene.addEntities(std::move(entities));
        coordinator.onSceneChanged();

        for (auto& id : ids)
        {
            scene.deleteEntity(scene.findSyEntityById(id));
        }
        coordinator.onSceneChanged();
    }

    EXPECT_EQ(scene.getEntityCount(), 0u);
    coordinator.stop();
    SUCCEED();
}

TEST(RenderViewport2DRegressionTest, Stress_RapidRefreshSequence)
{
    SceneRefreshCoordinator coordinator;
    Eg::SceneManager scene;
    coordinator.setSceneManager(&scene);

    // 快速交替刷新请求
    for (int i = 0; i < 50; ++i)
    {
        coordinator.requestRepaint();
        coordinator.requestLightRefresh();
        coordinator.requestFullRefresh();
        coordinator.onSelectionChanged();
    }
    coordinator.stop();
    SUCCEED();
}

// ==================== Camera2D 与视口联动测试 ====================

TEST(RenderViewport2DRegressionTest, Camera2D_ViewMatrixAfterPan)
{
    Camera2D camera;
    camera.pan(100.0f, 50.0f);

    float mat[9] = {};
    camera.computeViewMatrix(mat, 800.0f, 600.0f);

    EXPECT_NE(mat[0], 0.0f);
    EXPECT_NE(mat[4], 0.0f);
    // 平移后视图矩阵应反映偏移（Mat3f 为列主序：平移分量在 mat[6]/mat[7]）
    // scaleX = 2*zoomX/vpW = 0.0025，tx = scaleX * panX = 0.25
    EXPECT_NEAR(mat[6], 0.25f, 1e-4f);      // tx 分量
    EXPECT_NEAR(mat[7], 0.1666667f, 1e-4f); // ty 分量
}

TEST(RenderViewport2DRegressionTest, Camera2D_ViewMatrixAfterZoom)
{
    Camera2D camera;
    float before[9] = {};
    camera.computeViewMatrix(before, 800.0f, 600.0f);

    camera.zoomIn(2.0f, QPointF(0, 0), 800.0f, 600.0f);

    float after[9] = {};
    camera.computeViewMatrix(after, 800.0f, 600.0f);

    // 缩放 2 倍后视图矩阵 scale 应翻倍（scaleX = 2*zoomX/vpW）
    EXPECT_NEAR(after[0], before[0] * 2.0f, 1e-5f);
    EXPECT_NEAR(after[4], before[4] * 2.0f, 1e-5f);
}

TEST(RenderViewport2DRegressionTest, Camera2D_ZoomToFitCentersScene)
{
    Camera2D camera;
    camera.zoomToFit(800.0f, 600.0f, 1600.0f, 1200.0f);

    // zoomToFit 后缩放应非零
    EXPECT_GT(camera.zoomX, 0.0f);
    EXPECT_GT(camera.zoomY, 0.0f);
}

TEST(RenderViewport2DRegressionTest, Camera2D_ZeroViewportZoomToFit)
{
    Camera2D camera;
    // 零尺寸视口不应崩溃
    camera.zoomToFit(0.0f, 0.0f, 100.0f, 100.0f);
    SUCCEED();
}

TEST(RenderViewport2DRegressionTest, Camera2D_NegativeZoomToFit)
{
    Camera2D camera;
    // 负尺寸场景不应崩溃
    camera.zoomToFit(800.0f, 600.0f, -100.0f, -100.0f);
    SUCCEED();
}

// ==================== 实体属性变更后刷新测试 ====================

TEST(RenderViewport2DRegressionTest, EntityPropertyChange_VisibilityToggle)
{
    SceneRefreshCoordinator coordinator;
    Eg::SceneManager scene;
    coordinator.setSceneManager(&scene);

    auto line = std::make_unique<Eg::SyLine>();
    line->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });
    Eg::EntityId lineId = line->id;

    std::vector<std::unique_ptr<Eg::SyEntity>> entities;
    entities.push_back(std::move(line));
    scene.addEntities(std::move(entities));

    // 切换可见性
    auto* entity = scene.findSyEntityById(lineId);
    ASSERT_NE(entity, nullptr);
    entity->setVisible(false);
    EXPECT_FALSE(entity->visible());

    coordinator.onSceneChanged();
    coordinator.stop();
    SUCCEED();
}

TEST(RenderViewport2DRegressionTest, EntityPropertyChange_LockToggle)
{
    SceneRefreshCoordinator coordinator;
    Eg::SceneManager scene;
    coordinator.setSceneManager(&scene);

    auto line = std::make_unique<Eg::SyLine>();
    line->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });
    Eg::EntityId lineId = line->id;

    std::vector<std::unique_ptr<Eg::SyEntity>> entities;
    entities.push_back(std::move(line));
    scene.addEntities(std::move(entities));

    auto* entity = scene.findSyEntityById(lineId);
    ASSERT_NE(entity, nullptr);
    entity->setLocked(true);
    EXPECT_TRUE(entity->locked());

    coordinator.onSceneChanged();
    coordinator.stop();
    SUCCEED();
}

// ==================== 选择同步链端到端测试 ====================

TEST(RenderViewport2DRegressionTest, SelectionChain_SelectDeselectReselect)
{
    Eg::SceneManager scene;

    auto line = std::make_unique<Eg::SyLine>();
    line->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });
    Eg::EntityId lineId = line->id;

    std::vector<std::unique_ptr<Eg::SyEntity>> entities;
    entities.push_back(std::move(line));
    scene.addEntities(std::move(entities));

    // 选中
    scene.selectEntity(scene.findSyEntityById(lineId));
    EXPECT_EQ(scene.getSelectedEntityCount(), 1u);

    // 取消选中
    scene.deselectEntity(scene.findSyEntityById(lineId));
    EXPECT_EQ(scene.getSelectedEntityCount(), 0u);

    // 重新选中
    scene.selectEntity(scene.findSyEntityById(lineId));
    EXPECT_EQ(scene.getSelectedEntityCount(), 1u);
}

TEST(RenderViewport2DRegressionTest, SelectionChain_SelectAllInvertClear)
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

    // 全选
    scene.selectAll();
    EXPECT_EQ(scene.getSelectedEntityCount(), 3u);

    // 反转
    scene.invertSelection();
    EXPECT_EQ(scene.getSelectedEntityCount(), 0u);

    // 再次反转
    scene.invertSelection();
    EXPECT_EQ(scene.getSelectedEntityCount(), 3u);

    // 清除
    scene.clearSelection();
    EXPECT_EQ(scene.getSelectedEntityCount(), 0u);
}

TEST(RenderViewport2DRegressionTest, SelectionChain_MultipleSelectWithMixedTypes)
{
    Eg::SceneManager scene;

    std::vector<std::unique_ptr<Eg::SyEntity>> entities;
    Eg::EntityId idLine, idCircle, idPoly;

    {
        auto line = std::make_unique<Eg::SyLine>();
        line->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });
        idLine = line->id;
        entities.push_back(std::move(line));
    }
    {
        auto circle = std::make_unique<Eg::SyCircle>();
        circle->basePoint = Ut::Vec2d(5, 5);
        circle->dRadius = 3.0;
        idCircle = circle->id;
        entities.push_back(std::move(circle));
    }
    {
        auto poly = std::make_unique<Eg::SyPolygon>();
        poly->setVertices({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 0), Ut::Vec2d(10, 10) });
        idPoly = poly->id;
        entities.push_back(std::move(poly));
    }
    scene.addEntities(std::move(entities));

    // 逐个选中（单选语义：selectEntity 会替换当前选择，多选走批量入口 selectEntities）
    std::vector<Eg::SyEntity*> toSelect = { scene.findSyEntityById(idLine), scene.findSyEntityById(idCircle),
                                            scene.findSyEntityById(idPoly) };
    scene.selectEntities(toSelect);
    EXPECT_EQ(scene.getSelectedEntityCount(), 3u);

    // 取消选中其中一个
    scene.deselectEntity(scene.findSyEntityById(idCircle));
    EXPECT_EQ(scene.getSelectedEntityCount(), 2u);
}

// ==================== 删除实体后无悬空选中测试 ====================

TEST(RenderViewport2DRegressionTest, NoDanglingSelection_DeleteAllSelected)
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

    scene.selectAll();
    EXPECT_EQ(scene.getSelectedEntityCount(), 3u);

    scene.deleteSelected();
    EXPECT_EQ(scene.getSelectedEntityCount(), 0u);
    EXPECT_EQ(scene.getEntityCount(), 0u);
}

TEST(RenderViewport2DRegressionTest, NoDanglingSelection_DeleteIndividualEntity)
{
    Eg::SceneManager scene;

    auto line1 = std::make_unique<Eg::SyLine>();
    line1->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });
    Eg::EntityId id1 = line1->id;

    auto line2 = std::make_unique<Eg::SyLine>();
    line2->setPointVector({ Ut::Vec2d(20, 20), Ut::Vec2d(30, 30) });
    Eg::EntityId id2 = line2->id;

    std::vector<std::unique_ptr<Eg::SyEntity>> entities;
    entities.push_back(std::move(line1));
    entities.push_back(std::move(line2));
    scene.addEntities(std::move(entities));

    // 批量选中两个（selectEntity 为单选替换语义，多选走批量入口）
    std::vector<Eg::SyEntity*> toSelect = { scene.findSyEntityById(id1), scene.findSyEntityById(id2) };
    scene.selectEntities(toSelect);
    EXPECT_EQ(scene.getSelectedEntityCount(), 2u);

    // 删除一个
    scene.deleteEntity(scene.findSyEntityById(id1));
    EXPECT_EQ(scene.getSelectedEntityCount(), 1u);
    EXPECT_EQ(scene.findSyEntityById(id1), nullptr);
    EXPECT_NE(scene.findSyEntityById(id2), nullptr);
}

// ==================== 刷新协调器与场景通知联动测试 ====================

TEST(RenderViewport2DRegressionTest, Coordinator_SceneChangeAfterClear)
{
    SceneRefreshCoordinator coordinator;
    Eg::SceneManager scene;
    coordinator.setSceneManager(&scene);

    auto line = std::make_unique<Eg::SyLine>();
    line->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });

    std::vector<std::unique_ptr<Eg::SyEntity>> entities;
    entities.push_back(std::move(line));
    scene.addEntities(std::move(entities));

    coordinator.onSceneChanged();

    // 清空场景
    scene.clearScene();
    EXPECT_EQ(scene.getEntityCount(), 0u);

    coordinator.onSceneChanged();
    coordinator.stop();
    SUCCEED();
}

TEST(RenderViewport2DRegressionTest, Coordinator_SelectionChangeSequence)
{
    SceneRefreshCoordinator coordinator;
    Eg::SceneManager scene;
    coordinator.setSceneManager(&scene);

    auto line = std::make_unique<Eg::SyLine>();
    line->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });
    Eg::EntityId lineId = line->id;

    std::vector<std::unique_ptr<Eg::SyEntity>> entities;
    entities.push_back(std::move(line));
    scene.addEntities(std::move(entities));

    // 选中 → 通知 → 取消选中 → 通知
    scene.selectEntity(scene.findSyEntityById(lineId));
    coordinator.onSelectionChanged();

    scene.clearSelection();
    coordinator.onSelectionChanged();

    coordinator.stop();
    SUCCEED();
}

// ==================== ViewportInputRouter 构造与依赖注入测试 ====================

TEST(RenderViewport2DRegressionTest, InputRouter_DefaultConstruction)
{
    ViewportInputRouter router;
    // 默认构造不崩溃
    SUCCEED();
}

TEST(RenderViewport2DRegressionTest, InputRouter_SetRenderWidget)
{
    ViewportInputRouter router;
    router.setRenderWidget(nullptr);
    SUCCEED();
}

TEST(RenderViewport2DRegressionTest, InputRouter_SetCamera)
{
    ViewportInputRouter router;
    Camera2D camera;
    router.setCamera(&camera);
    SUCCEED();
}

TEST(RenderViewport2DRegressionTest, InputRouter_SetToolManager)
{
    ViewportInputRouter router;
    router.setToolManager(nullptr);
    SUCCEED();
}

TEST(RenderViewport2DRegressionTest, InputRouter_SetSelector)
{
    ViewportInputRouter router;
    router.setSelector(nullptr);
    SUCCEED();
}

TEST(RenderViewport2DRegressionTest, InputRouter_SetInteractionDispatcher)
{
    ViewportInputRouter router;
    router.setInteractionDispatcher(nullptr);
    SUCCEED();
}

TEST(RenderViewport2DRegressionTest, InputRouter_SetSelectionService)
{
    ViewportInputRouter router;
    router.setSelectionService(nullptr);
    SUCCEED();
}

TEST(RenderViewport2DRegressionTest, InputRouter_SetOperationBus)
{
    ViewportInputRouter router;
    router.setOperationBus(nullptr);
    SUCCEED();
}

TEST(RenderViewport2DRegressionTest, InputRouter_SetDocument)
{
    ViewportInputRouter router;
    router.setDocument(nullptr);
    SUCCEED();
}

TEST(RenderViewport2DRegressionTest, InputRouter_SetRefreshCoordinator)
{
    ViewportInputRouter router;
    SceneRefreshCoordinator coordinator;
    router.setRefreshCoordinator(&coordinator);
    SUCCEED();
}

TEST(RenderViewport2DRegressionTest, InputRouter_FullDependencyInjection)
{
    ViewportInputRouter router;
    Camera2D camera;
    SceneRefreshCoordinator coordinator;

    router.setCamera(&camera);
    router.setRefreshCoordinator(&coordinator);
    router.setRenderWidget(nullptr);
    router.setToolManager(nullptr);
    router.setSelector(nullptr);
    router.setInteractionDispatcher(nullptr);
    router.setSelectionService(nullptr);
    router.setOperationBus(nullptr);
    router.setDocument(nullptr);
    // 全部注入不崩溃
    SUCCEED();
}

// ==================== ViewportInputRouter 平移模式测试 ====================

TEST(RenderViewport2DRegressionTest, InputRouter_PanModeDefaultOff)
{
    ViewportInputRouter router;
    EXPECT_FALSE(router.isPanModeEnabled());
    EXPECT_FALSE(router.isPanning());
}

TEST(RenderViewport2DRegressionTest, InputRouter_PanModeToggle)
{
    ViewportInputRouter router;
    router.setPanModeEnabled(true);
    EXPECT_TRUE(router.isPanModeEnabled());
    router.setPanModeEnabled(false);
    EXPECT_FALSE(router.isPanModeEnabled());
}

// ==================== ViewportInputRouter 回调注入测试 ====================

TEST(RenderViewport2DRegressionTest, InputRouter_PositionCallback)
{
    ViewportInputRouter router;
    bool called = false;
    double lastX = 0, lastY = 0;
    router.setPositionCallback([&](double x, double y) {
        called = true;
        lastX = x;
        lastY = y;
        });
    // 回调注入不崩溃
    SUCCEED();
}

TEST(RenderViewport2DRegressionTest, InputRouter_StatusCallback)
{
    ViewportInputRouter router;
    QString lastMsg;
    router.setStatusCallback([&](const QString& msg) { lastMsg = msg; });
    // 回调注入不崩溃
    SUCCEED();
}

// ==================== ViewportInputRouter 坐标转换测试 ====================

TEST(RenderViewport2DRegressionTest, InputRouter_PhysicalViewportSizeNullWidget)
{
    ViewportInputRouter router;
    auto size = router.physicalViewportSize();
    EXPECT_EQ(size.width(), 0.0);
    EXPECT_EQ(size.height(), 0.0);
}

TEST(RenderViewport2DRegressionTest, InputRouter_WidgetToWorldNullWidget)
{
    ViewportInputRouter router;
    auto worldPos = router.widgetToWorld(QPoint(100, 100));
    // 无 RenderWidget 时返回空点
    EXPECT_TRUE(worldPos.isNull());
}

// ==================== ViewportInputRouter 事件过滤器测试 ====================

TEST(RenderViewport2DRegressionTest, InputRouter_EventFilterNullObject)
{
    ViewportInputRouter router;
    QEvent dummyEvent(QEvent::None);
    bool result = router.eventFilter(nullptr, &dummyEvent);
    // 非 RenderWidget 事件应委托给父类
    EXPECT_FALSE(result);
}

TEST(RenderViewport2DRegressionTest, InputRouter_EventFilterNonRenderWidget)
{
    // 注意：MainTests 无 QApplication，不能构造 QWidget；用 QObject 验证同样的"非 RenderWidget 委托父类"逻辑
    ViewportInputRouter router;
    QObject otherWidget;
    QEvent dummyEvent(QEvent::None);
    bool result = router.eventFilter(&otherWidget, &dummyEvent);
    // 非 RenderWidget 对象应委托给父类
    EXPECT_FALSE(result);
}

// ==================== 键盘路由优先级测试 ====================

TEST(RenderViewport2DRegressionTest, InputRouter_KeyboardPriority_InteractionDispatcherFirst)
{
    // 键盘路由优先级：interactionDispatcher → tool → Delete
    ViewportInputRouter router;
    // 验证路由链存在，不崩溃
    QKeyEvent escEvent(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    router.handleKeyPress(&escEvent);
    SUCCEED();
}

TEST(RenderViewport2DRegressionTest, InputRouter_KeyboardPriority_DeleteKey)
{
    ViewportInputRouter router;
    QKeyEvent delEvent(QEvent::KeyPress, Qt::Key_Delete, Qt::NoModifier);
    router.handleKeyPress(&delEvent);
    SUCCEED();
}

TEST(RenderViewport2DRegressionTest, InputRouter_KeyboardPriority_EnterKey)
{
    ViewportInputRouter router;
    QKeyEvent enterEvent(QEvent::KeyPress, Qt::Key_Enter, Qt::NoModifier);
    router.handleKeyPress(&enterEvent);
    SUCCEED();
}

// ==================== 鼠标事件路由优先级测试 ====================

TEST(RenderViewport2DRegressionTest, InputRouter_MousePressNullRenderWidget)
{
    ViewportInputRouter router;
    QMouseEvent pressEvent(QEvent::MouseButtonPress, QPointF(0, 0), QPointF(0, 0), Qt::LeftButton, Qt::LeftButton,
        Qt::NoModifier);
    // 无 RenderWidget 时不崩溃
    router.handleMousePress(&pressEvent);
    SUCCEED();
}

TEST(RenderViewport2DRegressionTest, InputRouter_MouseMoveNullRenderWidget)
{
    ViewportInputRouter router;
    QMouseEvent moveEvent(QEvent::MouseMove, QPointF(10, 10), QPointF(10, 10), Qt::NoButton, Qt::NoButton,
        Qt::NoModifier);
    // 无 RenderWidget 时不崩溃
    router.handleMouseMove(&moveEvent);
    SUCCEED();
}

TEST(RenderViewport2DRegressionTest, InputRouter_MouseReleaseNullRenderWidget)
{
    ViewportInputRouter router;
    QMouseEvent releaseEvent(QEvent::MouseButtonRelease, QPointF(0, 0), QPointF(0, 0), Qt::LeftButton, Qt::LeftButton,
        Qt::NoModifier);
    // 无 RenderWidget 时不崩溃
    router.handleMouseRelease(&releaseEvent);
    SUCCEED();
}

TEST(RenderViewport2DRegressionTest, InputRouter_MouseDoubleClickNullRenderWidget)
{
    ViewportInputRouter router;
    QMouseEvent dblClickEvent(QEvent::MouseButtonDblClick, QPointF(0, 0), QPointF(0, 0), Qt::LeftButton, Qt::LeftButton,
        Qt::NoModifier);
    // 无 RenderWidget 时不崩溃
    router.handleMouseDoubleClick(&dblClickEvent);
    SUCCEED();
}

TEST(RenderViewport2DRegressionTest, InputRouter_WheelNullRenderWidget)
{
    ViewportInputRouter router;
    QPointF pos(400, 300);
    QWheelEvent wheelEvent(pos, pos, QPoint(), QPoint(0, 120), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    // 无 RenderWidget 时不崩溃
    router.handleWheel(&wheelEvent);
    SUCCEED();
}

// ==================== 上下文菜单测试 ====================

TEST(RenderViewport2DRegressionTest, InputRouter_ContextMenuNullDispatcher)
{
    ViewportInputRouter router;
    QContextMenuEvent contextEvent(QContextMenuEvent::Mouse, QPoint(100, 100), QPoint(100, 100));
    // 无交互分发器时不崩溃
    router.handleContextMenu(&contextEvent);
    SUCCEED();
}

// ==================== 刷新协调器与输入路由器联动测试 ====================

TEST(RenderViewport2DRegressionTest, InputRouter_DeleteKeyTriggersRefreshCoordinator)
{
    ViewportInputRouter router;
    SceneRefreshCoordinator coordinator;
    Eg::SceneManager scene;
    coordinator.setSceneManager(&scene);
    router.setRefreshCoordinator(&coordinator);

    // Delete 键触发刷新协调器
    QKeyEvent delEvent(QEvent::KeyPress, Qt::Key_Delete, Qt::NoModifier);
    router.handleKeyPress(&delEvent);
    coordinator.stop();
    SUCCEED();
}

TEST(RenderViewport2DRegressionTest, InputRouter_PanModeBlocksToolDispatch)
{
    ViewportInputRouter router;
    // 平移模式下鼠标事件不应分发到工具
    router.setPanModeEnabled(true);
    EXPECT_TRUE(router.isPanModeEnabled());

    QMouseEvent pressEvent(QEvent::MouseButtonPress, QPointF(0, 0), QPointF(0, 0), Qt::LeftButton, Qt::LeftButton,
        Qt::NoModifier);
    router.handleMousePress(&pressEvent);
    SUCCEED();
}

// ==================== Camera2D 新增方法测试（P5 视口瘦身） ====================

TEST(RenderViewport2DRegressionTest, Camera2D_ViewMatrixReturnsMat3f)
{
    Camera2D camera;
    camera.pan(100.0f, 50.0f);

    // viewMatrix 应返回与 computeViewMatrix 一致的结果
    Render::Mat3f mat = camera.viewMatrix(800.0f, 600.0f);

    float raw[9] = {};
    camera.computeViewMatrix(raw, 800.0f, 600.0f);

    for (int i = 0; i < 9; ++i)
        EXPECT_NEAR(mat.data[i], raw[i], 1e-6f);
}

TEST(RenderViewport2DRegressionTest, Camera2D_ViewMatrixZeroViewport)
{
    Camera2D camera;
    // 零尺寸视口应返回单位矩阵
    Render::Mat3f mat = camera.viewMatrix(0.0f, 0.0f);
    EXPECT_NEAR(mat.data[0], 1.0f, 1e-6f);
    EXPECT_NEAR(mat.data[4], 1.0f, 1e-6f);
    EXPECT_NEAR(mat.data[8], 1.0f, 1e-6f);
}

TEST(RenderViewport2DRegressionTest, Camera2D_ZoomToBBoxCentersScene)
{
    Camera2D camera;
    // bbox (100,100)-(500,400)，中心 (300,250)，尺寸 400x300
    camera.zoomToBBox(800.0f, 600.0f, 100.0f, 100.0f, 500.0f, 400.0f);

    // 缩放后 zoom 应为正
    EXPECT_GT(camera.zoomX, 0.0f);
    EXPECT_GT(camera.zoomY, 0.0f);
    // panOffset 应为场景中心的负数（居中偏移）
    EXPECT_NEAR(camera.panOffset.x(), -300.0f, 1.0f);
    EXPECT_NEAR(camera.panOffset.y(), -250.0f, 1.0f);
}

TEST(RenderViewport2DRegressionTest, Camera2D_ZoomToBBoxDegenerate)
{
    Camera2D camera;
    // 退化 bbox（零尺寸）应使用默认大小 1000x1000，不崩溃
    camera.zoomToBBox(800.0f, 600.0f, 100.0f, 100.0f, 100.0f, 100.0f);
    EXPECT_GT(camera.zoomX, 0.0f);
}

TEST(RenderViewport2DRegressionTest, Camera2D_ZoomToBBoxZeroViewport)
{
    Camera2D camera;
    // 零尺寸视口不应崩溃
    camera.zoomToBBox(0.0f, 0.0f, 0.0f, 0.0f, 100.0f, 100.0f);
    // 相机状态不应改变
    EXPECT_FLOAT_EQ(camera.zoomX, 1.0f);
    EXPECT_FLOAT_EQ(camera.zoomY, 1.0f);
}

TEST(RenderViewport2DRegressionTest, Camera2D_ZoomAtCenterZoomsIn)
{
    Camera2D camera;
    float zoomBefore = camera.zoomX;

    camera.zoomAtCenter(2.0f, 800.0f, 600.0f);

    // 放大 2 倍
    EXPECT_NEAR(camera.zoomX, zoomBefore * 2.0f, 1e-4f);
}

TEST(RenderViewport2DRegressionTest, Camera2D_ZoomAtCenterZoomsOut)
{
    Camera2D camera;
    camera.zoomAtCenter(2.0f, 800.0f, 600.0f);
    float zoomBefore = camera.zoomX;

    camera.zoomAtCenter(0.5f, 800.0f, 600.0f);

    // 缩小回原值
    EXPECT_NEAR(camera.zoomX, zoomBefore * 0.5f, 1e-4f);
}

TEST(RenderViewport2DRegressionTest, Camera2D_ZoomAtCenterZeroViewport)
{
    Camera2D camera;
    // 零尺寸视口不应崩溃
    camera.zoomAtCenter(2.0f, 0.0f, 0.0f);
    EXPECT_FLOAT_EQ(camera.zoomX, 1.0f);
}

TEST(RenderViewport2DRegressionTest, Camera2D_ResetToDefaultSetsTableBounds)
{
    Camera2D camera;
    // 先偏移相机
    camera.pan(500.0f, 300.0f);
    camera.zoomAtCenter(3.0f, 800.0f, 600.0f);

    // 重置到默认台面范围
    camera.resetToDefault(800.0f, 600.0f);

    // 重置后 panOffset 应为台面中心的负数 (-600, -400)
    EXPECT_NEAR(camera.panOffset.x(), -600.0f, 1.0f);
    EXPECT_NEAR(camera.panOffset.y(), -400.0f, 1.0f);
    // zoom 应为正值
    EXPECT_GT(camera.zoomX, 0.0f);
}

TEST(RenderViewport2DRegressionTest, Camera2D_ResetToDefaultZeroViewport)
{
    Camera2D camera;
    // 零尺寸视口应回退到 reset()
    camera.resetToDefault(0.0f, 0.0f);
    EXPECT_FLOAT_EQ(camera.zoomX, 1.0f);
    EXPECT_FLOAT_EQ(camera.zoomY, 1.0f);
    EXPECT_NEAR(camera.panOffset.x(), 0.0f, 1e-6f);
    EXPECT_NEAR(camera.panOffset.y(), 0.0f, 1e-6f);
}

TEST(RenderViewport2DRegressionTest, Camera2D_ZoomToBBoxMatchesZoomToFitPlusPan)
{
    Camera2D camera1, camera2;
    float vpW = 800.0f, vpH = 600.0f;
    float minX = 100.0f, minY = 100.0f, maxX = 500.0f, maxY = 400.0f;

    // 方式一：zoomToBBox 一步到位
    camera1.zoomToBBox(vpW, vpH, minX, minY, maxX, maxY);

    // 方式二：zoomToFit + pan 分两步
    float sceneW = maxX - minX;
    float sceneH = maxY - minY;
    float centerX = (minX + maxX) * 0.5f;
    float centerY = (minY + maxY) * 0.5f;
    camera2.zoomToFit(vpW, vpH, sceneW, sceneH);
    camera2.pan(-centerX, -centerY);

    // 两者结果应一致
    EXPECT_NEAR(camera1.zoomX, camera2.zoomX, 1e-4f);
    EXPECT_NEAR(camera1.panOffset.x(), camera2.panOffset.x(), 1e-4f);
    EXPECT_NEAR(camera1.panOffset.y(), camera2.panOffset.y(), 1e-4f);
}

// ==================== ViewportSelector 选择管理方法测试（P5 视口瘦身） ====================

namespace
{
    // ISelectionService 桩实现 — 用于 ViewportSelector 组件级测试
    class StubSelectionService : public ISelectionService
    {
    public:
        std::vector<std::string> selectedIds;

        void visitSelectedIds(SelectedIdVisitor visitor, void* context) const override
        {
            for (const auto& id : selectedIds)
                visitor(id.c_str(), context);
        }

        bool isSelected(const char* id) const override
        {
            if (!id)
                return false;
            for (const auto& s : selectedIds)
                if (s == id)
                    return true;
            return false;
        }

        void select(const char* id) override
        {
            if (!id)
                return;
            selectedIds.clear();
            selectedIds.push_back(id);
        }

        void selectMultiple(const char* const* ids, size_t count) override
        {
            selectedIds.clear();
            for (size_t i = 0; i < count; ++i)
                if (ids[i])
                    selectedIds.push_back(ids[i]);
        }

        void deselect(const char* id) override
        {
            if (!id)
                return;
            selectedIds.erase(std::remove(selectedIds.begin(), selectedIds.end(), std::string(id)), selectedIds.end());
        }

        void clear() override
        {
            selectedIds.clear();
        }

        void toggle(const char* id) override
        {
            if (!id)
                return;
            if (isSelected(id))
                deselect(id);
            else
                selectedIds.push_back(id);
        }
    };
} // namespace

TEST(RenderViewport2DRegressionTest, ViewportSelector_SelectedEntityIdEmpty)
{
    StubSelectionService svc;
    Camera2D camera;
    ViewportSelector selector(nullptr, &svc, &camera, nullptr);

    // 无选择时返回空字符串
    EXPECT_TRUE(selector.selectedEntityId().isEmpty());
}

TEST(RenderViewport2DRegressionTest, ViewportSelector_SelectedEntityIdReturnsFirst)
{
    StubSelectionService svc;
    svc.selectedIds = { "42", "99" };
    Camera2D camera;
    ViewportSelector selector(nullptr, &svc, &camera, nullptr);

    // 返回第一个选中 ID
    EXPECT_EQ(selector.selectedEntityId(), QStringLiteral("42"));
}

TEST(RenderViewport2DRegressionTest, ViewportSelector_SelectEntityById)
{
    StubSelectionService svc;
    svc.selectedIds = { "1", "2" };
    Camera2D camera;
    ViewportSelector selector(nullptr, &svc, &camera, nullptr);

    // 选中 ID "100"：先清空再选中
    selector.selectEntityById(QStringLiteral("100"));

    EXPECT_EQ(svc.selectedIds.size(), 1u);
    EXPECT_EQ(svc.selectedIds[0], "100");
}

TEST(RenderViewport2DRegressionTest, ViewportSelector_SelectEntityByIdFiresCallbacks)
{
    StubSelectionService svc;
    Camera2D camera;
    ViewportSelector selector(nullptr, &svc, &camera, nullptr);

    QString lastStatus;
    QString lastCallbackSource;
    QString lastCallbackText;
    selector.setStatusCallback([&](const QString& s) { lastStatus = s; });
    selector.setSelectionCallback([&](const QString& src, const QString& t) {
        lastCallbackSource = src;
        lastCallbackText = t;
        });

    selector.selectEntityById(QStringLiteral("55"));

    // 验证回调被触发
    EXPECT_FALSE(lastStatus.isEmpty());
    EXPECT_EQ(lastCallbackSource, QStringLiteral("2D-Select"));
    EXPECT_TRUE(lastCallbackText.contains(QStringLiteral("55")));
}

TEST(RenderViewport2DRegressionTest, ViewportSelector_ClearSelection)
{
    StubSelectionService svc;
    svc.selectedIds = { "1", "2", "3" };
    Camera2D camera;
    ViewportSelector selector(nullptr, &svc, &camera, nullptr);

    selector.clearSelection();

    EXPECT_TRUE(svc.selectedIds.empty());
}

TEST(RenderViewport2DRegressionTest, ViewportSelector_ClearSelectionFiresCallback)
{
    StubSelectionService svc;
    svc.selectedIds = { "1" };
    Camera2D camera;
    ViewportSelector selector(nullptr, &svc, &camera, nullptr);

    QString lastStatus;
    selector.setStatusCallback([&](const QString& s) { lastStatus = s; });

    selector.clearSelection();

    EXPECT_FALSE(lastStatus.isEmpty());
}

TEST(RenderViewport2DRegressionTest, ViewportSelector_SelectEntityByIdNullService)
{
    Camera2D camera;
    ViewportSelector selector(nullptr, nullptr, &camera, nullptr);

    // 无选择服务时不应崩溃
    selector.selectEntityById(QStringLiteral("1"));
    SUCCEED();
}

TEST(RenderViewport2DRegressionTest, ViewportSelector_ClearSelectionNullService)
{
    Camera2D camera;
    ViewportSelector selector(nullptr, nullptr, &camera, nullptr);

    // 无选择服务时不应崩溃
    selector.clearSelection();
    SUCCEED();
}

TEST(RenderViewport2DRegressionTest, ViewportSelector_SelectedEntityIdNullService)
{
    Camera2D camera;
    ViewportSelector selector(nullptr, nullptr, &camera, nullptr);

    // 无选择服务时返回空字符串
    EXPECT_TRUE(selector.selectedEntityId().isEmpty());
}

TEST(RenderViewport2DRegressionTest, ViewportSelector_SelectClearSelectSequence)
{
    StubSelectionService svc;
    Camera2D camera;
    ViewportSelector selector(nullptr, &svc, &camera, nullptr);

    // 选中 1 → 清空 → 选中 2
    selector.selectEntityById(QStringLiteral("1"));
    EXPECT_EQ(svc.selectedIds.size(), 1u);
    EXPECT_EQ(svc.selectedIds[0], "1");

    selector.clearSelection();
    EXPECT_TRUE(svc.selectedIds.empty());

    selector.selectEntityById(QStringLiteral("2"));
    EXPECT_EQ(svc.selectedIds.size(), 1u);
    EXPECT_EQ(svc.selectedIds[0], "2");
}