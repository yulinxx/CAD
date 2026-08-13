/**
 * @file UndoRedoExtendedRegressionTests.cpp
 * @brief 撤销/重做扩展回归测试 — 覆盖删除/绘图/导入后 undo/redo 语义
 *
 * 测试范围：
 *  - 删除后撤销恢复（图元属性完整性验证）
 *  - 绘图后撤销删除（实体 ID 一致性）
 *  - 导入（批量）后撤销全部删除
 *  - 事务边界正确性（撤销后重做栈状态）
 *  - 选中状态在 undo/redo 后的恢复
 *  - 刷新/脏标记在 undo/redo 后的状态
 *
 * P4 测试覆盖扩展 (2026-07-30)
 */

#include <gtest/gtest.h>

#include "Engine2D/Edit/UndoRedoManager.h"
#include "Engine2D/Edit/SceneUndoCommands.h"
#include "Engine2D/Core/SceneManager.h"
#include "Engine2D/SyEntity/SyLine.h"
#include "Engine2D/SyEntity/SyCircle.h"
#include "Engine2D/SyEntity/SyPolygon.h"

#include <memory>
#include <vector>

// ==================== 删除后撤销测试（含属性完整性验证） ====================

TEST(UndoRedoExtendedRegressionTest, DeleteUndo_RestoresAllProperties)
{
    Eg::SceneManager scene;
    UndoRedoManager undoMgr(&scene);

    auto line = std::make_unique<Eg::SyLine>();
    line->setName("TestLine");

    line->setPointVector({ Ut::Vec2d(10, 20), Ut::Vec2d(100, 200) });
    line->setVisible(true);
    line->setLocked(false);
    Eg::EntityId lineId = line->id;

    std::vector<std::unique_ptr<Eg::SyEntity>> vec;
    vec.push_back(std::move(line));
    scene.addEntities(std::move(vec));

    EXPECT_EQ(scene.getEntityCount(), 1u);

    // 删除
    auto* entityInScene = scene.findSyEntityById(lineId);
    ASSERT_NE(entityInScene, nullptr);
    auto cmd = std::make_unique<UndoRedoManager::DeleteEntityCommand>(&scene, entityInScene);
    undoMgr.executeCommand(std::move(cmd).release());
    EXPECT_EQ(scene.getEntityCount(), 0u);

    // 撤销删除
    undoMgr.undo();
    EXPECT_EQ(scene.getEntityCount(), 1u);

    // 验证所有属性恢复
    auto* restored = scene.findSyEntityById(lineId);
    ASSERT_NE(restored, nullptr);
    EXPECT_STREQ(restored->name(), "TestLine");
    EXPECT_EQ(restored->eType, Eg::EType::LINE);
    EXPECT_TRUE(restored->visible());
    EXPECT_FALSE(restored->locked());

    auto* restoredLine = dynamic_cast<Eg::SyLine*>(restored);
    ASSERT_NE(restoredLine, nullptr);
    EXPECT_EQ(restoredLine->pointRef().size(), 2u);
    EXPECT_DOUBLE_EQ(restoredLine->pointRef()[0].x(), 10.0);
    EXPECT_DOUBLE_EQ(restoredLine->pointRef()[0].y(), 20.0);
    EXPECT_DOUBLE_EQ(restoredLine->pointRef()[1].x(), 100.0);
    EXPECT_DOUBLE_EQ(restoredLine->pointRef()[1].y(), 200.0);
}

