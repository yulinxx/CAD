/**
 * @file RenderViewport2D.cpp
 * @brief 基于 Renderx 的 2D 渲染视口实现
 */
#include "RenderViewport2D.h"
#include "RenderWidget.h"
#include "SceneDocument2D.h"
#include "ISelectionService.h"
#include "UiInteractionDispatcher.h"
#include "UI2D/Operation/OperationBus.h"
#include "UI2D/Operation/OperationId.h"
#include "Engine2D/Core/SceneManager.h"
#include "Engine2D/SyEntity/SyLine.h"
#include "UI2D/DrawTools/ToolManager.h"
#include "Ui/DrawTools/ToolContext.h"
#include "Ui/DrawTools/ITool.h"
#include "Ui/DrawTools/SelectTool.h"
#include "Ui/ViewWidget/ToolInitializer.h"
#include "Ui/ViewWidget/ViewRenderCoordinator.h"

#include <QVBoxLayout>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QContextMenuEvent>
#include <QTimer>
#include <cmath>
#include <array>
#include "Log/SyLogger.h"

namespace
{
    // 网格步长
    constexpr double kGridStep = 50.0;
    // 场景更新节流时间（毫秒）
    constexpr int kSceneUpdateDelay = 0;
    // 默认视图范围：中心 (0,0)，半宽半高 500，即可见范围 (-500,-500)~(500,500)
    constexpr float kDefaultViewHalfW = 500.0f;
    constexpr float kDefaultViewHalfH = 500.0f;

}

// ==================== RenderViewport2D 实现 ====================

RenderViewport2D::RenderViewport2D(QWidget* parent)
    : QWidget(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setAutoFillBackground(false);

    initRenderWidget();
    initTimers();

    // 初始相机状态：台面中心 (600,400)，可见范围 (0,0)~(1200,800)
    m_camera.panOffset = QPointF(-600.0f, -400.0f);

    m_selector = std::make_unique<ViewportSelector>(nullptr, nullptr, &m_camera, m_renderWidget);
}

RenderViewport2D::~RenderViewport2D()
{
    *m_alive = false;
    // 从场景管理器移除观察者，避免已销毁对象被通知导致崩溃
    // SceneManager 由 ApplicationCompositionRoot 管理，生命周期长于 RenderViewport2D
    if (m_sceneManager)
        m_sceneManager->notifier().removeObserver(this);
}

void RenderViewport2D::initRenderWidget()
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_renderWidget = new RenderWidget(this);
    m_renderWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_renderWidget->setMouseTracking(true);
    // RenderWidget 是 QOpenGLWidget 原生窗口，鼠标事件不会冒泡到父控件，
    // 通过事件过滤器将鼠标事件从 RenderWidget 转发到 RenderViewport2D 处理
    m_renderWidget->installEventFilter(this);
    layout->addWidget(m_renderWidget);
}

void RenderViewport2D::initTimers()
{
    m_sceneUpdateTimer = new QTimer(this);
    m_sceneUpdateTimer->setSingleShot(true);
    m_sceneUpdateTimer->setInterval(kSceneUpdateDelay);
    connect(m_sceneUpdateTimer, &QTimer::timeout, this, &RenderViewport2D::updateSceneRender);
}

Eg::SceneManager* RenderViewport2D::sceneManager() const
{
    return m_sceneManager;
}

// ==================== 外部接口实现 ====================

void RenderViewport2D::setStatusCallback(std::function<void(const QString&)>&& callback)
{
    m_statusCallback = std::move(callback);
    if (m_selector)
        m_selector->setStatusCallback(m_statusCallback);
}

void RenderViewport2D::setSelectionCallback(std::function<void(const QString&, const QString&)>&& callback)
{
    m_selectionCallback = std::move(callback);
    if (m_selector)
        m_selector->setSelectionCallback(m_selectionCallback);
}

void RenderViewport2D::setCommandStageCallback(std::function<void(const QString&)>&& callback)
{
    m_commandStageCallback = std::move(callback);
}

void RenderViewport2D::setPositionCallback(std::function<void(double, double)>&& callback)
{
    m_positionCallback = std::move(callback);
}

