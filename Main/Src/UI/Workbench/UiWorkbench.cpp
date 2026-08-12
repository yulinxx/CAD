#include "UiWorkbench.h"

#include <QAction>
#include <QActionGroup>
#include <QShortcut>
#include <QSizePolicy>
#include <QTextEdit>
#include <QToolBar>
#include <QWidget>
#include <QEvent>
#include <QKeyEvent>
#include <QTimer>
#include <QStatusBar>

#include "SceneDocument2D.h"
#include "UiServices.h"
#include "UiStateCenter.h"
#include "Engine2D/Edit/IUndoRedoManager.h"
#include "UiSceneTreeDock.h"
#include "UiPropertiesPanel.h"
#include "RenderViewport2D.h"
#include "DrawToolBarWidget.h"
#include "DrawToolSwitchRegistry.h"
#include "WorkbenchWindow.h"

#include "UI2D/Operation/CommandActionHub.h"
#include "UI2D/Operation/OperationBus.h"
#include "UI2D/Operation/OperationId.h"
#include "UI2D/Operation/OperationRouting.h"
#include "UI2D/Operation/CommandCatalog.h"
#include "UI2D/Edit/QtLayerManagerBridge.h"

#include "UI/RightToolBar/RightToolBar.h"
#include "UI/TopToolBar/TopToolBar.h"
#include "UI/Dlg/LayerManagerDialog.h"

#include "Engine2D/Edit/LayerEditService.h"
#include "Engine2D/Edit/SceneEditService.h"
#include "Engine2D/Interaction/LayerManager.h"
#include "UI2D/Operation/CommandCatalog.h"
#include "UI2D/Operation/OperationId.h"
#include "UI2D/Operation/OperationBus.h"
#include "UiStateCenter.h"

#include "Import/ImportService.h"
#include "Color/Color.hpp"
#include "Log/SyLogger.h"
#include "UI/StatusBar/StatusBar.h"

#if BUILD_UI3D
#include "UiEntities.h"
#include "UiViewport3D.h"
#include "SceneBuilder3D.h"

// 3D 服务与操作头文件（从 UiWorkbench.h 前向声明下沉）
#include "UI/TopToolBar/TopToolBar3D.h"
#include "UI/StatusBar/StatusBar3D.h"
#include "UI/MainWindow/MainWindow3D.h"
#include "UI/MenuManager/MenuManager3D.h"
#include "UI/Algorithm/AlgorithmApplicationService.h"

#include "Engine3D/SceneManager3D.h"
#include "UI3D/Service/ServicePack3D.h"
#include "UI3D/Operation/OperationBus3D.h"
#include "UI3D/Operation/CommandCatalog3D.h"
#include "UI3D/Manager/DocumentManager3D.h"
#include "UI3D/Edit/UndoRedoManager3D.h"
#include "UI3D/Edit/SceneEditService3D.h"
#include "UI3D/Service/SceneMonitor3D.h"
#include "UI3D/Shortcut/ShortcutManager3D.h"
#include "UI3D/Navigation/NavigationConfig3D.h"
#include "UI3D/Service/SceneDocument3D.h"
#include "UI3D/Service/CameraController3D.h"
#include "UI3D/Settings/SettingsUiCoordinator3D.h"
#include "UI3D/Operation/CommandActionHub3D.h"
#include "UI3D/Operation/AlgorithmRunner3D.h"
#include "UI/Settings/SettingsService.h"

#ifdef ENABLE_GEOMODELCORE
#include "UI3D/Service/BRepModelService3D.h"
#endif

// Workbench3D::ServiceOwner 定义（PIMPL：从 UiWorkbench.h 移至此）
struct Workbench3D::ServiceOwner
{
    std::unique_ptr<OperationBus3D> operationBus;
    std::unique_ptr<DocumentManager3D> documentManager;
    std::unique_ptr<UndoRedoManager3D> undoRedoManager;
    std::unique_ptr<SceneEditService3D> sceneEditService;
    std::unique_ptr<SceneMonitor3D> sceneMonitor;
    std::unique_ptr<ShortcutManager3D> shortcutManager;
    std::unique_ptr<NavigationConfig3D> navigationConfig;
    std::unique_ptr<SceneDocument3D> sceneDocument;
    std::unique_ptr<SceneDocument3DAdapter> sceneDocumentAdapter;
    std::unique_ptr<CameraController3D> cameraController;
    std::unique_ptr<AlgorithmApplicationService> algorithmService;
    std::unique_ptr<SettingsUiCoordinator3D> settingsCoordinator;
    std::unique_ptr<SettingsService> settingsService;
    std::unique_ptr<CommandActionHub3D> commandActionHub;
    std::unique_ptr<AlgorithmRunner3D> algorithmRunner;

#ifdef ENABLE_GEOMODELCORE
    std::unique_ptr<BRepModelService3D> brepModelService;
#endif
};

// 自定义删除器定义（ServiceOwner 在此处已完整定义）
void Workbench3D::ServiceOwnerDeleter::operator()(ServiceOwner *p) const
{
    delete p;
}
#endif

namespace
{
/// 创建面板部件
/// @param text 面板文本内容
/// @param parent 父部件
QWidget *createPanelWidget(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setAlignment(Qt::AlignCenter);
    label->setStyleSheet(QStringLiteral("background:#f0f0f0;border:1px solid #ccc;padding:20px;"));
    return label;
}
} // namespace

WorkbenchStateSnapshot UiWorkbench::currentSnapshot() const
{
    WorkbenchStateSnapshot snapshot = m_savedState;
    if (m_services.stateCenter)
    {
        auto snap = m_services.stateCenter->snapshot();
        snapshot.viewMode = snap.currentViewMode;
        snapshot.layerId = snap.currentLayerId;
        snapshot.documentId = snap.currentDocumentId;
        snapshot.selectionSource = snap.currentSelectionSource;
        snapshot.selectionText = snap.currentSelectionText;
        snapshot.selectionType = snap.currentSelectionType;
        snapshot.dirty = snap.dirty;
        // 工具/输入状态（工作台切换时恢复）
        snapshot.activeToolId = snap.activeToolId;
        snapshot.inputFocusWidget = snap.inputFocusWidget;
    }
    return snapshot;
}