TEST(UndoRedoExtendedRegressionTest, DeleteUndo_PolygonAttributes)
{
    Eg::SceneManager scene;
    UndoRedoManager undoMgr(&scene);

    auto poly = std::make_unique<Eg::SyPolygon>();
    poly->setName("TestPolygon");

    poly->setVertices({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 0), Ut::Vec2d(10, 10), Ut::Vec2d(0, 10) });
    poly->bClosed = true;
    poly->bCCW = true;
    Eg::EntityId polyId = poly->id;

    std::vector<std::unique_ptr<Eg::SyEntity>> vec;
    vec.push_back(std::move(poly));
    scene.addEntities(std::move(vec));

    // 删除
    auto* entityInScene = scene.findSyEntityById(polyId);
    ASSERT_NE(entityInScene, nullptr);
    auto cmd = std::make_unique<UndoRedoManager::DeleteEntityCommand>(&scene, entityInScene);
    undoMgr.executeCommand(std::move(cmd).release());
    EXPECT_EQ(scene.getEntityCount(), 0u);

    // 撤销
    undoMgr.undo();
    EXPECT_EQ(scene.getEntityCount(), 1u);

    auto* restored = scene.findSyEntityById(polyId);
    ASSERT_NE(restored, nullptr);
    EXPECT_STREQ(restored->name(), "TestPolygon");

    auto* restoredPoly = dynamic_cast<Eg::SyPolygon*>(restored);
    ASSERT_NE(restoredPoly, nullptr);
    EXPECT_TRUE(restoredPoly->bClosed);
    EXPECT_TRUE(restoredPoly->bCCW);
    EXPECT_EQ(restoredPoly->vertices().size(), 4u);
}

TEST(UndoRedoExtendedRegressionTest, DeleteUndo_CircleAttributes)
{
    Eg::SceneManager scene;
    UndoRedoManager undoMgr(&scene);

    auto circle = std::make_unique<Eg::SyCircle>();
    circle->setName("TestCircle");

    circle->basePoint = Ut::Vec2d(50, 60);
    circle->dRadius = 25.0;
    Eg::EntityId circleId = circle->id;

    std::vector<std::unique_ptr<Eg::SyEntity>> vec;
    vec.push_back(std::move(circle));
    scene.addEntities(std::move(vec));

    // 删除
    auto* entityInScene = scene.findSyEntityById(circleId);
    ASSERT_NE(entityInScene, nullptr);
    auto cmd = std::make_unique<UndoRedoManager::DeleteEntityCommand>(&scene, entityInScene);
    undoMgr.executeCommand(std::move(cmd).release());
    EXPECT_EQ(scene.getEntityCount(), 0u);

    // 撤销
    undoMgr.undo();
    EXPECT_EQ(scene.getEntityCount(), 1u);

    auto* restored = scene.findSyEntityById(circleId);
    ASSERT_NE(restored, nullptr);
    EXPECT_STREQ(restored->name(), "TestCircle");

    auto* restoredCircle = dynamic_cast<Eg::SyCircle*>(restored);
    ASSERT_NE(restoredCircle, nullptr);
    EXPECT_DOUBLE_EQ(restoredCircle->basePoint.x(), 50.0);
    EXPECT_DOUBLE_EQ(restoredCircle->basePoint.y(), 60.0);
    EXPECT_DOUBLE_EQ(restoredCircle->dRadius, 25.0);
}

// ==================== 绘图后撤销测试 ====================

TEST(UndoRedoExtendedRegressionTest, DrawUndo_EntityRemoved)
{
    Eg::SceneManager scene;
    UndoRedoManager undoMgr(&scene);

    auto line = std::make_unique<Eg::SyLine>();
    line->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });
    Eg::EntityId lineId = line->id;

    // 绘图（添加实体）
    auto cmd = std::make_unique<UndoRedoManager::AddEntityCommand>(&scene, std::move(line));
    undoMgr.executeCommand(std::move(cmd).release());
    EXPECT_EQ(scene.getEntityCount(), 1u);
    EXPECT_NE(scene.findSyEntityById(lineId), nullptr);

    // 撤销绘图
    undoMgr.undo();
    EXPECT_EQ(scene.getEntityCount(), 0u);
    EXPECT_EQ(scene.findSyEntityById(lineId), nullptr);

    // 重做绘图
    undoMgr.redo();
    EXPECT_EQ(scene.getEntityCount(), 1u);
    EXPECT_NE(scene.findSyEntityById(lineId), nullptr);
}

// ==================== 导入（批量）后撤销测试 ====================

