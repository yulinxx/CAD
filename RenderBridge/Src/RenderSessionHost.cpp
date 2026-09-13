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

        Render::RT::RuntimeDesc rd{};
        rd.abiVersion = RENDERX_ABI_VERSION;
        rd.backend = Render::RT::Backend::OpenGL;
        rd.enableValidation = 0;
        rd.transientBufferBytes = config.transientBufferBytes;
        rd.logCallback = &Render::host::rxLogBridge;
        rd.logUserData = nullptr;
        rd.applicationName = config.applicationName;
        // 交给 DLL 用 Qt 的符号解析，而不是平台默认路径（见 qtGlGetProcAddress 注释）
        rd.glGetProcAddress = reinterpret_cast<void*>(&Render::host::qtGlGetProcAddress);
        m_runtime = Render::RT::rxRuntimeCreate(&rd);
        if (!Render::RT::rxValid(m_runtime))
        {
            return false;
        }

        // ForeignGlContext：上下文由 Qt 拥有，DLL 只记录当前帧缓冲。
        // QOpenGLWidget 画到自己的 FBO，所以这里不能传任何窗口句柄。
        Render::RT::SurfaceDesc sd{};
        sd.windowKind = Render::RT::NativeWindowKind::ForeignGlContext;
        sd.presentMode = Render::RT::PresentMode::Fifo;
        sd.handleA = nullptr;
        sd.handleB = nullptr;
        sd.width = config.width;
        sd.height = config.height;
        sd.enableDepth = config.enableDepth ? 1 : 0;
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