void UiWorkbench::restoreFromSnapshot(const WorkbenchStateSnapshot &snapshot)
{
    if (m_services.stateCenter)
    {
        m_services.stateCenter->setCurrentViewMode(snapshot.viewMode);
        m_services.stateCenter->setCurrentLayerId(snapshot.layerId);
        m_services.stateCenter->setCurrentDocumentId(snapshot.documentId);
        // 工具/输入状态恢复（仅写入状态中心，子类 activate() 负责应用到视口）
        m_services.stateCenter->setActiveToolId(snapshot.activeToolId);
        m_services.stateCenter->setInputFocusWidget(snapshot.inputFocusWidget);
    }
}

// ==================== 框架层委托接口（默认实现） ====================

bool UiWorkbench::isCommandRegistered(const QString & /*commandId*/) const
{
    return false;
}

void UiWorkbench::dispatchCommand(const QString & /*commandId*/)
{
    SY_WARN("[UiWorkbench] Command dispatch requested without a workbench adapter");
}

QString UiWorkbench::commandText(const QString & /*commandId*/) const
{
    return QString();
}

void UiWorkbench::releaseCentralWidgetGLResources(QWidget * /*centralWidget*/) const
{
    // 默认不释放任何资源，子类按需重写
}

QString UiWorkbench::formatSelectionText(const UiStateSnapshot &state) const
{
    // 默认格式：通用格式，子类按需重写（如 2D/3D 专用格式）
    return QObject::tr("Sel=%1 | SelSrc=%2 | CmdSrc=%3 | SelType=%4 | CmdType=%5")
        .arg(state.currentSelectionText)
        .arg(state.currentSelectionSource)
        .arg(state.currentCommandOwner)
        .arg(state.currentSelectionType)
        .arg(state.currentCommandType);
}

bool UiWorkbench::requiresSkeletonDocks() const
{
    return true;
}

bool UiWorkbench::managesOwnMenus() const
{
    return false;
}

// ============================================================
// Workbench2D 实现
QString Workbench2D::id() const
{
    return QStringLiteral("2D");
}

namespace
{
bool workbenchFlagEnabled(const QStringList &workbenches, const QString &workbenchId)
{
    if (workbenches.isEmpty())
        return true;
    for (const auto &wb : workbenches)
    {
        if (wb.compare(workbenchId, Qt::CaseInsensitive) == 0)
            return true;
    }
    return false;
}
} // namespace

bool Workbench2D::isCommandRegistered(const QString &commandId) const
{
    return CommandCatalog::operationForCommandId(commandId) != OperationId::None;
}

void Workbench2D::dispatchCommand(const QString &commandId)
{
    if (!m_services.operationBus)
    {
        SY_WARNF("[Workbench2D] Cannot dispatch command without OperationBus: %s", qPrintable(commandId));
        return;
    }

    // 优先走 OperationRouting::dispatch：与工具栏/右键/快捷键同一条路径，
    // 保证 Edit_Rotate/Align 的 angle/mode 参数、Move/Mirror 的对话框分发完全一致。
    const UI::MenuActionId menuId = CommandCatalog::menuIdForCommandId(commandId);
    if (menuId != static_cast<UI::MenuActionId>(0))
    {
        SY_INFOF("[Workbench2D] Dispatch command='%s' menuId=%d source=Menu", qPrintable(commandId),
                 static_cast<int>(menuId));
        OperationRouting::dispatch(menuId);
        return;
    }

    // 无法精确定位菜单项（工具名、无目录条目的命令）时回退到 OperationId 直接分发
    const OperationId operation = CommandCatalog::operationForCommandId(commandId);
    if (operation == OperationId::None)
    {
        SY_WARNF("[Workbench2D] Unknown command: %s", qPrintable(commandId));
        return;
    }

    SY_INFOF("[Workbench2D] Dispatch command='%s'", qPrintable(commandId));
    m_services.operationBus->run(operation, {}, OperationSource::Menu);
}

QString Workbench2D::commandText(const QString &commandId) const
{
    const auto operation = CommandCatalog::operationForCommandId(commandId);
    const auto *entry = CommandCatalog::findByOperation(operation);
    return entry && entry->text ? QString::fromUtf8(entry->text) : QString();
}

Workbench2D::Workbench2D() = default;
Workbench2D::~Workbench2D() = default;

QString Workbench2D::displayName() const
{
    return QObject::tr("2D Workbench");
}

bool Workbench2D::initialize(const UiServices &services)
{
    if (!services.stateCenter || !services.interactionDispatcher)
        return false;
    m_services = services;

    // 阶段1收口：SelectionService 已由组合根创建并经 UiServices.selectionService 注入，
    // 工作台不再直接触碰底层 SceneManager

    m_initialState = WorkbenchStateSnapshot{};
    m_savedState = WorkbenchStateSnapshot{};
    return true;
}

void Workbench2D::attachToWindow(WorkbenchWindow &window)
{
    auto *viewport = createCentralViewport(window, nullptr);
    if (!viewport)
        return;

    window.setCentralWidget(viewport);

    auto *vp = qobject_cast<RenderViewport2D *>(viewport);
    if (vp)
    {
        m_viewport = vp;
        setupViewportServices(vp, window);
        vp->initializeTools();
        setupImportCallbacks(vp, window);
        vp->setActiveTool(QStringLiteral("SelectTool"));
    }

    createToolbars(window);
    createLayersDock(window);

    // 创建 2D 状态栏 widget 并挂载到窗口
    // StatusBar 封装了坐标/选择/消息/状态信息的显示，与 3D StatusBar3D 完全独立
    if (!m_statusBar2D)
    {
        m_statusBar2D = new StatusBar(&window);
        SY_INFO("[Workbench2D] StatusBar created");
    }
    window.mountStatusBar(m_statusBar2D);
}

QWidget *Workbench2D::createCentralViewport(WorkbenchWindow &window, PropertiesPanelWidget *properties)
{
    Q_UNUSED(window);
    Q_UNUSED(properties);
    auto *viewport = new RenderViewport2D();
    return viewport;
}

