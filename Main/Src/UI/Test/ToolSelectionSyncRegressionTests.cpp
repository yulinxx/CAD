/**
 * @file ToolSelectionSyncRegressionTests.cpp
 * @brief 工具初始化链与选择同步回归测试 — 覆盖 ToolManager/SelectTool/选择状态同步
 *
 * 测试范围：
 *  - ToolManager 构造/注册/初始化/工具切换
 *  - SelectTool 生命周期/选择同步/清除选择
 *  - 选择状态在增删实体后的同步
 *  - 工具回调链（entityCallback/switchToolCallback）
 *  - 删除后无悬空选中状态
 *
 * P5 测试覆盖扩展 (2026-07-30)
 */

#include <gtest/gtest.h>

#include "UI/DrawTools/ToolManager.h"
#include "UI/DrawTools/ToolContext.h"
#include "UI/DrawTools/ITool.h"
#include "UI/DrawTools/SelectTool.h"
#include "Engine2D/Core/SceneManager.h"
#include "Engine2D/SyEntity/SyLine.h"
#include "Engine2D/SyEntity/SyCircle.h"
#include "Engine2D/SyEntity/SyPolygon.h"
#include "Engine2D/SyEntity/SyImage.h"
#include "Engine2D/Edit/SceneEditService.h"
#include "Engine2D/Edit/UndoRedoManager.h"
#include "Engine2D/Geometry/Geo2DQuery.h"
#include "Engine2D/Geo/GeometryContext.h"
#include "UI/DrawTools/SelectionGizmo.h"
#include "UI/Services/SelectionService.h"

#include <QMouseEvent>
#include <QPointF>

#include <memory>
#include <vector>

// ==================== ToolManager 构造与生命周期 ====================

TEST(ToolSelectionSyncRegressionTest, ToolManager_DefaultConstruction)
{
    ToolManager tm;
    EXPECT_FALSE(tm.isInitialized());
    EXPECT_EQ(tm.getActiveTool(), nullptr);
    EXPECT_EQ(tm.getActiveToolName(), QString());
    EXPECT_EQ(tm.getToolCount(), 0u);
}

TEST(ToolSelectionSyncRegressionTest, ToolManager_DestructorWithoutInit)
{
    {
        ToolManager tm;
    }
    SUCCEED();
}

// ==================== ToolManager 工具注册 ====================

TEST(ToolSelectionSyncRegressionTest, ToolManager_RegisterSelectTool)
{
    ToolManager tm;
    tm.registerTool<SelectTool>("SelectTool");
    EXPECT_EQ(tm.getToolCount(), 0u);  // 注册后不会立即实例化，需 initializeTools
}

TEST(ToolSelectionSyncRegressionTest, ToolManager_RegisterMultipleTools)
{
    ToolManager tm;
    tm.registerTool<SelectTool>("SelectTool");
    // 重复注册同一名称会添加多个工厂，但不会实例化
    SUCCEED();
}

// ==================== ToolManager 初始化 ====================

TEST(ToolSelectionSyncRegressionTest, ToolManager_InitializeTools)
{
    ToolManager tm;
    tm.registerTool<SelectTool>("SelectTool");

    Eg::SceneManager scene;
    ToolContext ctx;
    ctx.sceneManager = &scene;

    tm.initializeTools(ctx);
    EXPECT_TRUE(tm.isInitialized());
    EXPECT_EQ(tm.getToolCount(), 1u);
    EXPECT_NE(tm.getTool("SelectTool"), nullptr);
}

TEST(ToolSelectionSyncRegressionTest, ToolManager_InitializeWithoutRegister)
{
    ToolManager tm;
    Eg::SceneManager scene;
    ToolContext ctx;
    ctx.sceneManager = &scene;

    tm.initializeTools(ctx);
    // 未注册任何工具工厂时保持 isInitialized()==false（实现有意为之，见 ToolManager::initializeTools 注释）
    EXPECT_FALSE(tm.isInitialized());
    EXPECT_EQ(tm.getToolCount(), 0u);
}

TEST(ToolSelectionSyncRegressionTest, ToolManager_DoubleInitialize)
{
    ToolManager tm;
    tm.registerTool<SelectTool>("SelectTool");

    Eg::SceneManager scene;
    ToolContext ctx;
    ctx.sceneManager = &scene;

    tm.initializeTools(ctx);
    tm.initializeTools(ctx);  // 二次初始化不应崩溃
    EXPECT_TRUE(tm.isInitialized());
}

// ==================== ToolManager 工具切换 ====================

TEST(ToolSelectionSyncRegressionTest, ToolManager_SetActiveTool)
{
    ToolManager tm;
    tm.registerTool<SelectTool>("SelectTool");

    Eg::SceneManager scene;
    ToolContext ctx;
    ctx.sceneManager = &scene;

    tm.initializeTools(ctx);
    EXPECT_TRUE(tm.setActiveTool("SelectTool"));
    EXPECT_EQ(tm.getActiveToolName(), "SelectTool");
    EXPECT_NE(tm.getActiveTool(), nullptr);
}

TEST(ToolSelectionSyncRegressionTest, ToolManager_SetActiveTool_Unknown)
{
    ToolManager tm;
    tm.registerTool<SelectTool>("SelectTool");

    Eg::SceneManager scene;
    ToolContext ctx;
    ctx.sceneManager = &scene;

    tm.initializeTools(ctx);
    EXPECT_FALSE(tm.setActiveTool("UnknownTool"));
    // 未知工具不切换，保持当前活动工具（initializeTools 已自动激活 SelectTool）
    EXPECT_EQ(tm.getActiveToolName(), "SelectTool");
    EXPECT_NE(tm.getActiveTool(), nullptr);
}

TEST(ToolSelectionSyncRegressionTest, ToolManager_SwitchBetweenTools)
{
    ToolManager tm;
    tm.registerTool<SelectTool>("SelectTool");

    Eg::SceneManager scene;
    ToolContext ctx;
    ctx.sceneManager = &scene;

    tm.initializeTools(ctx);

    tm.setActiveTool("SelectTool");
    EXPECT_EQ(tm.getActiveToolName(), "SelectTool");

    // cancelCurrentTool 只取消当前操作，不退出当前工具（与 BaseTool::cancel 契约一致）
    tm.cancelCurrentTool();
    EXPECT_NE(tm.getActiveTool(), nullptr);
    EXPECT_EQ(tm.getActiveToolName(), "SelectTool");
}

