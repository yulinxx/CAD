#include "RenderWidget3DAdapter.h"

#include <QCoreApplication>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QPainter>

#include "Render3D/RenderWidget3D.h"

namespace
{
    Qt::MouseButton toMouseButton(int button)
    {
        return static_cast<Qt::MouseButton>(button);
    }

    Qt::MouseButtons toMouseButtons(int buttons)
    {
        return static_cast<Qt::MouseButtons>(buttons);
    }
}

RenderWidget3DAdapter::RenderWidget3DAdapter() = default;
RenderWidget3DAdapter::~RenderWidget3DAdapter()
{
    shutdown();
}

bool RenderWidget3DAdapter::ensureWidgetCreated()
{
    if (m_renderWidget)
        return true;

    if (!m_parentWidget)
        return false;

    // 这里只负责创建内部 OpenGL 控件，不参与业务状态编排。
    m_renderWidget = std::make_unique<RenderWidget3D>(m_parentWidget);
    m_renderWidget->setMinimumSize(640, 480);
    bindWidgetSignals();
    return true;
}

void RenderWidget3DAdapter::bindWidgetSignals()
{
    if (!m_renderWidget)
        return;

    // 场景选择变化只同步到适配器，再由适配器回调上层。
    QObject::connect(m_renderWidget.get(), &RenderWidget3D::sigSelectionChanged,
        [this](const auto& entities)
        {
            if (!entities.empty() && entities[0])
            {
                m_selectedNodeId = QString::number(entities[0]->id);
                if (m_selectionCallback)
                    m_selectionCallback(m_selectedNodeId);
                emitStatus(QObject::tr("3D selected: %1").arg(m_selectedNodeId)); // 3D 已选中: %1
                return;
            }

            m_selectedNodeId.clear();
            if (m_selectionCallback)
                m_selectionCallback(QString());
            emitStatus(QObject::tr("3D selection cleared")); // 3D 选择已清除
        });

    // 相机变化只转发状态提示，不在这里写任何 UI 业务。
    QObject::connect(m_renderWidget.get(), &RenderWidget3D::sigCameraChanged,
        [this]()
        {
            emitStatus(m_renderWidget ? m_renderWidget->navigationStatusHint() : QStringLiteral("3D ready"));
        });
}

void RenderWidget3DAdapter::emitStatus(const QString& text)
{
    if (m_statusCallback)
        m_statusCallback(text);
}

bool RenderWidget3DAdapter::initialize(void* windowHandle)
{
    // 将窗口句柄转为父控件，适配器内部据此创建 OpenGL 控件
    if (windowHandle)
        m_parentWidget = static_cast<QWidget*>(windowHandle);
    m_ready = ensureWidgetCreated();
    return m_ready;
}

void RenderWidget3DAdapter::shutdown()
{
    // 退出时只释放内部控件，不回调上层，避免 Qt 销毁链与业务链交叉。
    m_renderWidget.reset();
    m_ready = false;
    m_renderLoopEnabled = false;
    m_selectedNodeId.clear();
    m_selectedPathNames.clear();
}

bool RenderWidget3DAdapter::isReady() const
{
    return m_ready && m_renderWidget != nullptr;
}

void RenderWidget3DAdapter::setRenderLoopEnabled(bool enabled)
{
    m_renderLoopEnabled = enabled;
    if (m_renderWidget)
        m_renderWidget->setUpdatesEnabled(enabled);
}

bool RenderWidget3DAdapter::isRenderLoopRunning() const
{
    return m_renderLoopEnabled && m_ready;
}

void RenderWidget3DAdapter::setScene(SceneDocument3D* document)
{
    Q_UNUSED(document);
    // 这里预留 SceneDocument3D -> RenderWidget3D 的场景转换入口。
    // 当前版本先保持适配器可用，但不强行耦合到具体场景管理实现。
}

void RenderWidget3DAdapter::setCamera(CameraController3D* controller)
{
    Q_UNUSED(controller);
    // 这里预留 CameraController3D -> RenderWidget3D 的相机同步入口。
}