void Workbench2D::setupViewportServices(RenderViewport2D *vp, WorkbenchWindow &window)
{
    Q_UNUSED(window);

    vp->setSelectionService(m_services.selectionService);
    vp->setInteractionDispatcher(m_services.interactionDispatcher);
    vp->setOperationBus(m_services.operationBus);

    // P1: 视口通过信号通知上层，不直接持有编辑服务
    if (m_services.sceneEditService)
    {
        QObject::connect(vp, &RenderViewport2D::entitySubmitRequested,
                         [service = m_services.sceneEditService](Eg::SyEntity *e) {
                             if (e)
                                 service->addEntityFromPointer(e, "Draw");
                         });
        QObject::connect(vp, &RenderViewport2D::nudgeRequested,
                         [service = m_services.sceneEditService](double dx, double dy) {
                             service->nudgeSelected(dx, dy, "Move endpoint");
                         });
    }

    vp->setDocument(m_services.document2D);

    // 状态回调：将视口状态写入状态中心
    if (m_services.stateCenter)
    {
        vp->setStatusCallback(
            [stateCenter = m_services.stateCenter](const QString &text) { stateCenter->setStatusPrompt(text); });
        vp->setSelectionCallback(
            [stateCenter = m_services.stateCenter](const QString &selText, const QString &selType) {
                stateCenter->setSelectionContext(selType, selText);
            });
        // 鼠标移动时实时更新状态栏位置标签
        vp->setPositionCallback([stateCenter = m_services.stateCenter, &window](double x, double y) {
            QVariantMap meta = stateCenter->metadata();
            meta["mouseX"] = x;
            meta["mouseY"] = y;
            stateCenter->setMetadata(meta);
            window.updatePositionLabel(x, y);
        });
    }
}

void Workbench2D::setupImportCallbacks(RenderViewport2D *vp, WorkbenchWindow &window)
{
    if (!m_services.importService)
        return;

    // 导入后自动 zoomToFit
    m_services.importService->setViewportFitCallback(
        [vp]() { QTimer::singleShot(0, vp, [vp]() { vp->zoomToFit(); }); });
    // 场景树刷新（导入后重建树结构）
    m_services.importService->setTreeRebuildCallback([&window]() {
        auto *dock = window.sceneTreeDock();
        if (dock)
            dock->refresh();
    });
    // 属性面板刷新（导入后更新属性显示）
    m_services.importService->setPropertyRefreshCallback([&window]() {
        auto *props = window.propertiesDock();
        if (props)
            props->refresh();
    });
}

void Workbench2D::createToolbars(WorkbenchWindow &window)
{
    // 左侧绘图工具栏
    auto *leftToolBar = new QToolBar(QObject::tr("Draw Tools"), &window);
    leftToolBar->setObjectName(QStringLiteral("DrawToolBar"));
    leftToolBar->setMovable(false);
    window.addToolBar(Qt::LeftToolBarArea, leftToolBar);

    auto *drawWidget = new DrawToolBarWidget(leftToolBar);
    drawWidget->setOperationBus(m_services.operationBus);

    // 从 CommandCatalog 构建工具定义列表
    QVector<DrawToolEntry> drawTools;
    for (const auto &entry : CommandCatalog::commands())
    {
        if (hasSurface(entry.surfaces, CommandSurface2D::LeftToolbar))
        {
            DrawToolEntry tool;
            tool.commandId = QString::fromUtf8(entry.toolName ? entry.toolName : "");
            tool.displayName = QString::fromUtf8(entry.text ? entry.text : "");
            tool.tooltip = QString::fromUtf8(entry.text ? entry.text : "");
            tool.shortcut = QString::fromUtf8(entry.shortcutId ? entry.shortcutId : "");
            tool.iconResource = QString::fromUtf8(entry.iconResource ? entry.iconResource : "");
            drawTools.append(tool);
        }
    }
    drawWidget->setToolDefinitions(drawTools);
    leftToolBar->addWidget(drawWidget);

    // 接通「工具栏 → OperationBus → 视口」：把工具栏展示的 Tool_* 操作注册到操作总线，
    // 点击按钮后经总线转发表驱动的 LambdaOperation 激活视口对应工具。
    // 必须在视口（m_viewport）创建完成后再注册，故放在这里而非组合根。
    DrawToolSwitchRegistry(m_services.operationBus, m_viewport).registerAll();

    // 双向状态同步：视口切换工具后高亮对应按钮；启动时初始化为当前活动工具（SelectTool）。
    QObject::connect(m_viewport, &RenderViewport2D::activeToolChanged, drawWidget,
                     &DrawToolBarWidget::updateActiveTool);
    drawWidget->updateActiveTool(m_viewport->activeToolName());

    // CommandActionHub：管理所有 QAction 的创建与绑定
    m_commandHub = std::make_unique<CommandActionHub>();
    m_commandHub->setMainWindow(&window);
    m_commandHub->rebuildAllActions();

    // 顶部工具栏（编辑命令）
    m_topToolBar = new TopToolBar(&window);
    m_topToolBar->setObjectName(QStringLiteral("TopToolBar"));
    m_topToolBar->installHubActions(m_commandHub.get());
    window.addToolBar(Qt::TopToolBarArea, m_topToolBar);

    // 右侧工具栏（颜色/图层）
    m_rightToolBar = new RightToolBar(&window);
    m_rightToolBar->setObjectName(QStringLiteral("RightToolBar"));
    window.addToolBar(Qt::RightToolBarArea, m_rightToolBar);

    if (m_services.layerManager)
    {
        m_rightToolBar->setLayerManager(m_services.layerManager, m_services.layerManagerBridge);
    }
}

SceneTreeDockWidget *Workbench2D::createLayersDock(WorkbenchWindow &window) const
{
    auto *sceneDock = new SceneTreeDockWidget(&window);
    sceneDock->setObjectName(QStringLiteral("SceneTreeDock"));
    window.registerDockWidget(QObject::tr("2D Layers"), sceneDock, Qt::LeftDockWidgetArea);
    return sceneDock;
}

void Workbench2D::activate()
{
    // 从状态快照恢复（首次激活使用 m_initialState，后续使用 m_savedState）
    const auto &snapshot = m_savedState.viewMode.isEmpty() ? m_initialState : m_savedState;
    restoreFromSnapshot(snapshot);

    // 恢复工具状态：将快照中的工具 ID 应用到视口
    if (m_viewport && !snapshot.activeToolId.isEmpty())
    {
        m_viewport->setActiveTool(snapshot.activeToolId);
        SY_INFOF("[Workbench2D] Restored tool: %s", qPrintable(snapshot.activeToolId));
    }

    // 激活时刷新一次动作状态（撤销/重做可用性）
    if (m_commandHub && m_services.undoManager)
    {
        CommandUiSnapshot snap;
        snap.canUndo = m_services.undoManager->canUndo();
        snap.canRedo = m_services.undoManager->canRedo();
        m_commandHub->refreshActionStates(snap);
    }
}

