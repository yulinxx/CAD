/**
 * @file RenderViewport2D.h
 * @brief 基于 Renderx 的 2D 渲染视口 — 替换旧的 QGraphicsView 视口
 *
 * 使用 RenderWidget (QOpenGLWidget + Renderx) 作为渲染内核，
 * 内置 2D 相机管理，SceneManager 作为数据源（ISceneDataSource）。
 * 对外接口与 Viewport2D 保持兼容，便于无缝替换。
 */
#pragma once

#include <QWidget>
#include <QPointF>
#include <QString>
#include <QPoint>
#include <functional>
#include <memory>
#include <unordered_set>

#include "Camera2D.h"
#include "ViewportSelector.h"
#include "Render/RenderTypes.h"

#include "Engine/EntityIdGenerator.h"
#include "Engine2D/Core/SceneNotifier.h"
#include "Engine2D/Edit/SceneEditService.h"

class RenderWidget;
class SceneDocument2D;
class ISelectionService;
class IInteractionDispatcher;
class OperationBus;
class ToolManager;
struct ToolContext;

class QMouseEvent;
class QWheelEvent;
class QContextMenuEvent;
class QKeyEvent;
class QTimer;
class QResizeEvent;

namespace Eg
{
    class SceneManager;
}

/**
 * @brief 2D 渲染视口 — 基于 Renderx 的 OpenGL 渲染
 *
 * 取代旧的 Viewport2D (QGraphicsView)，使用 RenderWidget + Renderx 进行硬件加速渲染。
 * 数据层通过 ISceneDataSource 接口推送几何原语，渲染层自行处理细分和缓存。
 */
class RenderViewport2D : public QWidget
    , private Eg::SceneNotifier::IObserver
{
    Q_OBJECT
public:
    explicit RenderViewport2D(QWidget* parent = nullptr);
    ~RenderViewport2D() override;

public:
    // ==================== 外部接口（与 Viewport2D 兼容）====================

    void setStatusCallback(std::function<void(const QString&)>&& callback);
    void setSelectionCallback(std::function<void(const QString&, const QString&)>&& callback);
    void setCommandStageCallback(std::function<void(const QString&)>&& callback);
    // 设置鼠标位置回调，用于在状态栏显示当前光标坐标
    void setPositionCallback(std::function<void(double, double)>&& callback);

    void setDocument(SceneDocument2D* document);

    // ==================== 工具系统接口 ====================
    /// 设置场景编辑服务（工具提交图元时使用）
    void setSceneEditService(SceneEditService* service);
    /// 初始化工具系统
    void initializeTools();
    /// 设置活动工具
    bool setActiveTool(const QString& toolName);
    /// 获取活动工具名称
    QString activeToolName() const;
    /// 获取工具管理器
    ToolManager* toolManager() const;
    SceneDocument2D* document() const
    {
        return m_document;
    }
    void setSelectionService(ISelectionService* service);
    void setInteractionDispatcher(IInteractionDispatcher* dispatcher);
    void setOperationBus(OperationBus* bus);

    void resetView();
    void zoomToFit();
    void zoomToSelection();
    void zoomIn();
    void zoomOut();
    void requestSceneRefresh();
    /// 轻量级重绘请求，不触发全量 gather，用于选中变化等仅需视觉刷新场景
    void requestRepaint();
    /// 全量刷新请求，强制完整 gather + submit（导入、大批量修改后）
    void requestFullRefresh();
    void setPanModeEnabled(bool enabled);
    bool isPanModeEnabled() const;
    void setDrawingEnabled(bool enabled);
    void setMeasureMode(bool enabled);

    QString selectedEntityId() const;
    void deleteSelectedEntity();
    void nudgeSelectedEndpoint(const QPointF& delta);
    void selectEntityById(const QString& entityId);
    void syncSelectionDetails();
    void clearSelection();

    // 坐标转换
    QPointF mapToScene(const QPoint& screenPos) const;

signals:
    void sceneChanged();

protected:
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void showEvent(QShowEvent* event) override;
    // RenderWidget 是 QOpenGLWidget 原生窗口，鼠标事件不会传递到父控件，
    // 通过事件过滤器将 RenderWidget 上的鼠标事件转发到本视口处理
    bool eventFilter(QObject* obj, QEvent* event) override;

private:
    // ==================== 内部方法 ====================

    // 初始化
    void initRenderWidget();
    void initTimers();

    // IObserver 实现
    void onSceneChanged() override;
    void onSelectionChanged() override;

    // 渲染更新
    void updateSceneRender();
    void scheduleSceneUpdate();

    // 视图控制
    void updateViewMatrix();

    // 辅助
    void updateStatus(const QString& text);
    Eg::SceneManager* sceneManager() const;

private:
    // 渲染控件
    RenderWidget* m_renderWidget{ nullptr };

    // 相机
    Camera2D m_camera;

    // 文档和服务
    SceneDocument2D* m_document{ nullptr };
    Eg::SceneManager* m_sceneManager{ nullptr };
    std::shared_ptr<bool> m_alive{ std::make_shared<bool>(true) };
    ISelectionService* m_selectionService{ nullptr };
    IInteractionDispatcher* m_interactionDispatcher{ nullptr };
    OperationBus* m_operationBus{ nullptr };
    SceneEditService* m_sceneEditService{ nullptr };

    // 工具系统
    std::unique_ptr<ToolManager> m_toolManager;

    // 选择控制器
    std::unique_ptr<ViewportSelector> m_selector;

    // 回调
    std::function<void(const QString&)> m_statusCallback;
    std::function<void(const QString&, const QString&)> m_selectionCallback;
    std::function<void(const QString&)> m_commandStageCallback;
    // 鼠标位置回调，参数为世界坐标 (x, y)
    std::function<void(double, double)> m_positionCallback;

    // 交互状态
    bool m_panning{ false };
    bool m_panModeEnabled{ false };
    QPoint m_lastMousePos;

    // 刷新级别（增量渲染策略）
    enum class RefreshLevel
    {
        None,         // 无待办，不要触发任何 GL 操作
        Repaint,      // 仅重绘（选择变化等纯视觉刷新，不触碰渲染数据）
        LightUpdate,  // 仅提交脏/删除图元到渲染设备（依赖 RenderWidget 增量 API）
        FullRefresh   // 全量 gather + submit（导入、大批量修改后）
    };
    RefreshLevel m_refreshLevel{ RefreshLevel::None };

    // 场景更新节流
    QTimer* m_sceneUpdateTimer{ nullptr };

    // 脏标记集合（用于增量渲染）
    std::unordered_set<Eg::EntityId> m_pendingDirtyIds;
    std::unordered_set<Eg::EntityId> m_pendingDeletedIds;

    // 连接管理
    QMetaObject::Connection m_sceneChangedConn;
};
