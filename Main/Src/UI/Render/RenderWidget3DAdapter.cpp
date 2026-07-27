#include "RenderWidget3DAdapter.h"

#include <QCoreApplication>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QPainter>

#include "Render3D/RenderWidget3D.h"
#include "Engine3D/SyEntity/SyMeshEntity.h"
#include "Engine3D/SceneManager3D.h"
#include "Engine3D/Selection/SelectionManager3D.h"
#include "UI3D/Service/SceneDocument3D.h"
#include "UI3D/Service/CameraController3D.h"
#include "Log/SyLogger.h"

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

    // 创建内部 OpenGL 控件，直接设置为父窗口的子控件
    // 这样 RenderWidget3D 会直接接收 Qt 事件，避免通过 sendEvent 转发导致的死循环
    m_renderWidget = std::make_unique<RenderWidget3D>(m_parentWidget);
    m_renderWidget->setMinimumSize(640, 480);
    m_renderWidget->setGeometry(m_parentWidget->rect());
    m_renderWidget->setParent(m_parentWidget);
    m_renderWidget->show();
    bindWidgetSignals();
    SY_INFO("[RenderWidget3DAdapter] RenderWidget3D created as child widget");
    return true;
}

void RenderWidget3DAdapter::bindWidgetSignals()
{
    if (!m_renderWidget)
        return;

    // 场景选择变化只同步到适配器，再由适配器回调上层。
    QObject::connect(m_renderWidget.get(), &RenderWidget3D::sigSelectionChanged,
        [this](const std::vector<Eg::SyMeshEntity*>& entities)
        {
            if (!entities.empty() && entities[0])
            {
                m_selectedNodeId = QString::number(entities[0]->id);

                // 同步路径名称
                m_selectedPathNames.clear();
                if (!entities[0]->strName.empty())
                    m_selectedPathNames.append(QString::fromStdString(entities[0]->strName));

                if (m_selectionCallback)
                    m_selectionCallback(m_selectedNodeId);
                if (m_pathCallback)
                    m_pathCallback(m_selectedPathNames);
                emitStatus(QObject::tr("3D selected: %1").arg(m_selectedNodeId));
                return;
            }

            m_selectedNodeId.clear();
            m_selectedPathNames.clear();
            if (m_selectionCallback)
                m_selectionCallback(QString());
            if (m_pathCallback)
                m_pathCallback({});
            emitStatus(QObject::tr("3D selection cleared"));
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
    if (!document || !m_renderWidget)
        return;

    // 将 SceneDocument3D 中的 SceneManager3D 设置给 RenderWidget3D
    auto* sceneManager = document->sceneManager();
    if (sceneManager)
    {
        m_sceneManager = sceneManager;
        m_renderWidget->setSceneManager(sceneManager);
        SY_INFOF("[RenderWidget3DAdapter] SceneManager3D set: %p", sceneManager);
        emitStatus(QObject::tr("Scene loaded"));
    }
}

void RenderWidget3DAdapter::setCamera(CameraController3D* controller)
{
    if (!controller || !m_renderWidget)
        return;

    // 将 CameraController3D 连接到 RenderWidget3D 的 Camera3D
    // 这样控制器就能真正操作相机，实现视图控制
    controller->setCamera(&m_renderWidget->camera());
    SY_INFOF("[RenderWidget3DAdapter] CameraController3D connected to Camera3D at %p", &m_renderWidget->camera());

    // 同步轨道模式
    setOrbitMode(controller->isOrbitMode());
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
    Q_UNUSED(x);
    Q_UNUSED(y);
    Q_UNUSED(button);
    Q_UNUSED(modifiers);
    Q_UNUSED(viewW);
    Q_UNUSED(viewH);
    // 事件不再通过此接口转发，RenderWidget3D 作为子控件直接接收 Qt 事件
}

void RenderWidget3DAdapter::onMouseMove(int x, int y, int buttons, int viewW, int viewH)
{
    Q_UNUSED(x);
    Q_UNUSED(y);
    Q_UNUSED(buttons);
    Q_UNUSED(viewW);
    Q_UNUSED(viewH);
    // 事件不再通过此接口转发，RenderWidget3D 作为子控件直接接收 Qt 事件
}

void RenderWidget3DAdapter::onMouseRelease(int x, int y, int button, int viewW, int viewH)
{
    Q_UNUSED(x);
    Q_UNUSED(y);
    Q_UNUSED(button);
    Q_UNUSED(viewW);
    Q_UNUSED(viewH);
    // 事件不再通过此接口转发，RenderWidget3D 作为子控件直接接收 Qt 事件
}

void RenderWidget3DAdapter::onWheel(int delta, int viewW, int viewH)
{
    Q_UNUSED(delta);
    Q_UNUSED(viewW);
    Q_UNUSED(viewH);
    // 事件不再通过此接口转发，RenderWidget3D 作为子控件直接接收 Qt 事件
}

void RenderWidget3DAdapter::selectNodeById(const QString& nodeId)
{
    if (!m_renderWidget || !m_sceneManager)
        return;

    bool ok = false;
    Eg::EntityId entityId = nodeId.toLongLong(&ok);
    if (!ok)
        return;

    // 通过 SceneManager3D 查找图元
    Eg::SyMeshEntity* entity = m_sceneManager->findEntityById(entityId);
    if (!entity)
        return;

    // 通过 SelectionManager3D 选中图元
    m_renderWidget->selectionManager().select(entity);
    m_selectedNodeId = nodeId;

    // 同步路径名称（使用图元名称作为路径的最后一级）
    m_selectedPathNames.clear();
    if (!entity->strName.empty())
        m_selectedPathNames.append(QString::fromStdString(entity->strName));

    SY_INFOF("[RenderWidget3DAdapter] Selected node by ID: %s, path: %s", 
        qPrintable(nodeId), qPrintable(m_selectedPathNames.join("/")));

    if (m_selectionCallback)
        m_selectionCallback(nodeId);
    if (m_pathCallback)
        m_pathCallback(m_selectedPathNames);
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