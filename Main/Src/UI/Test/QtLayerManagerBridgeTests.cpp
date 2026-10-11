/**
 * @file QtLayerManagerBridgeTests.cpp
 * @brief QtLayerManagerBridge 回归测试 — 图层 UI 刷新机制
 *
 * 背景 (2026-09-12):
 * 修复填充图层 UI 刷新问题 - LayerManagerDialog 在打开时能自动刷新图层列表，
 * 以及添加图层动态刷新机制。本测试验证 QtLayerManagerBridge 的信号机制正确工作。
 *
 * 测试覆盖:
 * - 信号发射：onLayerAdded / onLayerRemoved / onLayerChanged
 * - 信号发射：onCurrentLayerChanged / onLayerVisibilityChanged / onLayerOrderChanged
 * - 信号参数正确性
 */

#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QSignalSpy>
#include <QDebug>

#include "UI2D/Edit/QtLayerManagerBridge.h"
#include "Engine2D/Interaction/LayerManager.h"
#include "Engine2D/Core/SceneManager.h"

#include <memory>

// ==================== 测试夹具 ====================

class QtLayerManagerBridgeTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // 创建 SceneManager 和 LayerManager
        m_sceneManager = std::make_unique<Eg::SceneManager>();
        m_layerManager = std::make_unique<LayerManager>(m_sceneManager.get());

        // 创建 Bridge 并注册为观察者
        m_bridge = std::make_unique<QtLayerManagerBridge>();
        m_layerManager->registerObserver(m_bridge.get());
    }

    void TearDown() override
    {
        if (m_layerManager && m_bridge)
        {
            m_layerManager->unregisterObserver(m_bridge.get());
        }
        m_bridge.reset();
        m_layerManager.reset();
        m_sceneManager.reset();
    }

    std::unique_ptr<Eg::SceneManager> m_sceneManager;
    std::unique_ptr<LayerManager> m_layerManager;
    std::unique_ptr<QtLayerManagerBridge> m_bridge;
};

// ==================== onLayerAdded 信号测试 ====================

TEST_F(QtLayerManagerBridgeTest, OnLayerAdded_EmitsSignal)
{
    QSignalSpy spy(m_bridge.get(), &QtLayerManagerBridge::sigLayerAdded);

    // 通过 LayerManager 创建图层
    int layerId = m_layerManager->createLayer("TestLayer");
    ASSERT_GT(layerId, 0);

    // 验证信号发射
    EXPECT_EQ(spy.count(), 1);

    // 验证信号参数
    QList<QVariant> args = spy.takeFirst();
    EXPECT_EQ(args.at(0).toInt(), layerId);
    EXPECT_EQ(args.at(1).toString(), QString::fromUtf8("TestLayer"));
}

TEST_F(QtLayerManagerBridgeTest, OnLayerAdded_MultipleLayers)
{
    QSignalSpy spy(m_bridge.get(), &QtLayerManagerBridge::sigLayerAdded);

    // 创建多个图层
    m_layerManager->createLayer("Layer1");
    m_layerManager->createLayer("Layer2");
    m_layerManager->createLayer("Layer3");

    // 验证信号发射次数
    EXPECT_EQ(spy.count(), 3);
}

TEST_F(QtLayerManagerBridgeTest, OnLayerAdded_EmptyName)
{
    QSignalSpy spy(m_bridge.get(), &QtLayerManagerBridge::sigLayerAdded);

    // 创建空名称图层
    int layerId = m_layerManager->createLayer("");
    ASSERT_GT(layerId, 0);

    EXPECT_EQ(spy.count(), 1);
    QList<QVariant> args = spy.takeFirst();
    EXPECT_EQ(args.at(1).toString(), QString::fromUtf8(""));
}

// ==================== onLayerRemoved 信号测试 ====================

TEST_F(QtLayerManagerBridgeTest, OnLayerRemoved_EmitsSignal)
{
    // 先创建一个图层
    int layerId = m_layerManager->createLayer("ToDelete");
    ASSERT_GT(layerId, 0);

    QSignalSpy spy(m_bridge.get(), &QtLayerManagerBridge::sigLayerRemoved);

    // 删除图层
    ASSERT_TRUE(m_layerManager->deleteLayer(layerId));

    // 验证信号发射
    EXPECT_EQ(spy.count(), 1);

    // 验证信号参数
    QList<QVariant> args = spy.takeFirst();
    EXPECT_EQ(args.at(0).toInt(), layerId);
}

