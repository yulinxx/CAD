/**
 * @file ViewWidgetAdapter.cpp
 * @brief ViewWidget 适配器实现
 */
#include "ViewWidgetAdapter.h"
#include "UiViewWidgets.h"
#include "UiEntities.h"
#include "SceneEditServiceAdapter.h"

#include <QPoint>

ViewWidgetAdapter::ViewWidgetAdapter(CanvasViewport2D* viewport, QObject* parent)
    : QObject(parent)
    , m_viewport(viewport)
{
    // 创建 SceneEditService 适配器
    if (viewport && viewport->document())
    {
        m_sceneEditAdapter = new SceneEditServiceAdapter(viewport->document(), this);
    }
}

void ViewWidgetAdapter::setActiveTool(const QString& toolName)
{
    if (!m_viewport)
        return;

    // TODO: CanvasViewport2D 的 enter*Mode 函数目前为 private，暂时 stub
    Q_UNUSED(toolName);
}

void ViewWidgetAdapter::syncSelectionFromScene()
{
    // TODO: CanvasViewport2D::refreshSelectionStyle() 目前为 private，暂时 stub
}

void ViewWidgetAdapter::updateRenderData()
{
    // TODO: CanvasViewport2D::refreshFromDocument() 目前为 private，暂时 stub
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

QPointF ViewWidgetAdapter::getCurrentMouseWorldPos() const
{
    // 过渡期：返回原点
    return QPointF(0, 0);
}

QPointF ViewWidgetAdapter::screenToWorld(const QPoint& screenPos) const
{
    if (m_viewport)
        return m_viewport->mapToScene(screenPos);
    return QPointF(0, 0);
}

EntityDocument2D* ViewWidgetAdapter::document() const
{
    if (m_viewport)
        return m_viewport->document();
    return nullptr;
}