TEST(ToolSelectionSyncRegressionTest, ToolManager_CancelCurrentTool)
{
    ToolManager tm;
    tm.registerTool<SelectTool>("SelectTool");

    Eg::SceneManager scene;
    ToolContext ctx;
    ctx.sceneManager = &scene;

    tm.initializeTools(ctx);
    tm.setActiveTool("SelectTool");
    EXPECT_NE(tm.getActiveTool(), nullptr);

    tm.cancelCurrentTool();
    // 取消操作不退出工具
    EXPECT_NE(tm.getActiveTool(), nullptr);
}

TEST(ToolSelectionSyncRegressionTest, ToolManager_CancelCurrentTool_NoActive)
{
    ToolManager tm;
    tm.registerTool<SelectTool>("SelectTool");

    Eg::SceneManager scene;
    ToolContext ctx;
    ctx.sceneManager = &scene;

    tm.initializeTools(ctx);
    tm.cancelCurrentTool();  // 无活动工具时不崩溃
    SUCCEED();
}

// ==================== ToolManager 回调链 ====================

TEST(ToolSelectionSyncRegressionTest, ToolManager_EntityCallback)
{
    ToolManager tm;
    tm.registerTool<SelectTool>("SelectTool");

    Eg::SceneManager scene;
    ToolContext ctx;
    ctx.sceneManager = &scene;

    tm.initializeTools(ctx);

    bool entityCallbackSet = false;
    tm.setEntityCallbackForAllTools([&entityCallbackSet](Eg::SyEntity*) {
        entityCallbackSet = true;
    });
    // 回调已设置，不应崩溃
    SUCCEED();
}

TEST(ToolSelectionSyncRegressionTest, ToolManager_SwitchToolCallback)
{
    ToolManager tm;
    tm.registerTool<SelectTool>("SelectTool");

    Eg::SceneManager scene;
    ToolContext ctx;
    ctx.sceneManager = &scene;

    tm.initializeTools(ctx);

    QString lastSwitch;
    tm.setSwitchToolCallbackForAllTools([&lastSwitch](const QString& name) {
        lastSwitch = name;
    });
    // 回调已设置，不应崩溃
    SUCCEED();
}

TEST(ToolSelectionSyncRegressionTest, ToolManager_NullCallbackSet)
{
    ToolManager tm;
    tm.registerTool<SelectTool>("SelectTool");

    Eg::SceneManager scene;
    ToolContext ctx;
    ctx.sceneManager = &scene;

    tm.initializeTools(ctx);

    // 设置空回调不应崩溃
    tm.setEntityCallbackForAllTools(nullptr);
    tm.setSwitchToolCallbackForAllTools(nullptr);
    SUCCEED();
}

// ==================== SelectTool 生命周期 ====================

TEST(SelectToolRegressionTest, Construction)
{
    SelectTool tool;
    EXPECT_FALSE(tool.hasSelectedEntities());
    EXPECT_TRUE(tool.selected().empty());
}

TEST(SelectToolRegressionTest, Initialize)
{
    SelectTool tool;
    Eg::SceneManager scene;
    ToolContext ctx;
    ctx.sceneManager = &scene;

    tool.initialize(ctx);
    EXPECT_FALSE(tool.hasSelectedEntities());
}

TEST(SelectToolRegressionTest, ClearSelection_Empty)
{
    SelectTool tool;
    tool.clearSelection();
    EXPECT_FALSE(tool.hasSelectedEntities());
}

TEST(SelectToolRegressionTest, OnActivate_OnDeactivate)
{
    SelectTool tool;
    Eg::SceneManager scene;
    ToolContext ctx;
    ctx.sceneManager = &scene;

    tool.initialize(ctx);
    tool.onActivate();
    tool.onDeactivate();
    SUCCEED();
}

TEST(SelectToolRegressionTest, Cancel)
{
    SelectTool tool;
    Eg::SceneManager scene;
    ToolContext ctx;
    ctx.sceneManager = &scene;

    tool.initialize(ctx);
    tool.cancel();
    EXPECT_FALSE(tool.hasSelectedEntities());
}

// ==================== SelectTool 选择同步 ====================

TEST(SelectToolRegressionTest, SyncSelectionFromScene_Empty)
{
    SelectTool tool;
    Eg::SceneManager scene;
    ToolContext ctx;
    ctx.sceneManager = &scene;

    tool.initialize(ctx);
    tool.syncSelectionFromScene();
    EXPECT_FALSE(tool.hasSelectedEntities());
}

TEST(SelectToolRegressionTest, SyncSelectionFromScene_WithEntities)
{
    SelectTool tool;
    Eg::SceneManager scene;

    auto line = std::make_unique<Eg::SyLine>();
    line->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });
    Eg::EntityId lineId = line->id;

    std::vector<std::unique_ptr<Eg::SyEntity>> entities;
    entities.push_back(std::move(line));
    scene.addEntities(std::move(entities));

    scene.selectEntity(scene.findSyEntityById(lineId));
    EXPECT_EQ(scene.getSelectedEntityCount(), 1u);

    ToolContext ctx;
    ctx.sceneManager = &scene;
    tool.initialize(ctx);
    tool.syncSelectionFromScene();
    EXPECT_TRUE(tool.hasSelectedEntities());
}

// ==================== 选择状态在增删实体后的同步 ====================

TEST(SelectionSyncExtendedTest, AddEntity_SelectionStateUnchanged)
{
    Eg::SceneManager scene;

    auto line1 = std::make_unique<Eg::SyLine>();
    line1->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });
    Eg::EntityId id1 = line1->id;

    std::vector<std::unique_ptr<Eg::SyEntity>> entities;
    entities.push_back(std::move(line1));
    scene.addEntities(std::move(entities));

    scene.selectEntity(scene.findSyEntityById(id1));
    EXPECT_EQ(scene.getSelectedEntityCount(), 1u);

    // 添加新实体不影响已有选中
    auto line2 = std::make_unique<Eg::SyLine>();
    line2->setPointVector({ Ut::Vec2d(20, 20), Ut::Vec2d(30, 30) });
    std::vector<std::unique_ptr<Eg::SyEntity>> entities2;
    entities2.push_back(std::move(line2));
    scene.addEntities(std::move(entities2));

    EXPECT_EQ(scene.getSelectedEntityCount(), 1u);
    EXPECT_NE(scene.findSyEntityById(id1), nullptr);
    EXPECT_TRUE(scene.findSyEntityById(id1)->selected());
}

