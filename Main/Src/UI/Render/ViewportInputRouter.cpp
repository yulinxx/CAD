/**
 * @file ViewportInputRouter.cpp
 * @brief 2D 视口输入路由实现 — 事件分发、平移、键盘处理
 *
 * P5 大文件收口 (2026-07-30)
 */
#include "ViewportInputRouter.h"
#include "RenderWidget.h"
#include "Camera2D.h"
#include "SceneRefreshCoordinator.h"
#include "ViewportSelector.h"
#include "ISelectionService.h"
#include "UiInteractionDispatcher.h"
#include "SceneDocument2D.h"

#include "UI2D/Operation/OperationBus.h"
#include "UI2D/Operation/OperationId.h"

#include "Engine2D/Core/SceneManager.h"
#include "Engine2D/Edit/SceneEditService.h"

#include "UI/DrawTools/ToolManager.h"
#include "UI/DrawTools/ITool.h"

#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QInputMethodEvent>
#include <QContextMenuEvent>
#include <QNativeGestureEvent>
#include <QVariant>
#include <QRectF>
#include <cmath>
#include "Log/SyLogger.h"

// ==================== ViewportInputRouter 实现 ====================

ViewportInputRouter::ViewportInputRouter(QObject* parent)
    : QObject(parent)
{
}

ViewportInputRouter::~ViewportInputRouter() = default;

// ==================== 依赖注入 ====================

void ViewportInputRouter::setRenderWidget(RenderWidget* widget)
{
    m_renderWidget = widget;

    // 注入输入法查询处理器：RenderWidget 的 inputMethodQuery 委托至此
    // 覆盖 Windows/IM、macOS/NSTextInputClient、Linux/IBus-Fcitx 需要的全部查询
    if (widget)
    {
        widget->setInputMethodQueryHandler([this](Qt::InputMethodQuery query) -> QVariant {
            switch (query)
            {
            case Qt::ImEnabled:
                return true;
            case Qt::ImCursorRectangle:
                return inputMethodCursorRect();
            case Qt::ImFont:
                if (m_toolManager && m_toolManager->getActiveTool())
                {
                    const QFont f = m_toolManager->getActiveTool()->inputMethodFont();
                    if (!f.family().isEmpty())
                    {
                        return f;
                    }
                }
                return QVariant();
            case Qt::ImSurroundingText:
                if (m_toolManager && m_toolManager->getActiveTool())
                {
                    return m_toolManager->getActiveTool()->inputMethodSurroundingText();
                }
                return QVariant();
            case Qt::ImCursorPosition:
                if (m_toolManager && m_toolManager->getActiveTool())
                {
                    const int pos = m_toolManager->getActiveTool()->inputMethodCursorPos();
                    if (pos >= 0)
                    {
                        return pos;
                    }
                }
                return QVariant();
            case Qt::ImAnchorPosition:
                if (m_toolManager && m_toolManager->getActiveTool())
                {
                    const int pos = m_toolManager->getActiveTool()->inputMethodAnchorPos();
                    if (pos >= 0)
                    {
                        return pos;
                    }
                }
                return QVariant();
            default:
                return QVariant();
            }
        });

        // 注入输入法事件处理器：中文组字/上屏事件从 RenderWidget 转发至活动工具。
        // 这是 IME 生效的关键链路（不覆写 inputMethodEvent 则事件被 Qt 丢弃）。
        widget->setInputMethodEventHandler([this](QInputMethodEvent* event) -> bool {
            return handleInputMethodEvent(event);
        });
    }
}

void ViewportInputRouter::setCamera(Camera2D* camera)
{
    m_camera = camera;
}

void ViewportInputRouter::setToolManager(ToolManager* tm)
{
    m_toolManager = tm;
}

void ViewportInputRouter::setSelector(ViewportSelector* selector)
{
    m_selector = selector;
}

void ViewportInputRouter::setInteractionDispatcher(IInteractionDispatcher* dispatcher)
{
    m_interactionDispatcher = dispatcher;
}

void ViewportInputRouter::setSelectionService(ISelectionService* service)
{
    m_selectionService = service;
}

void ViewportInputRouter::setOperationBus(OperationBus* bus)
{
    m_operationBus = bus;
}

void ViewportInputRouter::setDocument(SceneDocument2D* document)
{
    m_document = document;
}

void ViewportInputRouter::setRefreshCoordinator(SceneRefreshCoordinator* coordinator)
{
    m_refreshCoordinator = coordinator;
}

