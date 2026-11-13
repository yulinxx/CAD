/**
 * @file DrawListBatchMeasurementTests.cpp
 * @brief 合批收益的实测数字（粒度 / 合批开关 / 材质编号三组对照）
 *
 * 为什么单独一个文件：3D 合批的收益此前只有「前置条件成立」的证明
 * （材质同编号 + 块起点连续），没有「真的合批、真的少画」的数字。
 * 这里用 RenderX 的 Null 后端 + 真实几何仓分配器，把同一场景的四种配置
 * 各跑一帧，从 rxSessionGetStats 读出本帧 drawCallCount / mergedDrawCount，
 * 直接对照：
 *   A 粒度 256 + 开合批（改前）：块起点被 256 补白，8 条目仍是 8 次 draw；
 *   B 粒度 8  + 关合批：块已连续，但合批开关关掉，仍是 8 次 draw；
 *   C 粒度 8  + 开合批（改后）：8 条目合成 1 次 draw；
 *   D 粒度 8  + 开合批 + 8 个不同材质编号：材质不同不能共批，回到 8 次。
 * C 与 D 的差值就是「材质按 MaterialDesc 去重」的贡献。
 *
 * 场景参数与 Mesh3DBuilder 侧一致：P3C3 步长 24 字节、每图元 3 顶点 = 72 字节，
 * 8 个图元。Mesh3DBuilder 的用例（UI3DRenderPipelineTests 的
 * IdenticalMaterialDescSharesOneMaterialIndex）断言它实际产出的就是
 * 「同一材质编号 + 块起点 0/72/144... 连续」，即本文件 C 组的输入。
 *
 * 只读：这里没有改任何写入语义，也不增加生产接口，读的就是既有统计出口。
 */

#include <gtest/gtest.h>

#include "render/renderx.h"

#include <cstdint>

namespace
{
    using namespace Render::RT;

    RuntimeDesc makeRuntimeDesc()
    {
        RuntimeDesc desc{};
        desc.abiVersion = RENDERX_ABI_VERSION;
        desc.backend = Backend::Null;
        desc.enableValidation = 1;
        desc.transientBufferBytes = 1024 * 1024;
        desc.applicationName = "DrawListBatchMeasurementTests";
        return desc;
    }

    SurfaceDesc makeSurfaceDesc()
    {
        SurfaceDesc desc{};
        desc.windowKind = NativeWindowKind::None;
        desc.presentMode = PresentMode::Fifo;
        desc.width = 64;
        desc.height = 64;
        desc.enableDepth = 0;
        return desc;
    }

    /// 与 Mesh3DBuilder 一致：每图元 3 顶点、P3C3 步长 24 → 块 72 字节
    constexpr uint32_t kMeasureEntities = 8;
    constexpr uint32_t kEntityVertexCount = 3;
    constexpr uint32_t kP3C3StrideBytes = 24;
    constexpr uint32_t kEntityBlockBytes = kEntityVertexCount * kP3C3StrideBytes;

    /// 一次测量的结果，供断言与汇报使用
    struct Measured
    {
        uint32_t drawCallCount = 0;
        uint32_t mergedDrawCount = 0;
        uint32_t triangleCount = 0;
        uint32_t entryCount = 0;
        uint32_t blockOffsets[kMeasureEntities] = {};
        bool blocksContiguous = false;
    };