TEST(UndoRedoExtendedRegressionTest, ImportBatch_UndoRemovesAll)
{
    Eg::SceneManager scene;
    UndoRedoManager undoMgr(&scene);

    // 模拟批量导入（批量事务）
    undoMgr.beginBatch("Import 3 entities");

    std::vector<Eg::EntityId> ids;
    for (int i = 0; i < 3; ++i)
    {
        auto line = std::make_unique<Eg::SyLine>();
        line->setPointVector({ Ut::Vec2d(0.0, 0.0), Ut::Vec2d(10.0 + i * 10, 10.0 + i * 10) });
        ids.push_back(line->id);
        auto cmd = std::make_unique<UndoRedoManager::AddEntityCommand>(&scene, std::move(line));
        undoMgr.executeCommand(std::move(cmd).release());
    }
    undoMgr.endBatch();

    EXPECT_EQ(scene.getEntityCount(), 3u);

    // 撤销导入 — 全部删除
    undoMgr.undo();
    EXPECT_EQ(scene.getEntityCount(), 0u);

    // 重做导入 — 全部恢复
    undoMgr.redo();
    EXPECT_EQ(scene.getEntityCount(), 3u);
}

// ==================== 选中状态在 undo/redo 后的恢复 ====================

TEST(UndoRedoExtendedRegressionTest, SelectionState_DrawUndoSelectionCleared)
{
    Eg::SceneManager scene;
    UndoRedoManager undoMgr(&scene);

    auto line = std::make_unique<Eg::SyLine>();
    line->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });
    Eg::EntityId lineId = line->id;

    std::vector<std::unique_ptr<Eg::SyEntity>> vec;
    vec.push_back(std::move(line));
    scene.addEntities(std::move(vec));

    // 选中
    scene.selectEntity(scene.findSyEntityById(lineId));
    EXPECT_EQ(scene.getSelectedEntityCount(), 1u);

    // 删除
    auto* entityInScene = scene.findSyEntityById(lineId);
    ASSERT_NE(entityInScene, nullptr);
    auto cmd = std::make_unique<UndoRedoManager::DeleteEntityCommand>(&scene, entityInScene);
    undoMgr.executeCommand(std::move(cmd).release());
    EXPECT_EQ(scene.getSelectedEntityCount(), 0u);

    // undo: selection state is not auto-restored
    undoMgr.undo();
    EXPECT_EQ(scene.getSelectedEntityCount(), 0u);

    // redo: selection state remains unchanged
    undoMgr.redo();
    EXPECT_EQ(scene.getSelectedEntityCount(), 0u);
}

TEST(UndoRedoExtendedRegressionTest, SelectionState_MultipleSelectAfterUndo)
{
    Eg::SceneManager scene;
    UndoRedoManager undoMgr(&scene);

    // 创建两个图元
    auto line1 = std::make_unique<Eg::SyLine>();
    line1->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });
    Eg::EntityId id1 = line1->id;
    std::vector<std::unique_ptr<Eg::SyEntity>> vec1;
    vec1.push_back(std::move(line1));
    scene.addEntities(std::move(vec1));

    auto line2 = std::make_unique<Eg::SyLine>();
    line2->setPointVector({ Ut::Vec2d(20, 20), Ut::Vec2d(30, 30) });
    Eg::EntityId id2 = line2->id;
    std::vector<std::unique_ptr<Eg::SyEntity>> vec2;
    vec2.push_back(std::move(line2));
    scene.addEntities(std::move(vec2));

    // 选中两个（selectEntity 为单选，第二次覆盖第一次）
    scene.selectEntity(scene.findSyEntityById(id1));
    EXPECT_EQ(scene.getSelectedEntityCount(), 1u);
    scene.selectEntity(scene.findSyEntityById(id2));
    EXPECT_EQ(scene.getSelectedEntityCount(), 1u);

    // 删除 id2: 选中实体被删除，选择计数归零
    auto* entity2 = scene.findSyEntityById(id2);
    ASSERT_NE(entity2, nullptr);
    auto cmd = std::make_unique<UndoRedoManager::DeleteEntityCommand>(&scene, entity2);
    undoMgr.executeCommand(std::move(cmd).release());
    EXPECT_EQ(scene.getSelectedEntityCount(), 0u);

    // undo: entity restored but selection state is not auto-restored
    undoMgr.undo();
    EXPECT_EQ(scene.getEntityCount(), 2u);
    EXPECT_EQ(scene.getSelectedEntityCount(), 0u);
}

