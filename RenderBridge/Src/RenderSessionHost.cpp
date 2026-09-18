#include "RenderBridge/RenderSessionHost.h"

// 持有 unique_ptr<IRenderDevice/IRenderSurface/IRenderScene>，删指针处需要完整类型
#include "RenderAbstraction/IRenderFactory.h"

// 句柄由本类自己建，这里只是把它们包成抽象层对象（私有实现头，不导出）
#include "RenderXAdapter/RenderXAdapterInternal.h"

// renderx.h 只在 .cpp 内部可见，头文件不再直接暴露它
#include "render/renderx.h"

#include "Log/SyLogger.h"

#include <QOpenGLContext>

namespace
{
    /**
     * @brief 把 DLL 的日志回调桥接到宿主日志库
     *
     * DLL 自身不依赖任何日志实现。参数用 int32_t 兼容任何后端的 LogLevel 枚举值
     * （Debug=0, Info=1, Warn=2, Error=3）。
     */
    void rxLogBridge(int32_t level, const char* message, void* /*userData*/)
    {
        if (!message)
        {
            return;
        }
        switch (static_cast<RenderAbstraction::LogLevel>(level))
        {
        case RenderAbstraction::LogLevel::Debug:
        case RenderAbstraction::LogLevel::Info:
            SY_INFOF("[Renderx] %s", message);
            break;
        case RenderAbstraction::LogLevel::Warn:
            SY_WARNF("[Renderx] %s", message);
            break;
        case RenderAbstraction::LogLevel::Error:
            SY_ERRORF("[Renderx] %s", message);
            break;
        }
    }

    /**
     * @brief 把 Qt 的符号解析桥接给 DLL 的 GL 后端
     *
     * 不能让 DLL 走平台默认路径（Windows `wglGetProcAddress`、Linux
     * `glXGetProcAddress`、macOS `dlsym`）：Qt 在部分平台上用的是另一套实现
     * （Windows 的 ANGLE / EGL 后端最典型），两套混用会得到「函数指针非空但
     * 行为不对」，没有任何报错，只表现为画面局部异常。
     *
     * 函数指针 → void* 的转换在 C++ 里是实现定义行为，但所有目标平台
     * （Win/macOS/Linux 的 ABI）都保证两者同宽且可往返，GL 的
     * getProcAddress 家族本身就是这么用的。
     */
    void* qtGlGetProcAddress(const char* name)
    {
        QOpenGLContext* ctx = QOpenGLContext::currentContext();
        if (!ctx || !name)
        {
            return nullptr;
        }
        return reinterpret_cast<void*>(ctx->getProcAddress(name));
    }

    // ---------- 类型转换辅助 ----------
    // RenderSessionHost.h 不再依赖 renderx.h，m_runtime/m_surface/m_session 存的是
    // uint64_t 裸值。这里统一做裸值 ↔ RenderX Handle 的转换。

    inline Render::RT::RuntimeHandle toRuntime(uint64_t v)
    {
        return static_cast<Render::RT::RuntimeHandle>(v);
    }
    inline Render::RT::SurfaceHandle toSurface(uint64_t v)
    {
        return static_cast<Render::RT::SurfaceHandle>(v);
    }
    inline Render::RT::SessionHandle toSession(uint64_t v)
    {
        return static_cast<Render::RT::SessionHandle>(v);
    }
    inline bool isValid(uint64_t v)
    {
        return Render::RT::rxValid(toRuntime(v)) != 0;
    }

    /// RA 枚举 → RenderX 枚举
    inline Render::RT::Backend toBackend(RenderAbstraction::RenderBackend b)
    {
        return static_cast<Render::RT::Backend>(b);
    }