void RenderViewport2D::setDocument(SceneDocument2D* document)
{
    // 断开旧文档的观察者注册
    if (m_sceneManager)
    {
        m_sceneManager->notifier().removeObserver(this);
        m_sceneManager = nullptr;
    }

    m_document = document;

    // 缓存 SceneManager 指针，避免析构时通过 m_document 访问已释放内存
    if (m_document)
        m_sceneManager = m_document->sceneManager();

    // 同步选择控制器
    if (m_selector)
        m_selector->setSceneManager(m_sceneManager);

    // 注册为新文档的观察者（同时接收场景变更和选择变更通知）
    if (m_sceneManager)
        m_sceneManager->notifier().addObserver(this);

    // 初始刷新
    scheduleSceneUpdate();
}

void RenderViewport2D::setSelectionService(ISelectionService* service)
{
    m_selectionService = service;
    if (m_selector)
        m_selector->setSelectionService(service);
}

void RenderViewport2D::setInteractionDispatcher(IInteractionDispatcher* dispatcher)
{
    m_interactionDispatcher = dispatcher;
}

void RenderViewport2D::setOperationBus(OperationBus* bus)
{
    m_operationBus = bus;
}

void RenderViewport2D::setSceneEditService(SceneEditService* service)
{
    m_sceneEditService = service;
}

void RenderViewport2D::initializeTools()
{
    if (!m_renderWidget || !m_sceneManager)
        return;

    m_toolManager = std::make_unique<ToolManager>();

    // 创建渲染协调器
    auto* coordinator = new Ui2D::ViewRenderCoordinator();
    coordinator->setRenderWidget(m_renderWidget);

    // 注册所有工具
    // 将状态栏回调传入，SelectTool 选择变化时自动更新状态栏显示 "Selected: N"
    ToolInitializer::registerAllTools(*m_toolManager, m_sceneManager, m_renderWidget, coordinator,
        [this](const QString& msg) { updateStatus(msg); });

    // 设置图元提交回调（通过 SceneEditService，支持 Undo）
    if (m_sceneEditService)
    {
        m_toolManager->setEntityCallbackForAllTools([this](Eg::SyEntity* e) {
            if (e) m_sceneEditService->addEntityFromPointer(e, "Draw");
            });
    }

    // 设置工具切换回调
    m_toolManager->setSwitchToolCallbackForAllTools([this](const QString& name) {
        setActiveTool(name);
        });

    // 注册图元编辑器
    ToolInitializer::registerAllEditors();

    updateStatus(tr("2D tools initialized"));
}

bool RenderViewport2D::setActiveTool(const QString& toolName)
{
    if (!m_toolManager)
        return false;

    bool ok = m_toolManager->setActiveTool(toolName);
    if (ok)
    {
        updateStatus(tr("2D tool: %1").arg(toolName));
        if (toolName == "SelectTool")
        {
            unsetCursor();
        }
        else
        {
            setCursor(Qt::CrossCursor);
        }
    }
    return ok;
}

QString RenderViewport2D::activeToolName() const
{
    if (!m_toolManager)
        return QString();
    return m_toolManager->getActiveToolName();
}

ToolManager* RenderViewport2D::toolManager() const
{
    return m_toolManager.get();
}

void RenderViewport2D::resetView()
{
    // 重置到台面范围：左下角 (0,0)，右上角 (1200,800)，中心在 (600,400)
    const float tableW = 1200.0f;
    const float tableH = 800.0f;
    float vpW = static_cast<float>(m_renderWidget ? m_renderWidget->width() : width());
    float vpH = static_cast<float>(m_renderWidget ? m_renderWidget->height() : height());
    if (vpW > 0 && vpH > 0)
        m_camera.setViewExtent(vpW, vpH, tableW / 2.0f, tableH / 2.0f, tableW / 2.0f, tableH / 2.0f);
    else
        m_camera.reset();
    updateViewMatrix();
    updateStatus(tr("2D view reset"));
}

