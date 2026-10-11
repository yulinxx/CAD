#pragma once
/**
 * @file RenderSessionHost.h
 * @brief 宿主侧 RenderX runtime / surface / session 的生命周期归口
 *
 * 2D 视口与 3D 视口原本各写一遍完全同构的代码：填 RuntimeDesc、填
 * SurfaceDesc、按 runtime → surface → session 的顺序创建、失败时原路回收、
 * 销毁时逆序拆。两边实际不同的部分都做成参数：
 *
 *   - applicationName（日志里区分是哪个视口）
 *   - enableDepth    （3D 靠深度缓冲遮挡，2D 靠排序键，是唯一的 Surface 差异）
 *   - clearColor     （2D 是浅灰纸面，3D 是深色背景）
 *   - backend        （OpenGL / Metal，见下）
 *   - nativeWindow   （Metal 需要的 NSView*，OpenGL 不使用）
 *
 * 后端的窗口契约完全不同，是这里唯一的分叉：
 *   - OpenGL：surface 走 ForeignGlContext——上下文由 Qt 的 QOpenGLWidget 拥有，
 *     DLL 只记录当前帧缓冲，因此不传任何窗口句柄，并需要 glGetProcAddress
 *     由宿主提供（Qt 的符号解析，见 qtGlGetProcAddress 注释）。
 *   - Metal：CAMetalLayer 必须挂在宿主自建的 layer-hosting NSView 上
 *     （Renderx 的 metalDevice.mm 会建一个独立子视图，不去改 Qt 的视图层级），
 *     因此必须传 NSView*；GL 符号解析器对 Metal 无意义，留空。
 *
 * 不放进 UICommon：UICommon 的定位是「UI 通用件」，不应该为了这个再去
 * 公共依赖 RenderX；RenderBridge 才是宿主与渲染 DLL 之间的那一层。
 *
 * Metal 下的 Runtime 是**进程级共享**的：设备、内建管线预热、字形图集、
 * GeometryStore、材质表与瞬态环都挂在 Runtime 上，而产品同一时刻只有一个
 * 中央视口 —— 每个视口各建一套的代价是「每次 2D↔3D 切换都把这一整套重做
 * 一遍」。因此 Metal 的 Runtime 由进程内一份持有者持有（实现见 .cpp 的
 * SharedMetalRuntime），每个视口只建自己的 Surface 与 Session。
 *
 * 它的生命周期是**应用级**：首个视口建立，此后一直保留，直到应用退出
 * （`shutdownSharedRuntime()`）**且最后一个视口已拆除**时才销毁。
 *
 * 为什么不是单纯的应用退出点销毁：实测发现 **Qt 的视口销毁是延迟的** —— 从 3D
 * 工作台退出时 `Workbench3D::deactivate()` 已跑完（MainWindow3D 已析构、服务已
 * 释放），但 `RenderWidget3D` 仍活着，其析构被 Qt 的对象树推到更晚。若此刻就销毁
 * 共享 Runtime，Renderx 会明确报错
 * `Runtime destroyed with N sessions still alive (host lifecycle error)`。
 * 因此 `shutdownSharedRuntime()` 只**请求**销毁，真正的销毁落在最后一次
 * `release()`（视口析构）之后——那里 Surface/Session 必然已经拆干净。
 *
 * 也不走纯引用计数：实测工作台切换是「先销毁旧视口、再创建新视口」，两者生命周期
 * 不重叠，纯计数必然 1→0→1，等于每次切换都重建。
 *
 * GL 侧不做共享：GL 资源属于上下文，两个视口的 QOpenGLWidget 不保证共享
 * 上下文，行为保持与之前完全一致。
 *
 * **代价**：瞬态环常驻整个应用会话（环总量 = transientBufferBytes × 2），
 * 换来的是工作台切换不重建设备与内建管线。
 *
 * ## 类型隔离说明
 *
 * 本文件头不再 include renderx.h，所有渲染相关类型统一来自
 * RenderAbstraction/IRenderTypes.h。handle 用 uint64_t 裸值传递，
 * renderx.h 的 include 只保留在 RenderSessionHost.cpp 内部。
 */

