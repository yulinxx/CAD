#include "RenderBridge/PersistentGeometryStore.h"

// renderx.h 只在 .cpp 内部引入，头文件不再暴露它
#include "render/renderx.h"

#include "Log/SyLogger.h"

namespace RenderBridge
{
    namespace
    {
        // ---------- 类型转换辅助 ----------
        // 头文件存 uint64_t 裸值，这里统一做裸值 ↔ RenderX Handle 的转换。

        inline Render::RT::RuntimeHandle toRuntime(uint64_t v)
        {
            return static_cast<Render::RT::RuntimeHandle>(v);
        }
        inline Render::RT::GeometryStoreHandle toStore(uint64_t v)
        {
            return static_cast<Render::RT::GeometryStoreHandle>(v);
        }
        inline Render::RT::DrawListHandle toDrawList(uint64_t v)
        {
            return static_cast<Render::RT::DrawListHandle>(v);
        }
        inline bool isValidRuntime(uint64_t v)
        {
            return Render::RT::rxValid(toRuntime(v)) != 0;
        }
        inline bool isValidStore(uint64_t v)
        {
            return Render::RT::rxValid(toStore(v)) != 0;
        }
    }  // namespace

    // ========== 静态方法 ==========

    uint64_t PersistentGeometryStore::recommendedMaxBytes()
    {
        uint64_t physical = 0;
#if defined(__APPLE__)
        int mib[2] = { CTL_HW, HW_MEMSIZE };
        std::size_t len = sizeof(physical);
        if (::sysctl(mib, 2, &physical, &len, nullptr, 0) != 0)
        {
            physical = 0;
        }
#elif defined(_WIN32)
        MEMORYSTATUSEX statex{};
        statex.dwLength = sizeof(statex);
        if (GlobalMemoryStatusEx(&statex))
        {
            physical = static_cast<uint64_t>(statex.ullTotalPhys);
        }
#elif defined(__linux__)
        struct sysinfo info{};
        if (::sysinfo(&info) == 0)
        {
            physical = static_cast<uint64_t>(info.totalram) * static_cast<uint64_t>(info.mem_unit);
        }
#endif
        if (physical == 0)
        {
            return kStoreFloorMaxBytes;
        }
        uint64_t target = physical / 4;
        if (target < kStoreFloorMaxBytes)
        {
            target = kStoreFloorMaxBytes;
        }
        if (target > kStoreAbsoluteMaxBytes)
        {
            target = kStoreAbsoluteMaxBytes;
        }
        return target;
    }

    // ========== 生命周期 ==========

    PersistentGeometryStore::~PersistentGeometryStore()
    {
        shutdown();
    }

    bool PersistentGeometryStore::initialize(uint64_t runtime, const Config& config)
    {
        shutdown();
        if (!isValidRuntime(runtime))
        {
            return false;
        }

        m_runtime = runtime;
        m_storeInitialBytes = config.storeInitialBytes;
        m_storeDescBytes = config.storeMaxBytes;
        m_storeGranularity = config.storeGranularity;
        m_maxStores = config.maxStores == 0 ? 1u : config.maxStores;

        Render::RT::GeometryStoreDesc storeDesc{};
        storeDesc.initialBytes = m_storeInitialBytes;
        storeDesc.maxBytes = m_storeDescBytes;
        storeDesc.granularity = m_storeGranularity;
        storeDesc.forIndices = 0;

        const Render::RT::GeometryStoreHandle store = Render::RT::rxGeometryStoreCreate(toRuntime(m_runtime), &storeDesc);
        if (!Render::RT::rxValid(store))
        {
            return false;
        }

        Render::RT::DrawListDesc listDesc{};
        listDesc.initialCapacity = config.drawListInitialCapacity;
        listDesc.enableMerging = config.drawListMerging ? 1 : 0;
        listDesc.enableCulling = config.drawListCulling ? 1 : 0;

        const Render::RT::DrawListHandle list = Render::RT::rxDrawListCreate(toRuntime(m_runtime), &listDesc);
        if (!Render::RT::rxValid(list))
        {
            Render::RT::rxGeometryStoreDestroy(toRuntime(m_runtime), store);
            return false;
        }

        m_stores.clear();
        m_stores.push_back(static_cast<uint64_t>(store));
        m_blockStore.clear();
        m_drawListRaw = static_cast<uint64_t>(list);
        return true;
    }