void RenderViewport2D::zoomToFit()
{
    if (!m_document || !sceneManager())
    {
        resetView();
        return;
    }

    auto* sm = sceneManager();
    auto bbox = sm->sceneBBox2D();

    if (!bbox.isValid())
    {
        resetView();
        return;
    }

    float sceneW = static_cast<float>(bbox.maxPt.x() - bbox.minPt.x());
    float sceneH = static_cast<float>(bbox.maxPt.y() - bbox.minPt.y());
    const float centerX = static_cast<float>((bbox.minPt.x() + bbox.maxPt.x()) / 2.0);
    const float centerY = static_cast<float>((bbox.minPt.y() + bbox.maxPt.y()) / 2.0);

    if (sceneW <= 0 || sceneH <= 0)
    {
        sceneW = 1000.0f;
        sceneH = 1000.0f;
    }

    float vpW = static_cast<float>(m_renderWidget ? m_renderWidget->width() : width());
    float vpH = static_cast<float>(m_renderWidget ? m_renderWidget->height() : height());

    m_camera.zoomToFit(vpW, vpH, sceneW, sceneH);
    m_camera.pan(-centerX, -centerY);

    updateViewMatrix();
    updateStatus(tr("2D zoom extents"));
}

void RenderViewport2D::zoomToSelection()
{
    if (!m_selectionService)
    {
        updateStatus(tr("No selection service"));
        return;
    }

    auto selected = m_selectionService->selectedEntities();
    if (selected.empty())
    {
        updateStatus(tr("No entities selected"));
        return;
    }

    // 计算所有选中图元的合并包围盒
    Ut::BBox2d combinedBbox;
    for (auto* entity : selected)
    {
        if (!entity)
            continue;
        Ut::BBox2d bbox = entity->getBbox();
        if (bbox.isValid())
            combinedBbox.expand(bbox);
    }

    if (!combinedBbox.isValid())
    {
        updateStatus(tr("Invalid selection bounds"));
        return;
    }

    float sceneW = static_cast<float>(combinedBbox.maxPt.x() - combinedBbox.minPt.x());
    float sceneH = static_cast<float>(combinedBbox.maxPt.y() - combinedBbox.minPt.y());

    // 如果包围盒太小（小于1个单位），使用默认大小
    if (sceneW <= 0 || sceneH <= 0)
    {
        sceneW = 100.0f;
        sceneH = 100.0f;
    }

    float vpW = static_cast<float>(m_renderWidget ? m_renderWidget->width() : width());
    float vpH = static_cast<float>(m_renderWidget ? m_renderWidget->height() : height());

    m_camera.zoomToFit(vpW, vpH, sceneW, sceneH);

    float centerX = static_cast<float>((combinedBbox.minPt.x() + combinedBbox.maxPt.x()) / 2.0);
    float centerY = static_cast<float>((combinedBbox.minPt.y() + combinedBbox.maxPt.y()) / 2.0);
    m_camera.pan(-centerX, -centerY);

    updateViewMatrix();
    updateStatus(tr("2D zoom to selection"));
}

void RenderViewport2D::zoomIn()
{
    // 以视口中心为锚点放大
    if (!m_renderWidget)
        return;
    float vpW = static_cast<float>(m_renderWidget->width());
    float vpH = static_cast<float>(m_renderWidget->height());
    if (vpW <= 0 || vpH <= 0)
        return;
    QPointF center(vpW / 2.0f, vpH / 2.0f);
    QPointF worldCenter = m_camera.screenToWorld(QPoint(static_cast<int>(center.x()), static_cast<int>(center.y())), vpW, vpH);
    m_camera.zoomIn(1.25f, worldCenter, vpW, vpH);
    updateViewMatrix();
}

void RenderViewport2D::zoomOut()
{
    // 以视口中心为锚点缩小
    if (!m_renderWidget)
        return;
    float vpW = static_cast<float>(m_renderWidget->width());
    float vpH = static_cast<float>(m_renderWidget->height());
    if (vpW <= 0 || vpH <= 0)
        return;
    QPointF center(vpW / 2.0f, vpH / 2.0f);
    QPointF worldCenter = m_camera.screenToWorld(QPoint(static_cast<int>(center.x()), static_cast<int>(center.y())), vpW, vpH);
    m_camera.zoomOut(1.25f, worldCenter, vpW, vpH);
    updateViewMatrix();
}

