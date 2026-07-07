#include "RenderCoreRenderer.h"

#include "DefaultSceneCompiler.h"
#include "RenderFrame.h"
#include "RenderTypes.h"
#include "SoftwareRenderer.h"

#include "../UI/UiEntities.h"

#include <QPainter>

// ============================================================================
// 构造/析构
// ============================================================================

RenderCoreRenderer::RenderCoreRenderer()
{
    m_compiler = std::make_unique<DefaultSceneCompiler>();
    m_softwareRenderer = std::make_unique<SoftwareRenderer>();
    m_context.backendName = QStringLiteral("RenderCore");
    m_context.sceneType = QStringLiteral("3D");
    m_context.renderMode = RenderMode::Wireframe;
}

RenderCoreRenderer::~RenderCoreRenderer()
{
    shutdown();
}

// ============================================================================
// 生命周期（桥接层核心职责）
// ============================================================================

bool RenderCoreRenderer::initialize(void* windowHandle)
{
    Q_UNUSED(windowHandle);
    m_ready = true;
    emitStatus(QStringLiteral("RenderCore 渲染器就绪"));
    return true;
}

void RenderCoreRenderer::shutdown()
{
    m_ready = false;
}

bool RenderCoreRenderer::isReady() const
{
    return m_ready;
}

void RenderCoreRenderer::setRenderLoopEnabled(bool enabled)
{
    m_renderLoopEnabled = enabled;
}

bool RenderCoreRenderer::isRenderLoopRunning() const
{
    return m_renderLoopEnabled;
}

bool RenderCoreRenderer::isOpenGL() const
{
    return false;
}

// ============================================================================
// 场景与相机
// ============================================================================

void RenderCoreRenderer::setScene(SceneDocument3D* document)
{
    m_document = document;
    m_context.markDirty();
    m_compiler->invalidateCache();
}

void RenderCoreRenderer::setCamera(CameraController3D* controller)
{
    m_cameraController = controller;
}

// ============================================================================
// 场景编译（纯桥接，编译策略由 SceneCompiler 内部决定）
// ============================================================================

RenderFrame RenderCoreRenderer::compileScene()
{
    if (!m_document)
        return {};

    m_context.advanceFrame();

    RenderFrame frame;
    if (m_context.isDirty || !m_compiler->hasCachedFrame())
    {
        frame = m_compiler->compile(m_document, m_context);
        m_context.clearDirty();
    }
    else
    {
        frame = m_compiler->compileIncremental(m_document, m_context, m_lastFrame);
    }

    m_lastFrame = frame;
    return frame;
}

// ============================================================================
// 渲染派发（委托 SoftwareRenderer，不承担渲染实现）
// ============================================================================

void RenderCoreRenderer::render(QPainter& painter, int width, int height)
{
    if (!m_ready)
        return;

    m_camera.setViewportSize(width, height);
    m_context.viewportSize = QSize(width, height);

    if (m_camera.isDirty())
    {
        m_context.markDirty();
        m_camera.clearDirty();
    }

    RenderFrame frame = compileScene();
    m_softwareRenderer->render(painter, frame, m_camera, m_context.viewportSize);
}

// ============================================================================
// 视口控制
// ============================================================================

void RenderCoreRenderer::resize(int width, int height)
{
    m_camera.setViewportSize(width, height);
    m_context.viewportSize = QSize(width, height);
    m_context.markDirty();
}

void RenderCoreRenderer::resetView()
{
    m_camera.reset();
    m_context.markDirty();
}

// ============================================================================
// 模式
// ============================================================================

void RenderCoreRenderer::setOrbitMode(bool enabled)
{
    m_camera.setOrbitMode(enabled);
}

void RenderCoreRenderer::setMeasureMode(bool enabled)
{
    m_camera.setMeasureMode(enabled);
}

bool RenderCoreRenderer::isOrbitMode() const
{
    return m_camera.isOrbitMode();
}

// ============================================================================
// 输入事件转发（完全委托给 Camera3D）
// ============================================================================

void RenderCoreRenderer::onMousePress(int x, int y, int button, int modifiers,
                                       int viewW, int viewH)
{
    m_camera.onMousePress(x, y, button, modifiers, viewW, viewH);

    if (m_camera.isRotating())
        emitStatus(QStringLiteral("旋转"));
    else if (m_camera.isPanning())
        emitStatus(QStringLiteral("平移"));
}

void RenderCoreRenderer::onMouseMove(int x, int y, int buttons, int viewW, int viewH)
{
    if (m_camera.onMouseMove(x, y, buttons, viewW, viewH))
        m_context.markDirty();
}

void RenderCoreRenderer::onMouseRelease(int x, int y, int button, int viewW, int viewH)
{
    m_camera.onMouseRelease(x, y, button, viewW, viewH);
    emitStatus(m_camera.isMeasureMode() ? QStringLiteral("测量模式") : QStringLiteral("就绪"));
}

void RenderCoreRenderer::onWheel(int delta, int viewW, int viewH)
{
    if (m_camera.onWheel(delta, viewW, viewH))
        m_context.markDirty();
}

// ============================================================================
// 选择状态管理
// ============================================================================

void RenderCoreRenderer::selectNodeById(const QString& nodeId)
{
    m_selectedNodeId = nodeId;
    m_selectedPathNames.clear();
    if (m_selectionCallback)
        m_selectionCallback(nodeId);
}

QString RenderCoreRenderer::selectedNodeId() const
{
    return m_selectedNodeId;
}

QStringList RenderCoreRenderer::selectedPathNames() const
{
    return m_selectedPathNames;
}

// ============================================================================
// 回调管理
// ============================================================================

void RenderCoreRenderer::setStatusCallback(StatusCallback callback)
{
    m_statusCallback = std::move(callback);
}

void RenderCoreRenderer::setSelectionCallback(SelectionCallback callback)
{
    m_selectionCallback = std::move(callback);
}

void RenderCoreRenderer::setPathCallback(PathCallback callback)
{
    m_pathCallback = std::move(callback);
}

void RenderCoreRenderer::emitStatus(const QString& text)
{
    if (m_statusCallback)
        m_statusCallback(text);
}