/**
 * @file FillAlphaVertexTests.cpp
 * @brief 2D 场景几何 alpha 通道回归测试（不依赖 GPU / 真实窗口）
 *
 * 背景：填充对话框已把 alpha 一路传到场景实体颜色，但场景几何顶点此前只有 RGB，
 * 片元着色器把 alpha 写死为 1，半透明色块看起来仍是实心。
 *
 * 本测试用 CPU 侧假设备 + 假场景直接检查 RenderSceneBuilder 的装配产物：
 *   - 写入几何仓的顶点是否带正确 alpha（步长 28 字节的 P3C4）；
 *   - 绘制命令的顶点格式是否为 PositionColorAlpha；
 *   - alpha = 1.0 时顶点字节与旧 P3C3 路径逐字节等价（位置 + RGB + 不透明标记）；
 *   - 只改 alpha 时内容哈希失效，几何会被重新写入。
 */
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <unordered_map>
#include <vector>

#include "Engine2D/Algorithm/EntityColorFiller.h"
#include "Engine2D/Algorithm/EntityContourFiller.h"
#include "Engine2D/Core/SceneManager.h"
#include "Engine2D/Interaction/LayerManager.h"
#include "Engine2D/Geometry/EntityGeometryEmitter.h"
#include "Engine2D/SyEntity/SyFillRegion.h"
#include "Engine2D/SyEntity/SyPolygon.h"

#include "Render/RenderSceneBuilder.h"
#include "RenderAbstraction/IRenderDevice.h"
#include "RenderAbstraction/IRenderScene.h"
#include "Ut/Color.h"
#include "Ut/Vec.h"
#include "Ut/VertexFormat.hpp"

namespace RA = RenderAbstraction;

namespace
{
    /**
     * @brief CPU 侧几何仓假设备
     *
     * 块分配与写入全部落在内存里，测试可直接回读顶点字节。
     * 分配粒度与真实仓保持一致（4 字节对齐），用于覆盖 28 字节步长的对齐前提。
     */
    class RecordingDeviceStub final : public RA::IRenderDevice
    {
    public:
        struct Block
        {
            uint32_t offset{ 0 };
            uint32_t size{ 0 };
        };

        struct Store
        {
            std::vector<uint8_t> bytes;
            std::unordered_map<uint64_t, Block> blocks;
            uint64_t nextBlockId{ 1 };
        };

        const char* backendName() const override
        {
            return "RecordingStub";
        }

        const char* deviceName() const override
        {
            return "RecordingStub";
        }

        RA::TextureHandle createTexture(const RA::TextureDesc&) override
        {
            return {};
        }

        void destroyTexture(RA::TextureHandle) override {}

        void updateTexture(RA::TextureHandle, uint32_t, uint32_t, uint32_t, uint32_t, const uint8_t*) override {}

        RA::GeometryStoreHandle createGeometryStore(const RA::GeometryStoreDesc&) override
        {
            m_stores.emplace_back();
            RA::GeometryStoreHandle handle;
            handle.value = m_stores.size();
            return handle;
        }

        void destroyGeometryStore(RA::GeometryStoreHandle) override {}

        RA::GeometryAllocResult allocGeometry(
            RA::GeometryStoreHandle store, uint64_t bytes, RA::GeometryBlock& out) override
        {
            Store* target = storeFor(store);
            if (target == nullptr || bytes == 0)
            {
                return RA::GeometryAllocResult::Failed;
            }

            // 与生产配置一致：场景几何仓粒度 4，28 字节步长才能整除，相邻块无缝
            const uint64_t need = ((bytes + 3ull) / 4ull) * 4ull;
            if (need > 0xFFFFFFFFull)
            {
                return RA::GeometryAllocResult::Failed;
            }

            Block block;
            block.offset = static_cast<uint32_t>(target->bytes.size());
            block.size = static_cast<uint32_t>(need);
            target->bytes.resize(target->bytes.size() + static_cast<size_t>(need), 0u);

            const uint64_t id = target->nextBlockId++;
            target->blocks[id] = block;

            out.buffer.value = store.value;
            out.id = id;
            out.offset = block.offset;
            out.sizeBytes = static_cast<uint32_t>(bytes);
            return RA::GeometryAllocResult::Ok;
        }

        bool writeGeometry(RA::GeometryStoreHandle store,
            uint64_t blockId,
            uint32_t byteOffset,
            uint32_t sizeBytes,
            const void* data) override
        {
            Store* target = storeFor(store);
            if (target == nullptr || data == nullptr)
            {
                return false;
            }
            auto it = target->blocks.find(blockId);
            if (it == target->blocks.end())
            {
                return false;
            }
            const uint64_t start = static_cast<uint64_t>(it->second.offset) + byteOffset;
            if (start + sizeBytes > target->bytes.size())
            {
                return false;
            }
            std::memcpy(target->bytes.data() + start, data, sizeBytes);
            ++m_writeCount;
            return true;
        }

        void freeGeometry(RA::GeometryStoreHandle, uint64_t) override {}