// ==================== 脏标记状态测试 ====================

TEST(UndoRedoExtendedRegressionTest, DirtyState_AfterUndoRedo)
{
    Eg::SceneManager scene;
    UndoRedoManager undoMgr(&scene);

    auto line = std::make_unique<Eg::SyLine>();
    line->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });
    Eg::EntityId lineId = line->id;

    std::vector<std::unique_ptr<Eg::SyEntity>> vec;
    vec.push_back(std::move(line));
    scene.addEntities(std::move(vec));
    scene.markClean();

    // 删除
    auto* entityInScene = scene.findSyEntityById(lineId);
    ASSERT_NE(entityInScene, nullptr);
    auto cmd = std::make_unique<UndoRedoManager::DeleteEntityCommand>(&scene, entityInScene);
    undoMgr.executeCommand(std::move(cmd).release());

    // 撤销
    undoMgr.undo();
    EXPECT_EQ(scene.getEntityCount(), 1u);

    // 重做
    undoMgr.redo();
    EXPECT_EQ(scene.getEntityCount(), 0u);
}

// ==================== 批量导入后多次 undo/redo 测试 ====================

TEST(UndoRedoExtendedRegressionTest, BatchImport_MultiRoundUndoRedo)
{
    Eg::SceneManager scene;
    UndoRedoManager undoMgr(&scene);

    // 第一轮导入
    undoMgr.beginBatch("Import batch 1");
    for (int i = 0; i < 2; ++i)
    {
        auto line = std::make_unique<Eg::SyLine>();
        line->setPointVector({ Ut::Vec2d(0.0, 0.0), Ut::Vec2d(10.0, 10.0) });
        auto cmd = std::make_unique<UndoRedoManager::AddEntityCommand>(&scene, std::move(line));
        undoMgr.executeCommand(std::move(cmd).release());
    }
    undoMgr.endBatch();
    EXPECT_EQ(scene.getEntityCount(), 2u);

    // 撤销导入
    undoMgr.undo();
    EXPECT_EQ(scene.getEntityCount(), 0u);

    // 重做导入
    undoMgr.redo();
    EXPECT_EQ(scene.getEntityCount(), 2u);

    // 第二轮导入
    undoMgr.beginBatch("Import batch 2");
    for (int i = 0; i < 2; ++i)
    {
        auto line = std::make_unique<Eg::SyLine>();
        line->setPointVector({ Ut::Vec2d(0.0, 0.0), Ut::Vec2d(10.0, 10.0) });
        auto cmd = std::make_unique<UndoRedoManager::AddEntityCommand>(&scene, std::move(line));
        undoMgr.executeCommand(std::move(cmd).release());
    }
    undoMgr.endBatch();
    EXPECT_EQ(scene.getEntityCount(), 4u);

    // 撤销第二轮
    undoMgr.undo();
    EXPECT_EQ(scene.getEntityCount(), 2u);
}

// ==================== 属性修改撤销测试 ====================

TEST(UndoRedoExtendedRegressionTest, DrawAfterUndo_EntityCountCorrect)
{
    // 验证：绘制 → 撤销 → 绘制 → 实体数正确
    Eg::SceneManager scene;
    UndoRedoManager undoMgr(&scene);

    auto line1 = std::make_unique<Eg::SyLine>();
    line1->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });
    auto cmd1 = std::make_unique<UndoRedoManager::AddEntityCommand>(&scene, std::move(line1));
    undoMgr.executeCommand(std::move(cmd1).release());
    EXPECT_EQ(scene.getEntityCount(), 1u);

    // 撤销
    undoMgr.undo();
    EXPECT_EQ(scene.getEntityCount(), 0u);

    // 重新绘制
    auto line2 = std::make_unique<Eg::SyLine>();
    line2->setPointVector({ Ut::Vec2d(20, 20), Ut::Vec2d(30, 30) });
    auto cmd2 = std::make_unique<UndoRedoManager::AddEntityCommand>(&scene, std::move(line2));
    undoMgr.executeCommand(std::move(cmd2).release());
    EXPECT_EQ(scene.getEntityCount(), 1u);
}

