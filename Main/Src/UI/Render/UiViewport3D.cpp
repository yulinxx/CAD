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
    SY_INFO("[Viewport3D] Constructed without default renderer, must be injected externally");
}

Viewport3D::~Viewport3D()
{
    if (m_renderer)
        m_renderer->shutdown();
}

void Viewport3D::setRenderer(std::unique_ptr<IRenderer3D> renderer)
{
    if (m_renderer)
        m_renderer->shutdown();
    m_renderer = std::move(renderer);
    if (m_renderer)
        m_renderer->initialize(static_cast<void*>(this));
}

bool Viewport3D::initialize(void* windowHandle)
{
    if (m_renderer)
        return m_renderer->initialize(windowHandle);
    return false;
}

void Viewport3D::setStatusCallback(std::function<void(const QString&)>&& callback)
{
    if (m_renderer)
        m_renderer->setStatusCallback(std::move(callback));
}

void Viewport3D::setSceneDocument(SceneDocument3D* document)
{
    if (m_renderer)
        m_renderer->setScene(document);
}

void Viewport3D::setCameraController(CameraController3D* controller)
{
    if (m_renderer)
        m_renderer->setCamera(controller);
}

void Viewport3D::setSelectionCallback(std::function<void(const QString&)>&& callback)
{
    if (m_renderer)
        m_renderer->setSelectionCallback(std::move(callback));
}

void Viewport3D::setPathCallback(std::function<void(const QStringList&)>&& callback)
{
    if (m_renderer)
        m_renderer->setPathCallback(std::move(callback));
}

void Viewport3D::resetCamera()
{
    if (m_renderer)
        m_renderer->resetView();
    update();
}

void Viewport3D::setOrbitMode(bool enabled)
{
    if (m_renderer)
        m_renderer->setOrbitMode(enabled);
}

void Viewport3D::setMeasureMode(bool enabled)
{
    if (m_renderer)
        m_renderer->setMeasureMode(enabled);
}

QString Viewport3D::selectedNodeId() const
{
    return m_renderer ? m_renderer->selectedNodeId() : QString();
}

void Viewport3D::selectNodeById(const QString& nodeId)
{
    if (m_renderer)
        m_renderer->selectNodeById(nodeId);
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
    // RenderWidget3D 作为子控件直接接收鼠标事件，不需要在这里转发
    // 避免通过 IRenderer3D 接口转发导致的事件循环
    QWidget::mousePressEvent(event);
}

void Viewport3D::mouseMoveEvent(QMouseEvent* event)
{
    // RenderWidget3D 作为子控件直接接收鼠标事件，不需要在这里转发
    QWidget::mouseMoveEvent(event);
}

void Viewport3D::mouseReleaseEvent(QMouseEvent* event)
{
    // RenderWidget3D 作为子控件直接接收鼠标事件，不需要在这里转发
    QWidget::mouseReleaseEvent(event);
}

void Viewport3D::wheelEvent(QWheelEvent* event)
{
    // RenderWidget3D 作为子控件直接接收滚轮事件，不需要在这里转发
    QWidget::wheelEvent(event);
}

void Viewport3D::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    if (isUsingOpenGL())
        return;

    QPainter painter(this);
    if (m_renderer)
        m_renderer->render(painter, width(), height());
}

void Viewport3D::contextMenuEvent(QContextMenuEvent* event)
{
    QWidget::contextMenuEvent(event);
}

void Viewport3D::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    if (m_renderer)
        m_renderer->resize(width(), height());
}