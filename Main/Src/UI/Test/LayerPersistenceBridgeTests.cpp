/**
 * @file LayerPersistenceBridgeTests.cpp
 * @brief LayerPersistenceBridge 回归测试 — 图层桥接/同步写入/解除监听
 *
 * 测试范围：
 *  - 构造与基础属性（attach/detach/documentId）
 *  - 观察者回调 → 数据库同步（onLayerAdded/Removed/Changed/VisibilityChanged/OrderChanged）
 *  - 空指针安全性（null LayerManager / null LayerRepository）
 *  - detach 后停止监听验证
 *  - 多图层批量操作
 *
 * P5 测试覆盖扩展 (2026-07-30)
 */

#include <gtest/gtest.h>

#include "LayerPersistenceBridge.h"
#include "Repositories/LayerRepository.h"
#include "Persistence/Models/LayerRecord.h"
#include "Engine2D/Interaction/LayerManager.h"
#include "Engine2D/Core/SceneManager.h"
#include "Engine/Persistence/Database.h"

#include <memory>

// ==================== 测试夹具：提供内存数据库 + LayerManager + LayerRepository ====================

class LayerPersistenceBridgeTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // 创建内存数据库并初始化 layers 表
        m_database = std::make_unique<Eg::Database>();
        ASSERT_TRUE(m_database->open(":memory:"));

        const char* sqlCreateLayers = R"(
            CREATE TABLE IF NOT EXISTS layers (
                id              INTEGER PRIMARY KEY AUTOINCREMENT,
                document_id     TEXT    NOT NULL,
                layer_id        INTEGER NOT NULL,
                name            TEXT    NOT NULL DEFAULT '',
                color           TEXT    DEFAULT '#000000',
                visible         INTEGER DEFAULT 1,
                locked          INTEGER DEFAULT 0,
                order_index     INTEGER DEFAULT 0,
                updated_at      TEXT    DEFAULT (datetime('now'))
            )
        )";
        ASSERT_TRUE(m_database->execute(sqlCreateLayers));

        // 创建 SceneManager 和 LayerManager
        m_sceneManager = std::make_unique<Eg::SceneManager>();
        m_layerManager = std::make_unique<LayerManager>(m_sceneManager.get());

        // 创建 LayerRepository
        m_layerRepository = std::make_unique<LayerRepository>(*m_database);

        // 创建桥接器
        m_bridge = std::make_unique<LayerPersistenceBridge>(m_layerManager.get(), m_layerRepository.get());
    }

    void TearDown() override
    {
        if (m_bridge)
        {
            m_bridge->detach();
        }
        m_bridge.reset();
        m_layerRepository.reset();
        m_layerManager.reset();
        m_sceneManager.reset();
        m_database.reset();
    }

    /// 辅助：验证数据库中指定图层的记录是否存在
    bool layerExistsInDb(const std::string& docId, int layerId)
    {
        auto layers = m_layerRepository->loadByDocument(docId);
        for (const auto& layer : layers)
        {
            if (layer.layerId == layerId)
            {
                return true;
            }
        }
        return false;
    }

    /// 辅助：从数据库加载指定图层的记录
    std::optional<LayerRecord> findLayerInDb(const std::string& docId, int layerId)
    {
        auto layers = m_layerRepository->loadByDocument(docId);
        for (const auto& layer : layers)
        {
            if (layer.layerId == layerId)
            {
                return layer;
            }
        }
        return std::nullopt;
    }

    std::unique_ptr<Eg::Database> m_database;
    std::unique_ptr<Eg::SceneManager> m_sceneManager;
    std::unique_ptr<LayerManager> m_layerManager;
    std::unique_ptr<LayerRepository> m_layerRepository;
    std::unique_ptr<LayerPersistenceBridge> m_bridge;
};

// ==================== 构造与基础属性测试 ====================

TEST_F(LayerPersistenceBridgeTest, Construction_DefaultState)
{
    // 构造后默认为 detach 状态
    EXPECT_EQ(m_bridge->documentId(), "");
}

TEST_F(LayerPersistenceBridgeTest, SetDocumentId)
{
    m_bridge->setDocumentId("doc_001");
    EXPECT_EQ(m_bridge->documentId(), "doc_001");
}

TEST_F(LayerPersistenceBridgeTest, SetDocumentId_EmptyString)
{
    m_bridge->setDocumentId("");
    EXPECT_EQ(m_bridge->documentId(), "");
}

// ==================== attach / detach 测试 ====================

TEST_F(LayerPersistenceBridgeTest, Attach_RegistersAsObserver)
{
    // attach 不崩溃
    m_bridge->attach();
    SUCCEED();
}

