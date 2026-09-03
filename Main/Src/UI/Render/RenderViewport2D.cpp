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

#include "UI2D/DrawTools/ToolManager.h"
#include "UI2D/DrawTools/SelectTool.h"
#include "UI2D/ViewWidget/ToolInitializer.h"
#include "UI2D/ViewWidget/ViewRenderCoordinator.h"

#include <QVBoxLayout>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QContextMenuEvent>
#include <QCursor>

#include "Log/SyLogger.h"
#include <QTimer>

// ==================== RenderViewport2D 实现 ====================

RenderViewport2D::RenderViewport2D(QWidget* parent)
    : QWidget(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setAutoFillBackground(false);
    setMinimumSize(640, 480);

    initRenderWidget();

    // 刷新协调器：封装四级刷新策略与增量渲染管线
    m_refreshCoordinator = std::make_unique<SceneRefreshCoordinator>(this);
    m_refreshCoordinator->setRenderWidget(m_renderWidget);

    // P5: 观察者注册收敛到 SceneRefreshCoordinator，视口通过信号同步工具状态
    QObject::connect(m_refreshCoordinator.get(), &SceneRefreshCoordinator::selectionChanged, this, [this]() {
        syncSelectionDetails();
    });

    // 初始相机状态：台面中心 (600,400)，可见范围 (0,0)~(1200,800)
    m_camera.panOffset = QPointF(-600.0f, -400.0f);

    // 选中集包围盒查询器（zoom_selection 等视图操作用；场景/选择服务在 setDocument/setSelectionService 注入）
    m_selector = std::make_unique<ViewportSelector>(nullptr, nullptr);

    // 输入路由器（P5 大文件收口：从 RenderViewport2D 中抽取事件分发逻辑）
    m_inputRouter = std::make_unique<ViewportInputRouter>(this);
    wireInputRouter();

    // 网格+对象捕捉管理器：引擎层纯计算门面，场景接入见 setDocument()
    m_gridSnapManager = std::make_unique<GridSnapManager>();
    m_inputRouter->setSnapPositionCallback([this](const QPointF& p) {
        return applySnap(p);
    });
}

RenderViewport2D::~RenderViewport2D()
{
    *m_alive = false;

    if (m_refreshCoordinator)
    {
        m_refreshCoordinator->stop();
    }
    // P5: 观察者注销由 SceneRefreshCoordinator 析构时自动处理
}

void RenderViewport2D::releaseGLResources()
{
    if (!m_renderWidget)
    {
        return;
    }

    // 1) 先停外部刷新来源，避免释放期间还有刷新请求命中已失效的 Runtime。
    //    控件内部的动画定时器由 RenderWidget::releaseGLResources 自己停——
    //    谁拥有重绘来源，谁负责关掉它。
    if (m_refreshCoordinator)
    {
        m_refreshCoordinator->stop();
    }

    // 2) 释放 GL 资源。上下文的当前性由 RenderWidget 自己保证（只有它知道自己的
    //    QOpenGLContext），这里不包 makeCurrent。
    m_renderWidget->releaseGLResources();

    // 释放后 m_session 已失效，RenderWidget::renderFrame 会在入口直接返回，
    // 因此后续 setCentralWidget(nullptr) → hide() 触发的绘制事件都是空操作。
    //
    // 这里刻意**不**做 layout()->removeWidget() + setParent(nullptr)：
    // 那是为了规避「hide() 期间访问失效 native handle」而加的手法，但那次崩溃的
    // 真因是 GL 删除没有当前上下文（已在 RenderWidget 内修正）。把子控件摘成
    // 孤立顶层反而有两个副作用：本视口析构时不再级联删除它（每次工作台切换泄漏
    // 一个 RenderWidget），且这个孤立控件仍会继续收到定时器与绘制事件。
}

void RenderViewport2D::wireInputRouter()
{
    if (!m_inputRouter)
    {
        return;
    }

    m_inputRouter->setRenderWidget(m_renderWidget);
    m_inputRouter->setCamera(&m_camera);
    m_inputRouter->setDocument(m_document);
    m_inputRouter->setRefreshCoordinator(m_refreshCoordinator.get());
    syncInputRouterCallbacks();
}

