/**
 * @file SceneEditIntegrationTests.cpp
 * @brief 场景编辑集成回归测试 — 覆盖「撤销命令零克隆状态交换」的端到端链路
 *
 * 测试范围（函数级单测覆盖不到、只靠编译保证的那几条集成链路）：
 *  - 1. transformEntities        —— 菜单/对话框变换（移动/旋转/对齐/镜像）的统一入口
 *  - 2. 合并语义                  —— 连按微调合成一条记录，两次独立操作各留一条
 *  - 3. beginInteractive/commit  —— Gizmo 拖动（按下 → 多帧改几何 → 松开）
 *  - 4. beginInteractive/cancel  —— 拖动中取消（Esc）
 *  - 5. pushExecutedChange       —— 属性面板编辑、全场景算法（偏移/布尔）包装
 *  - 6. 群组归属在状态交换后是否存活
 *  - 7. 全场景快照 + 场景内被删图元的回插（replaceEntitiesById 的 insert 分支）
 *  - 8. DocumentTransaction 的 commit/undo/redo 往返
 *  - 9. 子集作用域不得误删无关图元（SnapshotScope::Subset 守卫）
 *
 * 这些链路此前的覆盖为空：改动若破坏「场景持有一种状态、命令持有另一种状态」
 * 的不变量，编译不会报错，只会在用户撤销时丢图元或数量翻倍，故固化为常态用例。
 */

#include <gtest/gtest.h>

#include "Engine2D/Edit/SceneEditService.h"
#include "Engine2D/Edit/UndoRedoManager.h"
#include "Engine2D/Core/SceneManager.h"
#include "Engine2D/Core/DocumentTransaction.h"
#include "Engine2D/SyEntity/SyLine.h"
#include "Engine2D/SyEntity/SyCircle.h"

#include <Ut/Vec.h>
#include <Ut/Mat.h>
#include <Ut/BBox2d.h>

#include <memory>
#include <string>
#include <vector>

namespace
{
    /// 图元包围盒的最小 X，取不到时返回一个不可能值（让断言明确失败）
    double bboxMinX(Eg::SceneManager& scene, Eg::EntityId id)
    {
        Eg::SyEntity* entity = scene.findSyEntityById(id);
        return entity ? entity->getBbox().minPt.x() : -1.0e30;
    }

    bool nearly(double a, double b)
    {
        const double d = a - b;
        return d < 1e-6 && d > -1e-6;
    }

    /// 造一条直线并直接入场景，返回其 id
    Eg::EntityId addLine(Eg::SceneManager& scene, double x0, double y0, double x1, double y1)
    {
        auto line = std::make_unique<Eg::SyLine>();
        line->setPointVector({ Ut::Vec2d(x0, y0), Ut::Vec2d(x1, y1) });
        const Eg::EntityId id = line->id;
        std::vector<std::unique_ptr<Eg::SyEntity>> vec;
        vec.push_back(std::move(line));
        scene.addEntities(std::move(vec));
        return id;
    }
}  // namespace

// ==================== 1. transformEntities 往返 ====================

TEST(SceneEditIntegrationTest, TransformEntities_RoundTrip)
{
    Eg::SceneManager scene;
    UndoRedoManager undoMgr(&scene);
    SceneEditService edit(&scene, &undoMgr);

    const Eg::EntityId a = addLine(scene, 0, 0, 10, 0);
    const Eg::EntityId b = addLine(scene, 0, 5, 10, 5);
    EXPECT_TRUE(scene.getEntityCount() == 2) << "scene has 2 entities";
    EXPECT_TRUE(nearly(bboxMinX(scene, a), 0.0)) << "line A starts at x=0";

    edit.transformEntities(
        { a, b },
        [&]() {
            const Ut::Mat3d mat = Ut::Mat3d::translate(10.0, 0.0);
            for (Eg::EntityId id : { a, b })
            {
                if (auto* e = scene.findSyEntityById(id))
                {
                    e->transform(mat);
                }
            }
        },
        "Move");

    EXPECT_TRUE(nearly(bboxMinX(scene, a), 10.0)) << "after transform x=10";
    EXPECT_TRUE(undoMgr.undoCount() == 1) << "one undo record";
    EXPECT_TRUE(scene.getEntityCount() == 2) << "count stable after transform";

    undoMgr.undo();
    EXPECT_TRUE(nearly(bboxMinX(scene, a), 0.0)) << "undo restores x=0";
    EXPECT_TRUE(scene.getEntityCount() == 2) << "count stable after undo (no duplicate)";
    EXPECT_TRUE(nearly(bboxMinX(scene, b), 0.0)) << "undo restores line B too";

    undoMgr.redo();
    EXPECT_TRUE(nearly(bboxMinX(scene, a), 10.0)) << "redo restores x=10";

    undoMgr.undo();
    EXPECT_TRUE(nearly(bboxMinX(scene, a), 0.0)) << "2nd undo restores x=0 (ping-pong stable)";
    undoMgr.redo();
    EXPECT_TRUE(nearly(bboxMinX(scene, a), 10.0)) << "2nd redo restores x=10";
}