TEST(SelectionSyncExtendedTest, DeleteUnselectedEntity_SelectionUnchanged)
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

    scene.selectEntity(scene.findSyEntityById(id1));
    EXPECT_EQ(scene.getSelectedEntityCount(), 1u);

    // 删除未选中的实体
    scene.deleteEntity(scene.findSyEntityById(id2));
    EXPECT_EQ(scene.getSelectedEntityCount(), 1u);
    EXPECT_TRUE(scene.findSyEntityById(id1)->selected());
}

TEST(SelectionSyncExtendedTest, InvertSelection_AllToggled)
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

    EXPECT_EQ(scene.getSelectedEntityCount(), 0u);

    // 反转选择：从0到全部
    scene.invertSelection();
    EXPECT_EQ(scene.getSelectedEntityCount(), 3u);

    // 再次反转：从全部到0
    scene.invertSelection();
    EXPECT_EQ(scene.getSelectedEntityCount(), 0u);
}

TEST(SelectionSyncExtendedTest, SelectAllThenDeselectOne)
{
    Eg::SceneManager scene;

    std::vector<Eg::EntityId> ids;
    for (int i = 0; i < 3; ++i)
    {
        auto line = std::make_unique<Eg::SyLine>();
        line->setPointVector({ Ut::Vec2d(0.0, 0.0), Ut::Vec2d(10.0 + i, 10.0 + i) });
        ids.push_back(line->id);
        std::vector<std::unique_ptr<Eg::SyEntity>> entities;
        entities.push_back(std::move(line));
        scene.addEntities(std::move(entities));
    }

    scene.selectAll();
    EXPECT_EQ(scene.getSelectedEntityCount(), 3u);

    scene.deselectEntity(scene.findSyEntityById(ids[0]));
    EXPECT_EQ(scene.getSelectedEntityCount(), 2u);
}

// ==================== 删除后无悬空选中状态 ====================

TEST(SelectionSyncExtendedTest, DeleteAllEntities_ClearsSelection)
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

    scene.clearScene();
    EXPECT_EQ(scene.getEntityCount(), 0u);
    EXPECT_EQ(scene.getSelectedEntityCount(), 0u);
}

TEST(SelectionSyncExtendedTest, ClearSceneThenSelect_NoCrash)
{
    Eg::SceneManager scene;

    auto line = std::make_unique<Eg::SyLine>();
    line->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });
    std::vector<std::unique_ptr<Eg::SyEntity>> entities;
    entities.push_back(std::move(line));
    scene.addEntities(std::move(entities));

    scene.clearScene();
    scene.selectAll();        // 空场景全选不崩溃
    scene.invertSelection();  // 空场景反转不崩溃
    scene.deleteSelected();   // 空场景删除选中不崩溃
    SUCCEED();
}

// ==================== 混合实体类型选择 ====================

TEST(SelectionSyncExtendedTest, SelectMixedEntityTypes)
{
    Eg::SceneManager scene;

    auto line = std::make_unique<Eg::SyLine>();
    line->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });
    Eg::EntityId lineId = line->id;

    auto circle = std::make_unique<Eg::SyCircle>();
    circle->basePoint = Ut::Vec2d(50, 50);
    circle->dRadius = 20.0;
    Eg::EntityId circleId = circle->id;

    auto poly = std::make_unique<Eg::SyPolygon>();
    poly->setVertices({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 0), Ut::Vec2d(10, 10) });
    Eg::EntityId polyId = poly->id;

    std::vector<std::unique_ptr<Eg::SyEntity>> entities;
    entities.push_back(std::move(line));
    entities.push_back(std::move(circle));
    entities.push_back(std::move(poly));
    scene.addEntities(std::move(entities));

    scene.selectAll();
    EXPECT_EQ(scene.getSelectedEntityCount(), 3u);

    // 验证不同类型都被选中
    EXPECT_TRUE(scene.findSyEntityById(lineId)->selected());
    EXPECT_TRUE(scene.findSyEntityById(circleId)->selected());
    EXPECT_TRUE(scene.findSyEntityById(polyId)->selected());
}

TEST(SelectionSyncExtendedTest, SelectMixedTypes_DeleteOne_CheckOthers)
{
    Eg::SceneManager scene;

    auto line = std::make_unique<Eg::SyLine>();
    line->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });
    Eg::EntityId lineId = line->id;

    auto circle = std::make_unique<Eg::SyCircle>();
    circle->basePoint = Ut::Vec2d(50, 50);
    circle->dRadius = 20.0;
    Eg::EntityId circleId = circle->id;

    std::vector<std::unique_ptr<Eg::SyEntity>> entities;
    entities.push_back(std::move(line));
    entities.push_back(std::move(circle));
    scene.addEntities(std::move(entities));

    scene.selectAll();
    EXPECT_EQ(scene.getSelectedEntityCount(), 2u);

    scene.deleteEntity(scene.findSyEntityById(lineId));
    EXPECT_EQ(scene.getSelectedEntityCount(), 1u);
    EXPECT_TRUE(scene.findSyEntityById(circleId)->selected());
    EXPECT_EQ(scene.findSyEntityById(lineId), nullptr);
}

// ==================== 悬空选中与工具状态一致性 ====================

TEST(SelectionSyncExtendedTest, DeleteSelectedViaDeleteEntity_ClearsSelection)
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

    // 通过 deleteEntity 删除选中实体，选中集应自动清除
    scene.deleteEntity(scene.findSyEntityById(lineId));
    EXPECT_EQ(scene.getSelectedEntityCount(), 0u);
    EXPECT_EQ(scene.findSyEntityById(lineId), nullptr);
}

