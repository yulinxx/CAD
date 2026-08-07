/**
 * @file SceneNotifierTests.cpp
 * @brief 场景通知器回归测试 — 覆盖观察者注册/移除/通知广播/批量更新/延迟刷新
 *
 * 测试范围：
 *  - 观察者注册与移除 (addObserver/removeObserver)
 *  - 场景变更通知 (notifySceneChanged)
 *  - 选择变更通知 (notifySelectionChanged)
 *  - 实体添加/移除通知 (notifyEntityAdded/notifyEntityRemoved)
 *  - 批量更新与延迟通知 (beginBatch/endBatch/flushDeferred)
 *  - 边界条件：空指针、重复注册、嵌套批量
 *  - 通知顺序保证
 *
 * 文档 8.1 — 近期优先补测试缺口 (2026-07-30)
 */

#include <gtest/gtest.h>

#include "Engine2D/Core/SceneNotifier.h"
#include "Engine2D/SyEntity/SyLine.h"

#include <memory>
#include <vector>
#include <string>

 // ============================================================================
 // 测试用观察者 — 记录所有收到的通知
 // ============================================================================

class TestObserver : public Eg::SceneNotifier::IObserver
{
public:
    void onSceneChanged() override
    {
        m_calls.push_back("sceneChanged");
    }

    void onSelectionChanged() override
    {
        m_calls.push_back("selectionChanged");
    }

    void onEntityAdded(Eg::SyEntity* entity) override
    {
        m_addedEntities.push_back(entity);
        m_calls.push_back("entityAdded");
    }

    void onEntityRemoved(size_t index) override
    {
        m_removedIndices.push_back(index);
        m_calls.push_back("entityRemoved");
    }

    void reset()
    {
        m_calls.clear();
        m_addedEntities.clear();
        m_removedIndices.clear();
    }

    std::vector<std::string> m_calls;
    std::vector<Eg::SyEntity*> m_addedEntities;
    std::vector<size_t> m_removedIndices;
};

// ============================================================================
// 观察者注册与移除
// ============================================================================

TEST(SceneNotifierTest, AddObserver_NullIgnored)
{
    Eg::SceneNotifier notifier;
    notifier.addObserver(nullptr);
    // 不崩溃
    SUCCEED();
}

TEST(SceneNotifierTest, RemoveObserver_NullIgnored)
{
    Eg::SceneNotifier notifier;
    notifier.removeObserver(nullptr);
    // 不崩溃
    SUCCEED();
}

TEST(SceneNotifierTest, RemoveObserver_NotRegistered)
{
    Eg::SceneNotifier notifier;
    TestObserver obs;
    notifier.removeObserver(&obs);
    // 不崩溃
    SUCCEED();
}

TEST(SceneNotifierTest, AddRemoveObserver_Single)
{
    Eg::SceneNotifier notifier;
    TestObserver obs;

    notifier.addObserver(&obs);
    notifier.removeObserver(&obs);
    // 移除后不应再收到通知
    notifier.notifySceneChanged();
    EXPECT_TRUE(obs.m_calls.empty());
}

TEST(SceneNotifierTest, AddRemoveObserver_Multiple)
{
    Eg::SceneNotifier notifier;
    TestObserver obs1, obs2, obs3;

    notifier.addObserver(&obs1);
    notifier.addObserver(&obs2);
    notifier.addObserver(&obs3);

    // 移除中间一个
    notifier.removeObserver(&obs2);
    notifier.notifySceneChanged();

    EXPECT_EQ(obs1.m_calls.size(), 1u);
    EXPECT_TRUE(obs2.m_calls.empty());   // 已移除，不应收到
    EXPECT_EQ(obs3.m_calls.size(), 1u);
}

TEST(SceneNotifierTest, AddObserver_DuplicateOk)
{
    Eg::SceneNotifier notifier;
    TestObserver obs;

    notifier.addObserver(&obs);
    notifier.addObserver(&obs);  // 重复注册，实现可能允许也可能不允许
    notifier.notifySceneChanged();

    // 至少收到一次通知
    EXPECT_GE(obs.m_calls.size(), 1u);
}

