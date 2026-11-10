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
 *  - 10. DocumentTransaction Add/Delete 的 ID 往返（P1）与撞号换号保对象（P2）
 *
 * 这些链路此前的覆盖为空：改动若破坏「场景持有一种状态、命令持有另一种状态」
 * 的不变量，编译不会报错，只会在用户撤销时丢图元或数量翻倍，故固化为常态用例。
 */

#include <gtest/gtest.h>

#include "Engine2D/Edit/SceneEditService.h"
#include "Engine2D/Edit/UndoRedoManager.h"
#include "Engine2D/Core/SceneManager.h"
#include "Engine2D/Core/SceneWriteGateway.h"
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
    Eg::EntityId createLine(Eg::SceneManager& scene, double x0, double y0, double x1, double y1)
    {
        auto line = std::make_unique<Eg::SyLine>();
        line->setPointVector({ Ut::Vec2d(x0, y0), Ut::Vec2d(x1, y1) });
        const Eg::EntityId id = line->id;
        std::vector<std::unique_ptr<Eg::SyEntity>> vec;
        vec.push_back(std::move(line));
        Eg::SceneWriteGateway::addEntities(scene, std::move(vec));
        return id;
    }

    /// 造一条指定 id 的直线并以保 ID 方式入场景（构造 P2 撞号场景用）
    void createLineWithId(
        Eg::SceneManager& scene, Eg::EntityId forcedId, const char* name, double x0, double y0, double x1, double y1)
    {
        auto line = std::make_unique<Eg::SyLine>();
        line->id = forcedId;
        line->setName(name);
        line->setPointVector({ Ut::Vec2d(x0, y0), Ut::Vec2d(x1, y1) });
        Eg::SceneWriteGateway::insertEntityPreserveId(scene, std::move(line));
    }

    /// 按名字在场景里查找图元 id，找不到返回 0（P2 测试认领换号后的恢复对象用）
    Eg::EntityId findIdByName(Eg::SceneManager& scene, const char* name)
    {
        for (Eg::SyEntity* e : scene.getAllEntities())
        {
            if (e && std::string(e->name()) == name)
            {
                return e->id;
            }
        }
        return 0;
    }
}  // namespace

// ==================== 1. transformEntities 往返 ====================