    /**
     * @brief 进程级共享的 Metal Runtime
     *
     * **为什么要有它**：Runtime 是「一个 GPU 设备 + 全部共享资源」的粒度——
     * 设备创建、内建管线预热（实测 22 条）、字形图集、GeometryStore、材质表、
     * 管线缓存、瞬态环全挂在它上面。此前每个视口各建一套，于是每次 2D↔3D
     * 工作台切换都要把这一整套重做一遍：实测同一 PID 内出现两次
     * `Runtime ready`（一次 64MB、一次 128MB）与两次
     * `Built-in pipelines ready 22/22`，而产品同一时刻只有一个中央视口，
     * 这些工作纯属重复。
     *
     * **API 层本来就支持**：Renderx 按「1 个 Runtime + N 个 Surface」设计
     * （`Runtime::createSurface` / `destroySurface`；README：「同一 Runtime 可
     * 创建任意多个 Surface，共享全部 GPU 资源……这是多窗口的正确形态」），
     * 多窗口共享也是库内已有的支持路径（`Runtime::sessionsInFrame` 专门处理
     * 共享瞬态环的切段）。因此这一层只把「谁建、谁销毁」从「每个视口」
     * 改成「进程内一份」，**不需要动任何 ABI**。
     *
     * **为什么只对 Metal 这样做**：GL 的资源属于上下文，两个视口的
     * QOpenGLWidget 不保证共享上下文（surface 走 ForeignGlContext，只记录
     * 当前帧缓冲），跨上下文复用同一个 Runtime 等于把「资源在这里创建、
     * 在那边使用」引进来。GL 因此保持每视口一套，行为与之前完全一致。
     *
     * **生命周期是「应用级」，但销毁必须等视口拆完**：这一条被实测推着改了两次。
     *
     * 第一版用引用计数（最后一个视口销毁即销毁 Runtime），实测证伪：工作台切换是
     * 「先销毁旧视口、再创建新视口」，两个生命周期不重叠，计数必然 1→0→1，
     * 等于每次切换都重建。
     *
     * 第二版改成「应用退出时显式销毁」（AppBootstrapper::shutdown 里调
     * shutdownSharedRuntime）。这版又被实测证伪：**Qt 的视口销毁是延迟的**——
     * 从 3D 工作台退出时，`Workbench3D::deactivate()` 已经跑完（MainWindow3D
     * 已析构、服务已释放），但 RenderWidget3D 仍活着，其析构被 Qt 的对象树
     * 推到更晚。于是共享 Runtime 被提前销毁，Renderx 明确报错：
     *
     *   [E] Runtime destroyed with 1 sessions still alive (host lifecycle error)
     *   [E] Runtime destroyed with 1 surfaces still alive (host lifecycle error)
     *
     * 现版把两者合起来：**请求与应用退出绑定，销毁与最后一个视口释放绑定**。
     *   - `requestShutdown()`（应用退出时调用）只置标志；
     *   - `release()`（视口析构时调用）在「已请求 + 计数归零」时才真正销毁。
     * 这样销毁时机天然落在所有 Surface/Session 拆除之后，既不吃切换（那时没请求），
     * 也不踩 Qt 的延迟销毁。
     *
     * 若视口直到进程退出都没被析构（异常路径），则**不销毁**：静态析构只记 WARN。
     * 那种时刻渲染 DLL 可能已卸载，去调 rxRuntimeDestroy 的风险高于让 OS 回收。
     *
     * **代价（明确记录）**：瞬态环常驻整个应用会话。环总量 =
     * transientBufferBytes × 2，当前 64MB 预算即常驻 128MB —— 与收口前
     * 「2D 单开」的占用持平（那时 2D 的环是 64MB×2）。这是「切换零重建」换来的。
     *
     * **线程约定**：视口的创建与销毁、以及本持有者的请求都在 Qt 主线程，不加锁。
     */
    class SharedMetalRuntime
    {
    public:
        static SharedMetalRuntime& instance()
        {
            static SharedMetalRuntime holder;
            return holder;
        }

        ~SharedMetalRuntime()
        {
            if (Render::RT::rxValid(m_runtime))
            {
                // 走到这里说明应用退出了、但仍有视口没被析构（正常路径下
                // 最后一个视口的 release() 已经把 runtime 销毁了）。此时渲染 DLL
                // 可能已卸载，去调 rxRuntimeDestroy 的风险高于让 OS 回收，因此只报。
                SY_WARNF("[SharedRuntime] 进程退出时 runtime 仍存活：有 %u 个视口未被析构，"
                         "未销毁（交操作系统回收）",
                    m_holders);
            }
        }

        /// 取一份引用；首次调用负责按 desc 建立 Runtime
        Render::RT::RuntimeHandle acquire(const Render::RT::RuntimeDesc& desc)
        {
            if (Render::RT::rxValid(m_runtime))
            {
                if (desc.transientBufferBytes != m_transientBytes)
                {
                    // 该参数是 Runtime 级、只有首次建立那一次生效。两个视口必须声明
                    // 同一个值，否则「谁先建谁说了算」。这里留一条 WARN，
                    // 免得将来两边又长出分叉（本次收口之前就是 64MB 与 128MB 分叉）
                    // 而无人察觉。
                    SY_WARNF("[SharedRuntime] transientBufferBytes 请求 %llu 与已建 %llu 不一致，"
                             "沿用已建值（后建者拿不到扩容）",
                        static_cast<unsigned long long>(desc.transientBufferBytes),
                        static_cast<unsigned long long>(m_transientBytes));
                }
                m_holders += 1;
                SY_INFOF("[SharedRuntime] 复用进程级 Metal runtime（当前持有点 %u 个）", m_holders);
                return m_runtime;
            }

            m_runtime = Render::RT::rxRuntimeCreate(&desc);
            if (!Render::RT::rxValid(m_runtime))
            {
                return Render::RT::RuntimeHandle::Invalid;
            }
            m_transientBytes = desc.transientBufferBytes;
            m_holders = 1;
            m_shutdownRequested = false;
            SY_INFOF("[SharedRuntime] 建立进程级 Metal runtime"
                     "（transient=%llu bytes；应用级生命周期，2D/3D 共用，不随视口重建）",
                static_cast<unsigned long long>(m_transientBytes));
            return m_runtime;
        }