        void flushGeometry(RA::GeometryStoreHandle) override {}

        RA::GeometryStoreStats geometryStoreStats(RA::GeometryStoreHandle store) override
        {
            RA::GeometryStoreStats stats{};
            if (const Store* target = storeFor(store))
            {
                stats.capacityBytes = target->bytes.size();
                stats.usedBytes = target->bytes.size();
                stats.blockCount = static_cast<uint32_t>(target->blocks.size());
            }
            return stats;
        }

        uint32_t defaultPipeline(RA::PipelineKind) override
        {
            return 0;
        }

        uint32_t createPipeline(const RA::PipelineDesc&) override
        {
            return 0;
        }

        uint32_t createMaterial(const RA::MaterialDesc&) override
        {
            return 0;
        }

        bool updateMaterial(uint32_t, const RA::MaterialDesc&) override
        {
            return false;
        }

        bool destroyMaterial(uint32_t) override
        {
            return false;
        }

        RA::FontHandle createFont(const RA::FontDesc&) override
        {
            return {};
        }

        void destroyFont(RA::FontHandle) override {}

        bool fontMetrics(RA::FontHandle, RA::FontMetrics&) override
        {
            return false;
        }

        bool fontGlyph(RA::FontHandle, uint32_t, RA::GlyphInfo&) override
        {
            return false;
        }

        bool flushFontAtlas(RA::FontHandle) override
        {
            return false;
        }

        RA::TextureHandle fontAtlas(RA::FontHandle) override
        {
            return {};
        }

        /// 按命令给的缓冲句柄与偏移回读顶点字节
        const uint8_t* bytesAt(RA::BufferHandle buffer, uint32_t offset) const
        {
            const size_t index = static_cast<size_t>(buffer.value);
            if (buffer.value == 0 || index > m_stores.size())
            {
                return nullptr;
            }
            const Store& target = m_stores[index - 1];
            if (offset >= target.bytes.size())
            {
                return nullptr;
            }
            return target.bytes.data() + offset;
        }

        uint32_t writeCount() const
        {
            return m_writeCount;
        }

    private:
        Store* storeFor(RA::GeometryStoreHandle store)
        {
            const size_t index = static_cast<size_t>(store.value);
            if (store.value == 0 || index > m_stores.size())
            {
                return nullptr;
            }
            return &m_stores[index - 1];
        }

        const Store* storeFor(RA::GeometryStoreHandle store) const
        {
            const size_t index = static_cast<size_t>(store.value);
            if (store.value == 0 || index > m_stores.size())
            {
                return nullptr;
            }
            return &m_stores[index - 1];
        }

        std::vector<Store> m_stores;
        uint32_t m_writeCount{ 0 };
    };

    /// 只登记绘制命令的最小假场景
    class RecordingSceneStub final : public RA::IRenderScene
    {
    public:
        bool allocTransient(uint64_t, RA::TransientAlloc&) override
        {
            return false;
        }

        bool submit(const RA::DrawPacket&) override
        {
            return false;
        }

        void setModelMatrix(const RA::Matrix4x4*) override {}

        RA::DrawListHandle createDrawList(const RA::DrawListDesc&) override
        {
            RA::DrawListHandle handle;
            handle.value = 1;
            return handle;
        }

        void destroyDrawList(RA::DrawListHandle) override {}

        void clearDrawList(RA::DrawListHandle) override
        {
            m_commands.clear();
        }

        bool upsertDrawItem(
            RA::DrawListHandle, uint32_t slot, const RA::DrawInstruction& command, const RA::Aabb3*) override
        {
            m_commands[slot] = command;
            return true;
        }

        bool removeDrawItem(RA::DrawListHandle, uint32_t slot) override
        {
            m_commands.erase(slot);
            return true;
        }

        bool submitDrawList(RA::DrawListHandle, const RA::ViewVolume*) override
        {
            return false;
        }

        bool beginFrame() override
        {
            return true;
        }

        void endFrame() override {}

        RA::TextureHandle createRenderTarget(uint32_t, uint32_t) override
        {
            return {};
        }

        bool readPixels(uint32_t, uint32_t, uint32_t, uint32_t, void*, uint64_t) override
        {
            return false;
        }

        bool setRenderTarget(const RA::RenderTargetBinding&) override
        {
            return false;
        }

        bool readPixelsFromTexture(RA::TextureHandle, uint32_t, uint32_t, uint32_t, uint32_t, void*, uint64_t) override
        {
            return false;
        }

        void setCamera(const RA::CameraDesc&) override {}

        void setLighting(const RA::LightingDesc&) override {}

        RA::FrameStatistics getFrameStatistics() const override
        {
            return {};
        }

        bool isValid() const override
        {
            return true;
        }

        /// 当前登记的全部绘制命令（槽号 → 命令）
        const std::unordered_map<uint32_t, RA::DrawInstruction>& commands() const
        {
            return m_commands;
        }

    private:
        std::unordered_map<uint32_t, RA::DrawInstruction> m_commands;
    };

