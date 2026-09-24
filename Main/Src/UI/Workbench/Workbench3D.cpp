#include "Workbench3D.h"
#include "WorkbenchTiming.h"

#if BUILD_UI3D
    #include <QAction>
    #include <QApplication>
    #include <QDesktopServices>
    #include <QFileInfo>
    #include <QLineEdit>
    #include <QMenu>
    #include <QPlainTextEdit>
    #include <QShortcut>
    #include <QTextEdit>
    #include <QTimer>
    #include <QToolBar>
    #include <QUrl>
    #include <QWidget>

    #include <functional>
    #include <string>
    #include <unordered_set>
    #include <vector>

    #include "Composition/ApplicationCompositionRoot.h"
    #include "Import/ImportService.h"
    #include "Log/SyLogger.h"
    #include "UiServices.h"
    #include "UiStateCenter.h"
    #include "UiViewport3D.h"
    #include "SceneBuilder3D.h"
#include "WorkbenchMenuManager.h"
#include "WorkbenchWindow.h"
#include "UiDockIds.h"

    #include "ClientConfig/UiClientConfigBase.h"
    #include "ClientConfig/UiConfigurationManager.h"
    #include "ClientConfig/UiContextMenuService.h"
    #include "ClientConfig/UiLayoutBuilder.h"

    #include "UI/Services/HelpDialogService.h"
    #include "UI/Service/ViewCaptureService.h"
    #include "UI/Settings/SettingsService.h"
    #include "UI/TopToolBar/TopToolBar3D.h"
    #include "UI/StatusBar/StatusBar3D.h"
    #include "UI/MainWindow/MainWindow3D.h"
    #include "UI/Algorithm/AlgorithmApplicationService.h"

    #include "Engine3D/SceneManager3D.h"
    #include "UI3D/Service/ServicePack3D.h"
    #include "UI3D/Operation/OperationBus3D.h"
    #include "UI3D/Operation/CommandCatalog3D.h"
    #include "UI3D/Manager/DocumentManager3D.h"
    #include "UI3D/Edit/UndoRedoManager3D.h"
    #include "UI3D/Edit/SceneEditService3D.h"

    #include "UI3D/Service/SceneMonitor3D.h"
    #include "UI3D/Service/SceneDocument3D.h"
    #include "UI3D/Service/CameraController3D.h"
    #include "UI3D/Settings/SettingsUiCoordinator3D.h"
    #include "UI3D/Operation/CommandActionHub3D.h"
    #include "UI3D/Operation/AlgorithmRunner3D.h"

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
    std::unique_ptr<SceneDocument3D> sceneDocument;

    std::unique_ptr<CameraController3D> cameraController;
    std::unique_ptr<AlgorithmApplicationService> algorithmService;
    std::unique_ptr<SettingsUiCoordinator3D> settingsCoordinator;
    // 共享 singleton，非 ServiceOwner 所有，使用裸指针避免误删
    SettingsService* settingsService{ nullptr };
    std::unique_ptr<CommandActionHub3D> commandActionHub;
    std::unique_ptr<AlgorithmRunner3D> algorithmRunner;

    #ifdef ENABLE_GEOMODELCORE
    std::unique_ptr<BRepModelService3D> brepModelService;
    #endif
};

// 自定义删除器定义（ServiceOwner 在此处已完整定义）
void Workbench3D::ServiceOwnerDeleter::operator()(ServiceOwner* p) const
{
    delete p;
}

// Workbench3D 实现
// 统一工作台初始化模板
// 流程：initialize → attachToWindow → activate ↔ deactivate → shutdown
// 使用 MainWindow3D + ServiceLocator3D 架构
// 渲染链路：Viewport3D -> IRenderer3D -> RenderWidget3DAdapter -> RenderWidget3D

    #include "UI3D/Service/ServiceLocator3D.h"
    #include "UI3D/Operation/CommandRegistry3D.h"
    #include "UI3D/Algorithm/AlgorithmTaskRegistration3D.h"

    #include "Render3D/RenderWidget3D.h"
    #include "RenderWidget3DAdapter.h"
    #include "UI/LeftToolBar/LeftToolBar3D.h"
    #include "UI/RightToolBar/RightToolBar3D.h"

    #include "Engine3D/Selection/SelectionManager3D.h"
    #include "ViewportRendererFactory.h"

    #include "SceneTreeBuilder3D.h"
    #include "UiSceneTreePanel.h"
    #include "SceneTreeModel3D.h"
    #include "Engine/EntityIdUtils.h"

    #include "UiPropertiesPanel.h"
    #include "UI3D/Service/EntityPropertyEditSession3D.h"
    #include "UI3D/Service/EntityPropertyModel3D.h"

QString Workbench3D::id() const
{
    return QStringLiteral("3D");
}

QString Workbench3D::displayName() const
{
    return QObject::tr("3D Workbench");
}

bool Workbench3D::isCommandRegistered(const QString& commandId) const
{
    return CommandCatalog3D::operationForCommandId(commandId) != OperationId3D::None;
}

void Workbench3D::dispatchCommand(const QString& commandId, const QVariantMap& params)
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

    SY_DEBUGF("[Workbench3D] Dispatch command='%s'", qPrintable(commandId));
    // 走完整 Request：便捷入口 run(id, source) 不带 params，会把调用方参数丢掉
    OperationRequest3D req;
    req.id = operation;
    req.source = OperationSource3D::Menu;
    req.params = params;
    m_services3D.operationBus->run(req);
}

QString Workbench3D::commandText(const QString& commandId) const
{
    const auto operation = CommandCatalog3D::operationForCommandId(commandId);
    const auto* entry = CommandCatalog3D::findByOperation(operation);
    return entry && entry->text ? QString::fromUtf8(entry->text) : QString();
}

// 1 — 初始化，存储服务引用
bool Workbench3D::initialize(const WorkbenchServices& services)
{
    SY_INFO("[Workbench3D] initialize: starting 3D workbench initialization");

    if (!services.uiState.stateCenter || !services.uiState.interactionDispatcher)
    {
        SY_ERRORF("[Workbench3D] initialize failed: stateCenter=%p interactionDispatcher=%p",
            static_cast<void*>(services.uiState.stateCenter), static_cast<void*>(services.uiState.interactionDispatcher));
        return false;
    }
    m_uiState = services.uiState;
    m_commands = services.commands;
    m_scene = services.scene;
    m_persistence = services.persistence;
    m_view = services.view;

    // 使用 ApplicationCompositionRoot 中的共享 SceneManager3D，
    // 确保导入的 3D 图元与 3D 工作台使用同一数据源
    if (services.persistence.importService && services.persistence.importService->sceneManager3D())
    {
        m_sceneManager3D = services.persistence.importService->sceneManager3D();
        SY_DEBUG("[Workbench3D] Using shared SceneManager3D from ImportService");
    }
    else
    {
        SY_ERROR("[Workbench3D] No shared SceneManager3D available from ImportService");
        return false;
    }

    m_savedState = UiStateSnapshot{};
    m_initialState = UiStateSnapshot{};
    SY_DEBUG("[Workbench3D] initialize: 3D workbench initialized successfully");
    return true;
}

// 防御性析构：若 deactivate() 未被调用（如异常路径或上层遗漏 shutdown），
// 确保 RenderWidget3D 信号断开、服务按安全顺序释放，避免堆损坏
Workbench3D::~Workbench3D()
{
    if (m_serviceOwner)
    {
        shutdown();
    }
}

