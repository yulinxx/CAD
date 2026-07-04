#pragma once

#include <QGraphicsView>
#include <QStringList>
#include <QWidget>
#include <functional>

class QContextMenuEvent;
class QGraphicsLineItem;
class QGraphicsScene;
class QMenu;
class QMouseEvent;
class QPaintEvent;
class QTreeWidget;
class QTreeWidgetItem;
class QWheelEvent;
class EntityDocument2D;
class SceneDocument3D;
class SceneNode;
class CameraController3D;
class UiCommandDispatcher;
class UiStateCenter;

// ============================================================ 
class CanvasViewport2D final : public QGraphicsView
{
    Q_OBJECT
public:
    explicit CanvasViewport2D(QWidget* parent = nullptr);

public:
    void setStatusCallback(std::function<void(const QString&)>&& callback);
    void setDocument(EntityDocument2D* document);
    void setStateCenter(UiStateCenter* stateCenter);
    void setCommandDispatcher(UiCommandDispatcher* dispatcher);
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
    void trimSelectedByPoint(const QPointF& point);
    void extendSelectedByPoint(const QPointF& point);
    void setCommandStage(const QString& stage);

private:
    std::function<void(const QString&)> m_statusCallback;
    QGraphicsScene* m_scene{ nullptr };
    QGraphicsLineItem* m_previewLine{ nullptr };
    QPointF m_lastPanPoint;
    QPointF m_drawStartPoint;
    QPointF m_boxSelectStart;
    bool m_panning{ false };
    ToolContext m_toolContext;
    QVector<QPointF> m_polylinePoints;
    EntityDocument2D* m_document{ nullptr };
    UiStateCenter* m_stateCenter{ nullptr };
    UiCommandDispatcher* m_commandDispatcher{ nullptr };
    QString m_selectedEntityId;
    QStringList m_copiedEntityIds;
    int m_selectedEndpoint{ -1 };
};

// ============================================================ 
class Viewport3D final : public QWidget
{
    Q_OBJECT
public:
    explicit Viewport3D(QWidget* parent = nullptr);
    void setStatusCallback(std::function<void(const QString&)>&& callback);
    void setSceneDocument(SceneDocument3D* document);
    void setCameraController(CameraController3D* controller);
    void setSelectionCallback(std::function<void(const QString&)>&& callback);
    void resetCamera();
    void setOrbitMode(bool enabled);
    void setMeasureMode(bool enabled);
    QString selectedNodeId() const;
    void selectNodeById(const QString& nodeId);
    void setPathCallback(std::function<void(const QStringList&)>&& callback);
    QStringList selectedPathNames() const;

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;

private:
    void emitStatus(const QString& text);
    void drawAxes(QPainter& painter);
    void drawWireCube(QPainter& painter);
    void drawNodePathOverlay(QPainter& painter) const;
    void rebuildTreeHighlight();

private:
    std::function<void(const QString&)> m_statusCallback;
    QPoint m_lastPos;
    bool m_rotating{ false };
    bool m_orbitMode{ true };
    bool m_measureMode{ false };
    double m_yaw{ 0.0 };
    double m_pitch{ 15.0 };
    double m_distance{ 10.0 };
    SceneDocument3D* m_document{ nullptr };
    CameraController3D* m_cameraController{ nullptr };
    std::function<void(const QString&)> m_selectionCallback;
    std::function<void(const QStringList&)> m_pathCallback;
    QString m_selectedNodeId;
    QStringList m_selectedPathNames;
};

// ============================================================ 
class SceneTreeDockWidget final : public QWidget
{
    Q_OBJECT
public:
    explicit SceneTreeDockWidget(QWidget* parent = nullptr);

public:
    void setSceneDocument(SceneDocument3D* document);
    void setSelectionCallback(std::function<void(const QString&)>&& callback);
    void setPathCallback(std::function<void(const QStringList&)>&& callback);
    void refresh();
    QString currentNodeId() const;

signals:
    void nodeActivated(const QString& nodeId);

private:
    void rebuildTree();
    void addNodeItem(QTreeWidgetItem* parent, const std::shared_ptr<SceneNode>& node);
    void highlightPathInTree(const QString& nodeId);
    void selectPathParents(const QString& nodeId);
    QTreeWidgetItem* findItemByNodeId(const QString& nodeId) const;

private:
    QTreeWidget* m_tree{ nullptr };
    SceneDocument3D* m_document{ nullptr };
    std::function<void(const QString&)> m_selectionCallback;
    std::function<void(const QStringList&)> m_pathCallback;
};

// ============================================================ 
class PropertiesPanelWidget final : public QWidget
{
    Q_OBJECT
public:
    explicit PropertiesPanelWidget(QWidget* parent = nullptr);

public:
    enum class WorkbenchMode
    {
        Unknown,
        TwoD,
        ThreeD
    };

public:
    void setWorkbenchMode(WorkbenchMode mode);
    void setEntityDocument(EntityDocument2D* document);
    void setSceneDocument(SceneDocument3D* document);
    void setStateText(const QString& text);
    void setSelectionText(const QString& text);
    void setObjectDetails(const QString& title, const QStringList& lines);
    void refresh();

private:
    void syncText();

private:
    QTreeWidget* m_tree{ nullptr };
    WorkbenchMode m_workbenchMode{ WorkbenchMode::Unknown };
    EntityDocument2D* m_entityDocument{ nullptr };
    SceneDocument3D* m_sceneDocument{ nullptr };
    QString m_stateText;
    QString m_selectionText;
    QString m_objectTitle;
    QStringList m_objectLines;
};