void RenderViewport2D::setPanModeEnabled(bool enabled)
{
    m_panModeEnabled = enabled;
    updateStatus(enabled ? tr("2D pan mode") : tr("2D select mode"));
}

bool RenderViewport2D::isPanModeEnabled() const
{
    return m_panModeEnabled;
}

void RenderViewport2D::setDrawingEnabled(bool enabled)
{
    if (m_commandStageCallback)
        m_commandStageCallback(enabled ? tr("Waiting for first point") : tr("Idle"));
    updateStatus(enabled ? tr("2D draw mode") : tr("2D select mode"));
}

void RenderViewport2D::setMeasureMode(bool enabled)
{
    if (m_commandStageCallback)
        m_commandStageCallback(enabled ? tr("Waiting for first point") : tr("Idle"));
    updateStatus(enabled ? tr("2D measure mode") : tr("2D select mode"));
}

QString RenderViewport2D::selectedEntityId() const
{
    if (!m_document || !m_selectionService)
        return {};
    auto ids = m_selectionService->selectedIds();
    return ids.empty() ? QString() : QString::fromStdString(ids[0]);
}

void RenderViewport2D::deleteSelectedEntity()
{
    if (!m_document)
        return;
    auto selectedId = selectedEntityId();
    if (selectedId.isEmpty())
        return;

    // 只走 OperationBus 路径（经 SceneEditService → UndoRedoManager，确保可撤销）
    // 移除旧版 m_document->removeEntity() 直写，避免双重删除
    if (m_operationBus)
        m_operationBus->run(OperationId::Edit_Delete, {}, OperationSource::DrawTool);

    clearSelection();
    updateStatus(tr("2D entity deleted"));
    scheduleSceneUpdate();
}

void RenderViewport2D::nudgeSelectedEndpoint(const QPointF& delta)
{
    if (!m_document)
        return;
    auto selectedId = selectedEntityId();
    if (selectedId.isEmpty())
        return;
    auto* sm = sceneManager();
    if (!sm) return;
    bool ok = false;
    auto* entity = sm->findEntityById(static_cast<Eg::EntityId>(selectedId.toULongLong(&ok)));
    if (!ok || !entity || entity->eType != Eg::EType::LINE)
        return;
    auto* line = static_cast<Eg::SyLine*>(entity);
    for (auto& pt : line->vPoints)
    {
        pt.x() += delta.x();
        pt.y() += delta.y();
    }
    sm->clearSelection();
    sm->selectEntity(line);
    syncSelectionDetails();
    updateStatus(tr("2D endpoint moved"));
    scheduleSceneUpdate();
}

void RenderViewport2D::selectEntityById(const QString& entityId)
{
    if (!m_document)
        return;
    if (m_selectionService)
    {
        m_selectionService->clear();
        m_selectionService->select(entityId.toStdString());
    }
    syncSelectionDetails();
    updateStatus(tr("2D entity selected"));
    if (m_selectionCallback)
        m_selectionCallback(tr("2D-Select"), tr("2D entity: %1").arg(entityId));
    requestRepaint();
}

void RenderViewport2D::syncSelectionDetails()
{
    // 同步选择详情到属性面板等（由外部回调处理）
}

void RenderViewport2D::clearSelection()
{
    if (m_selectionService)
        m_selectionService->clear();
    requestRepaint();
}

QPointF RenderViewport2D::mapToScene(const QPoint& screenPos) const
{
    float vpW = static_cast<float>(m_renderWidget ? m_renderWidget->width() : width());
    float vpH = static_cast<float>(m_renderWidget ? m_renderWidget->height() : height());

    // 将相对于 RenderViewport2D 的坐标转换为相对于 m_renderWidget 的坐标
    QPoint widgetPos = screenPos;
    if (m_renderWidget)
        widgetPos = m_renderWidget->mapFromParent(screenPos);

    return m_camera.screenToWorld(widgetPos, vpW, vpH);
}