void RenderWidget3DAdapter::render(QPainter& painter, int width, int height)
{
    Q_UNUSED(painter);
    Q_UNUSED(width);
    Q_UNUSED(height);

    // RenderWidget3D 自己在 paintGL 中绘制，这里只触发刷新。
    if (m_renderWidget)
        m_renderWidget->update();
}

void RenderWidget3DAdapter::resize(int width, int height)
{
    if (m_renderWidget)
        m_renderWidget->resize(width, height);
}

void RenderWidget3DAdapter::resetView()
{
    if (m_renderWidget)
        m_renderWidget->resetView();
}

void RenderWidget3DAdapter::setOrbitMode(bool enabled)
{
    Q_UNUSED(enabled);
    // 导航模式由内部 RenderWidget3D 自己管理，适配器只保留接口一致性。
}

void RenderWidget3DAdapter::setMeasureMode(bool enabled)
{
    Q_UNUSED(enabled);
}

bool RenderWidget3DAdapter::isOrbitMode() const
{
    return true;
}

void RenderWidget3DAdapter::onMousePress(int x, int y, int button, int modifiers, int viewW, int viewH)
{
    Q_UNUSED(viewW);
    Q_UNUSED(viewH);
    if (!m_renderWidget)
        return;

    QMouseEvent event(QEvent::MouseButtonPress, QPoint(x, y),
        toMouseButton(button), toMouseButton(button), static_cast<Qt::KeyboardModifiers>(modifiers));
    QCoreApplication::sendEvent(m_renderWidget.get(), &event);
}

void RenderWidget3DAdapter::onMouseMove(int x, int y, int buttons, int viewW, int viewH)
{
    Q_UNUSED(viewW);
    Q_UNUSED(viewH);
    if (!m_renderWidget)
        return;

    QMouseEvent event(QEvent::MouseMove, QPoint(x, y),
        Qt::NoButton, toMouseButtons(buttons), Qt::NoModifier);
    QCoreApplication::sendEvent(m_renderWidget.get(), &event);
}

void RenderWidget3DAdapter::onMouseRelease(int x, int y, int button, int viewW, int viewH)
{
    Q_UNUSED(viewW);
    Q_UNUSED(viewH);
    if (!m_renderWidget)
        return;

    QMouseEvent event(QEvent::MouseButtonRelease, QPoint(x, y),
        toMouseButton(button), Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(m_renderWidget.get(), &event);
}

void RenderWidget3DAdapter::onWheel(int delta, int viewW, int viewH)
{
    Q_UNUSED(viewW);
    Q_UNUSED(viewH);
    if (!m_renderWidget)
        return;

    // 这里沿用现有 wheel 事件转发，后续可以再切换成更明确的视图输入模型。
    QWheelEvent event(QPoint(0, 0), QPoint(delta, 0),
        QPoint(delta, 0), QPoint(), Qt::NoButton, Qt::NoModifier, Qt::ScrollBegin, false);
    QCoreApplication::sendEvent(m_renderWidget.get(), &event);
}

void RenderWidget3DAdapter::selectNodeById(const QString& nodeId)
{
    m_selectedNodeId = nodeId;
    if (m_selectionCallback)
        m_selectionCallback(nodeId);
    emitStatus(QObject::tr("3D selected: %1").arg(nodeId));
}

QString RenderWidget3DAdapter::selectedNodeId() const
{
    return m_selectedNodeId;
}

QStringList RenderWidget3DAdapter::selectedPathNames() const
{
    return m_selectedPathNames;
}

void RenderWidget3DAdapter::setStatusCallback(StatusCallback callback)
{
    m_statusCallback = std::move(callback);
}

void RenderWidget3DAdapter::setSelectionCallback(SelectionCallback callback)
{
    m_selectionCallback = std::move(callback);
}

void RenderWidget3DAdapter::setPathCallback(PathCallback callback)
{
    m_pathCallback = std::move(callback);
}

RenderWidget3D* RenderWidget3DAdapter::widget() const
{
    return m_renderWidget.get();
}