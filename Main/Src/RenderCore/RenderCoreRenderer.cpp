#include "RenderCoreRenderer.h"

#include "DefaultSceneCompiler.h"
#include "RenderFrame.h"
#include "RenderTypes.h"
#include "SoftwareRenderer.h"
#include "Log/SyLogger.h"

#include "../UI/UiEntities.h"

#include <QCoreApplication>
#include <QPainter>
#include <stdexcept>

namespace
{
    QString trRenderCore(const char* text)
    {
        return QCoreApplication::translate("RenderCoreRenderer", text);
    }
}

// ============================================================================
// 构造/析构
// ============================================================================

RenderCoreRenderer::RenderCoreRenderer()
{
    SY_DEBUG("[RenderCoreRenderer] constructed");

    try
    {
        m_compiler = std::make_unique<DefaultSceneCompiler>();
        m_softwareRenderer = std::make_unique<SoftwareRenderer>();
        m_context.backendName = "RenderCore";
        m_context.sceneType = "3D";
        m_context.renderMode = RenderMode::Wireframe;

        SY_INFO("[RenderCoreRenderer] initialized with SoftwareRenderer backend");
    }
    catch (const std::exception& e)
    {
        SY_ERRORF("[RenderCoreRenderer] error code=render.construct_failed message=construction failed: %s", e.what());
        throw;
    }
}

RenderCoreRenderer::~RenderCoreRenderer()
{
    SY_DEBUG("[RenderCoreRenderer] destroying");
    shutdown();
}

// ============================================================================
// 生命周期（桥接层核心职责）
// ============================================================================

bool RenderCoreRenderer::initialize(void* windowHandle)
{
    SY_DEBUG("[RenderCoreRenderer] initializing with window handle");

    try
    {
        Q_UNUSED(windowHandle);
        m_ready = true;
        emitStatus(trRenderCore("RenderCore renderer ready"));

        SY_INFO("[RenderCoreRenderer] initialization completed successfully");
        return true;
    }
    catch (const std::exception& e)
    {
        SY_ERRORF("[RenderCoreRenderer] error code=render.init_failed message=initialization failed: %s", e.what());
        m_ready = false;
        return false;
    }
}

void RenderCoreRenderer::shutdown()
{
    SY_DEBUG("[RenderCoreRenderer] shutting down");

    m_ready = false;
    m_document = nullptr;
    m_cameraController = nullptr;

    SY_INFO("[RenderCoreRenderer] shutdown completed");
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
// 场景与相机（纯转发）
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
    {
        SY_WARN("[RenderCoreRenderer] compileScene called with null document");
        return {};
    }

    m_context.advanceFrame();

    RenderFrame frame;
    try
    {
        if (m_context.isDirty || !m_compiler->hasCachedFrame())
        {
            SY_DEBUG("[RenderCoreRenderer] compileScene performing full scene compilation");
            frame = m_compiler->compile(m_document, m_context);
            m_context.clearDirty();
            SY_INFOF("[RenderCoreRenderer] compileScene full compilation completed frameId=%d",
                m_context.frameId);
        }
        else
        {
            SY_DEBUG("[RenderCoreRenderer] compileScene performing incremental scene compilation");
            frame = m_compiler->compileIncremental(m_document, m_context, m_lastFrame);
            SY_INFOF("[RenderCoreRenderer] compileScene incremental compilation completed frameId=%d",
                m_context.frameId);
        }
    }
    catch (const std::exception& e)
    {
        SY_ERRORF("[RenderCoreRenderer] error code=render.compile_failed message=compileScene failed: %s", e.what());
        m_context.markDirty();
        return {};
    }

    m_lastFrame = frame;
    return frame;
}

// ============================================================================
// 渲染派发（单一入口，委托 SoftwareRenderer）
// ============================================================================

void RenderCoreRenderer::render(QPainter& painter, int width, int height)
{
    if (!m_ready)
    {
        SY_WARN("[RenderCoreRenderer] render called before initialization");
        return;
    }

    if (width <= 0 || height <= 0)
    {
        SY_WARNF("[RenderCoreRenderer] render called with invalid viewport size: %dx%d",
            width, height);
        return;
    }

    m_camera.setViewportSize(width, height);
    m_context.viewportSize = Size2D{ width, height };

    if (m_camera.isDirty())
    {
        m_context.markDirty();
        m_camera.clearDirty();
    }

    try
    {
        RenderFrame frame = compileScene();

        if (!frame.frameId)
        {
            SY_DEBUG("[RenderCoreRenderer] render skipping render with empty frame");
            return;
        }

        m_softwareRenderer->render(painter, frame, m_camera, m_context.viewportSize);
        SY_DEBUGF("[RenderCoreRenderer] render completed frameId=%d viewport=%dx%d",
            frame.frameId, width, height);
    }
    catch (const std::exception& e)
    {
        SY_ERRORF("[RenderCoreRenderer] error code=render.render_failed message=render failed: %s", e.what());
    }
}

// ============================================================================
// 视口控制（委托相机）
// ============================================================================

void RenderCoreRenderer::resize(int width, int height)
{
    m_camera.setViewportSize(width, height);
    m_context.viewportSize = Size2D{ width, height };
    m_context.markDirty();
}

void RenderCoreRenderer::resetView()
{
    m_camera.reset();
    m_context.markDirty();
}

// ============================================================================
// 模式（委托相机）
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
        emitStatus(trRenderCore("Rotate"));
    else if (m_camera.isPanning())
        emitStatus(trRenderCore("Pan"));
}

void RenderCoreRenderer::onMouseMove(int x, int y, int buttons, int viewW, int viewH)
{
    if (m_camera.onMouseMove(x, y, buttons, viewW, viewH))
        m_context.markDirty();
}

void RenderCoreRenderer::onMouseRelease(int x, int y, int button, int viewW, int viewH)
{
    m_camera.onMouseRelease(x, y, button, viewW, viewH);
    emitStatus(m_camera.isMeasureMode() ? trRenderCore("Measure mode") : trRenderCore("Ready"));
}

void RenderCoreRenderer::onWheel(int delta, int viewW, int viewH)
{
    if (m_camera.onWheel(delta, viewW, viewH))
        m_context.markDirty();
}

// ============================================================================
// 选择状态管理（纯数据传递）
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
// 回调管理（纯转发）
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