void ViewportInputRouter::setPositionCallback(std::function<void(double, double)> callback)
{
    m_positionCallback = std::move(callback);
}

void ViewportInputRouter::setStatusCallback(std::function<void(const QString&)> callback)
{
    m_statusCallback = std::move(callback);
}

void ViewportInputRouter::setCameraChangedCallback(std::function<void()> callback)
{
    m_cameraChangedCallback = std::move(callback);
}

// ==================== 事件过滤器 ====================

bool ViewportInputRouter::eventFilter(QObject* obj, QEvent* event)
{
    if (obj != m_renderWidget || !m_renderWidget)
    {
        return QObject::eventFilter(obj, event);
    }

    switch (event->type())
    {
    case QEvent::MouseButtonDblClick:
    case QEvent::MouseButtonPress:
    case QEvent::MouseMove:
    case QEvent::MouseButtonRelease:
    {
        // RenderWidget 的事件已经处于自身坐标系，直接按视口本地坐标处理即可
        switch (event->type())
        {
        case QEvent::MouseButtonDblClick:
            handleMouseDoubleClick(static_cast<QMouseEvent*>(event));
            break;
        case QEvent::MouseButtonPress:
            handleMousePress(static_cast<QMouseEvent*>(event));
            break;
        case QEvent::MouseMove:
            handleMouseMove(static_cast<QMouseEvent*>(event));
            break;
        case QEvent::MouseButtonRelease:
            handleMouseRelease(static_cast<QMouseEvent*>(event));
            break;
        default:
            break;
        }
        return true;
    }
    case QEvent::Wheel:
    {
        auto* we = static_cast<QWheelEvent*>(event);
        handleWheel(we);
        return true;
    }
    case QEvent::NativeGesture:
    {
        // macOS 触控板捏合以 NativeGesture 形式送达（非 Ctrl+Wheel），跨平台统一在此转缩放。
        auto* ge = static_cast<QNativeGestureEvent*>(event);
        handleNativeGesture(ge);
        return true;
    }
    case QEvent::KeyPress:
    {
        auto* ke = static_cast<QKeyEvent*>(event);
        handleKeyPress(ke);
        return true;
    }
    case QEvent::KeyRelease:
    {
        auto* ke = static_cast<QKeyEvent*>(event);
        handleKeyRelease(ke);
        return true;
    }
    case QEvent::InputMethod:
    {
        auto* ime = static_cast<QInputMethodEvent*>(event);
        // 输入法事件：活动工具消费则拦截，否则忽略交由系统默认（无效果）
        return handleInputMethodEvent(ime);
    }
    case QEvent::ContextMenu:
    {
        auto* ce = static_cast<QContextMenuEvent*>(event);
        handleContextMenu(ce);
        return true;
    }
    default:
        break;
    }

    return QObject::eventFilter(obj, event);
}

// ==================== 事件处理器 ====================

void ViewportInputRouter::handleMousePress(QMouseEvent* event)
{
    QPointF worldPos;
    QPoint widgetPos;
    QPoint physWidgetPos;
    if (!mouseEventToWorld(event, worldPos, widgetPos, physWidgetPos))
    {
        return;
    }

    if (handlePanMousePress(physWidgetPos, event))
    {
        return;
    }

    if (dispatchMousePressToInput(worldPos, event))
    {
        return;
    }

    event->ignore();
}

void ViewportInputRouter::handleMouseMove(QMouseEvent* event)
{
    QPointF worldPos;
    QPoint widgetPos;
    QPoint physWidgetPos;
    if (!mouseEventToWorld(event, worldPos, widgetPos, physWidgetPos))
    {
        return;
    }

    // 记录最近鼠标世界坐标，供粘贴等操作作为锚点（即使之后鼠标移出视口）
    m_lastCursorWorldPos = worldPos;
    m_hasCursorPos = true;

    if (m_positionCallback)
    {
        m_positionCallback(worldPos.x(), worldPos.y());
    }

    // 空格按住时：任意鼠标/触控板单指移动即进入临时平移（无需按下按键）。
    // 触控板单指移动是纯移动事件（无按键），与 AutoCAD "按住空格拖动" 手感一致。
    if (m_spaceHeld && !m_panning)
    {
        m_panning = true;
        m_spacePanned = true;
        m_lastMousePos = physWidgetPos;
    }

    if (handlePanMouseMove(physWidgetPos, event))
    {
        return;
    }

    if (dispatchMouseMoveToInput(worldPos, event))
    {
        return;
    }

    event->ignore();
}