TEST(SelectionSyncExtendedTest, DeleteAllSelected_OneByOne)
{
    Eg::SceneManager scene;

    std::vector<Eg::EntityId> ids;
    for (int i = 0; i < 4; ++i)
    {
        auto line = std::make_unique<Eg::SyLine>();
        line->setPointVector({ Ut::Vec2d(0.0, 0.0), Ut::Vec2d(10.0 + i, 10.0 + i) });
        ids.push_back(line->id);
        std::vector<std::unique_ptr<Eg::SyEntity>> entities;
        entities.push_back(std::move(line));
        scene.addEntities(std::move(entities));
    }

    scene.selectAll();
    EXPECT_EQ(scene.getSelectedEntityCount(), 4u);

    // 逐个删除选中实体，每次删除后选中数应减少
    for (size_t i = 0; i < ids.size(); ++i)
    {
        auto* entity = scene.findSyEntityById(ids[i]);
        ASSERT_NE(entity, nullptr);
        scene.deleteEntity(entity);
        EXPECT_EQ(scene.getSelectedEntityCount(), ids.size() - i - 1);
    }

    EXPECT_EQ(scene.getEntityCount(), 0u);
    EXPECT_EQ(scene.getSelectedEntityCount(), 0u);
}

TEST(SelectionSyncExtendedTest, DeleteEntity_CheckDirtyAndDeleted)
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

    // 删除后应有 deletedEntityIds
    const auto& deleted = scene.deletedEntityIds();
    EXPECT_FALSE(deleted.empty());
    // 删除后不应有悬空选中
    EXPECT_EQ(scene.getSelectedEntityCount(), 0u);
}

// ==================== SelectTool 与场景一致性测试 ====================

TEST(SelectToolRegressionTest, SyncFromScene_AfterExternalDelete)
{
    SelectTool tool;
    Eg::SceneManager scene;

    auto line = std::make_unique<Eg::SyLine>();
    line->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });
    Eg::EntityId lineId = line->id;

    std::vector<std::unique_ptr<Eg::SyEntity>> entities;
    entities.push_back(std::move(line));
    scene.addEntities(std::move(entities));

    scene.selectEntity(scene.findSyEntityById(lineId));

    ToolContext ctx;
    ctx.sceneManager = &scene;
    tool.initialize(ctx);
    tool.syncSelectionFromScene();
    EXPECT_TRUE(tool.hasSelectedEntities());

    // 外部删除实体后，同步应清空选择
    scene.deleteEntity(scene.findSyEntityById(lineId));
    tool.syncSelectionFromScene();
    EXPECT_FALSE(tool.hasSelectedEntities());
}

TEST(SelectToolRegressionTest, SyncFromScene_AfterExternalClear)
{
    SelectTool tool;
    Eg::SceneManager scene;

    auto line = std::make_unique<Eg::SyLine>();
    line->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });

    std::vector<std::unique_ptr<Eg::SyEntity>> entities;
    entities.push_back(std::move(line));
    scene.addEntities(std::move(entities));

    scene.selectAll();

    ToolContext ctx;
    ctx.sceneManager = &scene;
    tool.initialize(ctx);
    tool.syncSelectionFromScene();
    EXPECT_TRUE(tool.hasSelectedEntities());

    // 外部清空场景后，同步应清空选择
    scene.clearScene();
    tool.syncSelectionFromScene();
    EXPECT_FALSE(tool.hasSelectedEntities());
}

TEST(SelectToolRegressionTest, SyncFromScene_AfterExternalSelectChange)
{
    SelectTool tool;
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

    scene.selectEntity(scene.findSyEntityById(id1));

    ToolContext ctx;
    ctx.sceneManager = &scene;
    tool.initialize(ctx);
    tool.syncSelectionFromScene();
    EXPECT_TRUE(tool.hasSelectedEntities());

    // 外部切换选中
    scene.clearSelection();
    scene.selectEntity(scene.findSyEntityById(id2));
    tool.syncSelectionFromScene();
    EXPECT_TRUE(tool.hasSelectedEntities());
}

// ==================== ToolManager 空指针安全测试 ====================

TEST(ToolSelectionSyncRegressionTest, ToolManager_SetActiveTool_BeforeInit)
{
    ToolManager tm;
    tm.registerTool<SelectTool>("SelectTool");

    // 未初始化时切换工具不应崩溃
    EXPECT_FALSE(tm.setActiveTool("SelectTool"));
    EXPECT_EQ(tm.getActiveTool(), nullptr);
}

TEST(ToolSelectionSyncRegressionTest, ToolManager_GetTool_BeforeInit)
{
    ToolManager tm;
    tm.registerTool<SelectTool>("SelectTool");

    // 未初始化时获取工具返回 nullptr
    EXPECT_EQ(tm.getTool("SelectTool"), nullptr);
}

TEST(ToolSelectionSyncRegressionTest, ToolManager_GetTool_UnknownName)
{
    ToolManager tm;
    tm.registerTool<SelectTool>("SelectTool");

    Eg::SceneManager scene;
    ToolContext ctx;
    ctx.sceneManager = &scene;

    tm.initializeTools(ctx);
    EXPECT_EQ(tm.getTool("UnknownTool"), nullptr);
}

TEST(ToolSelectionSyncRegressionTest, ToolManager_GetActiveToolName_BeforeInit)
{
    ToolManager tm;
    tm.registerTool<SelectTool>("SelectTool");

    EXPECT_EQ(tm.getActiveToolName(), "");
}

TEST(ToolSelectionSyncRegressionTest, ToolManager_GetToolCount_BeforeInit)
{
    ToolManager tm;
    tm.registerTool<SelectTool>("SelectTool");
    tm.registerTool<SelectTool>("AnotherTool");

    EXPECT_EQ(tm.getToolCount(), 0u);
}

// ==================== 工具回调链完整性测试 ====================

TEST(ToolSelectionSyncRegressionTest, ToolManager_AllToolsReceiveEntityCallback)
{
    ToolManager tm;
    tm.registerTool<SelectTool>("SelectTool");

    Eg::SceneManager scene;
    ToolContext ctx;
    ctx.sceneManager = &scene;

    tm.initializeTools(ctx);

    int callbackCount = 0;
    tm.setEntityCallbackForAllTools([&callbackCount](Eg::SyEntity*) {
        callbackCount++;
    });

    EXPECT_NE(tm.getTool("SelectTool"), nullptr);
    SUCCEED();
}

