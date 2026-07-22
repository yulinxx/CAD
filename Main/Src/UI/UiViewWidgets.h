#pragma once

#include <QGraphicsView>
#include <QStringList>
#include <QWidget>
#include <functional>
#include <memory>

class OperationBus;
class QContextMenuEvent;
class QGraphicsLineItem;
class QGraphicsPathItem;
class QGraphicsScene;
class QMenu;
class QMouseEvent;
class QWheelEvent;
class SceneDocument2D;
class ISelectionService;
class IInteractionDispatcher;

/**
 * @brief 2D 视口 — 基于 QGraphicsView 的 2D 编辑视图（已弃用）
 *
 * @deprecated 2D 生产渲染路径已确定为 Render/2D/ + RenderWidget (QOpenGLWidget) +
 *             UI/2D/ViewWidget 体系。Viewport2D (QGraphicsView) 将在后续重构中逐步替换。
 *             新功能请勿依赖 Viewport2D，应使用 UI/2D/ 模块的 ViewWidget + RenderWidget。
 *
 * 取代旧的 CanvasViewport2D。
 * 继承 QGraphicsView 提供缩放/平移/编辑交互。
 * 通过 SceneDocument2D 与 Eg::SceneManager 交互。
 */
class [[deprecated("Use Render/2D/ + RenderWidget (QOpenGLWidget) + UI/2D/ViewWidget instead")]] Viewport2D final : public QGraphicsView
{
    Q_OBJECT
public:
    explicit Viewport2D(QWidget * parent = nullptr);

public:
    void setStatusCallback(std::function<void(const QString&)>&& callback);
    void setSelectionCallback(std::function<void(const QString&, const QString&)>&& callback);
    void setCommandStageCallback(std::function<void(const QString&)>&& callback);
    void setDocument(SceneDocument2D* document);
    void setSelectionService(ISelectionService* service);
    SceneDocument2D* document() const
    {
        return m_document;
    }
    void setInteractionDispatcher(IInteractionDispatcher* dispatcher);
    void setOperationBus(OperationBus* bus);
    void resetView();
    void zoomToFit();
    void setPanModeEnabled(bool enabled);
    bool isPanModeEnabled() const
    {
        return m_panModeEnabled;
    }
    void setDrawingEnabled(bool enabled);
    void setMeasureMode(bool enabled);
    QString selectedEntityId() const;
    void deleteSelectedEntity();
    void nudgeSelectedEndpoint(const QPointF& delta);
    void selectEntityById(const QString& entityId);
    void syncSelectionDetails();
    void clearSelection();

protected:
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    void ensureGrid();
    void ensureAxes();
    void addPreviewLine(const QPointF& start, const QPointF& end);
    void addPreviewCircle(const QPointF& center, double radius);
    void addPreviewPolyline(const QVector<QPointF>& points);
    void addPreviewBezier(const QVector<QPointF>& endpoints, const QVector<QPointF>& controlPoints);
    void clearPreviewItems();
    QPointF snapPoint(const QPointF& scenePos) const;
    void updateStatus(const QString& text);
    void refreshFromDocument();
    void refreshSelectionStyle();
    void setSelectedFromHitTest(const QPointF& scenePos);
    void startCommand(const QString& commandId);
    void finishCommand(bool committed);
    void beginBoxSelect(const QPointF& scenePos);
    void updateBoxSelect(const QPointF& scenePos);
    void endBoxSelect(const QPointF& scenePos);
    void updateCommandPreview();
    void setCommandStage(const QString& stage);
    QRectF documentBounds() const;

private:
    std::function<void(const QString&)> m_statusCallback;
    std::function<void(const QString&, const QString&)> m_selectionCallback;
    std::function<void(const QString&)> m_commandStageCallback;
    QGraphicsScene* m_scene{ nullptr };
    QGraphicsLineItem* m_previewLine{ nullptr };
    QGraphicsEllipseItem* m_previewEllipse{ nullptr };
    QList<QGraphicsLineItem*> m_previewPolylineItems;
    QList<QGraphicsPathItem*> m_previewPathItems;
    QPointF m_lastPanPoint;
    QPointF m_boxSelectStart;
    bool m_panning{ false };
    bool m_boxSelecting{ false };
    bool m_panModeEnabled{ false };
    SceneDocument2D* m_document{ nullptr };
    ISelectionService* m_selectionService{ nullptr };
    IInteractionDispatcher* m_interactionDispatcher{ nullptr };
    OperationBus* m_operationBus{ nullptr };
};
