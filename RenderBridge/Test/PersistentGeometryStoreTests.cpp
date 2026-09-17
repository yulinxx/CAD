/**
 * @file PersistentGeometryStoreTests.cpp
 * @brief 宿主侧几何仓的契约测试（分片 / 块路由 / 统计聚合 / 销毁）
 *
 * 为什么要有这个文件：`PersistentGeometryStore` 是 2D 与 3D 两个 builder 共用的
 * 块管理底座，但它此前**没有任何测试**（`RenderSceneBuilder` / `Mesh3DBuilder`
 * 也没有）。而它现在承担着一件容易错的事——用「多个 4GB 仓」绕过 DLL 单仓
 * 4GB 上限的分片（见头文件里的「为什么内部是多仓」）。分片一旦路由错了，
 * 表现是「某些图元不画」或画到别人的数据上，这类错误在密集图形里几乎看不出。
 *
 * 为什么用 Null 后端：几何仓的分配/写入/释放只需要 Runtime，不需要 GPU。
 * Null 后端是完整实现（与 Renderx 自己的 RxRuntimeTests 同一套用法），
 * 因此这些用例在没有显卡的机器上也能跑。
 *
 * 注意：本文件刻意**不**依赖 RenderBridge 的 DLL 符号——被测类与头文件里的
 * 工具函数都是 header-only，链接 RenderX + Log 即可。这样测试不会因为
 * RenderBridge 的导出面变化而失效。
 */

#include <gtest/gtest.h>

#include "RenderBridge/PersistentGeometryStore.h"

#include <cstdint>
#include <vector>

namespace
{
    /// 单仓上限取得很小（4KB），让分片在几个分配内就能确定性地发生
    constexpr uint64_t kTinyStoreMax = 4096;
    constexpr uint64_t kTinyStoreInitial = 1024;
    constexpr uint32_t kGranularity = 256;

    /// 一个块请求 256 字节：4KB 的仓最多装 16 块
    constexpr uint64_t kBlockBytes = 256;