// ==================== 2. 合并语义 ====================

TEST(SceneEditIntegrationTest, TransformEntities_MergeableNudgeMerges)
{
    Eg::SceneManager scene;
    UndoRedoManager undoMgr(&scene);
    SceneEditService edit(&scene, &undoMgr);

    const Eg::EntityId a = addLine(scene, 0, 0, 10, 0);

    auto nudge = [&](double dx) {
        edit.transformEntities(
            { a },
            [&]() {
                if (auto* e = scene.findSyEntityById(a))
                {
                    e->transform(Ut::Mat3d::translate(dx, 0.0));
                }
            },
            "Nudge",
            true);
    };
    nudge(5.0);
    nudge(5.0);
    EXPECT_TRUE(nearly(bboxMinX(scene, a), 10.0)) << "two nudges accumulate to x=10";
    EXPECT_TRUE(undoMgr.undoCount() == 1) << "merged into one undo record";
    undoMgr.undo();
    EXPECT_TRUE(nearly(bboxMinX(scene, a), 0.0)) << "single undo returns to x=0";
}

TEST(SceneEditIntegrationTest, TransformEntities_NonMergeableKeepsSeparateRecords)
{
    Eg::SceneManager scene;
    UndoRedoManager undoMgr(&scene);
    SceneEditService edit(&scene, &undoMgr);

    const Eg::EntityId a = addLine(scene, 0, 0, 10, 0);

    auto move = [&](double dx) {
        edit.transformEntities(
            { a },
            [&]() {
                if (auto* e = scene.findSyEntityById(a))
                {
                    e->transform(Ut::Mat3d::translate(dx, 0.0));
                }
            },
            "Move",
            false);
    };
    move(5.0);
    move(5.0);
    EXPECT_TRUE(undoMgr.undoCount() == 2) << "two separate undo records";
    undoMgr.undo();
    EXPECT_TRUE(nearly(bboxMinX(scene, a), 5.0)) << "1st undo returns to x=5";
    undoMgr.undo();
    EXPECT_TRUE(nearly(bboxMinX(scene, a), 0.0)) << "2nd undo returns to x=0";
}

// ==================== 3. 交互拖动 ====================

TEST(SceneEditIntegrationTest, InteractiveDrag_Commit)
{
    Eg::SceneManager scene;
    UndoRedoManager undoMgr(&scene);
    SceneEditService edit(&scene, &undoMgr);

    const Eg::EntityId a = addLine(scene, 0, 0, 10, 0);
    std::vector<Eg::SyEntity*> targets{ scene.findSyEntityById(a) };

    SceneEditService::InteractiveSession session = edit.beginInteractive(targets);
    EXPECT_TRUE(session.active()) << "session active after begin";
    EXPECT_TRUE(undoMgr.undoCount() == 0) << "begin does not push undo";

    // 模拟拖动期每帧只改几何、不入栈
    for (int frame = 0; frame < 3; ++frame)
    {
        if (auto* e = scene.findSyEntityById(a))
        {
            e->transform(Ut::Mat3d::translate(2.0, 0.0));
        }
    }
    EXPECT_TRUE(nearly(bboxMinX(scene, a), 6.0)) << "drag frames moved geometry";
    EXPECT_TRUE(undoMgr.undoCount() == 0) << "drag frames do not push undo";

    edit.commitInteractive(session, "Transform", false);
    EXPECT_TRUE(nearly(bboxMinX(scene, a), 6.0)) << "state kept after commit";
    EXPECT_TRUE(undoMgr.undoCount() == 1) << "commit pushes exactly one undo record";

    undoMgr.undo();
    EXPECT_TRUE(nearly(bboxMinX(scene, a), 0.0)) << "undo restores pre-drag state";
    undoMgr.redo();
    EXPECT_TRUE(nearly(bboxMinX(scene, a), 6.0)) << "redo restores post-drag state";
}

