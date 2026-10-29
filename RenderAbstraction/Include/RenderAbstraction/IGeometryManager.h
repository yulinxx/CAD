#pragma once
/**
 * @file IGeometryManager.h
 * @brief 几何管道的抽象接口（2D RenderSceneBuilder / 3D Mesh3DBuilder 共用）
 *
 * 实现在 RenderBridge/GeometryPipeline.h 的 RenderBridge::GeometryManager。
 * UI 层只持有本接口，不感知 RenderBridge 实现层的类型 —— 这是 UI 与 RenderBridge
 * 的分层边界：接口归 RenderAbstraction，实现归 RenderBridge，装配（构造实现）只发生在
 * UI 的 .cpp 里。
 */

#include "IRenderTypes.h"

#include <cstdint>

namespace RenderAbstraction
{
    class IRenderDevice;
    class IRenderScene;

    struct GeometryPipelineConfig
    {
        uint64_t storeInitialBytes = 8ull * 1024 * 1024;
        uint64_t storeMaxBytes = 0;
        uint32_t storeGranularity = 0;
        uint32_t maxStores = 8;
        uint32_t drawListInitialCapacity = 4096;
        bool drawListMerging = true;
        bool drawListCulling = true;
        ViewVolumeType drawListViewType = ViewVolumeType::Rect2D;
    };

    struct GeometryPipelineStats
    {
        uint32_t entityCount = 0;
        uint32_t storeCount = 0;
        uint64_t usedBytes = 0;
        uint64_t capacityBytes = 0;
        uint32_t growCount = 0;
    };

    /**
     * @brief 几何管道抽象：实体级几何仓 + 绘制列表 + 槽号台账
     *
     * 方法集只覆盖 UI 构建器实际用到的生命周期 / 块 / 槽 / 绘制命令操作；
     * 实现层的实体哈希、增量短路等内部机制不进接口。
     */
    class IGeometryManager
    {
    public:
        IGeometryManager() = default;
        virtual ~IGeometryManager() = default;

        IGeometryManager(const IGeometryManager&) = delete;
        IGeometryManager& operator=(const IGeometryManager&) = delete;

        virtual bool initialize(IRenderDevice& device, IRenderScene* scene, const GeometryPipelineConfig& config = {}) = 0;
        virtual void shutdown() = 0;
        virtual bool valid() const = 0;
        virtual DrawListHandle drawList() const = 0;

        virtual void beginRebuild() = 0;
        virtual void endRebuild() = 0;

        virtual void clear() = 0;
        virtual void clearDrawList() = 0;
        virtual void resetSlots() = 0;
        virtual void removeEntity(uint64_t id) = 0;
        virtual void removeCommand(uint32_t slot) = 0;
        virtual void removeSlot(uint32_t slot) = 0;
        virtual void releaseSlot(uint32_t slot) = 0;
        virtual uint32_t acquireSlot() = 0;

        virtual bool allocBlock(uint64_t bytes, GeometryBlock& outBlock) = 0;
        virtual bool writeBlock(uint64_t blockId, uint32_t offset, uint32_t sizeBytes, const void* data) = 0;
        virtual void freeBlock(uint64_t blockId) = 0;
        virtual bool upsertDrawItem(uint32_t slot, const DrawInstruction& cmd, const Aabb3* bounds) = 0;

        virtual void flush() = 0;
        virtual GeometryStoreStats totalStats() const = 0;
        virtual uint64_t reserveCapacity(uint64_t bytes) = 0;
        /// 几何仓字节上限的推荐值（估算策略由实现定，如按可用显存/内存）
        virtual uint64_t recommendedMaxBytes() const = 0;
    };
}  // namespace RenderAbstraction