void RenderViewport2D::syncInputRouterCallbacks()
{
    if (!m_inputRouter)
    {
        return;
    }

    m_inputRouter->setPositionCallback(m_positionCallback);
    m_inputRouter->setStatusCallback(m_statusCallback);
    // 缩放/平移后提交新相机矩阵并触发重绘
    m_inputRouter->setCameraChangedCallback([this]() {
        applyCameraToWidget();
        if (m_refreshCoordinator)
        {
            m_refreshCoordinator->requestRepaint();
        }
    });
    // 回车键缩放到选中图元范围
    m_inputRouter->setZoomToSelectionCallback([this]() {
        zoomToSelection();
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

// 对世界坐标应用吸附（图元/网格/起点），并刷新捕捉指示器。
// 捕捉由引擎层 GridSnapManager::snap() 完成（内部 SnapEngine 纯计算，无 UI 依赖）。
QPointF RenderViewport2D::applySnap(const QPointF& worldPos) const
{
    if (!m_gridSnapManager)
    {
        return worldPos;
    }

    const Ut::Vec2d src(worldPos.x(), worldPos.y());
    const Ut::Vec2d snapped = m_gridSnapManager->snap(src);

    const bool didSnap = (snapped.x() != src.x()) || (snapped.y() != src.y());
    if (m_renderCoordinator)
    {
        m_renderCoordinator->setSnapIndicator(snapped, didSnap);
    }

    return QPointF(snapped.x(), snapped.y());
}

// ==================== 外部接口实现 ====================

void RenderViewport2D::setStatusCallback(std::function<void(const QString&)> callback)
{
    m_statusCallback = std::move(callback);
    if (m_inputRouter)
    {
        syncInputRouterCallbacks();
    }
}

void RenderViewport2D::setCommandStageCallback(std::function<void(const QString&)> callback)
{
    m_commandStageCallback = std::move(callback);
}

void RenderViewport2D::setPositionCallback(std::function<void(double, double)> callback)
{
    m_positionCallback = std::move(callback);
    if (m_inputRouter)
    {
        syncInputRouterCallbacks();
    }
}

void RenderViewport2D::syncStatusMode(const QString& text)
{
    if (m_statusCallback)
    {
        m_statusCallback(text);
    }
}

void RenderViewport2D::syncCommandStage(const QString& text)
{
    if (m_commandStageCallback)
    {
        m_commandStageCallback(text);
    }
}

void RenderViewport2D::syncSelectionToolState()
{
    if (!m_toolManager)
    {
        return;
    }

    auto* selectTool = dynamic_cast<SelectTool*>(m_toolManager->getActiveTool());
    if (selectTool)
    {
        selectTool->syncSelectionFromScene();
    }
}

void RenderViewport2D::setDocument(SceneDocument2D* document)
{
    m_document = document;

    // 缓存 SceneManager 指针，避免析构时通过 m_document 访问已释放内存
    // 阶段1收口：SceneDocument2D 不再暴露 sceneManager()，渲染桥接层通过
    // 编辑服务外观获取场景对象
    if (m_document && m_document->editService())
    {
        m_sceneManager = m_document->editService()->sceneManager();
    }
    else
    {
        m_sceneManager = nullptr;
    }

    // 同步选择控制器
    if (m_selector)
    {
        m_selector->setSceneManager(m_sceneManager);
    }

    // P5: 观察者注册收敛到 SceneRefreshCoordinator::setSceneManager
    // 该方法自动从旧 SceneManager 注销、向新 SceneManager 注册
    if (m_refreshCoordinator)
    {
        m_refreshCoordinator->setSceneManager(m_sceneManager);
    }

    // 输入路由器同步文档
    if (m_inputRouter)
    {
        m_inputRouter->setDocument(m_document);
    }

    // 捕捉管理器接入场景：对象捕捉从 SceneManager 提取候选图元。
    // 优先使用场景空间索引按捕捉半径做区域查询，无索引时 SnapEngine 自动回退全量遍历。
    if (m_gridSnapManager)
    {
        m_gridSnapManager->setSceneManager(m_sceneManager);
        if (m_sceneManager)
        {
            m_gridSnapManager->setSpatialQueryCallback([scene = m_sceneManager](const Ut::BBox2d& box) {
                return scene->queryByBox(box, /*containedOnly=*/false);
            });
        }
        else
        {
            m_gridSnapManager->setSpatialQueryCallback(nullptr);
        }
    }

    // 初始刷新 - 新文档需要全量 gather，不能用增量路径
    if (m_refreshCoordinator)
    {
        m_refreshCoordinator->requestFullRefresh();
    }
}

void RenderViewport2D::setSelectionService(ISelectionService* service)
{
    m_selectionService = service;
    if (m_selector)
    {
        m_selector->setSelectionService(service);
    }
    if (m_inputRouter)
    {
        m_inputRouter->setSelectionService(service);
    }
}

void RenderViewport2D::setInteractionDispatcher(IInteractionDispatcher* dispatcher)
{
    m_interactionDispatcher = dispatcher;
    if (m_inputRouter)
    {
        m_inputRouter->setInteractionDispatcher(dispatcher);
    }
}

void RenderViewport2D::setOperationBus(OperationBus* bus)
{
    m_operationBus = bus;
    if (m_inputRouter)
    {
        m_inputRouter->setOperationBus(bus);
    }
}

void RenderViewport2D::setLayerManager(LayerManager* manager)
{
    m_layerManager = manager;
}

void RenderViewport2D::initializeTools()
{
    if (!m_renderWidget || !m_sceneManager)
    {
        return;
    }

    m_toolManager = std::make_unique<ToolManager>();

    // 创建渲染协调器（唯一持有，用于覆盖层/捕捉指示器）
    m_renderCoordinator = std::make_unique<Ui2D::ViewRenderCoordinator>();
    m_renderCoordinator->setRenderWidget(m_renderWidget);
    auto* coordinator = m_renderCoordinator.get();

    // 注册所有工具。
    // 额外注入场景编辑服务（Gizmo 变换 Undo）、图层管理器（锁定图层过滤）、
    // 重置视图回调（空格键）、网格+对象捕捉管理器等选择工具所需的依赖。
    ToolInitializer::registerAllTools(
        *m_toolManager,
        m_sceneManager,
        m_renderWidget,
        coordinator,
        [this](const QString& msg) {
            updateStatus(msg);
        },
        /*sceneEdit=*/m_document ? m_document->editService() : nullptr,
        /*layerManager=*/m_layerManager,
        /*onResetView=*/
        [this]() {
            zoomToFit();
        },
        /*onEntityDoubleClick=*/nullptr,
        /*gridSnapManager=*/m_gridSnapManager.get(),
        /*operationBus=*/m_operationBus,
        /*panViewByPixels=*/
        [this](double dxPx, double dyPx) {
            // 工具侧按**逻辑**像素表达平移意图，相机吃的是物理像素：DPR 换算在这里做一次，
            // 免得每个工具各自去查 devicePixelRatio。
            if (!m_inputRouter || !m_renderWidget)
            {
                return;
            }
            const double dpr = static_cast<double>(m_renderWidget->devicePixelRatio());
            m_inputRouter->panViewByPhysicalPixels(dxPx * dpr, dyPx * dpr);
        });

    // P1: 通过信号通知上层提交图元，视口不直接持有编辑服务
    m_toolManager->setEntityCallbackForAllTools([this](Eg::SyEntity* e) {
        if (e)
        {
            emit entitySubmitRequested(e);
        }
    });

    // 设置工具切换回调
    m_toolManager->setSwitchToolCallbackForAllTools([this](const QString& name) {
        setActiveTool(name);
    });

    // 注册图元编辑器
    ToolInitializer::registerAllEditors();

    // 输入路由器同步工具管理器
    if (m_inputRouter)
    {
        m_inputRouter->setToolManager(m_toolManager.get());
    }

    // 活动命令的事件消费绑定到工具层；输入路由器不再直接决定命令事件如何落到工具。
    if (m_interactionDispatcher)
    {
        m_interactionDispatcher->setEventHandler([this](const InteractionEvent& interaction) {
            if (!m_toolManager)
            {
                return false;
            }

            auto* tool = m_toolManager->getActiveTool();
            if (!tool)
            {
                return false;
            }

            // 统一应用吸附（与输入路由器同一入口），保证命令/交互分发路径也可捕捉
            const QPointF worldPos = applySnap(QPointF(interaction.x, interaction.y));
            switch (interaction.type)
            {
            case InteractionEventType::MouseDown:
            {
                QMouseEvent event(
                    QEvent::MouseButtonPress, worldPos, worldPos, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
                return tool->onMousePress(worldPos, &event);
            }
            case InteractionEventType::MouseMove:
            {
                QMouseEvent event(QEvent::MouseMove, worldPos, worldPos, Qt::NoButton, Qt::NoButton, Qt::NoModifier);
                return tool->onMouseMove(worldPos, &event);
            }
            case InteractionEventType::MouseUp:
            {
                QMouseEvent event(
                    QEvent::MouseButtonRelease, worldPos, worldPos, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
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
    {
        return false;
    }

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
    {
        return QString();
    }
    return m_toolManager->getActiveToolName();
}

ToolManager* RenderViewport2D::toolManager() const
{
    return m_toolManager.get();
}

bool RenderViewport2D::handleEscapeRequest()
{
    return m_inputRouter ? m_inputRouter->handleEscapeRequest() : false;
}

bool RenderViewport2D::handleStepBackRequest()
{
    return m_inputRouter ? m_inputRouter->handleStepBackRequest() : false;
}

bool RenderViewport2D::handleTextDeleteRequest(bool forward)
{
    return m_inputRouter ? m_inputRouter->handleTextDeleteRequest(forward) : false;
}

bool RenderViewport2D::handleTextUndoRequest(bool redo)
{
    return m_inputRouter ? m_inputRouter->handleTextUndoRequest(redo) : false;
}

void RenderViewport2D::resetView()
{
    // 相机重置到默认台面范围，视口只负责传视口尺寸和提交矩阵
    QSizeF physSize = physicalViewportSize();
    m_camera.resetToDefault(static_cast<float>(physSize.width()), static_cast<float>(physSize.height()));
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
        // 没有选中图元时，重置视图（与空格键行为一致）
        zoomToFit();
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
    {
        return;
    }
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
    {
        return;
    }
    // 以视口中心为锚点缩小
    m_camera.zoomAtCenter(1.0f / 1.25f, vpW, vpH);
    applyCameraToWidget();
}

void RenderViewport2D::setPanModeEnabled(bool enabled)
{
    if (m_inputRouter)
    {
        m_inputRouter->setPanModeEnabled(enabled);
    }
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

// ==================== 选择状态广播 ====================

void RenderViewport2D::syncSelectionDetails()
{
    syncSelectionToolState();
    // 通知上层：选择状态已变化（覆盖绘制后自动选中、点选/框选、撤销等所有路径）
    emit selectionChanged();
}

QPointF RenderViewport2D::mapToScene(const QPoint& screenPos) const
{
    // P5 收口: 简化坐标转换，委托给 widgetToWorld
    QPoint widgetPos = screenPos;
    if (m_renderWidget)
    {
        widgetPos = m_renderWidget->mapFromParent(screenPos);
    }
    return widgetToWorld(widgetPos);
}

QPointF RenderViewport2D::mapGlobalToScene(const QPoint& globalPos) const
{
    if (!m_renderWidget)
    {
        return QPointF();
    }
    // 全局屏幕坐标 → RenderWidget 本地坐标（设备无关像素），再反算世界坐标
    const QPoint local = m_renderWidget->mapFromGlobal(globalPos);
    return widgetToWorld(local);
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
            {
                m_refreshCoordinator->requestFullRefresh();
            }
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
    {
        m_inputRouter->handleMousePress(event);
    }
}

void RenderViewport2D::mouseMoveEvent(QMouseEvent* event)
{
    if (m_inputRouter)
    {
        m_inputRouter->handleMouseMove(event);
    }
}

void RenderViewport2D::mouseReleaseEvent(QMouseEvent* event)
{
    if (m_inputRouter)
    {
        m_inputRouter->handleMouseRelease(event);
    }
}

void RenderViewport2D::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (m_inputRouter)
    {
        m_inputRouter->handleMouseDoubleClick(event);
    }
}

void RenderViewport2D::wheelEvent(QWheelEvent* event)
{
    if (m_inputRouter)
    {
        m_inputRouter->handleWheel(event);
    }
    updateViewMatrix();
}

void RenderViewport2D::keyPressEvent(QKeyEvent* event)
{
    if (m_inputRouter)
    {
        m_inputRouter->handleKeyPress(event);
    }
}

void RenderViewport2D::contextMenuEvent(QContextMenuEvent* event)
{
    // 处于交互命令（如绘制）中时，交给输入路由取消当前命令并忽略菜单
    if (m_inputRouter && m_inputRouter->hasActiveCommand())
    {
        m_inputRouter->handleContextMenu(event);
        return;
    }
    // 否则通知上层（Workbench）基于命令中枢构建并弹出右键菜单，实现选择/锁定的实时联动
    emit contextMenuRequested(event);
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

QPointF RenderViewport2D::pasteAnchorWorld() const
{
    // 优先使用最近记录的鼠标世界坐标（鼠标在视口内移动过），保证粘贴跟随鼠标；
    // 否则回退到当前光标位置（若在视口内），最后回退到视口中心。
    if (m_inputRouter && m_inputRouter->hasCursorPos())
    {
        return m_inputRouter->lastCursorWorldPos();
    }

    const QPoint cursorLocal = mapFromGlobal(QCursor::pos());
    const QRect viewRect = rect();
    const QPoint anchorPx = viewRect.contains(cursorLocal) ? cursorLocal : viewRect.center();
    return widgetToWorld(anchorPx);
}

void RenderViewport2D::updateViewMatrix()
{
    applyCameraToWidget();
}

void RenderViewport2D::applyCameraToWidget()
{
    if (!m_renderWidget)
    {
        return;
    }

    QSizeF physSize = physicalViewportSize();
    float vpW = static_cast<float>(physSize.width());
    float vpH = static_cast<float>(physSize.height());

    if (vpW <= 0 || vpH <= 0)
    {
        return;
    }

    // Camera2D 直接返回 Mat3f，视口只负责提交给渲染控件
    Render::Mat3f mat = m_camera.viewMatrix(vpW, vpH);
    m_renderWidget->setViewMatrix(mat);

    // 视图矩阵变化后标记场景环境为脏，由 paintGL 统一重算
    if (m_renderWidget->isInitialized())
    {
        m_renderWidget->markSceneEnvDirty();
    }

    // 选中轮廓的离散密度按像素给定，缩放跨过一定倍数后必须重建。
    // 只按倍数判定、不是每次相机变化都重建：平移与微小缩放不改变屏幕上的边长量级，
    // 而重建要把整个选中集重新离散一遍 —— 选中上万个图元时那是逐帧都做不起的。
    const float scale = m_renderWidget->pixelToWorldScale();
    if (scale > 0.0f)
    {
        constexpr float kRebuildRatio = 1.5f;
        const bool crossed = m_outlineScaleAtBuild <= 0.0f || scale > m_outlineScaleAtBuild * kRebuildRatio ||
                             scale * kRebuildRatio < m_outlineScaleAtBuild;
        if (crossed)
        {
            m_outlineScaleAtBuild = scale;
            syncSelectionToolState();
        }
    }
}


// ==================== 刷新策略委托（→ SceneRefreshCoordinator） ====================

void RenderViewport2D::requestRepaint()
{
    // 纯视觉刷新
    if (m_refreshCoordinator)
    {
        m_refreshCoordinator->requestRepaint();
    }
}

void RenderViewport2D::requestLightRefresh()
{
    // 增量刷新
    if (m_refreshCoordinator)
    {
        m_refreshCoordinator->requestLightRefresh();
    }
}

void RenderViewport2D::requestFullRefresh()
{
    // 全量刷新
    if (m_refreshCoordinator)
    {
        m_refreshCoordinator->requestFullRefresh();
    }
}

// P5: onSceneChanged / onSelectionChanged 已收敛到 SceneRefreshCoordinator（IObserver）
// 视口不再直接实现 IObserver，通过 selectionChanged 信号同步工具状态

void RenderViewport2D::updateStatus(const QString& text)
{
    syncStatusMode(text);
}