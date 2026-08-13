#include "RenderWidget3DAdapter.h"

#include <QCoreApplication>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QPainter>
#include <QVBoxLayout>

#include "Render3D/RenderWidget3D.h"
#include "Engine3D/SyEntity/SyMeshEntity.h"
#include "Engine3D/SceneManager3D.h"
#include "Engine3D/Selection/SelectionManager3D.h"
#include "UI3D/Service/SceneDocument3D.h"
#include "UI3D/Service/CameraController3D.h"
#include "UiEntities.h"
#include "Log/SyLogger.h"

RenderWidget3DAdapter::RenderWidget3DAdapter() = default;

RenderWidget3DAdapter::~RenderWidget3DAdapter()
{
    shutdown();
}

bool RenderWidget3DAdapter::ensureWidgetCreated()
{
    if (m_renderWidget)
    {
        return true;
    }

    if (!m_parentWidget)
    {
        SY_WARN("[RenderWidget3DAdapter] ensureWidgetCreated called without a valid QWidget parent");
        return false;
    }

    if (!m_parentWidget->isWidgetType())
    {
        SY_ERROR("[RenderWidget3DAdapter] Parent object is not a QWidget, refusing to create RenderWidget3D");
        return false;
    }

    m_renderWidget = std::make_unique<RenderWidget3D>(m_parentWidget);
    m_renderWidget->setMinimumSize(640, 480);
    // 使用布局管理，让 RenderWidget3D 自动跟随父控件尺寸变化
    // 避免手动 setGeometry 导致的尺寸不同步和 native window 状态不一致
    // 注意：不要显式调用 show()，QOpenGLWidget 会随父控件自动显示
    // 提前 show() 会在父控件 native window 未就绪时强制创建原生窗口，
    // 导致 Windows 窗口句柄无效（INVALID_HANDLE_VALUE），引发访问冲突
    auto* layout = new QVBoxLayout(m_parentWidget);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_renderWidget.get());
    bindWidgetSignals();
    SY_INFO("[RenderWidget3DAdapter] RenderWidget3D created as child widget");
    return true;
}

void RenderWidget3DAdapter::bindWidgetSignals()
{
    if (!m_renderWidget)
    {
        return;
    }

    // 场景选择变化只同步到适配器，再由适配器回调上层。
    // 跨 DLL 安全：信号参数改为 POD 指针数组
    QObject::connect(
        m_renderWidget.get(), &RenderWidget3D::sigSelectionChanged, [this](const Eg::SyMeshEntity** entities, int count) {
            if (count > 0 && entities && entities[0])
            {
                m_selectedNodeId = QString::number(entities[0]->id);

                // 同步路径名称
                m_selectedPathNames.clear();
                const char* name = entities[0]->name();
                if (name && name[0] != '\0')
                {
                    m_selectedPathNames.append(QString::fromUtf8(name));
                }

                if (m_selectionCallback)
                {
                    m_selectionCallback(m_selectedNodeId);
                }
                if (m_pathCallback)
                {
                    m_pathCallback(m_selectedPathNames);
                }
                emitStatus(QObject::tr("3D selected: %1").arg(m_selectedNodeId));
                return;
            }

            m_selectedNodeId.clear();
            m_selectedPathNames.clear();
            if (m_selectionCallback)
            {
                m_selectionCallback(QString());
            }
            if (m_pathCallback)
            {
                m_pathCallback({});
            }
            emitStatus(QObject::tr("3D selection cleared"));
        });

    // 相机变化只转发状态提示，不在这里写任何 UI 业务。
    QObject::connect(m_renderWidget.get(), &RenderWidget3D::sigCameraChanged, [this]() {
        emitStatus(m_renderWidget ? m_renderWidget->navigationStatusHint() : QStringLiteral("3D ready"));
    });
}

void RenderWidget3DAdapter::emitStatus(const QString& text)
{
    if (m_statusCallback)
    {
        m_statusCallback(text);
    }
}