    void PersistentGeometryStore::shutdown()
    {
        if (!isValidRuntime(m_runtime))
        {
            return;
        }
        if (Render::RT::rxValid(toDrawList(m_drawListRaw)))
        {
            Render::RT::rxDrawListDestroy(toRuntime(m_runtime), toDrawList(m_drawListRaw));
            m_drawListRaw = 0;
        }
        for (uint64_t storeRaw : m_stores)
        {
            if (isValidStore(storeRaw))
            {
                Render::RT::rxGeometryStoreDestroy(toRuntime(m_runtime), toStore(storeRaw));
            }
        }
        m_stores.clear();
        m_blockStore.clear();
        m_runtime = 0;

        m_freeSlots.clear();
        m_nextSlot = 0;
    }

    bool PersistentGeometryStore::valid() const
    {
        return isValidRuntime(m_runtime);
    }

    uint64_t PersistentGeometryStore::runtimeValue() const { return m_runtime; }
    uint64_t PersistentGeometryStore::storeValue() const
    {
        return m_stores.empty() ? 0 : m_stores.front();
    }
    uint64_t PersistentGeometryStore::drawListValue() const { return m_drawListRaw; }
    uint64_t PersistentGeometryStore::storeForBlock(uint64_t blockId) const
    {
        uint32_t index = 0;
        if (!m_blockStore.empty())
        {
            const auto found = m_blockStore.find(blockId);
            if (found != m_blockStore.end())
            {
                index = found->second;
            }
        }
        return index < m_stores.size() ? m_stores[index] : 0;
    }
    uint32_t PersistentGeometryStore::storeCount() const
    {
        return static_cast<uint32_t>(m_stores.size());
    }

    GeometryStoreStats PersistentGeometryStore::totalStats() const
    {
        // 对各仓的 RenderX 统计做聚合，再逐字段填进本层的 POD 镜像
        GeometryStoreStats sum{};
        for (uint64_t storeRaw : m_stores)
        {
            Render::RT::GeometryStoreStats one{};
            if (Render::RT::rxGeometryStoreGetStats(toRuntime(m_runtime), toStore(storeRaw), &one) != Render::RT::RxResult::Ok)
            {
                continue;
            }
            sum.capacityBytes += one.capacityBytes;
            sum.usedBytes += one.usedBytes;
            // 最大连续空洞不是可加的，取最大者
            if (one.largestFreeBytes > sum.largestFreeBytes)
            {
                sum.largestFreeBytes = one.largestFreeBytes;
            }
            sum.blockCount += one.blockCount;
            sum.freeRangeCount += one.freeRangeCount;
            sum.dirtyBytesThisFrame += one.dirtyBytesThisFrame;
            sum.growCount += one.growCount;
        }
        return sum;
    }

    // ========== 槽位 ==========

    uint32_t PersistentGeometryStore::acquireSlot()
    {
        if (!m_freeSlots.empty())
        {
            const uint32_t slot = m_freeSlots.back();
            m_freeSlots.pop_back();
            return slot;
        }
        return m_nextSlot++;
    }

    void PersistentGeometryStore::releaseSlot(uint32_t slot)
    {
        m_freeSlots.push_back(slot);
    }

    void PersistentGeometryStore::removeCommand(uint32_t slot)
    {
        if (valid())
        {
            Render::RT::rxDrawListRemove(toRuntime(m_runtime), toDrawList(m_drawListRaw), slot);
        }
    }

    void PersistentGeometryStore::removeSlot(uint32_t slot)
    {
        removeCommand(slot);
        releaseSlot(slot);
    }

    void PersistentGeometryStore::clearDrawList()
    {
        if (valid())
        {
            Render::RT::rxDrawListClear(toRuntime(m_runtime), toDrawList(m_drawListRaw));
        }
    }

    void PersistentGeometryStore::resetSlots()
    {
        m_freeSlots.clear();
        m_nextSlot = 0;
    }

    // ========== 几何块 ==========

    bool PersistentGeometryStore::allocBlock(uint64_t bytes, GeometryBlock& out)
    {
        if (!valid())
        {
            return false;
        }

        size_t index = m_stores.size() - 1;
        Render::RT::GeometryBlock block{};
        Render::RT::RxResult result = Render::RT::rxGeometryAlloc(
            toRuntime(m_runtime), toStore(m_stores[index]), bytes, &block);

        if (result == Render::RT::RxResult::ErrorOutOfMemory)
        {
            // 活动仓已到单仓上限：开新仓继续（分片）
            if (!addStore())
            {
                return false;
            }
            index = m_stores.size() - 1;
            result = Render::RT::rxGeometryAlloc(
                toRuntime(m_runtime), toStore(m_stores[index]), bytes, &block);
        }

        if (result != Render::RT::RxResult::Ok && result != Render::RT::RxResult::ErrorGeometryStoreGrown)
        {
            return false;
        }

        // RenderX 块 → 本层 POD 镜像（句柄降为裸值）
        out.buffer = static_cast<uint64_t>(block.buffer);
        out.id = block.id;
        out.offset = block.offset;
        out.sizeBytes = block.sizeBytes;

        // 只登记分片块：仓 0 的块靠「查不到即默认 0」兜住
        if (index != 0)
        {
            m_blockStore[out.id] = static_cast<uint32_t>(index);
        }
        return true;
    }