// ============================================================================
// 场景变更通知
// ============================================================================

TEST(SceneNotifierTest, NotifySceneChanged_SingleObserver)
{
    Eg::SceneNotifier notifier;
    TestObserver obs;

    notifier.addObserver(&obs);
    notifier.notifySceneChanged();

    ASSERT_EQ(obs.m_calls.size(), 1u);
    EXPECT_EQ(obs.m_calls[0], "sceneChanged");
}

TEST(SceneNotifierTest, NotifySceneChanged_MultipleObservers)
{
    Eg::SceneNotifier notifier;
    TestObserver obs1, obs2, obs3;

    notifier.addObserver(&obs1);
    notifier.addObserver(&obs2);
    notifier.addObserver(&obs3);
    notifier.notifySceneChanged();

    EXPECT_EQ(obs1.m_calls.size(), 1u);
    EXPECT_EQ(obs2.m_calls.size(), 1u);
    EXPECT_EQ(obs3.m_calls.size(), 1u);
}

TEST(SceneNotifierTest, NotifySceneChanged_NoObservers)
{
    Eg::SceneNotifier notifier;
    notifier.notifySceneChanged();
    // 空观察者列表不崩溃
    SUCCEED();
}

TEST(SceneNotifierTest, NotifySceneChanged_MultipleCalls)
{
    Eg::SceneNotifier notifier;
    TestObserver obs;

    notifier.addObserver(&obs);
    notifier.notifySceneChanged();
    notifier.notifySceneChanged();
    notifier.notifySceneChanged();

    EXPECT_EQ(obs.m_calls.size(), 3u);
}

// ============================================================================
// 选择变更通知
// ============================================================================

TEST(SceneNotifierTest, NotifySelectionChanged_SingleObserver)
{
    Eg::SceneNotifier notifier;
    TestObserver obs;

    notifier.addObserver(&obs);
    notifier.notifySelectionChanged();

    ASSERT_EQ(obs.m_calls.size(), 1u);
    EXPECT_EQ(obs.m_calls[0], "selectionChanged");
}

TEST(SceneNotifierTest, NotifySelectionChanged_MultipleObservers)
{
    Eg::SceneNotifier notifier;
    TestObserver obs1, obs2;

    notifier.addObserver(&obs1);
    notifier.addObserver(&obs2);
    notifier.notifySelectionChanged();

    EXPECT_EQ(obs1.m_calls.size(), 1u);
    EXPECT_EQ(obs2.m_calls.size(), 1u);
}

TEST(SceneNotifierTest, NotifySelectionChanged_NoObservers)
{
    Eg::SceneNotifier notifier;
    notifier.notifySelectionChanged();
    SUCCEED();
}

// ============================================================================
// 实体添加通知
// ============================================================================

TEST(SceneNotifierTest, NotifyEntityAdded_SingleObserver)
{
    Eg::SceneNotifier notifier;
    TestObserver obs;

    Eg::SyLine line;
    line.setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });

    notifier.addObserver(&obs);
    notifier.notifyEntityAdded(&line);

    ASSERT_EQ(obs.m_addedEntities.size(), 1u);
    EXPECT_EQ(obs.m_addedEntities[0], &line);
}

TEST(SceneNotifierTest, NotifyEntityAdded_NullEntityIgnored)
{
    Eg::SceneNotifier notifier;
    TestObserver obs;

    notifier.addObserver(&obs);
    notifier.notifyEntityAdded(nullptr);

    // 空指针不应触发通知
    EXPECT_TRUE(obs.m_addedEntities.empty());
}

TEST(SceneNotifierTest, NotifyEntityAdded_MultipleEntities)
{
    Eg::SceneNotifier notifier;
    TestObserver obs;

    Eg::SyLine line1, line2, line3;
    line1.setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(1, 1) });
    line2.setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(2, 2) });
    line3.setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(3, 3) });

    notifier.addObserver(&obs);
    notifier.notifyEntityAdded(&line1);
    notifier.notifyEntityAdded(&line2);
    notifier.notifyEntityAdded(&line3);

    EXPECT_EQ(obs.m_addedEntities.size(), 3u);
    EXPECT_EQ(obs.m_addedEntities[0], &line1);
    EXPECT_EQ(obs.m_addedEntities[1], &line2);
    EXPECT_EQ(obs.m_addedEntities[2], &line3);
}

