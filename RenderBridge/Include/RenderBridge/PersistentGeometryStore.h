#pragma once
/**
 * @file PersistentGeometryStore.h
 * @brief 常驻几何仓 + 保留式绘制列表的宿主侧底座（头文件：仅声明）
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
 * ## 类型隔离说明
 *
 * 本文件头**不 include renderx.h**。所有 Render::RT 类型通过前向声明引入，
 * 方法实现放在 PersistentGeometryStore.cpp（那里 include renderx.h）。
 * 调用方需自行 include renderx.h（UI 层已这么做）。
 */

#include "RenderBridge/RenderBridgeAPI.h"

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
    /**
     * @brief 几何块描述
     *
     * 对应 RenderX 的 `Render::RT::GeometryBlock`，但句柄用 uint64_t 裸值，
     * 头文件因此不必 include renderx.h。与 RenderX 类型之间的转换在
     * PersistentGeometryStore.cpp 里逐字段做。
     */
    struct GeometryBlock
    {
        /// 块所在仓的缓冲句柄裸值（Render::RT::BufferHandle）
        uint64_t buffer = 0;
        /// 块标识。释放与写入都用它，不要用 offset 当身份（扩容/整理后会变）
        uint64_t id = 0;
        uint32_t offset = 0;
        uint32_t sizeBytes = 0;
    };

    static_assert(sizeof(GeometryBlock) == 24, "GeometryBlock 布局变了，检查与 RenderX 的字段对应");

    /**
     * @brief 几何仓统计
     *
     * 对应 RenderX 的 `Render::RT::GeometryStoreStats`，纯 POD，无句柄字段。
     */
    struct GeometryStoreStats
    {
        uint64_t capacityBytes = 0;
        /// 已分配给块的字节数（含粒度对齐产生的内部浪费）
        uint64_t usedBytes = 0;
        /// 空闲表中最大连续空洞，用于判断是否需要整理
        uint64_t largestFreeBytes = 0;
        uint32_t blockCount = 0;
        uint32_t freeRangeCount = 0;
        /// 本帧因写入而排队的脏字节数（合并后）
        uint64_t dirtyBytesThisFrame = 0;
        /// 累计扩容次数。频繁扩容说明 initialBytes 给小了。
        uint32_t growCount = 0;
        uint32_t _pad0 = 0;
    };

    static_assert(sizeof(GeometryStoreStats) == 48, "GeometryStoreStats 布局变了，检查与 RenderX 的字段对应");

    class RENDERBRIDGE_API PersistentGeometryStore
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
         */
        static uint64_t recommendedMaxBytes();

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

        ~PersistentGeometryStore();

        PersistentGeometryStore(const PersistentGeometryStore&) = delete;
        PersistentGeometryStore& operator=(const PersistentGeometryStore&) = delete;

        /**
         * @brief 在给定 Runtime 上创建几何仓与绘制列表
         *
         * 重复调用会先释放旧的。任一步失败都会回收已建好的部分并返回 false，
         * 不留半个资源。
         *
         * @param runtime DLL Runtime 句柄裸值
         */
        bool initialize(uint64_t runtime, const Config& config);

        /**
         * @brief 销毁全部几何仓与绘制列表；幂等
         *
         * 先拆绘制列表：它引用几何仓里的块，反向不引用。块与槽位都随这些对象
         * 一起消失，不必逐个释放——逐个释放只是白跑一遍空闲链表合并。
         */
        void shutdown();

        bool valid() const;

        uint64_t runtimeValue() const;
        uint64_t storeValue() const;
        uint64_t drawListValue() const;
        uint64_t storeForBlock(uint64_t blockId) const;
        uint32_t storeCount() const;

        /**
         * @brief 全部仓的统计聚合
         *
         * 容量/已用/空洞都跨仓求和 —— 分片之后「还剩多少空间」是各仓之和，
         * 只看主仓会得出「已满」的错误结论。`blockCount` 等计数字段同样是求和。
         */
        GeometryStoreStats totalStats() const;

        // ---------- 槽位 ----------

        /// 取一个空闲槽号（复用优先，其次递增）
        uint32_t acquireSlot();

        /// 归还槽号。重复归还会在空闲栈里留下重复项，调用方须保证幂等自身成立
        void releaseSlot(uint32_t slot);

        /// 摘掉某个槽位上的绘制命令，但保留槽号归属（例如临时隐藏某条命令）
        void removeCommand(uint32_t slot);

        /// 摘掉某个槽位上的绘制命令并归还槽号
        void removeSlot(uint32_t slot);

        /// 清空整个绘制列表（不解引用几何块）
        void clearDrawList();

        /**
         * @brief 归还全部槽号，从 0 起重发
         *
         * 只在「列表已清空、所有槽位都空出来」时调用（见 `clearDrawList`）；
         * 否则会把还挂在列表上的槽位重复分配给别的段。
         */
        void resetSlots();

        // ---------- 几何块 ----------

        /**
         * @brief 分配一块几何。仓满时自动开新仓（分片）
         *
         * 返回的 `out.buffer` 是该块所在仓的缓冲句柄，**可以长期持有**——仓扩容不会
         * 让它失效（DLL 原地改写句柄槽位）。因此调用方填 `DrawCommand::vertexBuffer`
         * 时直接用 `out.buffer` 即可，不要再去取"当前缓冲"（多仓下没有这种东西）。
         */
        bool allocBlock(uint64_t bytes, GeometryBlock& out);

        /// 写入块内数据。只标脏区间，不立即上传
        bool writeBlock(uint64_t blockId, uint32_t byteOffset, uint32_t sizeBytes, const void* data);

        /// 释放一块。空闲表会与相邻空洞合并，避免碎片累积
        void freeBlock(uint64_t blockId);

        /// 主动把累积的脏区间刷到 GPU。帧内提交时 Session 会自己刷
        void flush();

        /**
         * @brief 预留**总**容量：确保各仓容量之和 >= requiredBytes
         *
         * @return 实际总容量（可能 > requiredBytes，因为翻倍策略）
         */
        uint64_t reserveCapacity(uint64_t requiredBytes);

    private:
        bool addStore();

        uint64_t driveGrow(size_t index, uint64_t bytes);

        /// 无效句柄哨兵，与 renderx.h 的 enum class : uint64_t Invalid 值一致
        static constexpr uint64_t kInvalidHandle = 0;

        uint64_t m_runtime{ kInvalidHandle };
        std::vector<uint64_t> m_stores;  // GeometryStoreHandle 裸值，下标 0 是主仓
        std::unordered_map<uint64_t, uint32_t> m_blockStore;  // 块id → 仓下标（只登记分片块）
        uint64_t m_storeDescBytes = 0;     // storeMaxBytes 原始值
        uint64_t m_storeInitialBytes = 0;
        uint32_t m_storeGranularity = 0;
        uint32_t m_maxStores = 8;
        uint64_t m_drawListRaw{ kInvalidHandle };  // DrawListHandle 裸值

        std::vector<uint32_t> m_freeSlots;
        uint32_t m_nextSlot = 0;
    };
}  // namespace RenderBridge