// 2 — 构建 3D 工作台 UI
// 核心架构：Viewport3D -> IRenderer3D -> RenderWidget3DAdapter -> RenderWidget3D
// 通过 Viewport3D 统一视图宿主，Renderer 通过外部注入
//
// 内部拆分三个步骤：
//   create3DServices()             — 创建所有 3D 服务并组装 ServicePack3D
//   setup3DViewportAndSignals()    — 创建 MainWindow3D / Viewport3D / Renderer 并绑定信号
//   setup3DMenuAndShortcuts()      — 创建 CommandActionHub3D / 工具栏 / 状态栏 / 快捷键
void Workbench3D::build3DWorkbenchUi(WorkbenchWindow& window)
{
    window.setSkeletonDocksVisible(false);

    create3DServices();
    setup3DViewportAndSignals(window);
    setupSceneTree3D(window);
    setupProperties3D(window);
    setup3DMenuAndShortcuts(window);
}

/// 步骤一：创建所有 3D 服务，组装 ServicePack3D，注册到 ServiceLocator3D
void Workbench3D::create3DServices()
{
    SY_DEBUG("[Workbench3D] Creating 3D services...");
    m_serviceOwner = std::unique_ptr<ServiceOwner, ServiceOwnerDeleter>(new ServiceOwner());
    auto& own = *m_serviceOwner;

    own.operationBus = std::make_unique<OperationBus3D>(nullptr);
    own.documentManager = std::make_unique<DocumentManager3D>(nullptr);
    own.undoRedoManager = std::make_unique<UndoRedoManager3D>(nullptr);
    own.sceneEditService =
        std::make_unique<SceneEditService3D>(m_sceneManager3D, own.undoRedoManager.get(), own.documentManager.get());
    own.sceneMonitor = std::make_unique<SceneMonitor3D>(nullptr);
    // 3D 唯一文档入口：SceneDocument3D 自持引擎场景、UI 树与选择集
    own.sceneDocument = std::make_unique<SceneDocument3D>(m_sceneManager3D);
    own.cameraController = std::make_unique<CameraController3D>();
    own.algorithmService = std::make_unique<AlgorithmApplicationService>(nullptr);
    own.settingsService = ApplicationCompositionRoot::getSettingsService();
    own.settingsService->init();
    own.settingsCoordinator = std::make_unique<SettingsUiCoordinator3D>(own.settingsService);

    #ifdef ENABLE_GEOMODELCORE
    own.brepModelService = std::make_unique<BRepModelService3D>();
    #endif

    if (own.undoRedoManager)
    {
        own.undoRedoManager->setSceneManager(m_sceneManager3D);
    }
    if (own.sceneMonitor)
    {
        own.sceneMonitor->rewatch(m_sceneManager3D);
    }

    AlgorithmTaskRegistration3D::registerAll(*own.algorithmService);

    m_services3D.sceneManager = m_sceneManager3D;
    m_services3D.operationBus = own.operationBus.get();
    m_services3D.documentManager = own.documentManager.get();
    m_services3D.undoRedoManager = own.undoRedoManager.get();
    m_services3D.sceneEditService = own.sceneEditService.get();
    m_services3D.sceneMonitor = own.sceneMonitor.get();
    m_services3D.sceneDocument = own.sceneDocument.get();
    m_services3D.cameraController = own.cameraController.get();
    m_services3D.algorithmService = own.algorithmService.get();
    m_services3D.settingsService = own.settingsService;
    m_services3D.settingsCoordinator = own.settingsCoordinator.get();

    #ifdef ENABLE_GEOMODELCORE
    m_services3D.brepModelService = own.brepModelService.get();
    #endif

    ServiceLocator3D::adopt(m_services3D);
    SY_DEBUG("[Workbench3D] 3D services created and adopted");
}

