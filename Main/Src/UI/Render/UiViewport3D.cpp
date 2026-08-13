#include "UiViewport3D.h"

#include "Render3D/IRenderer3D.h"

#include <QContextMenuEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QWheelEvent>
#include "Log/SyLogger.h"

Viewport3D::Viewport3D(QWidget* parent)
    : QWidget(parent)
{
    setMinimumSize(640, 480);
    setAutoFillBackground(false);
    // QOpenGLWidget 子控件在 Windows 上需要父控件也启用鼠标追踪，
    // 否则无按键按下时 mouseMoveEvent 不会被触发
    setMouseTracking(true);
    SY_INFO("[Viewport3D] Constructed without default renderer, must be injected externally");
}

Viewport3D::~Viewport3D()
{
    if (m_renderer)
    {
        m_renderer->shutdown();
        m_renderer.reset();
    }
    // 从父控件脱离，避免 Qt 在析构链中继续处理本 widget 的子控件
    setParent(nullptr);
}

void Viewport3D::releaseGLResources()
{
    // 在 native window 被 setCentralWidget(nullptr) 的 hide() 销毁前，
    // 显式关闭渲染器并释放其持有的 OpenGL 资源，避免后续析构时
    // 访问已失效的 native window handle 崩溃。
    //
    // 关键：shutdown() 内部会通过 deleteLater() 异步释放 QOpenGLWidget，
    // 不能在这里直接删除 Viewport3D 自身，否则 Qt 事件循环会撞到已销毁的子控件。
    if (m_renderer)
    {
        m_renderer->shutdown();
        m_renderer.reset();
    }
}

void Viewport3D::setRenderer(std::unique_ptr<IRenderer3D> renderer)
{
    if (m_renderer)
    {
        m_renderer->shutdown();
        m_renderer.reset();
    }
    m_renderer = std::move(renderer);
    if (m_renderer)
    {
        m_renderer->initialize(static_cast<void*>(this));
    }
}

bool Viewport3D::initialize(void* windowHandle)
{
    if (m_renderer)
    {
        return m_renderer->initialize(windowHandle);
    }
    return false;
}

void Viewport3D::setStatusCallback(std::function<void(const QString&)> callback)
{
    if (m_renderer)
    {
        m_renderer->setStatusCallback(std::move(callback));
    }
}

void Viewport3D::setSceneDocument(SceneDocument3DAdapter* document)
{
    if (m_renderer)
    {
        m_renderer->setScene(document);
    }
}

void Viewport3D::setCameraController(CameraController3D* controller)
{
    if (m_renderer)
    {
        m_renderer->setCamera(controller);
    }
}

void Viewport3D::setSelectionCallback(std::function<void(const QString&)> callback)
{
    if (m_renderer)
    {
        m_renderer->setSelectionCallback(std::move(callback));
    }
}

void Viewport3D::setPathCallback(std::function<void(const QStringList&)> callback)
{
    if (m_renderer)
    {
        m_renderer->setPathCallback(std::move(callback));
    }
}

void Viewport3D::setInputHandler(std::function<bool(QEvent* event)> handler)
{
    m_inputHandler = std::move(handler);
}

void Viewport3D::resetCamera()
{
    if (m_renderer)
    {
        m_renderer->resetView();
    }
    update();
}

void Viewport3D::setOrbitMode(bool enabled)
{
    if (m_renderer)
    {
        m_renderer->setOrbitMode(enabled);
    }
}

void Viewport3D::setMeasureMode(bool enabled)
{
    if (m_renderer)
    {
        m_renderer->setMeasureMode(enabled);
    }
}

QString Viewport3D::selectedNodeId() const
{
    return m_renderer ? m_renderer->selectedNodeId() : QString();
}

void Viewport3D::selectNodeById(const QString& nodeId)
{
    if (m_renderer)
    {
        m_renderer->selectNodeById(nodeId);
    }
    update();
}

QStringList Viewport3D::selectedPathNames() const
{
    return m_renderer ? m_renderer->selectedPathNames() : QStringList();
}

bool Viewport3D::isUsingOpenGL() const
{
    return m_renderer && m_renderer->isOpenGL();
}

IRenderer3D* Viewport3D::renderer() const
{
    return m_renderer.get();
}

bool Viewport3D::isRendererReady() const
{
    return m_renderer && m_renderer->isReady();
}

void Viewport3D::mousePressEvent(QMouseEvent* event)
{
    if (m_inputHandler && m_inputHandler(event))
    {
        event->accept();
        return;
    }

    // RenderWidget3D 作为子控件直接接收鼠标事件，不需要在这里转发
    // 避免通过 IRenderer3D 接口转发导致的事件循环
    QWidget::mousePressEvent(event);
}

void Viewport3D::mouseMoveEvent(QMouseEvent* event)
{
    if (m_inputHandler && m_inputHandler(event))
    {
        event->accept();
        return;
    }

    // RenderWidget3D 作为子控件直接接收鼠标事件，不需要在这里转发
    QWidget::mouseMoveEvent(event);
}

void Viewport3D::mouseReleaseEvent(QMouseEvent* event)
{
    if (m_inputHandler && m_inputHandler(event))
    {
        event->accept();
        return;
    }

    // RenderWidget3D 作为子控件直接接收鼠标事件，不需要在这里转发
    QWidget::mouseReleaseEvent(event);
}

void Viewport3D::wheelEvent(QWheelEvent* event)
{
    if (m_inputHandler && m_inputHandler(event))
    {
        event->accept();
        return;
    }

    // RenderWidget3D 作为子控件直接接收滚轮事件，不需要在这里转发
    QWidget::wheelEvent(event);
}

void Viewport3D::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    if (isUsingOpenGL())
    {
        return;
    }

    QPainter painter(this);
    if (m_renderer)
    {
        m_renderer->render(painter, width(), height());
    }
}

void Viewport3D::contextMenuEvent(QContextMenuEvent* event)
{
    if (m_inputHandler && m_inputHandler(event))
    {
        event->accept();
        return;
    }

    QWidget::contextMenuEvent(event);
}

void Viewport3D::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    if (m_renderer)
    {
        m_renderer->resize(width(), height());
    }
}