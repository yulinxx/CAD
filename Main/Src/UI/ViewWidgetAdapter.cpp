/**
 * @file ViewWidgetAdapter.cpp
 * @brief ViewWidget 适配器实现
 */
#include "ViewWidgetAdapter.h"
#include "UiViewWidgets.h"
#include "SceneDocument2D.h"
#include "SceneEditServiceAdapter.h"

#include <QPoint>

ViewWidgetAdapter::ViewWidgetAdapter(Viewport2D* viewport, QObject* parent)
    : QObject(parent)
    , m_viewport(viewport)
{
    if (viewport && viewport->document())
    {
        m_sceneEditAdapter = new SceneEditServiceAdapter(viewport->document(), this);
    }
}

void ViewWidgetAdapter::resetView()
{
    if (m_viewport)
        m_viewport->resetView();
}

void ViewWidgetAdapter::zoomToFit()
{
    if (m_viewport)
        m_viewport->resetView();
}

QPointF ViewWidgetAdapter::screenToWorld(const QPoint& screenPos) const
{
    if (m_viewport)
        return m_viewport->mapToScene(screenPos);
    return QPointF(0, 0);
}

SceneDocument2D* ViewWidgetAdapter::document() const
{
    if (m_viewport)
        return m_viewport->document();
    return nullptr;
}