    /// 一个色块 + 场景构建器的测试夹具
    struct FillFixture
    {
        RecordingDeviceStub device;
        RecordingSceneStub scene;
        Render::RenderSceneBuilder builder;

        FillFixture()
        {
            EXPECT_TRUE(builder.initialize(device, scene));
        }

        ~FillFixture()
        {
            builder.shutdown();
        }
    };

    void buildTriangleFill(Eg::SyFillRegion& fill, const Ut::Color& color)
    {
        fill.setVertexVector({
            Ut::Vec2d(20.0, 20.0),
            Ut::Vec2d(30.0, 20.0),
            Ut::Vec2d(20.0, 30.0),
        });
        fill.setOverrideColor(color);
    }

    void emitFill(Render::RenderSceneBuilder& builder, const Eg::SyFillRegion& fill)
    {
        builder.beginRebuild();
        Eg::emitEntityGeometry(fill, builder);
        builder.endRebuild();
    }

    /// 取唯一一段色块几何的绘制命令
    const RA::DrawInstruction* singleFillCommand(const RecordingSceneStub& scene)
    {
        if (scene.commands().size() != 1)
        {
            return nullptr;
        }
        return &scene.commands().begin()->second;
    }

    inline void benchHashFloat(uint64_t& h, float value)
    {
        uint32_t bits = 0;
        std::memcpy(&bits, &value, sizeof(bits));
        h ^= static_cast<uint64_t>(bits);
        h *= 0x100000001b3ull;
    }

    /// 顶点打包 + 内容哈希的等价实现，withAlpha=false 复刻旧 P3C3 路径，用于 A/B 对照
    double measurePackAndHash(const std::vector<Ut::Vec2d>& points, const Ut::Color& color, bool withAlpha)
    {
        std::vector<Ut::VertexP3C3> legacy;
        std::vector<Ut::VertexP3C4> alpha;
        const auto start = std::chrono::steady_clock::now();
        if (withAlpha)
        {
            alpha.reserve(points.size());
            for (const Ut::Vec2d& p : points)
            {
                alpha.push_back(Ut::VertexP3C4{ static_cast<float>(p.x()), static_cast<float>(p.y()), 0.0f,
                    color.r(), color.g(), color.b(), color.a() });
            }
        }
        else
        {
            legacy.reserve(points.size());
            for (const Ut::Vec2d& p : points)
            {
                legacy.push_back(Ut::VertexP3C3{ static_cast<float>(p.x()), static_cast<float>(p.y()), 0.0f,
                    color.r(), color.g(), color.b() });
            }
        }

        uint64_t h = 0xcbf29ce484222325ull;
        if (withAlpha)
        {
            for (const Ut::VertexP3C4& v : alpha)
            {
                benchHashFloat(h, v.px);
                benchHashFloat(h, v.py);
                benchHashFloat(h, v.pz);
                benchHashFloat(h, v.cr);
                benchHashFloat(h, v.cg);
                benchHashFloat(h, v.cb);
                benchHashFloat(h, v.ca);
            }
        }
        else
        {
            for (const Ut::VertexP3C3& v : legacy)
            {
                benchHashFloat(h, v.px);
                benchHashFloat(h, v.py);
                benchHashFloat(h, v.pz);
                benchHashFloat(h, v.cr);
                benchHashFloat(h, v.cg);
                benchHashFloat(h, v.cb);
            }
        }

        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        // 让结果参与可见输出，避免整段被优化掉
        if (h == 0xFFFFFFFFFFFFFFFFull)
        {
            std::cout << "[bench] hash sink" << std::endl;
        }
        return ms;
    }
}  // namespace

// 相邻的两段填充几何必须保持块间无缝：28 字节步长下若分配粒度不能整除步长，
// 渲染侧会判定段起点换算不出整数顶点而逐块单独 Draw，合批直接退化
TEST(FillAlphaVertexTest, AdjacentFillPiecesStayMergeable)
{
    FillFixture fixture;
    Eg::SyFillRegion fillA;
    buildTriangleFill(fillA, Ut::Color(1.0f, 0.0f, 0.0f, 0.5f));
    Eg::SyFillRegion fillB;
    buildTriangleFill(fillB, Ut::Color(0.0f, 1.0f, 0.0f, 0.5f));

    fixture.builder.beginRebuild();
    Eg::emitEntityGeometry(fillA, fixture.builder);
    Eg::emitEntityGeometry(fillB, fixture.builder);
    fixture.builder.endRebuild();

    ASSERT_EQ(fixture.scene.commands().size(), 2u);
    std::vector<uint32_t> slotIds;
    for (const auto& entry : fixture.scene.commands())
    {
        slotIds.push_back(entry.first);
    }
    std::sort(slotIds.begin(), slotIds.end());

    const auto& commands = fixture.scene.commands();
    const auto firstIt = commands.find(slotIds[0]);
    const auto secondIt = commands.find(slotIds[1]);
    ASSERT_NE(firstIt, commands.end());
    ASSERT_NE(secondIt, commands.end());
    const RA::DrawInstruction& first = firstIt->second;
    const RA::DrawInstruction& second = secondIt->second;
    ASSERT_EQ(first.vertexBuffer.value, second.vertexBuffer.value);
    ASSERT_GT(second.vertexOffset, first.vertexOffset);

    const uint32_t stride = static_cast<uint32_t>(sizeof(Ut::VertexP3C4));
    const uint32_t delta = second.vertexOffset - first.vertexOffset;
    EXPECT_EQ(delta % stride, 0u) << "block offset delta must be a multiple of stride";
    EXPECT_EQ(delta, first.vertexCount * stride);
}

