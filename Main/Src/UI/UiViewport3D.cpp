#include "UiViewport3D.h"

#include "SimpleRenderer3D.h"
#include "RenderWidget3DAdapter.h"
#include "Render3D/RenderWidget3D.h"
#include "RenderCore/RenderCoreRenderer.h"

#include <QContextMenuEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QWheelEvent>

Viewport3D::Viewport3D(QWidget* parent)
    : QWidget(parent)
{
    setMinimumSize(640, 480);
    setAutoFillBackground(true);

    m_renderer = std::make_unique<RenderCoreRenderer>();
    m_renderer->initialize();
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

void Viewport3D::mousePressEvent(QMouseEvent* event)
{
    if (m_renderer)
        m_renderer->onMousePress(event->pos().x(), event->pos().y(),
            static_cast<int>(event->button()), static_cast<int>(event->modifiers()),
            width(), height());
    update();
}

void Viewport3D::mouseMoveEvent(QMouseEvent* event)
{
    if (m_renderer)
        m_renderer->onMouseMove(event->pos().x(), event->pos().y(),
            static_cast<int>(event->buttons()), width(), height());
    update();
}

void Viewport3D::mouseReleaseEvent(QMouseEvent* event)
{
    if (m_renderer)
        m_renderer->onMouseRelease(event->pos().x(), event->pos().y(),
            static_cast<int>(event->button()), width(), height());
    update();
}

void Viewport3D::wheelEvent(QWheelEvent* event)
{
    if (m_renderer)
        m_renderer->onWheel(event->angleDelta().y(), width(), height());
    update();
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