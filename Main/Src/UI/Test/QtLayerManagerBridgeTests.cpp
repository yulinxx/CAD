/**
 * @file QtLayerManagerBridgeTests.cpp
 * @brief QtLayerManagerBridge 回归测试 — 图层 UI 刷新机制
 *
 * 验证 QtLayerManagerBridge 把 LayerManager 的观察者回调转成 Qt 信号：
 * 图层增删、改名/改色、当前层切换、显隐切换、顺序变化都要发对应信号；
 * 注销观察者后不再发，重新注册后恢复。
 *
 * 信号记录方式：用普通 connect 记一份，而不是 QSignalSpy ——
 * MainTests 没有链接 Qt Test 模块（ProcessingChainTests 出于同样原因如此）。
 */
#include <gtest/gtest.h>

#include <QString>
#include <vector>

#include "UI2D/Edit/QtLayerManagerBridge.h"
#include "Engine2D/Interaction/LayerManager.h"

class QtLayerManagerBridgeTest : public ::testing::Test
{
protected:
    /// 图层桥接信号的记录（次数 + 最后一次参数）
    struct SignalLog
    {
        int addedCount = 0;
        int lastAddedId = -1;
        QString lastAddedName;

        int removedCount = 0;
        int lastRemovedId = -1;

        int changedCount = 0;
        int lastChangedId = -1;

        int currentChangedCount = 0;
        int lastCurrentChangedId = -1;

        int visibilityChangedCount = 0;
        int lastVisibilityChangedId = -1;
        bool lastVisibilityVisible = true;

        int orderChangedCount = 0;
    };

    void SetUp() override
    {
        // 创建 SceneManager 和 LayerManager
        m_sceneManager = std::make_unique<Eg::SceneManager>();
        m_layerManager = std::make_unique<LayerManager>(m_sceneManager.get());

        m_bridge = std::make_unique<QtLayerManagerBridge>();
        m_layerManager->addObserver(m_bridge.get());

        QObject::connect(m_bridge.get(), &QtLayerManagerBridge::sigLayerAdded, [this](int id, const QString& name) {
            ++m_log.addedCount;
            m_log.lastAddedId = id;
            m_log.lastAddedName = name;
        });
        QObject::connect(m_bridge.get(), &QtLayerManagerBridge::sigLayerRemoved, [this](int id) {
            ++m_log.removedCount;
            m_log.lastRemovedId = id;
        });
        QObject::connect(m_bridge.get(), &QtLayerManagerBridge::sigLayerChanged, [this](int id) {
            ++m_log.changedCount;
            m_log.lastChangedId = id;
        });
        QObject::connect(m_bridge.get(), &QtLayerManagerBridge::sigCurrentLayerChanged, [this](int id) {
            ++m_log.currentChangedCount;
            m_log.lastCurrentChangedId = id;
        });
        QObject::connect(m_bridge.get(), &QtLayerManagerBridge::sigLayerVisibilityChanged, [this](int id, bool visible) {
            ++m_log.visibilityChangedCount;
            m_log.lastVisibilityChangedId = id;
            m_log.lastVisibilityVisible = visible;
        });
        QObject::connect(m_bridge.get(), &QtLayerManagerBridge::sigLayerOrderChanged, [this]() {
            ++m_log.orderChangedCount;
        });
    }

    void TearDown() override
    {
        if (m_layerManager && m_bridge)
        {
            m_layerManager->removeObserver(m_bridge.get());
        }
        m_bridge.reset();
        m_layerManager.reset();
        m_sceneManager.reset();
    }

    std::unique_ptr<Eg::SceneManager> m_sceneManager;
    std::unique_ptr<LayerManager> m_layerManager;
    std::unique_ptr<QtLayerManagerBridge> m_bridge;
    SignalLog m_log;
};

// ==================== onLayerAdded 信号测试 ====================

TEST_F(QtLayerManagerBridgeTest, OnLayerAdded_EmitsSignal)
{
    // 通过 LayerManager 创建图层
    int layerId = m_layerManager->createLayer("TestLayer");
    ASSERT_GT(layerId, 0);

    // 验证信号发射
    EXPECT_EQ(m_log.addedCount, 1);

    // 验证信号参数
    EXPECT_EQ(m_log.lastAddedId, layerId);
    EXPECT_EQ(m_log.lastAddedName, QString::fromUtf8("TestLayer"));
}