// ==================== P5 补充：工具与选择状态深度同步测试 ====================

TEST(SelectToolRegressionTest, HasSelectedEntities_AfterSyncFromEmpty)
{
    SelectTool tool;
    Eg::SceneManager scene;
    ToolContext ctx;
    ctx.sceneManager = &scene;
    tool.initialize(ctx);

    tool.syncSelectionFromScene();
    EXPECT_FALSE(tool.hasSelectedEntities());
}

TEST(SelectToolRegressionTest, HasSelectedEntities_AfterPartialDeselect)
{
    SelectTool tool;
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

    scene.selectAll();
    EXPECT_EQ(scene.getSelectedEntityCount(), 2u);

    ToolContext ctx;
    ctx.sceneManager = &scene;
    tool.initialize(ctx);
    tool.syncSelectionFromScene();
    EXPECT_TRUE(tool.hasSelectedEntities());

    // 取消选中一个
    scene.deselectEntity(scene.findSyEntityById(id1));
    EXPECT_EQ(scene.getSelectedEntityCount(), 1u);

    tool.syncSelectionFromScene();
    EXPECT_TRUE(tool.hasSelectedEntities());
}

TEST(SelectToolRegressionTest, Deactivate_DoesNotAffectSelection)
{
    SelectTool tool;
    Eg::SceneManager scene;

    auto line = std::make_unique<Eg::SyLine>();
    line->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });
    Eg::EntityId lineId = line->id;

    std::vector<std::unique_ptr<Eg::SyEntity>> entities;
    entities.push_back(std::move(line));
    scene.addEntities(std::move(entities));

    scene.selectEntity(scene.findSyEntityById(lineId));
    EXPECT_EQ(scene.getSelectedEntityCount(), 1u);

    // 工具停用不应影响场景选择状态
    ToolContext ctx;
    ctx.sceneManager = &scene;
    tool.initialize(ctx);
    tool.onActivate();
    tool.onDeactivate();

    EXPECT_EQ(scene.getSelectedEntityCount(), 1u);
}

// ==================== P5 补充：ToolManager 工具切换与选择同步 ====================

TEST(ToolSelectionSyncRegressionTest, ToolManager_CancelActiveToolDuringOperation)
{
    ToolManager tm;
    tm.registerTool<SelectTool>("SelectTool");

    Eg::SceneManager scene;
    ToolContext ctx;
    ctx.sceneManager = &scene;

    tm.initializeTools(ctx);
    tm.setActiveTool("SelectTool");
    EXPECT_NE(tm.getActiveTool(), nullptr);

    // 取消当前工具（取消操作，不退出工具）
    tm.cancelCurrentTool();
    EXPECT_NE(tm.getActiveTool(), nullptr);

    // 再次激活
    tm.setActiveTool("SelectTool");
    EXPECT_NE(tm.getActiveTool(), nullptr);
}

TEST(ToolSelectionSyncRegressionTest, ToolManager_RepeatedActivateDeactivate)
{
    ToolManager tm;
    tm.registerTool<SelectTool>("SelectTool");

    Eg::SceneManager scene;
    ToolContext ctx;
    ctx.sceneManager = &scene;

    tm.initializeTools(ctx);

    // 反复激活/取消
    for (int i = 0; i < 5; ++i)
    {
        tm.setActiveTool("SelectTool");
        EXPECT_NE(tm.getActiveTool(), nullptr);
        tm.cancelCurrentTool();
        EXPECT_NE(tm.getActiveTool(), nullptr);
    }
}

// ==================== P5 补充：选择状态在场景变更后的一致性 ====================

TEST(SelectionSyncExtendedTest, ClearSceneAfterSelect_NoCrash)
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

    scene.clearScene();
    EXPECT_EQ(scene.getEntityCount(), 0u);
    EXPECT_EQ(scene.getSelectedEntityCount(), 0u);

    // 空场景上操作不应崩溃
    scene.selectAll();
    scene.invertSelection();
    scene.deleteSelected();
    SUCCEED();
}

TEST(SelectionSyncExtendedTest, DeleteEntityThenSelectAgain)
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

    // 选中 id1
    scene.selectEntity(scene.findSyEntityById(id1));
    EXPECT_EQ(scene.getSelectedEntityCount(), 1u);

    // 删除 id1
    scene.deleteEntity(scene.findSyEntityById(id1));
    EXPECT_EQ(scene.getSelectedEntityCount(), 0u);

    // 选中 id2
    scene.selectEntity(scene.findSyEntityById(id2));
    EXPECT_EQ(scene.getSelectedEntityCount(), 1u);
}

TEST(SelectionSyncExtendedTest, AddEntityWhileSelected_SelectionUnchanged)
{
    Eg::SceneManager scene;

    auto line1 = std::make_unique<Eg::SyLine>();
    line1->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });
    Eg::EntityId id1 = line1->id;

    std::vector<std::unique_ptr<Eg::SyEntity>> entities;
    entities.push_back(std::move(line1));
    scene.addEntities(std::move(entities));

    scene.selectEntity(scene.findSyEntityById(id1));
    EXPECT_EQ(scene.getSelectedEntityCount(), 1u);

    // 添加新实体不应影响已有选中
    auto line2 = std::make_unique<Eg::SyLine>();
    line2->setPointVector({ Ut::Vec2d(20, 20), Ut::Vec2d(30, 30) });
    std::vector<std::unique_ptr<Eg::SyEntity>> entities2;
    entities2.push_back(std::move(line2));
    scene.addEntities(std::move(entities2));

    EXPECT_EQ(scene.getSelectedEntityCount(), 1u);
    EXPECT_TRUE(scene.findSyEntityById(id1)->selected());
}

TEST(SelectionSyncExtendedTest, DeselectAllThenSelectAll)
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

    scene.selectAll();
    EXPECT_EQ(scene.getSelectedEntityCount(), 5u);
}

