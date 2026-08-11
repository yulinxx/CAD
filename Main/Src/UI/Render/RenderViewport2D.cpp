/**
 * @file RenderViewport2D.cpp
 * @brief 基于 Renderx 的 2D 渲染视口实现
 *
 * 输入路由委托给 ViewportInputRouter（P5 大文件收口）。
 */
#include "RenderViewport2D.h"
#include "RenderWidget.h"
#include "SceneRefreshCoordinator.h"
#include "ViewportInputRouter.h"
#include "SceneDocument2D.h"
#include "ISelectionService.h"
#include "UiInteractionDispatcher.h"

#include "UI2D/Operation/OperationBus.h"
#include "UI2D/Operation/OperationId.h"

#include "Engine2D/Core/SceneManager.h"
#include "Engine2D/Edit/SceneEditService.h"

#include "UI/DrawTools/ToolManager.h"
#include "UI/DrawTools/SelectTool.h"
#include "UI/ViewWidget/ToolInitializer.h"
#include "UI/ViewWidget/ViewRenderCoordinator.h"

#include <QVBoxLayout>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QContextMenuEvent>
#include <QTimer>
#include "Log/SyLogger.h"

// ==================== RenderViewport2D 实现 ====================

RenderViewport2D::RenderViewport2D(QWidget* parent)
    : QWidget(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setAutoFillBackground(false);

    initRenderWidget();

    // 刷新协调器：封装四级刷新策略与增量渲染管线
    m_refreshCoordinator = std::make_unique<SceneRefreshCoordinator>(this);
    m_refreshCoordinator->setRenderWidget(m_renderWidget);

    // P5: 观察者注册收敛到 SceneRefreshCoordinator，视口通过信号同步工具状态
    QObject::connect(m_refreshCoordinator.get(), &SceneRefreshCoordinator::selectionChanged,
        this, [this]() { syncSelectionDetails(); });

    // 初始相机状态：台面中心 (600,400)，可见范围 (0,0)~(1200,800)
    m_camera.panOffset = QPointF(-600.0f, -400.0f);

    m_selector = std::make_unique<ViewportSelector>(nullptr, nullptr, &m_camera, m_renderWidget);

    // 输入路由器（P5 大文件收口：从 RenderViewport2D 中抽取事件分发逻辑）
    m_inputRouter = std::make_unique<ViewportInputRouter>(this);
    wireInputRouter();
}

RenderViewport2D::~RenderViewport2D()
{
    *m_alive = false;

    if (m_refreshCoordinator)
        m_refreshCoordinator->stop();
    // P5: 观察者注销由 SceneRefreshCoordinator 析构时自动处理
}

void RenderViewport2D::releaseGLResources()
{
    if (!m_renderWidget)
        return;

    // 先停止刷新协调器，避免释放期间还有刷新请求命中已失效的 widget
    if (m_refreshCoordinator)
        m_refreshCoordinator->stop();

    // 先从布局中移除子 RenderWidget（QOpenGLWidget），
    // 阻止 Qt 在 makeCurrent() 之后继续向它派发布局/绘制事件。
    if (auto* layout = this->layout())
        layout->removeWidget(m_renderWidget);

    // 再释放 GL 资源（内部会判断可见性，避免在无效 surface 上 makeCurrent）
    m_renderWidget->releaseGLResources();

    // 最后把子 widget 从父控件脱离，防止后续 setCentralWidget(nullptr) → hide()
    // 时 Qt 再次访问已释放的 QOpenGLWidget
    m_renderWidget->setParent(nullptr);
}

void RenderViewport2D::wireInputRouter()
{
    if (!m_inputRouter)
        return;

    m_inputRouter->setRenderWidget(m_renderWidget);
    m_inputRouter->setCamera(&m_camera);
    m_inputRouter->setDocument(m_document);
    m_inputRouter->setRefreshCoordinator(m_refreshCoordinator.get());
    m_inputRouter->setSelector(m_selector.get());
    syncInputRouterCallbacks();
}

