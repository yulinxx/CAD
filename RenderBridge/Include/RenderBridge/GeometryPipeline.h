#pragma once
/**
 * @file GeometryPipeline.h
 * @brief 统一的几何管理管道：2D/3D 共享的块管理、槽位、哈希比较
 *
 * ## 设计目标
 *
 * RenderSceneBuilder (2D) 和 Mesh3DBuilder (3D) 约有 80% 的逻辑相同：
 * - 几何仓的块分配/写入/释放
 * - 紧凑槽号管理
 * - 内容哈希比较（跳过未变化的图元）
 * - 绘制命令的创建与 upsert
 * - 增量更新（新增/修改/删除图元）
 *
 * 本文件提供两个可复用组件：
 * - `GeometryManager`：封装 PersistentGeometryStore 的实体级管理
 * - `GeometryPipelineConfig`：配置参数
 *
 * 两个 builder 通过组合使用 GeometryManager，
 * 各自保留特有的实体数据结构和绘制命令生成逻辑。
 *
 * ## 类型隔离
 *
 * 本文件只依赖 RenderAbstraction 的抽象接口，不依赖 renderx.h。
 */

#include "RenderAbstraction/IRenderDevice.h"
#include "RenderAbstraction/IRenderScene.h"
#include "RenderBridge/RenderBridgeAPI.h"
#include "RenderBridge/PersistentGeometryStore.h"