// 色块顶点的 alpha 必须原样进入几何仓，并且绘制命令走带 alpha 的世界管线
TEST(FillAlphaVertexTest, FillVerticesCarryAlpha)
{
    static_assert(sizeof(Ut::VertexP3C4) == 28, "P3C4 stride must be 28 bytes");
    static_assert(sizeof(Ut::VertexP3C3) == 24, "P3C3 stride must be 24 bytes");

    FillFixture fixture;
    Eg::SyFillRegion fill;
    buildTriangleFill(fill, Ut::Color(1.0f, 0.0f, 0.0f, 0.5f));
    emitFill(fixture.builder, fill);

    const RA::DrawInstruction* command = singleFillCommand(fixture.scene);
    ASSERT_NE(command, nullptr);
    EXPECT_EQ(command->format, RA::VertexFormat::PositionColorAlpha);
    EXPECT_EQ(command->topology, RA::PrimitiveType::Triangles);
    EXPECT_EQ(command->space, RA::RenderSpace::World);
    ASSERT_EQ(command->vertexCount, 3u);

    const uint8_t* raw = fixture.device.bytesAt(command->vertexBuffer, command->vertexOffset);
    ASSERT_NE(raw, nullptr);
    const Ut::VertexP3C4* vertices = reinterpret_cast<const Ut::VertexP3C4*>(raw);

    const float expectedX[3] = { 20.0f, 30.0f, 20.0f };
    const float expectedY[3] = { 20.0f, 20.0f, 30.0f };
    for (uint32_t i = 0; i < 3; ++i)
    {
        EXPECT_FLOAT_EQ(vertices[i].px, expectedX[i]);
        EXPECT_FLOAT_EQ(vertices[i].py, expectedY[i]);
        EXPECT_FLOAT_EQ(vertices[i].cr, 1.0f);
        EXPECT_FLOAT_EQ(vertices[i].cg, 0.0f);
        EXPECT_FLOAT_EQ(vertices[i].cb, 0.0f);
        EXPECT_FLOAT_EQ(vertices[i].ca, 0.5f) << "alpha must survive vertex packing, index " << i;
    }
}

// alpha = 1.0 时，顶点前 24 字节必须与旧 P3C3 输出逐字节一致（视觉零差异的前提）
TEST(FillAlphaVertexTest, OpaqueFillMatchesLegacyP3C3Output)
{
    FillFixture fixture;
    Eg::SyFillRegion fill;
    buildTriangleFill(fill, Ut::Color(0.25f, 0.5f, 0.75f, 1.0f));
    emitFill(fixture.builder, fill);

    const RA::DrawInstruction* command = singleFillCommand(fixture.scene);
    ASSERT_NE(command, nullptr);
    ASSERT_EQ(command->vertexCount, 3u);

    const uint8_t* raw = fixture.device.bytesAt(command->vertexBuffer, command->vertexOffset);
    ASSERT_NE(raw, nullptr);
    const Ut::VertexP3C4* vertices = reinterpret_cast<const Ut::VertexP3C4*>(raw);

    // 旧路径输出的等价字节：位置 + RGB，alpha 不在结构里
    const Ut::Vec2d points[3] = { Ut::Vec2d(20.0, 20.0), Ut::Vec2d(30.0, 20.0), Ut::Vec2d(20.0, 30.0) };
    const Ut::Color legacyColor(0.25f, 0.5f, 0.75f, 1.0f);
    for (uint32_t i = 0; i < 3; ++i)
    {
        Ut::VertexP3C3 legacy{};
        legacy.px = static_cast<float>(points[i].x());
        legacy.py = static_cast<float>(points[i].y());
        legacy.pz = 0.0f;
        legacy.cr = legacyColor.r();
        legacy.cg = legacyColor.g();
        legacy.cb = legacyColor.b();

        EXPECT_EQ(std::memcmp(&legacy, &vertices[i], sizeof(Ut::VertexP3C3)), 0)
            << "opaque vertex bytes differ from legacy P3C3 layout, index " << i;
        // 混合状态为 SrcAlpha / OneMinusSrcAlpha，alpha = 1 时结果等于源色
        EXPECT_FLOAT_EQ(vertices[i].ca, 1.0f);
    }
}

