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
#include "Engine2D/Interaction/GridSnapManager.h"  // 网格+对象捕捉管理器（本视口持有）

class RenderWidget;
class SceneDocument2D;
class ISelectionService;
class IInteractionDispatcher;
class OperationBus;
class ToolManager;
class ITool;
class SceneRefreshCoordinator;
class ViewportInputRouter;
class LayerManager;
struct ToolContext;

namespace Ui2D
{
    class ViewRenderCoordinator;
}

class QMouseEvent;
class QWheelEvent;
class QContextMenuEvent;
class QKeyEvent;
class QTimer;
class QResizeEvent;

namespace Eg
{
    class SceneManager;
    struct SyEntity;
}  // namespace Eg

/**
 * @brief 2D 渲染视口 — 基于 Renderx 的 OpenGL 渲染
 *
 * 取代旧的 Viewport2D (QGraphicsView)，使用 RenderWidget + Renderx 进行硬件加速渲染。
 * 数据层通过 ISceneDataSource 接口推送几何原语，渲染层自行处理细分和缓存。
 *
 * 输入路由委托给 ViewportInputRouter（P5 大文件收口）。
 */
class RenderViewport2D : public QWidget, public UI::IViewportHost  // P1: 实现 2D/3D 公共视口宿主接口
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

    /**
     * @brief ESC 请求入口（供工作台的窗口级快捷键调用）
     *
     * 返回 true 表示视口已消费（绘制中丢弃图元 / 绘图工具退回选择工具）；
     * 返回 false 表示视口不管，由上层清空选择。实现在 ViewportInputRouter，
     * 这里只转发 —— ESC 的分级语义只有一份。
     */
    bool handleEscapeRequest();

    /**
     * @brief Delete / Backspace 请求入口（供工作台的应用级快捷键调用）
     *
     * 返回 true 表示绘制中已回退一个落点，上层**不要**再删除选中图元。
     * 实现在 ViewportInputRouter，这里只转发。
     */
    bool handleStepBackRequest();

    /**
     * @brief 文本编辑态的 ⌫ / ⌦：删选区，或删光标前／后一个字符
     *
     * @param forward false = ⌫（删光标前）；true = ⌦（删光标后）
     * @return true 表示已消费，上层**不要**再删除选中图元。
     * 实现在 ViewportInputRouter，这里只转发。
     */
    bool handleTextDeleteRequest(bool forward);

    /**
     * @brief 文本编辑态的 ⌘Z / ⇧⌘Z：在会话内的快照栈上撤销/重做
     *
     * @param redo false = 撤销；true = 重做
     * @return true 表示已消费，上层**不要**再跑全局 Undo。
     * 实现在 ViewportInputRouter，这里只转发。
     */
    bool handleTextUndoRequest(bool redo);


    SceneDocument2D* document() const
    {
        return m_document;
    }

    void setSelectionService(ISelectionService* service);
    void setInteractionDispatcher(IInteractionDispatcher* dispatcher);
    void setOperationBus(OperationBus* bus);
    /// 注入图层管理器，供选择工具过滤锁定图层
    void setLayerManager(LayerManager* manager);

    /// 获取内部捕捉管理器（网格+对象捕捉）。用于向设置对话框/输入路由暴露捕捉能力。
    GridSnapManager* gridSnapManager() const
    {
        return m_gridSnapManager.get();
    }

    /// 在 native window 销毁前显式释放 OpenGL 资源，避免析构时访问无效句柄崩溃
    void releaseGLResources();

    /// 获取内部渲染控件（用于场景环境参数的读取/回写）
    RenderWidget* renderWidget() const
    {
        return m_renderWidget;
    }

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

    // 选择状态广播（选择的读写在 SelectTool / ISelectionService，视口只负责通知上层）
    // 注：selectedEntityId / selectEntityById / clearSelection / nudgeSelectedEndpoint
    // 已删除（2026-08-30）—— 全部零调用方，且各自都是第二套实现：选择走 SelectTool +
    // ISelectionService，删除走 OperationBus 的 Edit_Delete，微调走 Edit_Nudge。
    void syncSelectionDetails();

    // 坐标转换
    QPointF mapToScene(const QPoint& screenPos) const;
    /// 将 RenderWidget 本地坐标转换为世界坐标（物理像素 → 相机反算）
    QPointF widgetToWorld(QPoint widgetLocalPos) const;
    /// 将全局屏幕坐标转换为世界坐标（用于拖放等跨控件定位）
    QPointF mapGlobalToScene(const QPoint& globalPos) const;

    /// 粘贴锚点：鼠标在视口内则取鼠标世界坐标，否则取视口中心世界坐标
    QPointF pasteAnchorWorld() const;

signals:
    void sceneChanged();
    // P1: 视口不直接持有编辑服务，通过信号通知上层
    void entitySubmitRequested(Eg::SyEntity* entity);
    // 活动工具切换成功时发出，供工具栏等上层同步按钮高亮状态
    void activeToolChanged(const QString& toolName);
    // 场景选择状态变化（含绘制后自动选中、点选/框选、撤销等所有路径），供上层刷新命令可用性
    void selectionChanged();
    // 右键菜单请求：由 UI 层（Workbench）构建并弹出菜单，使右键联动统一走命令中枢
    void contextMenuRequested(QContextMenuEvent* event);

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

    // 捕捉辅助：对世界坐标应用吸附（图元/网格/起点），并刷新捕捉指示器
    QPointF applySnap(const QPointF& worldPos) const;

    // 辅助
    void updateStatus(const QString& text);
    void syncStatusMode(const QString& text);
    void syncCommandStage(const QString& text);
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

    /// 上次重建选中轮廓时所用的 pixelToWorld 比例（0 = 尚未记录）。
    /// 轮廓离散密度是像素基准的（见 SelectionOutlineBuilder），缩放跨过一定倍数后
    /// 必须重建：否则放大看虚线会显棱角，缩小则白白背着过密的顶点。
    float m_outlineScaleAtBuild{ 0.0f };

    // 文档和服务
    SceneDocument2D* m_document{ nullptr };
    Eg::SceneManager* m_sceneManager{ nullptr };
    std::shared_ptr<bool> m_alive{ std::make_shared<bool>(true) };
    ISelectionService* m_selectionService{ nullptr };
    IInteractionDispatcher* m_interactionDispatcher{ nullptr };
    OperationBus* m_operationBus{ nullptr };
    LayerManager* m_layerManager{ nullptr };

    // 工具系统
    std::unique_ptr<ToolManager> m_toolManager;

    // 选中集包围盒查询器（只服务 zoom_selection 等视图操作）
    std::unique_ptr<ViewportSelector> m_selector;

    // 回调
    std::function<void(const QString&)> m_statusCallback;
    std::function<void(const QString&)> m_commandStageCallback;
    // 鼠标位置回调，参数为世界坐标 (x, y)
    std::function<void(double, double)> m_positionCallback;

    // 刷新协调器（四级刷新策略 + 增量渲染管线）
    std::unique_ptr<SceneRefreshCoordinator> m_refreshCoordinator;

    // 输入路由器（P5 大文件收口：从 RenderViewport2D 中抽取事件分发逻辑）
    std::unique_ptr<ViewportInputRouter> m_inputRouter;

    // 网格+对象捕捉管理器（引擎层纯计算门面，非 UI 拥有，便于移植复用）
    std::unique_ptr<GridSnapManager> m_gridSnapManager;

    // 渲染协调器（覆盖层/捕捉指示器桥接；由 initializeTools 创建）
    std::unique_ptr<Ui2D::ViewRenderCoordinator> m_renderCoordinator;
};