bool RenderWidget3DAdapter::initialize(void* windowHandle)
{
    // 这里预期传入的是 QWidget*；若上层传入的是原生句柄或其他类型，
    // 直接按失败处理，避免把非法地址当作 QObject/QWidget 使用。
    m_parentWidget = static_cast<QWidget*>(windowHandle);
    if (!m_parentWidget)
    {
        SY_WARN("[RenderWidget3DAdapter] initialize called with null windowHandle");
        m_ready = false;
        return false;
    }

    m_ready = ensureWidgetCreated();
    return m_ready;
}

void RenderWidget3DAdapter::shutdown()
{
    // 退出时先断开所有信号与回调，再延迟释放内部控件，避免 Qt 销毁链与业务链交叉。
    if (m_renderWidget)
    {
        QObject::disconnect(m_renderWidget.get(), nullptr, nullptr, nullptr);
        m_renderWidget->setTransformUndoHandler(nullptr, nullptr);
        m_renderWidget->setDocumentModifiedHandler(nullptr, nullptr);
        m_renderWidget->setMeshSurfacePickHandler(nullptr, nullptr);
        m_renderWidget->setMeshSurfacePickActive(false);

        // 关键：先断开所有回调，再把 widget 从父对象和布局中彻底脱离，
        // 然后交给 Qt 事件循环安全删除。
        // 必须用 release() 放弃 unique_ptr 所有权，否则 reset() 会立即析构对象，
        // 与 deleteLater() 形成双重释放。
        m_renderWidget->setParent(nullptr);
        m_renderWidget->deleteLater();
        m_renderWidget.release();
    }

    m_sceneManager = nullptr;
    m_parentWidget = nullptr;
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
    {
        m_renderWidget->setUpdatesEnabled(enabled);
    }
}

bool RenderWidget3DAdapter::isRenderLoopRunning() const
{
    return m_renderLoopEnabled && m_ready;
}

void RenderWidget3DAdapter::setScene(SceneDocument3DAdapter* document)
{
    if (!document || !m_renderWidget)
    {
        return;
    }

    // 将 SceneDocument3DAdapter 中的 SceneManager3D 设置给 RenderWidget3D
    auto engineScene = document->engineScene();
    auto* sceneManager = engineScene ? engineScene.get() : nullptr;
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
    {
        return;
    }

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
    {
        m_renderWidget->update();
    }
}

void RenderWidget3DAdapter::resize(int width, int height)
{
    if (m_renderWidget)
    {
        m_renderWidget->resize(width, height);
    }
}

void RenderWidget3DAdapter::resetView()
{
    if (m_renderWidget)
    {
        m_renderWidget->resetView();
    }
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

void RenderWidget3DAdapter::selectNodeById(const QString& nodeId)
{
    if (!m_renderWidget || !m_sceneManager)
    {
        return;
    }

    bool ok = false;
    Eg::EntityId entityId = nodeId.toLongLong(&ok);
    if (!ok)
    {
        return;
    }

    // 通过 SceneManager3D 查找图元
    Eg::SyMeshEntity* entity = m_sceneManager->findEntityById(entityId);
    if (!entity)
    {
        return;
    }

    // 通过 SelectionManager3D 选中图元
    m_renderWidget->selectionManager().select(entity);
    m_selectedNodeId = nodeId;

    // 同步路径名称（使用图元名称作为路径的最后一级）
    m_selectedPathNames.clear();
    const char* entityName = entity->name();
    if (entityName && entityName[0] != '\0')
    {
        m_selectedPathNames.append(QString::fromUtf8(entityName));
    }

    SY_INFOF("[RenderWidget3DAdapter] Selected node by ID: %s, path: %s",
        qPrintable(nodeId),
        qPrintable(m_selectedPathNames.join("/")));

    if (m_selectionCallback)
    {
        m_selectionCallback(nodeId);
    }
    if (m_pathCallback)
    {
        m_pathCallback(m_selectedPathNames);
    }
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