    class PersistentGeometryStoreTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            Render::RT::RuntimeDesc desc{};
            desc.abiVersion = RENDERX_ABI_VERSION;
            desc.backend = Render::RT::Backend::Null;
            desc.enableValidation = 1;
            desc.transientBufferBytes = 1024 * 1024;
            desc.applicationName = "PersistentGeometryStoreTests";
            m_runtime = Render::RT::rxRuntimeCreate(&desc);
            ASSERT_TRUE(Render::RT::rxValid(m_runtime));
        }

        void TearDown() override
        {
            m_store.shutdown();
            if (Render::RT::rxValid(m_runtime))
            {
                Render::RT::rxRuntimeDestroy(m_runtime);
            }
        }

        RenderBridge::PersistentGeometryStore::Config tinyConfig(uint32_t maxStores = 8) const
        {
            RenderBridge::PersistentGeometryStore::Config config;
            config.storeInitialBytes = kTinyStoreInitial;
            config.storeMaxBytes = kTinyStoreMax;
            config.storeGranularity = kGranularity;
            config.maxStores = maxStores;
            config.drawListInitialCapacity = 256;
            return config;
        }

        /// 写一块并读回校验（Null 后端没有回读接口，这里只验证写入被接受）
        bool writePattern(uint64_t blockId, uint8_t seed)
        {
            std::vector<uint8_t> payload(static_cast<size_t>(kBlockBytes), seed);
            return m_store.writeBlock(blockId, 0, static_cast<uint32_t>(kBlockBytes), payload.data());
        }

        Render::RT::RuntimeHandle m_runtime{ Render::RT::RuntimeHandle::Invalid };
        RenderBridge::PersistentGeometryStore m_store;
    };

    /// 未达单仓上限时只有主仓：分片表保持为空，路径与改造前一致
    TEST_F(PersistentGeometryStoreTest, StaysOnSingleStoreWhileUnderLimit)
    {
        ASSERT_TRUE(m_store.initialize(m_runtime, tinyConfig()));
        EXPECT_EQ(m_store.storeCount(), 1u);

        Render::RT::GeometryBlock block{};
        ASSERT_TRUE(m_store.allocBlock(kBlockBytes, block));
        EXPECT_TRUE(Render::RT::rxValid(block.buffer));
        EXPECT_EQ(m_store.storeCount(), 1u) << "一块都还没超上限，不该分片";
        EXPECT_TRUE(writePattern(block.id, 0x11));
    }

    /// 主仓达到单仓上限后自动开新仓，且**两块都仍可读写**（块路由正确）
    TEST_F(PersistentGeometryStoreTest, ShardsIntoSecondStoreWhenPrimaryHitsLimit)
    {
        ASSERT_TRUE(m_store.initialize(m_runtime, tinyConfig()));

        std::vector<Render::RT::GeometryBlock> blocks;
        // 一直分配到发生分片为止；上限 1024 次是防呆，正常在 20 次内
        for (int i = 0; i < 1024 && m_store.storeCount() == 1; ++i)
        {
            Render::RT::GeometryBlock block{};
            ASSERT_TRUE(m_store.allocBlock(kBlockBytes, block)) << "第 " << i << " 块分配失败";
            blocks.push_back(block);
        }

        ASSERT_EQ(m_store.storeCount(), 2u) << "主仓 4KB 上限用尽后必须开第二个仓，否则总容量卡在单仓上限";

        Render::RT::GeometryBlock sharded{};
        ASSERT_TRUE(m_store.allocBlock(kBlockBytes, sharded));
        blocks.push_back(sharded);

        // 关键：跨仓的老块与新块都要能被写入 —— 路由错的表现是某些块写不进去
        for (size_t i = 0; i < blocks.size(); ++i)
        {
            EXPECT_TRUE(writePattern(blocks[i].id, static_cast<uint8_t>(i + 1)))
                << "第 " << i << " 块写入失败（跨仓路由错误？）";
        }

        // 落进第二仓的块，其 buffer 必须与主仓的块不同 —— 证明它真的换了 buffer
        bool sawDistinctBuffer = false;
        for (const Render::RT::GeometryBlock& block : blocks)
        {
            if (block.buffer != blocks.front().buffer)
            {
                sawDistinctBuffer = true;
            }
        }
        EXPECT_TRUE(sawDistinctBuffer) << "分片后应当出现第二个缓冲句柄";
    }

    /// 释放跨仓的块后，统计里的已用量必须正确回落（释放也要路由到正确的仓）
    TEST_F(PersistentGeometryStoreTest, FreeRoutesToOwningStoreAcrossShards)
    {
        ASSERT_TRUE(m_store.initialize(m_runtime, tinyConfig()));

        std::vector<Render::RT::GeometryBlock> blocks;
        for (int i = 0; i < 1024 && m_store.storeCount() == 1; ++i)
        {
            Render::RT::GeometryBlock block{};
            ASSERT_TRUE(m_store.allocBlock(kBlockBytes, block));
            blocks.push_back(block);
        }
        ASSERT_EQ(m_store.storeCount(), 2u);

        Render::RT::GeometryBlock sharded{};
        ASSERT_TRUE(m_store.allocBlock(kBlockBytes, sharded));
        blocks.push_back(sharded);

        const uint64_t usedBefore = m_store.totalStats().usedBytes;
        ASSERT_GT(usedBefore, 0u);

        // 释放分片块（在第二仓里）——它必须把该仓的 usedBytes 降下来
        m_store.freeBlock(sharded.id);
        const uint64_t usedAfter = m_store.totalStats().usedBytes;
        EXPECT_LT(usedAfter, usedBefore) << "释放分片块没有反映到聚合统计上，说明释放时路由到了错误的仓";
    }

    /// 统计是各仓之和：分片后总容量必须超过单仓上限
    TEST_F(PersistentGeometryStoreTest, TotalStatsAggregatesAllStores)
    {
        ASSERT_TRUE(m_store.initialize(m_runtime, tinyConfig()));

        for (int i = 0; i < 1024 && m_store.storeCount() == 1; ++i)
        {
            Render::RT::GeometryBlock block{};
            ASSERT_TRUE(m_store.allocBlock(kBlockBytes, block));
        }
        ASSERT_EQ(m_store.storeCount(), 2u);

        const Render::RT::GeometryStoreStats stats = m_store.totalStats();
        EXPECT_GT(stats.capacityBytes, kTinyStoreMax)
            << "分片之后总容量必须大于单仓上限，否则「4GB 天花板」并没有被突破";
    }

    /// 仓数上限生效：到顶后分配必须明确失败，而不是无限开仓把内存耗光
    TEST_F(PersistentGeometryStoreTest, StopsAtMaxStoresInsteadOfGrowingForever)
    {
        ASSERT_TRUE(m_store.initialize(m_runtime, tinyConfig(/*maxStores=*/2)));

        int allocated = 0;
        for (int i = 0; i < 4096; ++i)
        {
            Render::RT::GeometryBlock block{};
            if (!m_store.allocBlock(kBlockBytes, block))
            {
                break;
            }
            ++allocated;
        }

        EXPECT_EQ(m_store.storeCount(), 2u) << "不得突破 maxStores";
        EXPECT_GT(allocated, 0) << "至少应该能分配出两块仓的容量";
        // 到顶之后仍必须明确失败（而不是返回一个不可用的块）
        Render::RT::GeometryBlock overflow{};
        EXPECT_FALSE(m_store.allocBlock(kBlockBytes, overflow));
    }

    /// shutdown 必须销毁全部仓（含分片出来的），且幂等
    TEST_F(PersistentGeometryStoreTest, ShutdownReleasesEveryStore)
    {
        ASSERT_TRUE(m_store.initialize(m_runtime, tinyConfig()));

        for (int i = 0; i < 1024 && m_store.storeCount() == 1; ++i)
        {
            Render::RT::GeometryBlock block{};
            ASSERT_TRUE(m_store.allocBlock(kBlockBytes, block));
        }
        ASSERT_EQ(m_store.storeCount(), 2u);

        m_store.shutdown();
        EXPECT_FALSE(m_store.valid());
        EXPECT_EQ(m_store.storeCount(), 0u);

        // 幂等：第二次调用不应触碰已失效的句柄
        m_store.shutdown();
        EXPECT_FALSE(m_store.valid());
    }
}  // namespace