// ==================== 事件处理 ====================

void RenderViewport2D::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    QTimer::singleShot(0, this, [this]() {
        resetView();
        updateViewMatrix();

        if (m_sceneUpdateTimer && m_sceneUpdateTimer->isActive())
            m_sceneUpdateTimer->stop();

        if (!m_document && m_renderWidget && m_renderWidget->isInitialized())
        {
            m_renderWidget->submitDefaultSceneEnv();
        }
        else if (m_document)
        {
            m_refreshLevel = RefreshLevel::FullRefresh;
            if (m_sceneUpdateTimer)
                m_sceneUpdateTimer->start();
            else
                updateSceneRender();
        }
        });
}

void RenderViewport2D::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    updateViewMatrix();
}

bool RenderViewport2D::eventFilter(QObject* obj, QEvent* event)
{
    if (obj != m_renderWidget || !m_renderWidget)
        return QWidget::eventFilter(obj, event);

    switch (event->type())
    {
        case QEvent::MouseButtonDblClick:
        case QEvent::MouseButtonPress:
        case QEvent::MouseMove:
        case QEvent::MouseButtonRelease:
        {
            // 将 RenderWidget 坐标系的鼠标事件转发到本视口处理
            // RenderWidget 是 QOpenGLWidget 原生窗口，事件不会自动冒泡到父控件
            auto* me = static_cast<QMouseEvent*>(event);
            QPoint parentPos = m_renderWidget->mapToParent(me->pos());
            QMouseEvent parentEvent(me->type(), parentPos, me->globalPos(),
                me->button(), me->buttons(), me->modifiers());

            switch (event->type())
            {
                case QEvent::MouseButtonDblClick:
                    mouseDoubleClickEvent(&parentEvent);
                    break;
                case QEvent::MouseButtonPress:
                    mousePressEvent(&parentEvent);
                    break;
                case QEvent::MouseMove:
                    mouseMoveEvent(&parentEvent);
                    break;
                case QEvent::MouseButtonRelease:
                    mouseReleaseEvent(&parentEvent);
                    break;
                default:
                    break;
            }
            return true; // 事件已处理，不再传播
        }
        case QEvent::Wheel:
        {
            auto* we = static_cast<QWheelEvent*>(event);
            wheelEvent(we);
            return true;
        }
        case QEvent::KeyPress:
        {
            auto* ke = static_cast<QKeyEvent*>(event);
            keyPressEvent(ke);
            return true;
        }
        case QEvent::ContextMenu:
        {
            auto* ce = static_cast<QContextMenuEvent*>(event);
            contextMenuEvent(ce);
            return true;
        }
        default:
            break;
    }

    return QWidget::eventFilter(obj, event);
}

void RenderViewport2D::mousePressEvent(QMouseEvent* event)
{
    if (!m_renderWidget)
    {
        QWidget::mousePressEvent(event);
        return;
    }

    float vpW = static_cast<float>(m_renderWidget->width());
    float vpH = static_cast<float>(m_renderWidget->height());

    if (vpW <= 0 || vpH <= 0)
        return;

    QPoint widgetPos = m_renderWidget->mapFromParent(event->pos());
    QPointF worldPos = m_camera.screenToWorld(widgetPos, vpW, vpH);

    if (event->button() == Qt::MiddleButton)
    {
        m_panning = true;
        m_lastMousePos = m_renderWidget->mapFromParent(event->pos());
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }

    if (event->button() == Qt::LeftButton)
    {
        if (m_panModeEnabled)
        {
            m_panning = true;
            m_lastMousePos = m_renderWidget->mapFromParent(event->pos());
            setCursor(Qt::ClosedHandCursor);
        }
        else if (m_interactionDispatcher && m_interactionDispatcher->hasActiveCommand())
        {
            m_interactionDispatcher->forwardMouseDown(
                static_cast<int>(worldPos.x()),
                static_cast<int>(worldPos.y()));
        }
        else if (m_toolManager && m_toolManager->getActiveTool())
        {
            // 转发鼠标事件到活动工具（包括 SelectTool）
            m_toolManager->getActiveTool()->onMousePress(worldPos, event);
        }
        else if (m_selector)
        {
            m_selector->beginBoxSelect(worldPos);
        }
        event->accept();
        return;
    }

    QWidget::mousePressEvent(event);
}

