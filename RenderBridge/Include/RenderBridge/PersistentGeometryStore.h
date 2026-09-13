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
 * 做成纯头文件：它只是把 Renderx 的 C 接口收成一组不抛异常的小包装，
 * 没有跨模块状态，内联掉可以少一层 DLL 边界。
 */

#include "render/renderx.h"

#include <cstdint>
#include <vector>

namespace RenderBridge
{
    class PersistentGeometryStore
    {
    public:
        /// 创建参数：只暴露两个 builder 真正不同的部分
        struct Config
        {
            /// 几何仓初始容量（字节）。0 表示用 DLL 默认值
            uint64_t storeInitialBytes = 8ull * 1024 * 1024;
            /// 几何仓增长上限（字节）。0 表示不限
            uint64_t storeMaxBytes = 512ull * 1024 * 1024;
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
        ~PersistentGeometryStore() { shutdown(); }

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

            const Render::RT::GeometryStoreHandle store =
                Render::RT::rxGeometryStoreCreate(runtime, &storeDesc);
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
            m_store = store;
            m_drawList = list;
            return true;
        }

        /**
         * @brief 销毁几何仓与绘制列表；幂等
         *
         * 先拆绘制列表：它引用几何仓里的块，反向不引用。块与槽位都随这两个对象
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
            if (Render::RT::rxValid(m_store))
            {
                Render::RT::rxGeometryStoreDestroy(m_runtime, m_store);
                m_store = Render::RT::GeometryStoreHandle::Invalid;
            }
            m_runtime = Render::RT::RuntimeHandle::Invalid;

            m_freeSlots.clear();
            m_nextSlot = 0;
        }

        bool valid() const { return Render::RT::rxValid(m_runtime); }
        Render::RT::RuntimeHandle runtime() const { return m_runtime; }
        Render::RT::GeometryStoreHandle store() const { return m_store; }
        Render::RT::DrawListHandle drawList() const { return m_drawList; }

        /**
         * @brief 取仓当前的底层缓冲句柄
         *
         * ⚠️ 仓扩容时底层缓冲会被替换，此时所有已发出的 `GeometryBlock::buffer`
         * 都会失效。因此**每次都重新取**，不要缓存上一次的结果。
         */
        Render::RT::BufferHandle currentBuffer() const
        {
            if (!valid())
            {
                return Render::RT::BufferHandle::Invalid;
            }
            return Render::RT::rxGeometryStoreGetBuffer(m_runtime, m_store);
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
        void releaseSlot(uint32_t slot) { m_freeSlots.push_back(slot); }

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
         * @brief 在仓内分配一块几何
         *
         * `ErrorGeometryStoreGrown` 视为成功：分配成功，只是底层缓冲被替换了，
         * 调用方之后取缓冲要走 `currentBuffer()`。
         */
        bool allocBlock(uint64_t bytes, Render::RT::GeometryBlock& out)
        {
            if (!valid())
            {
                return false;
            }
            const Render::RT::RxResult result =
                Render::RT::rxGeometryAlloc(m_runtime, m_store, bytes, &out);
            return result == Render::RT::RxResult::Ok
                || result == Render::RT::RxResult::ErrorGeometryStoreGrown;
        }

        /// 写入块内数据。只标脏区间，不立即上传
        bool writeBlock(uint64_t blockId, uint32_t byteOffset, uint32_t sizeBytes, const void* data)
        {
            if (!valid())
            {
                return false;
            }
            return Render::RT::rxGeometryWrite(m_runtime, m_store, blockId, byteOffset, sizeBytes, data)
                == Render::RT::RxResult::Ok;
        }

        /// 释放一块。空闲表会与相邻空洞合并，避免碎片累积
        void freeBlock(uint64_t blockId)
        {
            if (valid() && blockId != 0)
            {
                Render::RT::rxGeometryFree(m_runtime, m_store, blockId);
            }
        }

        /// 主动把累积的脏区间刷到 GPU。帧内提交时 Session 会自己刷
        void flush()
        {
            if (valid())
            {
                Render::RT::rxGeometryFlush(m_runtime, m_store);
            }
        }

    private:
        Render::RT::RuntimeHandle m_runtime{ Render::RT::RuntimeHandle::Invalid };
        Render::RT::GeometryStoreHandle m_store{ Render::RT::GeometryStoreHandle::Invalid };
        Render::RT::DrawListHandle m_drawList{ Render::RT::DrawListHandle::Invalid };

        std::vector<uint32_t> m_freeSlots;
        uint32_t m_nextSlot = 0;
    };
}  // namespace RenderBridge