void Workbench2D::deactivate()
{
    m_savedState = currentSnapshot();

    // 清除 ImportService 中持有的视口回调，防止切换后悬空指针
    // ImportService 生命周期长于工作台，不清理会导致 use-after-free
    if (m_services.importService)
    {
        m_services.importService->setViewportFitCallback(nullptr);
        m_services.importService->setTreeRebuildCallback(nullptr);
        m_services.importService->setPropertyRefreshCallback(nullptr);
    }

    // 清理命令动作中枢（unique_ptr 管理生命周期，reset 释放所有权）
    m_commandHub.reset();

    // 工具栏指针会随窗口清理而失效（clearWorkbenchContent 会 delete QToolBar）
    m_topToolBar = nullptr;
    m_rightToolBar = nullptr;
    // 视口指针随窗口清理而失效
    m_viewport = nullptr;
}

void Workbench2D::shutdown()
{
    deactivate();
    m_services = UiServices{};
}

void Workbench2D::releaseCentralWidgetGLResources(QWidget *centralWidget) const
{
    if (auto *vp = qobject_cast<RenderViewport2D *>(centralWidget))
        vp->releaseGLResources();
}

QString Workbench2D::formatSelectionText(const UiStateSnapshot &state) const
{
    return QObject::tr("2D Sel=%1 | SelType=%2 | CmdSrc=%3 | CmdType=%4")
        .arg(state.currentSelectionText)
        .arg(state.currentSelectionType)
        .arg(state.currentCommandOwner)
        .arg(state.currentCommandType);
}

#if BUILD_UI3D
// Workbench3D 实现
// 统一工作台初始化模板
// 流程：initialize → attachToWindow → activate ↔ deactivate → shutdown
// 使用 MainWindow3D + ServiceLocator3D 架构
// 渲染链路：Viewport3D -> IRenderer3D -> RenderWidget3DAdapter -> RenderWidget3D

#include "UI3D/Service/ServiceLocator3D.h"
#include "UI3D/Service/ServicePack3D.h"
#include "UI3D/Operation/OperationBus3D.h"
#include "UI3D/Operation/CommandCatalog3D.h"
#include "UI3D/Operation/CommandActionHub3D.h"
#include "UI3D/Operation/CommandRegistry3D.h"
#include "UI3D/Manager/DocumentManager3D.h"
#include "UI3D/Edit/UndoRedoManager3D.h"
#include "UI3D/Edit/SceneEditService3D.h"
#include "UI3D/Service/SceneMonitor3D.h"
#include "UI3D/Shortcut/ShortcutManager3D.h"
#include "UI3D/Navigation/NavigationConfig3D.h"
#include "UI3D/Service/SceneDocument3D.h"
#include "UI3D/Service/CameraController3D.h"
#include "UI3D/Settings/SettingsUiCoordinator3D.h"
#include "UI3D/Algorithm/AlgorithmTaskRegistration3D.h"
#include "UI3D/Operation/AlgorithmRunner3D.h"
#include "UI/Algorithm/AlgorithmApplicationService.h"
#ifdef ENABLE_GEOMODELCORE
#include "UI3D/Service/BRepModelService3D.h"
#endif
#include "Render3D/RenderWidget3D.h"
#include "RenderWidget3DAdapter.h"
#include "UI/MainWindow/MainWindow3D.h"
#include "UI/LeftToolBar/LeftToolBar3D.h"
#include "UI/RightToolBar/RightToolBar3D.h"
#include "UI/MenuManager/MenuManager3D.h"
#include "UI/MenuManager/FileMenu3D.h"
#include "UI/MenuManager/EditMenu3D.h"
#include "UI/MenuManager/ViewMenu3D.h"
#include "UI/MenuManager/HelpMenu3D.h"
#include "Engine3D/SceneManager3D.h"
#include "Renderer3DFactory.h"
#include "Log/SyLogger.h"

QString Workbench3D::id() const
{
    return QStringLiteral("3D");
}
QString Workbench3D::displayName() const
{
    return QObject::tr("3D Workbench");
}

bool Workbench3D::isCommandRegistered(const QString &commandId) const
{
    return CommandCatalog3D::operationForCommandId(commandId) != OperationId3D::None;
}

void Workbench3D::dispatchCommand(const QString &commandId)
{
    if (!m_services3D.operationBus)
    {
        SY_WARNF("[Workbench3D] Cannot dispatch command without OperationBus3D: %s", qPrintable(commandId));
        return;
    }

    const OperationId3D operation = CommandCatalog3D::operationForCommandId(commandId);
    if (operation == OperationId3D::None)
    {
        SY_WARNF("[Workbench3D] Unknown command: %s", qPrintable(commandId));
        return;
    }

    SY_INFOF("[Workbench3D] Dispatch command='%s'", qPrintable(commandId));
    m_services3D.operationBus->run(operation);
}

QString Workbench3D::commandText(const QString &commandId) const
{
    const auto operation = CommandCatalog3D::operationForCommandId(commandId);
    const auto *entry = CommandCatalog3D::findByOperation(operation);
    return entry && entry->text ? QString::fromUtf8(entry->text) : QString();
}

// 1 — 初始化，存储服务引用
bool Workbench3D::initialize(const UiServices &services)
{
    if (!services.stateCenter || !services.interactionDispatcher)
        return false;
    m_services = services;

    // 使用 ApplicationCompositionRoot 中的共享 SceneManager3D，
    // 确保导入的 3D 图元与 3D 工作台使用同一数据源
    if (services.importService && services.importService->sceneManager3D())
    {
        m_sceneManager3D = services.importService->sceneManager3D();
        SY_INFO("[Workbench3D] Using shared SceneManager3D from ImportService");
    }
    else
    {
        SY_ERROR("[Workbench3D] No shared SceneManager3D available from ImportService");
        return false;
    }

    m_savedState = WorkbenchStateSnapshot{};
    m_initialState = WorkbenchStateSnapshot{};
    return true;
}

