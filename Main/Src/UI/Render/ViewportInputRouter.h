/**
 * @file ViewportInputRouter.h
 * @brief 2D 视口输入路由 — 从 RenderViewport2D 中抽取的事件分发逻辑
 *
 * 职责：
 *   - RenderWidget → 视口之间的事件转发（eventFilter）
 *   - 鼠标事件三级优先级分发：平移 → 交互分发器 → 活动工具 → 选择器
 *   - 键盘事件优先级分发：交互分发器 → 活动工具 → Delete → Esc（回到选择工具）
 *   - 滚轮缩放
 *   - 双击事件转发到活动工具
 *
 * 不负责：
 *   - 渲染（委托给 SceneRefreshCoordinator）
 *   - 视图控制（委托给 RenderViewport2D）
 *   - 工具初始化（委托给 RenderViewport2D）
 *
 * P5 大文件收口 (2026-07-30)
 */
#pragma once

#include <QObject>
#include <QPointF>
#include <QPoint>
#include <functional>

class QMouseEvent;
class QWheelEvent;
class QKeyEvent;
class QInputMethodEvent;
class QContextMenuEvent;
class RenderWidget;
class Camera2D;
class ToolManager;
class ViewportSelector;
class IInteractionDispatcher;
class ISelectionService;
class OperationBus;
class SceneDocument2D;
class SceneRefreshCoordinator;
class ITool;

namespace Eg
{
    class SceneManager;
}

class ViewportInputRouter : public QObject
{
    Q_OBJECT

public:
    explicit ViewportInputRouter(QObject* parent = nullptr);
    ~ViewportInputRouter() override;

    // ==================== 依赖注入 ====================

    void setRenderWidget(RenderWidget* widget);
    void setCamera(Camera2D* camera);
    void setToolManager(ToolManager* tm);
    void setSelector(ViewportSelector* selector);
    void setInteractionDispatcher(IInteractionDispatcher* dispatcher);
    void setSelectionService(ISelectionService* service);
    void setOperationBus(OperationBus* bus);
    void setDocument(SceneDocument2D* document);
    void setRefreshCoordinator(SceneRefreshCoordinator* coordinator);

    // ==================== 回调注入 ====================

    void setPositionCallback(std::function<void(double, double)> callback);
    void setStatusCallback(std::function<void(const QString&)> callback);
    // 相机变化回调 — 缩放/平移后通知视口更新视图矩阵并重绘
    void setCameraChangedCallback(std::function<void()> callback);

    // ==================== 事件过滤器（转发 RenderWidget 事件到视口） ====================

    bool eventFilter(QObject* obj, QEvent* event) override;

    // ==================== 事件处理器（由 RenderViewport2D 委托） ====================

    void handleMousePress(QMouseEvent* event);
    void handleMouseMove(QMouseEvent* event);
    void handleMouseRelease(QMouseEvent* event);
    void handleMouseDoubleClick(QMouseEvent* event);
    void handleWheel(QWheelEvent* event);
    void handleKeyPress(QKeyEvent* event);
    void handleKeyRelease(QKeyEvent* event);
    void handleContextMenu(QContextMenuEvent* event);

    // ==================== 输入法（IME） ====================

    /// 输入法事件转发给活动工具（返回是否被消费）
    bool handleInputMethodEvent(QInputMethodEvent* event);

    /// 输入法光标屏幕矩形（由活动工具的光标世界矩形经相机换算）
    QRectF inputMethodCursorRect() const;

    // ==================== 交互状态 ====================

    // 滚轮/触控板手势分类结果：Zoom(缩放)、Pan(平移)、HorizontalPan(水平平移)
    enum class WheelGestureType
    {
        Zoom,
        Pan,
        HorizontalPan,
    };

    /// 跨平台滚轮/触控板手势分类（纯函数，便于单测）：
    ///  - Ctrl+滚轮  = 触控板捏合缩放（Qt 在 Windows/macOS/Linux 上统一翻译为 Ctrl+Wheel）
    ///  - Shift+滚轮 = 水平平移
    ///  - 带像素增量(触控板双指拖动) = 平移
    ///  - 无像素增量(普通鼠标滚轮)  = 缩放
    static WheelGestureType classifyWheel(const QPoint& angleDelta, const QPointF& pixelDelta, Qt::KeyboardModifiers modifiers);