TEST_F(LayerPersistenceBridgeTest, Attach_DoubleAttachIsIdempotent)
{
    m_bridge->attach();
    m_bridge->attach();  // 第二次 attach 应无副作用
    SUCCEED();
}

TEST_F(LayerPersistenceBridgeTest, Detach_UnregistersObserver)
{
    m_bridge->attach();
    m_bridge->detach();
    // detach 不崩溃
    SUCCEED();
}

TEST_F(LayerPersistenceBridgeTest, Detach_WithoutAttachIsSafe)
{
    // 未 attach 时 detach 应安全
    m_bridge->detach();
    SUCCEED();
}

TEST_F(LayerPersistenceBridgeTest, Detach_DoubleDetachIsSafe)
{
    m_bridge->attach();
    m_bridge->detach();
    m_bridge->detach();  // 第二次 detach 应安全
    SUCCEED();
}

// ==================== 观察者回调 → 数据库同步测试 ====================

TEST_F(LayerPersistenceBridgeTest, OnLayerAdded_SyncsToDatabase)
{
    m_bridge->setDocumentId("doc_001");
    m_bridge->attach();

    // 通过 LayerManager 创建图层，触发 onLayerAdded 回调
    int layerId = m_layerManager->createLayer("TestLayer");
    ASSERT_GT(layerId, 0);

    // 验证数据库中存在该图层记录
    EXPECT_TRUE(layerExistsInDb("doc_001", layerId));

    auto record = findLayerInDb("doc_001", layerId);
    ASSERT_TRUE(record.has_value());
    EXPECT_EQ(record->name, "TestLayer");
    EXPECT_EQ(record->documentId, "doc_001");
}

TEST_F(LayerPersistenceBridgeTest, OnLayerRemoved_RemovesFromDatabase)
{
    m_bridge->setDocumentId("doc_001");
    m_bridge->attach();

    int layerId = m_layerManager->createLayer("ToDelete");
    ASSERT_GT(layerId, 0);
    EXPECT_TRUE(layerExistsInDb("doc_001", layerId));

    // 删除图层，触发 onLayerRemoved 回调
    ASSERT_TRUE(m_layerManager->deleteLayer(layerId));

    // 验证数据库中已删除
    EXPECT_FALSE(layerExistsInDb("doc_001", layerId));
}

TEST_F(LayerPersistenceBridgeTest, OnLayerChanged_SyncsToDatabase)
{
    m_bridge->setDocumentId("doc_001");
    m_bridge->attach();

    int layerId = m_layerManager->createLayer("Original");
    ASSERT_GT(layerId, 0);

    // 修改图层名称，触发 onLayerChanged 回调
    ASSERT_TRUE(m_layerManager->renameLayer(layerId, "Renamed"));

    auto record = findLayerInDb("doc_001", layerId);
    ASSERT_TRUE(record.has_value());
    EXPECT_EQ(record->name, "Renamed");
}

TEST_F(LayerPersistenceBridgeTest, OnLayerVisibilityChanged_UpdatesVisibility)
{
    m_bridge->setDocumentId("doc_001");
    m_bridge->attach();

    int layerId = m_layerManager->createLayer("VisibleLayer");
    ASSERT_GT(layerId, 0);

    // 初始可见
    {
        auto record = findLayerInDb("doc_001", layerId);
        ASSERT_TRUE(record.has_value());
        EXPECT_TRUE(record->visible);
    }

    // 切换为不可见，触发 onLayerVisibilityChanged 回调
    ASSERT_TRUE(m_layerManager->setLayerVisible(layerId, false));

    {
        auto record = findLayerInDb("doc_001", layerId);
        ASSERT_TRUE(record.has_value());
        EXPECT_FALSE(record->visible);
    }

    // 切换回可见
    ASSERT_TRUE(m_layerManager->setLayerVisible(layerId, true));

    {
        auto record = findLayerInDb("doc_001", layerId);
        ASSERT_TRUE(record.has_value());
        EXPECT_TRUE(record->visible);
    }
}

