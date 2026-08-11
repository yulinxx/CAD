/**
 * @file UndoRedoRegressionTests.cpp
 * @brief 撤销/重做回归测试 — 覆盖命令栈、事务、批量操作、快照、保存点
 *
 * 测试范围：
 *  - UndoRedoManager 基础命令 (AddEntity / DeleteEntity / MoveEntity / ModifyEntity / LambdaCommand)
 *  - EntitySnapshotsCommand 快照命令
 *  - Batch 事务（beginBatch / endBatch）
 *  - SavePoint 保存点
 *  - 历史上限 (200)
 *  - 合并 (MoveEntityCommand merge)
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "Engine2D/Edit/UndoRedoManager.h"
#include "Engine2D/Edit/SceneUndoCommands.h"
#include "Engine2D/Core/SceneManager.h"
#include "Engine2D/SyEntity/SyLine.h"
#include "Engine2D/SyEntity/SyCircle.h"

#include <memory>
#include <vector>
#include <atomic>

 // ==================== 基础命令测试 ====================

TEST(UndoRedoRegressionTest, AddEntityCommand_ExecuteAndUndo)
{
    Eg::SceneManager scene;
    UndoRedoManager undoMgr(&scene);

    auto line = std::make_unique<Eg::SyLine>();
    line->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });
    Eg::EntityId entityId = line->id;

    auto cmd = std::make_unique<UndoRedoManager::AddEntityCommand>(
        &scene, std::move(line));
    undoMgr.executeCommand(std::move(cmd).release());

    // 执行后图元应存在
    EXPECT_EQ(scene.getEntityCount(), 1u);
    EXPECT_TRUE(undoMgr.canUndo());
    EXPECT_FALSE(undoMgr.canRedo());

    // 撤销
    undoMgr.undo();
    EXPECT_EQ(scene.getEntityCount(), 0u);
    EXPECT_FALSE(undoMgr.canUndo());
    EXPECT_TRUE(undoMgr.canRedo());

    // 重做
    undoMgr.redo();
    EXPECT_EQ(scene.getEntityCount(), 1u);
    EXPECT_TRUE(undoMgr.canUndo());
    EXPECT_FALSE(undoMgr.canRedo());

    // 验证重做后图元属性
    auto* restored = scene.findSyEntityById(entityId);
    ASSERT_NE(restored, nullptr);
    auto* restoredLine = dynamic_cast<Eg::SyLine*>(restored);
    ASSERT_NE(restoredLine, nullptr);
    EXPECT_EQ(restoredLine->pointRef().size(), 2u);
}

TEST(UndoRedoRegressionTest, DeleteEntityCommand_ExecuteAndUndo)
{
    Eg::SceneManager scene;
    UndoRedoManager undoMgr(&scene);

    // 使用 addEntities(move) 保留原始 ID，避免 clone 导致 ID 变化
    auto line = std::make_unique<Eg::SyLine>();
    line->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });
    Eg::EntityId entityId = line->id;
    Eg::SyLine* rawLine = line.get();

    std::vector<std::unique_ptr<Eg::SyEntity>> vec;
    vec.push_back(std::move(line));
    scene.addEntities(std::move(vec));

    EXPECT_EQ(scene.getEntityCount(), 1u);

    // 删除命令 — 使用场景中实际持有的实体指针
    auto* entityInScene = scene.findSyEntityById(entityId);
    ASSERT_NE(entityInScene, nullptr);
    auto cmd = std::make_unique<UndoRedoManager::DeleteEntityCommand>(
        &scene, entityInScene);
    undoMgr.executeCommand(std::move(cmd).release());

    EXPECT_EQ(scene.getEntityCount(), 0u);
    EXPECT_TRUE(undoMgr.canUndo());

    // 撤销删除
    undoMgr.undo();
    EXPECT_EQ(scene.getEntityCount(), 1u);
    auto* restored = scene.findSyEntityById(entityId);
    ASSERT_NE(restored, nullptr);
}

TEST(UndoRedoRegressionTest, MoveEntityCommand_Merge)
{
    Eg::SceneManager scene;
    UndoRedoManager undoMgr(&scene);

    auto line = std::make_unique<Eg::SyLine>();
    line->basePoint = Ut::Vec2d(0, 0);
    line->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 0) });
    Eg::EntityId entityId = line->id;

    std::vector<std::unique_ptr<Eg::SyEntity>> vec;
    vec.push_back(std::move(line));
    scene.addEntities(std::move(vec));

    auto* rawLine = dynamic_cast<Eg::SyLine*>(scene.findSyEntityById(entityId));
    ASSERT_NE(rawLine, nullptr);

    // 连续多次移动，应合并为一条命令
    // 首条命令通过 executeCommand 执行并入栈
    auto cmd1 = std::make_unique<UndoRedoManager::MoveEntityCommand>(
        &scene, rawLine, Ut::Vec2d(5, 0));
    undoMgr.executeCommand(std::move(cmd1).release());
    EXPECT_DOUBLE_EQ(rawLine->basePoint.x(), 5.0);

    // 后续移动命令通过 pushExecutedCommand 入栈，触发合并逻辑
    auto cmd2 = std::make_unique<UndoRedoManager::MoveEntityCommand>(
        &scene, rawLine, Ut::Vec2d(5, 0));
    cmd2->execute();  // 先执行
    undoMgr.pushExecutedCommand(std::move(cmd2).release());  // 再入栈（触发合并）
    EXPECT_DOUBLE_EQ(rawLine->basePoint.x(), 10.0);

    // 合并后 undo 一次应回到原始位置
    undoMgr.undo();
    EXPECT_DOUBLE_EQ(rawLine->basePoint.x(), 0.0);
}

TEST(UndoRedoRegressionTest, ModifyEntityCommand_CustomApply)
{
    Eg::SceneManager scene;
    UndoRedoManager undoMgr(&scene);

    auto line = std::make_unique<Eg::SyLine>();
    line->setName("Original");

    line->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });
    Eg::EntityId entityId = line->id;

    std::vector<std::unique_ptr<Eg::SyEntity>> vec;
    vec.push_back(std::move(line));
    scene.addEntities(std::move(vec));

    auto* rawLine = dynamic_cast<Eg::SyLine*>(scene.findSyEntityById(entityId));
    ASSERT_NE(rawLine, nullptr);

    auto cmd = std::make_unique<UndoRedoManager::ModifyEntityCommand>(
        &scene, rawLine,
        [](Eg::SyEntity* e) { e->setName("Modified"); },

        [](Eg::SyEntity* e) { e->setName("Original"); },

        "Rename entity");
    undoMgr.executeCommand(std::move(cmd).release());

    EXPECT_STREQ(rawLine->name(), "Modified");

    undoMgr.undo();
    EXPECT_STREQ(rawLine->name(), "Original");

    undoMgr.redo();
    EXPECT_STREQ(rawLine->name(), "Modified");
}

TEST(UndoRedoRegressionTest, LambdaCommand)
{
    int counter = 0;
    UndoRedoManager undoMgr(nullptr);

    auto cmd = std::make_unique<UndoRedoManager::LambdaCommand>(
        [&]() { counter += 1; },
        [&]() { counter -= 1; },
        "Increment/Decrement");
    undoMgr.executeCommand(std::move(cmd).release());

    EXPECT_EQ(counter, 1);
    EXPECT_TRUE(undoMgr.canUndo());

    undoMgr.undo();
    EXPECT_EQ(counter, 0);
    EXPECT_FALSE(undoMgr.canUndo());
    EXPECT_TRUE(undoMgr.canRedo());

    undoMgr.redo();
    EXPECT_EQ(counter, 1);
}

// ==================== 快照命令测试 ====================

TEST(UndoRedoRegressionTest, EntitySnapshotsCommand_RoundTrip)
{
    Eg::SceneManager scene;
    UndoRedoManager undoMgr(&scene);

    // 创建两个图元
    auto line = std::make_unique<Eg::SyLine>();
    line->setName("Line1");

    line->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });
    Eg::EntityId lineId = line->id;

    auto circle = std::make_unique<Eg::SyCircle>();
    circle->setName("Circle1");

    circle->basePoint = Ut::Vec2d(5, 5);
    circle->dRadius = 3.0;
    Eg::EntityId circleId = circle->id;

    std::vector<std::unique_ptr<Eg::SyEntity>> entities;
    entities.push_back(std::move(line));
    entities.push_back(std::move(circle));
    scene.addEntities(std::move(entities));

    EXPECT_EQ(scene.getEntityCount(), 2u);

    // 拍摄修改前快照
    Eg::EntityId snapIds[2] = { lineId, circleId };
    Eg::SyEntity* beforeSnap[2] = { nullptr, nullptr };
    size_t beforeCount = Eg::captureEntitySnapshots(&scene, snapIds, 2, beforeSnap, 2);
    ASSERT_EQ(beforeCount, 2u);

    // 修改图元
    auto* linePtr = scene.findSyEntityById(lineId);
    ASSERT_NE(linePtr, nullptr);
    linePtr->setName("Line1_Modified");

    auto* circlePtr = static_cast<Eg::SyCircle*>(scene.findSyEntityById(circleId));
    ASSERT_NE(circlePtr, nullptr);
    circlePtr->dRadius = 5.0;

    // 拍摄修改后快照
    Eg::SyEntity* afterSnap[2] = { nullptr, nullptr };
    size_t afterCount = Eg::captureEntitySnapshots(&scene, snapIds, 2, afterSnap, 2);
    ASSERT_EQ(afterCount, 2u);

    // 执行快照命令
    undoMgr.executeCommand(Eg::createEntitySnapshotsCommand(
        &scene, beforeSnap, beforeCount, afterSnap, afterCount,
        "Modify line and circle"));

    // 验证修改生效
    auto* modifiedLine = scene.findSyEntityById(lineId);
    ASSERT_NE(modifiedLine, nullptr);
    EXPECT_STREQ(modifiedLine->name(), "Line1_Modified");

    auto* modifiedCircle = static_cast<Eg::SyCircle*>(scene.findSyEntityById(circleId));
    ASSERT_NE(modifiedCircle, nullptr);
    EXPECT_DOUBLE_EQ(modifiedCircle->dRadius, 5.0);

    // 撤销
    undoMgr.undo();

    auto* undoneLine = scene.findSyEntityById(lineId);
    ASSERT_NE(undoneLine, nullptr);
    EXPECT_STREQ(undoneLine->name(), "Line1");

    auto* undoneCircle = static_cast<Eg::SyCircle*>(scene.findSyEntityById(circleId));
    ASSERT_NE(undoneCircle, nullptr);
    EXPECT_DOUBLE_EQ(undoneCircle->dRadius, 3.0);

    // 重做
    undoMgr.redo();

    auto* redoneLine = scene.findSyEntityById(lineId);
    ASSERT_NE(redoneLine, nullptr);
    EXPECT_STREQ(redoneLine->name(), "Line1_Modified");

    auto* redoneCircle = static_cast<Eg::SyCircle*>(scene.findSyEntityById(circleId));
    ASSERT_NE(redoneCircle, nullptr);
    EXPECT_DOUBLE_EQ(redoneCircle->dRadius, 5.0);
}

// ==================== 批量事务测试 ====================

TEST(UndoRedoRegressionTest, BatchTransaction_GroupedUndo)
{
    Eg::SceneManager scene;
    UndoRedoManager undoMgr(&scene);

    // 开始批量事务
    undoMgr.beginBatch("Create 3 entities");

    for (int i = 0; i < 3; ++i)
    {
        auto line = std::make_unique<Eg::SyLine>();
        line->setPointVector({ Ut::Vec2d(0.0, 0.0), Ut::Vec2d(10.0, 10.0) });
        auto cmd = std::make_unique<UndoRedoManager::AddEntityCommand>(
            &scene, std::move(line));
        undoMgr.executeCommand(std::move(cmd).release());
    }

    undoMgr.endBatch();

    EXPECT_EQ(scene.getEntityCount(), 3u);
    EXPECT_TRUE(undoMgr.canUndo());
    EXPECT_FALSE(undoMgr.canRedo());

    // 一次 undo 应撤销所有 3 个图元
    undoMgr.undo();
    EXPECT_EQ(scene.getEntityCount(), 0u);
    EXPECT_FALSE(undoMgr.canUndo());
    EXPECT_TRUE(undoMgr.canRedo());

    // 一次 redo 应恢复所有 3 个图元
    undoMgr.redo();
    EXPECT_EQ(scene.getEntityCount(), 3u);
}

TEST(UndoRedoRegressionTest, BatchTransaction_Description)
{
    Eg::SceneManager scene;
    UndoRedoManager undoMgr(&scene);

    undoMgr.beginBatch("Create 2 entities + delete 1");

    auto line = std::make_unique<Eg::SyLine>();
    line->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });
    auto cmd = std::make_unique<UndoRedoManager::AddEntityCommand>(
        &scene, std::move(line));
    undoMgr.executeCommand(std::move(cmd).release());

    undoMgr.endBatch();

    EXPECT_STREQ(undoMgr.undoText(), "Create 2 entities + delete 1");
}

// ==================== 保存点测试 ====================

TEST(UndoRedoRegressionTest, SavePoint_DetectDirty)
{
    Eg::SceneManager scene;
    UndoRedoManager undoMgr(&scene);

    // 初始状态：在保存点
    undoMgr.markSavePoint();
    EXPECT_TRUE(undoMgr.isAtSavePoint());

    // 执行命令后：不在保存点
    auto line = std::make_unique<Eg::SyLine>();
    line->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });
    auto cmd = std::make_unique<UndoRedoManager::AddEntityCommand>(
        &scene, std::move(line));
    undoMgr.executeCommand(std::move(cmd).release());
    EXPECT_FALSE(undoMgr.isAtSavePoint());

    // 撤销后：回到保存点
    undoMgr.undo();
    EXPECT_TRUE(undoMgr.isAtSavePoint());
}

// ==================== 历史上限测试 ====================

TEST(UndoRedoRegressionTest, HistoryLimit_TrimsExcess)
{
    Eg::SceneManager scene;
    UndoRedoManager undoMgr(&scene);

    // 执行超过 200 条命令
    for (int i = 0; i < 250; ++i)
    {
        auto line = std::make_unique<Eg::SyLine>();
        line->setPointVector({ Ut::Vec2d(0.0, 0.0), Ut::Vec2d(10.0, 10.0) });
        auto cmd = std::make_unique<UndoRedoManager::AddEntityCommand>(
            &scene, std::move(line));
        undoMgr.executeCommand(std::move(cmd).release());
    }

    // 历史不应超过 200
    EXPECT_LE(undoMgr.undoCount(), 200u);
}

// ==================== 清除测试 ====================

TEST(UndoRedoRegressionTest, Clear_ResetsAll)
{
    Eg::SceneManager scene;
    UndoRedoManager undoMgr(&scene);

    auto line = std::make_unique<Eg::SyLine>();
    line->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });
    auto cmd = std::make_unique<UndoRedoManager::AddEntityCommand>(
        &scene, std::move(line));
    undoMgr.executeCommand(std::move(cmd).release());

    EXPECT_TRUE(undoMgr.canUndo());

    undoMgr.clear();

    EXPECT_FALSE(undoMgr.canUndo());
    EXPECT_FALSE(undoMgr.canRedo());
    EXPECT_EQ(undoMgr.undoCount(), 0u);
    EXPECT_EQ(undoMgr.redoCount(), 0u);
}

// ==================== 描述文本测试 ====================

TEST(UndoRedoRegressionTest, DescriptionText)
{
    Eg::SceneManager scene;
    UndoRedoManager undoMgr(&scene);

    auto line = std::make_unique<Eg::SyLine>();
    line->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });
    auto cmd = std::make_unique<UndoRedoManager::AddEntityCommand>(
        &scene, std::move(line));
    undoMgr.executeCommand(std::move(cmd).release());

    EXPECT_FALSE(undoMgr.undoText()[0] == '\0');
    EXPECT_TRUE(undoMgr.redoText()[0] == '\0');

    undoMgr.undo();
    EXPECT_TRUE(undoMgr.undoText()[0] == '\0');
    EXPECT_FALSE(undoMgr.redoText()[0] == '\0');
}

// ==================== 空状态测试 ====================

TEST(UndoRedoRegressionTest, CannotUndoRedoWhenEmpty)
{
    Eg::SceneManager scene;
    UndoRedoManager undoMgr(&scene);

    EXPECT_FALSE(undoMgr.canUndo());
    EXPECT_FALSE(undoMgr.canRedo());

    // 在空栈上调用 undo/redo 不应崩溃
    undoMgr.undo();
    undoMgr.redo();

    EXPECT_FALSE(undoMgr.canUndo());
    EXPECT_FALSE(undoMgr.canRedo());
}

// ==================== 嵌套批量事务测试 ====================

TEST(UndoRedoRegressionTest, NestedBatchTransaction)
{
    Eg::SceneManager scene;
    UndoRedoManager undoMgr(&scene);

    // 单层批量事务
    undoMgr.beginBatch("Create 2 entities");

    auto line1 = std::make_unique<Eg::SyLine>();
    line1->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });
    auto cmd1 = std::make_unique<UndoRedoManager::AddEntityCommand>(
        &scene, std::move(line1));
    undoMgr.executeCommand(std::move(cmd1).release());

    auto line2 = std::make_unique<Eg::SyLine>();
    line2->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(20, 20) });
    auto cmd2 = std::make_unique<UndoRedoManager::AddEntityCommand>(
        &scene, std::move(line2));
    undoMgr.executeCommand(std::move(cmd2).release());

    undoMgr.endBatch();

    EXPECT_EQ(scene.getEntityCount(), 2u);
    EXPECT_TRUE(undoMgr.canUndo());

    undoMgr.undo();
    EXPECT_EQ(scene.getEntityCount(), 0u);
}

// ==================== Observer 通知测试 ====================

class MockUndoObserver : public IUndoRedoObserver
{
public:
    void onUndoStateChanged() override
    {
        ++stateChangedCount;
    }
    void onCanUndoChanged(bool canUndo) override
    {
        lastCanUndo = canUndo;
        ++canUndoChangedCount;
    }
    void onCanRedoChanged(bool canRedo) override
    {
        lastCanRedo = canRedo;
        ++canRedoChangedCount;
    }

    void reset()
    {
        stateChangedCount = 0;
        canUndoChangedCount = 0;
        canRedoChangedCount = 0;
        lastCanUndo = false;
        lastCanRedo = false;
    }

    int stateChangedCount = 0;
    int canUndoChangedCount = 0;
    int canRedoChangedCount = 0;
    bool lastCanUndo = false;
    bool lastCanRedo = false;
};

TEST(UndoRedoRegressionTest, Observer_NotifiedOnExecuteCommand)
{
    Eg::SceneManager scene;
    UndoRedoManager undoMgr(&scene);
    MockUndoObserver observer;
    undoMgr.addObserver(&observer);

    auto line = std::make_unique<Eg::SyLine>();
    line->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });
    auto cmd = std::make_unique<UndoRedoManager::AddEntityCommand>(
        &scene, std::move(line));
    undoMgr.executeCommand(std::move(cmd).release());

    // 执行命令后观察者应收到通知
    EXPECT_GT(observer.stateChangedCount, 0);
    EXPECT_GT(observer.canUndoChangedCount, 0);
    EXPECT_TRUE(observer.lastCanUndo);

    // 撤销后观察者应收到通知
    observer.reset();
    undoMgr.undo();
    EXPECT_GT(observer.stateChangedCount, 0);
    EXPECT_GT(observer.canRedoChangedCount, 0);
    EXPECT_TRUE(observer.lastCanRedo);

    undoMgr.removeObserver(&observer);
}

TEST(UndoRedoRegressionTest, DeleteEntitiesCommand_EmptyList)
{
    Eg::SceneManager scene;
    UndoRedoManager undoMgr(&scene);

    undoMgr.executeCommand(Eg::createDeleteEntitiesCommand(&scene, nullptr, 0, "Delete empty"));

    EXPECT_EQ(scene.getEntityCount(), 0u);
    EXPECT_TRUE(undoMgr.canUndo());

    undoMgr.undo();
    EXPECT_EQ(scene.getEntityCount(), 0u);
}

TEST(UndoRedoRegressionTest, AddEntitiesCommand_BatchAddAndUndo)
{
    Eg::SceneManager scene;
    UndoRedoManager undoMgr(&scene);

    std::vector<Eg::EntityId> ids;
    std::vector<std::unique_ptr<Eg::SyEntity>> entities;
    for (int i = 0; i < 3; ++i)
    {
        auto line = std::make_unique<Eg::SyLine>();
        line->setPointVector({ Ut::Vec2d(0.0, 0.0), Ut::Vec2d(10.0 + i, 10.0 + i) });
        ids.push_back(line->id);
        entities.push_back(std::move(line));
    }

    std::vector<const Eg::SyEntity*> rawEntities;
    rawEntities.reserve(entities.size());
    for (const auto& e : entities)
        rawEntities.push_back(e.get());

    undoMgr.executeCommand(Eg::createAddEntitiesCommand(
        &scene, rawEntities.data(), rawEntities.size(), "Add 3 lines"));

    EXPECT_EQ(scene.getEntityCount(), 3u);
    EXPECT_TRUE(undoMgr.canUndo());

    // 撤销
    undoMgr.undo();
    EXPECT_EQ(scene.getEntityCount(), 0u);
}

// ==================== 保存点边界测试 ====================

TEST(UndoRedoRegressionTest, SavePoint_AfterMultipleUndo)
{
    Eg::SceneManager scene;
    UndoRedoManager undoMgr(&scene);

    // 执行 3 条命令
    for (int i = 0; i < 3; ++i)
    {
        auto line = std::make_unique<Eg::SyLine>();
        line->setPointVector({ Ut::Vec2d(0.0, 0.0), Ut::Vec2d(10.0, 10.0) });
        auto cmd = std::make_unique<UndoRedoManager::AddEntityCommand>(
            &scene, std::move(line));
        undoMgr.executeCommand(std::move(cmd).release());
    }

    EXPECT_EQ(scene.getEntityCount(), 3u);

    // 标记保存点
    undoMgr.markSavePoint();
    EXPECT_TRUE(undoMgr.isAtSavePoint());

    // undo 1 条命令后不在保存点
    undoMgr.undo();
    EXPECT_FALSE(undoMgr.isAtSavePoint());

    // redo 后回到保存点
    undoMgr.redo();
    EXPECT_TRUE(undoMgr.isAtSavePoint());
}

TEST(UndoRedoRegressionTest, SavePoint_AfterClear)
{
    Eg::SceneManager scene;
    UndoRedoManager undoMgr(&scene);

    auto line = std::make_unique<Eg::SyLine>();
    line->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });

    undoMgr.clear();
    // after clear, stack is empty and save point index resets to default, so isAtSavePoint() returns true
    EXPECT_TRUE(undoMgr.isAtSavePoint());
}

// ==================== 历史上限边界测试 ====================

TEST(UndoRedoRegressionTest, HistoryLimit_ExactlyAtLimit)
{
    Eg::SceneManager scene;
    UndoRedoManager undoMgr(&scene);

    // 恰好执行 200 条命令
    for (int i = 0; i < 200; ++i)
    {
        auto line = std::make_unique<Eg::SyLine>();
        line->setPointVector({ Ut::Vec2d(0.0, 0.0), Ut::Vec2d(10.0, 10.0) });
        auto cmd = std::make_unique<UndoRedoManager::AddEntityCommand>(
            &scene, std::move(line));
        undoMgr.executeCommand(std::move(cmd).release());
    }

    // 恰好 200 条命令，不应被裁剪
    EXPECT_EQ(undoMgr.undoCount(), 200u);
    EXPECT_EQ(scene.getEntityCount(), 200u);
}

// ==================== 空 Batch 边界测试 ====================

TEST(UndoRedoRegressionTest, Batch_EmptyBatchNoCrash)
{
    Eg::SceneManager scene;
    UndoRedoManager undoMgr(&scene);

    undoMgr.beginBatch("Empty batch");
    undoMgr.endBatch();

    // 空 batch 不应产生 undo 条目
    EXPECT_FALSE(undoMgr.canUndo());
}

TEST(UndoRedoRegressionTest, Batch_UndoRedoStackState)
{
    Eg::SceneManager scene;
    UndoRedoManager undoMgr(&scene);

    undoMgr.beginBatch("Batch with 2 entities");

    auto line1 = std::make_unique<Eg::SyLine>();
    line1->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });
    auto cmd1 = std::make_unique<UndoRedoManager::AddEntityCommand>(
        &scene, std::move(line1));
    undoMgr.executeCommand(std::move(cmd1).release());

    auto line2 = std::make_unique<Eg::SyLine>();
    line2->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(20, 20) });
    auto cmd2 = std::make_unique<UndoRedoManager::AddEntityCommand>(
        &scene, std::move(line2));
    undoMgr.executeCommand(std::move(cmd2).release());

    undoMgr.endBatch();

    EXPECT_EQ(scene.getEntityCount(), 2u);
    EXPECT_TRUE(undoMgr.canUndo());
    EXPECT_FALSE(undoMgr.canRedo());

    undoMgr.undo();
    EXPECT_EQ(scene.getEntityCount(), 0u);
    EXPECT_FALSE(undoMgr.canUndo());
    EXPECT_TRUE(undoMgr.canRedo());

    undoMgr.redo();
    EXPECT_EQ(scene.getEntityCount(), 2u);
    EXPECT_TRUE(undoMgr.canUndo());
    EXPECT_FALSE(undoMgr.canRedo());
}

// ==================== EntitySnapshotsCommand 合并测试 ====================

TEST(UndoRedoRegressionTest, EntitySnapshots_MergeTwoCommands)
{
    Eg::SceneManager scene;
    UndoRedoManager undoMgr(&scene);

    auto line = std::make_unique<Eg::SyLine>();
    line->setName("V1");

    line->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });
    Eg::EntityId lineId = line->id;

    std::vector<std::unique_ptr<Eg::SyEntity>> vec;
    vec.push_back(std::move(line));
    scene.addEntities(std::move(vec));

    // 快照 V1 → V2
    Eg::EntityId snapId[1] = { lineId };
    Eg::SyEntity* beforeSnap1[1] = { nullptr };
    size_t beforeCount1 = Eg::captureEntitySnapshots(&scene, snapId, 1, beforeSnap1, 1);

    auto* l1 = scene.findSyEntityById(lineId);
    l1->setName("V2");

    Eg::SyEntity* afterSnap1[1] = { nullptr };
    size_t afterCount1 = Eg::captureEntitySnapshots(&scene, snapId, 1, afterSnap1, 1);

    auto* snap1 = Eg::createEntitySnapshotsCommand(
        &scene, beforeSnap1, beforeCount1, afterSnap1, afterCount1, "snap");
    undoMgr.executeCommand(snap1);
    EXPECT_STREQ(scene.findSyEntityById(lineId)->name(), "V2");

    // 快照 V2 → V3，使用相同 description 以触发合并
    Eg::SyEntity* beforeSnap2[1] = { nullptr };
    size_t beforeCount2 = Eg::captureEntitySnapshots(&scene, snapId, 1, beforeSnap2, 1);

    auto* l2 = scene.findSyEntityById(lineId);
    l2->setName("V3");

    Eg::SyEntity* afterSnap2[1] = { nullptr };
    size_t afterCount2 = Eg::captureEntitySnapshots(&scene, snapId, 1, afterSnap2, 1);

    auto* snap2 = Eg::createEntitySnapshotsCommand(
        &scene, beforeSnap2, beforeCount2, afterSnap2, afterCount2, "snap");
    snap2->execute();
    undoMgr.pushExecutedCommand(snap2);
    EXPECT_STREQ(scene.findSyEntityById(lineId)->name(), "V3");

    // 合并后一次 undo 回到 V1
    undoMgr.undo();
    EXPECT_STREQ(scene.findSyEntityById(lineId)->name(), "V1");
}

// ==================== 图元属性变更测试 ====================

TEST(UndoRedoRegressionTest, PropertyChange_Visibility)
{
    Eg::SceneManager scene;
    UndoRedoManager undoMgr(&scene);

    auto line = std::make_unique<Eg::SyLine>();
    line->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });
    line->setVisible(true);
    auto* rawLine = line.get();

    std::vector<std::unique_ptr<Eg::SyEntity>> vec;
    vec.push_back(std::move(line));
    scene.addEntities(std::move(vec));

    auto cmd = std::make_unique<UndoRedoManager::ModifyEntityCommand>(
        &scene, rawLine,
        [](Eg::SyEntity* e) { e->setVisible(false); },
        [](Eg::SyEntity* e) { e->setVisible(true); },
        "Toggle visibility");
    undoMgr.executeCommand(std::move(cmd).release());
    EXPECT_FALSE(rawLine->visible());

    undoMgr.undo();
    EXPECT_TRUE(rawLine->visible());

    undoMgr.redo();
    EXPECT_FALSE(rawLine->visible());
}

TEST(UndoRedoRegressionTest, PropertyChange_LockedState)
{
    Eg::SceneManager scene;
    UndoRedoManager undoMgr(&scene);

    auto line = std::make_unique<Eg::SyLine>();
    line->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });
    line->setLocked(false);
    auto* rawLine = line.get();

    std::vector<std::unique_ptr<Eg::SyEntity>> vec;
    vec.push_back(std::move(line));
    scene.addEntities(std::move(vec));

    auto cmd = std::make_unique<UndoRedoManager::ModifyEntityCommand>(
        &scene, rawLine,
        [](Eg::SyEntity* e) { e->setLocked(true); },
        [](Eg::SyEntity* e) { e->setLocked(false); },
        "Toggle locked");
    undoMgr.executeCommand(std::move(cmd).release());
    EXPECT_TRUE(rawLine->locked());

    undoMgr.undo();
    EXPECT_FALSE(rawLine->locked());

    undoMgr.redo();
    EXPECT_TRUE(rawLine->locked());
}

TEST(UndoRedoRegressionTest, PropertyChange_SelectionState)
{
    Eg::SceneManager scene;
    UndoRedoManager undoMgr(&scene);

    auto line = std::make_unique<Eg::SyLine>();
    line->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });
    line->setSelected(false);
    auto* rawLine = line.get();

    std::vector<std::unique_ptr<Eg::SyEntity>> vec;
    vec.push_back(std::move(line));
    scene.addEntities(std::move(vec));

    auto cmd = std::make_unique<UndoRedoManager::ModifyEntityCommand>(
        &scene, rawLine,
        [](Eg::SyEntity* e) { e->setSelected(true); },
        [](Eg::SyEntity* e) { e->setSelected(false); },
        "Toggle selection");
    undoMgr.executeCommand(std::move(cmd).release());
    EXPECT_TRUE(rawLine->selected());

    undoMgr.undo();
    EXPECT_FALSE(rawLine->selected());

    undoMgr.redo();
    EXPECT_TRUE(rawLine->selected());
}

// ==================== 混合实体类型批量测试 ====================

TEST(UndoRedoRegressionTest, Batch_MixedEntityTypes)
{
    Eg::SceneManager scene;
    UndoRedoManager undoMgr(&scene);

    undoMgr.beginBatch("Create mixed entities");

    auto line = std::make_unique<Eg::SyLine>();
    line->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });
    Eg::EntityId lineId = line->id;
    undoMgr.executeCommand(std::make_unique<UndoRedoManager::AddEntityCommand>(
        &scene, std::move(line)).release());

    auto circle = std::make_unique<Eg::SyCircle>();
    circle->basePoint = Ut::Vec2d(5, 5);
    circle->dRadius = 3.0;
    Eg::EntityId circleId = circle->id;
    undoMgr.executeCommand(std::make_unique<UndoRedoManager::AddEntityCommand>(
        &scene, std::move(circle)).release());

    undoMgr.endBatch();

    EXPECT_EQ(scene.getEntityCount(), 2u);

    undoMgr.undo();
    EXPECT_EQ(scene.getEntityCount(), 0u);

    undoMgr.redo();
    EXPECT_EQ(scene.getEntityCount(), 2u);
    EXPECT_NE(scene.findSyEntityById(lineId), nullptr);
    EXPECT_NE(scene.findSyEntityById(circleId), nullptr);
}

TEST(UndoRedoRegressionTest, AddEntityCommand_PreservesNameAndSelectionState)
{
    Eg::SceneManager scene;
    UndoRedoManager undoMgr(&scene);

    auto line = std::make_unique<Eg::SyLine>();
    line->setName("NamedLine");

    line->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });
    Eg::EntityId entityId = line->id;

    auto cmd = std::make_unique<UndoRedoManager::AddEntityCommand>(&scene, std::move(line));
    undoMgr.executeCommand(cmd.release());

    auto* added = scene.findSyEntityById(entityId);
    ASSERT_NE(added, nullptr);
    EXPECT_STREQ(added->name(), "NamedLine");
    EXPECT_FALSE(added->selected());

    undoMgr.undo();
    EXPECT_EQ(scene.findSyEntityById(entityId), nullptr);

    undoMgr.redo();
    added = scene.findSyEntityById(entityId);
    ASSERT_NE(added, nullptr);
    EXPECT_STREQ(added->name(), "NamedLine");
    EXPECT_FALSE(added->selected());
}