void ViewportInputRouter::handleMouseRelease(QMouseEvent* event)
{
    QPointF worldPos;
    QPoint widgetPos;
    QPoint physWidgetPos;
    if (!mouseEventToWorld(event, worldPos, widgetPos, physWidgetPos))
    {
        return;
    }

    if (handlePanMouseRelease(event))
    {
        return;
    }

    if (dispatchMouseReleaseToInput(worldPos, event))
    {
        return;
    }

    event->ignore();
}

void ViewportInputRouter::handleMouseDoubleClick(QMouseEvent* event)
{
    if (!m_renderWidget || !event)
    {
        return;
    }

    QPointF worldPos = widgetToWorld(event->pos().toPointF());
    if (worldPos.isNull())
    {
        return;
    }

    if (event->button() == Qt::LeftButton)
    {
        if (m_toolManager && m_toolManager->getActiveTool())
        {
            m_toolManager->getActiveTool()->onMouseDoubleClick(worldPos, event);
            event->accept();
            return;
        }
    }

    event->ignore();
}

void ViewportInputRouter::handleWheel(QWheelEvent* event)
{
    if (!m_renderWidget || !m_camera || !event)
    {
        return;
    }

    QPointF worldPos = widgetToWorld(event->position());
    if (worldPos.isNull())
    {
        return;
    }

    QSizeF physSize = physicalViewportSize();
    float vpW = static_cast<float>(physSize.width());
    float vpH = static_cast<float>(physSize.height());

    const QPoint angleDelta = event->angleDelta();
    const QPointF pixelDelta = event->pixelDelta();
    const Qt::ScrollPhase phase = event->phase();

    switch (classifyWheel(angleDelta, pixelDelta, event->modifiers(), phase))
    {
    case WheelGestureType::Zoom:
    {
        // 触控板捏合（Ctrl+滚动阶段事件）：按增量比例缩放，平滑连续；
        // 普通鼠标滚轮（含 Ctrl/Shift 修饰）：保持既有"每格 10%"缩放不变。
        float factor;
        if (phase != Qt::NoScrollPhase && event->modifiers().testFlag(Qt::ControlModifier))
        {
            factor = 1.0f + static_cast<float>(angleDelta.y()) / 1200.0f;
        }
        else
        {
            factor = (angleDelta.y() > 0) ? 1.1f : 0.9f;
        }
        m_camera->zoomIn(factor, worldPos, vpW, vpH);
        break;
    }
    case WheelGestureType::Pan:
        // 触控板双指拖动 → 平移视图（方向与既有鼠标平移一致）
        m_camera->pan(static_cast<float>(pixelDelta.x()) / m_camera->zoomX,
            -static_cast<float>(pixelDelta.y()) / m_camera->zoomY);
        break;
    case WheelGestureType::HorizontalPan:
    {
        // 触控板 Shift+双指 → 水平平移
        const double scrollValue = (angleDelta.y() != 0)
            ? static_cast<double>(angleDelta.y())
            : (pixelDelta.isNull() ? static_cast<double>(angleDelta.x())
                                   : static_cast<double>(pixelDelta.y()));
        m_camera->pan(static_cast<float>(scrollValue) / m_camera->zoomX, 0.0f);
        break;
    }
    }

    // 相机参数已变，通知视口提交新矩阵并重绘
    if (m_cameraChangedCallback)
    {
        m_cameraChangedCallback();
    }
    event->accept();
}

// 跨平台滚轮/触控板手势分类：
//  - 无滚动阶段（普通鼠标滚轮，macOS 上鼠标滚轮也带像素增量）→ 一律缩放，
//    不改变既有鼠标操作（无论是否带 Ctrl/Shift）
//  - 触控板（带滚动阶段 ScrollBegin/ScrollUpdate/ScrollMomentum/ScrollEnd）：
//      * Ctrl = 捏合缩放
//      * Shift = 水平平移
//      * 其余 = 平移（双指拖动）
ViewportInputRouter::WheelGestureType ViewportInputRouter::classifyWheel(
    const QPoint& angleDelta, const QPointF& pixelDelta, Qt::KeyboardModifiers modifiers, Qt::ScrollPhase phase)
{
    Q_UNUSED(angleDelta);
    Q_UNUSED(pixelDelta);
    // 鼠标滚轮：保持原有"滚轮=缩放"语义，不因修饰键改变
    if (phase == Qt::NoScrollPhase)
    {
        return WheelGestureType::Zoom;
    }
    // 以下仅针对触控板（带滚动阶段）
    if (modifiers.testFlag(Qt::ControlModifier))
    {
        return WheelGestureType::Zoom;
    }
    if (modifiers.testFlag(Qt::ShiftModifier))
    {
        return WheelGestureType::HorizontalPan;
    }
    return WheelGestureType::Pan;
}

