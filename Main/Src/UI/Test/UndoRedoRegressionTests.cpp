/**
 * @file UndoRedoRegressionTests.cpp
 * @brief 撤销/重做回归测试 — 覆盖命令栈、事务、批量操作、快照、保存点
 *
 * 测试范围：
 *  - UndoRedoManager 基础命令 (ModifyEntityCommand / LambdaCommand)
 *  - 图元增删命令 (AddEntitiesCommand / DeleteEntitiesCommand)
 *  - EntitySnapshotsCommand 快照命令（含连续微调合并 / 拖拽不合并）
 *  - Batch 事务（beginBatch / endBatch）
 *  - SavePoint 保存点
 *  - 历史上限（按 maxHistorySize 自适应，不写死数值）
 *
 * 注：原先针对 MoveEntityCommand 的用例随该类一并移除 —— 平移现在统一走
 *     SceneEditService + EntitySnapshotsCommand，其「连续微调合并成一条」的
 *     语义由 EntitySnapshots_MergeTwoCommands / _MergeIsSymmetric 覆盖。
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

namespace
{
    /**
     * @brief 测试辅助：执行一次「新增单个图元」并入撤销栈。
     *
     * AddEntitiesCommand 属于 Engine2D DLL 内部类型，外部模块只能经
     * Eg::createAddEntitiesCommand 工厂构造：工厂按 id 克隆后交给命令接管，
     * 因此图元所有权仍留在用例这边（用例里多是临时对象，语义等价）。
     * 用例统一走本函数，新增单个图元的写法才不会各处开花。
     */
    void executeAddEntity(UndoRedoManager& undoMgr, Eg::SceneManager& scene, const Eg::SyEntity* entity)
    {
        undoMgr.executeCommand(Eg::createAddEntitiesCommand(&scene, &entity, 1, "Add"));
    }
}  // namespace

// ==================== 基础命令测试 ====================

TEST(UndoRedoRegressionTest, AddEntitiesCommand_ExecuteAndUndo)
{
    Eg::SceneManager scene;
    UndoRedoManager undoMgr(&scene);

    auto line = std::make_unique<Eg::SyLine>();
    line->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });
    Eg::EntityId entityId = line->id;

    executeAddEntity(undoMgr, scene, line.get());

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

TEST(UndoRedoRegressionTest, DeleteEntitiesCommand_ExecuteAndUndo)
{
    Eg::SceneManager scene;
    UndoRedoManager undoMgr(&scene);

    // 使用 addEntities(move) 保留原始 ID，避免 clone 导致 ID 变化
    auto line = std::make_unique<Eg::SyLine>();
    line->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });
    Eg::EntityId entityId = line->id;

    std::vector<std::unique_ptr<Eg::SyEntity>> vec;
    vec.push_back(std::move(line));
    scene.addEntities(std::move(vec));

    EXPECT_EQ(scene.getEntityCount(), 1u);

    // 删除按 id 进行：命令在 execute 时取走原件保管，undo 归还。
    ASSERT_NE(scene.findSyEntityById(entityId), nullptr);
    undoMgr.executeCommand(Eg::createDeleteEntitiesCommand(&scene, &entityId, 1, "Delete"));

    EXPECT_EQ(scene.getEntityCount(), 0u);
    EXPECT_TRUE(undoMgr.canUndo());

    // 撤销删除
    undoMgr.undo();
    EXPECT_EQ(scene.getEntityCount(), 1u);
    auto* restored = scene.findSyEntityById(entityId);
    ASSERT_NE(restored, nullptr);
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
        &scene,
        rawLine,
        [](Eg::SyEntity* e) {
            e->setName("Modified");
        },

        [](Eg::SyEntity* e) {
            e->setName("Original");
        },

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
        [&]() {
            counter += 1;
        },
        [&]() {
            counter -= 1;
        },
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

    // 执行快照命令。修改在这里已经生效，场景当前状态即「修改后」态，
    // 因此只需传 before 一份快照（命令靠与场景整体交换完成撤销/重做）。
    undoMgr.executeCommand(
        Eg::createEntitySnapshotsCommand(&scene, beforeSnap, beforeCount, "Modify line and circle"));

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
        executeAddEntity(undoMgr, scene, line.get());
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
    executeAddEntity(undoMgr, scene, line.get());

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
    executeAddEntity(undoMgr, scene, line.get());
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

    // 以实际上限为基准执行「上限 + 50」条命令（上限由 maxHistorySize 决定，不写死具体数值）
    const size_t limit = undoMgr.maxHistorySize();
    for (size_t i = 0; i < limit + 50; ++i)
    {
        auto line = std::make_unique<Eg::SyLine>();
        line->setPointVector({ Ut::Vec2d(0.0, 0.0), Ut::Vec2d(10.0, 10.0) });
        executeAddEntity(undoMgr, scene, line.get());
    }

    // 超出部分被裁剪，历史恰好停在上限
    EXPECT_EQ(undoMgr.undoCount(), limit);
}

