#pragma once
/**
 * @file RenderUploadQueue.h
 * @brief 渲染上传命令队列：把「拼命令」与「操作 GPU」分开
 *
 * 背景（见 `Docs/Todo.md` 第 14 节）：当前上传直接发生在场景刷新与增量编辑里，
 * 靠 `makeCurrent()` 把 GL 上下文切过来。这在 QOpenGLWidget 下能跑，但会造成
 * 渲染线程无法独立、后期 Vulkan/Metal 无法照搬、后台算法线程不能安全提交、
 * 资源释放时容易出现上下文生命周期问题。
 *
 * 队列把这条链路拆成两段：
 *
 * ```text
 * 生产者（任意线程）：只拼命令 + 交出一份 CPU 顶点数据，不碰 GPU
 * 消费者（渲染帧内）：flush() 时统一写进 GeometryStore / DrawList
 * ```
 *
 * ## 当前状态：只有命令模型与消费契约，尚未接入视口
 *
 * 真正的收益要等「渲染线程独立」才兑现。现在 2D/3D 视口整个活在 Qt 主线程里，
 * 接进去只是把一次同步上传拆成「入队 + 同一线程里立刻 drain」，白增一层间接。
 * 因此这里先落地命令模型、线程边界与消费契约，等 P2 的多后端 / 渲染线程落地后，
 * 再把 `Render::RenderSceneBuilder` 与 `UI3D::Mesh3DBuilder` 切过来。
 *
 * 明确不做的事：不在这里维护线程、不持有 GL 上下文、不自己建几何仓、不认槽号。
 * 那些属于调用方（builder 持有仓与台账），塞进来就又是一个假抽象。
 */

#include "render/renderx.h"

#include <cstdint>
#include <mutex>
#include <utility>
#include <vector>

namespace RenderBridge
{
    /// 一次上传要动的东西
    enum class RenderUploadKind : uint8_t
    {
        /// 新增或整体替换某个图元的顶点流
        UpsertGeometry,
        /// 摘掉某个图元的全部段（槽位与几何块回收）
        RemoveGeometry,
        /// 只换材质，不动顶点。仅 3D 有效（2D 颜色在顶点里）
        UpdateMaterial,
    };

    /**
     * @brief 一份待上传的顶点流
     *
     * `bytes` 是 CPU 侧的唯一副本：生产者填完就交出所有权，消费者 flush 时读出。
     * 不做共享指针 + 常量的写法，是因为顶点数据在入队后就没人再读，
     * 拷贝一次比维护一份共享生命周期更便宜也更不容易错。
     */
    struct RenderUploadGeometry
    {
        /// 交织顶点字节流，按 `format` 解释
        std::vector<uint8_t> bytes;
        Render::RT::PrimitiveTopology topology = Render::RT::PrimitiveTopology::Lines;
        Render::RT::VertexFormat format = Render::RT::VertexFormat::P3C3;
        uint32_t vertexCount = 0;
        /// 2D 世界矩形 AABB（x/y 有效）
        float aabb2D[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        uint8_t aabb2DValid = 0;
        /// 3D 世界空间 AABB，配六平面视锥剔除
        Render::RT::RxAabb3 bounds3D{};
        uint8_t bounds3DValid = 0;
    };

    /// 队列里的一条命令
    struct RenderUploadCommand
    {
        RenderUploadKind kind = RenderUploadKind::UpsertGeometry;
        uint64_t entityId = 0;
        /// 几何内容；`RemoveGeometry` / `UpdateMaterial` 时留空
        RenderUploadGeometry geometry;
        /// `UpdateMaterial` 用：材质号，0 表示不改
        uint16_t material = 0;
        /// 绘制命令的层号与层内序号，由生产者按场景顺序决定
        uint8_t layer = 100;
        uint16_t sequence = 0;
    };

    /**
     * @brief 生产者 / 消费者之间的命令队列
     *
     * 线程边界：`enqueue` 可以被任意线程调用；`flush` 只能在持有 GL 上下文的
     * 渲染帧内调用。两者之间用队列自身的锁交接，消费者在锁外执行 ——
     * 持锁写几何仓会把生产者挡在门外，而队列存在的意义恰恰是让生产者不必等。
     */
    class RenderUploadQueue
    {
    public:
        RenderUploadQueue() = default;

        RenderUploadQueue(const RenderUploadQueue&) = delete;
        RenderUploadQueue& operator=(const RenderUploadQueue&) = delete;

        void enqueue(RenderUploadCommand command)
        {
            std::lock_guard<std::mutex> guard(m_mutex);
            m_commands.push_back(std::move(command));
        }

        size_t pendingCount() const
        {
            std::lock_guard<std::mutex> guard(m_mutex);
            return m_commands.size();
        }

        /// 丢弃全部未消费的命令。视口销毁、或整场景要重来时用
        void clear()
        {
            std::lock_guard<std::mutex> guard(m_mutex);
            m_commands.clear();
        }

        /**
         * @brief 在渲染帧内把队列交给消费者执行，执行完队列为空
         *
         * `consumer` 是一个接受 `const RenderUploadCommand&` 的可调用对象，
         * 通常是持有几何仓与段位台账的 builder。这里刻意用模板而不是抽象接口：
         * 队列不需要知道消费者是谁，也不该为它定义一个只有一种实现的虚函数。
         */
        template <typename Consumer>
        void flush(const Consumer& consumer)
        {
            std::vector<RenderUploadCommand> batch;
            {
                std::lock_guard<std::mutex> guard(m_mutex);
                batch.swap(m_commands);
            }
            // 锁外执行：消费者要写几何仓、发绘制命令，可能很慢
            for (const RenderUploadCommand& command : batch)
            {
                consumer(command);
            }
        }

    private:
        mutable std::mutex m_mutex;
        std::vector<RenderUploadCommand> m_commands;
    };
}  // namespace RenderBridge