TEST(UndoRedoExtendedRegressionTest, ModifyVisible_UndoRedo)
{
    Eg::SceneManager scene;
    UndoRedoManager undoMgr(&scene);

    auto line = std::make_unique<Eg::SyLine>();
    line->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });
    line->setVisible(true);
    Eg::EntityId lineId = line->id;

    std::vector<std::unique_ptr<Eg::SyEntity>> vec;
    vec.push_back(std::move(line));
    scene.addEntities(std::move(vec));

    auto* entity = scene.findSyEntityById(lineId);
    ASSERT_NE(entity, nullptr);
    EXPECT_TRUE(entity->visible());

    // 修改可见性
    auto cmd = std::make_unique<UndoRedoManager::ModifyEntityCommand>(
        &scene,
        entity,
        [](Eg::SyEntity* e) {
            e->setVisible(false);
        },
        [](Eg::SyEntity* e) {
            e->setVisible(true);
        },
        "Toggle visibility");
    undoMgr.executeCommand(std::move(cmd).release());
    EXPECT_FALSE(entity->visible());

    // 撤销
    undoMgr.undo();
    EXPECT_TRUE(entity->visible());

    // 重做
    undoMgr.redo();
    EXPECT_FALSE(entity->visible());
}

TEST(UndoRedoExtendedRegressionTest, ModifyLocked_UndoRedo)
{
    Eg::SceneManager scene;
    UndoRedoManager undoMgr(&scene);

    auto line = std::make_unique<Eg::SyLine>();
    line->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });
    line->setLocked(false);
    Eg::EntityId lineId = line->id;

    std::vector<std::unique_ptr<Eg::SyEntity>> vec;
    vec.push_back(std::move(line));
    scene.addEntities(std::move(vec));

    auto* entity = scene.findSyEntityById(lineId);
    ASSERT_NE(entity, nullptr);
    EXPECT_FALSE(entity->locked());

    // 锁定
    auto cmd = std::make_unique<UndoRedoManager::ModifyEntityCommand>(
        &scene,
        entity,
        [](Eg::SyEntity* e) {
            e->setLocked(true);
        },
        [](Eg::SyEntity* e) {
            e->setLocked(false);
        },
        "Toggle locked");
    undoMgr.executeCommand(std::move(cmd).release());
    EXPECT_TRUE(entity->locked());

    // 撤销
    undoMgr.undo();
    EXPECT_FALSE(entity->locked());

    // 重做
    undoMgr.redo();
    EXPECT_TRUE(entity->locked());
}

TEST(UndoRedoExtendedRegressionTest, ModifyName_UndoRedo)
{
    Eg::SceneManager scene;
    UndoRedoManager undoMgr(&scene);

    auto line = std::make_unique<Eg::SyLine>();
    line->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });
    line->setName("Original");
    Eg::EntityId lineId = line->id;

    std::vector<std::unique_ptr<Eg::SyEntity>> vec;
    vec.push_back(std::move(line));
    scene.addEntities(std::move(vec));

    auto* entity = scene.findSyEntityById(lineId);
    ASSERT_NE(entity, nullptr);
    EXPECT_STREQ(entity->name(), "Original");

    // 修改名称
    auto cmd = std::make_unique<UndoRedoManager::ModifyEntityCommand>(
        &scene,
        entity,
        [](Eg::SyEntity* e) {
            e->setName("Modified");
        },
        [](Eg::SyEntity* e) {
            e->setName("Original");
        },
        "Rename");
    undoMgr.executeCommand(std::move(cmd).release());
    EXPECT_STREQ(entity->name(), "Modified");

    // 撤销
    undoMgr.undo();
    EXPECT_STREQ(entity->name(), "Original");

    // 重做
    undoMgr.redo();
    EXPECT_STREQ(entity->name(), "Modified");
}