    bool PersistentGeometryStore::writeBlock(uint64_t blockId, uint32_t byteOffset, uint32_t sizeBytes, const void* data)
    {
        if (!valid())
        {
            return false;
        }
        return Render::RT::rxGeometryWrite(
            toRuntime(m_runtime),
            toStore(storeForBlock(blockId)),
            blockId, byteOffset, sizeBytes, data) == Render::RT::RxResult::Ok;
    }

    void PersistentGeometryStore::freeBlock(uint64_t blockId)
    {
        if (!valid() || blockId == 0)
        {
            return;
        }
        Render::RT::rxGeometryFree(
            toRuntime(m_runtime),
            toStore(storeForBlock(blockId)),
            blockId);
        if (!m_blockStore.empty())
        {
            m_blockStore.erase(blockId);
        }
    }

    void PersistentGeometryStore::flush()
    {
        if (!valid())
        {
            return;
        }
        for (uint64_t storeRaw : m_stores)
        {
            Render::RT::rxGeometryFlush(toRuntime(m_runtime), toStore(storeRaw));
        }
    }

    uint64_t PersistentGeometryStore::reserveCapacity(uint64_t requiredBytes)
    {
        if (!valid())
        {
            return 0;
        }

        uint64_t total = totalStats().capacityBytes;
        total = driveGrow(m_stores.size() - 1, requiredBytes > total ? requiredBytes - total : 0);

        while (total < requiredBytes && m_stores.size() < m_maxStores)
        {
            if (!addStore())
            {
                break;
            }
            total = driveGrow(m_stores.size() - 1, requiredBytes - total);
        }
        return total;
    }

    // ========== 私有 ==========

    uint64_t PersistentGeometryStore::driveGrow(size_t index, uint64_t bytes)
    {
        if (bytes > 0 && index < m_stores.size())
        {
            Render::RT::GeometryBlock tmp{};
            const Render::RT::RxResult result = Render::RT::rxGeometryAlloc(
                toRuntime(m_runtime), toStore(m_stores[index]), bytes, &tmp);
            if (result == Render::RT::RxResult::Ok || result == Render::RT::RxResult::ErrorGeometryStoreGrown)
            {
                Render::RT::rxGeometryFree(
                    toRuntime(m_runtime), toStore(m_stores[index]), tmp.id);
            }
        }
        return totalStats().capacityBytes;
    }

    bool PersistentGeometryStore::addStore()
    {
        if (m_stores.size() >= m_maxStores)
        {
            SY_ERRORF("[GeomStore] 已达分片上限 %u 个仓（总容量 %llu bytes），"
                      "无法再分配——检查是否出现了不释放的几何增长",
                m_maxStores,
                static_cast<unsigned long long>(totalStats().capacityBytes));
            return false;
        }

        Render::RT::GeometryStoreDesc storeDesc{};
        storeDesc.initialBytes = m_storeInitialBytes;
        storeDesc.maxBytes = m_storeDescBytes;
        storeDesc.granularity = m_storeGranularity;
        storeDesc.forIndices = 0;

        const Render::RT::GeometryStoreHandle store =
            Render::RT::rxGeometryStoreCreate(toRuntime(m_runtime), &storeDesc);
        if (!Render::RT::rxValid(store))
        {
            SY_ERRORF("[GeomStore] 第 %u 个仓创建失败（单仓上限 %llu bytes）",
                static_cast<uint32_t>(m_stores.size() + 1),
                static_cast<unsigned long long>(m_storeDescBytes));
            return false;
        }

        m_stores.push_back(static_cast<uint64_t>(store));
        SY_INFOF("[GeomStore] 单仓达上限，已开第 %u 个仓（分片生效）；"
                 "总容量 %llu bytes，此后合批只在仓内发生",
            static_cast<uint32_t>(m_stores.size()),
            static_cast<unsigned long long>(totalStats().capacityBytes));
        return true;
    }
}  // namespace RenderBridge
