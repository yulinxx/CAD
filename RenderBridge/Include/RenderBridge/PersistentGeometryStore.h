#pragma once
/**
 * @file PersistentGeometryStore.h
 * @brief 常驻几何仓 + 保留式绘制列表的宿主侧底座
 *
 * 2D 的 `Render::RenderSceneBuilder` 与 3D 的 `UI3D::Mesh3DBuilder` 原本各写一遍
 * 同构代码：建 GeometryStore、建 DrawList、维护紧凑槽号、分配/写入/释放几何块、
 * 帧外收口 flush、销毁时逆序拆。差异只有仓容量与 DrawList 的合批/剔除开关，
 * 这里做成 `Config`。
 *
 * 只共享「资源生命周期 + 槽位管理 + 块管理」，**不**共享几何处理：2D 是 P3C3 +
 * 世界矩形 AABB，3D 是 P3N3 + 世界 3D AABB + 材质 + 视锥剔除，顶点格式、拓扑、
 * 材质、剔除判据都留在各自 builder 里。
 *
 * 槽号必须紧凑分配（DrawList 上限 `1 << 24`，条目按槽号直接下标存放），所以这里
 * 维护空闲槽号栈；调用方**不要**拿 64 位图元 ID 当槽号。
 *
 * ## 为什么内部是「多仓」而不是一个仓
 *
 * DLL 的单仓硬顶是 4GB−1 —— 硬顶来自 `GeometryBlock::offset` 是 uint32
 * （`rxIncremental.cpp` 的 `kAbsoluteMaxBytes`），而且 `DrawCommand` 的 80 字节
 * 布局被 `static_assert` 锁死，把它加宽到 uint64 等于破 ABI。
 *
 * 但 4GB **不是「总容量」的天花板，只是「一个 buffer」的天花板**：几何块是逐图元
 * 的小块，且每条 `DrawCommand` 自带 `vertexBuffer` 句柄，因此天然可以分片到多个仓。
 * 于是这里在单仓达上限时**新开一个仓**继续分配——**一个字节的 ABI 都不用动**。
 *
 * 两个前提让分片是安全的（都是 DLL 侧已有的不变式，见 `rxIncremental.cpp` 的
 * `createBuffer`：扩容时原地改写公共句柄槽位，数值不变）：
 *   1. 每个仓的公共缓冲句柄在它的整个生命周期内稳定 —— 所以块自带的
 *      `GeometryBlock::buffer` 可以长期持有（3D 侧一直就是这么用的）；
 *   2. 块 id 在它所属的仓内唯一 —— 所以「块 → 仓」的归属必须由本类记住。
 *
 * 归属用一张**只登记分片块**的表来表达：查不到即默认仓 0。于是单仓（常态）下
 * 这张表是空的，`alloc/write/free` 只多一次 `size()==1` 判断，零额外开销；只有
 * 真的跨过 4GB 之后，分片出去的那部分块才进表。
 *
 * 代价：合批只在仓内发生（合批要求顶点区间在同一缓冲内字节连续），因此跨仓的块
 * 不会被合并。这只在总容量超过单仓上限后才可能发生，那时本来也没有多少合批空间。
 *
 * 做成纯头文件：它只是把 Renderx 的 C 接口收成一组不抛异常的小包装，
 * 没有跨模块状态，内联掉可以少一层 DLL 边界。
 */

#include "render/renderx.h"

#include "Log/SyLogger.h"

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

// 物理内存查询只用于推荐几何仓上限，按平台各取最轻量的 API。
#if defined(__APPLE__)
    #include <sys/sysctl.h>
#elif defined(_WIN32)
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <windows.h>
#elif defined(__linux__)
    #include <sys/sysinfo.h>
#endif

namespace RenderBridge
{
    class PersistentGeometryStore
    {
    public:
        /// DLL 几何仓的硬顶：块偏移/长度都是 uint32，单仓不能超过 4GB-1。
        /// 必须与 Renderx 内 rxIncremental.cpp 的 kAbsoluteMaxBytes 保持一致。
        static constexpr uint64_t kStoreAbsoluteMaxBytes = 0xFFFFFFFFull;
        /// 推荐上限的下限：小于此值普通图纸都可能装不下，没有工程意义。
        static constexpr uint64_t kStoreFloorMaxBytes = 512ull * 1024 * 1024;