// 只改 alpha 必须让内容哈希失效并重新写入几何，否则画面上的透明度不会刷新
TEST(FillAlphaVertexTest, AlphaChangeRewritesGeometry)
{
    FillFixture fixture;
    Eg::SyFillRegion fill;
    buildTriangleFill(fill, Ut::Color(1.0f, 0.0f, 0.0f, 0.5f));
    emitFill(fixture.builder, fill);

    const uint32_t writesAfterFirst = fixture.device.writeCount();
    EXPECT_EQ(writesAfterFirst, 1u);

    // 几何完全不变，仅透明度改变
    buildTriangleFill(fill, Ut::Color(1.0f, 0.0f, 0.0f, 0.8f));
    emitFill(fixture.builder, fill);
    EXPECT_EQ(fixture.device.writeCount(), writesAfterFirst + 1u)
        << "alpha must participate in the content hash, otherwise the stale vertices are reused";

    const RA::DrawInstruction* command = singleFillCommand(fixture.scene);
    ASSERT_NE(command, nullptr);
    const uint8_t* raw = fixture.device.bytesAt(command->vertexBuffer, command->vertexOffset);
    ASSERT_NE(raw, nullptr);
    const Ut::VertexP3C4* vertices = reinterpret_cast<const Ut::VertexP3C4*>(raw);
    for (uint32_t i = 0; i < 3; ++i)
    {
        EXPECT_FLOAT_EQ(vertices[i].ca, 0.8f);
    }
}

// CPU 侧装配微基准：量化顶点从 24 字节涨到 28 字节后的字节量与装配耗时
// （GPU 帧耗时无法在无 GPU 环境下测量，此用例只统计 CPU 装配与内存占用）
TEST(FillAlphaVertexTest, AssemblyMicroBenchmark)
{
    const size_t vertexCount = 150000u;  // 50k 个三角形
    std::vector<Ut::Vec2d> points;
    points.reserve(vertexCount);
    for (size_t i = 0; i < vertexCount; ++i)
    {
        const double angle = static_cast<double>(i);
        points.emplace_back(Ut::Vec2d(std::cos(angle) * 100.0, std::sin(angle) * 100.0));
    }

    FillFixture fixture;
    Eg::SyFillRegion fill;
    buildTriangleFill(fill, Ut::Color(0.2f, 0.4f, 0.9f, 0.5f));

    const double iterations = 3.0;
    double totalMs = 0.0;
    size_t emitted = 0;
    for (int run = 0; run < 3; ++run)
    {
        fill.setVertexVector(points);
        const auto start = std::chrono::steady_clock::now();
        fixture.builder.beginRebuild();
        Eg::emitEntityGeometry(fill, fixture.builder);
        fixture.builder.endRebuild();
        totalMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        emitted = static_cast<size_t>(points.size());
    }

    const double avgMs = totalMs / iterations;
    const uint64_t newBytes = static_cast<uint64_t>(emitted) * sizeof(Ut::VertexP3C4);
    const uint64_t legacyBytes = static_cast<uint64_t>(emitted) * sizeof(Ut::VertexP3C3);

    // 只打印，不对耗时做断言：CI 机器波动会让时间断言变得不稳定
    std::cout << "[bench] fill triangles vertices=" << emitted << " cpuAssemblyAvgMs=" << avgMs
              << " vertexBytes=" << newBytes << " legacyVertexBytes=" << legacyBytes
              << " deltaPercent=" << (100.0 * static_cast<double>(newBytes - legacyBytes) / legacyBytes) << std::endl;

    // A/B 对照：同样的点集，分别用旧 P3C3 与带 alpha 的 P3C4 打包 + 哈希，取 3 轮平均
    const Ut::Color benchColor(0.2f, 0.4f, 0.9f, 0.5f);
    double legacyMs = 0.0;
    double alphaMs = 0.0;
    for (int run = 0; run < 3; ++run)
    {
        legacyMs += measurePackAndHash(points, benchColor, false);
        alphaMs += measurePackAndHash(points, benchColor, true);
    }
    std::cout << "[bench] packAndHash vertices=" << points.size() << " legacyAvgMs=" << (legacyMs / 3.0)
              << " alphaAvgMs=" << (alphaMs / 3.0)
              << " alphaDeltaPercent=" << (100.0 * (alphaMs - legacyMs) / legacyMs) << std::endl;

    EXPECT_EQ(emitted, vertexCount);
    EXPECT_GT(fixture.device.writeCount(), 0u);
}