// 防御性析构：若 deactivate() 未被调用（如异常路径或上层遗漏 shutdown），
// 确保 RenderWidget3D 信号断开、服务按安全顺序释放，避免堆损坏
Workbench3D::~Workbench3D()
{
    if (m_serviceOwner)
        shutdown();
}

// 2 — 构建 3D 工作台 UI
// 核心架构：Viewport3D -> IRenderer3D -> RenderWidget3DAdapter -> RenderWidget3D
// 通过 Viewport3D 统一视图宿主，Renderer 通过外部注入
//
// 内部拆分三个步骤：
//   create3DServices()             — 创建所有 3D 服务并组装 ServicePack3D
//   setup3DViewportAndSignals()    — 创建 MainWindow3D / Viewport3D / Renderer 并绑定信号
//   setup3DMenuAndShortcuts()      — 创建 CommandActionHub3D / MenuManager3D / 快捷键
void Workbench3D::build3DWorkbenchUi(WorkbenchWindow &window)
{
    window.setSkeletonDocksVisible(false);

    create3DServices();
    setup3DViewportAndSignals(window);
    setup3DMenuAndShortcuts(window);
}

/// 步骤一：创建所有 3D 服务，组装 ServicePack3D，注册到 ServiceLocator3D
void Workbench3D::create3DServices()
{
    SY_INFO("[Workbench3D] Creating 3D services...");
    m_serviceOwner = std::unique_ptr<ServiceOwner, ServiceOwnerDeleter>(new ServiceOwner());
    auto &own = *m_serviceOwner;

    own.operationBus = std::make_unique<OperationBus3D>(nullptr);
    own.documentManager = std::make_unique<DocumentManager3D>(nullptr);
    own.undoRedoManager = std::make_unique<UndoRedoManager3D>(nullptr);
    own.sceneEditService =
        std::make_unique<SceneEditService3D>(m_sceneManager3D, own.undoRedoManager.get(), own.documentManager.get());
    own.sceneMonitor = std::make_unique<SceneMonitor3D>(nullptr);
    own.shortcutManager = std::make_unique<ShortcutManager3D>(nullptr);
    own.navigationConfig = std::make_unique<NavigationConfig3D>();
    own.sceneDocument = std::make_unique<SceneDocument3D>(m_sceneManager3D);
    own.sceneDocumentAdapter = std::make_unique<SceneDocument3DAdapter>();
    auto sceneManagerAlias = std::shared_ptr<Eg::SceneManager3D>(m_sceneManager3D, [](Eg::SceneManager3D *) {});
    own.sceneDocumentAdapter->setEngineScene(sceneManagerAlias);
    own.cameraController = std::make_unique<CameraController3D>();
    own.algorithmService = std::make_unique<AlgorithmApplicationService>(nullptr);
    own.settingsService = std::make_unique<SettingsService>(nullptr);
    own.settingsService->init();
    own.settingsCoordinator =
        std::make_unique<SettingsUiCoordinator3D>(own.settingsService.get(), own.navigationConfig.get());

#ifdef ENABLE_GEOMODELCORE
    own.brepModelService = std::make_unique<BRepModelService3D>();
#endif

    if (own.undoRedoManager)
        own.undoRedoManager->setSceneManager(m_sceneManager3D);
    if (own.sceneMonitor)
        own.sceneMonitor->rewatch(m_sceneManager3D);

    AlgorithmTaskRegistration3D::registerAll(*own.algorithmService);

    m_services3D.sceneManager = m_sceneManager3D;
    m_services3D.operationBus = own.operationBus.get();
    m_services3D.documentManager = own.documentManager.get();
    m_services3D.undoRedoManager = own.undoRedoManager.get();
    m_services3D.sceneEditService = own.sceneEditService.get();
    m_services3D.sceneMonitor = own.sceneMonitor.get();
    m_services3D.shortcutManager = own.shortcutManager.get();
    m_services3D.navigationConfig = own.navigationConfig.get();
    m_services3D.sceneDocument = own.sceneDocument.get();
    m_services3D.cameraController = own.cameraController.get();
    m_services3D.algorithmService = own.algorithmService.get();
    m_services3D.settingsService = own.settingsService.get();
    m_services3D.settingsCoordinator = own.settingsCoordinator.get();

#ifdef ENABLE_GEOMODELCORE
    m_services3D.brepModelService = own.brepModelService.get();
#endif

    ServiceLocator3D::adopt(m_services3D);
    SY_INFO("[Workbench3D] 3D services created and adopted");
}

/// 步骤二：创建 MainWindow3D、Viewport3D 和渲染链，绑定视口信号
void Workbench3D::setup3DViewportAndSignals(WorkbenchWindow &window)
{
    auto &own = *m_serviceOwner;

    SY_INFO("[Workbench3D] Creating MainWindow3D wrapper...");
    m_mainWindow3D = std::make_unique<MainWindow3D>(m_services3D, &window);
    SY_INFO("[Workbench3D] MainWindow3D created");

    connect(m_mainWindow3D.get(), &MainWindow3D::sigSwitchTo2D, &window, [&window]() {
        SY_INFO("[Workbench3D] sigSwitchTo2D triggered, switching to 2D workbench");
        window.triggerWorkbench(QStringLiteral("2D"));
    });

    // 创建视口并设置渲染器
    create3DViewport(window);
    // 绑定信号到状态中心和操作总线
    bind3DRenderSignals(own);
    // 创建删除快捷键
    setup3DDeleteShortcuts(window);
}