        /**
         * @brief 按本机物理内存推荐**单仓**增长上限
         *
         * 取物理内存的 1/4，再钳到 [512MB, 4GB)。4GB 是 uint32 块偏移的硬顶，
         * 单仓无法通过配置绕过；**但总容量可以**——单仓满了会自动开新仓，
         * 见文件头的「为什么内部是多仓」。
         *
         * 为什么是 1/4：CpuToGpu 几何仓除 GPU 缓冲外（Apple Silicon 上与进程
         * 共享物理页）还持有一份等大的 CPU 影子内存，稳态占用约为容量的 2 倍；
         * 翻倍扩容的瞬间旧缓冲与旧影子都还在，峰值更高。取 1/4 可给文档模型、
         * Qt 与系统留出余量。24GB 机器取到硬顶 4GB，**单仓**即可容纳约 90 万图元、
         * 3GB 顶点量级的图纸；再大就落在第二个仓里。查询失败时保守回退到 512MB。
         */
        static uint64_t recommendedMaxBytes()
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

        /// 创建参数：只暴露两个 builder 真正不同的部分
        struct Config
        {
            /// 几何仓初始容量（字节）。0 表示用 DLL 默认值
            uint64_t storeInitialBytes = 8ull * 1024 * 1024;
            /// **单仓**增长上限（字节）。0 表示不限（DLL 仍会钳到 4GB 硬顶）
            uint64_t storeMaxBytes = kStoreFloorMaxBytes;
            /**
             * 最多允许几个仓（分片上限）。
             *
             * 总容量上限 ≈ maxStores × 单仓上限。8 个已经能到 32GB，远高于
             * `recommendedMaxBytes()`（物理内存的 1/4），既够用，也能在分配出现
             * 失控增长时明确失败而不是把机器内存耗光。
             */
            uint32_t maxStores = 8;
            /// 分配粒度（字节），0 表示用 DLL 默认（256，已按 vec4 对齐）
            uint32_t storeGranularity = 0;
            /// DrawList 初始容量（条目数）
            uint32_t drawListInitialCapacity = 4096;
            /// 相邻同伴合并。3D 必须关掉：相邻图元各有自己的 materialIndex，
            /// 合批会把材质抹成一个
            bool drawListMerging = true;
            /// 是否按条目的包围盒做剔除
            bool drawListCulling = true;
        };

        PersistentGeometryStore() = default;

        ~PersistentGeometryStore()
        {
            shutdown();
        }

        PersistentGeometryStore(const PersistentGeometryStore&) = delete;
        PersistentGeometryStore& operator=(const PersistentGeometryStore&) = delete;

        /**
         * @brief 在给定 Runtime 上创建几何仓与绘制列表
         *
         * 重复调用会先释放旧的。任一步失败都会回收已建好的部分并返回 false，
         * 不留半个资源。
         */
        bool initialize(Render::RT::RuntimeHandle runtime, const Config& config)
        {
            shutdown();
            if (!Render::RT::rxValid(runtime))
            {
                // 收到无效 Runtime 句柄
                return false;
            }

            Render::RT::GeometryStoreDesc storeDesc{};
            storeDesc.initialBytes = config.storeInitialBytes;
            storeDesc.maxBytes = config.storeMaxBytes;
            storeDesc.granularity = config.storeGranularity;
            storeDesc.forIndices = 0;
            m_storeDesc = storeDesc;
            m_maxStores = config.maxStores == 0 ? 1u : config.maxStores;

            const Render::RT::GeometryStoreHandle store = Render::RT::rxGeometryStoreCreate(runtime, &storeDesc);
            if (!Render::RT::rxValid(store))
            {
                // 几何仓创建失败
                return false;
            }

            Render::RT::DrawListDesc listDesc{};
            listDesc.initialCapacity = config.drawListInitialCapacity;
            listDesc.enableMerging = config.drawListMerging ? 1 : 0;
            listDesc.enableCulling = config.drawListCulling ? 1 : 0;

            const Render::RT::DrawListHandle list = Render::RT::rxDrawListCreate(runtime, &listDesc);
            if (!Render::RT::rxValid(list))
            {
                // 绘制列表创建失败
                Render::RT::rxGeometryStoreDestroy(runtime, store);
                return false;
            }

            m_runtime = runtime;
            m_stores.clear();
            m_stores.push_back(store);
            m_blockStore.clear();
            m_drawList = list;
            return true;
        }