TEST(SceneEditIntegrationTest, InteractiveDrag_CancelRestoresWithoutUndoRecord)
{
    Eg::SceneManager scene;
    UndoRedoManager undoMgr(&scene);
    SceneEditService edit(&scene, &undoMgr);

    const Eg::EntityId a = addLine(scene, 0, 0, 10, 0);
    std::vector<Eg::SyEntity*> targets{ scene.findSyEntityById(a) };

    SceneEditService::InteractiveSession session = edit.beginInteractive(targets);
    if (auto* e = scene.findSyEntityById(a))
    {
        e->transform(Ut::Mat3d::translate(7.0, 0.0));
    }
    EXPECT_TRUE(nearly(bboxMinX(scene, a), 7.0)) << "geometry changed during drag";

    edit.cancelInteractive(session);
    EXPECT_TRUE(nearly(bboxMinX(scene, a), 0.0)) << "cancel restores original state";
    EXPECT_TRUE(!session.active()) << "session released after cancel";
    EXPECT_TRUE(undoMgr.undoCount() == 0) << "cancel pushes no undo record";
    EXPECT_TRUE(scene.getEntityCount() == 1) << "count stable after cancel";
}

// ==================== 4. pushExecutedChange 路径 ====================

TEST(SceneEditIntegrationTest, PushExecutedChange_PropertyPanelPath)
{
    Eg::SceneManager scene;
    UndoRedoManager undoMgr(&scene);
    SceneEditService edit(&scene, &undoMgr);

    const Eg::EntityId a = addLine(scene, 0, 0, 10, 0);
    const Eg::EntityId b = addLine(scene, 0, 5, 10, 5);

    auto before = edit.captureAllSnapshots();
    EXPECT_TRUE(before.size() == 2) << "captured full-scene before snapshots";

    scene.findSyEntityById(a)->setName("RenamedA");
    scene.findSyEntityById(b)->setName("RenamedB");
    edit.pushExecutedChange(std::move(before), "Edit properties");

    EXPECT_TRUE(std::string(scene.findSyEntityById(a)->name()) == "RenamedA") << "change applied";
    EXPECT_TRUE(undoMgr.undoCount() == 1) << "one undo record pushed";

    undoMgr.undo();
    EXPECT_TRUE(scene.findSyEntityById(a) != nullptr) << "entity A still resolvable after undo";
    EXPECT_TRUE(std::string(scene.findSyEntityById(a)->name()) != "RenamedA") << "undo restored old name";
    EXPECT_TRUE(scene.getEntityCount() == 2) << "count stable after undo";

    undoMgr.redo();
    EXPECT_TRUE(std::string(scene.findSyEntityById(a)->name()) == "RenamedA") << "redo re-applies change";
}

// ==================== 5. 群组归属存活 ====================

TEST(SceneEditIntegrationTest, GroupMembership_SurvivesStateSwap)
{
    Eg::SceneManager scene;
    UndoRedoManager undoMgr(&scene);
    SceneEditService edit(&scene, &undoMgr);

    const Eg::EntityId a = addLine(scene, 0, 0, 10, 0);
    const Eg::EntityId b = addLine(scene, 0, 5, 10, 5);

    edit.groupEntities({ a, b }, "G1");
    EXPECT_TRUE(scene.findSyEntityById(a)->group() != nullptr) << "entity A belongs to a group";

    edit.transformEntities(
        { a, b },
        [&]() {
            if (auto* e = scene.findSyEntityById(a))
            {
                e->transform(Ut::Mat3d::translate(3.0, 0.0));
            }
        },
        "Move grouped");

    EXPECT_TRUE(scene.findSyEntityById(a)->group() != nullptr) << "group kept after transform";
    undoMgr.undo();
    EXPECT_TRUE(scene.findSyEntityById(a) != nullptr) << "entity A resolvable after undo";
    EXPECT_TRUE(scene.findSyEntityById(a)->group() != nullptr) << "group kept after undo (swap preserves it)";
    EXPECT_TRUE(nearly(bboxMinX(scene, a), 0.0)) << "geometry restored with group intact";
}

// ==================== 6. 全场景快照 + 回插分支 ====================