/// 创建 Viewport3D 并设置渲染链
void Workbench3D::create3DViewport(WorkbenchWindow &window)
{
    SY_INFO("[Workbench3D] Creating Viewport3D...");
    auto *viewport = new Viewport3D(&window);
    SY_INFOF("[Workbench3D] Viewport3D created: %p", viewport);

    // 先设为中心控件，确保 Viewport3D 有正确的尺寸和窗口状态
    // 必须在 setRenderer 之前执行，否则 RenderWidget3DAdapter 初始化时
    // Viewport3D rect 为 (0,0,0,0)，QOpenGLWidget 以零尺寸创建 native window
    // 后续 resize 会导致 Qt 内部 native window 状态不一致，引发访问冲突崩溃
    window.setCentralWidget(viewport);
    SY_INFO("[Workbench3D] Viewport3D set as central widget");

    SY_INFO("[Workbench3D] Creating renderer via Renderer3DFactory...");
    auto renderer = Renderer3DFactory::createDefault();
    SY_INFO("[Workbench3D] Renderer created via factory");

    viewport->setRenderer(std::move(renderer));
    viewport->setSceneDocument(m_serviceOwner->sceneDocumentAdapter.get());
    SY_INFOF("[Workbench3D] SceneDocument3DAdapter set to Viewport3D: %p", m_serviceOwner->sceneDocumentAdapter.get());

    // 从适配器中取出内部 RenderWidget3D 指针，注册到 ServicePack3D
    // 这样新号绑定、SceneEditService3D、操作注册等都能访问到实际的渲染控件
    if (auto *r3dAdapter = dynamic_cast<RenderWidget3DAdapter *>(viewport->renderer()))
    {
        m_services3D.renderWidget = r3dAdapter->widget();
        SY_INFOF("[Workbench3D] RenderWidget3D registered to ServicePack3D: %p", m_services3D.renderWidget);
        if (m_services3D.sceneEditService && m_services3D.renderWidget)
            m_services3D.sceneEditService->bindRenderWidget(m_services3D.renderWidget);
    }
    else
    {
        SY_WARN("[Workbench3D] Renderer is not RenderWidget3DAdapter, renderWidget will be null");
    }

    viewport->setCameraController(m_serviceOwner->cameraController.get());
    SY_INFO("[Workbench3D] CameraController3D set to Viewport3D");
}

/// 绑定 3D 渲染器信号到状态中心和操作总线
void Workbench3D::bind3DRenderSignals(ServiceOwner &own)
{
    // 通过 m_services3D.renderWidget 获取已在 create3DViewport 中注册的 widget
    if (!m_services3D.renderWidget)
    {
        SY_ERROR("[Workbench3D] RenderWidget3D not available in ServiceLocator3D");
        return;
    }

    bind3DCursorSignal();
    bind3DSelectionSignal();
    bind3DDeleteKeySignal();

    if (auto *viewport = qobject_cast<Viewport3D *>(m_mainWindow3D ? m_mainWindow3D->centralWidget() : nullptr))
    {
        viewport->setInputHandler([this](QEvent *event) {
            if (!event || !m_services3D.operationBus)
                return false;

            if (event->type() == QEvent::KeyPress)
            {
                auto *keyEvent = static_cast<QKeyEvent *>(event);
                if (keyEvent->key() == Qt::Key_Delete || keyEvent->key() == Qt::Key_Backspace)
                {
                    m_services3D.operationBus->run(OperationId3D::Edit_Delete);
                    return true;
                }
            }
            return false;
        });
    }
}

/// 绑定光标世界坐标信号
void Workbench3D::bind3DCursorSignal()
{
    auto *renderWidget = m_services3D.renderWidget;
    connect(renderWidget, &RenderWidget3D::sigCursorWorldPosition,
            [stateCenter = m_services.stateCenter](float x, float y, float z, bool valid) {
                if (!stateCenter)
                    return;
                QVariantMap meta;
                if (valid)
                    meta[QStringLiteral("positionText")] =
                        QObject::tr("Position: (%1, %2, %3) mm").arg(x, 0, 'f', 2).arg(y, 0, 'f', 2).arg(z, 0, 'f', 2);
                else
                    meta[QStringLiteral("positionText")] = QObject::tr("Position: -");
                stateCenter->setMetadata(meta);
            });
}

/// 绑定选中变化信号
void Workbench3D::bind3DSelectionSignal()
{
    auto *renderWidget = m_services3D.renderWidget;
    // 跨 DLL 安全：信号参数改为 POD 指针数组
    connect(renderWidget, &RenderWidget3D::sigSelectionChanged,
            [stateCenter = m_services.stateCenter](const Eg::SyMeshEntity **entities, int count) {
                if (!stateCenter)
                    return;
                QString modelName;
                if (count > 0 && entities && entities[0])
                    modelName = QString::number(entities[0]->id);

                QVariantMap meta;
                meta[QStringLiteral("3d_selCount")] = count;
                meta[QStringLiteral("3d_modelName")] = modelName;

                if (count > 0)
                    stateCenter->setSelectionContext(QObject::tr("3D-Viewport"),
                                                     QObject::tr("%1 entities selected").arg(count));
                else
                    stateCenter->setSelectionContext(QObject::tr("3D-Viewport"), QStringLiteral("none"));
                stateCenter->setMetadata(meta);
            });
}

/// 绑定 Delete/Backspace 键盘按键信号
void Workbench3D::bind3DDeleteKeySignal()
{
    auto *renderWidget = m_services3D.renderWidget;
    connect(renderWidget, &RenderWidget3D::sigKeyPressed, this, [this](int key, Qt::KeyboardModifiers) {
        if (key == Qt::Key_Delete || key == Qt::Key_Backspace)
        {
            if (m_services3D.operationBus)
            {
                SY_INFO("[Workbench3D] Delete key pressed (renderWidget signal), running Edit_Delete operation");
                m_services3D.operationBus->run(OperationId3D::Edit_Delete);
            }
        }
    });
}

/// 创建全局删除快捷键
void Workbench3D::setup3DDeleteShortcuts(WorkbenchWindow &window)
{
    // 全局 Delete 快捷键（渲染 widget 无焦点时也生效）
    m_deleteShortcut = new QShortcut(QKeySequence(Qt::Key_Delete), &window);
    m_deleteShortcut->setContext(Qt::ApplicationShortcut);
    connect(m_deleteShortcut, &QShortcut::activated, this, [this]() {
        if (m_services3D.operationBus)
        {
            SY_INFO("[Workbench3D] Delete shortcut activated, running Edit_Delete operation");
            m_services3D.operationBus->run(OperationId3D::Edit_Delete);
        }
    });
    window.registerShortcut(m_deleteShortcut);

    // 全局 Backspace 快捷键
    m_backspaceShortcut = new QShortcut(QKeySequence(Qt::Key_Backspace), &window);
    m_backspaceShortcut->setContext(Qt::ApplicationShortcut);
    connect(m_backspaceShortcut, &QShortcut::activated, this, [this]() {
        if (m_services3D.operationBus)
        {
            SY_INFO("[Workbench3D] Backspace shortcut activated, running Edit_Delete operation");
            m_services3D.operationBus->run(OperationId3D::Edit_Delete);
        }
    });
    window.registerShortcut(m_backspaceShortcut);
}

