#include "ViewportNavigation2D.h"

#include "Camera2D.h"
#include "RenderWidget.h"

#include <QNativeGestureEvent>
#include <QWheelEvent>

#include <cmath>

void ViewportNavigation2D::setRenderWidget(const RenderWidget* widget)
{
    m_renderWidget = widget;
}

void ViewportNavigation2D::setCamera(Camera2D* camera)
{
    m_camera = camera;
}

Camera2D* ViewportNavigation2D::camera() const
{
    return m_camera;
}

void ViewportNavigation2D::setCameraChangedCallback(std::function<void()> callback)
{
    m_cameraChangedCallback = std::move(callback);
}

void ViewportNavigation2D::notifyCameraChanged()
{
    if (m_cameraChangedCallback)
    {
        m_cameraChangedCallback();
    }
}

ViewportNavigation2D::WheelGestureType ViewportNavigation2D::classifyWheel(
    const QPoint& angleDelta, const QPointF& pixelDelta, Qt::KeyboardModifiers modifiers, Qt::ScrollPhase phase)
{
    Q_UNUSED(angleDelta);
    Q_UNUSED(pixelDelta);
    // 鼠标滚轮：保持"滚轮=缩放"语义，不因修饰键改变
    if (phase == Qt::NoScrollPhase)
    {
        return WheelGestureType::Zoom;
    }
    // 仅针对触控板（带滚动阶段）
    if (modifiers.testFlag(Qt::ControlModifier))
    {
        return WheelGestureType::Zoom;
    }
    if (modifiers.testFlag(Qt::ShiftModifier))
    {
        return WheelGestureType::HorizontalPan;
    }
    return WheelGestureType::Pan;
}

void ViewportNavigation2D::handleWheel(QWheelEvent* event)
{
    if (!event || !m_camera)
    {
        return;
    }

    const float vpW = physicalWidth();
    const float vpH = physicalHeight();
    const QPointF worldPos = widgetToWorld(event->position());

    const QPoint angleDelta = event->angleDelta();
    const QPointF pixelDelta = event->pixelDelta();
    const Qt::ScrollPhase phase = event->phase();

    switch (classifyWheel(angleDelta, pixelDelta, event->modifiers(), phase))
    {
    case WheelGestureType::Zoom:
    {
        // 触控板捏合(Ctrl+滚动阶段)：平滑连续；普通鼠标滚轮(含 Ctrl/Shift)：每格 10%
        float factor;
        if (phase != Qt::NoScrollPhase && event->modifiers().testFlag(Qt::ControlModifier))
        {
            factor = 1.0f + static_cast<float>(angleDelta.y()) / 1200.0f;
        }
        else
        {
            factor = (angleDelta.y() > 0) ? 1.1f : 0.9f;
        }
        m_camera->zoomIn(factor, worldPos, vpW, vpH);
        break;
    }
    case WheelGestureType::Pan:
        // 触控板双指拖动 → 平移（方向与鼠标拖拽一致）
        m_camera->pan(static_cast<float>(pixelDelta.x()) / m_camera->zoomX,
            -static_cast<float>(pixelDelta.y()) / m_camera->zoomY);
        break;
    case WheelGestureType::HorizontalPan:
    {
        // 触控板 Shift+双指 → 水平平移
        const double scrollValue = (angleDelta.y() != 0)
            ? static_cast<double>(angleDelta.y())
            : (pixelDelta.isNull() ? static_cast<double>(angleDelta.x())
                                   : static_cast<double>(pixelDelta.y()));
        m_camera->pan(static_cast<float>(scrollValue) / m_camera->zoomX, 0.0f);
        break;
    }
    }

    notifyCameraChanged();
}

void ViewportNavigation2D::handleNativeGesture(QNativeGestureEvent* event)
{
    if (!event || !m_camera || event->gestureType() != Qt::ZoomNativeGesture)
    {
        return;
    }

    // macOS 触控板捏合：value() 为原始增量（张开≈+0.05，捏拢≈-0.05）
    const double factor = 1.0 + event->value();
    if (!std::isfinite(factor) || factor < 0.3 || factor > 3.0)
    {
        return;
    }

    const float vpW = physicalWidth();
    const float vpH = physicalHeight();
    const QPointF worldPos = widgetToWorld(event->position());
    m_camera->zoomIn(static_cast<float>(factor), worldPos, vpW, vpH);

    notifyCameraChanged();
}

void ViewportNavigation2D::beginPan(const QPoint& physWidgetPos)
{
    m_panning = true;
    m_lastMousePos = physWidgetPos;
}

void ViewportNavigation2D::updatePan(const QPoint& physWidgetPos)
{
    if (!m_panning || !m_camera)
    {
        return;
    }

    const QPoint delta = physWidgetPos - m_lastMousePos;
    m_lastMousePos = physWidgetPos;
    const float worldDx = static_cast<float>(delta.x()) / m_camera->zoomX;
    const float worldDy = -static_cast<float>(delta.y()) / m_camera->zoomY;
    m_camera->pan(worldDx, worldDy);

    notifyCameraChanged();
}

void ViewportNavigation2D::endPan()
{
    m_panning = false;
}

QSizeF ViewportNavigation2D::physicalViewportSize() const
{
    if (!m_renderWidget)
    {
        return QSizeF(0, 0);
    }
    const float dpr = m_renderWidget->devicePixelRatio();
    return QSizeF(m_renderWidget->width() * dpr, m_renderWidget->height() * dpr);
}

float ViewportNavigation2D::physicalWidth() const
{
    return static_cast<float>(physicalViewportSize().width());
}

float ViewportNavigation2D::physicalHeight() const
{
    return static_cast<float>(physicalViewportSize().height());
}

QPointF ViewportNavigation2D::physicalToWorld(const QPoint& physPos) const
{
    const QSizeF phys = physicalViewportSize();
    return m_camera->screenToWorld(physPos, static_cast<float>(phys.width()), static_cast<float>(phys.height()));
}

QPointF ViewportNavigation2D::widgetToWorld(const QPointF& widgetLocalPos) const
{
    if (!m_renderWidget)
    {
        return QPointF();
    }
    const float dpr = m_renderWidget->devicePixelRatio();
    const QPoint physPos(static_cast<int>(widgetLocalPos.x() * dpr), static_cast<int>(widgetLocalPos.y() * dpr));
    return physicalToWorld(physPos);
}
