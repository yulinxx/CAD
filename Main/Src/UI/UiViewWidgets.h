#pragma once

#include <QGraphicsView>
#include <QStringList>
#include <QWidget>
#include <functional>
#include <memory>

class OperationBus;
class QContextMenuEvent;
class QGraphicsLineItem;
class QGraphicsScene;
class QMenu;
class QMouseEvent;
class QWheelEvent;
class SceneDocument2D;
class UiCommandDispatcher;
class IInteractionDispatcher;

/**
 * @brief 2D 视口 — 基于 QGraphicsView 的 2D 编辑视图
 *
 * 取代旧的 CanvasViewport2D。
 * 继承 QGraphicsView 提供缩放/平移/编辑交互。
 * 通过 SceneDocument2D 与 Eg::SceneManager 交互。
 */
class Viewport2D final : public QGraphicsView
{
    Q_OBJECT
public:
    explicit Viewport2D(QWidget* parent = nullptr);

public:
    void setStatusCallback(std::function<void(const QString&)>&& callback);
    void setSelectionCallback(std::function<void(const QString&, const QString&)>&& callback);
    void setCommandStageCallback(std::function<void(const QString&)>&& callback);
    void setDocument(SceneDocument2D* document);
    SceneDocument2D* document() const { return m_document; }
    void setCommandDispatcher(UiCommandDispatcher* dispatcher);
    void setInteractionDispatcher(IInteractionDispatcher* dispatcher);
    void setOperationBus(OperationBus* bus);
    void resetView();
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

private:
    std::function<void(const QString&)> m_statusCallback;
    std::function<void(const QString&, const QString&)> m_selectionCallback;
    std::function<void(const QString&)> m_commandStageCallback;
    QGraphicsScene* m_scene{ nullptr };
    QGraphicsLineItem* m_previewLine{ nullptr };
    QGraphicsEllipseItem* m_previewEllipse{ nullptr };
    QList<QGraphicsLineItem*> m_previewPolylineItems;
    QPointF m_lastPanPoint;
    QPointF m_boxSelectStart;
    bool m_panning{ false };
    bool m_boxSelecting{ false };
    SceneDocument2D* m_document{ nullptr };
    UiCommandDispatcher* m_commandDispatcher{ nullptr };
    IInteractionDispatcher* m_interactionDispatcher{ nullptr };
    OperationBus* m_operationBus{ nullptr };
};