#include <cstdint>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace RenderBridge
{
    struct GeometryPipelineConfig
    {
        uint64_t storeInitialBytes = 8ull * 1024 * 1024;
        uint64_t storeMaxBytes = 0;
        uint32_t storeGranularity = 0;
        uint32_t maxStores = 8;
        uint32_t drawListInitialCapacity = 4096;
        bool drawListMerging = true;
        bool drawListCulling = true;
        RenderAbstraction::ViewVolumeType drawListViewType = RenderAbstraction::ViewVolumeType::Rect2D;
    };

    struct GeometryPipelineStats
    {
        uint32_t entityCount = 0;
        uint32_t storeCount = 0;
        uint64_t usedBytes = 0;
        uint64_t capacityBytes = 0;
        uint32_t growCount = 0;
    };

    class GeometryManager
    {
    public:
        GeometryManager() = default;
        virtual ~GeometryManager() = default;

    public:
        GeometryManager(const GeometryManager&) = delete;
        GeometryManager& operator=(const GeometryManager&) = delete;

        bool initialize(RenderAbstraction::IRenderDevice& device,
            RenderAbstraction::IRenderScene* scene,
            const GeometryPipelineConfig& config = {})
        {
            shutdown();

            PersistentGeometryStore::Config storeConfig;
            storeConfig.storeInitialBytes = config.storeInitialBytes;
            storeConfig.storeMaxBytes = config.storeMaxBytes;
            storeConfig.storeGranularity = config.storeGranularity;
            storeConfig.maxStores = config.maxStores;
            storeConfig.drawListInitialCapacity = config.drawListInitialCapacity;
            storeConfig.drawListMerging = config.drawListMerging;
            storeConfig.drawListCulling = config.drawListCulling;
            storeConfig.drawListViewType = config.drawListViewType;

            if (!m_store.initialize(device, scene, storeConfig))
            {
                return false;
            }

            m_config = config;
            return true;
        }

        void shutdown()
        {
            m_store.shutdown();
            m_entities.clear();
            m_seen.clear();
            m_sequence = 0;
            m_syntheticId = 0;
            m_currentEntityId = 0;
        }

        bool valid() const
        {
            return m_store.valid();
        }

        RenderAbstraction::DrawListHandle drawList() const
        {
            return m_store.drawList();
        }

        void beginRebuild()
        {
            m_seen.clear();
            m_syntheticId = 0;
            m_currentEntityId = 0;
        }

        void endRebuild()
        {
            std::vector<uint64_t> stale;
            for (const auto& [id, entry] : m_entities)
            {
                if (m_seen.find(id) == m_seen.end())
                {
                    stale.push_back(id);
                }
            }
            for (const uint64_t id : stale)
            {
                removeEntity(id);
            }
            m_store.flush();
        }

        void setCurrentEntityId(uint64_t id)
        {
            m_currentEntityId = id;
        }

        void clear()
        {
            for (auto& [id, entry] : m_entities)
            {
                releaseEntry(entry);
            }
            m_entities.clear();
            m_store.clearDrawList();
            m_store.resetSlots();
            m_sequence = 0;
            m_syntheticId = 0;
        }

        void removeEntity(uint64_t id)
        {
            auto it = m_entities.find(id);
            if (it == m_entities.end())
            {
                return;
            }
            releaseEntry(it->second);
            m_entities.erase(it);
        }

        uint32_t entityCount() const
        {
            return static_cast<uint32_t>(m_entities.size());
        }

        void flush()
        {
            m_store.flush();
        }

        GeometryPipelineStats stats() const
        {
            GeometryPipelineStats result;
            result.entityCount = static_cast<uint32_t>(m_entities.size());
            result.storeCount = m_store.storeCount();
            const auto total = m_store.totalStats();
            result.usedBytes = total.usedBytes;
            result.capacityBytes = total.capacityBytes;
            result.growCount = static_cast<uint32_t>(total.growCount);
            return result;
        }

        GeometryStoreStats totalStats() const
        {
            return m_store.totalStats();
        }

        uint64_t reserveCapacity(uint64_t bytes)
        {
            return m_store.reserveCapacity(bytes);
        }

        static uint64_t recommendedMaxBytes()
        {
            return PersistentGeometryStore::recommendedMaxBytes();
        }

        uint32_t storeCount() const
        {
            return m_store.storeCount();
        }

        void resetSlots()
        {
            m_store.resetSlots();
        }

        void clearDrawList()
        {
            m_store.clearDrawList();
        }

        void removeCommand(uint32_t slot)
        {
            m_store.removeCommand(slot);
        }

        bool allocBlock(uint64_t bytes, RenderAbstraction::GeometryBlock& outBlock)
        {
            return m_store.allocBlock(bytes, outBlock);
        }

        bool writeBlock(uint64_t blockId, uint32_t offset, uint32_t sizeBytes, const void* data)
        {
            return m_store.writeBlock(blockId, offset, sizeBytes, data);
        }

        void freeBlock(uint64_t blockId)
        {
            m_store.freeBlock(blockId);
        }

        uint32_t acquireSlot()
        {
            return m_store.acquireSlot();
        }

        void releaseSlot(uint32_t slot)
        {
            m_store.releaseSlot(slot);
        }

        void removeSlot(uint32_t slot)
        {
            m_store.removeSlot(slot);
        }

        bool upsertDrawItem(
            uint32_t slot, const RenderAbstraction::DrawInstruction& cmd, const RenderAbstraction::Aabb3* bounds)
        {
            return m_store.upsertDrawItem(slot, cmd, bounds);
        }

        PersistentGeometryStore& geometryStore()
        {
            return m_store;
        }

        uint64_t currentEntityId() const
        {
            return m_currentEntityId;
        }

    protected:
        struct EntityEntryBase
        {
            uint64_t blockId = 0;
            uint32_t slot = 0;
            uint32_t vertexCount = 0;
            RenderAbstraction::BufferHandle buffer;
            uint32_t byteOffset = 0;
            uint64_t contentHash = 0;
            uint16_t sequence = 0;
        };

        template<typename VertexT>
        bool upsertEntityInternal(uint64_t entityId,
            const VertexT* vertices,
            uint32_t vertexCount,
            RenderAbstraction::PrimitiveType topology,
            uint32_t layerIndex,
            uint64_t hash)
        {
            if (!m_store.valid() || !vertices || vertexCount == 0)
            {
                return false;
            }

            const uint64_t bytes = static_cast<uint64_t>(vertexCount) * sizeof(VertexT);
            auto it = m_entities.find(entityId);
            const bool isNew = (it == m_entities.end());
            EntityEntryBase& entry = isNew ? m_entities[entityId] : it->second;

            if (!isNew && entry.contentHash == hash && entry.vertexCount == vertexCount)
            {
                return true;
            }

            const bool needRealloc = isNew || entry.vertexCount != vertexCount;

            if (needRealloc)
            {
                if (entry.blockId != 0)
                {
                    m_store.freeBlock(entry.blockId);
                    entry.blockId = 0;
                }

                RenderAbstraction::GeometryBlock block{};
                if (!allocBlock(bytes, block))
                {
                    if (!isNew)
                    {
                        releaseEntry(entry);
                    }
                    m_entities.erase(entityId);
                    return false;
                }
                entry.blockId = block.id;
                entry.buffer = block.buffer;
                entry.byteOffset = block.offset;
                if (isNew)
                {
                    entry.slot = m_store.acquireSlot();
                }
            }

            if (!m_store.writeBlock(entry.blockId, 0, static_cast<uint32_t>(bytes), vertices))
            {
                if (entry.blockId != 0)
                {
                    m_store.freeBlock(entry.blockId);
                    entry.blockId = 0;
                }
                if (!isNew)
                {
                    releaseEntry(entry);
                }
                m_entities.erase(entityId);
                return false;
            }

            entry.vertexCount = vertexCount;
            entry.contentHash = hash;
            entry.sequence = m_sequence++;

            auto bounds = doComputeBounds(vertices, vertexCount);
            auto cmd = doMakeDrawCommand(entry, layerIndex, topology);

            if (!m_store.upsertDrawItem(entry.slot, cmd, bounds ? &*bounds : nullptr))
            {
                if (entry.blockId != 0)
                {
                    m_store.freeBlock(entry.blockId);
                    entry.blockId = 0;
                }
                if (!isNew)
                {
                    releaseEntry(entry);
                }
                m_entities.erase(entityId);
                return false;
            }

            return true;
        }

        void releaseEntry(EntityEntryBase& entry)
        {
            if (entry.blockId != 0)
            {
                m_store.freeBlock(entry.blockId);
                entry.blockId = 0;
            }
            if (entry.slot != 0)
            {
                m_store.removeSlot(entry.slot);
                entry.slot = 0;
            }
        }

        virtual RenderAbstraction::DrawInstruction doMakeDrawCommand(
            const EntityEntryBase& /*entry*/, uint32_t /*layerIndex*/, RenderAbstraction::PrimitiveType /*topology*/)
        {
            return {};
        }

        virtual std::optional<RenderAbstraction::Aabb3> doComputeBounds(const void* /*vertices*/, uint32_t /*count*/)
        {
            return std::nullopt;
        }

        void markSeen(uint64_t id)
        {
            m_seen.insert(id);
        }

        bool isSeen(uint64_t id) const
        {
            return m_seen.count(id) > 0;
        }

        PersistentGeometryStore m_store;
        std::unordered_map<uint64_t, EntityEntryBase> m_entities;
        std::unordered_set<uint64_t> m_seen;
        GeometryPipelineConfig m_config;
        uint16_t m_sequence = 0;
        uint64_t m_syntheticId = 0;
        uint64_t m_currentEntityId = 0;
    };

}  // namespace RenderBridge