        /**
         * @brief 销毁全部几何仓与绘制列表；幂等
         *
         * 先拆绘制列表：它引用几何仓里的块，反向不引用。块与槽位都随这些对象
         * 一起消失，不必逐个释放——逐个释放只是白跑一遍空闲链表合并。
         */
        void shutdown()
        {
            if (!Render::RT::rxValid(m_runtime))
            {
                return;
            }
            if (Render::RT::rxValid(m_drawList))
            {
                Render::RT::rxDrawListDestroy(m_runtime, m_drawList);
                m_drawList = Render::RT::DrawListHandle::Invalid;
            }
            for (const Render::RT::GeometryStoreHandle store : m_stores)
            {
                if (Render::RT::rxValid(store))
                {
                    Render::RT::rxGeometryStoreDestroy(m_runtime, store);
                }
            }
            m_stores.clear();
            m_blockStore.clear();
            m_runtime = Render::RT::RuntimeHandle::Invalid;

            m_freeSlots.clear();
            m_nextSlot = 0;
        }

        bool valid() const
        {
            return Render::RT::rxValid(m_runtime);
        }

        Render::RT::RuntimeHandle runtime() const
        {
            return m_runtime;
        }

        /// 主仓（下标 0）。多仓下的「某个块在哪个仓」请用 `storeForBlock`；
        /// 这个访问器只用于拿主仓做诊断。
        Render::RT::GeometryStoreHandle store() const
        {
            return m_stores.empty() ? Render::RT::GeometryStoreHandle::Invalid : m_stores.front();
        }

        /// 当前仓数（1 表示未发生分片）
        uint32_t storeCount() const
        {
            return static_cast<uint32_t>(m_stores.size());
        }

        Render::RT::DrawListHandle drawList() const
        {
            return m_drawList;
        }

        /// 块所在仓的句柄。查不到即默认主仓（见文件头的归属说明）
        Render::RT::GeometryStoreHandle storeForBlock(uint64_t blockId) const
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
            return index < m_stores.size() ? m_stores[index] : Render::RT::GeometryStoreHandle::Invalid;
        }

