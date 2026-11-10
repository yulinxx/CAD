/**
 * @file SceneSerializerAlphaTests.cpp
 * @brief SceneManagerSerializer 颜色 alpha 持久化回归测试
 *
 * 覆盖：
 *  - 图层色/填充色 alpha 经 EnginePersistence 序列化后原样往返
 *  - alpha=255 的不透明色往返
 *  - 历史数据 alpha 位为 0 时回落到不透明
 *  - 图元 overrideColor 的 alpha 往返
 *  - 无覆盖色的图元加载后仍跟随图层色
 */

#include <gtest/gtest.h>

#include "Engine/Persistence/SceneManagerSerializer.h"
#include "Engine2D/Core/SceneManager.h"
#include "Engine2D/Core/SceneWriteGateway.h"
#include "Engine2D/Interaction/LayerManager.h"
#include "Engine2D/AprEntity/AprFillRegion.h"
#include "Engine2D/AprEntity/AprPolygon.h"
#include "Ut/Color.h"

#include <memory>
#include <vector>

class SceneSerializerAlphaTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        m_scene = std::make_unique<Eg::SceneManager>();
        m_layers = std::make_unique<LayerManager>(m_scene.get());
        m_serializer.setLayerManager(m_layers.get());
    }

    /// 序列化当前场景到内存
    std::vector<uint8_t> serializeScene()
    {
        const auto buffer = m_serializer.serializeToBuffer(m_scene.get());
        EXPECT_TRUE(buffer.has_value());
        return buffer.has_value() ? *buffer : std::vector<uint8_t>();
    }

    /// 从内存反序列化到新的场景 + 图层管理器
    void deserializeScene(const std::vector<uint8_t>& data, Eg::SceneManager& scene, LayerManager& layers)
    {
        m_serializer.setLayerManager(&layers);
        const auto result = m_serializer.deserializeFromBuffer(&scene, data.data(), data.size());
        EXPECT_EQ(result.error, Eg::SceneSerializerError::Success) << result.errorMessage;
    }

    std::unique_ptr<Eg::SceneManager> m_scene;
    std::unique_ptr<LayerManager> m_layers;
    Eg::SceneManagerSerializer m_serializer;
};

// ==================== 图层颜色 alpha ====================

TEST_F(SceneSerializerAlphaTest, LayerColorAlphaRoundTrip)
{
    const int layerId = m_layers->createLayer("AlphaLayer");
    ASSERT_GT(layerId, 0);
    ASSERT_TRUE(m_layers->setLayerColor(layerId, Ut::Color::fromRGB255(31, 97, 191, 128)));
    ASSERT_TRUE(m_layers->setLayerFill(layerId, true));
    ASSERT_TRUE(m_layers->setLayerFillColor(layerId, Ut::Color::fromRGB255(200, 40, 10, 64)));

    const auto data = serializeScene();
    ASSERT_FALSE(data.empty());

    Eg::SceneManager restoredScene;
    LayerManager restoredLayers(&restoredScene);
    deserializeScene(data, restoredScene, restoredLayers);

    const int restoredId = restoredLayers.findLayerByName("AlphaLayer");
    ASSERT_GT(restoredId, 0);

    const Ut::Color color = restoredLayers.layerColor(restoredId);
    EXPECT_EQ(color.red255(), 31);
    EXPECT_EQ(color.green255(), 97);
    EXPECT_EQ(color.blue255(), 191);
    EXPECT_EQ(color.alpha255(), 128);

    const Ut::Color fillColor = restoredLayers.layerFillColor(restoredId);
    EXPECT_EQ(fillColor.red255(), 200);
    EXPECT_EQ(fillColor.green255(), 40);
    EXPECT_EQ(fillColor.blue255(), 10);
    EXPECT_EQ(fillColor.alpha255(), 64);
}

TEST_F(SceneSerializerAlphaTest, LayerColorOpaqueRoundTrip)
{
    const int layerId = m_layers->createLayer("OpaqueLayer");
    ASSERT_GT(layerId, 0);
    ASSERT_TRUE(m_layers->setLayerColor(layerId, Ut::Color::fromRGB255(31, 97, 191, 255)));

    const auto data = serializeScene();
    ASSERT_FALSE(data.empty());

    Eg::SceneManager restoredScene;
    LayerManager restoredLayers(&restoredScene);
    deserializeScene(data, restoredScene, restoredLayers);

    const int restoredId = restoredLayers.findLayerByName("OpaqueLayer");
    ASSERT_GT(restoredId, 0);
    EXPECT_EQ(restoredLayers.layerColor(restoredId).alpha255(), 255);
}