void RenderViewport2D::mouseMoveEvent(QMouseEvent* event)
{
    if (!m_renderWidget)
    {
        QWidget::mouseMoveEvent(event);
        return;
    }

    float vpW = static_cast<float>(m_renderWidget->width());
    float vpH = static_cast<float>(m_renderWidget->height());

    if (vpW <= 0 || vpH <= 0)
        return;

    QPoint widgetPos = m_renderWidget->mapFromParent(event->pos());
    QPointF worldPos = m_camera.screenToWorld(widgetPos, vpW, vpH);

    // 始终更新鼠标位置到状态栏（无论后续如何处理）
    if (m_positionCallback)
        m_positionCallback(worldPos.x(), worldPos.y());

    if (m_panning)
    {
        QPoint widgetPos = m_renderWidget->mapFromParent(event->pos());
        QPoint delta = widgetPos - m_lastMousePos;
        m_lastMousePos = widgetPos;

        float worldDx = static_cast<float>(delta.x()) / m_camera.zoomX;
        float worldDy = -static_cast<float>(delta.y()) / m_camera.zoomY;

        m_camera.pan(worldDx, worldDy);
        updateViewMatrix();
        event->accept();
        return;
    }

    if (m_interactionDispatcher && m_interactionDispatcher->hasActiveCommand())
    {
        m_interactionDispatcher->forwardMouseMove(
            static_cast<int>(worldPos.x()),
            static_cast<int>(worldPos.y()));
        event->accept();
        return;
    }

    if (m_toolManager && m_toolManager->getActiveTool())
    {
        m_toolManager->getActiveTool()->onMouseMove(worldPos, event);
        event->accept();
        return;
    }

    QWidget::mouseMoveEvent(event);
}

void RenderViewport2D::mouseReleaseEvent(QMouseEvent* event)
{
    if (!m_renderWidget)
    {
        QWidget::mouseReleaseEvent(event);
        return;
    }

    float vpW = static_cast<float>(m_renderWidget->width());
    float vpH = static_cast<float>(m_renderWidget->height());

    if (vpW <= 0 || vpH <= 0)
        return;

    QPoint widgetPos = m_renderWidget->mapFromParent(event->pos());
    QPointF worldPos = m_camera.screenToWorld(widgetPos, vpW, vpH);

    if (event->button() == Qt::MiddleButton)
    {
        m_panning = false;
        unsetCursor();
        event->accept();
        return;
    }

    if (event->button() == Qt::LeftButton)
    {
        if (m_panning)
        {
            m_panning = false;
            unsetCursor();
            event->accept();
            return;
        }

        if (m_interactionDispatcher && m_interactionDispatcher->hasActiveCommand())
        {
            m_interactionDispatcher->forwardMouseUp(
                static_cast<int>(worldPos.x()),
                static_cast<int>(worldPos.y()));
            event->accept();
            return;
        }

        if (m_toolManager && m_toolManager->getActiveTool())
        {
            // 转发鼠标释放事件到活动工具（包括 SelectTool）
            m_toolManager->getActiveTool()->onMouseRelease(worldPos, event);
            event->accept();
            return;
        }

        if (m_selector)
        {
            m_selector->handleClick(worldPos);
        }
        event->accept();
        return;
    }

    QWidget::mouseReleaseEvent(event);
}

void RenderViewport2D::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (!m_renderWidget)
    {
        QWidget::mouseDoubleClickEvent(event);
        return;
    }

    float vpW = static_cast<float>(m_renderWidget->width());
    float vpH = static_cast<float>(m_renderWidget->height());

    if (vpW <= 0 || vpH <= 0)
        return;

    QPoint widgetPos = m_renderWidget->mapFromParent(event->pos());
    QPointF worldPos = m_camera.screenToWorld(widgetPos, vpW, vpH);

    if (event->button() == Qt::LeftButton)
    {
        if (m_toolManager && m_toolManager->getActiveTool())
        {
            // 转发双击事件到活动工具（SelectTool 用于进入图元编辑）
            m_toolManager->getActiveTool()->onMouseDoubleClick(worldPos, event);
            event->accept();
            return;
        }
    }

    QWidget::mouseDoubleClickEvent(event);
}