/// 步骤二：创建 MainWindow3D、Viewport3D 和渲染链，绑定视口信号
void Workbench3D::setup3DViewportAndSignals(WorkbenchWindow& window)
{
    auto& own = *m_serviceOwner;

    SY_DEBUG("[Workbench3D] Creating MainWindow3D wrapper...");
    m_mainWindow3D = std::make_unique<MainWindow3D>(m_services3D, &window);
    SY_DEBUG("[Workbench3D] MainWindow3D created");

    connect(m_mainWindow3D.get(), &MainWindow3D::sigSwitchTo2D, &window, [&window]() {
        SY_DEBUG("[Workbench3D] sigSwitchTo2D triggered, switching to 2D workbench");
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
void Workbench3D::create3DViewport(WorkbenchWindow& window)
{
    SY_DEBUG("[Workbench3D] Creating Viewport3D...");
    auto* viewport = new Viewport3D(&window);
    SY_DEBUGF("[Workbench3D] Viewport3D created: %p", viewport);

    // 先设为中心控件，确保 Viewport3D 有正确的尺寸和窗口状态
    // 必须在 setRenderer 之前执行，否则 RenderWidget3DAdapter 初始化时
    // Viewport3D rect 为 (0,0,0,0)，QOpenGLWidget 以零尺寸创建 native window
    // 后续 resize 会导致 Qt 内部 native window 状态不一致，引发访问冲突崩溃
    window.setCentralWidget(viewport);
    SY_DEBUG("[Workbench3D] Viewport3D set as central widget");

    SY_DEBUG("[Workbench3D] Creating renderer via ViewportRendererFactory...");
    auto renderer = ViewportRendererFactory::createDefault();
    SY_DEBUG("[Workbench3D] Renderer created via factory");

    viewport->setRenderer(std::move(renderer));
    viewport->setSceneDocument(m_serviceOwner->sceneDocument.get());
    SY_DEBUGF("[Workbench3D] SceneDocument3D set to Viewport3D: %p", m_serviceOwner->sceneDocument.get());

    // 从适配器中取出内部 RenderWidget3D 指针，注册到 ServicePack3D
    // 这样新号绑定、SceneEditService3D、操作注册等都能访问到实际的渲染控件
    if (auto* r3dAdapter = dynamic_cast<RenderWidget3DAdapter*>(viewport->renderer()))
    {
        m_services3D.renderWidget = r3dAdapter->widget();
        SY_DEBUGF("[Workbench3D] RenderWidget3D registered to ServicePack3D: %p", m_services3D.renderWidget);
        if (m_services3D.sceneEditService && m_services3D.renderWidget)
        {
            m_services3D.sceneEditService->bindRenderWidget(m_services3D.renderWidget);
        }
    }
    else
    {
        SY_WARN("[Workbench3D] Renderer is not RenderWidget3DAdapter, renderWidget will be null");
    }

    viewport->setCameraController(m_serviceOwner->cameraController.get());
    SY_DEBUG("[Workbench3D] CameraController3D set to Viewport3D");
}

/// 绑定 3D 渲染器信号到状态中心和操作总线
void Workbench3D::bind3DRenderSignals(ServiceOwner& own)
{
    // 通过 m_services3D.renderWidget 获取已在 create3DViewport 中注册的 widget
    if (!m_services3D.renderWidget)
    {
        SY_ERROR("[Workbench3D] RenderWidget3D not available in ServiceLocator3D");
        return;
    }

    bind3DSelectionSignal();

    // 右键菜单请求：交给命令中枢基于统一快照构建并弹出（与 2D 视口一致）
    connect(m_services3D.renderWidget,
        &RenderWidget3D::sigContextMenuRequested,
        this,
        &Workbench3D::on3DContextMenuRequested);
}

/// 绑定选中变化信号
/// 不再往 metadata 写 positionText / 3d_selCount / 3d_modelName 等无消费者的镜像。
void Workbench3D::bind3DSelectionSignal()
{
    auto* renderWidget = m_services3D.renderWidget;
    // 跨 DLL 安全：信号参数改为 POD 指针数组
    auto conn = connect(renderWidget,
        &RenderWidget3D::sigSelectionChanged,
        [this, stateCenter = m_uiState.stateCenter](const Eg::SyMeshEntity** entities, int count) {
            if (stateCenter)
            {
                if (count > 0)
                {
                    stateCenter->setSelectionContext(
                        QObject::tr("3D-Viewport"), QObject::tr("%1 entities selected").arg(count));
                }
                else
                {
                    stateCenter->setSelectionContext(QObject::tr("3D-Viewport"), QStringLiteral("none"));
                }
            }
            schedulePropertiesPanelRefresh3D();
        });
    m_workbenchConnections.push_back(conn);
}

/// 创建全局删除快捷键
void Workbench3D::setup3DDeleteShortcuts(WorkbenchWindow& window)
{
    // Delete/Backspace 在 3D 侧只保留这一条通路（窗口级 QShortcut）。
    // 此前还并存 RenderWidget3D::sigKeyPressed 与 Viewport3D::setInputHandler 两条，
    // 三者都跑 Edit_Delete：ApplicationShortcut 会在按键送达 widget 前就消费掉事件，
    // 所以那两条平时是死代码，一旦快捷键被禁用/判 ambiguous 就变成一次按键删两次。
    //
    // 与 2D 同一套护栏：焦点在文本控件里时不拦截，否则在参数面板里按退格会删图元。
    const auto editingText = []() -> bool {
        QWidget* fw = QApplication::focusWidget();
        return fw && (qobject_cast<QLineEdit*>(fw) || qobject_cast<QTextEdit*>(fw) || qobject_cast<QPlainTextEdit*>(fw));
    };
    const auto deleteSelected3D = [this, editingText]() {
        if (editingText())
        {
            return;
        }
        if (m_services3D.operationBus)
        {
            SY_DEBUG("[Workbench3D] Delete shortcut activated, running Edit_Delete operation");
            m_services3D.operationBus->run(OperationId3D::Edit_Delete);
        }
    };

    // 全局 Delete 快捷键（渲染 widget 无焦点时也生效）
    m_deleteShortcut = new QShortcut(QKeySequence(Qt::Key_Delete), &window);
    m_deleteShortcut->setContext(Qt::ApplicationShortcut);
    connect(m_deleteShortcut, &QShortcut::activated, this, deleteSelected3D);
    window.registerShortcut(m_deleteShortcut);

    // 全局 Backspace 快捷键
    m_backspaceShortcut = new QShortcut(QKeySequence(Qt::Key_Backspace), &window);
    m_backspaceShortcut->setContext(Qt::ApplicationShortcut);
    connect(m_backspaceShortcut, &QShortcut::activated, this, deleteSelected3D);
    window.registerShortcut(m_backspaceShortcut);
}

void Workbench3D::on3DContextMenuRequested(const QPoint& globalPos)
{
    auto* renderWidget = m_services3D.renderWidget;
    if (!renderWidget || !m_serviceOwner || !m_serviceOwner->commandActionHub)
    {
        return;
    }
    QMenu menu;
    // 基于命令中枢实时快照构建菜单（count / 锁定 来自与 3D 工具栏相同的单一事实来源），
    // 避免右键菜单再走一套独立的选择数据源导致显隐/灰显规则漂移（与 2D 一致）。
    CommandUiSnapshot3D snapshot;
    const auto& selected = renderWidget->selectionManager().getSelectedEntities();
    snapshot.hasSelection = !selected.empty();
    snapshot.selectionCount = static_cast<int>(selected.size());
    // 统一锁定判定：任一选中图元被锁定则视为有锁（与 2D 语义一致）
    for (const Eg::SyMeshEntity* e : selected)
    {
        if (e && e->locked())
        {
            snapshot.anyLockedEntity = true;
            break;
        }
    }
    // 配置驱动优先：客户 JSON 声明了 contextMenus["canvas.3d"] 时由配置接管
    if (QMenu* configured = buildConfiguredContextMenu(QStringLiteral("canvas.3d")))
    {
        // 与 2D 同构：配置化菜单的 QAction 是每次弹出新建的临时对象，中枢的
        // refreshActionStates / WorkbenchMenuManager::refreshCommandStates3D 都触达不到，
        // 弹出前必须按同一份快照套一次启用态，否则空选时右键的 Delete 仍是亮态。
        CommandActionHub3D::applySnapshotToMenu(configured, snapshot);

        // 生命周期同 2D：就地 exec、就地 delete。
        configured->exec(globalPos);
        delete configured;
        return;
    }

    m_serviceOwner->commandActionHub->populateContextMenu(&menu, snapshot);
    if (menu.isEmpty())
    {
        return;
    }
    menu.exec(globalPos);
}

QMenu* Workbench3D::buildConfiguredContextMenu(const QString& contextMenuId)
{
    const UiConfigData* config = UiConfigurationManager::shared().configData();
    if (!UiContextMenuService::hasConfigFor(config, contextMenuId))
    {
        return nullptr;
    }

    // 分发器统一取 WorkbenchMenuManager::commandDispatcher()（MenuDispatcher）：
    // 与 3D 顶部菜单/工具栏共用同一条分发链，窗口级命令在右键里同样可用；
    // 寿命随菜单管理器，闭包捕获的裸指针长期有效。
    // 不要在这里建局部适配器：UiLayoutBuilder 把 dispatcher 裸指针捕进 QAction 的
    // triggered 闭包，而菜单是在调用方 exec() 的 —— 局部对象出栈即悬垂。
    auto* dispatcher = m_workbenchWindow ? m_workbenchWindow->menuManager()->commandDispatcher()
                                         : static_cast<IUiCommandDispatcher*>(this);
    return UiContextMenuService::instance().buildMenu(config, contextMenuId, dispatcher, m_services3D.renderWidget);
}

/// 步骤三：创建 CommandActionHub3D、注册命令、初始化菜单管理器和快捷键
void Workbench3D::setup3DMenuAndShortcuts(WorkbenchWindow& window)
{
    auto& own = *m_serviceOwner;
    auto* mainWindow3D = m_mainWindow3D.get();
    // MainWindow3D 仅作为服务容器（QAction parent / command hub parent），
    // 不作为可见窗口 — 菜单/工具栏/状态栏直接挂载到 WorkbenchWindow
    mainWindow3D->hide();

    own.commandActionHub = std::make_unique<CommandActionHub3D>(mainWindow3D);
    own.algorithmRunner = std::make_unique<AlgorithmRunner3D>(
        own.algorithmService.get(), m_sceneManager3D, own.sceneEditService.get(), mainWindow3D);
    m_services3D.commandActionHub = own.commandActionHub.get();
    m_services3D.algorithmRunner = own.algorithmRunner.get();
    if (own.settingsCoordinator)
    {
        own.settingsCoordinator->init();
        // 启动时从数据库加载并应用已保存的 3D 专属设置（渲染/相机/光照）
        if (m_mainWindow3D)
        {
            own.settingsCoordinator->loadAndApplySettings(m_mainWindow3D->renderWidget());
        }
    }

    SY_DEBUG("[Workbench3D] Calling CommandRegistry3D::registerAll()...");
    // Help_Settings / Help_Shortcut 由工作台层注册（需要框架层快捷键台账）
    // 快捷键台账（配置驱动菜单建出的 QAction/QShortcut + 用户覆盖），而 UI3D 库看不到
    // 那一层 —— Settings 需要台账模型注入，Shortcuts 需要台账渲染内容。因此统一由工作台层
    // 注册，Settings 再解析活动工作台转发给 UiWorkbench::showSettingsDialog。
    // 注册必须发生在 registerAll 之前：OperationRegistryBase 用 try_emplace，先到先得，
    // 后注册的同名 Operation 会被忽略（HelpOperations3D 里已不再注册这两项）。
    #if 0
    own.operationBus->registerOperation(std::make_unique<LambdaOperation3D>(
        OperationId3D::Help_Settings, [windowPtr = &window](OperationContext3D&, const OperationRequest3D&) {
            OperationResult3D result;
            UiWorkbench* activeWb = windowPtr->currentWorkbench();
            result.success = activeWb && activeWb->showSettingsDialog(windowPtr);
            if (!result.success)
            {
                SY_WARNF("[Workbench3D] Help_Settings: no active workbench");
            }
            return result;
        }));
    own.operationBus->registerOperation(std::make_unique<LambdaOperation3D>(
        OperationId3D::Help_Shortcut, [windowPtr = &window](OperationContext3D&, const OperationRequest3D&) {
            OperationResult3D result;
            result.success = true;
            IShortcutSettingsModel* shortcutModel =
                windowPtr->menuManager() ? windowPtr->menuManager()->shortcutSettingsModel() : nullptr;
            if (!shortcutModel)
            {
                SY_WARNF("[Workbench3D] Help_Shortcut: shortcut ledger unavailable");
            }
            HelpDialogService::showShortcutsDialog(windowPtr, shortcutModel);
            return result;
        }));
    #endif

    // 「导出视图」：3D 侧的 View_Capture。
    //
    // 与 2D 的 View_Capture 共用同一个 ViewCaptureService 实例（落盘目录与命名
    // 规则因此完全一致），差别只在取 3D 视口并走 capture3D 的离屏读回——
    // capture3D / RenderWidget3D::captureOffscreen 早已实现，此前**全仓没有任何
    // 调用者**，等于 3D 视口截不了图（F12 在 3D 工作台下也不做任何事）。
    //
    // 注册放在工作台层的原因与上面两条一致：ViewCaptureService 由组合根持有，
    // UI3D 库看不到它，因此不能像 View_Wireframe 那样在 ViewOperations3D 里注册。
    own.operationBus->registerOperation(std::make_unique<LambdaOperation3D>(OperationId3D::View_Capture,
        [captureService = m_view.captureService](OperationContext3D& ctx, const OperationRequest3D&) {
            OperationResult3D result;
            if (!captureService || !ctx.renderWidget)
            {
                SY_WARNF("[Workbench3D] View_Capture: captureService=%s renderWidget=%s",
                    captureService ? "ok" : "null",
                    ctx.renderWidget ? "ok" : "null");
                return result;
            }

            Ui::CaptureRequest req;
            req.scope = Ui::CaptureScope::CurrentView;
            req.framing = Ui::FramingKind::UseCurrent;
            req.autoSave = true;

            const QImage img = captureService->capture3D(ctx.renderWidget, req);
            if (img.isNull())
            {
                SY_WARNF("[Workbench3D] View_Capture: capture3D 返回空图（离屏渲染失败）");
                return result;
            }
            const QString path = captureService->saveImage(img, req, true);
            if (path.isEmpty())
            {
                SY_WARNF("[Workbench3D] View_Capture: 截图保存失败");
                return result;
            }
            SY_INFOF("[View_Capture] Saved to %s (3D %dx%d)", path.toStdString().c_str(), img.width(), img.height());
            // 保存成功后打开图片所在目录
            QString dir = QFileInfo(path).absolutePath();
            QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
            result.success = true;
            return result;
        }));

    CommandRegistry3D::registerAll(mainWindow3D);
    SY_DEBUG("[Workbench3D] CommandRegistry3D::registerAll() completed");

    // F12 截图（与 2D 同一条快捷键；此前只有 2D 工作台注册过它）。
    // 焦点在文本控件里时不拦截，理由与 setup3DDeleteShortcuts 的护栏相同。
    auto* captureSc = new QShortcut(QKeySequence(Qt::Key_F12), &window);
    connect(captureSc, &QShortcut::activated, this, [&own]() {
        QWidget* focusWidget = QApplication::focusWidget();
        if (focusWidget &&
            (qobject_cast<QLineEdit*>(focusWidget) || qobject_cast<QTextEdit*>(focusWidget) ||
                qobject_cast<QPlainTextEdit*>(focusWidget)))
        {
            return;
        }
        if (own.operationBus)
        {
            own.operationBus->run(OperationId3D::View_Capture);
        }
    });
    window.registerShortcut(captureSc);

    // 1. 先创建所有 Action（bindXxx 依赖 action() 返回有效指针）
    own.commandActionHub->setOperationBus(own.operationBus.get());
    own.commandActionHub->rebuildAll();
    SY_DEBUG("[Workbench3D] CommandActionHub3D rebuilt with all actions");

    // 2. 菜单：统一由 WorkbenchMenuManager 从客户 JSON 生成
    //
    // 2D / 3D 走同一条 JSON 链路：菜单项在客户配置里按 workbenches: ["3D"] 声明，
    // 命令 ID 经 Workbench3D::dispatchCommand 落到 OperationBus3D。
    SY_DEBUG("[Workbench3D] Menus are built by WorkbenchMenuManager from client config");

    // 5. 转移并绑定工具栏。
    // 菜单由统一配置路径生成，3D CommandActionHub 继续负责高频工具栏动作。
    // 注意：mainWindow3D->hide() 会隐藏所有子控件，removeToolBar 也会显式隐藏工具栏，
    // 因此 reparent 后必须显式 show() 才能恢复可见性
    // LeftToolBar3D：reparent 到 WorkbenchWindow 后填充视角导航按钮
    if (auto* lt = mainWindow3D->leftToolBar())
    {
        lt->setParent(&window);
        window.addToolBar(Qt::LeftToolBarArea, lt);
        own.commandActionHub->bindLeftToolBar(lt);
        lt->show();
    }
    // TopToolBar3D：从 MainWindow3D 移除后 reparent 到 WorkbenchWindow
    if (auto* tb = mainWindow3D->topToolBar())
    {
        mainWindow3D->removeToolBar(tb);
        tb->setParent(&window);
        window.addToolBar(Qt::TopToolBarArea, tb);
        own.commandActionHub->bindTopToolBar(tb);
        tb->show();
    }
    // RightToolBar3D：从 MainWindow3D 转移到 WorkbenchWindow
    if (auto* rt = mainWindow3D->rightToolBar())
    {
        mainWindow3D->removeToolBar(rt);
        rt->setParent(&window);
        window.addToolBar(Qt::RightToolBarArea, rt);
        own.commandActionHub->bindRightToolBar(rt);
        rt->show();
    }

    // 配置化菜单栏的 QAction 不在中枢的 m_actions 里，refreshFromSnapshot 触达不到。
    // 订阅中枢广播，用同一份 3D 快照 + CommandCatalog3D 规则外挂刷新菜单栏，
    // 与 2D 侧 selectionContextChanged → refreshCommandStates 对等。
    if (own.commandActionHub)
    {
        connect(own.commandActionHub.get(),

            &CommandActionHub3D::commandUiSnapshotRefreshed,
            this,
            [this](const CommandUiSnapshot3D& snapshot) {
                if (!m_workbenchWindow)
                {
                    return;
                }
                if (WorkbenchMenuManager* menus = m_workbenchWindow->menuManager())
                {
                    menus->refreshCommandStates3D(snapshot);
                }
            });
    }

    // 6. 状态栏：创建独立的 StatusBar3D 并挂载到 WorkbenchWindow
    //    Workbench3D 拥有 StatusBar3D 的完整生命周期，不再从 MainWindow3D reparent
    //    MainWindow3D 保留自己的 StatusBar3D 供内部使用（如导航提示）
    if (!m_statusBar3D)
    {
        m_statusBar3D = new StatusBar3D(&window);
        SY_DEBUG("[Workbench3D] StatusBar3D created for WorkbenchWindow");
    }
    window.mountStatusBar(m_statusBar3D);

    // 将光标世界坐标信号直接连接到 StatusBar3D 的位置标签
    if (m_services3D.renderWidget)
    {
        connect(m_services3D.renderWidget,
            &RenderWidget3D::sigCursorWorldPosition,
            m_statusBar3D,
            [sb3d = m_statusBar3D](float x, float y, float z, bool valid) {
                if (valid)
                {
                    sb3d->setPositionText(
                        QObject::tr("Position: (%1, %2, %3) mm").arg(x, 0, 'f', 2).arg(y, 0, 'f', 2).arg(z, 0, 'f', 2));
                }
                else
                {
                    sb3d->setPositionText(QObject::tr("Position: -"));
                }
            });

        // 将选中变化信号直接连接到 StatusBar3D 的选择/模型标签
        connect(m_services3D.renderWidget,
            &RenderWidget3D::sigSelectionChanged,
            m_statusBar3D,
            [sb3d = m_statusBar3D](const Eg::SyMeshEntity** entities, int count) {
                QString modelName;
                if (count > 0 && entities && entities[0])
                {
                    modelName = QString::number(entities[0]->id);
                }
                sb3d->setSelectionInfo(count, modelName, 0);
            });
    }

    SY_DEBUG("[Workbench3D] 3D workbench UI build completed");
}

// ==================== 3D 场景树（数据/算法/UI 分离） ====================

void Workbench3D::setupSceneTree3D(WorkbenchWindow& window)
{
    // 3D 场景树面板使用与 2D 共享的 SceneTreePanel
    // 不再单独创建 SceneTreePanel3D，确保 2D/3D 切换时使用同一实例
    m_scenePanel3D = window.sceneTreeDock();

    // 尝试从 skeleton's dock 查找已存在的 SceneTreePanel
    // 如果 skeleton's 面板类型不匹配，我们需要创建一个新的统一面板
    if (!m_scenePanel3D || qobject_cast<SceneTreePanel*>(m_scenePanel3D) == nullptr)
    {
        // skeleton's 面板不存在或类型不匹配，创建我们的 SceneTreePanel
        auto* sharedPanel = new SceneTreePanel(&window);
        sharedPanel->setObjectName(QStringLiteral("SceneTreeDock"));
        auto* sceneDock = window.registerDockWidget(QObject::tr("Scene"), sharedPanel, Qt::LeftDockWidgetArea);
        if (sceneDock)
        {
            sceneDock->setObjectName(UiDockIds::sceneQString());
            sceneDock->setMinimumWidth(180);
            sceneDock->setMaximumWidth(300);
        }
        m_scenePanel3D = sharedPanel;
    }

    if (!m_scenePanel3D)
    {
        return;
    }

    // 面板（UI）→ 引擎（业务）：用户操作通过引擎/算法层写回
    connect(m_scenePanel3D, &SceneTreePanel::selectionChanged, this, &Workbench3D::applySceneTreeSelection3D);
    // 使用 QueuedConnection 避免在 itemChanged 处理中重建树导致崩溃
    connect(m_scenePanel3D,
        &SceneTreePanel::visibilityToggled,
        this,
        &Workbench3D::toggleEntityVisibility3D,
        Qt::QueuedConnection);
    connect(m_scenePanel3D, &SceneTreePanel::renameRequested, this, &Workbench3D::renameEntity3D);
    connect(m_scenePanel3D, &SceneTreePanel::batchVisibilityRequested, this, &Workbench3D::setSceneTreeVisibility3D);
    connect(m_scenePanel3D, &SceneTreePanel::batchLockRequested, this, &Workbench3D::setSceneTreeLock3D);
    connect(m_scenePanel3D, &SceneTreePanel::deleteRequested, this, &Workbench3D::deleteSceneTreeSelection3D);

    // 引擎/场景（业务）→ 面板（UI）：变化后刷新展示与选中高亮
    if (m_services3D.renderWidget)
    {
        connect(m_services3D.renderWidget,
            &RenderWidget3D::sigSelectionChanged,
            this,
            [this](const Eg::SyMeshEntity** /*entities*/, int /*count*/) {
                syncSceneTreeSelection3D();
            });
    }
    else
    {
        SY_ERROR("[Workbench3D] setupSceneTree3D: m_services3D.renderWidget is null!");
    }
    if (m_services3D.sceneMonitor)
    {
        // 场景监视器会在几何变换（拖动）等高频路径上反复触发，而这些不改变树行集合；
        // 按结构签名（图元增删）判定即可跳过纯几何变更。可见性/锁定/改名由各自的
        // 显式方法直接重建（它们不推进 structureRevision，见 setSceneTreeVisibility3D 等）。
        connect(
            m_services3D.sceneMonitor, &SceneMonitor3D::sceneChanged, this, &Workbench3D::refreshSceneTree3DIfNeeded);
    }

    // 初始化防抖定时器
    if (!m_sceneTree3DRefreshTimer)
    {
        m_sceneTree3DRefreshTimer = new QTimer(this);
        m_sceneTree3DRefreshTimer->setSingleShot(true);
        m_sceneTree3DRefreshTimer->setInterval(WorkbenchTiming::kSceneTreeDebounceMs);
        connect(m_sceneTree3DRefreshTimer, &QTimer::timeout, this, [this]() {
            applySceneTreeIncremental3D("debounce");
        });
    }

    // 3D 导入完成后显式刷新一次树（导入会触发 markDataChanged → SceneMonitor ，
    // 此处 importFinished 作为兜底与显式接入点，二者叠加安全）
    if (m_persistence.importService)
    {
        connect(
            m_persistence.importService, &ImportService::importFinished, this, &Workbench3D::refreshSceneTree3DIfNeeded);
    }

    // 初始填充
    refreshSceneTree3D();
}

void Workbench3D::setupProperties3D(WorkbenchWindow& window)
{
    // 属性面板是可选 UI：配置驱动时可能不存在，因此先探测再绑定
    auto* props = window.propertiesDock();
    if (!props)
    {
        return;
    }

    props->setWorkbenchMode(PropertiesPanelWidget::WorkbenchMode::ThreeD);

    // 属性被编辑后延迟重建模型，避免在内联编辑器提交过程中重入（与 2D 同语义）
    auto conn = QObject::connect(props, &PropertiesPanelWidget::sigPropertyEdited, this, [this]() {
        QTimer::singleShot(0, this, [this]() {
            refreshPropertiesPanel3D();
        });
    });
    m_workbenchConnections.push_back(conn);

    refreshPropertiesPanel3D();
}

void Workbench3D::schedulePropertiesPanelRefresh3D()
{
    if (!m_propertiesRefreshTimer3D)
    {
        m_propertiesRefreshTimer3D = new QTimer(this);
        m_propertiesRefreshTimer3D->setSingleShot(true);
        m_propertiesRefreshTimer3D->setInterval(WorkbenchTiming::kPropertiesDebounceMs);
        connect(m_propertiesRefreshTimer3D, &QTimer::timeout, this, [this]() {
            refreshPropertiesPanel3D();
        });
    }
    // 已在等待窗口内 → 不重启也不新起：尾包语义，窗口结束只跑一次最新状态
    if (m_propertiesRefreshTimer3D->isActive())
    {
        return;
    }
    m_propertiesRefreshTimer3D->start();
}

void Workbench3D::refreshPropertiesPanel3D()
{
    if (!m_workbenchWindow)
    {
        return;
    }
    auto* props = m_workbenchWindow->propertiesDock();
    if (!props)
    {
        return;
    }

    // 读取当前选中图元 id（数据来源：引擎场景 / SelectionManager）
    std::vector<Eg::EntityId> entityIds;
    if (m_services3D.renderWidget)
    {
        const auto& selected = m_services3D.renderWidget->selectionManager().getSelectedEntities();
        entityIds.reserve(selected.size());
        for (const Eg::SyMeshEntity* e : selected)
        {
            if (e)
            {
                entityIds.push_back(e->id);
            }
        }
    }
    else if (m_sceneManager3D)
    {
        m_sceneManager3D->forEachSelectedEntityId(
            [](Eg::EntityId id, void* ctx) {
                static_cast<std::vector<Eg::EntityId>*>(ctx)->push_back(id);
            },
            &entityIds);
    }

    // 创建编辑会话（算法层）：持有图元 id，负责按需解析图元、应用修改
    auto session = std::make_shared<EntityPropertyEditSession3D>(m_sceneManager3D, std::move(entityIds));

    // 数据/算法产物推送给 UI 层：模型用于展示，会话作为编辑目标。
    props->setEditTarget(session);
    props->setPropertyModel(session->buildModel());
}

void Workbench3D::refreshSceneTree3D()
{
    if (!m_scenePanel3D)
    {
        return;
    }
    m_scenePanel3D->setMode3D(SceneTreeBuilder3D::build(m_sceneManager3D));
    // 记录本轮重建时的结构签名：之后的 sceneChanged 只要签名没变就不必再建
    m_lastSceneTree3DEntityCount = m_sceneManager3D ? m_sceneManager3D->getEntityCount() : 0;
    m_lastSceneTree3DStructureRevision = m_sceneManager3D ? m_sceneManager3D->structureRevision() : 0;
    // 全量重建已消费掉当前全部变更：推进增量游标，之后增量只读真正的新变更
    if (m_sceneManager3D)
    {
        m_sceneTree3DCursor = m_sceneManager3D->currentRevision();
    }
}

void Workbench3D::refreshSceneTree3DIfNeeded()
{
    if (!m_scenePanel3D || !m_sceneManager3D)
    {
        return;
    }

    // 结构签名只认图元增删：可见性 / 锁定 / 几何变换都不推进它。
    // 行集合不变时重建（build + 换模型 + 全表 dataChanged）纯属白做功，
    // 而拖动、显隐切换、批量锁定这些高频操作都会触发 sceneChanged。
    const std::size_t count = m_sceneManager3D->getEntityCount();
    const uint64_t structureRev = m_sceneManager3D->structureRevision();
    if (count == m_lastSceneTree3DEntityCount && structureRev == m_lastSceneTree3DStructureRevision)
    {
        return;
    }

    // 先记下已消费的签名再启表：否则连续 sceneChanged 每次都会重启单次定时器，
    // 重建被无限推迟；重建函数结束后会把签名再刷成最新值。
    m_lastSceneTree3DEntityCount = count;
    m_lastSceneTree3DStructureRevision = structureRev;

    if (m_sceneTree3DRefreshTimer)
    {
        // 防抖：批量增删（导入/阵列/批量删除）合并成一次重建
        m_sceneTree3DRefreshTimer->start();
    }
    else
    {
        applySceneTreeIncremental3D();
    }
}

void Workbench3D::applySceneTreeIncremental3D(const char* src)
{
    if (!m_scenePanel3D || !m_sceneManager3D)
    {
        return;
    }

    // 重入保护：分类/追加期间会触发嵌套场景通知；嵌套读到的游标仍是旧值，
    // 会把同一批变更再扫一遍。外层正在消费这批，嵌套延后到下一轮事件循环重跑。
    if (m_sceneTree3DIncrementalBusy)
    {
        QTimer::singleShot(0, this, [this]() { applySceneTreeIncremental3D("deferred"); });
        return;
    }
    struct BusyGuard
    {
        bool& flag;
        explicit BusyGuard(bool& f)
            : flag(f)
        {
            flag = true;
        }
        ~BusyGuard() { flag = false; }
    } busyGuard(m_sceneTree3DIncrementalBusy);

    Eg::SceneChangeSet set;
    if (!m_sceneManager3D->readChanges(m_sceneTree3DCursor, set))
    {
        // 游标太旧或日志截断：推进游标后必须全量重建
        m_sceneTree3DCursor = m_sceneManager3D->currentRevision();
        refreshSceneTree3D();
        return;
    }
    m_sceneTree3DCursor = set.toRevision;

    if (set.changes.empty())
    {
        // 无新变更：签名一致则无事可做；不一致（如手动启动的定时器）则全量刷新
        const std::size_t count = m_sceneManager3D->getEntityCount();
        if (count != m_lastSceneTree3DEntityCount ||
            m_sceneManager3D->structureRevision() != m_lastSceneTree3DStructureRevision)
        {
            refreshSceneTree3D();
        }
        return;
    }

    // 纯几何/样式/选中变更不影响 3D 树行集合，消费掉即可
    bool hasTreeRelevant = false;
    for (const Eg::SceneChange& ch : set.changes)
    {
        if (Eg::hasSceneChangeKind(ch.kinds, Eg::SceneChangeKind::Added) ||
            Eg::hasSceneChangeKind(ch.kinds, Eg::SceneChangeKind::Removed) ||
            Eg::hasSceneChangeKind(ch.kinds, Eg::SceneChangeKind::StructureChanged) ||
            Eg::hasSceneChangeKind(ch.kinds, Eg::SceneChangeKind::VisibilityChanged) ||
            Eg::hasSceneChangeKind(ch.kinds, Eg::SceneChangeKind::LockChanged))
        {
            hasTreeRelevant = true;
            break;
        }
    }
    if (!hasTreeRelevant)
    {
        return;
    }

    // 只有「全部是 Added 且图元当前仍在场景」才可增量；出现 Removed/可见性/锁定等
    // 一律回退全量。撤销会同时置 Added+Removed，必须按「当前是否在场景」判定。
    std::unordered_set<uint64_t> selectedSet;
    bool selectedInited = false;
    QList<SceneTreeNode3D> added;
    added.reserve(static_cast<int>(set.changes.size()));
    bool hasNonAdd = false;
    for (const Eg::SceneChange& ch : set.changes)
    {
        if (!Eg::hasSceneChangeKind(ch.kinds, Eg::SceneChangeKind::Added))
        {
            hasNonAdd = true;
            break;
        }
        auto* mesh = m_sceneManager3D->findMeshById(ch.entityId);
        if (!mesh)
        {
            hasNonAdd = true;
            break;
        }
        if (!selectedInited)
        {
            m_sceneManager3D->forEachSelectedEntityId(
                [](Eg::EntityId id, void* ctx) {
                    static_cast<std::unordered_set<uint64_t>*>(ctx)->insert(id);
                },
                &selectedSet);
            selectedInited = true;
        }
        const bool isSelected = selectedSet.count(ch.entityId) > 0;
        added.append(SceneTreeBuilder3D::buildMeshNode(mesh, isSelected));
    }

    if (hasNonAdd)
    {
        refreshSceneTree3D();
        return;
    }
    if (added.isEmpty())
    {
        return;
    }

    m_scenePanel3D->appendTopLevelNodes(added);

    // 与 refreshSceneTree3D() 尾部一致地更新结构签名（游标已在上方推进）
    m_lastSceneTree3DEntityCount = m_sceneManager3D->getEntityCount();
    m_lastSceneTree3DStructureRevision = m_sceneManager3D->structureRevision();

    SY_DEBUGF("[Workbench3D] scene tree incremental src=%s: +%lld nodes entities=%zu structRev=%llu",
        src ? src : "misc",
        static_cast<long long>(added.size()),
        m_lastSceneTree3DEntityCount,
        static_cast<unsigned long long>(m_lastSceneTree3DStructureRevision));
}

void Workbench3D::syncSceneTreeSelection3D()
{
    if (!m_scenePanel3D || !m_sceneManager3D || !m_services3D.renderWidget)
    {
        return;
    }

    // 从 SelectionManager3D 获取选中ID，而不是从 SceneManager3D
    // 因为框选/点击选择直接操作的是 SelectionManager3D
    auto& sel = m_services3D.renderWidget->selectionManager();
    const auto& selectedEntities = sel.getSelectedEntities();

    QSet<QString> selected;
    for (const Eg::SyMeshEntity* entity : selectedEntities)
    {
        if (entity)
        {
            selected.insert(QString::number(entity->id));
        }
    }

    m_scenePanel3D->setSelectedIds(selected);
}

void Workbench3D::applySceneTreeSelection3D(const QStringList& ids)
{
    if (!m_sceneManager3D || !m_services3D.renderWidget)
    {
        return;
    }
    auto& sel = m_services3D.renderWidget->selectionManager();

    // 收集后一次提交：逐个 addSelect 会让每个图元触发一次属性面板/状态栏刷新，
    // 场景树里多选（尤其全选）时会形成成片的无效刷新
    std::vector<Eg::SyMeshEntity*> meshes;
    meshes.reserve(static_cast<size_t>(ids.size()));
    for (const QString& id : ids)
    {
        const auto eid = Eg::parseEntityId(id.toStdString());
        if (!eid)
        {
            continue;
        }
        if (auto* mesh = m_sceneManager3D->findMeshById(*eid))
        {
            meshes.push_back(mesh);
        }
    }
    // additive=false：整体替换，等价于原来的 clearSelection + 逐个 addSelect
    sel.selectMany(meshes, false);

    // 选中变更不修改几何，通过调度器节流合并更新，避免 m_meshDirty 导致
    // gatherGeometry() 遍历所有图元
    m_services3D.renderWidget->requestSceneUpdate();
    syncSceneTreeSelection3D();
    schedulePropertiesPanelRefresh3D();
}

void Workbench3D::toggleEntityVisibility3D(const QString& id, bool visible)
{
    if (!m_sceneManager3D)
    {
        return;
    }
    const auto eid = Eg::parseEntityId(id.toStdString());
    if (!eid)
    {
        return;
    }
    if (auto* mesh = m_sceneManager3D->findMeshById(*eid))
    {
        mesh->setVisible(visible);
        m_sceneManager3D->markDataChanged();
        // 勾选路径：模型 setData 已先改写本行复选框，再进本回调；
        // 可见性不推进 structureRevision，无需 O(N) 树重建，只发一次 dataChanged 兜底
        if (m_scenePanel3D)
        {
            m_scenePanel3D->refreshRows({ static_cast<qint64>(mesh->id) });
        }
        schedulePropertiesPanelRefresh3D();
    }
}

void Workbench3D::renameEntity3D(const QString& id, const QString& newName)
{
    if (!m_sceneManager3D || newName.isEmpty())
    {
        return;
    }
    const auto eid = Eg::parseEntityId(id.toStdString());
    if (!eid)
    {
        return;
    }
    if (auto* mesh = m_sceneManager3D->findMeshById(*eid))
    {
        const QByteArray utf8 = newName.toUtf8();
        mesh->setName(utf8.constData());
        m_sceneManager3D->markDataChanged();
        // 改名：回调先于模型 setData 落盘，行文本由 setData 更新；
        // 不推进 structureRevision，不重建整树
        if (m_scenePanel3D)
        {
            m_scenePanel3D->refreshRows({ static_cast<qint64>(mesh->id) });
        }
        schedulePropertiesPanelRefresh3D();
    }
}

void Workbench3D::setSceneTreeVisibility3D(const QVector<qint64>& ids, bool visible)
{
    if (!m_sceneManager3D || ids.isEmpty())
    {
        return;
    }

    // 整数 id 直接转 uint64_t，批量设置（内部经回调记录 VisibilityChanged）
    std::vector<uint64_t> entityIds;
    entityIds.reserve(static_cast<size_t>(ids.size()));
    for (qint64 id : ids)
    {
        entityIds.push_back(static_cast<uint64_t>(id));
    }
    m_sceneManager3D->setEntitiesVisible(entityIds, visible);

    // 隐藏图元时清除选择（3D 目前只支持清空全部选择）
    if (!visible)
    {
        m_sceneManager3D->clearSelection();
    }

    // 可见性影响渲染，标记数据变更（经调度器节流）
    m_sceneManager3D->markDataChanged();

    // 场景树增量刷新受影响行（复选框态 + 发 dataChanged），不重建拓扑
    if (m_scenePanel3D)
    {
        m_scenePanel3D->setRowsVisible(ids, visible);
    }
}

void Workbench3D::setSceneTreeLock3D(const QVector<qint64>& ids, bool locked)
{
    if (!m_sceneManager3D || ids.isEmpty())
    {
        return;
    }

    // 批量锁定（内部经回调记录 LockChanged）。锁定不影响渲染，不调 markDataChanged()。
    std::vector<uint64_t> entityIds;
    entityIds.reserve(static_cast<size_t>(ids.size()));
    for (qint64 id : ids)
    {
        entityIds.push_back(static_cast<uint64_t>(id));
    }
    m_sceneManager3D->setEntitiesLocked(entityIds, locked);

    // 锁定态当前不在场景树中可视化，无需刷新树；命令状态由选择上下文刷新兜底。
}

void Workbench3D::deleteSceneTreeSelection3D(const QStringList& ids)
{
    if (!m_sceneManager3D || ids.isEmpty())
    {
        return;
    }

    // 收集后一次批量删除：逐个 removeEntity 每个都是 O(场景规模) 的查找与尾部搬移，
    // 而且每个都触发一次全场景选择同步 + 一次场景广播（M 个图元 → O(N·M) + M 次 UI 扇出）
    std::vector<Eg::SyMeshEntity*> targets;
    targets.reserve(static_cast<size_t>(ids.size()));
    for (const QString& id : ids)
    {
        const auto eid = Eg::parseEntityId(id.toStdString());
        if (!eid)
        {
            continue;
        }
        if (auto* mesh = m_sceneManager3D->findMeshById(*eid))
        {
            targets.push_back(mesh);
        }
    }
    if (targets.empty())
    {
        return;
    }

    m_sceneManager3D->deleteEntities(targets.data(), static_cast<int>(targets.size()));
    m_sceneManager3D->markDataChanged();
    // 删除推进 structureRevision，走防抖重建即可
    refreshSceneTree3DIfNeeded();
}

void Workbench3D::attachToWindow(WorkbenchWindow& window)
{
    m_workbenchWindow = &window;
    build3DWorkbenchUi(window);

    // 连接 MainWindow3D 的切换信号到 WorkbenchWindow 的工作台切换机制
    if (m_mainWindow3D)
    {
        connect(
            m_mainWindow3D.get(),
            &MainWindow3D::sigSwitchTo2D,
            &window,
            [&window]() {
                window.triggerWorkbench(QStringLiteral("2D"));
            },
            Qt::QueuedConnection);

        connect(
            m_mainWindow3D.get(),
            &MainWindow3D::sigSwitchTo3D,
            &window,
            [&window]() {
                window.triggerWorkbench(QStringLiteral("3D"));
            },
            Qt::QueuedConnection);
    }
}

void Workbench3D::refreshCommandUiState()
{
    // MainWindow3D 组装 3D 快照并推给中枢；中枢刷完托管动作后广播
    // commandUiSnapshotRefreshed，由 attachToWindow 里挂的订阅刷新配置化菜单栏。
    if (m_mainWindow3D)
    {
        m_mainWindow3D->refreshCommandUiState();
    }
}

// 4 — 激活工作台，应用初始状态
void Workbench3D::activate()
{
    // 从状态快照恢复（与 2D 保持一致）
    const auto& snapshot = m_savedState.currentViewMode.isEmpty() ? m_initialState : m_savedState;
    restoreFromSnapshot(snapshot);

    if (m_uiState.stateCenter)
    {
        m_uiState.stateCenter->setCurrentWorkbenchId(id());
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
    }
}

// 5 — 停用工作台，保存状态并清理资源
void Workbench3D::deactivate()
{
    m_savedState = currentSnapshot();

    // 先释放与窗口/快捷键绑定的 Qt 对象，再释放共享服务。
    // 这样可以避免 QMainWindow / 菜单 / 快捷键 在析构链中继续访问已销毁的 3D 服务。
    SY_DEBUG("[Workbench3D] Deactivating, tearing down UI objects first...");

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
        // sceneEditService 与 renderWidget 是两个独立指针，不能靠外层判空推断它非空
        if (m_services3D.sceneEditService)
        {
            m_services3D.sceneEditService->bindRenderWidget(nullptr);
        }
        m_services3D.renderWidget = nullptr;
    }

    // 场景树面板随窗口销毁，清空引用避免悬空
    m_scenePanel3D = nullptr;

    // 本工作台建立的连接整批断开（属性面板编辑回刷等），避免切台后回调命中悬空对象
    for (auto& conn : m_workbenchConnections)
    {
        disconnect(conn);
    }
    m_workbenchConnections.clear();

    // 属性面板节流定时器：切台后 propertiesDock 已销毁，挂起的回调必须取消
    if (m_propertiesRefreshTimer3D)
    {
        m_propertiesRefreshTimer3D->stop();
        m_propertiesRefreshTimer3D->deleteLater();
        m_propertiesRefreshTimer3D = nullptr;
    }

    // 清理 3D 场景树防抖定时器
    if (m_sceneTree3DRefreshTimer)
    {
        m_sceneTree3DRefreshTimer->stop();
        m_sceneTree3DRefreshTimer->deleteLater();
        m_sceneTree3DRefreshTimer = nullptr;
    }
    m_lastSceneTree3DEntityCount = 0;
    m_lastSceneTree3DStructureRevision = 0;
    m_sceneTree3DCursor = 0;
    m_sceneTree3DIncrementalBusy = false;

    // 2) 先销毁 3D 主窗口包装对象。
    //    它会持有大量 QAction / signal-slot / UI 状态引用，必须先于服务释放。
    SY_DEBUG("[Workbench3D] Destroying MainWindow3D...");
    m_mainWindow3D.reset();
    SY_DEBUG("[Workbench3D] MainWindow3D destroyed");

    // 3) 再释放 3D 服务 locator 与共享服务对象。
    //    注意：窗口销毁已经完成，这里再 shutdown 服务，可减少 Qt 析构阶段访问悬空对象的风险。
    SY_DEBUG("[Workbench3D] Releasing 3D services...");
    ServiceLocator3D::shutdown();
    m_services3D = ServicePack3D{};
    m_serviceOwner.reset();
    SY_DEBUG("[Workbench3D] Service released");
}

// 6 — 关闭工作台，清理所有资源
void Workbench3D::shutdown()
{
    deactivate();
    m_uiState = {};
    m_commands = {};
    m_scene = {};
    m_persistence = {};
    m_view = {};
}

void Workbench3D::releaseCentralWidgetGLResources(QWidget* centralWidget) const
{
    if (auto* vp = qobject_cast<Viewport3D*>(centralWidget))
    {
        vp->releaseGLResources();
    }
}

bool Workbench3D::requiresSkeletonDocks() const
{
    return false;
}

bool Workbench3D::managesOwnMenus() const
{
    // 菜单统一由 WorkbenchMenuManager 从客户 JSON 构建，
    // 3D 工作台不再自行管理窗口菜单栏。硬编码的 MenuManager3D 路径已移除。
    return false;
}

bool Workbench3D::showSettingsDialog(QWidget* /*parent*/)
{
    if (!m_mainWindow3D)
    {
        SY_WARN("[Workbench3D] showSettingsDialog: MainWindow3D is null");
        return false;
    }

    auto& own = *m_serviceOwner;
    if (!own.settingsCoordinator)
    {
        SY_WARN("[Workbench3D] showSettingsDialog: settings coordinator is null");
        return false;
    }

    // 直接在此调协调器，而不是转给 MainWindow3D::showSettingsDialog：快捷键页的模型来自
    // 框架层台账（配置驱动菜单建出的 QAction/QShortcut），UI3D 库看不到那一层。
    // 与 2D 的 Workbench2D::showSettingsDialog 保持同构。
    IShortcutSettingsModel* shortcutModel = m_workbenchWindow && m_workbenchWindow->menuManager()
        ? m_workbenchWindow->menuManager()->shortcutSettingsModel()
        : nullptr;

    if (!own.settingsCoordinator->showSettingsDialog(m_mainWindow3D->renderWidget(), shortcutModel))
    {
        return false;
    }

    m_mainWindow3D->setStatusMessage(QObject::tr("Settings applied and saved"));
    return true;
}

void Workbench3D::saveCurrentSettings()
{
    if (!m_mainWindow3D || !m_serviceOwner || !m_serviceOwner->settingsCoordinator)
    {
        return;
    }
    m_serviceOwner->settingsCoordinator->saveCurrentSettings(m_mainWindow3D->renderWidget());
}
#endif
