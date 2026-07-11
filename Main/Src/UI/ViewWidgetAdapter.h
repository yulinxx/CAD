/**
 * @file ViewWidgetAdapter.h
 * @brief ViewWidget 适配器 — 让 Viewport2D 能被 OperationBus 使用
 */
#pragma once

#include <QObject>
#include <QPointF>
#include <QString>
#include <functional>

class Viewport2D;
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
    explicit ViewWidgetAdapter(Viewport2D* viewport, QObject* parent = nullptr);
    ~ViewWidgetAdapter() override = default;

    void resetView();
    void zoomToFit();
    QPointF screenToWorld(const QPoint& screenPos) const;
    SceneDocument2D* document() const;
    SceneEditServiceAdapter* sceneEditService() const { return m_sceneEditAdapter; }
    Viewport2D* viewport() const { return m_viewport; }

private:
    Viewport2D* m_viewport{ nullptr };
    SceneEditServiceAdapter* m_sceneEditAdapter{ nullptr };
};
