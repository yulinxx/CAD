/**
 * @file RenderViewportAdapter.cpp
 * @brief RenderViewport2D 适配器实现
 */
#include "RenderViewportAdapter.h"
#include "RenderViewport2D.h"
#include "SceneDocument2D.h"
#include "SceneEditServiceAdapter.h"

#include <QPoint>

RenderViewportAdapter::RenderViewportAdapter(RenderViewport2D* viewport, QObject* parent)
    : QObject(parent)
    , m_viewport(viewport)
{
    if (viewport && viewport->document())
    {
        m_sceneEditAdapter = new SceneEditServiceAdapter(viewport->document(), this);
    }
}

void RenderViewportAdapter::resetView()
{
    if (m_viewport)
        m_viewport->resetView();
}

void RenderViewportAdapter::zoomToFit()
{
    if (m_viewport)
        m_viewport->zoomToFit();
}

QPointF RenderViewportAdapter::screenToWorld(const QPoint& screenPos) const
{
    if (m_viewport)
        return m_viewport->mapToScene(screenPos);
    return QPointF(0, 0);
}

SceneDocument2D* RenderViewportAdapter::document() const
{
    if (m_viewport)
        return m_viewport->document();
    return nullptr;
}