/// 步骤三：创建 CommandActionHub3D、注册命令、初始化菜单管理器和快捷键
void Workbench3D::setup3DMenuAndShortcuts(WorkbenchWindow &window)
{
    auto &own = *m_serviceOwner;
    auto *mainWindow3D = m_mainWindow3D.get();
    // MainWindow3D 仅作为服务容器（QAction parent / command hub parent），
    // 不作为可见窗口 — 菜单/工具栏/状态栏直接挂载到 WorkbenchWindow
    mainWindow3D->hide();

    own.commandActionHub = std::make_unique<CommandActionHub3D>(mainWindow3D);
    own.algorithmRunner = std::make_unique<AlgorithmRunner3D>(own.algorithmService.get(), m_sceneManager3D,
                                                              own.sceneEditService.get(), mainWindow3D);
    m_services3D.commandActionHub = own.commandActionHub.get();
    m_services3D.algorithmRunner = own.algorithmRunner.get();
    if (own.settingsCoordinator)
        own.settingsCoordinator->init();

    SY_INFO("[Workbench3D] Calling CommandRegistry3D::registerAll()...");
    CommandRegistry3D::registerAll(mainWindow3D);
    SY_INFO("[Workbench3D] CommandRegistry3D::registerAll() completed");

    // 1. 先创建所有 Action（bindXxx 依赖 action() 返回有效指针）
    own.commandActionHub->setOperationBus(own.operationBus.get());
    own.commandActionHub->rebuildAll(own.shortcutManager.get());
    SY_INFO("[Workbench3D] CommandActionHub3D rebuilt with all actions");

    // 2. 创建菜单骨架到 WorkbenchWindow 菜单栏
#ifdef SANYI_ENABLE_CONFIG_DRIVEN_UI
    // 配置驱动模式下，WorkbenchMenuManager 负责统一创建 2D / 3D 菜单。
    // MenuManager3D 仍保留为 3D 专用 ActionHub/菜单适配器，不再直接清空并创建窗口菜单。
    SY_INFO("[Workbench3D] Config-driven menu mode: using WorkbenchMenuManager");
#else
    m_menuManager3D = std::make_unique<MenuManager3D>(mainWindow3D);
    m_menuManager3D->createMenus(window.menuBar());
#endif

#ifndef SANYI_ENABLE_CONFIG_DRIVEN_UI
    // 3. 绑定菜单 Action（在 rebuildAll 之后，action() 才有有效指针）
    if (auto *fm = qobject_cast<FileMenu3D *>(m_menuManager3D->fileMenu()))
        own.commandActionHub->bindFileMenu(fm);
    if (auto *em = m_menuManager3D->editMenu())
        own.commandActionHub->bindEditMenu(em);
    if (auto *vm = m_menuManager3D->viewMenu())
        own.commandActionHub->bindViewMenu(vm);
#ifdef ENABLE_GEOMODELCORE
    if (auto *sm = m_menuManager3D->solidMenu())
        own.commandActionHub->bindSolidMenu(sm);
#endif

    // 4. 连接非 hub 菜单信号（hub action 已由 wireAction 直接连接 OperationBus）
    m_menuManager3D->connectMenuSignals();
    SY_INFO("[Workbench3D] MenuManager3D initialized on WorkbenchWindow menubar");

    connect(m_menuManager3D.get(), &MenuManager3D::sigMenuAction, this, &Workbench3D::onMenuAction);
#endif

    // 5. 转移并绑定工具栏。
    // 菜单由统一配置路径生成，3D CommandActionHub 继续负责高频工具栏动作。
    // 注意：mainWindow3D->hide() 会隐藏所有子控件，removeToolBar 也会显式隐藏工具栏，
    // 因此 reparent 后必须显式 show() 才能恢复可见性
    // LeftToolBar3D：reparent 到 WorkbenchWindow 后填充视角导航按钮
    if (auto *lt = mainWindow3D->leftToolBar())
    {
        lt->setParent(&window);
        window.addToolBar(Qt::LeftToolBarArea, lt);
        own.commandActionHub->bindLeftToolBar(lt);
        lt->show();
    }
    // TopToolBar3D：从 MainWindow3D 移除后 reparent 到 WorkbenchWindow
    if (auto *tb = mainWindow3D->topToolBar())
    {
        mainWindow3D->removeToolBar(tb);
        tb->setParent(&window);
        window.addToolBar(Qt::TopToolBarArea, tb);
        own.commandActionHub->bindTopToolBar(tb);
        tb->show();
    }
    // RightToolBar3D：从 MainWindow3D 转移到 WorkbenchWindow
    if (auto *rt = mainWindow3D->rightToolBar())
    {
        mainWindow3D->removeToolBar(rt);
        rt->setParent(&window);
        window.addToolBar(Qt::RightToolBarArea, rt);
        own.commandActionHub->bindRightToolBar(rt);
        rt->show();
    }

    // 6. 状态栏：创建独立的 StatusBar3D 并挂载到 WorkbenchWindow
    //    Workbench3D 拥有 StatusBar3D 的完整生命周期，不再从 MainWindow3D reparent
    //    MainWindow3D 保留自己的 StatusBar3D 供内部使用（如导航提示）
    if (!m_statusBar3D)
    {
        m_statusBar3D = new StatusBar3D(&window);
        SY_INFO("[Workbench3D] StatusBar3D created for WorkbenchWindow");
    }
    window.mountStatusBar(m_statusBar3D);

    // 将光标世界坐标信号直接连接到 StatusBar3D 的位置标签
    if (m_services3D.renderWidget)
    {
        connect(
            m_services3D.renderWidget, &RenderWidget3D::sigCursorWorldPosition, m_statusBar3D,
            [sb3d = m_statusBar3D](float x, float y, float z, bool valid) {
                if (valid)
                    sb3d->setPositionText(
                        QObject::tr("Position: (%1, %2, %3) mm").arg(x, 0, 'f', 2).arg(y, 0, 'f', 2).arg(z, 0, 'f', 2));
                else
                    sb3d->setPositionText(QObject::tr("Position: -"));
            });

        // 将选中变化信号直接连接到 StatusBar3D 的选择/模型标签
        connect(m_services3D.renderWidget, &RenderWidget3D::sigSelectionChanged, m_statusBar3D,
                [sb3d = m_statusBar3D](const Eg::SyMeshEntity **entities, int count) {
                    QString modelName;
                    if (count > 0 && entities && entities[0])
                        modelName = QString::number(entities[0]->id);
                    sb3d->setSelectionInfo(count, modelName, 0);
                });
    }

    SY_INFO("[Workbench3D] 3D workbench UI build completed");
}