TEST(ToolSelectionSyncRegressionTest, ToolManager_AllToolsReceiveSwitchCallback)
{
    ToolManager tm;
    tm.registerTool<SelectTool>("SelectTool");

    Eg::SceneManager scene;
    ToolContext ctx;
    ctx.sceneManager = &scene;

    tm.initializeTools(ctx);

    QString lastSwitch;
    tm.setSwitchToolCallbackForAllTools([&lastSwitch](const QString& name) {
        lastSwitch = name;
    });

    EXPECT_NE(tm.getTool("SelectTool"), nullptr);
    SUCCEED();
}

// ==================== P5 补测试: 外部修改选择状态同步 ====================

TEST(ToolSelectionSyncRegressionTest, ExternalModify_SyncAfterSceneDelete)
{
    // 场景管理器直接删除实体后，选择状态应同步
    Eg::SceneManager scene;
    SelectionService svc(&scene);

    auto line = std::make_unique<Eg::SyLine>();
    line->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });
    Eg::EntityId lineId = line->id;

    std::vector<std::unique_ptr<Eg::SyEntity>> entities;
    entities.push_back(std::move(line));
    scene.addEntities(std::move(entities));

    std::string idStr = std::to_string(lineId);
    svc.select(idStr.c_str());
    EXPECT_TRUE(svc.isSelected(idStr.c_str()));

    // 场景管理器直接删除实体
    scene.deleteEntity(scene.findSyEntityById(lineId));
    EXPECT_EQ(scene.getEntityCount(), 0u);
    // 选择应自动清除
    EXPECT_FALSE(svc.isSelected(idStr.c_str()));
}

TEST(ToolSelectionSyncRegressionTest, ExternalModify_SyncAfterSceneClear)
{
    // 场景清空后选择应同步清除
    Eg::SceneManager scene;
    SelectionService svc(&scene);

    std::vector<std::unique_ptr<Eg::SyEntity>> entities;
    std::string ids[3];
    for (int i = 0; i < 3; ++i)
    {
        auto line = std::make_unique<Eg::SyLine>();
        line->setPointVector({ Ut::Vec2d(0.0, 0.0), Ut::Vec2d(10.0 + i, 10.0 + i) });
        ids[i] = std::to_string(line->id);
        entities.push_back(std::move(line));
    }
    scene.addEntities(std::move(entities));

    const char* idPtrs[] = { ids[0].c_str(), ids[1].c_str(), ids[2].c_str() };
    svc.selectMultiple(idPtrs, 3);
    EXPECT_TRUE(svc.isSelected(ids[0].c_str()));
    EXPECT_TRUE(svc.isSelected(ids[1].c_str()));
    EXPECT_TRUE(svc.isSelected(ids[2].c_str()));

    // 场景清空
    scene.clearScene();
    // 选择应全部清除
    EXPECT_FALSE(svc.isSelected(ids[0].c_str()));
    EXPECT_FALSE(svc.isSelected(ids[1].c_str()));
    EXPECT_FALSE(svc.isSelected(ids[2].c_str()));
}

TEST(ToolSelectionSyncRegressionTest, ExternalModify_DeselectAllAfterSelectAll)
{
    // 全选后逐个取消选中
    Eg::SceneManager scene;
    SelectionService svc(&scene);

    std::vector<std::unique_ptr<Eg::SyEntity>> entities;
    std::string ids[3];
    for (int i = 0; i < 3; ++i)
    {
        auto line = std::make_unique<Eg::SyLine>();
        line->setPointVector({ Ut::Vec2d(0.0, 0.0), Ut::Vec2d(10.0 + i, 10.0 + i) });
        ids[i] = std::to_string(line->id);
        entities.push_back(std::move(line));
    }
    scene.addEntities(std::move(entities));

    // 全选
    scene.selectAll();
    EXPECT_EQ(scene.getSelectedEntityCount(), 3u);

    // 逐个取消
    scene.clearSelection();
    EXPECT_EQ(scene.getSelectedEntityCount(), 0u);
}

TEST(ToolSelectionSyncRegressionTest, SelectTool_ConsistencyWithScene)
{
    // 验证 SelectTool 与场景选择状态的一致性
    Eg::SceneManager scene;
    SelectionService svc(&scene);

    auto line = std::make_unique<Eg::SyLine>();
    line->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });
    Eg::EntityId lineId = line->id;

    std::vector<std::unique_ptr<Eg::SyEntity>> entities;
    entities.push_back(std::move(line));
    scene.addEntities(std::move(entities));

    // 通过 SelectionService 选中
    std::string idStr = std::to_string(lineId);
    svc.select(idStr.c_str());
    EXPECT_TRUE(svc.isSelected(idStr.c_str()));
    EXPECT_EQ(scene.getSelectedEntityCount(), 1u);

    // 通过场景管理器取消选中
    scene.clearSelection();
    EXPECT_EQ(scene.getSelectedEntityCount(), 0u);
    EXPECT_FALSE(svc.isSelected(idStr.c_str()));
}

// ==================== P5 补测试: 无悬空选中状态 ====================

TEST(ToolSelectionSyncRegressionTest, NoDangling_DeleteAllEntitiesOneByOne)
{
    // 逐个删除所有实体，验证无悬空选中
    Eg::SceneManager scene;
    SelectionService svc(&scene);

    std::vector<std::unique_ptr<Eg::SyEntity>> entities;
    std::vector<Eg::EntityId> entityIds;
    for (int i = 0; i < 5; ++i)
    {
        auto line = std::make_unique<Eg::SyLine>();
        line->setPointVector({ Ut::Vec2d(0.0, 0.0), Ut::Vec2d(10.0 + i, 10.0 + i) });
        entityIds.push_back(line->id);
        entities.push_back(std::move(line));
    }
    scene.addEntities(std::move(entities));

    // 全选
    scene.selectAll();
    EXPECT_EQ(scene.getSelectedEntityCount(), 5u);

    // 逐个删除
    for (size_t i = 0; i < entityIds.size(); ++i)
    {
        scene.deleteEntity(scene.findSyEntityById(entityIds[i]));
        EXPECT_EQ(scene.getSelectedEntityCount(), 4u - i);
    }

    // 全部删除后无悬空选中
    EXPECT_EQ(scene.getSelectedEntityCount(), 0u);
    EXPECT_EQ(scene.getEntityCount(), 0u);
}