void ViewportInputRouter::handleNativeGesture(QNativeGestureEvent* event)
{
    if (!m_renderWidget || !m_camera || !event)
    {
        return;
    }

    // macOS 触控板捏合：Qt 将 NSEvent.magnification 作为原始增量传入 value()
    // （张开 ≈ +0.05，捏拢 ≈ -0.05），转换为缩放因子：factor = 1 + value
    if (event->gestureType() != Qt::ZoomNativeGesture)
    {
        return;
    }

    const double magnification = event->value();
    const double factor = 1.0 + magnification;
    if (!std::isfinite(factor) || factor < 0.3 || factor > 3.0)
    {
        return;
    }

    QPointF worldPos = widgetToWorld(event->position());
    if (worldPos.isNull())
    {
        return;
    }

    QSizeF physSize = physicalViewportSize();
    float vpW = static_cast<float>(physSize.width());
    float vpH = static_cast<float>(physSize.height());

    m_camera->zoomIn(static_cast<float>(factor), worldPos, vpW, vpH);

    if (m_cameraChangedCallback)
    {
        m_cameraChangedCallback();
    }
    event->accept();
}

void ViewportInputRouter::handleKeyPress(QKeyEvent* event)
{
    if (!event)
    {
        return;
    }

    // 空格按下：进入"临时平移"态，但不立即确认——空格语义（绘图工具确认/SelectTool 重置视图）
    // 延迟到空格释放时触发；若期间发生了空格+左键拖动平移，则释放时不触发（视为导航）。
    if (event->key() == Qt::Key_Space)
    {
        if (event->isAutoRepeat())
        {
            event->accept();
            return;
        }
        m_spaceHeld = true;
        m_spacePanned = false;
        event->accept();
        return;
    }

    if (handleKeyPressDispatch(event))
    {
        return;
    }

    event->ignore();
}

void ViewportInputRouter::handleKeyRelease(QKeyEvent* event)
{
    if (!event || event->key() != Qt::Key_Space)
    {
        return;
    }

    m_spaceHeld = false;

    // 空格单独按下并释放（未用于平移）→ 触发原有确认/重置语义；
    // 用一次合成的空格按下事件走既有分发管线。
    if (!m_spacePanned)
    {
        QKeyEvent spacePress(QEvent::KeyPress, Qt::Key_Space, event->modifiers());
        if (handleKeyPressDispatch(&spacePress))
        {
            event->accept();
            return;
        }
    }

    event->accept();
}

// ==================== 输入法（IME） ====================

bool ViewportInputRouter::handleInputMethodEvent(QInputMethodEvent* event)
{
    if (!m_toolManager)
    {
        return false;
    }
    auto* tool = m_toolManager->getActiveTool();
    if (!tool)
    {
        return false;
    }
    const bool consumed = tool->onInputMethodEvent(event);
    if (consumed)
    {
        SY_DEBUG("[ViewportInputRouter] IME event consumed by active tool");
    }
    return consumed;
}