// ============================================================================
// 实体移除通知
// ============================================================================

TEST(SceneNotifierTest, NotifyEntityRemoved_SingleObserver)
{
    Eg::SceneNotifier notifier;
    TestObserver obs;

    notifier.addObserver(&obs);
    notifier.notifyEntityRemoved(42);

    ASSERT_EQ(obs.m_removedIndices.size(), 1u);
    EXPECT_EQ(obs.m_removedIndices[0], 42u);
}

TEST(SceneNotifierTest, NotifyEntityRemoved_MultipleObservers)
{
    Eg::SceneNotifier notifier;
    TestObserver obs1, obs2;

    notifier.addObserver(&obs1);
    notifier.addObserver(&obs2);
    notifier.notifyEntityRemoved(7);

    EXPECT_EQ(obs1.m_removedIndices.size(), 1u);
    EXPECT_EQ(obs2.m_removedIndices.size(), 1u);
    EXPECT_EQ(obs1.m_removedIndices[0], 7u);
    EXPECT_EQ(obs2.m_removedIndices[0], 7u);
}

// ============================================================================
// 批量更新与延迟通知
// ============================================================================

TEST(SceneNotifierTest, Batch_BeginEndEmpty)
{
    Eg::SceneNotifier notifier;
    notifier.beginBatch();
    notifier.endBatch();
    // 空批量操作不崩溃
    SUCCEED();
}

TEST(SceneNotifierTest, Batch_IsInBatch)
{
    Eg::SceneNotifier notifier;

    EXPECT_FALSE(notifier.isInBatch());
    notifier.beginBatch();
    EXPECT_TRUE(notifier.isInBatch());
    notifier.endBatch();
    EXPECT_FALSE(notifier.isInBatch());
}

TEST(SceneNotifierTest, Batch_SceneChangedDeferred)
{
    Eg::SceneNotifier notifier;
    TestObserver obs;

    notifier.addObserver(&obs);

    // 批量模式下，通知应延迟
    notifier.beginBatch();
    notifier.notifySceneChanged();
    EXPECT_TRUE(obs.m_calls.empty());  // 批量中不发送

    notifier.endBatch();
    EXPECT_EQ(obs.m_calls.size(), 1u);  // 批量结束后发送
    EXPECT_EQ(obs.m_calls[0], "sceneChanged");
}

TEST(SceneNotifierTest, Batch_SelectionChangedDeferred)
{
    Eg::SceneNotifier notifier;
    TestObserver obs;

    notifier.addObserver(&obs);

    notifier.beginBatch();
    notifier.notifySelectionChanged();
    EXPECT_TRUE(obs.m_calls.empty());  // 批量中不发送

    notifier.endBatch();
    EXPECT_EQ(obs.m_calls.size(), 1u);
    EXPECT_EQ(obs.m_calls[0], "selectionChanged");
}

TEST(SceneNotifierTest, Batch_EntityAddedDeferred)
{
    Eg::SceneNotifier notifier;
    TestObserver obs;

    Eg::SyLine line;
    line.setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });

    notifier.addObserver(&obs);

    notifier.beginBatch();
    notifier.notifyEntityAdded(&line);
    EXPECT_TRUE(obs.m_addedEntities.empty());  // 批量中不发送

    notifier.endBatch();
    EXPECT_EQ(obs.m_addedEntities.size(), 1u);
    EXPECT_EQ(obs.m_addedEntities[0], &line);
}

TEST(SceneNotifierTest, Batch_EntityRemovedNotDeferred)
{
    // entityRemoved 不受批量影响（实现中直接广播）
    Eg::SceneNotifier notifier;
    TestObserver obs;

    notifier.addObserver(&obs);

    notifier.beginBatch();
    notifier.notifyEntityRemoved(99);
    // entityRemoved 不延迟
    EXPECT_EQ(obs.m_removedIndices.size(), 1u);
    EXPECT_EQ(obs.m_removedIndices[0], 99u);

    notifier.endBatch();
    SUCCEED();
}

