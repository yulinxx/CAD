#include "MinimalOpenGLBackend.h"

#include "Log/SyLogger.h"
#include "RenderFrame.h"
#include "RenderTypes.h"

#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QOpenGLFunctions>
#include <QOffscreenSurface>
#include <QSurfaceFormat>
#include <QImage>

#include <chrono>
#include <string>

struct MinimalOpenGLBackend::GLResources
{
    std::unique_ptr<QOpenGLContext> glContext;
    std::unique_ptr<QOffscreenSurface> offscreenSurface;
    std::unique_ptr<QOpenGLFramebufferObject> fbo;
};

// ============================================================================
// 构造/析构
// ============================================================================

MinimalOpenGLBackend::MinimalOpenGLBackend()
{
    m_context.backendName = "OpenGL";
    m_context.sceneType = "2D";
    m_context.renderMode = RenderMode::Wireframe;
}

MinimalOpenGLBackend::~MinimalOpenGLBackend()
{
    shutdown();
}

// ============================================================================
// 生命周期
// ============================================================================

bool MinimalOpenGLBackend::initialize(void* nativeWindowHandle)
{
    (void)nativeWindowHandle;

    if (m_initialized)
        return true;

    if (!ensureGLContext())
    {
        SY_WARN("[MinimalOpenGLBackend] Failed to create OpenGL context");
        return false;
    }

    m_initialized = true;
    m_ready = true;
    m_context.markDirty();

    SY_INFOF("[MinimalOpenGLBackend] Initialized, backend: %s", m_context.backendName.c_str());
    return true;
}

void MinimalOpenGLBackend::shutdown()
{
    if (m_glResources)
    {
        m_glResources->fbo.reset();
        m_glResources->offscreenSurface.reset();
        m_glResources->glContext.reset();
    }
    m_glResources.reset();
    m_ready = false;
    m_initialized = false;
}

bool MinimalOpenGLBackend::isReady() const
{
    return m_ready && m_initialized;
}

// ============================================================================
// 上下文绑定
// ============================================================================

void MinimalOpenGLBackend::bindContext(const RenderContext& context)
{
    m_context = context;
    m_context.markDirty();
}

const RenderContext& MinimalOpenGLBackend::context() const
{
    return m_context;
}

// ============================================================================
// 场景绑定
// ============================================================================

void MinimalOpenGLBackend::setScene(Eg::SceneManager* scene)
{
    m_scene = scene;
    m_context.sceneType = "2D";
    m_context.markDirty();
}

void MinimalOpenGLBackend::setCamera(CameraController3D* controller)
{
    m_camera = controller;
    m_context.markDirty();
}

// ============================================================================
// 渲染管线
// ============================================================================

void MinimalOpenGLBackend::compile()
{
    m_stats.compileTimeMs = 0.0f;
}

void MinimalOpenGLBackend::submitFrame(const RenderFrame& frame)
{
    m_lastFrame = frame;
    m_stats.batchCount = frame.batchCount();
    m_stats.totalVertexCount = frame.totalVertexCount();
    m_stats.entityCount = frame.entityCount();
    m_stats.frameTimeMs = 0.0f;
}

void MinimalOpenGLBackend::render()
{
    if (!m_ready || !ensureGLContext())
        return;

    if (!ensureFBO())
        return;

    if (!m_glResources->glContext->makeCurrent(m_glResources->offscreenSurface.get()))
        return;

    m_glResources->fbo->bind();

    auto* gl = m_glResources->glContext->functions();
    if (!gl)
    {
        m_glResources->fbo->release();
        m_glResources->glContext->doneCurrent();
        return;
    }

    gl->glClearColor(0.15f, 0.15f, 0.15f, 1.0f);
    gl->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    gl->glViewport(0, 0, m_context.viewportSize.width, m_context.viewportSize.height);

    m_glResources->fbo->release();
    m_glResources->glContext->doneCurrent();
}

void MinimalOpenGLBackend::beginFrame()
{
    m_context.advanceFrame();
    m_stats.frameId = m_context.frameId;
    m_stats.timestamp = std::chrono::steady_clock::now();
}

void MinimalOpenGLBackend::endFrame()
{
    m_context.clearDirty();
}

// ============================================================================
// 视口控制
// ============================================================================

void MinimalOpenGLBackend::resize(const Size2D& size)
{
    m_context.viewportSize = size;
    m_context.markDirty();

    if (m_glResources)
        m_glResources->fbo.reset();
}

void MinimalOpenGLBackend::resetView()
{
    m_context.markDirty();
}

// ============================================================================
// 模式切换
// ============================================================================

void MinimalOpenGLBackend::setOrbitMode(bool enabled)
{
    m_context.orbitMode = enabled;
}

