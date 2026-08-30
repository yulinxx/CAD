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

#include "ViewportNavigation2D.h"

class QMouseEvent;
class QWheelEvent;
class QNativeGestureEvent;
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
    // 相机变化回调 — 缩放/平移后通知视口更新视图矩阵并重绘（转发到共享导航控制器）
    void setCameraChangedCallback(std::function<void()> callback);

    // 捕捉回调 — 在把世界坐标分发给活动工具前统一应用（图元/网格/起点吸附）。
    // 通过函数注入而非直接依赖 GridSnapManager，保持输入路由低耦合、便于移植与测试。
    void setSnapPositionCallback(std::function<QPointF(const QPointF&)> callback);

    // ==================== 事件过滤器（转发 RenderWidget 事件到视口） ====================

    bool eventFilter(QObject* obj, QEvent* event) override;

    // ==================== 事件处理器（由 RenderViewport2D 委托） ====================

    void handleMousePress(QMouseEvent* event);
    void handleMouseMove(QMouseEvent* event);
    void handleMouseRelease(QMouseEvent* event);
    void handleMouseDoubleClick(QMouseEvent* event);
    void handleWheel(QWheelEvent* event);
    void handleNativeGesture(QNativeGestureEvent* event);
    void handleKeyPress(QKeyEvent* event);
    void handleKeyRelease(QKeyEvent* event);
    void handleContextMenu(QContextMenuEvent* event);

    /**
     * @brief ESC 的唯一语义实现（分级）
     *
     * 一级：活动工具正在绘制 → 丢弃当前图元，**留在该工具**；
     * 二级：绘图工具空闲 → 经 OperationBus 退回选择工具；
     * 三级：已在选择工具 → 返回 false，由上层清空选择。
     *
     * 必须对外可见：ESC 实际是工作台的窗口级 QShortcut（macOS 下 QOpenGLWidget
     * 不自动获焦，键事件到不了视口，快捷键会先把 ESC 吃掉），因此「快捷键」与
     * 「键盘路由」两条来路共用这一份实现，不允许各写一套。
     */
    bool handleEscapeRequest();

    /**
     * @brief Delete / Backspace 的第一优先级：绘制中回退一个已确定的落点
     *
     * 返回 true 表示已被绘图工具消费，上层**不要**再删除选中图元。
     *
     * 必须对外可见的理由与 handleEscapeRequest 相同，且更硬：Delete / Backspace 是
     * 工作台的 Qt::ApplicationShortcut，按键在送达视口 widget 之前就被快捷键系统吃掉，
     * `BaseTool::onKeyPress` 里的回退分支在绘图态从来没被走到过。
     *
     * 与主 Undo 栈无关：只回退当前正在构造的图元的输入点，已提交图元不受影响。
     */
    bool handleStepBackRequest();

    /**
     * @brief 文本编辑态的 ⌫ / ⌦：删选区，或删光标前／后一个字符
     *
     * @param forward false = ⌫（删光标前）；true = ⌦（删光标后）
     * @return true 表示已消费，上层**不要**再删除选中图元
     *
     * 成因与 handleStepBackRequest 完全相同：Delete / Backspace 是应用级快捷键，
     * 按键在送达视口 widget 之前就被吃掉，`TextEditTool::onKeyPress` 的
     * Key_Backspace / Key_Delete 分支从来没被走到过 —— 不补这一跳，在画布上
     * 编辑文字时按 ⌫ 会把整个文字群组删掉，而不是删一个字符。
     */
    bool handleTextDeleteRequest(bool forward);

    /**
     * @brief 文本编辑态的 ⌘Z / ⇧⌘Z：在会话内的快照栈上撤销/重做
     *
     * @param redo false = 撤销；true = 重做
     * @return true 表示已消费，调用方**不要**再跑全局 Undo
     *
     * 与 handleTextDeleteRequest 同因：⌘Z 归菜单/工作台的应用级快捷键，按键送不到视口。
     * 一整个编辑会话在全局栈上只有一条 Undo，会话中直落全局栈会把上一个整体操作退掉。
     */
    bool handleTextUndoRequest(bool redo);


    /// 是否存在正在进行的编辑/绘制命令（视口右键时用于决定是否取消当前命令而非弹菜单）
    bool hasActiveCommand() const;

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
    ///  - 无滚动阶段(普通鼠标滚轮, macOS 上鼠标滚轮也带像素增量) = 缩放，不改变鼠标既有操作
    ///  - 触控板(带滚动阶段 ScrollBegin/Update/Momentum/End)：
    ///    * Ctrl = 捏合缩放
    ///    * Shift = 水平平移
    ///    * 其余 = 平移（双指拖动）
    static WheelGestureType classifyWheel(
        const QPoint& angleDelta, const QPointF& pixelDelta, Qt::KeyboardModifiers modifiers, Qt::ScrollPhase phase);

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

    /// 按物理像素程序化平移视图（文字拖选到视口外的自动平移走这条路）。
    /// 刻意不动 m_panning：那个标志表示「用户正在拖拽平移」，自动平移不是拖拽会话，
    /// 置上它会让随后的 MouseRelease 被当成平移收尾吃掉。
    void panViewByPhysicalPixels(double dxPhys, double dyPhys)
    {
        m_navigation.panByPixels(dxPhys, dyPhys);
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

    // ==================== 捕捉辅助 ====================

    /// 对世界坐标统一应用吸附（图元/网格/起点）。无回调或未命中时返回原坐标。
    QPointF applySnap(const QPointF& worldPos) const;

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
    // 捕捉回调：分发到活动工具前对世界坐标应用吸附（图元/网格/起点）
    std::function<QPointF(const QPointF&)> m_snapPositionCallback;

    // 共享导航控制器：手势→相机的单一实现（本路由与独立预览窗口共用）
    ViewportNavigation2D m_navigation;
};