TEST(SceneEditIntegrationTest, TransformEntities_RoundTrip)
{
    Eg::SceneManager scene;
    UndoRedoManager undoMgr(&scene);
    SceneEditService edit(&scene, &undoMgr);

    const Eg::EntityId a = createLine(scene, 0, 0, 10, 0);
    const Eg::EntityId b = createLine(scene, 0, 5, 10, 5);
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

    const Eg::EntityId a = createLine(scene, 0, 0, 10, 0);

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

    const Eg::EntityId a = createLine(scene, 0, 0, 10, 0);

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

    const Eg::EntityId a = createLine(scene, 0, 0, 10, 0);
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

    const Eg::EntityId a = createLine(scene, 0, 0, 10, 0);
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

    const Eg::EntityId a = createLine(scene, 0, 0, 10, 0);
    const Eg::EntityId b = createLine(scene, 0, 5, 10, 5);

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

    const Eg::EntityId a = createLine(scene, 0, 0, 10, 0);
    const Eg::EntityId b = createLine(scene, 0, 5, 10, 5);

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

    const Eg::EntityId a = createLine(scene, 0, 0, 10, 0);
    const Eg::EntityId b = createLine(scene, 0, 5, 10, 5);
    const Eg::EntityId c = createLine(scene, 0, 9, 10, 9);

    auto before = edit.captureAllSnapshots();
    EXPECT_TRUE(before.size() == 3) << "captured 3 before snapshots";

    // 模拟算法在场景内部直接删掉一个图元（偏移/布尔等就会这么做）
    Eg::SceneWriteGateway::extractEntitiesById(scene, { b });
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

    const Eg::EntityId a = createLine(scene, 0, 0, 10, 0);

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

    const Eg::EntityId a = createLine(scene, 0, 0, 10, 0);
    const Eg::EntityId b = createLine(scene, 0, 5, 10, 5);
    const Eg::EntityId untouched = createLine(scene, 0, 9, 10, 9);

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

// ==================== 10. DocumentTransaction Add/Delete ID 往返（P1 回归） ====================
// 审计 P1：Add/Delete 的 change.entityId 与落库/恢复对象 ID 错位 —— Add 撤销残留、
// 二次执行出重复图元、Delete 撤销恢复用新 ID。以下为验收断言（含 P2 撞号用例）。

TEST(SceneEditIntegrationTest, DocumentTransaction_AddUndo_ZeroResidue)
{
    Eg::SceneManager scene;
    UndoRedoManager undoMgr(&scene);
    const Eg::EntityId base = createLine(scene, 0, 0, 10, 0);
    const size_t baseline = scene.getEntityCount();

    Eg::DocumentTransaction txn(scene, &undoMgr);
    txn.setDescription("Add two lines");
    txn.begin();

    auto l1 = std::make_unique<Eg::SyLine>();
    l1->setPointVector({ Ut::Vec2d(0, 1), Ut::Vec2d(10, 1) });
    const Eg::EntityId id1 = l1->id;
    txn.addEntity(std::move(l1));

    auto l2 = std::make_unique<Eg::SyLine>();
    l2->setPointVector({ Ut::Vec2d(0, 2), Ut::Vec2d(10, 2) });
    const Eg::EntityId id2 = l2->id;
    txn.addEntity(std::move(l2));

    EXPECT_TRUE(txn.commit()) << "transaction committed";
    EXPECT_TRUE(scene.getEntityCount() == baseline + 2) << "commit adds exactly two entities";
    EXPECT_TRUE(scene.findSyEntityById(id1) != nullptr) << "added entity resolvable by recorded id";
    EXPECT_TRUE(scene.findSyEntityById(id2) != nullptr) << "second added entity resolvable by recorded id";

    undoMgr.undo();
    EXPECT_TRUE(scene.getEntityCount() == baseline) << "Add undo: scene back to baseline (zero residue)";
    EXPECT_TRUE(scene.findSyEntityById(id1) == nullptr) << "Add undo: id1 not lingering in scene";
    EXPECT_TRUE(scene.findSyEntityById(id2) == nullptr) << "Add undo: id2 not lingering in scene";
    EXPECT_TRUE(scene.findSyEntityById(base) != nullptr) << "unrelated entity intact";
}

TEST(SceneEditIntegrationTest, DocumentTransaction_AddUndoRedo_IdMatchesFirstInsert)
{
    Eg::SceneManager scene;
    UndoRedoManager undoMgr(&scene);
    createLine(scene, 0, 0, 10, 0);
    const size_t baseline = scene.getEntityCount();

    Eg::DocumentTransaction txn(scene, &undoMgr);
    txn.setDescription("Add one line");
    txn.begin();
    auto line = std::make_unique<Eg::SyLine>();
    line->setPointVector({ Ut::Vec2d(0, 1), Ut::Vec2d(10, 1) });
    const Eg::EntityId id = line->id;
    txn.addEntity(std::move(line));
    EXPECT_TRUE(txn.commit());

    undoMgr.undo();
    EXPECT_TRUE(scene.findSyEntityById(id) == nullptr) << "undo removes the added entity";
    EXPECT_TRUE(scene.getEntityCount() == baseline) << "count back to baseline after undo";

    undoMgr.redo();
    EXPECT_TRUE(scene.findSyEntityById(id) != nullptr) << "redo restores the same id as first insert";
    EXPECT_TRUE(scene.getEntityCount() == baseline + 1) << "redo adds exactly one (no duplicate entity)";

    undoMgr.undo();
    EXPECT_TRUE(scene.getEntityCount() == baseline) << "2nd undo stable";
    undoMgr.redo();
    EXPECT_TRUE(scene.getEntityCount() == baseline + 1) << "2nd redo stable (ping-pong)";
    EXPECT_TRUE(scene.findSyEntityById(id) != nullptr) << "id stable across ping-pong";
}

TEST(SceneEditIntegrationTest, DocumentTransaction_DeleteUndo_RestoresOriginalId)
{
    Eg::SceneManager scene;
    UndoRedoManager undoMgr(&scene);
    const Eg::EntityId a = createLine(scene, 0, 0, 10, 0);
    const Eg::EntityId b = createLine(scene, 0, 5, 10, 5);
    const size_t baseline = scene.getEntityCount();

    Eg::DocumentTransaction txn(scene, &undoMgr);
    txn.setDescription("Delete line A");
    txn.begin();
    txn.deleteEntity(a);
    EXPECT_TRUE(txn.commit());

    EXPECT_TRUE(scene.findSyEntityById(a) == nullptr) << "deleted by commit";
    EXPECT_TRUE(scene.getEntityCount() == baseline - 1) << "one entity removed";

    undoMgr.undo();
    EXPECT_TRUE(scene.findSyEntityById(a) != nullptr) << "Delete undo: original id restored and queryable";
    EXPECT_TRUE(scene.getEntityCount() == baseline) << "count restored after undo";
    EXPECT_TRUE(scene.findSyEntityById(b) != nullptr) << "unrelated entity intact";
}

TEST(SceneEditIntegrationTest, DocumentTransaction_DeleteUndoRedo_RoundTrip)
{
    Eg::SceneManager scene;
    UndoRedoManager undoMgr(&scene);
    const Eg::EntityId a = createLine(scene, 0, 0, 10, 0);
    createLine(scene, 0, 5, 10, 5);
    const size_t baseline = scene.getEntityCount();

    Eg::DocumentTransaction txn(scene, &undoMgr);
    txn.setDescription("Delete line A");
    txn.begin();
    txn.deleteEntity(a);
    EXPECT_TRUE(txn.commit());
    EXPECT_TRUE(scene.findSyEntityById(a) == nullptr) << "deleted by commit";

    undoMgr.undo();
    EXPECT_TRUE(scene.findSyEntityById(a) != nullptr) << "undo restores original id";
    EXPECT_TRUE(scene.getEntityCount() == baseline) << "count restored after undo";

    undoMgr.redo();
    EXPECT_TRUE(scene.findSyEntityById(a) == nullptr) << "redo removes again";
    EXPECT_TRUE(scene.getEntityCount() == baseline - 1) << "count reflects redo";

    undoMgr.undo();
    EXPECT_TRUE(scene.findSyEntityById(a) != nullptr) << "2nd undo restores again (ping-pong stable)";
    EXPECT_TRUE(scene.getEntityCount() == baseline) << "count stable across ping-pong";
}

TEST(SceneEditIntegrationTest, DocumentTransaction_TxnLevelDeleteUndoRedo_KeepsOriginalId)
{
    // 不挂 UndoRedoManager，走事务级 undo/redo —— 覆盖 revertChanges/applyChanges 的 ID 稳定
    Eg::SceneManager scene;
    const Eg::EntityId a = createLine(scene, 0, 0, 10, 0);
    createLine(scene, 0, 5, 10, 5);
    const size_t baseline = scene.getEntityCount();

    Eg::DocumentTransaction txn(scene);
    txn.begin();
    txn.deleteEntity(a);
    EXPECT_TRUE(txn.commit());
    EXPECT_TRUE(scene.findSyEntityById(a) == nullptr) << "deleted by commit";

    EXPECT_TRUE(txn.undo()) << "txn-level undo";
    EXPECT_TRUE(scene.findSyEntityById(a) != nullptr) << "txn-level undo restores original id";
    EXPECT_TRUE(scene.getEntityCount() == baseline) << "count restored";

    EXPECT_TRUE(txn.redo()) << "txn-level redo";
    EXPECT_TRUE(scene.findSyEntityById(a) == nullptr) << "txn-level redo deletes by the same id";

    EXPECT_TRUE(txn.undo()) << "second txn-level undo";
    EXPECT_TRUE(scene.findSyEntityById(a) != nullptr) << "ping-pong stable at txn level";
}

TEST(SceneEditIntegrationTest, DocumentTransaction_DeleteUndo_IdCollision_RemapsKeepsObject)
{
    // P2：undo 恢复撞号时换新 ID 保对象（绝不静默丢弃），换号后 redo/undo 仍按新 ID 闭环。
    Eg::SceneManager scene;
    UndoRedoManager undoMgr(&scene);
    const Eg::EntityId victim = createLine(scene, 0, 0, 10, 0);
    createLine(scene, 0, 5, 10, 5);
    scene.findSyEntityById(victim)->setName("Victim");
    const size_t baseline = scene.getEntityCount();

    Eg::DocumentTransaction txn(scene, &undoMgr);
    txn.setDescription("Delete victim");
    txn.begin();
    txn.deleteEntity(victim);
    EXPECT_TRUE(txn.commit());
    EXPECT_TRUE(scene.findSyEntityById(victim) == nullptr) << "victim deleted";

    // 构造撞号：删除后、撤销前，另一对象占住旧 ID（模拟导入/外部插入抢号）
    createLineWithId(scene, victim, "Occupier", 0, 9, 10, 9);
    EXPECT_TRUE(scene.findSyEntityById(victim) != nullptr) << "occupier holds the old id now";

    undoMgr.undo();
    EXPECT_TRUE(scene.getEntityCount() == baseline + 1) << "restored object kept alongside occupier (not dropped)";
    Eg::SyEntity* oldIdOwner = scene.findSyEntityById(victim);
    EXPECT_TRUE(oldIdOwner != nullptr) << "occupier still owns the old id";
    EXPECT_TRUE(std::string(oldIdOwner->name()) == "Occupier") << "old id not hijacked from occupier";

    const Eg::EntityId restoredId = findIdByName(scene, "Victim");
    EXPECT_TRUE(restoredId != 0) << "victim object not lost: restored and queryable under a new id";
    EXPECT_TRUE(restoredId != victim) << "id remapped because of collision";

    // redo → undo：按换号后的 ID 闭环取回，对象始终不丢
    undoMgr.redo();
    EXPECT_TRUE(scene.findSyEntityById(restoredId) == nullptr) << "redo removes the remapped restore";
    EXPECT_TRUE(scene.getEntityCount() == baseline) << "back to occupier + unrelated after redo";
    EXPECT_TRUE(scene.findSyEntityById(victim) != nullptr) << "occupier untouched by redo";

    undoMgr.undo();
    EXPECT_TRUE(scene.getEntityCount() == baseline + 1) << "2nd undo restores again";
    EXPECT_TRUE(findIdByName(scene, "Victim") != 0) << "victim recoverable after redo/undo cycle";
}