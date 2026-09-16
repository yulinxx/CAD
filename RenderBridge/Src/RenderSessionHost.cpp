#include "RenderBridge/RenderSessionHost.h"

#include "RenderBridge/HostCallbacks.h"
#include "Log/SyLogger.h"

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

        const bool metal = config.backend == Render::RT::Backend::Metal;
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
        rd.backend = config.backend;
        rd.enableValidation = 0;
        rd.transientBufferBytes = config.transientBufferBytes;
        rd.logCallback = &Render::host::rxLogBridge;
        rd.logUserData = nullptr;
        rd.applicationName = config.applicationName;
        if (!metal)
        {
            // 交给 DLL 用 Qt 的符号解析，而不是平台默认路径（见 qtGlGetProcAddress 注释）。
            // Metal 不解析 GL 符号：留空。
            rd.glGetProcAddress = reinterpret_cast<void*>(&Render::host::qtGlGetProcAddress);
        }
        m_runtime = Render::RT::rxRuntimeCreate(&rd);
        if (!Render::RT::rxValid(m_runtime))
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
        m_surface = Render::RT::rxSurfaceCreate(m_runtime, &sd);
        if (!Render::RT::rxValid(m_surface))
        {
            shutdown();
            return false;
        }

        Render::RT::SessionDesc ses{};
        ses.runtime = m_runtime;
        ses.surface = m_surface;
        for (int i = 0; i < 4; ++i)
        {
            ses.clearColor[i] = config.clearColor[i];
        }
        m_session = Render::RT::rxSessionCreate(&ses);
        if (!Render::RT::rxValid(m_session))
        {
            shutdown();
            return false;
        }
        return true;
    }

    void RenderSessionHost::shutdown()
    {
        // 逆序：会话持有表面与运行时内部对象，表面持有交换链
        if (Render::RT::rxValid(m_session))
        {
            Render::RT::rxSessionDestroy(m_session);
            m_session = Render::RT::SessionHandle::Invalid;
        }
        if (Render::RT::rxValid(m_surface))
        {
            Render::RT::rxSurfaceDestroy(m_runtime, m_surface);
            m_surface = Render::RT::SurfaceHandle::Invalid;
        }
        if (Render::RT::rxValid(m_runtime))
        {
            Render::RT::rxRuntimeDestroy(m_runtime);
            m_runtime = Render::RT::RuntimeHandle::Invalid;
        }
    }

    bool RenderSessionHost::isReady() const
    {
        return Render::RT::rxValid(m_runtime) && Render::RT::rxValid(m_surface)
            && Render::RT::rxValid(m_session);
    }

    void RenderSessionHost::logCapabilities(const char* tag) const
    {
        Render::RT::Capabilities caps{};
        if (Render::RT::rxRuntimeGetCapabilities(m_runtime, &caps) != Render::RT::RxResult::Ok)
        {
            return;
        }
        SY_INFOF("%s: backend=%s device=%s maxLineWidth=%.1f",
            tag ? tag : "RenderSessionHost",
            Render::RT::rxBackendName(caps.backend), caps.deviceName, caps.maxLineWidth);
    }
}  // namespace RenderBridge