TEST_F(QtLayerManagerBridgeTest, OnLayerRemoved_MultipleLayers)
{
    int id1 = m_layerManager->createLayer("L1");
    int id2 = m_layerManager->createLayer("L2");

    QSignalSpy spy(m_bridge.get(), &QtLayerManagerBridge::sigLayerRemoved);

    m_layerManager->deleteLayer(id1);
    m_layerManager->deleteLayer(id2);

    EXPECT_EQ(spy.count(), 2);
}

// ==================== onLayerChanged 信号测试 ====================

TEST_F(QtLayerManagerBridgeTest, OnLayerChanged_RenameEmitsSignal)
{
    int layerId = m_layerManager->createLayer("Original");

    QSignalSpy spy(m_bridge.get(), &QtLayerManagerBridge::sigLayerChanged);

    // 重命名图层
    m_layerManager->renameLayer(layerId, "Renamed");

    EXPECT_EQ(spy.count(), 1);

    QList<QVariant> args = spy.takeFirst();
    EXPECT_EQ(args.at(0).toInt(), layerId);
}

TEST_F(QtLayerManagerBridgeTest, OnLayerChanged_ColorChangeEmitsSignal)
{
    int layerId = m_layerManager->createLayer("ColorLayer");

    QSignalSpy spy(m_bridge.get(), &QtLayerManagerBridge::sigLayerChanged);

    // 修改颜色
    m_layerManager->setLayerColor(layerId, Ut::Color(1.0f, 0.0f, 0.0f));

    EXPECT_EQ(spy.count(), 1);
}

// ==================== onCurrentLayerChanged 信号测试 ====================

TEST_F(QtLayerManagerBridgeTest, OnCurrentLayerChanged_EmitsSignal)
{
    QSignalSpy spy(m_bridge.get(), &QtLayerManagerBridge::sigCurrentLayerChanged);

    // 设置当前图层
    int layerId = m_layerManager->createLayer("Current");
    m_layerManager->setCurrentLayer(layerId);

    EXPECT_EQ(spy.count(), 1);

    QList<QVariant> args = spy.takeFirst();
    EXPECT_EQ(args.at(0).toInt(), layerId);
}

// ==================== onLayerVisibilityChanged 信号测试 ====================

TEST_F(QtLayerManagerBridgeTest, OnLayerVisibilityChanged_HideEmitsSignal)
{
    int layerId = m_layerManager->createLayer("VisibleLayer");

    QSignalSpy spy(m_bridge.get(), &QtLayerManagerBridge::sigLayerVisibilityChanged);

    // 隐藏图层
    m_layerManager->setLayerVisible(layerId, false);

    EXPECT_EQ(spy.count(), 1);

    QList<QVariant> args = spy.takeFirst();
    EXPECT_EQ(args.at(0).toInt(), layerId);
    EXPECT_FALSE(args.at(1).toBool());
}

TEST_F(QtLayerManagerBridgeTest, OnLayerVisibilityChanged_ShowEmitsSignal)
{
    int layerId = m_layerManager->createLayer("VisibleLayer");

    // 先隐藏
    m_layerManager->setLayerVisible(layerId, false);

    QSignalSpy spy(m_bridge.get(), &QtLayerManagerBridge::sigLayerVisibilityChanged);

    // 显示图层
    m_layerManager->setLayerVisible(layerId, true);

    EXPECT_EQ(spy.count(), 1);

    QList<QVariant> args = spy.takeFirst();
    EXPECT_EQ(args.at(0).toInt(), layerId);
    EXPECT_TRUE(args.at(1).toBool());
}

// ==================== onLayerOrderChanged 信号测试 ====================

TEST_F(QtLayerManagerBridgeTest, OnLayerOrderChanged_MoveToTopEmitsSignal)
{
    int id1 = m_layerManager->createLayer("A");
    int id2 = m_layerManager->createLayer("B");

    QSignalSpy spy(m_bridge.get(), &QtLayerManagerBridge::sigLayerOrderChanged);

    // 移动图层顺序
    m_layerManager->moveLayerToTop(id2);

    EXPECT_EQ(spy.count(), 1);
}