    bool isPanning() const
    {
        return m_panning;
    }

    bool isPanModeEnabled() const
    {
        return m_panModeEnabled;
    }

    void setPanModeEnabled(bool enabled)
    {
        m_panModeEnabled = enabled;
    }

    /// 空格是否正处于"临时平移"按下状态
    bool isSpaceHeld() const
    {
        return m_spaceHeld;
    }

    // ==================== 坐标转换（P5 收口: RenderViewport2D 也委托至此，消除重复） ====================

    QPointF widgetToWorld(QPointF widgetLocalPos) const;
    QSizeF physicalViewportSize() const;

    /// 最近一次鼠标在视口内的世界坐标（供粘贴等操作作为锚点）
    QPointF lastCursorWorldPos() const
    {
        return m_lastCursorWorldPos;
    }

    bool hasCursorPos() const
    {
        return m_hasCursorPos;
    }

private:
    // ==================== 坐标转换辅助 ====================

    bool mouseEventToWorld(QMouseEvent* event, QPointF& worldPos, QPoint& widgetPos, QPoint& physWidgetPos) const;

    // ==================== 鼠标事件分发 ====================

    bool dispatchMousePressToInput(const QPointF& worldPos, QMouseEvent* event);
    bool dispatchRightButtonPressToInput(const QPointF& worldPos, QMouseEvent* event);
    bool dispatchMouseMoveToInput(const QPointF& worldPos, QMouseEvent* event);
    bool dispatchMouseReleaseToInput(const QPointF& worldPos, QMouseEvent* event);
    bool dispatchToActiveTool(
        const QPointF& worldPos, QMouseEvent* event, bool (ITool::*handler)(const QPointF&, QMouseEvent*));
    bool dispatchToSelectorPress(const QPointF& worldPos, QMouseEvent* event);
    bool dispatchToSelectorRelease(const QPointF& worldPos, QMouseEvent* event);

    // ==================== 平移处理 ====================

    bool handlePanMousePress(const QPoint& physWidgetPos, QMouseEvent* event);
    bool handlePanMouseMove(const QPoint& physWidgetPos, QMouseEvent* event);
    bool handlePanMouseRelease(QMouseEvent* event);

    // ==================== 键盘事件分发 ====================

    bool handleKeyPressDispatch(QKeyEvent* event);
    bool handleInteractionDispatcherKeyPress(QKeyEvent* event);
    bool handleToolKeyPress(QKeyEvent* event);
    bool handleDeleteKeyPress(QKeyEvent* event);
    bool handleEscapeKeyPress(QKeyEvent* event);

    // ==================== 内部状态 ====================

    RenderWidget* m_renderWidget{ nullptr };
    Camera2D* m_camera{ nullptr };
    ToolManager* m_toolManager{ nullptr };
    ViewportSelector* m_selector{ nullptr };
    IInteractionDispatcher* m_interactionDispatcher{ nullptr };
    ISelectionService* m_selectionService{ nullptr };
    OperationBus* m_operationBus{ nullptr };
    SceneDocument2D* m_document{ nullptr };
    SceneRefreshCoordinator* m_refreshCoordinator{ nullptr };

    // 交互状态
    bool m_panning{ false };
    bool m_panModeEnabled{ false };
    QPoint m_lastMousePos;
    // 空格临时平移（按住空格 + 单指/左键拖动 = 平移，空格单独按下释放 = 原有确认/重置语义）
    bool m_spaceHeld{ false };
    bool m_spacePanned{ false };
    // 最近一次鼠标世界坐标（粘贴锚点）
    QPointF m_lastCursorWorldPos{ 0.0, 0.0 };
    bool m_hasCursorPos{ false };

    // 回调
    std::function<void(double, double)> m_positionCallback;
    std::function<void(const QString&)> m_statusCallback;
    std::function<void()> m_cameraChangedCallback;
};