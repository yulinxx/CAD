#include "RenderBackendFactory.h"

#include "IRenderBackend.h"
#include "RenderContext.h"
#include "RenderFrame.h"

#include <QDebug>

// ============================================================================
// 占位后端实现（实现完整 IRenderBackend 接口）
// ============================================================================

namespace
{
    class PlaceholderBackend final : public IRenderBackend
    {
    public:
        explicit PlaceholderBackend(QString name, BackendCapability caps)
            : m_name(std::move(name))
            , m_capabilities(caps)
        {
            m_context.backendName = m_name;
        }

        // ============ 生命周期 ============

        bool initialize(void* nativeWindowHandle = nullptr) override
        {
            Q_UNUSED(nativeWindowHandle);
            m_ready = true;
            m_context.markDirty();
            return true;
        }

        void shutdown() override
        {
            m_ready = false;
        }

        bool isReady() const override
        {
            return m_ready;
        }

        // ============ 上下文绑定 ============

        void bindContext(const RenderContext& context) override
        {
            m_context = context;
            m_context.markDirty();
        }

        const RenderContext& context() const override
        {
            return m_context;
        }

        // ============ 场景绑定 ============

        void setScene(EntityDocument2D* document) override
        {
            m_document2D = document;
            m_context.markDirty(DirtyRegionType::Geometry);
        }

        void setScene(SceneDocument3D* document) override
        {
            m_document3D = document;
            m_context.markDirty(DirtyRegionType::Geometry);
        }

        void setCamera(CameraController3D* controller) override
        {
            m_camera = controller;
            m_context.markDirty(DirtyRegionType::View);
        }

        // ============ 渲染管线 ============

        void compile() override
        {
            m_stats.compileTimeMs = 0.0f;
        }

        void submitFrame(const RenderFrame& frame) override
        {
            m_lastFrame = frame;
            m_stats.batchCount = frame.batchCount();
            m_stats.totalVertexCount = frame.totalVertexCount();
            m_stats.entityCount = frame.entityCount();
        }

        void render() override
        {
            // 占位后端不做实际渲染
        }

        void beginFrame() override
        {
            m_context.advanceFrame();
            m_stats.frameId = m_context.frameId;
            m_stats.timestamp = std::chrono::steady_clock::now();
        }

        void endFrame() override
        {
            m_context.clearDirty();
        }

        // ============ 视口控制 ============

        void resize(const QSize& size) override
        {
            m_context.viewportSize = size;
            m_context.markDirty(DirtyRegionType::View);
        }

        void resetView() override
        {
            m_context.markDirty(DirtyRegionType::View);
        }

        // ============ 模式切换 ============

        void setOrbitMode(bool enabled) override
        {
            m_context.orbitMode = enabled;
        }

        void setMeasureMode(bool enabled) override
        {
            m_context.measureMode = enabled;
        }

        void setRenderMode(RenderMode mode) override
        {
            m_context.renderMode = mode;
            m_context.markDirty();
        }

        RenderMode renderMode() const override
        {
            return m_context.renderMode;
        }

        // ============ 帧输出 ============

        QImage captureFrame() const override
        {
            // 占位后端返回空图像
            return QImage(m_context.viewportSize, QImage::Format_ARGB32);
        }

        RenderStatistics getStatistics() const override
        {
            return m_stats;
        }

        // ============ 后端信息 ============

        QString backendName() const override
        {
            return m_name;
        }

        bool supportsCapability(BackendCapability cap) const override
        {
            return hasCapability(m_capabilities, cap);
        }

        BackendCapability capabilities() const override
        {
            return m_capabilities;
        }

    private:
        QString m_name;
        BackendCapability m_capabilities{ BackendCapability::None };
        bool m_ready{ false };

        RenderContext m_context;
        RenderStatistics m_stats;