TEST(SceneNotifierTest, Batch_MultipleNotificationsDeferred)
{
    Eg::SceneNotifier notifier;
    TestObserver obs;

    Eg::SyLine line1, line2;
    line1.setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(1, 1) });
    line2.setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(2, 2) });

    notifier.addObserver(&obs);

    notifier.beginBatch();
    notifier.notifySceneChanged();
    notifier.notifySelectionChanged();
    notifier.notifyEntityAdded(&line1);
    notifier.notifyEntityAdded(&line2);
    // 批量期间全部延迟
    EXPECT_TRUE(obs.m_calls.empty());

    notifier.endBatch();
    // 所有延迟通知应全部发出：2×entityAdded + selectionChanged + sceneChanged → 4 calls
    EXPECT_EQ(obs.m_calls.size(), 4u);
    EXPECT_EQ(obs.m_addedEntities.size(), 2u);
}

TEST(SceneNotifierTest, Batch_NestedBatch)
{
    Eg::SceneNotifier notifier;
    TestObserver obs;

    notifier.addObserver(&obs);

    notifier.beginBatch();
    notifier.beginBatch();  // 嵌套批量
    notifier.notifySceneChanged();

    notifier.endBatch();    // 内层结束，不刷新
    EXPECT_TRUE(obs.m_calls.empty());

    notifier.endBatch();    // 外层结束，刷新
    EXPECT_EQ(obs.m_calls.size(), 1u);
}

TEST(SceneNotifierTest, Batch_EndBatchWithoutBegin)
{
    Eg::SceneNotifier notifier;
    notifier.endBatch();  // 未调用 beginBatch 时不应崩溃
    SUCCEED();
}

TEST(SceneNotifierTest, Batch_EndBatchDepthUnderflow)
{
    Eg::SceneNotifier notifier;
    notifier.beginBatch();
    notifier.endBatch();
    notifier.endBatch();  // 多调用一次 endBatch 不应崩溃
    SUCCEED();
}

// ============================================================================
// 混合通知顺序
// ============================================================================

TEST(SceneNotifierTest, MixedNotifications_Order)
{
    Eg::SceneNotifier notifier;
    TestObserver obs;

    notifier.addObserver(&obs);

    notifier.notifySceneChanged();
    notifier.notifySelectionChanged();
    notifier.notifySceneChanged();

    ASSERT_EQ(obs.m_calls.size(), 3u);
    EXPECT_EQ(obs.m_calls[0], "sceneChanged");
    EXPECT_EQ(obs.m_calls[1], "selectionChanged");
    EXPECT_EQ(obs.m_calls[2], "sceneChanged");
}

// ============================================================================
// 默认构造与析构
// ============================================================================

TEST(SceneNotifierTest, DefaultConstruction)
{
    Eg::SceneNotifier notifier;
    EXPECT_FALSE(notifier.isInBatch());
}

TEST(SceneNotifierTest, DestructorWithObservers)
{
    // 析构时持有观察者指针不应崩溃
    {
        Eg::SceneNotifier notifier;
        TestObserver obs;
        notifier.addObserver(&obs);
    }
    SUCCEED();
}

// ============================================================================
// 不与 SceneManager 耦合的独立性测试
// ============================================================================

TEST(SceneNotifierTest, IndependentFromSceneManager)
{
    // SceneNotifier 作为一个独立组件，不依赖 SceneManager 的生命周期
    Eg::SceneNotifier notifier;
    TestObserver obs;

    notifier.addObserver(&obs);

    // 所有通知类型独立测试
    notifier.notifySceneChanged();
    notifier.notifySelectionChanged();

    Eg::SyLine line;
    line.setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(5, 5) });
    notifier.notifyEntityAdded(&line);
    notifier.notifyEntityRemoved(0);

    EXPECT_EQ(obs.m_calls.size(), 4u);  // sceneChanged + selectionChanged + entityAdded + entityRemoved
    EXPECT_EQ(obs.m_removedIndices.size(), 1u);
}