TEST(ToolSelectionSyncRegressionTest, NoDangling_DeleteMixedTypes)
{
    // 删除混合类型实体，验证无悬空选中
    Eg::SceneManager scene;
    SelectionService svc(&scene);

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

    // 选中所有
    scene.selectAll();
    EXPECT_EQ(scene.getSelectedEntityCount(), 3u);

    // 删除一个
    scene.deleteEntity(scene.findSyEntityById(idCircle));
    EXPECT_EQ(scene.getSelectedEntityCount(), 2u);
    EXPECT_EQ(scene.getEntityCount(), 2u);

    // 验证剩余选中有效
    EXPECT_NE(scene.findSyEntityById(idLine), nullptr);
    EXPECT_NE(scene.findSyEntityById(idPoly), nullptr);
    EXPECT_EQ(scene.findSyEntityById(idCircle), nullptr);
}

// ==================== 本体拖动平移 + 变换撤销（Gizmo 全链路） ====================
//
// 这一组补的是集成缺口：原先只有 EntitySnapshotsCommand 的单元测试，
// 「SelectionGizmo::onMousePress/Move/Release → InteractiveEditSession → 撤销栈」
// 这一整条没有任何覆盖。wireGizmo() 里的 `if (m_sceneEdit)` 一旦因注入顺序变化
// 落空，变换就静默失去撤销，而旧测试全部照绿。
//
// 平移的判据是 HandleKind::Body —— 名字里有 Handle，但它不是手柄：
// 它表示"按在图元本体上"，由 hitsSelectedGeometry() → Geo2DQuery::hitTest 判定，
// 缩放/旋转手柄在它之前就已判完并返回。所以「点在图元上按住左键拖」和
// 「拖角点缩放」共用同一条 onMouseMove 路径，撤销登记也是同一套。

namespace
{
    QMouseEvent makeMouseEvent(QEvent::Type type, const QPointF& pos, Qt::MouseButton button)
    {
#if QT_VERSION >= QT_VERSION_CHECK(6, 4, 0)
        return QMouseEvent(type, pos, pos, button, button, Qt::NoModifier);
#else
        return QMouseEvent(type, pos, button, button, Qt::NoModifier);
#endif
    }

    /// 驱动一次完整的「按下 → 移动 → 松开」，坐标均为世界坐标。
    /// 返回按下是否被 Gizmo 接住（false 表示没命中，走的是框选而不是变换）。
    bool dragFromTo(SelectTool& tool, const QPointF& from, const QPointF& to)
    {
        QMouseEvent press = makeMouseEvent(QEvent::MouseButtonPress, from, Qt::LeftButton);
        tool.onMousePress(from, &press);
        const bool grabbed = tool.gizmo().isInteracting();

        QMouseEvent move = makeMouseEvent(QEvent::MouseMove, to, Qt::NoButton);
        tool.onMouseMove(to, &move);

        QMouseEvent release = makeMouseEvent(QEvent::MouseButtonRelease, to, Qt::LeftButton);
        tool.onMouseRelease(to, &release);
        return grabbed;
    }

    /// 手柄命中容差 = pixelToWorld × handlePixelRadius()。
    /// 测试里的图元有上百个世界单位，若沿用 1.0 的兜底比例，角点手柄的命中圈
    /// 会盖住整个包围盒内部，本体命中永远竞争不过缩放手柄。
    void initSelectToolForDrag(SelectTool& tool, Eg::SceneManager& scene, SceneEditService& edit)
    {
        ToolContext ctx;
        ctx.sceneManager = &scene;
        ctx.sceneEditService = &edit;
        ctx.pixelToWorld = []() -> double { return 0.05; };
        tool.initialize(ctx);
    }
}  // namespace

TEST(GizmoBodyDragRegressionTest, DragLineBodyTranslatesEntity)
{
    Eg::SceneManager scene;
    UndoRedoManager undoMgr(&scene);
    SceneEditService edit(&scene, &undoMgr);

    auto line = std::make_unique<Eg::SyLine>();
    line->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(100, 100) });
    const Eg::EntityId lineId = line->id;

    std::vector<std::unique_ptr<Eg::SyEntity>> entities;
    entities.push_back(std::move(line));
    scene.addEntities(std::move(entities));

    SelectTool tool;
    initSelectToolForDrag(tool, scene, edit);
    scene.selectEntity(scene.findSyEntityById(lineId));
    tool.syncSelectionFromScene();
    ASSERT_TRUE(tool.hasSelectedEntities());

    // (50,50) 在线上，且离四个角点手柄都远
    EXPECT_TRUE(dragFromTo(tool, QPointF(50, 50), QPointF(60, 70)));

    const Ut::BBox2d bbox = scene.findSyEntityById(lineId)->computeBBox();
    EXPECT_NEAR(bbox.minPt.x(), 10.0, 1e-9);
    EXPECT_NEAR(bbox.minPt.y(), 20.0, 1e-9);
    EXPECT_NEAR(bbox.maxPt.x(), 110.0, 1e-9);
    EXPECT_NEAR(bbox.maxPt.y(), 120.0, 1e-9);
}