    /**
     * @brief 跑一帧并读出统计
     * @param granularity 几何仓分配粒度（256 = 割接前的默认值，8 = 现在的值）
     * @param enableMerging 绘制列表合批开关
     * @param distinctMaterials true = 每个图元各用独立材质编号（对照 D）
     */
    Measured runScene(uint32_t granularity, bool enableMerging, bool distinctMaterials)
    {
        Measured result;

        const RuntimeDesc runtimeDesc = makeRuntimeDesc();
        const RuntimeHandle runtime = rxRuntimeCreate(&runtimeDesc);
        EXPECT_TRUE(rxValid(runtime));
        if (!rxValid(runtime))
        {
            return result;
        }

        const SurfaceDesc surfaceDesc = makeSurfaceDesc();
        const SurfaceHandle surface = rxSurfaceCreate(runtime, &surfaceDesc);
        EXPECT_TRUE(rxValid(surface));

        SessionDesc sessionDesc{};
        sessionDesc.runtime = runtime;
        sessionDesc.surface = surface;
        sessionDesc.clearColor[3] = 1.0f;
        const SessionHandle session = rxSessionCreate(&sessionDesc);
        EXPECT_TRUE(rxValid(session));

        GeometryStoreDesc storeDesc{};
        storeDesc.initialBytes = 1u << 16;
        storeDesc.maxBytes = 1u << 20;
        storeDesc.granularity = granularity;
        const GeometryStoreHandle store = rxGeometryStoreCreate(runtime, &storeDesc);
        EXPECT_TRUE(rxValid(store));

        const BufferHandle buffer = rxGeometryStoreGetBuffer(runtime, store);
        EXPECT_TRUE(rxValid(buffer));

        DrawListDesc listDesc{};
        listDesc.initialCapacity = kMeasureEntities;
        listDesc.enableMerging = enableMerging ? 1u : 0u;
        const DrawListHandle list = rxDrawListCreate(runtime, &listDesc);
        EXPECT_TRUE(rxValid(list));

        for (uint32_t i = 0; i < kMeasureEntities; ++i)
        {
            GeometryBlock block{};
            EXPECT_EQ(rxGeometryAlloc(runtime, store, kEntityBlockBytes, &block), RxResult::Ok);
            result.blockOffsets[i] = block.offset;

            DrawCommand command{};
            command.vertexBuffer = buffer;
            command.sortKey = i + 1;
            command.vertexOffset = block.offset;
            command.vertexCount = kEntityVertexCount;
            command.topology = PrimitiveTopology::Triangles;
            command.space = RenderSpace::World;
            command.vertexFormat = VertexFormat::P3C3;
            command.indexType = IndexType::None;
            // 材质编号：distinctMaterials 时逐个不同，否则全部同一编号
            command.materialIndex = static_cast<uint16_t>(distinctMaterials ? (i + 1) : 1);
            EXPECT_EQ(rxDrawListUpsert(runtime, list, i, &command, nullptr), RxResult::Ok);
        }

        result.blocksContiguous = true;
        for (uint32_t i = 1; i < kMeasureEntities; ++i)
        {
            if (result.blockOffsets[i] != result.blockOffsets[i - 1] + kEntityBlockBytes)
            {
                result.blocksContiguous = false;
            }
        }

        EXPECT_EQ(rxSessionBeginFrame(session), RxResult::Ok);
        EXPECT_EQ(rxSessionSubmitDrawList(session, list, nullptr), RxResult::Ok);

        FrameStats frame{};
        EXPECT_EQ(rxSessionGetStats(session, &frame), RxResult::Ok);
        result.drawCallCount = frame.drawCallCount;
        result.mergedDrawCount = frame.mergedDrawCount;
        result.triangleCount = frame.triangleCount;

        DrawListStats listStats{};
        EXPECT_EQ(rxDrawListGetStats(runtime, list, &listStats), RxResult::Ok);
        result.entryCount = listStats.entryCount;

        EXPECT_EQ(rxSessionEndFrame(session), RxResult::Ok);

        rxDrawListDestroy(runtime, list);
        rxGeometryStoreDestroy(runtime, store);
        rxSessionDestroy(session);
        rxSurfaceDestroy(runtime, surface);
        rxRuntimeDestroy(runtime);
        return result;
    }
}  // namespace

/// A：改前的分配粒度（256）。256 % 24 = 16，块之间被补白，合批一次都不发生
TEST(DrawListBatchMeasurementTest, Granularity256WithMergingStillDrawsPerEntity)
{
    const Measured m = runScene(256, true, false);
    EXPECT_EQ(m.entryCount, kMeasureEntities);
    EXPECT_EQ(m.drawCallCount, kMeasureEntities) << "cannot merge when block starts are not multiples of the stride";
    EXPECT_EQ(m.mergedDrawCount, 0u);
}

/// B：粒度已降到 8，块连续了，但关掉合批 → 仍按条目逐个画
TEST(DrawListBatchMeasurementTest, Granularity8WithoutMergingStillDrawsPerEntity)
{
    const Measured m = runScene(8, false, false);
    EXPECT_TRUE(m.blocksContiguous) << "granularity 8 should pack 72-byte blocks tightly";
    EXPECT_EQ(m.drawCallCount, kMeasureEntities) << "contiguity alone without merging must not reduce the draw count";
    EXPECT_EQ(m.mergedDrawCount, 0u);
}

/// C：改后的组合（粒度 8 + 合批开 + 同一材质编号）→ 8 条目合成 1 次 draw
TEST(DrawListBatchMeasurementTest, Granularity8WithMergingFusesIntoSingleDraw)
{
    const Measured m = runScene(8, true, false);
    EXPECT_TRUE(m.blocksContiguous);
    EXPECT_EQ(m.drawCallCount, 1u) << "contiguous blocks with the same material and pipeline should fuse into a single draw";
    EXPECT_EQ(m.mergedDrawCount, kMeasureEntities - 1);
    EXPECT_EQ(m.triangleCount, kMeasureEntities) << "merging must not drop entities";
}

/// D：同样连续 + 开合批，但材质编号各不同 → 不能共批，回到逐条画
TEST(DrawListBatchMeasurementTest, DistinctMaterialIndicesBlockMerging)
{
    const Measured m = runScene(8, true, true);
    EXPECT_TRUE(m.blocksContiguous);
    EXPECT_EQ(m.drawCallCount, kMeasureEntities) << "different material indices must not be batched together";
    EXPECT_EQ(m.mergedDrawCount, 0u);
}