        /// 视口析构：减持有计数；若应用已请求退出且这是最后一个视口，此刻才销毁
        void release()
        {
            if (!Render::RT::rxValid(m_runtime))
            {
                return;
            }
            if (m_holders > 0)
            {
                m_holders -= 1;
            }
            if (m_shutdownRequested && m_holders == 0)
            {
                destroy("应用已请求退出，且最后一个视口已拆除");
                return;
            }
            SY_INFOF("[SharedRuntime] 视口释放（当前持有点 %u 个；runtime 保留给下一个视口）", m_holders);
        }

        /// 应用退出：只置标志。真正销毁交给最后一个视口的 release()（见类注释）
        void requestShutdown()
        {
            if (!Render::RT::rxValid(m_runtime))
            {
                return;
            }
            m_shutdownRequested = true;
            if (m_holders == 0)
            {
                destroy("应用已请求退出，且当前无视口");
                return;
            }
            SY_INFOF("[SharedRuntime] 应用请求退出：仍有 %u 个视口未拆除，"
                     "runtime 交给最后一次视口释放时销毁（Qt 的视口析构是延迟的）",
                m_holders);
        }

    private:
        void destroy(const char* reason)
        {
            m_shutdownRequested = false;
            SY_INFOF("[SharedRuntime] 销毁进程级 Metal runtime（%s）", reason ? reason : "?");
            Render::RT::rxRuntimeDestroy(m_runtime);
            m_runtime = Render::RT::RuntimeHandle::Invalid;
            m_transientBytes = 0;
        }

        Render::RT::RuntimeHandle m_runtime{ Render::RT::RuntimeHandle::Invalid };
        /// 已建 Runtime 实际采用的瞬态环预算，用于对后续请求做一致性告警
        uint64_t m_transientBytes = 0;
        /// 当前持有点数量：只用于日志与「是否已拆完」的判断
        uint32_t m_holders = 0;
        /// 应用是否已请求退出（决定 release 到 0 时是销毁还是保留）
        bool m_shutdownRequested = false;
    };
}  // namespace

namespace RenderBridge
{
    RenderSessionHost::~RenderSessionHost()
    {
        shutdown();
    }

