#pragma once

#include <QGraphicsView>
#include <QStringList>
#include <QWidget>
#include <functional>
#include <memory>

#include "Render3D/IRenderer3D.h"

class OperationBus;
class QContextMenuEvent;
class QGraphicsLineItem;
class QGraphicsScene;
class QMenu;
class QMouseEvent;
class QPaintEvent;
class QResizeEvent;
class QTreeWidget;
class QTreeWidgetItem;
class QWheelEvent;
class EntityDocument2D;
class SceneDocument3D;
class SceneNode;
class CameraController3D;
class UiCommandDispatcher;
class IInteractionDispatcher;
class UiStateCenter;
class OpenGLRenderer3D;

// ============================================================ 
class CanvasViewport2D final : public QGraphicsView
{
    Q_OBJECT
public:
    explicit CanvasViewport2D(QWidget* parent = nullptr);

public:
    void setStatusCallback(std::function<void(const QString&)>&& callback);
    void setSelectionCallback(std::function<void(const QString&, const QString&)>&& callback);
    void setCommandStageCallback(std::function<void(const QString&)>&& callback);
    void setDocument(EntityDocument2D* document);
    /// 获取文档
    EntityDocument2D* document() const { return m_document; }
    void setCommandDispatcher(UiCommandDispatcher* dispatcher);
    void setInteractionDispatcher(IInteractionDispatcher* dispatcher);
    /// 设置操作总线引用，用于直接执行操作
    void setOperationBus(OperationBus* bus);
    void resetView();
    void setDrawingEnabled(bool enabled);
    void setMeasureMode(bool enabled);
    /// 获取当前选中的实体 ID（从文档 selection 读取，唯一事实源）
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
    // ToolContext 是过渡期桥接结构，后续逐步迁移到 OperationBus 后移除
    // 仅保留 pure UI 状态（如 panning），其余全部迁移到 IOperation
    //
    // 旧工具桥迁移清单（按优先级排列）：
    //   已迁移（OperationBus 有对应 OperationId）：
    //     - Line      → Tool_Line（OperationBus 已注册 ✓，旧桥仅兜底）
    //     - Move      → Edit_Move（OperationBus 已注册 ✓，旧桥仅兜底）
    //     - Rotate    → Edit_Rotate（OperationBus 已注册 ✓，旧桥仅兜底）
    //     - Copy      → Edit_Copy（OperationBus 已注册 ✓，旧桥仅兜底）
    //     - Delete    → Edit_Delete（OperationBus 已注册 ✓，旧桥仅兜底）
    //     - SelectAll → Edit_SelectAll（OperationBus 已注册 ✓，旧桥仅兜底）
    //   待迁移（OperationBus 有 OperationId，但未接入视口）：
    //     - Polyline  → Tool_Polyline
    //     - Circle    → Tool_Circle
    //     - Arc       → Tool_Arc
    //     - Mirror    → Edit_MirrorH / Edit_MirrorV
    //     - Trim      → 待创建 TrimOperation
    //     - Extend    → 待创建 ExtendOperation
    //     - BoxSelect → 待并入 SelectTool
    //
    // 迁移原则：
    //   新操作只走 OperationBus::run()
    //   旧工具桥只做兼容，不新增逻辑
    //   每迁移一个操作，删掉旧桥对应分支
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
    EntityDocument2D* m_document{ nullptr };
    UiCommandDispatcher* m_commandDispatcher{ nullptr };
    IInteractionDispatcher* m_interactionDispatcher{ nullptr };
    /// 操作总线引用，新操作优先通过此入口执行
    OperationBus* m_operationBus{ nullptr };
    // 选择状态唯一来源是 EntityDocument2D::selection()
    // 视口不再维护 m_selectedEntityId / m_selectedEndpoint 副本
    QStringList m_copiedEntityIds;
};

// ============================================================ 
/**
 * @class Viewport3D
 * @brief 3D 视口 Qt 适配器
 *
 * 负责接收 Qt 事件并转发给 IRenderer3D 渲染后端，
 * 将渲染结果回传到 UI 状态中心。
 * 自身不包含渲染逻辑，仅作为 Qt 壳层。
 */
class Viewport3D final : public QWidget
{
    Q_OBJECT
public:
    explicit Viewport3D(QWidget* parent = nullptr);
    ~Viewport3D() override;

    /// 注入渲染后端（默认使用 SimpleRenderer3D）
    void setRenderer(std::unique_ptr<IRenderer3D> renderer);

    /// 初始化渲染器（调用渲染器的 initialize）
    bool initialize(void* windowHandle = nullptr);

    /// 设置状态文本回调（转发到渲染器）
    void setStatusCallback(std::function<void(const QString&)>&& callback);
    /// 设置场景文档
    void setSceneDocument(SceneDocument3D* document);
    /// 设置相机控制器
    void setCameraController(CameraController3D* controller);
    /// 设置选择变更回调
    void setSelectionCallback(std::function<void(const QString&)>&& callback);
    /// 设置路径变更回调
    void setPathCallback(std::function<void(const QStringList&)>&& callback);

    /// 重置相机
    void resetCamera();
    /// 设置轨道旋转模式
    void setOrbitMode(bool enabled);
    /// 设置测量模式
    void setMeasureMode(bool enabled);
    /// 获取当前选中节点 ID
    QString selectedNodeId() const;
    /// 按 ID 选中节点
    void selectNodeById(const QString& nodeId);
    /// 获取选中节点路径
    QStringList selectedPathNames() const;

    /// 判断当前是否使用 OpenGL 渲染（RenderWidget3DAdapter）
    bool isUsingOpenGL() const;

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    /// 3D 渲染后端
    std::unique_ptr<IRenderer3D> m_renderer;
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

    /**
     * @struct PropertiesData
     * @brief 属性面板的统一输入数据结构
     * 
     * 本结构体作为面板的唯一数据输入入口，将所有展示数据集中管理。
     * 面板只负责渲染，不负责数据计算和业务逻辑。
     */
    struct PropertiesData
    {
        QString stateText;
        QString selectionText;
        QString objectTitle;
        QStringList objectLines;
        WorkbenchMode mode{ WorkbenchMode::Unknown };
        QString documentType;
        QString documentStatus;
        QStringList modeSpecificFields;
    };

public:
    /**
     * @brief 设置统一的属性数据（推荐使用此方法）
     * @param data 属性数据结构
     */
    void setPropertiesData(const PropertiesData& data);

    /**
     * @brief 设置工作台模式
     * @param mode 工作台模式
     */
    void setWorkbenchMode(WorkbenchMode mode);

    /**
     * @brief 设置状态文本
     * @param text 状态文本
     */
    void setStateText(const QString& text);

    /**
     * @brief 设置选择文本
     * @param text 选择文本
     */
    void setSelectionText(const QString& text);

    /**
     * @brief 设置对象详情
     * @param title 详情标题
     * @param lines 详情行列表
     */
    void setObjectDetails(const QString& title, const QStringList& lines);

    /**
     * @brief 刷新面板显示
     */
    void refresh();

private:
    /**
     * @brief 同步数据到 UI 显示
     * 只做渲染，不做任何业务逻辑计算
     */
    void syncText();

private:
    QTreeWidget* m_tree{ nullptr };
    PropertiesData m_data;
};