void Workbench3D::onMenuAction(int actionId, const QVariantMap &params)
{
    Q_UNUSED(params);

    const auto menuId = UI3D::fromCommonMenuId(actionId);
    if (menuId == UI3D::MenuActionId3D::Help_Settings)
    {
        if (m_mainWindow3D)
        {
            m_mainWindow3D->showSettingsDialog();
            return;
        }

        SY_WARN("[Workbench3D] Help_Settings received but MainWindow3D is null");
        return;
    }

    if (!m_services3D.operationBus)
        return;

    const auto operationId = CommandCatalog3D::operationForMenu(menuId);
    if (operationId != OperationId3D::None)
    {
        m_services3D.operationBus->run(operationId);
    }
    else
    {
        SY_WARNF("[Workbench3D] No operation found for menu action id: %d", actionId);
    }
}

// 3 — 附加到窗口，触发 UI 构建
void Workbench3D::attachToWindow(WorkbenchWindow &window)
{
    build3DWorkbenchUi(window);
}

// 4 — 激活工作台，应用初始状态
void Workbench3D::activate()
{
    // 从状态快照恢复（与 2D 保持一致）
    const auto &snapshot = m_savedState.viewMode.isEmpty() ? m_initialState : m_savedState;
    restoreFromSnapshot(snapshot);

    if (m_services.stateCenter)
    {
        m_services.stateCenter->setCurrentWorkbenchId(id());
    }

    if (m_mainWindow3D)
    {
        QTimer::singleShot(0, m_mainWindow3D.get(), [this]() {
            if (m_mainWindow3D)
            {
                m_mainWindow3D->refreshCommandUiState();
                m_mainWindow3D->syncViewDisplayUi();
            }
        });
    }

    // 刷新命令动作状态（撤销/重做可用性）
    if (m_services3D.commandActionHub && m_services3D.undoRedoManager)
    {
        // 3D 命令中枢在 attachToWindow 中已创建，此处刷新其 UI 状态
        SY_INFO("[Workbench3D] Refreshing command action states on activate");
    }
}

// 5 — 停用工作台，保存状态并清理资源
void Workbench3D::deactivate()
{
    m_savedState = currentSnapshot();

    // 先释放与窗口/快捷键绑定的 Qt 对象，再释放共享服务。
    // 这样可以避免 QMainWindow / 菜单 / 快捷键 在析构链中继续访问已销毁的 3D 服务。
    SY_INFO("[Workbench3D] Deactivating, tearing down UI objects first...");

    // 1) 先断开快捷键信号并置空，避免 clearWorkbenchContent 之前快捷键仍触发 3D 命令
    if (m_deleteShortcut)
    {
        disconnect(m_deleteShortcut, nullptr, this, nullptr);
        m_deleteShortcut = nullptr;
    }
    if (m_backspaceShortcut)
    {
        disconnect(m_backspaceShortcut, nullptr, this, nullptr);
        m_backspaceShortcut = nullptr;
    }

    // 1.5) 先断开 RenderWidget3D 的所有信号连接，避免窗口销毁过程中信号回调命中悬空引用
    if (m_services3D.renderWidget)
    {
        disconnect(m_services3D.renderWidget, nullptr, nullptr, nullptr);
        m_services3D.sceneEditService->bindRenderWidget(nullptr);
        m_services3D.renderWidget = nullptr;
    }

    // 2) 先销毁 3D 菜单与主窗口包装对象。
    //    这两个对象会持有大量 QAction / signal-slot / UI 状态引用，必须先于服务释放。
    SY_INFO("[Workbench3D] Destroying MenuManager3D...");
    m_menuManager3D.reset();
    SY_INFO("[Workbench3D] MenuManager3D destroyed");

    SY_INFO("[Workbench3D] Destroying MainWindow3D...");
    m_mainWindow3D.reset();
    SY_INFO("[Workbench3D] MainWindow3D destroyed");

    // 3) 再释放 3D 服务 locator 与共享服务对象。
    //    注意：窗口销毁已经完成，这里再 shutdown 服务，可减少 Qt 析构阶段访问悬空对象的风险。
    SY_INFO("[Workbench3D] Releasing 3D services...");
    ServiceLocator3D::shutdown();
    m_services3D = ServicePack3D{};
    m_serviceOwner.reset();
    SY_INFO("[Workbench3D] Service released");
}

// 6 — 关闭工作台，清理所有资源
void Workbench3D::shutdown()
{
    deactivate();
    m_services = UiServices{};
}

void Workbench3D::releaseCentralWidgetGLResources(QWidget *centralWidget) const
{
    if (auto *vp = qobject_cast<Viewport3D *>(centralWidget))
        vp->releaseGLResources();
}

QString Workbench3D::formatSelectionText(const UiStateSnapshot &state) const
{
    return QObject::tr("3D Sel=%1 | NodeType=%2 | CmdSrc=%3 | CmdType=%4")
        .arg(state.currentSelectionText)
        .arg(state.currentSelectionType)
        .arg(state.currentCommandOwner)
        .arg(state.currentCommandType);
}

bool Workbench3D::requiresSkeletonDocks() const
{
    return false;
}

bool Workbench3D::managesOwnMenus() const
{
    // 配置驱动模式下由 WorkbenchMenuManager 统一构建 2D/3D 菜单（见 setup3DMenuAndShortcuts），
    // 3D 不再自行管理窗口菜单栏；仅旧路径（MenuManager3D）由本工作台自理。
#ifdef SANYI_ENABLE_CONFIG_DRIVEN_UI
    return false;
#else
    return true;
#endif
}
#endif