TEST_F(SceneSerializerAlphaTest, LegacyZeroAlphaFallsBackOpaque)
{
    // 历史数据 alpha 位为 0：加载后必须回落到不透明，RGB 不变
    const int layerId = m_layers->createLayer("LegacyAlphaLayer");
    ASSERT_GT(layerId, 0);
    ASSERT_TRUE(m_layers->setLayerColor(layerId, Ut::Color::fromRGB255(10, 20, 30, 0)));

    const auto data = serializeScene();
    ASSERT_FALSE(data.empty());

    Eg::SceneManager restoredScene;
    LayerManager restoredLayers(&restoredScene);
    deserializeScene(data, restoredScene, restoredLayers);

    const int restoredId = restoredLayers.findLayerByName("LegacyAlphaLayer");
    ASSERT_GT(restoredId, 0);
    const Ut::Color color = restoredLayers.layerColor(restoredId);
    EXPECT_EQ(color.red255(), 10);
    EXPECT_EQ(color.green255(), 20);
    EXPECT_EQ(color.blue255(), 30);
    EXPECT_EQ(color.alpha255(), 255);
}

// ==================== 图元覆盖色 alpha ====================

TEST_F(SceneSerializerAlphaTest, EntityOverrideColorAlphaRoundTrip)
{
    auto fill = std::make_unique<Eg::AprFillRegion>();
    fill->id = 2001;
    const Ut::Vec2d tri[] = { Ut::Vec2d(0.0, 0.0), Ut::Vec2d(10.0, 0.0), Ut::Vec2d(10.0, 10.0) };
    fill->setVertices(tri, 3);
    fill->setOverrideColor(Ut::Color::fromRGB255(90, 160, 220, 96));
    ASSERT_TRUE(Eg::SceneWriteGateway::insertEntityPreserveId(*m_scene, std::move(fill)));

    const auto data = serializeScene();
    ASSERT_FALSE(data.empty());

    Eg::SceneManager restoredScene;
    LayerManager restoredLayers(&restoredScene);
    deserializeScene(data, restoredScene, restoredLayers);

    const auto entities = restoredScene.getAllEntities();
    ASSERT_EQ(entities.size(), 1u);
    ASSERT_EQ(entities[0]->eType, Eg::EType::FILL_REGION);
    EXPECT_EQ(static_cast<const Eg::AprFillRegion*>(entities[0])->vertexCount(), 3u);
    const auto overrideColor = entities[0]->overrideColor();
    ASSERT_TRUE(overrideColor.has_value());
    EXPECT_EQ(overrideColor->red255(), 90);
    EXPECT_EQ(overrideColor->green255(), 160);
    EXPECT_EQ(overrideColor->blue255(), 220);
    EXPECT_EQ(overrideColor->alpha255(), 96);
    EXPECT_EQ(entities[0]->getColor().alpha255(), 96);
}

TEST_F(SceneSerializerAlphaTest, EntityWithoutOverrideColorStaysFollowingLayer)
{
    auto poly = std::make_unique<Eg::AprPolygon>();
    poly->id = 2002;
    poly->setVertices({ Ut::Vec2d(0.0, 0.0), Ut::Vec2d(10.0, 0.0), Ut::Vec2d(10.0, 10.0) });
    ASSERT_TRUE(Eg::SceneWriteGateway::insertEntityPreserveId(*m_scene, std::move(poly)));

    const auto data = serializeScene();
    ASSERT_FALSE(data.empty());

    Eg::SceneManager restoredScene;
    LayerManager restoredLayers(&restoredScene);
    deserializeScene(data, restoredScene, restoredLayers);

    const auto entities = restoredScene.getAllEntities();
    ASSERT_EQ(entities.size(), 1u);
    // 缺失 override_argb 表示跟随图层色，不能变成独立覆盖色
    EXPECT_FALSE(entities[0]->overrideColor().has_value());
}