// 端到端填充色验收：真实走「螺旋填充计算 → 写场景 → RenderSceneBuilder 顶点装配」，
// 再用顶点流做 CPU 光栅化并采样像素 RGB。
// 取色口径：螺旋/同心环不使用对话框的显式颜色，填充色 = 源图形所属图层的颜色，
// 因此这里只改图层色，断言像素跟着变；同时断言结果图元落回源图层。
// 说明：RenderX 的 Null 后端离屏渲染不落像素，GPU 离屏回读在本机测试环境不可用，
// 因此这里对真实顶点流做软件光栅化；顶点色就是 GPU 管线消费的颜色通道。
TEST(FillColorPixelTest, SpiralFillFollowsSourceLayerColor)
{
    // 取一个已知位于外圈环底边上的世界坐标点（矩形 (0,0)-(40,24)，首层内缩 2mm）
    const int nImageW = 96;
    const int nImageH = 96;
    const double dSampleX = 20.0;
    const double dSampleY = 2.0;
    const int nPixelX = static_cast<int>(dSampleX);
    const int nPixelY = nImageH - static_cast<int>(dSampleY);

    // 用真实的填充链路生成结果图元，并返回“渲染后”的像素 RGB
    auto sampleFilledPixel = [&](const Ut::Color& layerColor) -> std::array<uint8_t, 3> {
        Eg::SceneManager scene;
        LayerManager layerManager(&scene);
        const int nLayerId = layerManager.createLayer("FillLayer");
        if (nLayerId <= 0 || !layerManager.setLayerColor(nLayerId, layerColor))
        {
            ADD_FAILURE() << "test layer must be created with the requested color";
            return std::array<uint8_t, 3>{ 0, 0, 0 };
        }

        auto src = std::make_unique<Eg::SyPolygon>();
        src->setVertices({ Ut::Vec2d(0, 0), Ut::Vec2d(40, 0), Ut::Vec2d(40, 24), Ut::Vec2d(0, 24) });
        src->bClosed = true;
        Eg::SyEntity* srcPtr = src.get();
        std::vector<std::unique_ptr<Eg::SyEntity>> srcBatch;
        srcBatch.push_back(std::move(src));
        scene.addEntities(std::move(srcBatch));
        // 源图形必须真正属于该图层，填充才会跟随它的颜色并落回同层
        layerManager.assignEntityToLayer(srcPtr, nLayerId);

        Eg::ContourFillParameters params;
        params.dOffsetDistance = 2.0;
        params.bAutoMaxLayers = false;
        params.nMaxLayers = 3;
        params.dMinArea = 0.0;

        std::vector<std::vector<Ut::Vec2d>> paths;
        double totalLength = 0.0;
        int nJumps = 0;
        Eg::EntityContourFiller filler(&scene);
        // 注意：lambda 有返回值，内部不能用 ASSERT_*（会展开成无值 return）
        if (!Eg::EntityContourFiller::computeSpiralPaths({ srcPtr }, params, paths, totalLength, nJumps))
        {
            ADD_FAILURE() << "spiral fill must produce paths for the test rectangle";
            return std::array<uint8_t, 3>{ 0, 0, 0 };
        }
        if (filler.writeSpiralPathsToScene(paths, srcPtr) <= 0)
        {
            ADD_FAILURE() << "spiral fill must write result entities";
            return std::array<uint8_t, 3>{ 0, 0, 0 };
        }

        // 取色口径：不设覆盖色，结果色回落到源图形所属图层色，且结果落回同一图层
        int nFillLayerId = -1;
        for (Eg::SyEntity* e : scene.getAllEntities())
        {
            if (e == srcPtr)
            {
                continue;
            }
            EXPECT_EQ(e->getColor().red255(), layerColor.red255());
            EXPECT_EQ(e->getColor().green255(), layerColor.green255());
            EXPECT_EQ(e->getColor().blue255(), layerColor.blue255());
            EXPECT_EQ(e->layerId(), nLayerId) << "fill must land on the source layer";
            nFillLayerId = e->layerId();
        }
        std::cout << "[layer] sourceLayer=" << nLayerId << " spiralFillLayer=" << nFillLayerId << std::endl;

        // 把结果图元送进真实渲染装配链路，取回实际顶点字节
        FillFixture fixture;
        fixture.builder.beginRebuild();
        for (Eg::SyEntity* e : scene.getAllEntities())
        {
            if (e != srcPtr)
            {
                Eg::emitEntityGeometry(*e, fixture.builder);
            }
        }
        fixture.builder.endRebuild();
        if (fixture.scene.commands().empty())
        {
            ADD_FAILURE() << "render scene builder must emit draw commands";
            return std::array<uint8_t, 3>{ 0, 0, 0 };
        }

        // 软件光栅化：按世界坐标直线画出顶点流里的每一段，颜色取顶点色
        std::vector<std::array<uint8_t, 3>> image(
            static_cast<size_t>(nImageW) * nImageH, std::array<uint8_t, 3>{ 255, 255, 255 });
        auto plot = [&](int x, int y, const std::array<uint8_t, 3>& c) {
            if (x < 0 || y < 0 || x >= nImageW || y >= nImageH)
            {
                return;
            }
            image[static_cast<size_t>(y) * nImageW + x] = c;
        };
        for (const auto& entry : fixture.scene.commands())
        {
            const RA::DrawInstruction& cmd = entry.second;
            const bool bLoop = cmd.topology == RA::PrimitiveType::LineLoop;
            if (cmd.topology != RA::PrimitiveType::Lines && cmd.topology != RA::PrimitiveType::LineStrip && !bLoop)
            {
                continue;
            }
            const uint8_t* raw = fixture.device.bytesAt(cmd.vertexBuffer, cmd.vertexOffset);
            if (raw == nullptr)
            {
                ADD_FAILURE() << "vertex buffer readback returned null";
                continue;
            }
            if (cmd.format != RA::VertexFormat::PositionColorAlpha)
            {
                continue;
            }
            const Ut::VertexP3C4* verts = reinterpret_cast<const Ut::VertexP3C4*>(raw);
            for (uint32_t i = 1; i < cmd.vertexCount; ++i)
            {
                const std::array<uint8_t, 3> c{ static_cast<uint8_t>(verts[i - 1].cr * 255.0f),
                    static_cast<uint8_t>(verts[i - 1].cg * 255.0f),
                    static_cast<uint8_t>(verts[i - 1].cb * 255.0f) };
                const double x0 = verts[i - 1].px;
                const double y0 = verts[i - 1].py;
                const double x1 = verts[i].px;
                const double y1 = verts[i].py;
                const int nSteps = static_cast<int>((std::max)(std::abs(x1 - x0), std::abs(y1 - y0))) + 1;
                for (int s = 0; s <= nSteps; ++s)
                {
                    const double t = static_cast<double>(s) / nSteps;
                    plot(static_cast<int>(std::lround(x0 + (x1 - x0) * t)),
                        nImageH - static_cast<int>(std::lround(y0 + (y1 - y0) * t)),
                        c);
                }
            }
            // LineLoop 由硬件自动补最后一段闭合边，软件光栅化要显式补上
            if (bLoop && cmd.vertexCount > 2)
            {
                const Ut::VertexP3C4& first = verts[0];
                const Ut::VertexP3C4& last = verts[cmd.vertexCount - 1];
                const int nSteps = static_cast<int>((std::max)(std::abs(first.px - last.px), std::abs(first.py - last.py))) + 1;
                const std::array<uint8_t, 3> c{ static_cast<uint8_t>(last.cr * 255.0f),
                    static_cast<uint8_t>(last.cg * 255.0f),
                    static_cast<uint8_t>(last.cb * 255.0f) };
                for (int s = 0; s <= nSteps; ++s)
                {
                    const double t = static_cast<double>(s) / nSteps;
                    plot(static_cast<int>(std::lround(last.px + (first.px - last.px) * t)),
                        nImageH - static_cast<int>(std::lround(last.py + (first.py - last.py) * t)),
                        c);
                }
            }
        }

        return image[static_cast<size_t>(nPixelY) * nImageW + nPixelX];
    };

    const std::array<uint8_t, 3> redPixel = sampleFilledPixel(Ut::Color(1.0f, 0.0f, 0.0f, 1.0f));
    const std::array<uint8_t, 3> bluePixel = sampleFilledPixel(Ut::Color(0.0f, 0.0f, 1.0f, 1.0f));

    std::cout << "[pixel] layerColor=#FF0000 sampled=" << static_cast<int>(redPixel[0]) << ","
              << static_cast<int>(redPixel[1]) << "," << static_cast<int>(redPixel[2])
              << " layerColor=#0000FF sampled=" << static_cast<int>(bluePixel[0]) << ","
              << static_cast<int>(bluePixel[1]) << "," << static_cast<int>(bluePixel[2]) << std::endl;

    EXPECT_EQ(redPixel[0], 255);
    EXPECT_EQ(redPixel[1], 0);
    EXPECT_EQ(redPixel[2], 0);
    EXPECT_EQ(bluePixel[0], 0);
    EXPECT_EQ(bluePixel[1], 0);
    EXPECT_EQ(bluePixel[2], 255);
    EXPECT_NE(redPixel, bluePixel) << "two different fill colors must render to different pixels";
}