QRectF ViewportInputRouter::inputMethodCursorRect() const
{
    if (!m_toolManager)
    {
        return QRectF();
    }
    auto* tool = m_toolManager->getActiveTool();
    if (!tool)
    {
        return QRectF();
    }
    const QRectF worldRect = tool->caretWorldRect();
    if (worldRect.isEmpty() || !m_camera || !m_renderWidget)
    {
        return QRectF();
    }

    const float vpW = physicalViewportSize().width();
    const float vpH = physicalViewportSize().height();
    if (vpW <= 0 || vpH <= 0)
    {
        return QRectF();
    }

    // 世界坐标 → NDC（相机视图矩阵）→ 逻辑像素（Widget 本地坐标）
    const Render::Mat3f vm = m_camera->viewMatrix(vpW, vpH);
    const float lw = static_cast<float>(m_renderWidget->width());
    const float lh = static_cast<float>(m_renderWidget->height());
    const auto toScreen = [&vm, lw, lh](const QPointF& w) -> QPointF {
        const float ndcX = vm.at(0, 0) * static_cast<float>(w.x()) + vm.at(0, 1) * static_cast<float>(w.y()) +
            vm.at(0, 2);
        const float ndcY = vm.at(1, 0) * static_cast<float>(w.x()) + vm.at(1, 1) * static_cast<float>(w.y()) +
            vm.at(1, 2);
        return QPointF((ndcX * 0.5f + 0.5f) * lw, (1.0f - (ndcY * 0.5f + 0.5f)) * lh);
    };

    const QPointF tl = toScreen(worldRect.topLeft());
    const QPointF br = toScreen(worldRect.bottomRight());
    return QRectF(tl, br).normalized();
}

void ViewportInputRouter::handleContextMenu(QContextMenuEvent* event)
{
    if (m_interactionDispatcher && m_interactionDispatcher->hasActiveCommand())
    {
        m_interactionDispatcher->cancel();
        event->accept();
        return;
    }
    event->ignore();
}

// ==================== 坐标转换 ====================

QSizeF ViewportInputRouter::physicalViewportSize() const
{
    if (!m_renderWidget)
    {
        return QSizeF(0, 0);
    }
    const float dpr = static_cast<float>(m_renderWidget->devicePixelRatio());
    return QSizeF(m_renderWidget->width() * dpr, m_renderWidget->height() * dpr);
}

QPointF ViewportInputRouter::widgetToWorld(QPointF widgetLocalPos) const
{
    if (!m_renderWidget || !m_camera)
    {
        return QPointF();
    }

    QSizeF physSize = physicalViewportSize();
    float vpW = static_cast<float>(physSize.width());
    float vpH = static_cast<float>(physSize.height());
    if (vpW <= 0 || vpH <= 0)
    {
        return QPointF();
    }

    const float dpr = static_cast<float>(m_renderWidget->devicePixelRatio());
    QPoint physPos(static_cast<int>(widgetLocalPos.x() * dpr), static_cast<int>(widgetLocalPos.y() * dpr));
    return m_camera->screenToWorld(physPos, vpW, vpH);
}

bool ViewportInputRouter::mouseEventToWorld(
    QMouseEvent* event, QPointF& worldPos, QPoint& widgetPos, QPoint& physWidgetPos) const
{
    if (!m_renderWidget || !m_camera || !event)
    {
        return false;
    }

    widgetPos = event->pos();
    const float dpr = static_cast<float>(m_renderWidget->devicePixelRatio());
    physWidgetPos = QPoint(static_cast<int>(widgetPos.x() * dpr), static_cast<int>(widgetPos.y() * dpr));

    QSizeF physSize = physicalViewportSize();
    float vpW = static_cast<float>(physSize.width());
    float vpH = static_cast<float>(physSize.height());
    if (vpW <= 0 || vpH <= 0)
    {
        return false;
    }

    worldPos = m_camera->screenToWorld(physWidgetPos, vpW, vpH);
    return !worldPos.isNull();
}

// ==================== 鼠标事件分发 ====================

bool ViewportInputRouter::dispatchToActiveTool(
    const QPointF& worldPos, QMouseEvent* event, bool (ITool::*handler)(const QPointF&, QMouseEvent*))
{
    if (!m_toolManager)
    {
        return false;
    }

    auto* tool = m_toolManager->getActiveTool();
    if (!tool)
    {
        return false;
    }

    (tool->*handler)(worldPos, event);
    event->accept();
    return true;
}

bool ViewportInputRouter::dispatchToSelectorPress(const QPointF& worldPos, QMouseEvent* event)
{
    if (!m_selector)
    {
        return false;
    }

    m_selector->beginBoxSelect(worldPos);
    event->accept();
    return true;
}

bool ViewportInputRouter::dispatchToSelectorRelease(const QPointF& worldPos, QMouseEvent* event)
{
    if (!m_selector)
    {
        return false;
    }

    m_selector->handleClick(worldPos);
    event->accept();
    return true;
}

