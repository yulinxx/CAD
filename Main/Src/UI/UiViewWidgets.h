/**
 * @file UiViewWidgets.h
 * @brief 2D/3D 视口与辅助面板 — CanvasViewport2D、Viewport3D、SceneTreeDockWidget、PropertiesPanelWidget
 */

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
//  CanvasViewport2D — 2D 画布视口
// ============================================================

/**
 * @brief 2D 画布视口
 *
 * 基于 QGraphicsView 的 2D 绘图视口，负责：
 * - 网格和坐标轴绘制
 * - 实体创建（线段、多段线、圆、弧）
 * - 鼠标交互（平移、框选、捕捉）
 * - 命令状态管理（绘制、移动、复制、旋转、镜像、修剪、延伸）
 */
class CanvasViewport2D final : public QGraphicsView
{
    Q_OBJECT
public:
    explicit CanvasViewport2D(QWidget* parent = nullptr);

public:
    /// 设置状态栏回调
    void setStatusCallback(std::function<void(const QString&)>&& callback);

    /// 绑定 2D 实体文档
    void setDocument(EntityDocument2D* document);

    /// 绑定 UI 状态中心
    void setStateCenter(UiStateCenter* stateCenter);

    /// 绑定命令调度器
    void setCommandDispatcher(UiCommandDispatcher* dispatcher);

    /// 重置视图到原点和默认缩放
    void resetView();

    /// 启用/禁用绘制模式
    void setDrawingEnabled(bool enabled);

    /// 启用/禁用测量模式
    void setMeasureMode(bool enabled);

    /// 返回当前选中实体的 ID
    QString selectedEntityId() const;

    /// 删除当前选中的实体
    void deleteSelectedEntity();

    /// 微调选中线段的端点
    void nudgeSelectedEndpoint(const QPointF& delta);

    /// 根据 ID 选中实体
    void selectEntityById(const QString& entityId);

    /// 同步选中实体的详细信息到状态栏
    void syncSelectionDetails();

    /// 清空当前选择
    void clearSelection();

protected:
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;

private:
    /// 绘制背景网格线
    void ensureGrid();

    /// 绘制 X/Y 坐标轴
    void ensureAxes();

    void addPreviewLine(const QPointF& start, const QPointF& end);
    void commitLine(const QPointF& start, const QPointF& end);
    void commitPolylinePoint(const QPointF& pt);
    void finishPolyline(const QPointF& pt);
    void commitCircle(const QPointF& center, double radius);
    void commitArc(const QPointF& center, double radius, double startDeg, double spanDeg);
    void refreshCopiedSelection();

    /// 场景坐标捕捉到最近网格点
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
    void beginBoxSelect(const QPointF& scenePos);
    void updateBoxSelect(const QPointF& scenePos);
    void endBoxSelect(const QPointF& scenePos);
    void applySelectionTransform(const QPointF& anchor, const QPointF& target, const QString& mode);
    void trimSelectedByPoint(const QPointF& point);
    void extendSelectedByPoint(const QPointF& point);

    /// 更新命令阶段文本到 UiStateCenter
    void setCommandStage(const QString& stage);

private:
    /// 工具上下文 — 追踪当前激活的绘制工具和交互状态
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
//  Viewport3D — 3D 视口
// ============================================================

/**
 * @brief 3D 视口
 *
 * 负责 3D 场景的渲染预览、相机交互和节点选择。
 */
class Viewport3D final : public QWidget
{
    Q_OBJECT
public:
    explicit Viewport3D(QWidget* parent = nullptr);

    void setStatusCallback(std::function<void(const QString&)>&& callback);
    void setSceneDocument(SceneDocument3D* document);
    void setCameraController(CameraController3D* controller);
    void setSelectionCallback(std::function<void(const QString&)>&& callback);

    /// 重置相机到默认视角
    void resetCamera();

    /// 启用/禁用轨道旋转模式
    void setOrbitMode(bool enabled);

    /// 启用/禁用测量模式
    void setMeasureMode(bool enabled);

    /// 返回当前选中节点的 ID
    QString selectedNodeId() const;

    /// 根据 ID 选中节点
    void selectNodeById(const QString& nodeId);

    /// 设置路径变更回调
    void setPathCallback(std::function<void(const QStringList&)>&& callback);

    /// 返回当前选中节点的路径名称列表
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

    /// 根据当前选中节点重建路径高亮
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
//  SceneTreeDockWidget — 场景树面板
// ============================================================

/**
 * @brief 场景树面板
 *
 * 以树形结构展示 3D 场景节点，支持点击选择和路径展开。
 */
class SceneTreeDockWidget final : public QWidget
{
    Q_OBJECT
public:
    explicit SceneTreeDockWidget(QWidget* parent = nullptr);

    void setSceneDocument(SceneDocument3D* document);
    void setSelectionCallback(std::function<void(const QString&)>&& callback);
    void setPathCallback(std::function<void(const QStringList&)>&& callback);

    /// 刷新树形结构
    void refresh();

    /// 返回当前选中节点的 ID
    QString currentNodeId() const;

signals:
    void nodeActivated(const QString& nodeId);

private:
    void rebuildTree();
    void addNodeItem(QTreeWidgetItem* parent, const std::shared_ptr<SceneNode>& node);

    /// 在树中高亮指定节点
    void highlightPathInTree(const QString& nodeId);

    /// 展开目标节点的所有父级
    void selectPathParents(const QString& nodeId);

    /// 根据节点 ID 查找树节点项
    QTreeWidgetItem* findItemByNodeId(const QString& nodeId) const;

private:
    QTreeWidget* m_tree{ nullptr };
    SceneDocument3D* m_document{ nullptr };
    std::function<void(const QString&)> m_selectionCallback;
    std::function<void(const QStringList&)> m_pathCallback;
};

// ============================================================
//  PropertiesPanelWidget — 属性面板
// ============================================================

/**
 * @brief 属性面板
 *
 * 以键值对形式展示当前状态、选择信息和对象详细属性。
 */
class PropertiesPanelWidget final : public QWidget
{
    Q_OBJECT
public:
    explicit PropertiesPanelWidget(QWidget* parent = nullptr);

    void setEntityDocument(EntityDocument2D* document);
    void setSceneDocument(SceneDocument3D* document);
    void setStateText(const QString& text);
    void setSelectionText(const QString& text);
    void setObjectDetails(const QString& title, const QStringList& lines);

    /// 刷新面板内容
    void refresh();

private:
    void syncText();

private:
    QTreeWidget* m_tree{ nullptr };
    EntityDocument2D* m_entityDocument{ nullptr };
    SceneDocument3D* m_sceneDocument{ nullptr };
    QString m_stateText;
    QString m_selectionText;
    QString m_objectTitle;
    QStringList m_objectLines;
};