        /**
         * @brief 全部仓的统计聚合
         *
         * 容量/已用/空洞都跨仓求和 —— 分片之后「还剩多少空间」是各仓之和，
         * 只看主仓会得出「已满」的错误结论。`blockCount` 等计数字段同样是求和。
         */
        Render::RT::GeometryStoreStats totalStats() const
        {
            Render::RT::GeometryStoreStats sum{};
            for (const Render::RT::GeometryStoreHandle store : m_stores)
            {
                Render::RT::GeometryStoreStats one{};
                if (Render::RT::rxGeometryStoreGetStats(m_runtime, store, &one) != Render::RT::RxResult::Ok)
                {
                    continue;
                }
                sum.capacityBytes += one.capacityBytes;
                sum.usedBytes += one.usedBytes;
                // 最大连续空洞不是可加的，取最大者（它表达「单次最大可分配」）
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

        // ---------- 槽位 ----------

        /// 取一个空闲槽号（复用优先，其次递增）
        uint32_t acquireSlot()
        {
            if (!m_freeSlots.empty())
            {
                const uint32_t slot = m_freeSlots.back();
                m_freeSlots.pop_back();
                return slot;
            }
            return m_nextSlot++;
        }

        /// 归还槽号。重复归还会在空闲栈里留下重复项，调用方须保证幂等自身成立
        void releaseSlot(uint32_t slot)
        {
            m_freeSlots.push_back(slot);
        }

        /// 摘掉某个槽位上的绘制命令，但保留槽号归属（例如临时隐藏某条命令）
        void removeCommand(uint32_t slot)
        {
            if (valid())
            {
                Render::RT::rxDrawListRemove(m_runtime, m_drawList, slot);
            }
        }

        /// 摘掉某个槽位上的绘制命令并归还槽号
        void removeSlot(uint32_t slot)
        {
            removeCommand(slot);
            releaseSlot(slot);
        }

        /// 清空整个绘制列表（不解引用几何块）
        void clearDrawList()
        {
            if (valid())
            {
                Render::RT::rxDrawListClear(m_runtime, m_drawList);
            }
        }

        /**
         * @brief 归还全部槽号，从 0 起重发
         *
         * 只在「列表已清空、所有槽位都空出来」时调用（见 `clearDrawList`）；
         * 否则会把还挂在列表上的槽位重复分配给别的段。
         */
        void resetSlots()
        {
            m_freeSlots.clear();
            m_nextSlot = 0;
        }

        // ---------- 几何块 ----------

        /**
         * @brief 分配一块几何。仓满时自动开新仓（分片）
         *
         * `ErrorGeometryStoreGrown` 视为成功：分配成功，只是该仓的底层缓冲被替换了
         * （公共句柄数值不变，见 `RxRuntimeTests.GeometryAllocReportsGrowthAndKeepsBufferHandleStable`）。
         * `ErrorOutOfMemory` 表示该仓已达单仓上限，这里会开一个新仓再分配一次。
         *
         * 返回的 `out.buffer` 是该块所在仓的缓冲句柄，**可以长期持有**——仓扩容不会
         * 让它失效（DLL 原地改写句柄槽位）。因此调用方填 `DrawCommand::vertexBuffer`
         * 时直接用 `out.buffer` 即可，不要再去取"当前缓冲"（多仓下没有这种东西）。
         */
        bool allocBlock(uint64_t bytes, Render::RT::GeometryBlock& out)
        {
            if (!valid())
            {
                return false;
            }

            size_t index = m_stores.size() - 1;
            Render::RT::RxResult result = Render::RT::rxGeometryAlloc(m_runtime, m_stores[index], bytes, &out);

            if (result == Render::RT::RxResult::ErrorOutOfMemory)
            {
                // 活动仓已到单仓上限：开新仓继续。分片的理由与安全性见文件头。
                if (!addStore())
                {
                    return false;
                }
                index = m_stores.size() - 1;
                result = Render::RT::rxGeometryAlloc(m_runtime, m_stores[index], bytes, &out);
            }

            if (result != Render::RT::RxResult::Ok && result != Render::RT::RxResult::ErrorGeometryStoreGrown)
            {
                return false;
            }

            // 只登记分片块：仓 0 的块靠「查不到即默认 0」兜住，常态下这张表是空的
            if (index != 0)
            {
                m_blockStore[out.id] = static_cast<uint32_t>(index);
            }
            return true;
        }

        /// 写入块内数据。只标脏区间，不立即上传
        bool writeBlock(uint64_t blockId, uint32_t byteOffset, uint32_t sizeBytes, const void* data)
        {
            if (!valid())
            {
                return false;
            }
            return Render::RT::rxGeometryWrite(m_runtime, storeForBlock(blockId), blockId, byteOffset, sizeBytes, data) ==
                Render::RT::RxResult::Ok;
        }

        /// 释放一块。空闲表会与相邻空洞合并，避免碎片累积
        void freeBlock(uint64_t blockId)
        {
            if (!valid() || blockId == 0)
            {
                return;
            }
            Render::RT::rxGeometryFree(m_runtime, storeForBlock(blockId), blockId);
            if (!m_blockStore.empty())
            {
                m_blockStore.erase(blockId);
            }
        }

        /// 主动把累积的脏区间刷到 GPU。帧内提交时 Session 会自己刷
        void flush()
        {
            if (!valid())
            {
                return;
            }
            for (const Render::RT::GeometryStoreHandle store : m_stores)
            {
                Render::RT::rxGeometryFlush(m_runtime, store);
            }
        }

        /**
         * @brief 预留**总**容量：确保各仓容量之和 >= requiredBytes
         *
         * 顺序：先顶满活动仓（翻倍策略一次到位），不够再加仓并把新仓也顶起来，
         * 直到够或达到 `maxStores`。缺口跨仓时不保证一次到位——那要求把每个新仓的
         * maxBytes 都真实分配一遍，代价远大于收益；但可以保证的是**只要额度够，
         * 这里就会把仓铺到位**，不会让后续批量写入反复触发加仓与搬迁。
         *
         * @return 实际总容量（可能 > requiredBytes，因为翻倍策略）
         */
        uint64_t reserveCapacity(uint64_t requiredBytes)
        {
            if (!valid())
            {
                return 0;
            }

            uint64_t total = totalStats().capacityBytes;
            // 先把活动仓顶起来：它最多到单仓上限
            total = driveGrow(m_stores.size() - 1, requiredBytes > total ? requiredBytes - total : 0);

            // 仍不足则加仓，每个新仓同样顶起来
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

    private:
        /**
         * @brief 在指定仓上驱动一次翻倍扩容：临时分配再立即释放
         *
         * `grow()` 内部按翻倍策略扩容，一次调用就能到位，因此这里不必反复试。
         *
         * @return 全部仓的容量之和（不是单个仓的）
         */
        uint64_t driveGrow(size_t index, uint64_t bytes)
        {
            if (bytes > 0 && index < m_stores.size())
            {
                Render::RT::GeometryBlock tmp{};
                const Render::RT::RxResult result = Render::RT::rxGeometryAlloc(m_runtime, m_stores[index], bytes, &tmp);
                if (result == Render::RT::RxResult::Ok || result == Render::RT::RxResult::ErrorGeometryStoreGrown)
                {
                    Render::RT::rxGeometryFree(m_runtime, m_stores[index], tmp.id);
                }
            }
            return totalStats().capacityBytes;
        }

        /// 追加一个新仓（分片）。失败返回 false，调用方按分配失败处理
        bool addStore()
        {
            if (m_stores.size() >= m_maxStores)
            {
                SY_ERRORF("[GeomStore] 已达分片上限 %u 个仓（总容量 %llu bytes），"
                          "无法再分配——检查是否出现了不释放的几何增长",
                    m_maxStores,
                    static_cast<unsigned long long>(totalStats().capacityBytes));
                return false;
            }

            const Render::RT::GeometryStoreHandle store = Render::RT::rxGeometryStoreCreate(m_runtime, &m_storeDesc);
            if (!Render::RT::rxValid(store))
            {
                SY_ERRORF("[GeomStore] 第 %u 个仓创建失败（单仓上限 %llu bytes）",
                    static_cast<uint32_t>(m_stores.size() + 1),
                    static_cast<unsigned long long>(m_storeDesc.maxBytes));
                return false;
            }

            m_stores.push_back(store);
            // 分片是「超过单仓上限」才发生的事，因此每次都要留一条——它解释了
            // 为什么后续的合批会变弱（跨仓的块不能合并）。
            SY_INFOF("[GeomStore] 单仓达上限，已开第 %u 个仓（分片生效）；"
                     "总容量 %llu bytes，此后合批只在仓内发生",
                static_cast<uint32_t>(m_stores.size()),
                static_cast<unsigned long long>(totalStats().capacityBytes));
            return true;
        }

        Render::RT::RuntimeHandle m_runtime{ Render::RT::RuntimeHandle::Invalid };
        /// 几何仓。下标 0 是主仓，超过单仓上限后按需追加（分片）。
        std::vector<Render::RT::GeometryStoreHandle> m_stores;
        /**
         * 分片块的「块 id → 仓下标」。
         *
         * **只登记下标 ≠ 0 的块**：查不到即视为主仓。于是单仓（常态）下这张表
         * 始终为空，alloc/write/free 只多一次 `empty()` 判断，零额外开销；
         * 只有真的跨过单仓上限之后，分片出去的那部分块才进表。
         */
        std::unordered_map<uint64_t, uint32_t> m_blockStore;
        /// 新仓的创建参数（与主仓同配置）
        Render::RT::GeometryStoreDesc m_storeDesc{};
        uint32_t m_maxStores = 8;
        Render::RT::DrawListHandle m_drawList{ Render::RT::DrawListHandle::Invalid };

        std::vector<uint32_t> m_freeSlots;
        uint32_t m_nextSlot = 0;
    };
}  // namespace RenderBridge