#include "RenderBridge/RenderBridgeAPI.h"
#include "RenderAbstraction/IRenderTypes.h"

#include <cstdint>
#include <memory>

// 只前向声明：头文件不必引 IRenderFactory.h，成员 unique_ptr 的完整性由
// 本类的析构函数（在 .cpp 内定义）保证
namespace RenderAbstraction
{
    class IRenderDevice;
    class IRenderSurface;
    class IRenderScene;
}

namespace RenderBridge
{
    class RENDERBRIDGE_API RenderSessionHost
    {
    public:
        /**
         * Metal 共享 Runtime 的瞬态环预算（即 Config::transientBufferBytes 的
         * 取值）。两个视口必须声明同一个值。
         *
         * 为什么必须同源：该参数是 **Runtime 级**的，共享后只有「首次建立」
         * 那一次生效——后建者报一个更大的值也拿不到扩容，只会让
         * SharedMetalRuntime 留一条 WARN。收口之前这里就是分叉的（2D 用默认
         * 64MB、3D 用 128MB），一旦共享就会变成「谁先建谁说了算」。
         *
         * **单位的坑（先看这里再看数字）**：`transientBufferBytes` 是**单段**容量，
         * 环的缓冲总量是它的 **2 倍**（TransientRing::kSegmentCount = 2）。所以
         * 32MB 预算 = 单段 32MB + 常驻缓冲 64MB；告警阈值是单段的 75%，
         * 即 **24MB**。可用显存实测反证：128MB 预算时 `gpuMem` 比 64MB 预算时
         * 高出的那部分正好是 2×128MB。
         *
         * **为什么是 32MB（从 64MB 下调）**：64MB 预算下导入
         * `13451_Golden_Crown_v1_L2.obj` 并选中实体操作，全程未触发 75% 告警
         * → 单帧用量 < 48MB、且从未超过 64MB。空闲启动并不触碰瞬态环，下调
         * 只影响「重载帧是否走溢出缓冲」；两条安全网仍然兜底：
         *
         * **安全网**：`Session::endFrame` 在用量越过单段 75%（24MB）时明确告警；
         * 真正超出单段容量时 TransientRing 会为该次分配另开缓冲（帧末释放），
         * **不会画错，只是慢一点**。所以最坏情况是「一条告警 + 轻微回退」，
         * 真吃到告警把它抬回去即可。
         *
         * 取 32MB 而不是 64MB 的收益：常驻缓冲从 128MB 降到 64MB。
         */
        static constexpr uint64_t kSharedRuntimeTransientBytes = 32ull * 1024 * 1024;

        /// 创建参数：只暴露两个视口真正不同的部分，其余走同一套默认
        struct Config
        {
            const char* applicationName = "CAD Viewport";
            uint32_t width = 0;
            uint32_t height = 0;
            bool enableDepth = false;
            uint64_t transientBufferBytes = 32ull * 1024 * 1024;
            float clearColor[4]{ 0.94f, 0.94f, 0.94f, 1.0f };
            /// 渲染后端。默认 OpenGL，保持既有调用方零改动。
            RenderAbstraction::RenderBackend backend = RenderAbstraction::RenderBackend::OpenGL;
            /**
             * 原生窗口句柄。
             *
             * Metal 下必须是 NSView*（转成 void* 以免公共头文件引入 ObjC）；
             * 该视图只需在 initialize 期间存活，DLL 会往它上面挂一个自建的
             * 宿主子视图，之后不再回访它。OpenGL 忽略此字段（上下文由 Qt 拥有）。
             */
            void* nativeWindow = nullptr;
        };

        RenderSessionHost() = default;
        ~RenderSessionHost();

        RenderSessionHost(const RenderSessionHost&) = delete;
        RenderSessionHost& operator=(const RenderSessionHost&) = delete;