// 颜色填充（ColorFill 页）语义不变：仍使用对话框里的显式颜色。
// 这里走真实的色块填充链路（三角剖分 → 写场景 → 顶点装配），
// 用重心坐标判定采样点是否落在某个三角形内，取该三角形顶点色作为“渲染结果”。
TEST(FillColorPixelTest, ColorFillExplicitColorReachesRenderedPixel)
{
    const double dSampleX = 20.0;
    const double dSampleY = 12.0;

    // 点在三角形内判定（三次叉积同号，含边界）
    auto pointInTriangle = [](double px, double py, const Ut::VertexP3C4& a, const Ut::VertexP3C4& b,
                               const Ut::VertexP3C4& c) {
        const double d1 = (px - b.px) * (a.py - b.py) - (a.px - b.px) * (py - b.py);
        const double d2 = (px - c.px) * (b.py - c.py) - (b.px - c.px) * (py - c.py);
        const double d3 = (px - a.px) * (c.py - a.py) - (c.px - a.px) * (py - a.py);
        const bool bNeg = (d1 < 0) || (d2 < 0) || (d3 < 0);
        const bool bPos = (d1 > 0) || (d2 > 0) || (d3 > 0);
        return !(bNeg && bPos);
    };

    auto sampleFilledPixel = [&](const Ut::Color& fillColor) -> std::array<uint8_t, 3> {
        Eg::SceneManager scene;
        auto src = std::make_unique<Eg::SyPolygon>();
        src->setVertices({ Ut::Vec2d(0, 0), Ut::Vec2d(40, 0), Ut::Vec2d(40, 24), Ut::Vec2d(0, 24) });
        src->bClosed = true;
        Eg::SyEntity* srcPtr = src.get();
        std::vector<std::unique_ptr<Eg::SyEntity>> srcBatch;
        srcBatch.push_back(std::move(src));
        scene.addEntities(std::move(srcBatch));

        // 色块填充写入需要图层管理器：这里给它一个独立图层，颜色仍由显式参数决定
        LayerManager layerManager(&scene);
        const int nLayerId = layerManager.createLayer("ColorFillLayer");
        if (nLayerId <= 0)
        {
            ADD_FAILURE() << "color fill test layer must be created";
            return std::array<uint8_t, 3>{ 0, 0, 0 };
        }

        Eg::ColorFillParameters params;
        Eg::FillRegionList regions;
        // 注意：lambda 有返回值，内部不能用 ASSERT_*（会展开成无值 return）
        if (!Eg::EntityColorFiller::computeRegions({ srcPtr }, params, regions))
        {
            ADD_FAILURE() << "color fill must triangulate the test rectangle";
            return std::array<uint8_t, 3>{ 0, 0, 0 };
        }
        Eg::EntityColorFiller filler(&scene, &layerManager);
        if (filler.writeRegionsToScene(regions, nLayerId, fillColor) <= 0)
        {
            ADD_FAILURE() << "color fill must write fill region entities";
            return std::array<uint8_t, 3>{ 0, 0, 0 };
        }

        // 颜色填充的语义：显式颜色写到结果图元的覆盖色上
        for (Eg::SyEntity* e : scene.getAllEntities())
        {
            if (e == srcPtr)
            {
                continue;
            }
            EXPECT_EQ(e->getColor().red255(), fillColor.red255());
            EXPECT_EQ(e->getColor().green255(), fillColor.green255());
            EXPECT_EQ(e->getColor().blue255(), fillColor.blue255());
        }

        FillFixture fixture;
        fixture.builder.beginRebuild();
        for (Eg::SyEntity* e : scene.getAllEntities())
        {
            if (e != srcPtr)
            {
                Eg::emitEntityGeometry(*e, fixture.builder);
            }
        }
        fixture.builder.endRebuild();

        // 色块填充以三角形集渲染：找到覆盖采样点的三角形，取其顶点色
        for (const auto& entry : fixture.scene.commands())
        {
            const RA::DrawInstruction& cmd = entry.second;
            if (cmd.topology != RA::PrimitiveType::Triangles ||
                cmd.format != RA::VertexFormat::PositionColorAlpha)
            {
                continue;
            }
            const uint8_t* raw = fixture.device.bytesAt(cmd.vertexBuffer, cmd.vertexOffset);
            if (raw == nullptr)
            {
                continue;
            }
            const Ut::VertexP3C4* verts = reinterpret_cast<const Ut::VertexP3C4*>(raw);
            for (uint32_t i = 0; i + 2 < cmd.vertexCount; i += 3)
            {
                if (pointInTriangle(dSampleX, dSampleY, verts[i], verts[i + 1], verts[i + 2]))
                {
                    return std::array<uint8_t, 3>{ static_cast<uint8_t>(verts[i].cr * 255.0f),
                        static_cast<uint8_t>(verts[i].cg * 255.0f),
                        static_cast<uint8_t>(verts[i].cb * 255.0f) };
                }
            }
        }
        ADD_FAILURE() << "sample point is not covered by any fill triangle";
        return std::array<uint8_t, 3>{ 255, 255, 255 };
    };

    const std::array<uint8_t, 3> redPixel = sampleFilledPixel(Ut::Color(1.0f, 0.0f, 0.0f, 1.0f));
    const std::array<uint8_t, 3> bluePixel = sampleFilledPixel(Ut::Color(0.0f, 0.0f, 1.0f, 1.0f));

    std::cout << "[pixel] colorFill=#FF0000 sampled=" << static_cast<int>(redPixel[0]) << ","
              << static_cast<int>(redPixel[1]) << "," << static_cast<int>(redPixel[2])
              << " colorFill=#0000FF sampled=" << static_cast<int>(bluePixel[0]) << ","
              << static_cast<int>(bluePixel[1]) << "," << static_cast<int>(bluePixel[2]) << std::endl;

    EXPECT_EQ(redPixel[0], 255);
    EXPECT_EQ(redPixel[1], 0);
    EXPECT_EQ(redPixel[2], 0);
    EXPECT_EQ(bluePixel[0], 0);
    EXPECT_EQ(bluePixel[1], 0);
    EXPECT_EQ(bluePixel[2], 255);
    EXPECT_NE(redPixel, bluePixel) << "two different fill colors must render to different pixels";
}