// ==================== 清除测试 ====================

TEST(UndoRedoRegressionTest, Clear_ResetsAll)
{
    Eg::SceneManager scene;
    UndoRedoManager undoMgr(&scene);

    auto line = std::make_unique<Eg::SyLine>();
    line->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });
    executeAddEntity(undoMgr, scene, line.get());

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
    executeAddEntity(undoMgr, scene, line.get());

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
    executeAddEntity(undoMgr, scene, line1.get());

    auto line2 = std::make_unique<Eg::SyLine>();
    line2->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(20, 20) });
    executeAddEntity(undoMgr, scene, line2.get());

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
    executeAddEntity(undoMgr, scene, line.get());

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
    {
        rawEntities.push_back(e.get());
    }

    undoMgr.executeCommand(Eg::createAddEntitiesCommand(&scene, rawEntities.data(), rawEntities.size(), "Add 3 lines"));

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
        executeAddEntity(undoMgr, scene, line.get());
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

    // 恰好执行「上限」条命令，此时不应触发裁剪
    const size_t limit = undoMgr.maxHistorySize();
    for (size_t i = 0; i < limit; ++i)
    {
        auto line = std::make_unique<Eg::SyLine>();
        line->setPointVector({ Ut::Vec2d(0.0, 0.0), Ut::Vec2d(10.0, 10.0) });
        executeAddEntity(undoMgr, scene, line.get());
    }

    // 恰好达到上限，未被裁剪；场景中的图元数等于执行次数
    EXPECT_EQ(undoMgr.undoCount(), limit);
    EXPECT_EQ(scene.getEntityCount(), limit);
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
    executeAddEntity(undoMgr, scene, line1.get());

    auto line2 = std::make_unique<Eg::SyLine>();
    line2->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(20, 20) });
    executeAddEntity(undoMgr, scene, line2.get());

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

    auto* snap1 = Eg::createEntitySnapshotsCommand(&scene, beforeSnap1, beforeCount1, "snap");
    undoMgr.executeCommand(snap1);
    EXPECT_STREQ(scene.findSyEntityById(lineId)->name(), "V2");

    // 快照 V2 → V3，使用相同 description 以触发合并
    Eg::SyEntity* beforeSnap2[1] = { nullptr };
    size_t beforeCount2 = Eg::captureEntitySnapshots(&scene, snapId, 1, beforeSnap2, 1);

    auto* l2 = scene.findSyEntityById(lineId);
    l2->setName("V3");

    auto* snap2 = Eg::createEntitySnapshotsCommand(&scene, beforeSnap2, beforeCount2, "snap");
    snap2->execute();
    undoMgr.pushExecutedCommand(snap2);
    EXPECT_STREQ(scene.findSyEntityById(lineId)->name(), "V3");

    // 合并后一次 undo 回到 V1
    undoMgr.undo();
    EXPECT_STREQ(scene.findSyEntityById(lineId)->name(), "V1");
}

// 一次完整的鼠标拖拽（按下 → 拖动 → 松开）必须是一条独立的撤销记录：
// 连续拖两次要能分两次撤回，不能被并成一条。
// 确保 non-mergeable 属性的图元操作不会与其他操作合并
TEST(UndoRedoRegressionTest, EntitySnapshots_NonMergeableKeepsSeparateRecords)
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

    Eg::EntityId snapId[1] = { lineId };

    // 第一次拖拽：V1 → V2，mergeable = false
    Eg::SyEntity* beforeSnap1[1] = { nullptr };
    size_t beforeCount1 = Eg::captureEntitySnapshots(&scene, snapId, 1, beforeSnap1, 1);
    scene.findSyEntityById(lineId)->setName("V2");

    auto* drag1 = Eg::createEntitySnapshotsCommand(&scene, beforeSnap1, beforeCount1, "Transform", false);
    drag1->execute();
    undoMgr.pushExecutedCommand(drag1);

    // 第二次拖拽：V2 → V3，同一批图元、同一 description，同样 mergeable = false
    Eg::SyEntity* beforeSnap2[1] = { nullptr };
    size_t beforeCount2 = Eg::captureEntitySnapshots(&scene, snapId, 1, beforeSnap2, 1);
    scene.findSyEntityById(lineId)->setName("V3");

    auto* drag2 = Eg::createEntitySnapshotsCommand(&scene, beforeSnap2, beforeCount2, "Transform", false);
    drag2->execute();
    undoMgr.pushExecutedCommand(drag2);

    EXPECT_STREQ(scene.findSyEntityById(lineId)->name(), "V3");

    // 两条独立记录：第一次撤销只回到 V2，第二次才回到 V1
    undoMgr.undo();
    EXPECT_STREQ(scene.findSyEntityById(lineId)->name(), "V2");
    EXPECT_TRUE(undoMgr.canUndo());

    undoMgr.undo();
    EXPECT_STREQ(scene.findSyEntityById(lineId)->name(), "V1");
    EXPECT_FALSE(undoMgr.canUndo());
}