TEST_F(QtLayerManagerBridgeTest, OnLayerAdded_MultipleLayers)
{
    // 创建多个图层
    m_layerManager->createLayer("Layer1");
    m_layerManager->createLayer("Layer2");
    m_layerManager->createLayer("Layer3");

    // 验证信号发射次数
    EXPECT_EQ(m_log.addedCount, 3);
}

TEST_F(QtLayerManagerBridgeTest, OnLayerAdded_EmptyName_UsesGeneratedName)
{
    // 创建空名称图层：LayerManager 会自动补一个名字（"Layer N"），
    // 桥接层要转发的是**实际生效**的名字，而不是空串。
    int layerId = m_layerManager->createLayer("");
    ASSERT_GT(layerId, 0);

    EXPECT_EQ(m_log.addedCount, 1);
    EXPECT_EQ(m_log.lastAddedId, layerId);
    EXPECT_EQ(m_log.lastAddedName, QString::fromUtf8(m_layerManager->layerName(layerId).c_str()));
}

// ==================== onLayerRemoved 信号测试 ====================

TEST_F(QtLayerManagerBridgeTest, OnLayerRemoved_EmitsSignal)
{
    // 先创建一个图层
    int layerId = m_layerManager->createLayer("ToDelete");
    ASSERT_GT(layerId, 0);

    // 删除图层
    ASSERT_TRUE(m_layerManager->deleteLayer(layerId));

    // 验证信号发射
    EXPECT_EQ(m_log.removedCount, 1);

    // 验证信号参数
    EXPECT_EQ(m_log.lastRemovedId, layerId);
}

TEST_F(QtLayerManagerBridgeTest, OnLayerRemoved_MultipleLayers)
{
    int id1 = m_layerManager->createLayer("L1");
    int id2 = m_layerManager->createLayer("L2");

    m_layerManager->deleteLayer(id1);
    m_layerManager->deleteLayer(id2);

    EXPECT_EQ(m_log.removedCount, 2);
}

// ==================== onLayerChanged 信号测试 ====================

TEST_F(QtLayerManagerBridgeTest, OnLayerChanged_RenameEmitsSignal)
{
    int layerId = m_layerManager->createLayer("Original");

    // 重命名图层
    m_layerManager->renameLayer(layerId, "Renamed");

    EXPECT_EQ(m_log.changedCount, 1);

    EXPECT_EQ(m_log.lastChangedId, layerId);
}

TEST_F(QtLayerManagerBridgeTest, OnLayerChanged_ColorChangeEmitsSignal)
{
    int layerId = m_layerManager->createLayer("ColorLayer");

    // 改色：LayerManager 会拒绝与其它图层重复的颜色（默认图层已占用红/绿/蓝等），
    // 这里用一个未被占用的颜色，并断言这次修改确实生效。
    const Ut::Color newColor(0.25f, 0.5f, 0.75f);
    ASSERT_TRUE(m_layerManager->setLayerColor(layerId, newColor));

    EXPECT_EQ(m_log.changedCount, 1);
    EXPECT_EQ(m_log.lastChangedId, layerId);
}

TEST_F(QtLayerManagerBridgeTest, OnLayerChanged_DuplicateColorRejected_NoSignal)
{
    int layerId = m_layerManager->createLayer("ColorLayer");

    // 与已有图层重复的颜色会被拒绝，且不应发出变更信号
    const Ut::Color defaultRed(1.0f, 0.0f, 0.0f);
    EXPECT_FALSE(m_layerManager->setLayerColor(layerId, defaultRed));

    EXPECT_EQ(m_log.changedCount, 0);
}

// ==================== onCurrentLayerChanged 信号测试 ====================

TEST_F(QtLayerManagerBridgeTest, OnCurrentLayerChanged_EmitsSignal)
{
    // 设置当前图层
    int layerId = m_layerManager->createLayer("Current");
    m_layerManager->setCurrentLayer(layerId);

    EXPECT_EQ(m_log.currentChangedCount, 1);

    EXPECT_EQ(m_log.lastCurrentChangedId, layerId);
}

// ==================== onLayerVisibilityChanged 信号测试 ====================

TEST_F(QtLayerManagerBridgeTest, OnLayerVisibilityChanged_HideEmitsSignal)
{
    int layerId = m_layerManager->createLayer("VisibleLayer");

    // 隐藏图层
    m_layerManager->setLayerVisible(layerId, false);

    EXPECT_EQ(m_log.visibilityChangedCount, 1);

    EXPECT_EQ(m_log.lastVisibilityChangedId, layerId);
    EXPECT_FALSE(m_log.lastVisibilityVisible);
}