        EntityDocument2D* m_document2D{ nullptr };
        SceneDocument3D* m_document3D{ nullptr };
        CameraController3D* m_camera{ nullptr };
        RenderFrame m_lastFrame;
    };
}

// ============================================================================
// 工厂方法
// ============================================================================

std::unique_ptr<IRenderBackend> RenderBackendFactory::create(BackendType type)
{
    switch (type)
    {
    case BackendType::OpenGL:
        return std::make_unique<PlaceholderBackend>(
            QStringLiteral("OpenGL"),
            BackendCapability::HardwareAccelerated
            | BackendCapability::AntiAliasing
            | BackendCapability::HighDPI
            | BackendCapability::OffscreenRendering);

    case BackendType::Vulkan:
        return std::make_unique<PlaceholderBackend>(
            QStringLiteral("Vulkan"),
            BackendCapability::HardwareAccelerated
            | BackendCapability::MultiViewport
            | BackendCapability::InstancedRendering
            | BackendCapability::ComputeShader
            | BackendCapability::AntiAliasing
            | BackendCapability::HighDPI
            | BackendCapability::OffscreenRendering);

    case BackendType::Metal:
        return std::make_unique<PlaceholderBackend>(
            QStringLiteral("Metal"),
            BackendCapability::HardwareAccelerated
            | BackendCapability::MultiViewport
            | BackendCapability::InstancedRendering
            | BackendCapability::ComputeShader
            | BackendCapability::AntiAliasing
            | BackendCapability::HighDPI);

    case BackendType::Software:
        return std::make_unique<PlaceholderBackend>(
            QStringLiteral("Software"),
            BackendCapability::OffscreenRendering);

    default:
        return std::make_unique<PlaceholderBackend>(
            QStringLiteral("Unknown"),
            BackendCapability::None);
    }
}

QVector<RenderBackendFactory::BackendType> RenderBackendFactory::availableBackends()
{
    QVector<BackendType> backends;
    backends.append(BackendType::OpenGL);

#ifdef _WIN32
    // Vulkan 在 Windows 上可用
    // backends.append(BackendType::Vulkan);
#endif

#ifdef __APPLE__
    backends.append(BackendType::Metal);
#endif

    backends.append(BackendType::Software);
    return backends;
}

QString RenderBackendFactory::backendTypeName(BackendType type)
{
    switch (type)
    {
    case BackendType::OpenGL:   return QStringLiteral("OpenGL");
    case BackendType::Vulkan:   return QStringLiteral("Vulkan");
    case BackendType::Metal:    return QStringLiteral("Metal");
    case BackendType::Software: return QStringLiteral("Software");
    default:                    return QStringLiteral("Unknown");
    }
}

BackendCapability RenderBackendFactory::capabilitiesFor(BackendType type)
{
    switch (type)
    {
    case BackendType::OpenGL:
        return BackendCapability::HardwareAccelerated
             | BackendCapability::AntiAliasing
             | BackendCapability::HighDPI
             | BackendCapability::OffscreenRendering;

    case BackendType::Vulkan:
        return BackendCapability::HardwareAccelerated
             | BackendCapability::MultiViewport
             | BackendCapability::InstancedRendering
             | BackendCapability::ComputeShader
             | BackendCapability::AntiAliasing
             | BackendCapability::HighDPI
             | BackendCapability::OffscreenRendering;

    case BackendType::Metal:
        return BackendCapability::HardwareAccelerated
             | BackendCapability::MultiViewport
             | BackendCapability::InstancedRendering
             | BackendCapability::ComputeShader
             | BackendCapability::AntiAliasing
             | BackendCapability::HighDPI;

    case BackendType::Software:
        return BackendCapability::OffscreenRendering;

    default:
        return BackendCapability::None;
    }
}

RenderBackendFactory::BackendType RenderBackendFactory::defaultBackendType()
{
#ifdef __APPLE__
    return BackendType::Metal;
#else
    return BackendType::OpenGL;
#endif
}