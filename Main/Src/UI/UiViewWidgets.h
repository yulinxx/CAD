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

private:
    struct ToolContext
    {
        enum class DrawTool
        {
            None,
            Line,
            Polyline,
            Circle,
            Arc,
            Move,
            Copy,
            Rotate,
            Mirror,
            Trim,
            Extend,
            BoxSelect
        };

        DrawTool tool{ DrawTool::None };
        bool drawing{ false };
        bool measureMode{ false };
        bool hasDrawStart{ false };
        bool boxSelecting{ false };
        bool transformCopy{ false };
    };

private:
    void ensureGrid();
    void ensureAxes();
    void addPreviewLine(const QPointF& start, const QPointF& end);
    void commitLine(const QPointF& start, const QPointF& end);
    void commitPolylinePoint(const QPointF& pt);
    void finishPolyline(const QPointF& pt);
    void commitCircle(const QPointF& center, double radius);
    void commitArc(const QPointF& center, double radius, double startDeg, double spanDeg);
    void refreshCopiedSelection();
    QPointF snapPoint(const QPointF& scenePos) const;
    void updateStatus(const QString& text);
    void refreshFromDocument();
    void refreshSelectionStyle();
    void setSelectedFromHitTest(const QPointF& scenePos);
    void startCommand(const QString& commandId);
    void finishCommand(bool committed);
    void enterPolylineMode();
    void enterCircleMode();
    void enterArcMode();
    void enterSelectMode();
    void enterMoveMode();
    void enterCopyMode();
    void enterRotateMode();
    void enterMirrorMode();
    void enterTrimMode();
    void enterExtendMode();
    void enterBoxSelectMode();
    void activateDrawTool(ToolContext::DrawTool tool, const QString& commandId, const QString& statusText);
    void activateTransformTool(ToolContext::DrawTool tool, const QString& commandId, const QString& statusText);
    void beginBoxSelect(const QPointF& scenePos);
    void updateBoxSelect(const QPointF& scenePos);
    void endBoxSelect(const QPointF& scenePos);
    void applySelectionTransform(const QPointF& anchor, const QPointF& target, const QString& mode);
    void updateCommandPreview();
    void trimSelectedByPoint(const QPointF& point);
    void extendSelectedByPoint(const QPointF& point);
    void setCommandStage(const QString& stage);

private:
    std::function<void(const QString&)> m_statusCallback;
    std::function<void(const QString&, const QString&)> m_selectionCallback;
    std::function<void(const QString&)> m_commandStageCallback;
    QGraphicsScene* m_scene{ nullptr };
    QGraphicsLineItem* m_previewLine{ nullptr };
    QPointF m_lastPanPoint;
    QPointF m_drawStartPoint;
    QPointF m_boxSelectStart;
    bool m_panning{ false };
    ToolContext m_toolContext;
    QVector<QPointF> m_polylinePoints;
    SceneDocument2D* m_document{ nullptr };
    UiCommandDispatcher* m_commandDispatcher{ nullptr };
    IInteractionDispatcher* m_interactionDispatcher{ nullptr };
    OperationBus* m_operationBus{ nullptr };
    QStringList m_copiedEntityIds;
};