void RenderViewport2D::wheelEvent(QWheelEvent* event)
{
    if (!m_renderWidget)
    {
        QWidget::wheelEvent(event);
        return;
    }

    float vpW = static_cast<float>(m_renderWidget->width());
    float vpH = static_cast<float>(m_renderWidget->height());

    if (vpW <= 0 || vpH <= 0)
        return;

    QPoint widgetPos = event->position().toPoint();
    QPointF worldPos = m_camera.screenToWorld(widgetPos, vpW, vpH);

    float factor = (event->angleDelta().y() > 0) ? 1.1f : 0.9f;
    m_camera.zoomIn(factor, worldPos, vpW, vpH);
    updateViewMatrix();
    event->accept();
}

void RenderViewport2D::keyPressEvent(QKeyEvent* event)
{
    if (m_interactionDispatcher && m_interactionDispatcher->hasActiveCommand())
    {
        if (event->key() == Qt::Key_Escape)
        {
            m_interactionDispatcher->cancel();
            event->accept();
            return;
        }
        if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)
        {
            m_interactionDispatcher->submit();
            event->accept();
            return;
        }
    }

    // 转发键盘事件到活动工具（包括 SelectTool，用于处理快捷键）
    if (m_toolManager && m_toolManager->getActiveTool())
    {
        if (m_toolManager->getActiveTool()->onKeyPress(event))
        {
            event->accept();
            return;
        }
    }

    if (event->key() == Qt::Key_Delete)
    {
        deleteSelectedEntity();
        event->accept();
        return;
    }

    QWidget::keyPressEvent(event);
}

void RenderViewport2D::contextMenuEvent(QContextMenuEvent* event)
{
    if (m_interactionDispatcher && m_interactionDispatcher->hasActiveCommand())
    {
        m_interactionDispatcher->cancel();
        event->accept();
        return;
    }
    QWidget::contextMenuEvent(event);
}

// ==================== 内部方法 ====================

void RenderViewport2D::updateViewMatrix()
{
    if (!m_renderWidget)
        return;

    float vpW = static_cast<float>(m_renderWidget->width());
    float vpH = static_cast<float>(m_renderWidget->height());

    if (vpW <= 0 || vpH <= 0)
        return;

    float viewMat[9];
    m_camera.computeViewMatrix(viewMat, vpW, vpH);

    Render::Mat3f mat;
    for (int i = 0; i < 9; ++i)
        mat.data[i] = viewMat[i];

    SY_TRACEF("RenderViewport2D::updateViewMatrix: vpW=%.0f, vpH=%.0f, zoom=(%.6f,%.6f), pan=(%.2f,%.2f)",
        vpW, vpH, m_camera.zoomX, m_camera.zoomY, m_camera.panOffset.x(), m_camera.panOffset.y());

    // 即使渲染设备尚未初始化，也先缓存视图矩阵；initializeGL 后会补发一次
    m_renderWidget->setViewMatrix(mat);

    // 视图矩阵变化后，场景环境（网格、台面、标尺）需要重新提交以跟随视图变化
    SY_INFOF("RenderViewport2D::updateViewMatrix: calling submitDefaultSceneEnv, document=%p, initialized=%d",
        m_document, m_renderWidget->isInitialized());
    if (m_renderWidget->isInitialized())
    {
        m_renderWidget->submitDefaultSceneEnv();
    }
}

void RenderViewport2D::scheduleSceneUpdate()
{
    if (m_refreshLevel < RefreshLevel::LightUpdate)
        m_refreshLevel = RefreshLevel::LightUpdate;
    if (m_sceneUpdateTimer && !m_sceneUpdateTimer->isActive())
        m_sceneUpdateTimer->start();
}

