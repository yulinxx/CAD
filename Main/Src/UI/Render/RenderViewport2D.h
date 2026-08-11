/**
 * @file RenderViewport2D.h
 * @brief 基于 Renderx 的 2D 渲染视口 — 替换旧的 QGraphicsView 视口
 *
 * 使用 RenderWidget (QOpenGLWidget + Renderx) 作为渲染内核，
 * 内置 2D 相机管理，SceneManager 作为数据源（ISceneDataSource）。
 * 对外接口与 Viewport2D 保持兼容，便于无缝替换。
 *
 * 输入路由委托给 ViewportInputRouter（P5 大文件收口）。
 */
#pragma once

#include <QWidget>
#include <QPointF>
#include <QString>
#include <QPoint>
#include <functional>
#include <memory>

#include "Camera2D.h"
#include "ViewportSelector.h"

#include "UI/IViewportHost.h"  // P1: 2D/3D 公共视口宿主接口

class RenderWidget;
class SceneDocument2D;
class ISelectionService;
class IInteractionDispatcher;
class OperationBus;
class ToolManager;
class ITool;
class SceneRefreshCoordinator;
class ViewportInputRouter;
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
    class SyEntity;
}

/**
 * @brief 2D 渲染视口 — 基于 Renderx 的 OpenGL 渲染
 *
 * 取代旧的 Viewport2D (QGraphicsView)，使用 RenderWidget + Renderx 进行硬件加速渲染。
 * 数据层通过 ISceneDataSource 接口推送几何原语，渲染层自行处理细分和缓存。
 *
 * 输入路由委托给 ViewportInputRouter（P5 大文件收口）。
 */
class RenderViewport2D : public QWidget
    , public UI::IViewportHost       // P1: 实现 2D/3D 公共视口宿主接口
{
    Q_OBJECT
public:
    explicit RenderViewport2D(QWidget* parent = nullptr);
    ~RenderViewport2D() override;

public:
    // ==================== 外部接口（与 Viewport2D 兼容）====================

    // ==================== UI::IViewportHost 接口实现 ====================
    int viewportWidth() const override
    {
        return width();
    }
    int viewportHeight() const override
    {
        return height();
    }
    double devicePixelRatio() const override
    {
        return devicePixelRatioF();
    }
    QWidget* viewportWidget() const override
    {
        return const_cast<RenderViewport2D*>(this);
    }
    UI::ViewportDimension dimension() const override
    {
        return UI::ViewportDimension::Dim2D;
    }

    void setStatusCallback(std::function<void(const QString&)> callback);
    void setSelectionCallback(std::function<void(const QString&, const QString&)> callback);
    void setCommandStageCallback(std::function<void(const QString&)> callback);
    // 设置鼠标位置回调，用于在状态栏显示当前光标坐标
    void setPositionCallback(std::function<void(double, double)> callback);

    void setDocument(SceneDocument2D* document);

    // ==================== 工具系统接口 ====================
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

    /// 在 native window 销毁前显式释放 OpenGL 资源，避免析构时访问无效句柄崩溃
    void releaseGLResources();

    void resetView();
    void zoomToFit();
    void zoomToSelection();
    void zoomIn();
    void zoomOut();
    // 刷新 API（P5 语义统一：三级刷新，语义明确）
    /// 轻量重绘 — 纯视觉刷新，不触碰渲染数据（选择变化、光标移动等）
    void requestRepaint();
    /// 增量刷新 — 提交脏/删除图元到渲染设备（图元修改、少量增删后）
    void requestLightRefresh();
    /// 全量刷新 — 完整 gather + submit（导入、大批量修改、文档加载后）
    void requestFullRefresh();
    void setPanModeEnabled(bool enabled);
    bool isPanModeEnabled() const;
    void setDrawingEnabled(bool enabled);
    void setMeasureMode(bool enabled);

    // 选择/编辑操作（P5 已下沉：选择管理 → ViewportSelector，编辑 → SceneEditService）
    QString selectedEntityId() const;
    void deleteSelectedEntity();
    void nudgeSelectedEndpoint(const QPointF& delta);
    void selectEntityById(const QString& entityId);
    void syncSelectionDetails();
    void clearSelection();

    // 坐标转换
    QPointF mapToScene(const QPoint& screenPos) const;
    /// 将 RenderWidget 本地坐标转换为世界坐标（物理像素 → 相机反算）
    QPointF widgetToWorld(QPoint widgetLocalPos) const;

signals:
    void sceneChanged();
    // P1: 视口不直接持有编辑服务，通过信号通知上层
    void entitySubmitRequested(Eg::SyEntity* entity);
    void nudgeRequested(double dx, double dy);
    // 活动工具切换成功时发出，供工具栏等上层同步按钮高亮状态
    void activeToolChanged(const QString& toolName);

protected:
    void resizeEvent(QResizeEvent* event) override;
    // 以下事件委托给 ViewportInputRouter（P5 大文件收口）
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void showEvent(QShowEvent* event) override;
    bool eventFilter(QObject* obj, QEvent* event) override;

private:
    // ==================== 内部方法 ====================

    // 初始化
    void initRenderWidget();

    // 视图控制
    void updateViewMatrix();
    /// 将相机矩阵提交给渲染控件并标记场景环境为脏
    void applyCameraToWidget();

    // 获取物理像素视口尺寸（与 GPU 渲染一致）
    QSizeF physicalViewportSize() const;

    // 辅助
    void updateStatus(const QString& text);
    void syncStatusMode(const QString& text);
    void syncCommandStage(const QString& text);
    void syncSelectionCallback(const QString& source, const QString& text);
    void syncSelectionToolState();

    Eg::SceneManager* sceneManager() const;

    // 连接输入路由器的依赖
    void wireInputRouter();
    void syncInputRouterCallbacks();

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

    // 刷新协调器（四级刷新策略 + 增量渲染管线）
    std::unique_ptr<SceneRefreshCoordinator> m_refreshCoordinator;

    // 输入路由器（P5 大文件收口：从 RenderViewport2D 中抽取事件分发逻辑）
    std::unique_ptr<ViewportInputRouter> m_inputRouter;
};