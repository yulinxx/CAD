#include "RenderBridge/PersistentGeometryStore.h"

// 头文件只有前向声明，调用接口方法需要完整定义。
// renderx.h 在本文件里已彻底不存在：后端细节只在适配层。
#include "RenderAbstraction/IRenderDevice.h"
#include "RenderAbstraction/IRenderScene.h"

#include "Log/SyLogger.h"

namespace RenderBridge
{
    using namespace RenderAbstraction;

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

    bool PersistentGeometryStore::initialize(
        IRenderDevice& device, IRenderScene* scene, const Config& config)
    {
        shutdown();
        m_device = &device;
        m_scene = scene;

        m_storeInitialBytes = config.storeInitialBytes;
        m_storeDescBytes = config.storeMaxBytes;
        m_storeGranularity = config.storeGranularity;
        m_maxStores = config.maxStores == 0 ? 1u : config.maxStores;

        GeometryStoreDesc storeDesc{};
        storeDesc.initialBytes = m_storeInitialBytes;
        storeDesc.maxBytes = m_storeDescBytes;
        storeDesc.granularity = m_storeGranularity;
        storeDesc.forIndices = false;

        const GeometryStoreHandle store = device.createGeometryStore(storeDesc);
        if (!isValid(store))
        {
            m_device = nullptr;
            m_scene = nullptr;
            return false;
        }

        // 绘制列表走抽象层创建：列表的剔除判据（2D 矩形 / 3D 视锥）由后端在创建时
        // 记住，之后按句柄分派 upsert / submit 的实现。scene 为空时跳过。
        if (scene != nullptr)
        {
            DrawListDesc listDesc{};
            listDesc.initialCapacity = config.drawListInitialCapacity;
            listDesc.enableMerging = config.drawListMerging;
            listDesc.enableCulling = config.drawListCulling;
            listDesc.type = config.drawListViewType;

            m_drawList = scene->createDrawList(listDesc);
            if (!isValid(m_drawList))
            {
                device.destroyGeometryStore(store);
                m_device = nullptr;
                m_scene = nullptr;
                return false;
            }
        }

        m_stores.clear();
        m_stores.push_back(store);
        m_blockStore.clear();
        return true;
    }

    void PersistentGeometryStore::shutdown()
    {
        if (m_device == nullptr)
        {
            return;
        }
        // 先拆绘制列表：它引用几何仓里的块，反向不引用
        if (m_scene != nullptr && isValid(m_drawList))
        {
            m_scene->destroyDrawList(m_drawList);
        }
        m_drawList = {};

        for (const GeometryStoreHandle store : m_stores)
        {
            if (isValid(store))
            {
                m_device->destroyGeometryStore(store);
            }
        }
        m_stores.clear();
        m_blockStore.clear();
        m_device = nullptr;
        m_scene = nullptr;

        m_freeSlots.clear();
        m_nextSlot = 0;
    }

    bool PersistentGeometryStore::valid() const
    {
        return m_device != nullptr && !m_stores.empty();
    }

    GeometryStoreHandle PersistentGeometryStore::storeForBlock(uint64_t blockId) const
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
        return index < m_stores.size() ? m_stores[index] : GeometryStoreHandle{};
    }

    uint32_t PersistentGeometryStore::storeCount() const
    {
        return static_cast<uint32_t>(m_stores.size());
    }

    GeometryStoreStats PersistentGeometryStore::totalStats() const
    {
        GeometryStoreStats sum{};
        if (m_device == nullptr)
        {
            return sum;
        }
        for (const GeometryStoreHandle store : m_stores)
        {
            const GeometryStoreStats one = m_device->geometryStoreStats(store);
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

    bool PersistentGeometryStore::upsertDrawItem(
        uint32_t slot, const DrawInstruction& command, const Aabb3* bounds)
    {
        if (m_scene == nullptr || !isValid(m_drawList))
        {
            return false;
        }
        return m_scene->upsertDrawItem(m_drawList, slot, command, bounds);
    }

    void PersistentGeometryStore::removeCommand(uint32_t slot)
    {
        if (m_scene != nullptr && isValid(m_drawList))
        {
            m_scene->removeDrawItem(m_drawList, slot);
        }
    }

    void PersistentGeometryStore::removeSlot(uint32_t slot)
    {
        removeCommand(slot);
        releaseSlot(slot);
    }

    void PersistentGeometryStore::clearDrawList()
    {
        if (m_scene != nullptr && isValid(m_drawList))
        {
            m_scene->clearDrawList(m_drawList);
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
        GeometryBlock block{};
        GeometryAllocResult result = m_device->allocGeometry(m_stores[index], bytes, block);

        if (result == GeometryAllocResult::StoreFull)
        {
            // 活动仓已到单仓上限：开新仓继续（分片）
            if (!addStore())
            {
                return false;
            }
            index = m_stores.size() - 1;
            result = m_device->allocGeometry(m_stores[index], bytes, block);
        }

        if (result != GeometryAllocResult::Ok)
        {
            return false;
        }

        // 成功后才落 out：失败路径不留下半个块
        out = block;

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
        return m_device->writeGeometry(storeForBlock(blockId), blockId, byteOffset, sizeBytes, data);
    }

    void PersistentGeometryStore::freeBlock(uint64_t blockId)
    {
        if (!valid() || blockId == 0)
        {
            return;
        }
        m_device->freeGeometry(storeForBlock(blockId), blockId);
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
        for (const GeometryStoreHandle store : m_stores)
        {
            m_device->flushGeometry(store);
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
            // 借一次分配把仓顶上去，拿到块立刻还回去：只为触发扩容，不占用空间
            GeometryBlock tmp{};
            if (m_device->allocGeometry(m_stores[index], bytes, tmp) == GeometryAllocResult::Ok)
            {
                m_device->freeGeometry(m_stores[index], tmp.id);
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

        GeometryStoreDesc storeDesc{};
        storeDesc.initialBytes = m_storeInitialBytes;
        storeDesc.maxBytes = m_storeDescBytes;
        storeDesc.granularity = m_storeGranularity;
        storeDesc.forIndices = false;

        const GeometryStoreHandle store = m_device->createGeometryStore(storeDesc);
        if (!isValid(store))
        {
            SY_ERRORF("[GeomStore] 第 %u 个仓创建失败（单仓上限 %llu bytes）",
                static_cast<uint32_t>(m_stores.size() + 1),
                static_cast<unsigned long long>(m_storeDescBytes));
            return false;
        }

        m_stores.push_back(store);
        SY_INFOF("[GeomStore] 单仓达上限，已开第 %u 个仓（分片生效）；"
                 "总容量 %llu bytes，此后合批只在仓内发生",
            static_cast<uint32_t>(m_stores.size()),
            static_cast<unsigned long long>(totalStats().capacityBytes));
        return true;
    }
}  // namespace RenderBridge