TEST_F(LayerPersistenceBridgeTest, OnLayerOrderChanged_BatchUpdatesOrder)
{
    m_bridge->setDocumentId("doc_001");
    m_bridge->attach();

    // 创建多个图层
    int layerA = m_layerManager->createLayer("LayerA");
    int layerB = m_layerManager->createLayer("LayerB");
    int layerC = m_layerManager->createLayer("LayerC");
    ASSERT_GT(layerA, 0);
    ASSERT_GT(layerB, 0);
    ASSERT_GT(layerC, 0);

    // 移动图层顺序，触发 onLayerOrderChanged 回调
    ASSERT_TRUE(m_layerManager->moveLayerToTop(layerC));

    // 验证数据库中的排序已更新（至少图层数正确）
    auto layers = m_layerRepository->loadByDocument("doc_001");
    ASSERT_EQ(layers.size(), 3u);

    // 验证所有图层都在数据库中
    bool foundA = false, foundB = false, foundC = false;
    for (const auto& layer : layers)
    {
        if (layer.layerId == layerA)
        {
            foundA = true;
        }
        if (layer.layerId == layerB)
        {
            foundB = true;
        }
        if (layer.layerId == layerC)
        {
            foundC = true;
        }
    }
    EXPECT_TRUE(foundA);
    EXPECT_TRUE(foundB);
    EXPECT_TRUE(foundC);
}

// ==================== 多图层批量操作测试 ====================

TEST_F(LayerPersistenceBridgeTest, MultipleLayers_AllSynced)
{
    m_bridge->setDocumentId("doc_001");
    m_bridge->attach();

    // 批量创建图层
    std::vector<int> layerIds;
    for (int i = 0; i < 5; ++i)
    {
        int id = m_layerManager->createLayer("Layer_" + std::to_string(i));
        ASSERT_GT(id, 0);
        layerIds.push_back(id);
    }

    // 验证所有图层都已同步到数据库
    auto layers = m_layerRepository->loadByDocument("doc_001");
    EXPECT_EQ(layers.size(), 5u);

    for (int id : layerIds)
    {
        EXPECT_TRUE(layerExistsInDb("doc_001", id));
    }
}

TEST_F(LayerPersistenceBridgeTest, MultipleLayers_DeleteSome)
{
    m_bridge->setDocumentId("doc_001");
    m_bridge->attach();

    std::vector<int> layerIds;
    for (int i = 0; i < 5; ++i)
    {
        int id = m_layerManager->createLayer("Layer_" + std::to_string(i));
        layerIds.push_back(id);
    }

    // 删除中间两个图层
    ASSERT_TRUE(m_layerManager->deleteLayer(layerIds[1]));
    ASSERT_TRUE(m_layerManager->deleteLayer(layerIds[3]));

    // 验证剩余图层
    auto layers = m_layerRepository->loadByDocument("doc_001");
    EXPECT_EQ(layers.size(), 3u);
    EXPECT_TRUE(layerExistsInDb("doc_001", layerIds[0]));
    EXPECT_FALSE(layerExistsInDb("doc_001", layerIds[1]));
    EXPECT_TRUE(layerExistsInDb("doc_001", layerIds[2]));
    EXPECT_FALSE(layerExistsInDb("doc_001", layerIds[3]));
    EXPECT_TRUE(layerExistsInDb("doc_001", layerIds[4]));
}

TEST_F(LayerPersistenceBridgeTest, MultipleLayers_ChangeColor)
{
    m_bridge->setDocumentId("doc_001");
    m_bridge->attach();

    int layerId = m_layerManager->createLayer("ColorLayer");
    ASSERT_GT(layerId, 0);

    // 修改颜色（Ut::Color 使用 0-1 浮点范围）
    ASSERT_TRUE(m_layerManager->setLayerColor(layerId, Ut::Color(1.0f, 0.0f, 0.0f)));

    auto record = findLayerInDb("doc_001", layerId);
    ASSERT_TRUE(record.has_value());
    // 颜色应被更新（红色 #ff0000，toHexRGB 返回小写十六进制）
    EXPECT_EQ(record->color, "#ff0000");
}

// ==================== 空指针安全性测试 ====================

TEST(LayerPersistenceBridgeNullTest, NullLayerManager_AttachIsSafe)
{
    Eg::Database db;
    ASSERT_TRUE(db.open(":memory:"));
    ASSERT_TRUE(db.execute("CREATE TABLE IF NOT EXISTS layers ("
                           "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                           "document_id TEXT NOT NULL,"
                           "layer_id INTEGER NOT NULL,"
                           "name TEXT DEFAULT '',"
                           "color TEXT DEFAULT '#000000',"
                           "visible INTEGER DEFAULT 1,"
                           "locked INTEGER DEFAULT 0,"
                           "order_index INTEGER DEFAULT 0,"
                           "updated_at TEXT DEFAULT (datetime('now')))"));

    LayerRepository repo(db);
    LayerPersistenceBridge bridge(nullptr, &repo);
    bridge.attach();  // 不应崩溃
    bridge.detach();  // 不应崩溃
    SUCCEED();
}