void RenderViewport2D::requestRepaint()
{
    // 仅重绘，不触发全量 gather（适用于选择变化等仅需视觉刷新的场景）
    if (m_refreshLevel < RefreshLevel::Repaint)
        m_refreshLevel = RefreshLevel::Repaint;
    if (m_renderWidget)
        m_renderWidget->update();
}

void RenderViewport2D::requestSceneRefresh()
{
    m_refreshLevel = RefreshLevel::FullRefresh;
    if (m_sceneUpdateTimer)
    {
        if (!m_sceneUpdateTimer->isActive())
            m_sceneUpdateTimer->start();
    }
    else
    {
        updateSceneRender();
    }
}

void RenderViewport2D::requestFullRefresh()
{
    // 强制全量 gather + submit（导入、大批量修改后）
    m_refreshLevel = RefreshLevel::FullRefresh;
    if (m_sceneUpdateTimer)
    {
        if (!m_sceneUpdateTimer->isActive())
            m_sceneUpdateTimer->start();
    }
    else
    {
        updateSceneRender();
    }
}

void RenderViewport2D::onSceneChanged()
{
    // 记录脏图元 ID，为未来增量渲染提供输入
    if (auto* sm = sceneManager())
    {
        for (auto id : sm->dirtyEntities())
            m_pendingDirtyIds.insert(id);
        for (auto id : sm->deletedEntityIds())
            m_pendingDeletedIds.insert(id);
    }
    scheduleSceneUpdate();
}

void RenderViewport2D::onSelectionChanged()
{
    // 同步选择工具的选择状态，处理删除后悬空指针问题
    if (m_toolManager)
    {
        auto* selectTool = dynamic_cast<SelectTool*>(m_toolManager->getActiveTool());
        if (selectTool)
            selectTool->syncSelectionFromScene();
    }
    // 选择变更仅需重绘，无需全量 gather
    requestRepaint();
}

void RenderViewport2D::updateSceneRender()
{
    if (!m_renderWidget || m_refreshLevel == RefreshLevel::None)
        return;

    SY_INFOF("RenderViewport2D::updateSceneRender: refreshLevel=%d initialized=%d",
        static_cast<int>(m_refreshLevel),
        m_renderWidget->isInitialized() ? 1 : 0);

    // GL 尚未初始化时保留刷新标记，延迟重试
    if (!m_renderWidget->isInitialized())
    {
        if (m_sceneUpdateTimer && !m_sceneUpdateTimer->isActive())
            m_sceneUpdateTimer->start();
        return;
    }

    RefreshLevel level = m_refreshLevel;
    m_refreshLevel = RefreshLevel::None;

    auto* sm = sceneManager();
    if (!sm)
        return;

    updateViewMatrix();

    if (level == RefreshLevel::Repaint)
    {
        // 纯视觉重绘（选择变化、叠加层更新），不触碰渲染数据
        m_renderWidget->update();
        return;
    }

    if (level == RefreshLevel::LightUpdate && !m_pendingDeletedIds.empty())
    {
        // 增量删除路径 — 先移除已删除的图元
        // TODO: 当 RenderWidget 支持 removeEntity() 时，逐个移除
        // 目前 fallback 到全量 gather
    }

    if (level == RefreshLevel::LightUpdate && !m_pendingDirtyIds.empty())
    {
        // 增量更新路径 — 对脏图元逐个更新
        // TODO: 当 RenderWidget 支持 updateEntity()/addEntity() 时，逐个增/改
        // 目前 fallback 到全量 gather
    }

    if (level >= RefreshLevel::LightUpdate)
    {
        // 全量 gather + submit（兜底路径，也是当前唯一实现）
        m_renderWidget->submitSceneFromDataSource(sm);
        // 提交网格背景
        m_renderWidget->submitDefaultSceneEnv();
    }

    // 清理已处理的脏图元标记
    sm->markClean();
    m_pendingDirtyIds.clear();
    m_pendingDeletedIds.clear();
}



void RenderViewport2D::updateStatus(const QString& text)
{
    if (m_statusCallback)
        m_statusCallback(text);
}