    bool RenderSessionHost::initialize(const Config& config)
    {
        // 复用同一个宿主重建时，先把上一次的运行时收干净
        shutdown();

        const auto backend = toBackend(config.backend);
        const bool metal = backend == Render::RT::Backend::Metal;
        if (metal && config.nativeWindow == nullptr)
        {
            // Metal 的交换链必须挂在 NSView 上；没有句柄就无法建 surface，
            // 提前失败比让 DLL 在深处报一句模糊的日志更容易定位。
            SY_ERRORF("RenderSessionHost[%s]: Metal 后端需要 nativeWindow（NSView*）",
                config.applicationName ? config.applicationName : "?");
            return false;
        }

        Render::RT::RuntimeDesc rd{};
        rd.abiVersion = RENDERX_ABI_VERSION;
        rd.backend = backend;
        rd.enableValidation = 0;
        rd.transientBufferBytes = config.transientBufferBytes;
        rd.logCallback = reinterpret_cast<Render::RT::rxLogCallback>(&rxLogBridge);
        rd.logUserData = nullptr;
        rd.applicationName = config.applicationName;
        if (!metal)
        {
            // 交给 DLL 用 Qt 的符号解析，而不是平台默认路径（见 qtGlGetProcAddress 注释）。
            // Metal 不解析 GL 符号：留空。
            rd.glGetProcAddress = reinterpret_cast<void*>(&qtGlGetProcAddress);
        }
        // Metal 的 Runtime 在进程内共享，GL 的每视口一套——理由见 SharedMetalRuntime 注释。
        if (metal)
        {
            m_runtime = static_cast<uint64_t>(SharedMetalRuntime::instance().acquire(rd));
            m_runtimeShared = true;
        }
        else
        {
            m_runtime = static_cast<uint64_t>(Render::RT::rxRuntimeCreate(&rd));
            m_runtimeShared = false;
        }
        if (!isValid(m_runtime))
        {
            return false;
        }

        Render::RT::SurfaceDesc sd{};
        sd.presentMode = Render::RT::PresentMode::Fifo;
        sd.width = config.width;
        sd.height = config.height;
        sd.enableDepth = config.enableDepth ? 1 : 0;
        sd.handleB = nullptr;
        if (metal)
        {
            // CocoaNsView：句柄是 NSView*，DLL 在其上挂一个自建的 layer-hosting
            // 子视图承载 CAMetalLayer（不去改这个视图自身，见 metalDevice.mm）。
            sd.windowKind = Render::RT::NativeWindowKind::CocoaNsView;
            sd.handleA = config.nativeWindow;
        }
        else
        {
            // ForeignGlContext：上下文由 Qt 拥有，DLL 只记录当前帧缓冲。
            // QOpenGLWidget 画到自己的 FBO，所以这里不能传任何窗口句柄。
            sd.windowKind = Render::RT::NativeWindowKind::ForeignGlContext;
            sd.handleA = nullptr;
        }
        m_surface = static_cast<uint64_t>(Render::RT::rxSurfaceCreate(toRuntime(m_runtime), &sd));
        if (!Render::RT::rxValid(toSurface(m_surface)))
        {
            shutdown();
            return false;
        }

        Render::RT::SessionDesc ses{};
        ses.runtime = toRuntime(m_runtime);
        ses.surface = toSurface(m_surface);
        for (int i = 0; i < 4; ++i)
        {
            ses.clearColor[i] = config.clearColor[i];
        }
        m_session = static_cast<uint64_t>(Render::RT::rxSessionCreate(&ses));
        if (!Render::RT::rxValid(toSession(m_session)))
        {
            shutdown();
            return false;
        }

        // 句柄齐了，再包出抽象层视图。包失败不回收句柄：视口仍可用裸值工作，
        // 只是走不了抽象层接口，这比整个渲染器建不起来轻得多。
        m_deviceObj = RenderBridge::wrapRenderXDevice(m_runtime);
        m_surfaceObj = RenderBridge::wrapRenderXSurface(m_runtime, m_surface);
        m_sceneObj = RenderBridge::wrapRenderXScene(m_runtime, m_session);
        if (!m_deviceObj || !m_surfaceObj || !m_sceneObj)
        {
            SY_ERRORF("RenderSessionHost[%s]: 抽象层对象包装失败，视口将无法使用渲染接口",
                config.applicationName ? config.applicationName : "?");
        }
        return true;
    }

    void RenderSessionHost::shutdown()
    {
        // 先放抽象层视图：它们只是句柄的包装，必须比句柄先消失
        m_sceneObj.reset();
        m_surfaceObj.reset();
        m_deviceObj.reset();

        // 逆序：会话持有表面与运行时内部对象，表面持有交换链
        if (Render::RT::rxValid(toSession(m_session)))
        {
            Render::RT::rxSessionDestroy(toSession(m_session));
            m_session = 0;
        }
        if (Render::RT::rxValid(toSurface(m_surface)))
        {
            Render::RT::rxSurfaceDestroy(toRuntime(m_runtime), toSurface(m_surface));
            m_surface = 0;
        }
        if (isValid(m_runtime))
        {
            if (m_runtimeShared)
            {
                // 共享 Runtime 只减持有计数、不销毁：它活到应用退出（见 SharedMetalRuntime
                // 注释——实测工作台切换是先销毁旧视口再建新视口，两个生命周期不重叠，
                // 按引用计数必然 1→0→1，等于每次切换都重建）。
                SharedMetalRuntime::instance().release();
            }
            else
            {
                Render::RT::rxRuntimeDestroy(toRuntime(m_runtime));
            }
            m_runtime = 0;
        }
        m_runtimeShared = false;
    }

    void RenderSessionHost::shutdownSharedRuntime()
    {
        SharedMetalRuntime::instance().requestShutdown();
    }

    bool RenderSessionHost::isReady() const
    {
        return isValid(m_runtime) && Render::RT::rxValid(toSurface(m_surface)) && Render::RT::rxValid(toSession(m_session));
    }

    void RenderSessionHost::logCapabilities(const char* tag) const
    {
        Render::RT::Capabilities caps{};
        if (Render::RT::rxRuntimeGetCapabilities(toRuntime(m_runtime), &caps) != Render::RT::RxResult::Ok)
        {
            return;
        }
        SY_INFOF("%s: backend=%s device=%s maxLineWidth=%.1f",
            tag ? tag : "RenderSessionHost",
            Render::RT::rxBackendName(caps.backend),
            caps.deviceName,
            caps.maxLineWidth);
    }
}  // namespace RenderBridge