bool ViewportInputRouter::dispatchMousePressToInput(const QPointF& worldPos, QMouseEvent* event)
{
    // 右键：取消当前图元（绘制态）或退出工具（空闲态），与 ESC 语义一致
    if (event->button() == Qt::RightButton)
    {
        return dispatchRightButtonPressToInput(worldPos, event);
    }

    if (event->button() != Qt::LeftButton || m_panModeEnabled)
    {
        return false;
    }

    if (m_interactionDispatcher && m_interactionDispatcher->hasActiveCommand() &&
        m_interactionDispatcher->dispatchEvent(
            { InteractionEventType::MouseDown, static_cast<int>(worldPos.x()), static_cast<int>(worldPos.y()), -1 }))
    {
        event->accept();
        return true;
    }

    if (dispatchToActiveTool(worldPos, event, &ITool::onMousePress))
    {
        return true;
    }

    return dispatchToSelectorPress(worldPos, event);
}

// 右键分发：绘制态交给活动工具取消当前图元；空闲态退出工具并切回选择工具（与 ESC 一致）。
// 文档约定（绘图交互 §4.4/§5.5）：右键 = Esc；双击/空格/Enter = 确认；Backspace = 回退。
bool ViewportInputRouter::dispatchRightButtonPressToInput(const QPointF& worldPos, QMouseEvent* event)
{
    if (m_panModeEnabled)
    {
        return false;
    }

    // 交互分发器有活跃命令时，右键视为取消该命令
    if (m_interactionDispatcher && m_interactionDispatcher->hasActiveCommand() &&
        m_interactionDispatcher->dispatchEvent(
            { InteractionEventType::MouseDown, static_cast<int>(worldPos.x()), static_cast<int>(worldPos.y()), -1 }))
    {
        event->accept();
        return true;
    }

    if (m_toolManager)
    {
        auto* tool = m_toolManager->getActiveTool();
        if (tool)
        {
            // 绘制态：BaseTool::onMousePress 处理右键取消当前图元，返回 true 表示已消费
            if (tool->onMousePress(worldPos, event))
            {
                event->accept();
                return true;
            }

            // 空闲态：与 ESC 一致，退出当前绘图工具并切回选择工具
            const QString activeTool = m_toolManager->getActiveToolName();
            if (!activeTool.isEmpty() && activeTool != QStringLiteral("SelectTool"))
            {
                m_toolManager->cancelCurrentTool();
                if (m_operationBus)
                {
                    m_operationBus->run(OperationId::Tool_Select, {}, OperationSource::ContextMenu);
                }
                event->accept();
                return true;
            }
        }
    }

    // SelectTool 空闲态右键：不消费，保留给右键菜单/后续处理
    return false;
}

bool ViewportInputRouter::dispatchMouseMoveToInput(const QPointF& worldPos, QMouseEvent* event)
{
    if (m_interactionDispatcher && m_interactionDispatcher->hasActiveCommand() &&
        m_interactionDispatcher->dispatchEvent(
            { InteractionEventType::MouseMove, static_cast<int>(worldPos.x()), static_cast<int>(worldPos.y()), -1 }))
    {
        event->accept();
        return true;
    }

    return dispatchToActiveTool(worldPos, event, &ITool::onMouseMove);
}

bool ViewportInputRouter::dispatchMouseReleaseToInput(const QPointF& worldPos, QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton)
    {
        return false;
    }

    if (m_interactionDispatcher && m_interactionDispatcher->hasActiveCommand() &&
        m_interactionDispatcher->dispatchEvent(
            { InteractionEventType::MouseUp, static_cast<int>(worldPos.x()), static_cast<int>(worldPos.y()), -1 }))
    {
        event->accept();
        return true;
    }

    if (dispatchToActiveTool(worldPos, event, &ITool::onMouseRelease))
    {
        return true;
    }

    return dispatchToSelectorRelease(worldPos, event);
}

// ==================== 平移处理 ====================

bool ViewportInputRouter::handlePanMousePress(const QPoint& physWidgetPos, QMouseEvent* event)
{
    if (event->button() == Qt::MiddleButton)
    {
        m_panning = true;
        m_lastMousePos = physWidgetPos;
        event->accept();
        return true;
    }
    if (event->button() == Qt::LeftButton && m_panModeEnabled)
    {
        m_panning = true;
        m_lastMousePos = physWidgetPos;
        event->accept();
        return true;
    }
    // 按住空格 + 左键/单指拖动 = 临时平移（保持"空格=确认"在单独释放时语义不变）
    if (event->button() == Qt::LeftButton && m_spaceHeld)
    {
        m_panning = true;
        m_spacePanned = true;
        m_lastMousePos = physWidgetPos;
        event->accept();
        return true;
    }
    return false;
}