TEST(GizmoBodyDragRegressionTest, DragImageInteriorTranslatesEntity)
{
    Eg::SceneManager scene;
    UndoRedoManager undoMgr(&scene);
    SceneEditService edit(&scene, &undoMgr);

    auto image = std::make_unique<Eg::SyImage>();
    image->nWidth = 2;
    image->nHeight = 2;
    image->ePixelFormat = Eg::SyPixelFormat::RGBA32;
    image->setPixelDataVector(std::vector<unsigned char>(2 * 2 * 4, 0xFF));
    image->topLeft = Ut::Vec2d(0, 60);
    image->topRight = Ut::Vec2d(100, 60);
    image->bottomLeft = Ut::Vec2d(0, 0);
    image->bottomRight = Ut::Vec2d(100, 0);
    image->basePoint = Ut::Vec2d(50, 30);
    const Eg::EntityId imageId = image->id;

    std::vector<std::unique_ptr<Eg::SyEntity>> entities;
    entities.push_back(std::move(image));
    scene.addEntities(std::move(entities));

    SelectTool tool;
    initSelectToolForDrag(tool, scene, edit);
    scene.selectEntity(scene.findSyEntityById(imageId));
    tool.syncSelectionFromScene();

    // 逐环定位：实体在场景里 → 选中同步进工具 → 同步进 Gizmo → 引擎层几何命中
    ASSERT_NE(scene.findSyEntityById(imageId), nullptr);
    ASSERT_TRUE(tool.hasSelectedEntities());
    ASSERT_EQ(tool.gizmo().selection().size(), 1u);
    {
        Eg::GeometryContext probe;
        probe.dPick = 0.25;
        probe.dModeling = 1e-6;
        EXPECT_TRUE(Eg::Geo2DQuery::hitTest(scene.findSyEntityById(imageId), Ut::Vec2d(20, 45), probe));
    }

    // 位图内部任意一点都应命中本体：Geo2DPath 的 IMAGE 分支把四个角点作为闭合轮廓，
    // locatePoint 才能判出 Inside。缺这个分支时会退化成按 value(t) 采样，
    // 32 个采样点全落在 basePoint 上，"到轮廓的距离"变成"到中心的距离"，
    // 于是只有缩放/旋转手柄可用，图片拖不动。
    EXPECT_TRUE(dragFromTo(tool, QPointF(20, 45), QPointF(35, 55)));

    const auto* moved = static_cast<const Eg::SyImage*>(scene.findSyEntityById(imageId));
    ASSERT_NE(moved, nullptr);
    EXPECT_NEAR(moved->bottomLeft.x(), 15.0, 1e-9);
    EXPECT_NEAR(moved->bottomLeft.y(), 10.0, 1e-9);
    EXPECT_NEAR(moved->topRight.x(), 115.0, 1e-9);
    EXPECT_NEAR(moved->topRight.y(), 70.0, 1e-9);
    // basePoint 必须跟着走，否则纹理贴图与几何错位
    EXPECT_NEAR(moved->basePoint.x(), 65.0, 1e-9);
    EXPECT_NEAR(moved->basePoint.y(), 40.0, 1e-9);
}

TEST(GizmoBodyDragRegressionTest, BodyDragIsUndoable)
{
    Eg::SceneManager scene;
    UndoRedoManager undoMgr(&scene);
    SceneEditService edit(&scene, &undoMgr);

    auto image = std::make_unique<Eg::SyImage>();
    image->nWidth = 1;
    image->nHeight = 1;
    image->ePixelFormat = Eg::SyPixelFormat::RGBA32;
    image->setPixelDataVector(std::vector<unsigned char>(4, 0xFF));
    image->topLeft = Ut::Vec2d(0, 60);
    image->topRight = Ut::Vec2d(100, 60);
    image->bottomLeft = Ut::Vec2d(0, 0);
    image->bottomRight = Ut::Vec2d(100, 0);
    image->basePoint = Ut::Vec2d(50, 30);
    const Eg::EntityId imageId = image->id;

    std::vector<std::unique_ptr<Eg::SyEntity>> entities;
    entities.push_back(std::move(image));
    scene.addEntities(std::move(entities));

    SelectTool tool;
    initSelectToolForDrag(tool, scene, edit);
    scene.selectEntity(scene.findSyEntityById(imageId));
    tool.syncSelectionFromScene();

    ASSERT_FALSE(undoMgr.canUndo());
    ASSERT_TRUE(dragFromTo(tool, QPointF(50, 30), QPointF(70, 30)));

    // 一次拖拽 = 一条记录（commitInteractive 传 allowMerge=false）
    EXPECT_TRUE(undoMgr.canUndo());
    EXPECT_EQ(undoMgr.undoCount(), 1u);

    undoMgr.undo();

    // 撤销走 applyEntitySnapshots，实体按 id 被整体替换，指针必须重新解析
    const auto* restored = static_cast<const Eg::SyImage*>(scene.findSyEntityById(imageId));
    ASSERT_NE(restored, nullptr);
    EXPECT_NEAR(restored->bottomLeft.x(), 0.0, 1e-9);
    EXPECT_NEAR(restored->topRight.x(), 100.0, 1e-9);
    EXPECT_NEAR(restored->basePoint.x(), 50.0, 1e-9);
    // 像素数据不能在快照往返中丢失
    EXPECT_EQ(restored->pixelDataSize(), 4u);

    ASSERT_TRUE(undoMgr.canRedo());
    undoMgr.redo();
    const auto* redone = static_cast<const Eg::SyImage*>(scene.findSyEntityById(imageId));
    ASSERT_NE(redone, nullptr);
    EXPECT_NEAR(redone->bottomLeft.x(), 20.0, 1e-9);
}

TEST(GizmoBodyDragRegressionTest, CornerHandleScaleIsUndoable)
{
    Eg::SceneManager scene;
    UndoRedoManager undoMgr(&scene);
    SceneEditService edit(&scene, &undoMgr);

    auto line = std::make_unique<Eg::SyLine>();
    line->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(100, 100) });
    const Eg::EntityId lineId = line->id;

    std::vector<std::unique_ptr<Eg::SyEntity>> entities;
    entities.push_back(std::move(line));
    scene.addEntities(std::move(entities));

    SelectTool tool;
    initSelectToolForDrag(tool, scene, edit);
    scene.selectEntity(scene.findSyEntityById(lineId));
    tool.syncSelectionFromScene();

    // 精确压在包围盒右上角 → TopRight 缩放手柄
    ASSERT_TRUE(dragFromTo(tool, QPointF(100, 100), QPointF(150, 150)));

    const Ut::BBox2d scaled = scene.findSyEntityById(lineId)->computeBBox();
    EXPECT_GT(scaled.width(), 100.0);
    EXPECT_EQ(undoMgr.undoCount(), 1u);

    undoMgr.undo();
    const Ut::BBox2d back = scene.findSyEntityById(lineId)->computeBBox();
    EXPECT_NEAR(back.width(), 100.0, 1e-9);
    EXPECT_NEAR(back.height(), 100.0, 1e-9);
}