void RenderViewport2D::syncInputRouterCallbacks()
{
    if (!m_inputRouter)
        return;

    m_inputRouter->setPositionCallback(m_positionCallback);
    m_inputRouter->setStatusCallback(m_statusCallback);
    // 缩放/平移后提交新相机矩阵并触发重绘
    m_inputRouter->setCameraChangedCallback([this]() {
        applyCameraToWidget();
        if (m_refreshCoordinator)
            m_refreshCoordinator->requestRepaint();
    });
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

Eg::SceneManager* RenderViewport2D::sceneManager() const
{
    return m_sceneManager;
}

// ==================== 外部接口实现 ====================

void RenderViewport2D::setStatusCallback(std::function<void(const QString&)> callback)
{
    m_statusCallback = std::move(callback);
    if (m_selector)
        m_selector->setStatusCallback(m_statusCallback);
    if (m_inputRouter)
        syncInputRouterCallbacks();
}

void RenderViewport2D::setSelectionCallback(std::function<void(const QString&, const QString&)> callback)
{
    m_selectionCallback = std::move(callback);
    if (m_selector)
        m_selector->setSelectionCallback(m_selectionCallback);
}

void RenderViewport2D::setCommandStageCallback(std::function<void(const QString&)> callback)
{
    m_commandStageCallback = std::move(callback);
}

void RenderViewport2D::setPositionCallback(std::function<void(double, double)> callback)
{
    m_positionCallback = std::move(callback);
    if (m_inputRouter)
        syncInputRouterCallbacks();
}

void RenderViewport2D::syncStatusMode(const QString& text)
{
    if (m_statusCallback)
        m_statusCallback(text);
}

void RenderViewport2D::syncCommandStage(const QString& text)
{
    if (m_commandStageCallback)
        m_commandStageCallback(text);
}

void RenderViewport2D::syncSelectionCallback(const QString& source, const QString& text)
{
    if (m_selectionCallback)
        m_selectionCallback(source, text);
}

void RenderViewport2D::syncSelectionToolState()
{
    if (!m_toolManager)
        return;

    auto* selectTool = dynamic_cast<SelectTool*>(m_toolManager->getActiveTool());
    if (selectTool)
        selectTool->syncSelectionFromScene();
}

void RenderViewport2D::setDocument(SceneDocument2D* document)
{
    m_document = document;

    // 缓存 SceneManager 指针，避免析构时通过 m_document 访问已释放内存
    // 阶段1收口：SceneDocument2D 不再暴露 sceneManager()，渲染桥接层通过
    // 编辑服务外观获取场景对象
    if (m_document && m_document->editService())
        m_sceneManager = m_document->editService()->sceneManager();
    else
        m_sceneManager = nullptr;

    // 同步选择控制器
    if (m_selector)
        m_selector->setSceneManager(m_sceneManager);

    // P5: 观察者注册收敛到 SceneRefreshCoordinator::setSceneManager
    // 该方法自动从旧 SceneManager 注销、向新 SceneManager 注册
    if (m_refreshCoordinator)
        m_refreshCoordinator->setSceneManager(m_sceneManager);

    // 输入路由器同步文档
    if (m_inputRouter)
        m_inputRouter->setDocument(m_document);

    // 初始刷新 - 新文档需要全量 gather，不能用增量路径
    if (m_refreshCoordinator)
        m_refreshCoordinator->requestFullRefresh();
}

void RenderViewport2D::setSelectionService(ISelectionService* service)
{
    m_selectionService = service;
    if (m_selector)
        m_selector->setSelectionService(service);
    if (m_inputRouter)
        m_inputRouter->setSelectionService(service);
}

void RenderViewport2D::setInteractionDispatcher(IInteractionDispatcher* dispatcher)
{
    m_interactionDispatcher = dispatcher;
    if (m_inputRouter)
        m_inputRouter->setInteractionDispatcher(dispatcher);
}

void RenderViewport2D::setOperationBus(OperationBus* bus)
{
    m_operationBus = bus;
    if (m_inputRouter)
        m_inputRouter->setOperationBus(bus);
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
    ToolInitializer::registerAllTools(*m_toolManager, m_sceneManager, m_renderWidget, coordinator,
        [this](const QString& msg) { updateStatus(msg); });

    // P1: 通过信号通知上层提交图元，视口不直接持有编辑服务
    m_toolManager->setEntityCallbackForAllTools([this](Eg::SyEntity* e) {
        if (e) emit entitySubmitRequested(e);
        });

    // 设置工具切换回调
    m_toolManager->setSwitchToolCallbackForAllTools([this](const QString& name) {
        setActiveTool(name);
        });

    // 注册图元编辑器
    ToolInitializer::registerAllEditors();

    // 输入路由器同步工具管理器
    if (m_inputRouter)
        m_inputRouter->setToolManager(m_toolManager.get());

    // 活动命令的事件消费绑定到工具层；输入路由器不再直接决定命令事件如何落到工具。
    if (m_interactionDispatcher)
    {
        m_interactionDispatcher->setEventHandler([this](const InteractionEvent& interaction) {
            if (!m_toolManager)
                return false;

            auto* tool = m_toolManager->getActiveTool();
            if (!tool)
                return false;

            const QPointF worldPos(interaction.x, interaction.y);
            switch (interaction.type)
            {
                case InteractionEventType::MouseDown:
                {
                    QMouseEvent event(QEvent::MouseButtonPress, worldPos,
                        Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
                    return tool->onMousePress(worldPos, &event);
                }
                case InteractionEventType::MouseMove:
                {
                    QMouseEvent event(QEvent::MouseMove, worldPos,
                        Qt::NoButton, Qt::NoButton, Qt::NoModifier);
                    return tool->onMouseMove(worldPos, &event);
                }
                case InteractionEventType::MouseUp:
                {
                    QMouseEvent event(QEvent::MouseButtonRelease, worldPos,
                        Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
                    return tool->onMouseRelease(worldPos, &event);
                }
                case InteractionEventType::KeyPress:
                {
                    QKeyEvent event(QEvent::KeyPress, interaction.key, Qt::NoModifier);
                    return tool->onKeyPress(&event);
                }
            }
            return false;
        });
    }

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
        SY_INFOF("[RenderViewport2D] active tool=%s", qPrintable(toolName));
        // 通知上层（如绘图工具栏）同步活动工具状态
        emit activeToolChanged(toolName);
    }
    else
    {
        SY_WARNF("[RenderViewport2D] Failed to set active tool: %s", qPrintable(toolName));
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
    // 相机重置到默认台面范围，视口只负责传视口尺寸和提交矩阵
    QSizeF physSize = physicalViewportSize();
    m_camera.resetToDefault(static_cast<float>(physSize.width()),
                            static_cast<float>(physSize.height()));
    applyCameraToWidget();
    updateStatus(tr("2D view reset"));
}

void RenderViewport2D::zoomToFit()
{
    // 无文档或空场景时回退到默认视图
    if (!m_document || !sceneManager())
    {
        resetView();
        return;
    }

    auto bbox = sceneManager()->sceneBBox2D();
    if (!bbox.isValid())
    {
        resetView();
        return;
    }

    // 相机编排逻辑已下沉到 Camera2D::zoomToBBox
    QSizeF physSize = physicalViewportSize();
    m_camera.zoomToBBox(static_cast<float>(physSize.width()),
                         static_cast<float>(physSize.height()),
                         static_cast<float>(bbox.minPt.x()),
                         static_cast<float>(bbox.minPt.y()),
                         static_cast<float>(bbox.maxPt.x()),
                         static_cast<float>(bbox.maxPt.y()));
    applyCameraToWidget();
    updateStatus(tr("2D zoom extents"));
}

void RenderViewport2D::zoomToSelection()
{
    // BBox 计算已下沉到 ViewportSelector::selectionBBox
    if (!m_selector)
    {
        updateStatus(tr("No selection service"));
        return;
    }

    auto bboxOpt = m_selector->selectionBBox();
    if (!bboxOpt)
    {
        updateStatus(tr("No entities selected"));
        return;
    }

    // 相机编排逻辑已下沉到 Camera2D::zoomToBBox
    QSizeF physSize = physicalViewportSize();
    m_camera.zoomToBBox(static_cast<float>(physSize.width()),
                         static_cast<float>(physSize.height()),
                         static_cast<float>(bboxOpt->minPt.x()),
                         static_cast<float>(bboxOpt->minPt.y()),
                         static_cast<float>(bboxOpt->maxPt.x()),
                         static_cast<float>(bboxOpt->maxPt.y()));
    applyCameraToWidget();
    updateStatus(tr("2D zoom to selection"));
}

void RenderViewport2D::zoomIn()
{
    QSizeF physSize = physicalViewportSize();
    float vpW = static_cast<float>(physSize.width());
    float vpH = static_cast<float>(physSize.height());
    if (vpW <= 0 || vpH <= 0)
        return;
    // 以视口中心为锚点放大
    m_camera.zoomAtCenter(1.25f, vpW, vpH);
    applyCameraToWidget();
}

void RenderViewport2D::zoomOut()
{
    QSizeF physSize = physicalViewportSize();
    float vpW = static_cast<float>(physSize.width());
    float vpH = static_cast<float>(physSize.height());
    if (vpW <= 0 || vpH <= 0)
        return;
    // 以视口中心为锚点缩小
    m_camera.zoomAtCenter(1.0f / 1.25f, vpW, vpH);
    applyCameraToWidget();
}

void RenderViewport2D::setPanModeEnabled(bool enabled)
{
    if (m_inputRouter)
        m_inputRouter->setPanModeEnabled(enabled);
    updateStatus(enabled ? tr("2D pan mode") : tr("2D select mode"));
}

bool RenderViewport2D::isPanModeEnabled() const
{
    return m_inputRouter ? m_inputRouter->isPanModeEnabled() : false;
}

void RenderViewport2D::setDrawingEnabled(bool enabled)
{
    syncCommandStage(enabled ? tr("Waiting for first point") : tr("Idle"));
    syncStatusMode(enabled ? tr("2D draw mode") : tr("2D select mode"));
}

void RenderViewport2D::setMeasureMode(bool enabled)
{
    syncCommandStage(enabled ? tr("Waiting for first point") : tr("Idle"));
    syncStatusMode(enabled ? tr("2D measure mode") : tr("2D select mode"));
}

// ==================== 选择/编辑操作（P5 下沉到 ViewportSelector / SceneEditService） ====================

QString RenderViewport2D::selectedEntityId() const
{
    // 委托给 ViewportSelector
    return m_selector ? m_selector->selectedEntityId() : QString();
}

void RenderViewport2D::deleteSelectedEntity()
{
    if (!m_document)
        return;
    auto selectedId = selectedEntityId();
    if (selectedId.isEmpty())
        return;

    // 删除走 OperationBus 命令路径，场景变更自动触发 SceneNotifier → 增量刷新
    if (m_operationBus)
        m_operationBus->run(OperationId::Edit_Delete, {}, OperationSource::DrawTool);

    // 清除选择（SceneNotifier 已通过 onSceneChanged 触发增量刷新，无需显式 requestFullRefresh）
    if (m_selector)
        m_selector->clearSelection();
    updateStatus(tr("2D entity deleted"));
}

void RenderViewport2D::nudgeSelectedEndpoint(const QPointF& delta)
{
    if (!m_document)
        return;
    auto selectedId = selectedEntityId();
    if (selectedId.isEmpty())
        return;

    // P1: 通过信号通知上层执行 nudge，视口不直接持有编辑服务
    emit nudgeRequested(delta.x(), delta.y());

    syncSelectionDetails();
    updateStatus(tr("2D endpoint moved"));
    // 图元修改后增量刷新：onSceneChanged 已收集脏 ID，LightUpdate 足够
    if (m_refreshCoordinator)
        m_refreshCoordinator->requestLightRefresh();
}

void RenderViewport2D::selectEntityById(const QString& entityId)
{
    if (!m_document)
        return;
    // 选择管理委托给 ViewportSelector
    if (m_selector)
        m_selector->selectEntityById(entityId);
    syncSelectionDetails();
    syncStatusMode(tr("2D entity selected"));
    requestRepaint();
}

void RenderViewport2D::syncSelectionDetails()
{
    syncSelectionToolState();
}

void RenderViewport2D::clearSelection()
{
    // 委托给 ViewportSelector
    if (m_selector)
        m_selector->clearSelection();
    requestRepaint();
}

QPointF RenderViewport2D::mapToScene(const QPoint& screenPos) const
{
    // P5 收口: 简化坐标转换，委托给 widgetToWorld
    QPoint widgetPos = screenPos;
    if (m_renderWidget)
        widgetPos = m_renderWidget->mapFromParent(screenPos);
    return widgetToWorld(widgetPos);
}

// ==================== 事件处理（委托给 ViewportInputRouter） ====================

void RenderViewport2D::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    QTimer::singleShot(0, this, [this]() {
        resetView();
        updateViewMatrix();

        // 注意：此处不能调用 m_refreshCoordinator->stop()。
        // stop() 会移除 SceneManager 观察者并将 m_sceneManager 置空，
        // 导致此后导入/绘制触发的场景变更通知无法到达刷新协调器，图元不显示。
        if (!m_document && m_renderWidget && m_renderWidget->isInitialized())
        {
            // 标记场景环境为脏，由 paintGL 在下一帧统一计算提交
            m_renderWidget->markSceneEnvDirty();
        }
        else if (m_document)
        {
            if (m_refreshCoordinator)
                m_refreshCoordinator->requestFullRefresh();
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
    return m_inputRouter->eventFilter(obj, event);
}

void RenderViewport2D::mousePressEvent(QMouseEvent* event)
{
    if (m_inputRouter)
        m_inputRouter->handleMousePress(event);
}

void RenderViewport2D::mouseMoveEvent(QMouseEvent* event)
{
    if (m_inputRouter)
        m_inputRouter->handleMouseMove(event);
}

void RenderViewport2D::mouseReleaseEvent(QMouseEvent* event)
{
    if (m_inputRouter)
        m_inputRouter->handleMouseRelease(event);
}

void RenderViewport2D::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (m_inputRouter)
        m_inputRouter->handleMouseDoubleClick(event);
}

void RenderViewport2D::wheelEvent(QWheelEvent* event)
{
    if (m_inputRouter)
        m_inputRouter->handleWheel(event);
    updateViewMatrix();
}

void RenderViewport2D::keyPressEvent(QKeyEvent* event)
{
    if (m_inputRouter)
        m_inputRouter->handleKeyPress(event);
}

void RenderViewport2D::contextMenuEvent(QContextMenuEvent* event)
{
    if (m_inputRouter)
        m_inputRouter->handleContextMenu(event);
}

// ==================== 内部方法 ====================

QSizeF RenderViewport2D::physicalViewportSize() const
{
    // P5 收口: 委托给 ViewportInputRouter，消除与 ViewportInputRouter 的重复实现
    return m_inputRouter ? m_inputRouter->physicalViewportSize() : QSizeF(0, 0);
}

QPointF RenderViewport2D::widgetToWorld(QPoint widgetLocalPos) const
{
    // P5 收口: 委托给 ViewportInputRouter，消除与 ViewportInputRouter 的重复实现
    return m_inputRouter ? m_inputRouter->widgetToWorld(widgetLocalPos) : QPointF();
}

void RenderViewport2D::updateViewMatrix()
{
    applyCameraToWidget();
}

void RenderViewport2D::applyCameraToWidget()
{
    if (!m_renderWidget)
        return;

    QSizeF physSize = physicalViewportSize();
    float vpW = static_cast<float>(physSize.width());
    float vpH = static_cast<float>(physSize.height());

    if (vpW <= 0 || vpH <= 0)
        return;

    // Camera2D 直接返回 Mat3f，视口只负责提交给渲染控件
    Render::Mat3f mat = m_camera.viewMatrix(vpW, vpH);
    m_renderWidget->setViewMatrix(mat);

    // 视图矩阵变化后标记场景环境为脏，由 paintGL 统一重算
    if (m_renderWidget->isInitialized())
        m_renderWidget->markSceneEnvDirty();
}

// ==================== 刷新策略委托（→ SceneRefreshCoordinator） ====================

void RenderViewport2D::requestRepaint()
{
    // 纯视觉刷新
    if (m_refreshCoordinator)
        m_refreshCoordinator->requestRepaint();
}

void RenderViewport2D::requestLightRefresh()
{
    // 增量刷新
    if (m_refreshCoordinator)
        m_refreshCoordinator->requestLightRefresh();
}

void RenderViewport2D::requestFullRefresh()
{
    // 全量刷新
    if (m_refreshCoordinator)
        m_refreshCoordinator->requestFullRefresh();
}

// P5: onSceneChanged / onSelectionChanged 已收敛到 SceneRefreshCoordinator（IObserver）
// 视口不再直接实现 IObserver，通过 selectionChanged 信号同步工具状态

void RenderViewport2D::updateStatus(const QString& text)
{
    syncStatusMode(text);
}