TEST_F(QtLayerManagerBridgeTest, OnLayerVisibilityChanged_ShowEmitsSignal)
{
    int layerId = m_layerManager->createLayer("VisibleLayer");

    // 先隐藏
    m_layerManager->setLayerVisible(layerId, false);

    // 显示图层
    m_layerManager->setLayerVisible(layerId, true);

    EXPECT_EQ(m_log.visibilityChangedCount, 2);

    EXPECT_EQ(m_log.lastVisibilityChangedId, layerId);
    EXPECT_TRUE(m_log.lastVisibilityVisible);
}

// ==================== onLayerOrderChanged 信号测试 ====================

TEST_F(QtLayerManagerBridgeTest, OnLayerOrderChanged_MoveToTopEmitsSignal)
{
    int id1 = m_layerManager->createLayer("A");
    int id2 = m_layerManager->createLayer("B");
    (void)id1;

    // 移动图层顺序
    m_layerManager->moveLayerToTop(id2);

    EXPECT_EQ(m_log.orderChangedCount, 1);
}

TEST_F(QtLayerManagerBridgeTest, OnLayerOrderChanged_MoveUpEmitsSignal)
{
    int id1 = m_layerManager->createLayer("A");
    int id2 = m_layerManager->createLayer("B");
    (void)id1;

    m_layerManager->moveLayerUp(id2);

    EXPECT_EQ(m_log.orderChangedCount, 1);
}

// ==================== 注册/注销观察者测试 ====================

TEST_F(QtLayerManagerBridgeTest, UnregisterObserver_StopsSignals)
{
    // 注销观察者
    m_layerManager->removeObserver(m_bridge.get());

    // 创建图层 - 不应发射信号
    m_layerManager->createLayer("AfterUnregister");

    EXPECT_EQ(m_log.addedCount, 0);
}

TEST_F(QtLayerManagerBridgeTest, ReRegisterObserver_ResumesSignals)
{
    // 注销
    m_layerManager->removeObserver(m_bridge.get());

    // 重新注册
    m_layerManager->addObserver(m_bridge.get());

    m_layerManager->createLayer("AfterReregister");

    EXPECT_EQ(m_log.addedCount, 1);
}

// ==================== 边界条件测试 ====================

TEST_F(QtLayerManagerBridgeTest, DoubleRegister_IsIdempotent)
{
    // 重复注册
    m_layerManager->addObserver(m_bridge.get());

    m_layerManager->createLayer("Test");

    // 只应发射一次信号
    EXPECT_EQ(m_log.addedCount, 1);
}

TEST_F(QtLayerManagerBridgeTest, DeleteNonExistentLayer_NoSignal)
{
    // 删除不存在的图层
    m_layerManager->deleteLayer(9999);

    // 不应发射信号
    EXPECT_EQ(m_log.removedCount, 0);
}

TEST_F(QtLayerManagerBridgeTest, RenameNonExistentLayer_NoSignal)
{
    // 重命名不存在的图层
    m_layerManager->renameLayer(9999, "NewName");

    EXPECT_EQ(m_log.changedCount, 0);
}

// ==================== 填充图层相关测试 ====================

TEST_F(QtLayerManagerBridgeTest, OnLayerFillChanged_EmitsSignal)
{
    int layerId = m_layerManager->createLayer("FillLayer");

    // 任何图层变更都应该触发信号
    m_layerManager->renameLayer(layerId, "RenamedFillLayer");

    EXPECT_EQ(m_log.changedCount, 1);
}

// ==================== 压力测试 ====================

TEST_F(QtLayerManagerBridgeTest, ManyLayers_SignalsCorrect)
{
    constexpr int kCount = 100;
    std::vector<int> layerIds;

    // 创建 100 个图层
    for (int i = 0; i < kCount; ++i)
    {
        int id = m_layerManager->createLayer("Layer_" + std::to_string(i));
        ASSERT_GT(id, 0);
        layerIds.push_back(id);
    }

    EXPECT_EQ(m_log.addedCount, kCount);

    // 删除一半
    for (int i = 0; i < kCount / 2; ++i)
    {
        m_layerManager->deleteLayer(layerIds[i]);
    }

    EXPECT_EQ(m_log.removedCount, kCount / 2);
}