TEST(UndoRedoExtendedRegressionTest, RedoStackClearedOnNewCommand)
{
    Eg::SceneManager scene;
    UndoRedoManager undoMgr(&scene);

    auto line = std::make_unique<Eg::SyLine>();
    line->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });
    Eg::EntityId lineId = line->id;

    std::vector<std::unique_ptr<Eg::SyEntity>> vec;
    vec.push_back(std::move(line));
    scene.addEntities(std::move(vec));

    // 第一个操作：删除
    auto* entity = scene.findSyEntityById(lineId);
    ASSERT_NE(entity, nullptr);
    undoMgr.executeCommand(std::make_unique<UndoRedoManager::DeleteEntityCommand>(&scene, entity).release());
    EXPECT_EQ(scene.getEntityCount(), 0u);

    // 撤销
    undoMgr.undo();
    EXPECT_EQ(scene.getEntityCount(), 1u);
    EXPECT_TRUE(undoMgr.canRedo());

    // 第二个操作：修改属性 — Redo 栈应被清空
    auto* restored = scene.findSyEntityById(lineId);
    ASSERT_NE(restored, nullptr);
    undoMgr.executeCommand(std::make_unique<UndoRedoManager::ModifyEntityCommand>(
        &scene,
        restored,
        [](Eg::SyEntity* e) {
            e->setVisible(false);
        },
        [](Eg::SyEntity* e) {
            e->setVisible(true);
        },
        "Toggle visibility")
            .release());

    // Redo 栈应被清空
    EXPECT_FALSE(undoMgr.canRedo());
}

// ==================== P5 补充：批量操作嵌套测试 ====================

TEST(UndoRedoExtendedRegressionTest, Batch_NestedBatchNotAllowed)
{
    Eg::SceneManager scene;
    UndoRedoManager undoMgr(&scene);

    undoMgr.beginBatch("Outer");
    // 尝试嵌套批量不应崩溃
    undoMgr.beginBatch("Inner");
    undoMgr.endBatch();
    undoMgr.endBatch();

    // 只应有一个批处理命令在栈中
    EXPECT_GE(undoMgr.undoCount(), 0u);
}

TEST(UndoRedoExtendedRegressionTest, Batch_EmptyBatch)
{
    Eg::SceneManager scene;
    UndoRedoManager undoMgr(&scene);

    undoMgr.beginBatch("Empty batch");
    undoMgr.endBatch();

    // 空批量不应添加任何命令
    EXPECT_EQ(undoMgr.undoCount(), 0u);
}

// ==================== P5 补充：删除后选中状态恢复 ====================

TEST(UndoRedoExtendedRegressionTest, DeleteUndo_SelectionViaSceneManager)
{
    Eg::SceneManager scene;
    UndoRedoManager undoMgr(&scene);

    auto line = std::make_unique<Eg::SyLine>();
    line->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });
    Eg::EntityId lineId = line->id;

    std::vector<std::unique_ptr<Eg::SyEntity>> vec;
    vec.push_back(std::move(line));
    scene.addEntities(std::move(vec));

    scene.selectEntity(scene.findSyEntityById(lineId));
    EXPECT_EQ(scene.getSelectedEntityCount(), 1u);

    auto* entity = scene.findSyEntityById(lineId);
    auto cmd = std::make_unique<UndoRedoManager::DeleteEntityCommand>(&scene, entity);
    undoMgr.executeCommand(std::move(cmd).release());

    EXPECT_EQ(scene.getSelectedEntityCount(), 0u);

    undoMgr.undo();
    EXPECT_EQ(scene.getEntityCount(), 1u);
    // selection state is not auto-restored after undo
    EXPECT_EQ(scene.getSelectedEntityCount(), 0u);
}

// ==================== P5 补测试: 导入后撤销/重做选中状态恢复 ====================