void MinimalOpenGLBackend::setMeasureMode(bool enabled)
{
    m_context.measureMode = enabled;
}

void MinimalOpenGLBackend::setRenderMode(RenderMode mode)
{
    m_context.renderMode = mode;
    m_context.markDirty();
}

RenderMode MinimalOpenGLBackend::renderMode() const
{
    return m_context.renderMode;
}

// ============================================================================
// 帧输出
// ============================================================================

ImageBuffer MinimalOpenGLBackend::captureFrame() const
{
    ImageBuffer result;

    if (!m_glResources || !m_glResources->fbo || !m_glResources->fbo->isValid())
        return result;

    QImage qImage = m_glResources->fbo->toImage();
    result.width = qImage.width();
    result.height = qImage.height();
    result.channels = 4;
    result.data.resize(result.width * result.height * 4);

    for (int y = 0; y < result.height; ++y)
    {
        const QRgb* src = reinterpret_cast<const QRgb*>(qImage.scanLine(y));
        uint8_t* dst = result.data.data() + y * result.width * 4;
        for (int x = 0; x < result.width; ++x)
        {
            dst[0] = qRed(src[x]);
            dst[1] = qGreen(src[x]);
            dst[2] = qBlue(src[x]);
            dst[3] = qAlpha(src[x]);
            dst += 4;
        }
    }

    return result;
}

RenderStatistics MinimalOpenGLBackend::getStatistics() const
{
    return m_stats;
}

// ============================================================================
// 后端信息
// ============================================================================

std::string MinimalOpenGLBackend::backendName() const
{
    return "OpenGL";
}

bool MinimalOpenGLBackend::supportsCapability(BackendCapability cap) const
{
    return hasCapability(m_capabilities, cap);
}

BackendCapability MinimalOpenGLBackend::capabilities() const
{
    return m_capabilities;
}

// ============================================================================
// 私有方法
// ============================================================================

bool MinimalOpenGLBackend::ensureGLContext()
{
    if (!m_glResources)
        m_glResources = std::make_unique<GLResources>();

    if (m_glResources->glContext && m_glResources->glContext->isValid())
        return true;

    QSurfaceFormat format;
    format.setVersion(4, 5);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24);
    format.setSamples(4);

    m_glResources->glContext = std::make_unique<QOpenGLContext>();
    m_glResources->glContext->setFormat(format);

    if (!m_glResources->glContext->create())
    {
        SY_WARN("[MinimalOpenGLBackend] Failed to create OpenGL 4.5 Core context, trying compatibility format");

        QSurfaceFormat fallbackFormat;
        fallbackFormat.setDepthBufferSize(24);
        m_glResources->glContext->setFormat(fallbackFormat);

        if (!m_glResources->glContext->create())
        {
            SY_WARN("[MinimalOpenGLBackend] Fallback OpenGL format also failed");
            return false;
        }
    }

    m_glResources->offscreenSurface = std::make_unique<QOffscreenSurface>();
    m_glResources->offscreenSurface->setFormat(m_glResources->glContext->format());
    m_glResources->offscreenSurface->create();

    if (!m_glResources->glContext->makeCurrent(m_glResources->offscreenSurface.get()))
    {
        SY_WARN("[MinimalOpenGLBackend] makeCurrent failed");
        return false;
    }

    SY_INFOF("[MinimalOpenGLBackend] OpenGL context created: version %d.%d, profile %s",
        m_glResources->glContext->format().majorVersion(),
        m_glResources->glContext->format().minorVersion(),
        m_glResources->glContext->format().profile() == QSurfaceFormat::CoreProfile ? "Core" : "Compatibility");

    m_glResources->glContext->doneCurrent();
    return true;
}

bool MinimalOpenGLBackend::ensureFBO()
{
    if (!m_glResources)
        return false;

    const Size2D& size = m_context.viewportSize;
    if (!size.isValid())
        return false;

    if (m_glResources->fbo && m_glResources->fbo->size() == QSize(size.width, size.height) && m_glResources->fbo->isValid())
        return true;

    if (!m_glResources->glContext || !m_glResources->glContext->makeCurrent(m_glResources->offscreenSurface.get()))
        return false;

    QOpenGLFramebufferObjectFormat fboFormat;
    fboFormat.setAttachment(QOpenGLFramebufferObject::CombinedDepthStencil);
    fboFormat.setSamples(4);

    m_glResources->fbo = std::make_unique<QOpenGLFramebufferObject>(QSize(size.width, size.height), fboFormat);

    if (!m_glResources->fbo->isValid())
    {
        SY_WARN("[MinimalOpenGLBackend] FBO creation failed");
        m_glResources->glContext->doneCurrent();
        return false;
    }

    m_glResources->glContext->doneCurrent();
    return true;
}

void MinimalOpenGLBackend::renderTestGrid()
{
}
