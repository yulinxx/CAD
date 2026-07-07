#include "MinimalOpenGLBackend.h"

#include "RenderFrame.h"
#include "RenderTypes.h"

#include <QDebug>
#include <chrono>

// ============================================================================
// 构造/析构
// ============================================================================

MinimalOpenGLBackend::MinimalOpenGLBackend()
{
    m_context.backendName = QStringLiteral("OpenGL");
    m_context.sceneType = QStringLiteral("2D");
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
    Q_UNUSED(nativeWindowHandle);

    if (m_initialized)
        return true;

    if (!ensureGLContext())
    {
        qWarning() << "[MinimalOpenGLBackend] 无法创建 OpenGL 上下文";
        return false;
    }

    m_initialized = true;
    m_ready = true;
    m_context.markDirty();

    qDebug() << "[MinimalOpenGLBackend] 初始化完成, 后端:" << m_context.backendName;
    return true;
}

void MinimalOpenGLBackend::shutdown()
{
    m_fbo.reset();
    m_offscreenSurface.reset();
    m_glContext.reset();
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

void MinimalOpenGLBackend::setScene(EntityDocument2D* document)
{
    m_document2D = document;
    m_scene = nullptr;
    m_document3D = nullptr;
    m_context.sceneType = QStringLiteral("2D");
    m_context.markDirty();
}

void MinimalOpenGLBackend::setScene(Eg::SceneManager* scene)
{
    m_scene = scene;
    m_document2D = nullptr;
    m_document3D = nullptr;
    m_context.sceneType = QStringLiteral("2D");
    m_context.markDirty();
}

void MinimalOpenGLBackend::setScene(SceneDocument3D* document)
{
    m_document3D = document;
    m_document2D = nullptr;
    m_context.sceneType = QStringLiteral("3D");
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

    if (!m_glContext->makeCurrent(m_offscreenSurface.get()))
        return;

    m_fbo->bind();

    auto* gl = m_glContext->functions();
    if (!gl)
    {
        m_fbo->release();
        m_glContext->doneCurrent();
        return;
    }

    // 清屏
    gl->glClearColor(0.15f, 0.15f, 0.15f, 1.0f);
    gl->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // 设置视口
    gl->glViewport(0, 0, m_context.viewportSize.width(), m_context.viewportSize.height());

    // 清屏完成（后续在此处接入实际渲染）
    m_fbo->release();
    m_glContext->doneCurrent();
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

void MinimalOpenGLBackend::resize(const QSize& size)
{
    m_context.viewportSize = size;
    m_context.markDirty();

    // 重建 FBO
    m_fbo.reset();
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

QImage MinimalOpenGLBackend::captureFrame() const
{
    if (!m_fbo || !m_fbo->isValid())
        return {};

    return m_fbo->toImage();
}

RenderStatistics MinimalOpenGLBackend::getStatistics() const
{
    return m_stats;
}

// ============================================================================
// 后端信息
// ============================================================================

QString MinimalOpenGLBackend::backendName() const
{
    return QStringLiteral("OpenGL");
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
    if (m_glContext && m_glContext->isValid())
        return true;

    // 创建 OpenGL 4.5 Core Profile 上下文
    QSurfaceFormat format;
    format.setVersion(4, 5);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24);
    format.setSamples(4);

    m_glContext = std::make_unique<QOpenGLContext>();
    m_glContext->setFormat(format);

    if (!m_glContext->create())
    {
        qWarning() << "[MinimalOpenGLBackend] 创建 OpenGL 4.5 Core 上下文失败，尝试兼容格式";

        QSurfaceFormat fallbackFormat;
        fallbackFormat.setDepthBufferSize(24);
        m_glContext->setFormat(fallbackFormat);

        if (!m_glContext->create())
        {
            qWarning() << "[MinimalOpenGLBackend] 回退格式也失败";
            return false;
        }
    }

    // 创建离屏表面
    m_offscreenSurface = std::make_unique<QOffscreenSurface>();
    m_offscreenSurface->setFormat(m_glContext->format());
    m_offscreenSurface->create();

    if (!m_glContext->makeCurrent(m_offscreenSurface.get()))
    {
        qWarning() << "[MinimalOpenGLBackend] makeCurrent 失败";
        return false;
    }

    qDebug() << "[MinimalOpenGLBackend] OpenGL 上下文创建成功";
    qDebug() << "  Version:" << m_glContext->format().majorVersion() << "." << m_glContext->format().minorVersion();
    qDebug() << "  Profile:" << (m_glContext->format().profile() == QSurfaceFormat::CoreProfile ? "Core" : "Compatibility");

    m_glContext->doneCurrent();
    return true;
}

bool MinimalOpenGLBackend::ensureFBO()
{
    const QSize size = m_context.viewportSize;
    if (!size.isValid() || size.width() <= 0 || size.height() <= 0)
        return false;

    if (m_fbo && m_fbo->size() == size && m_fbo->isValid())
        return true;

    if (!m_glContext || !m_glContext->makeCurrent(m_offscreenSurface.get()))
        return false;

    QOpenGLFramebufferObjectFormat fboFormat;
    fboFormat.setAttachment(QOpenGLFramebufferObject::CombinedDepthStencil);
    fboFormat.setSamples(4);

    m_fbo = std::make_unique<QOpenGLFramebufferObject>(size, fboFormat);

    if (!m_fbo->isValid())
    {
        qWarning() << "[MinimalOpenGLBackend] FBO 创建失败";
        m_glContext->doneCurrent();
        return false;
    }

    m_glContext->doneCurrent();
    return true;
}

void MinimalOpenGLBackend::renderTestGrid()
{
    // 当前阶段：最小骨架，清屏后不绘制额外内容
    // 后续接入 VBO + Shader 进行实际图元渲染
}