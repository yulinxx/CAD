/**
 * @file ViewWidgetAdapter.h
 * @brief ViewWidget 适配器 — 让 Viewport2D/RenderViewport2D 能被 OperationBus 使用
 */
#pragma once

#include <QObject>
#include <QPointF>
#include <QString>
#include <functional>

class RenderViewport2D;
class SceneDocument2D;
class SceneEditServiceAdapter;

namespace Eg
{
    class SceneManager;
}

class ViewWidgetAdapter : public QObject
{
    Q_OBJECT

public:
    explicit ViewWidgetAdapter(RenderViewport2D* viewport, QObject* parent = nullptr);
    ~ViewWidgetAdapter() override = default;

    void resetView();
    void zoomToFit();
    QPointF screenToWorld(const QPoint& screenPos) const;
    SceneDocument2D* document() const;
    SceneEditServiceAdapter* sceneEditService() const
    {
        return m_sceneEditAdapter;
    }
    RenderViewport2D* viewport() const
    {
        return m_viewport;
    }

private:
    RenderViewport2D* m_viewport{ nullptr };
    SceneEditServiceAdapter* m_sceneEditAdapter{ nullptr };
};