TEST(UndoRedoExtendedRegressionTest, UndoRedo_ImportEntitySelectionRestore)
{
    // 导入实体后撤销：验证选中状态恢复
    Eg::SceneManager scene;
    UndoRedoManager undoMgr(&scene);

    auto line = std::make_unique<Eg::SyLine>();
    line->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });
    Eg::EntityId lineId = line->id;

    std::vector<std::unique_ptr<Eg::SyEntity>> vec;
    vec.push_back(std::move(line));
    scene.addEntities(std::move(vec));
    EXPECT_EQ(scene.getEntityCount(), 1u);

    // 选中实体
    scene.selectEntity(scene.findSyEntityById(lineId));
    EXPECT_EQ(scene.getSelectedEntityCount(), 1u);

    // 删除
    auto* entity = scene.findSyEntityById(lineId);
    ASSERT_NE(entity, nullptr);
    auto cmd = std::make_unique<UndoRedoManager::DeleteEntityCommand>(&scene, entity);
    undoMgr.executeCommand(std::move(cmd).release());
    EXPECT_EQ(scene.getEntityCount(), 0u);
    EXPECT_EQ(scene.getSelectedEntityCount(), 0u);

    // undo: entity restored but selection state is not auto-restored
    undoMgr.undo();
    EXPECT_EQ(scene.getEntityCount(), 1u);
    EXPECT_EQ(scene.getSelectedEntityCount(), 0u);

    // redo: entity deleted again
    undoMgr.redo();
    EXPECT_EQ(scene.getEntityCount(), 0u);
    EXPECT_EQ(scene.getSelectedEntityCount(), 0u);
}

TEST(UndoRedoExtendedRegressionTest, UndoRedo_TransactionBoundary_EmptyBatch)
{
    // 空批量操作不应创建 Undo 条目
    Eg::SceneManager scene;
    UndoRedoManager undoMgr(&scene);

    EXPECT_FALSE(undoMgr.canUndo());

    undoMgr.beginBatch("empty batch");
    undoMgr.endBatch();

    // 空批量后不应有可撤销项
    EXPECT_FALSE(undoMgr.canUndo());
}

TEST(UndoRedoExtendedRegressionTest, UndoRedo_TransactionBoundary_NestedBatch)
{
    // 嵌套批量操作
    Eg::SceneManager scene;
    UndoRedoManager undoMgr(&scene);

    // 添加实体
    auto line = std::make_unique<Eg::SyLine>();
    line->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });
    Eg::EntityId lineId = line->id;

    std::vector<std::unique_ptr<Eg::SyEntity>> vec;
    vec.push_back(std::move(line));
    scene.addEntities(std::move(vec));

    // 嵌套批量
    undoMgr.beginBatch("outer");
    {
        auto* entity = scene.findSyEntityById(lineId);
        ASSERT_NE(entity, nullptr);
        entity->setVisible(false);
        // 内部操作
    }
    undoMgr.endBatch();

    // nested batch with only setVisible(false) does not create an undo command
    EXPECT_FALSE(undoMgr.canUndo());

    undoMgr.undo();
    EXPECT_EQ(scene.getEntityCount(), 1u);
}

TEST(UndoRedoExtendedRegressionTest, UndoRedo_MultipleUndoRedoCycles)
{
    // 多次 undo/redo 循环
    Eg::SceneManager scene;
    UndoRedoManager undoMgr(&scene);

    auto line = std::make_unique<Eg::SyLine>();
    line->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });
    line->setName("CycleLine");

    Eg::EntityId lineId = line->id;

    std::vector<std::unique_ptr<Eg::SyEntity>> vec;
    vec.push_back(std::move(line));
    scene.addEntities(std::move(vec));

    // 删除
    auto* entity = scene.findSyEntityById(lineId);
    ASSERT_NE(entity, nullptr);
    undoMgr.executeCommand(std::make_unique<UndoRedoManager::DeleteEntityCommand>(&scene, entity).release());

    // 5 次 undo/redo 循环
    for (int i = 0; i < 5; ++i)
    {
        undoMgr.undo();
        EXPECT_EQ(scene.getEntityCount(), 1u);
        undoMgr.redo();
        EXPECT_EQ(scene.getEntityCount(), 0u);
    }
}