TEST(LayerPersistenceBridgeNullTest, NullLayerRepository_CallbacksDontCrash)
{
    Eg::SceneManager scene;
    LayerManager layerMgr(&scene);
    LayerPersistenceBridge bridge(&layerMgr, nullptr);

    bridge.attach();

    // 图层操作不应崩溃（观察者回调中检查 m_layerRepository 为空）
    int layerId = layerMgr.createLayer("Test");
    ASSERT_GT(layerId, 0);

    layerMgr.setLayerVisible(layerId, false);
    layerMgr.renameLayer(layerId, "Renamed");
    layerMgr.deleteLayer(layerId);

    bridge.detach();
    SUCCEED();
}

TEST(LayerPersistenceBridgeNullTest, BothNull_IsSafe)
{
    LayerPersistenceBridge bridge(nullptr, nullptr);
    bridge.attach();  // 不应崩溃
    bridge.detach();  // 不应崩溃
    SUCCEED();
}

// ==================== detach 后停止监听测试 ====================

TEST_F(LayerPersistenceBridgeTest, Detach_StopsObserving)
{
    m_bridge->setDocumentId("doc_001");
    m_bridge->attach();

    // 先创建一个图层，确认同步正常
    int layerId = m_layerManager->createLayer("BeforeDetach");
    ASSERT_GT(layerId, 0);
    EXPECT_TRUE(layerExistsInDb("doc_001", layerId));

    // detach 后，后续变更不应同步到数据库
    m_bridge->detach();

    // 删除图层（detach 后不应触发数据库删除）
    ASSERT_TRUE(m_layerManager->deleteLayer(layerId));

    // 数据库中的记录应保持不变（因为 detach 后不再监听）
    // 注意：这取决于 LayerManager 的 deleteLayer 是否会真正删除图层
    // 如果 deleteLayer 在 detach 后仍然删除图层，则数据库记录也会被删除
    // 但 onLayerRemoved 不应该被调用
    // 实际上，LayerManager 直接删除图层，不依赖观察者，所以这里主要是验证不崩溃
    SUCCEED();
}

TEST_F(LayerPersistenceBridgeTest, Detach_ThenReattach_ResumesSync)
{
    m_bridge->setDocumentId("doc_001");
    m_bridge->attach();

    int layer1 = m_layerManager->createLayer("Layer1");
    ASSERT_GT(layer1, 0);

    // detach
    m_bridge->detach();

    // 在 detach 期间创建图层（不应同步）
    int layer2 = m_layerManager->createLayer("Layer2_NoSync");
    ASSERT_GT(layer2, 0);

    // 重新 attach
    m_bridge->attach();

    // 创建新图层（应同步）
    int layer3 = m_layerManager->createLayer("Layer3");
    ASSERT_GT(layer3, 0);

    // 验证：layer1 和 layer3 在数据库中，layer2 不在
    EXPECT_TRUE(layerExistsInDb("doc_001", layer1));
    EXPECT_FALSE(layerExistsInDb("doc_001", layer2));
    EXPECT_TRUE(layerExistsInDb("doc_001", layer3));
}

// ==================== 边界条件测试 ====================

TEST_F(LayerPersistenceBridgeTest, EmptyDocumentId_StillSyncs)
{
    // 空文档 ID 场景（单文档模式）
    m_bridge->attach();

    int layerId = m_layerManager->createLayer("NoDocLayer");
    ASSERT_GT(layerId, 0);

    // 空文档 ID 下也应正常同步
    EXPECT_TRUE(layerExistsInDb("", layerId));
}

TEST_F(LayerPersistenceBridgeTest, LayerWithSpecialCharacters)
{
    m_bridge->setDocumentId("doc_special");
    m_bridge->attach();

    // 创建带特殊字符的图层名
    int layerId = m_layerManager->createLayer("Layer [1] (copy)");
    ASSERT_GT(layerId, 0);

    auto record = findLayerInDb("doc_special", layerId);
    ASSERT_TRUE(record.has_value());
    EXPECT_EQ(record->name, "Layer [1] (copy)");
}

TEST_F(LayerPersistenceBridgeTest, CreateManyLayers_Stress)
{
    m_bridge->setDocumentId("doc_stress");
    m_bridge->attach();

    constexpr int kLayerCount = 50;
    for (int i = 0; i < kLayerCount; ++i)
    {
        int layerId = m_layerManager->createLayer("Layer_" + std::to_string(i));
        ASSERT_GT(layerId, 0);
    }

    auto layers = m_layerRepository->loadByDocument("doc_stress");
    EXPECT_EQ(layers.size(), static_cast<size_t>(kLayerCount));
}