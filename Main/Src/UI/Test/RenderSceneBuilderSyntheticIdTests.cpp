/**
 * @file RenderSceneBuilderSyntheticIdTests.cpp
 * @brief 合成 ID 号段隔离回归测试（P3）
 *
 * 复现：未设 ID 的发射源（数据源没调 setCurrentEntityId 就 emit）走合成归属 ID。
 * 旧实现用 ++m_syntheticId 从 1 起，与真实持久 ID（1 起递增）同号空间，会和 id=1
 * 的真实图元共用 m_pieces[1]，段位游标踩踏、trimPieces/endRebuild 误裁 → 残影/误裁。
 *
 * 修法：合成 ID 从 INT64_MAX 向下递减，与真实持久 ID、临时 ID（-1 递减）永不相交。
 * 本测试用 Null 后端设备（无 GPU）+ 最小化假场景走完 addPiece 全链路，断言
 * 真实图元 id=1 名下只有它自己这一段，合成段落在独立高位号段。
 *
 * 为什么 Null 后端 + 假场景：几何块的分配/写入只依赖设备（Null 后端即完整实现），
 * 常驻绘制列表才依赖场景。这里给一个 createDrawList 返回有效句柄、upsertDrawItem
 * 恒 true 的最小假场景，即可让 RenderSceneBuilder::addPiece 的 allocBlock →
 * writeBlock → acquireSlot → upsertDrawItem → 登记 piece 整条链走通，无需真实 GPU。
 */
#include <gtest/gtest.h>

#include <cstdint>
#include <memory>

#include "Render/RenderSceneBuilder.h"
#include "RenderAbstraction/IRenderFactory.h"
#include "RenderAbstraction/IRenderScene.h"
#include "RenderBridge/RenderXAdapter.h"

namespace
{
    /// 最小化假场景：只为让常驻绘制列表初始化成功，其余全部空实现。
    class NullSceneStub final : public RenderAbstraction::IRenderScene
    {
    public:
        bool allocTransient(uint64_t, RenderAbstraction::TransientAlloc&) override
        {
            return false;
        }
        bool submit(const RenderAbstraction::DrawPacket&) override
        {
            return false;
        }
        void setModelMatrix(const RenderAbstraction::Matrix4x4*) override {}

        RenderAbstraction::DrawListHandle createDrawList(const RenderAbstraction::DrawListDesc&) override
        {
            RenderAbstraction::DrawListHandle h;
            h.value = 1;  // 非 0 即视为有效句柄（isValid: value != 0）
            return h;
        }
        void destroyDrawList(RenderAbstraction::DrawListHandle) override {}
        void clearDrawList(RenderAbstraction::DrawListHandle) override {}
        bool upsertDrawItem(RenderAbstraction::DrawListHandle,
            uint32_t,
            const RenderAbstraction::DrawInstruction&,
            const RenderAbstraction::Aabb3*) override
        {
            return true;
        }
        bool removeDrawItem(RenderAbstraction::DrawListHandle, uint32_t) override
        {
            return true;
        }
        bool submitDrawList(RenderAbstraction::DrawListHandle, const RenderAbstraction::ViewVolume*) override
        {
            return false;
        }

        bool beginFrame() override
        {
            return true;
        }
        void endFrame() override {}

        RenderAbstraction::TextureHandle createRenderTarget(uint32_t, uint32_t) override
        {
            return {};
        }
        bool readPixels(uint32_t, uint32_t, uint32_t, uint32_t, void*, uint64_t) override
        {
            return false;
        }
        bool setRenderTarget(const RenderAbstraction::RenderTargetBinding&) override
        {
            return false;
        }
        bool readPixelsFromTexture(RenderAbstraction::TextureHandle,
            uint32_t,
            uint32_t,
            uint32_t,
            uint32_t,
            void*,
            uint64_t) override
        {
            return false;
        }

        void setCamera(const RenderAbstraction::CameraDesc&) override {}
        void setLighting(const RenderAbstraction::LightingDesc&) override {}

        RenderAbstraction::FrameStatistics getFrameStatistics() const override
        {
            return {};
        }
        bool isValid() const override
        {
            return true;
        }
    };
}  // namespace

TEST(RenderSceneBuilderSyntheticIdTest, SyntheticOwnerDoesNotCollideWithRealId)
{
    auto factory = RenderBridge::RenderXAdapter::createFactory();
    ASSERT_NE(factory, nullptr);

    RenderAbstraction::DeviceConfig config;
    config.backend = RenderAbstraction::RenderBackend::Null;
    config.enableValidation = true;
    config.applicationName = "RenderSceneBuilderSyntheticIdTests";
    auto device = factory->createDevice(config);
    ASSERT_NE(device, nullptr);

    NullSceneStub scene;
    Render::RenderSceneBuilder builder;
    ASSERT_TRUE(builder.initialize(*device, scene));
    ASSERT_TRUE(builder.valid());

    const Ut::Vec2d pts[] = { Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) };

    // 真实图元 id=1：先标 ID 再发射，段挂到 m_pieces[1]
    builder.setCurrentEntityId(1);
    builder.emitPolyline(pts, 2, false);

    // 未设 ID 的发射源：setCurrentEntityId(0) 后发射，段走合成号段（INT64_MAX 向下递减）
    builder.setCurrentEntityId(0);
    builder.emitPolyline(pts, 2, false);

    // 真实图元 id=1 名下只应有它自己这一段（旧实现这里会变成 2，段被合成路径污染）
    EXPECT_EQ(builder.pieceCountFor(1), 1u);

    // 合成段应落在独立高位号段 INT64_MAX-1，而不是 1、2、... 这些真实持久 ID
    const Render::EntityId syntheticOwner = static_cast<Render::EntityId>(INT64_MAX) - 1;
    EXPECT_EQ(builder.pieceCountFor(syntheticOwner), 1u);

    builder.shutdown();
}