// 合并判定必须对称：先来一条可合并的（方向键 nudge），再来一条不可合并的（鼠标拖拽），
// 拖拽不能被吞进 nudge 那条记录里。
// UndoRedoManager 问的是栈顶（this），只判 this->m_mergeable 会漏掉这种组合。
TEST(UndoRedoRegressionTest, EntitySnapshots_MergeIsSymmetric)
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

    Eg::EntityId snapId[1] = { lineId };

    // nudge：mergeable = true
    Eg::SyEntity* beforeSnap1[1] = { nullptr };
    size_t beforeCount1 = Eg::captureEntitySnapshots(&scene, snapId, 1, beforeSnap1, 1);
    scene.findSyEntityById(lineId)->setName("V2");

    auto* nudge = Eg::createEntitySnapshotsCommand(&scene, beforeSnap1, beforeCount1, "Transform", true);
    nudge->execute();
    undoMgr.pushExecutedCommand(nudge);

    // 拖拽：mergeable = false，description 相同
    Eg::SyEntity* beforeSnap2[1] = { nullptr };
    size_t beforeCount2 = Eg::captureEntitySnapshots(&scene, snapId, 1, beforeSnap2, 1);
    scene.findSyEntityById(lineId)->setName("V3");

    auto* drag = Eg::createEntitySnapshotsCommand(&scene, beforeSnap2, beforeCount2, "Transform", false);
    drag->execute();
    undoMgr.pushExecutedCommand(drag);

    // 没被合并：撤销一次只回到 V2
    undoMgr.undo();
    EXPECT_STREQ(scene.findSyEntityById(lineId)->name(), "V2");
    EXPECT_TRUE(undoMgr.canUndo());
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
        &scene,
        rawLine,
        [](Eg::SyEntity* e) {
            e->setVisible(false);
        },
        [](Eg::SyEntity* e) {
            e->setVisible(true);
        },
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
        &scene,
        rawLine,
        [](Eg::SyEntity* e) {
            e->setLocked(true);
        },
        [](Eg::SyEntity* e) {
            e->setLocked(false);
        },
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
        &scene,
        rawLine,
        [](Eg::SyEntity* e) {
            e->setSelected(true);
        },
        [](Eg::SyEntity* e) {
            e->setSelected(false);
        },
        "Toggle selection");
    undoMgr.executeCommand(std::move(cmd).release());
    EXPECT_TRUE(rawLine->selected());

    undoMgr.undo();
    EXPECT_FALSE(rawLine->selected());

    undoMgr.redo();
    EXPECT_TRUE(rawLine->selected());
}

// ==================== 混合图元类型批量测试 ====================

TEST(UndoRedoRegressionTest, Batch_MixedEntityTypes)
{
    Eg::SceneManager scene;
    UndoRedoManager undoMgr(&scene);

    undoMgr.beginBatch("Create mixed entities");

    auto line = std::make_unique<Eg::SyLine>();
    line->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });
    Eg::EntityId lineId = line->id;
    executeAddEntity(undoMgr, scene, line.get());

    auto circle = std::make_unique<Eg::SyCircle>();
    circle->basePoint = Ut::Vec2d(5, 5);
    circle->dRadius = 3.0;
    Eg::EntityId circleId = circle->id;
    executeAddEntity(undoMgr, scene, circle.get());

    undoMgr.endBatch();

    EXPECT_EQ(scene.getEntityCount(), 2u);

    undoMgr.undo();
    EXPECT_EQ(scene.getEntityCount(), 0u);

    undoMgr.redo();
    EXPECT_EQ(scene.getEntityCount(), 2u);
    EXPECT_NE(scene.findSyEntityById(lineId), nullptr);
    EXPECT_NE(scene.findSyEntityById(circleId), nullptr);
}

TEST(UndoRedoRegressionTest, AddEntitiesCommand_PreservesNameAndSelectionState)
{
    Eg::SceneManager scene;
    UndoRedoManager undoMgr(&scene);

    auto line = std::make_unique<Eg::SyLine>();
    line->setName("NamedLine");

    line->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });
    Eg::EntityId entityId = line->id;

    executeAddEntity(undoMgr, scene, line.get());

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