bool ViewportInputRouter::handlePanMouseMove(const QPoint& physWidgetPos, QMouseEvent* event)
{
    if (!m_panning || !m_camera)
    {
        return false;
    }

    QPoint delta = physWidgetPos - m_lastMousePos;
    m_lastMousePos = physWidgetPos;
    float worldDx = static_cast<float>(delta.x()) / m_camera->zoomX;
    float worldDy = -static_cast<float>(delta.y()) / m_camera->zoomY;
    m_camera->pan(worldDx, worldDy);
    if (m_cameraChangedCallback)
    {
        m_cameraChangedCallback();
    }
    event->accept();
    return true;
}

bool ViewportInputRouter::handlePanMouseRelease(QMouseEvent* event)
{
    if (event->button() == Qt::MiddleButton || (event->button() == Qt::LeftButton && m_panning))
    {
        m_panning = false;
        event->accept();
        return true;
    }
    return false;
}

// ==================== 键盘事件分发 ====================

bool ViewportInputRouter::handleInteractionDispatcherKeyPress(QKeyEvent* event)
{
    if (!m_interactionDispatcher || !m_interactionDispatcher->hasActiveCommand())
    {
        return false;
    }

    if (event->key() != Qt::Key_Escape && event->key() != Qt::Key_Return && event->key() != Qt::Key_Enter)
    {
        if (m_interactionDispatcher->dispatchEvent({ InteractionEventType::KeyPress, -1, -1, event->key() }))
        {
            event->accept();
            return true;
        }
    }

    if (event->key() == Qt::Key_Escape)
    {
        m_interactionDispatcher->cancel();
        event->accept();
        return true;
    }
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)
    {
        m_interactionDispatcher->submit();
        event->accept();
        return true;
    }
    return false;
}

bool ViewportInputRouter::handleToolKeyPress(QKeyEvent* event)
{
    if (!m_toolManager)
    {
        return false;
    }

    auto* tool = m_toolManager->getActiveTool();
    if (!tool)
    {
        return false;
    }

    if (tool->onKeyPress(event))
    {
        event->accept();
        return true;
    }
    return false;
}

bool ViewportInputRouter::handleDeleteKeyPress(QKeyEvent* event)
{
    if (event->key() != Qt::Key_Delete)
    {
        return false;
    }

    // 删除选中图元：直接走文档的 SceneEditService（P5 下沉）。
    // 旧 OperationBus 的 OperationContext 在生产路径不绑定场景，
    // 经它执行 Edit_Delete 只会被拒绝并刷告警，且必须先删除再清空选择。
    if (m_document && m_document->editService())
    {
        m_document->editService()->deleteSelected("Delete");
    }
    if (m_selectionService)
    {
        m_selectionService->clear();
    }
    // 删除后全量刷新：重建渲染数据，确保已删除图元从 GPU 端清除
    if (m_refreshCoordinator)
    {
        m_refreshCoordinator->requestFullRefresh();
    }
    event->accept();
    return true;
}

bool ViewportInputRouter::handleKeyPressDispatch(QKeyEvent* event)
{
    if (handleInteractionDispatcherKeyPress(event))
    {
        return true;
    }

    if (handleToolKeyPress(event))
    {
        return true;
    }

    if (handleDeleteKeyPress(event))
    {
        return true;
    }

    return handleEscapeKeyPress(event);
}

bool ViewportInputRouter::handleEscapeKeyPress(QKeyEvent* event)
{
    if (event->key() != Qt::Key_Escape)
    {
        return false;
    }

    // 仅当左侧绘图工具栏的工具（非选择工具）处于激活状态时处理：
    // 丢弃当前工具未完成的草图，并切回选择工具。
    const QString activeTool = m_toolManager ? m_toolManager->getActiveToolName() : QString();
    if (activeTool.isEmpty() || activeTool == QStringLiteral("SelectTool"))
    {
        return false;
    }

    if (m_toolManager)
    {
        m_toolManager->cancelCurrentTool();
    }

    // 走 OperationBus：UI 入口 → Tool_Select → 视口激活选择工具 → activeToolChanged → 工具栏高亮同步
    if (m_operationBus)
    {
        m_operationBus->run(OperationId::Tool_Select, {}, OperationSource::Keyboard);
    }

    event->accept();
    return true;
}