TEST(SceneEditIntegrationTest, WholeSceneSnapshot_RestoresEntityRemovedInsideAlgorithm)
{
    Eg::SceneManager scene;
    UndoRedoManager undoMgr(&scene);
    SceneEditService edit(&scene, &undoMgr);

    const Eg::EntityId a = addLine(scene, 0, 0, 10, 0);
    const Eg::EntityId b = addLine(scene, 0, 5, 10, 5);
    const Eg::EntityId c = addLine(scene, 0, 9, 10, 9);

    auto before = edit.captureAllSnapshots();
    EXPECT_TRUE(before.size() == 3) << "captured 3 before snapshots";

    // 模拟算法在场景内部直接删掉一个图元（偏移/布尔等就会这么做）
    scene.extractEntitiesById({ b });
    EXPECT_TRUE(scene.getEntityCount() == 2) << "algorithm removed one entity";

    edit.pushExecutedChange(std::move(before), "Algorithm offset", false, Eg::SnapshotScope::WholeScene);
    undoMgr.undo();

    EXPECT_TRUE(scene.getEntityCount() == 3) << "undo restored the removed entity (insert branch)";
    EXPECT_TRUE(scene.findSyEntityById(b) != nullptr) << "restored entity B resolvable";
    EXPECT_TRUE(scene.findSyEntityById(a) != nullptr) << "entity A intact";
    EXPECT_TRUE(scene.findSyEntityById(c) != nullptr) << "entity C intact";

    undoMgr.redo();
    EXPECT_TRUE(scene.getEntityCount() == 2) << "redo re-applies the removal (WholeScene scope)";
    EXPECT_TRUE(scene.findSyEntityById(b) == nullptr) << "removed entity B stays gone after redo";

    undoMgr.undo();
    EXPECT_TRUE(scene.getEntityCount() == 3) << "2nd undo restores again (ping-pong stable)";
    undoMgr.redo();
    EXPECT_TRUE(scene.getEntityCount() == 2) << "2nd redo removes again";
}

// ==================== 7. DocumentTransaction 往返 ====================

TEST(SceneEditIntegrationTest, DocumentTransaction_CommitUndoRedo)
{
    Eg::SceneManager scene;
    UndoRedoManager undoMgr(&scene);

    const Eg::EntityId a = addLine(scene, 0, 0, 10, 0);

    Eg::DocumentTransaction txn(scene, &undoMgr);
    txn.setDescription("Replace line");
    txn.begin();

    auto newState = std::make_unique<Eg::SyLine>();
    newState->setName("Replaced");
    newState->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(20, 0) });
    txn.replaceEntity(a, std::move(newState));

    EXPECT_TRUE(txn.changeCount() == 1) << "transaction recorded one change";
    EXPECT_TRUE(txn.commit()) << "transaction committed";

    Eg::SyEntity* afterCommit = scene.findSyEntityById(a);
    EXPECT_TRUE(afterCommit != nullptr) << "entity resolvable after commit";
    EXPECT_TRUE(std::string(afterCommit->name()) == "Replaced") << "replacement applied by commit";
    EXPECT_TRUE(scene.getEntityCount() == 1) << "count stable after commit";

    undoMgr.undo();
    Eg::SyEntity* afterUndo = scene.findSyEntityById(a);
    EXPECT_TRUE(afterUndo != nullptr) << "entity resolvable after undo";
    EXPECT_TRUE(std::string(afterUndo->name()) != "Replaced") << "undo restored original state";
    EXPECT_TRUE(scene.getEntityCount() == 1) << "count stable after undo";

    undoMgr.redo();
    Eg::SyEntity* afterRedo = scene.findSyEntityById(a);
    EXPECT_TRUE(afterRedo != nullptr) << "entity resolvable after redo";
    EXPECT_TRUE(std::string(afterRedo->name()) == "Replaced") << "redo re-applied replacement";
}

// ==================== 8. 子集作用域守卫 ====================

TEST(SceneEditIntegrationTest, SubsetScope_LeavesUnrelatedEntitiesAlone)
{
    Eg::SceneManager scene;
    UndoRedoManager undoMgr(&scene);
    SceneEditService edit(&scene, &undoMgr);

    const Eg::EntityId a = addLine(scene, 0, 0, 10, 0);
    const Eg::EntityId b = addLine(scene, 0, 5, 10, 5);
    const Eg::EntityId untouched = addLine(scene, 0, 9, 10, 9);

    // 只对 a、b 取快照，untouched 不在本次操作范围内
    auto before = edit.captureSnapshots({ a, b });
    EXPECT_TRUE(before.size() == 2) << "captured 2 of 3 entities";

    scene.findSyEntityById(a)->setName("A2");
    scene.findSyEntityById(b)->setName("B2");
    edit.pushExecutedChange(std::move(before), "Rename two", false, Eg::SnapshotScope::Subset);

    EXPECT_TRUE(std::string(scene.findSyEntityById(a)->name()) == "A2") << "subset change applied";

    undoMgr.undo();
    EXPECT_TRUE(scene.getEntityCount() == 3) << "subset undo must not remove the unrelated entity";
    EXPECT_TRUE(scene.findSyEntityById(untouched) != nullptr) << "unrelated entity still present";
    EXPECT_TRUE(std::string(scene.findSyEntityById(a)->name()) != "A2") << "subset undo restored A";

    undoMgr.redo();
    EXPECT_TRUE(scene.getEntityCount() == 3) << "subset redo keeps count stable";
    EXPECT_TRUE(std::string(scene.findSyEntityById(a)->name()) == "A2") << "subset redo re-applied A";
}