        /**
         * @brief 按 runtime → surface → session 的顺序创建
         *
         * 后端按 Config::backend 分叉（见文件头注释）：
         *   - OpenGL：调用前 GL 上下文必须已当前（surface 走 ForeignGlContext，
         *     需记录当前帧缓冲）。
         *   - Metal：必须提供 Config::nativeWindow（NSView*），且该视图已挂进
         *     窗口层级（DLL 会在其上挂宿主子视图；未挂窗口时不会报错，但
         *     交换链要等到视图真正上屏后才有有效尺寸）。
         * 任一步失败都会回收已建好的部分并返回 false，不会留下半个运行时。
         */
        bool initialize(const Config& config);

        /// 逆序销毁 session → surface → runtime；幂等，可重复调用
        void shutdown();

        bool isReady() const;

        // ---------- 渲染接口视图 ----------
        //
        // 由本类持有的句柄包出来，视口用它调用渲染接口，不必知道后端的名字。
        // 对象**不拥有**句柄，销毁仍由本类负责（见 initialize / shutdown 的顺序）。
        // isReady() 为假时三个访问器都返回 nullptr。

        RenderAbstraction::IRenderDevice* device() const { return m_deviceObj.get(); }
        RenderAbstraction::IRenderSurface* surface() const { return m_surfaceObj.get(); }
        RenderAbstraction::IRenderScene* scene() const { return m_sceneObj.get(); }

        /// 把后端能力打一条日志；tag 是调用方前缀（两个视口前缀不同）
        void logCapabilities(const char* tag) const;

        /**
         * @brief 请求销毁进程级共享的 Metal Runtime（应用退出时调用一次）
         *
         * **只置标志，不一定当场销毁**：真正的销毁要等最后一个视口销毁（即最后一次
         * `release()`）。原因是 Qt 的视口析构是延迟的——实测从 3D 工作台退出时，
         * `Workbench3D::deactivate()` 已跑完但 `RenderWidget3D` 仍活着，若此刻就
         * 销毁 Runtime，它的 Surface/Session 还在，Renderx 会报
         * `Runtime destroyed with N sessions still alive (host lifecycle error)`。
         *
         * 调用时机：`AppBootstrapper::shutdown()` 里工作台释放之后。若调用时已经没有
         * 活跃视口，则立即销毁。
         *
         * 幂等。GL 侧无共享 Runtime，调用它是空操作。
         */
        static void shutdownSharedRuntime();

    private:
        /// 无效句柄哨兵，与 renderx.h 的 enum class : uint64_t Invalid 值一致
        static constexpr uint64_t kInvalidHandle = 0;

        uint64_t m_runtime{ kInvalidHandle };
        uint64_t m_surface{ kInvalidHandle };
        uint64_t m_session{ kInvalidHandle };

        /**
         * 同一批句柄的抽象层视图（见 device() / surface() / scene()）。
         *
         * 生命周期：在 initialize 末尾建立、shutdown 开头释放 —— 它们只是句柄的
         * 包装，必须比句柄先消失。
         *
         * 上面存裸值、这里存对象，不是过渡态：本头文件不能 include renderx.h，
         * 句柄只能以 uint64_t 存放；对象是给调用方的视图，对外不暴露裸值。
         */
        std::unique_ptr<RenderAbstraction::IRenderDevice> m_deviceObj;
        std::unique_ptr<RenderAbstraction::IRenderSurface> m_surfaceObj;
        std::unique_ptr<RenderAbstraction::IRenderScene> m_sceneObj;
        /**
         * 本实例的 Runtime 是否来自进程级共享持有者。
         *
         * 决定 shutdown() 是「还一份引用」还是「直接销毁」——写错会导致
         * 共享 Runtime 被提前销毁（其它视口的 Surface 全部失效）或泄漏。
         */
        bool m_runtimeShared = false;
    };
}  // namespace RenderBridge