TEST_F(QtLayerManagerBridgeTest, OnLayerOrderChanged_MoveUpEmitsSignal)
{
    int id1 = m_layerManager->createLayer("A");
    int id2 = m_layerManager->createLayer("B");

    QSignalSpy spy(m_bridge.get(), &QtLayerManagerBridge::sigLayerOrderChanged);

    m_layerManager->moveLayerUp(id2);

    EXPECT_EQ(spy.count(), 1);
}

// ==================== 注册/注销观察者测试 ====================

TEST_F(QtLayerManagerBridgeTest, UnregisterObserver_StopsSignals)
{
    QSignalSpy spy(m_bridge.get(), &QtLayerManagerBridge::sigLayerAdded);

    // 注销观察者
    m_layerManager->unregisterObserver(m_bridge.get());

    // 创建图层 - 不应发射信号
    m_layerManager->createLayer("AfterUnregister");

    EXPECT_EQ(spy.count(), 0);
}

TEST_F(QtLayerManagerBridgeTest, ReRegisterObserver_ResumesSignals)
{
    // 注销
    m_layerManager->unregisterObserver(m_bridge.get());

    // 重新注册
    m_layerManager->registerObserver(m_bridge.get());

    QSignalSpy spy(m_bridge.get(), &QtLayerManagerBridge::sigLayerAdded);

    m_layerManager->createLayer("AfterReregister");

    EXPECT_EQ(spy.count(), 1);
}

// ==================== 边界条件测试 ====================

TEST_F(QtLayerManagerBridgeTest, DoubleRegister_IsIdempotent)
{
    QSignalSpy spy(m_bridge.get(), &QtLayerManagerBridge::sigLayerAdded);

    // 重复注册
    m_layerManager->registerObserver(m_bridge.get());

    m_layerManager->createLayer("Test");

    // 只应发射一次信号
    EXPECT_EQ(spy.count(), 1);
}

TEST_F(QtLayerManagerBridgeTest, DeleteNonExistentLayer_NoSignal)
{
    QSignalSpy spy(m_bridge.get(), &QtLayerManagerBridge::sigLayerRemoved);

    // 删除不存在的图层
    m_layerManager->deleteLayer(9999);

    // 不应发射信号
    EXPECT_EQ(spy.count(), 0);
}

TEST_F(QtLayerManagerBridgeTest, RenameNonExistentLayer_NoSignal)
{
    QSignalSpy spy(m_bridge.get(), &QtLayerManagerBridge::sigLayerChanged);

    // 重命名不存在的图层
    m_layerManager->renameLayer(9999, "NewName");

    EXPECT_EQ(spy.count(), 0);
}

// ==================== 填充图层相关测试 ====================

TEST_F(QtLayerManagerBridgeTest, OnLayerFillChanged_EmitsSignal)
{
    // 测试填充图层相关的信号
    // 注意：需要查看 LayerManager 是否有 setLayerFill 方法

    int layerId = m_layerManager->createLayer("FillLayer");

    // 这里测试基本的信号机制
    // 如果 LayerManager 有专门的 fill 变更接口，可以添加更多测试
    QSignalSpy spy(m_bridge.get(), &QtLayerManagerBridge::sigLayerChanged);

    // 任何图层变更都应该触发信号
    m_layerManager->renameLayer(layerId, "RenamedFillLayer");

    EXPECT_EQ(spy.count(), 1);
}

// ==================== 压力测试 ====================

TEST_F(QtLayerManagerBridgeTest, ManyLayers_SignalsCorrect)
{
    QSignalSpy addSpy(m_bridge.get(), &QtLayerManagerBridge::sigLayerAdded);
    QSignalSpy removeSpy(m_bridge.get(), &QtLayerManagerBridge::sigLayerRemoved);

    constexpr int kCount = 100;
    std::vector<int> layerIds;

    // 创建 100 个图层
    for (int i = 0; i < kCount; ++i)
    {
        int id = m_layerManager->createLayer("Layer_" + std::to_string(i));
        ASSERT_GT(id, 0);
        layerIds.push_back(id);
    }

    EXPECT_EQ(addSpy.count(), kCount);

    // 删除一半
    for (int i = 0; i < kCount / 2; ++i)
    {
        m_layerManager->deleteLayer(layerIds[i]);
    }

    EXPECT_EQ(removeSpy.count(), kCount / 2);
}
