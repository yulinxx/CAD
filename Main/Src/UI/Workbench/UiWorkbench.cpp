#include "UiWorkbench.h"

#include <QAction>
#include <QShortcut>
#include <QSizePolicy>
#include <QTextEdit>
#include <QToolBar>
#include <QWidget>
#include <QTimer>
#include <QStatusBar>

#include "SceneDocument2D.h"
#include "SelectionService.h"
#include "UiServices.h"
#include "UiStateCenter.h"
#include "UI2D/Operation/CommandCatalog.h"
#include "UI2D/Operation/CommandActionHub.h"
#include "Engine2D/Edit/IUndoRedoManager.h"
#include "UiSceneTreeDock.h"
#include "UiPropertiesPanel.h"
#include "RenderViewport2D.h"
#include "DrawToolBarWidget.h"
#include "WorkbenchWindow.h"

#include "UI2D/Operation/OperationBus.h"
#include "UI2D/Operation/OperationId.h"
#include "UI2D/Operation/CommandCatalog.h"
#include "UI2D/Edit/QtLayerManagerBridge.h"
#include "UI/RightToolBar/RightToolBar.h"

#include "Ui/TopToolBar/TopToolBar.h"
#include "Ui/Dlg/LayerManagerDialog.h"
#include "Engine2D/SyEntity/SyLine.h"
#include "Engine2D/Interaction/LayerManager.h"
#include "Engine2D/Edit/LayerEditService.h"
#include "Import/ImportService.h"
#include "Color/Color.hpp"
#include "Log/SyLogger.h"

#if BUILD_UI3D
#include "UiEntities.h"
#include "UiViewport3D.h"
#include "SceneBuilder3D.h"
#include "Ui/TopToolBar/TopToolBar3D.h"
#include "Ui/StatusBar/StatusBar3D.h"
#endif

namespace
{
    /// 创建面板部件
    /// @param text 面板文本内容
    /// @param parent 父部件
    QWidget* createPanelWidget(const QString& text, QWidget* parent)
    {
        auto* label = new QLabel(text, parent);
        label->setAlignment(Qt::AlignCenter);
        label->setStyleSheet(QStringLiteral("background:#f0f0f0;border:1px solid #ccc;padding:20px;"));
        return label;
    }
}

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
    }
    return snapshot;
}

void UiWorkbench::restoreFromSnapshot(const WorkbenchStateSnapshot& snapshot)
{
    if (m_services.stateCenter)
    {
        m_services.stateCenter->setCurrentViewMode(snapshot.viewMode);
        m_services.stateCenter->setCurrentLayerId(snapshot.layerId);
        m_services.stateCenter->setCurrentDocumentId(snapshot.documentId);
    }
}

// ============================================================
// Workbench2D 实现
QString Workbench2D::id() const
{
    return QStringLiteral("2D");
}

Workbench2D::~Workbench2D()
{
    // 确保命令动作中枢被清理
    // 如果 deactivate/shutdown 已执行，这里只是安全兜底
    delete m_commandHub;
    m_commandHub = nullptr;
}

QString Workbench2D::displayName() const
{
    return QObject::tr("2D Workbench");
}

bool Workbench2D::initialize(const UiServices& services)
{
    if (!services.stateCenter || !services.interactionDispatcher)
        return false;
    m_services = services;

    // 创建选择服务，绑定到当前 SceneManager
    if (m_services.sceneManager)
        m_selectionService = std::make_unique<SelectionService>(m_services.sceneManager);
    m_services.selectionService = m_selectionService.get();

    m_initialState = WorkbenchStateSnapshot{};
    m_savedState = WorkbenchStateSnapshot{};
    return true;
}

void Workbench2D::attachToWindow(WorkbenchWindow& window)
{
    auto* viewport = createCentralViewport(window, nullptr);
    if (viewport)
    {
        window.setCentralWidget(viewport);
        configureModernViewport(viewport);

        // 注入服务到视口
        auto* vp = qobject_cast<RenderViewport2D*>(viewport);
        if (vp)
        {
            vp->setSelectionService(m_services.selectionService);
            vp->setInteractionDispatcher(m_services.interactionDispatcher);
            vp->setOperationBus(m_services.operationBus);
            vp->setSceneEditService(m_services.sceneEditService);
            vp->setDocument(m_services.document2D);

            // 设置状态回调
            if (m_services.stateCenter)
            {
                vp->setStatusCallback([stateCenter = m_services.stateCenter](const QString& text) {
                    QVariantMap meta = stateCenter->metadata();
                    meta["statusPrompt"] = text;
                    stateCenter->setMetadata(meta);
                    });
                vp->setSelectionCallback([stateCenter = m_services.stateCenter](const QString& selText, const QString& selType) {
                    stateCenter->setSelectionContext(selType, selText);
                    });
                vp->setPositionCallback([stateCenter = m_services.stateCenter](double x, double y) {
                    QVariantMap meta = stateCenter->metadata();
                    meta["mouseX"] = x;
                    meta["mouseY"] = y;
                    stateCenter->setMetadata(meta);
                    });
            }

            // 初始化工具系统
            vp->initializeTools();

            // 导入后自动 zoomToFit 回调
            if (m_services.importService)
            {
                m_services.importService->setViewportFitCallback([vp]() {
                    QTimer::singleShot(0, vp, [vp]() { vp->zoomToFit(); });
                    });
            }

            // 默认激活选择工具
            vp->setActiveTool(QStringLiteral("SelectTool"));
        }
    }

    // 创建左侧绘图工具栏
    auto* leftToolBar = new QToolBar(QObject::tr("Draw Tools"), &window);
    leftToolBar->setObjectName(QStringLiteral("DrawToolBar"));
    leftToolBar->setMovable(false);
    window.addToolBar(Qt::LeftToolBarArea, leftToolBar);

    auto* drawWidget = new DrawToolBarWidget(leftToolBar);
    drawWidget->setOperationBus(m_services.operationBus);

    // 从 CommandCatalog 构建工具定义列表
    QVector<DrawToolEntry> drawTools;
    for (const auto& entry : CommandCatalog::commands())
    {
        if (hasSurface(entry.surfaces, CommandSurface2D::LeftToolbar))
        {
            DrawToolEntry tool;
            tool.commandId = QString::fromUtf8(entry.toolName ? entry.toolName : "");
            tool.displayName = QString::fromUtf8(entry.text ? entry.text : "");
            tool.tooltip = QString::fromUtf8(entry.text ? entry.text : "");
            tool.shortcut = QString::fromUtf8(entry.shortcutId ? entry.shortcutId : "");
            drawTools.append(tool);
        }
    }
    drawWidget->setToolDefinitions(drawTools);
    leftToolBar->addWidget(drawWidget);

    // 创建 CommandActionHub 并初始化所有动作
    m_commandHub = new CommandActionHub(&window);
    m_commandHub->setMainWindow(&window);
    m_commandHub->rebuildAllActions();

    // 创建顶部工具栏（编辑命令）
    m_topToolBar = new TopToolBar(&window);
    m_topToolBar->setObjectName(QStringLiteral("TopToolBar"));
    m_topToolBar->installHubActions(m_commandHub);
    window.addToolBar(Qt::TopToolBarArea, m_topToolBar);

    // 创建右侧工具栏（颜色/图层）
    m_rightToolBar = new RightToolBar(&window);
    m_rightToolBar->setObjectName(QStringLiteral("RightToolBar"));
    window.addToolBar(Qt::RightToolBarArea, m_rightToolBar);

    // 绑定图层管理器到右侧工具栏
    if (m_services.layerManager)
    {
        m_rightToolBar->setLayerManager(m_services.layerManager, m_services.layerManagerBridge);
    }

    // 创建图层面板停靠窗口
    createLayersDock(window);

    configureWorkbenchPanels(nullptr);
    configureInitialWorkbenchState(nullptr);
}

QWidget* Workbench2D::createCentralViewport(WorkbenchWindow& window, PropertiesPanelWidget* properties)
{
    Q_UNUSED(window);
    Q_UNUSED(properties);
    auto* viewport = new RenderViewport2D();
    return viewport;
}

void Workbench2D::configureModernViewport(QWidget* viewport) const
{
    Q_UNUSED(viewport);
}

void Workbench2D::configureWorkbenchPanels(PropertiesPanelWidget* properties) const
{
    Q_UNUSED(properties);
}

SceneTreeDockWidget* Workbench2D::createLayersDock(WorkbenchWindow& window) const
{
    auto* sceneDock = new SceneTreeDockWidget(&window);
    sceneDock->setObjectName(QStringLiteral("SceneTreeDock"));
    window.registerDockWidget(QObject::tr("2D Layers"), sceneDock, Qt::LeftDockWidgetArea);
    return sceneDock;
}

void Workbench2D::configureWorkbenchActions(QToolBar* mainBar, QToolBar* viewBar) const
{
    Q_UNUSED(mainBar);
    Q_UNUSED(viewBar);
}

void Workbench2D::configureInitialWorkbenchState(PropertiesPanelWidget* properties) const
{
    Q_UNUSED(properties);
}

void Workbench2D::activate()
{
    // 从状态快照恢复（首次激活使用 m_initialState，后续使用 m_savedState）
    const auto& snapshot = m_savedState.viewMode.isEmpty()
        ? m_initialState
        : m_savedState;
    restoreFromSnapshot(snapshot);

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

    // 清理命令动作中枢
    // CommandActionHub 以 window 为 parent，Qt 会在窗口销毁时删除它
    // 但在工作台切换时需要显式清理，避免重复创建导致动作叠加
    delete m_commandHub;
    m_commandHub = nullptr;

    // 工具栏指针会随窗口清理而失效（clearWorkbenchContent 会 delete QToolBar）
    m_topToolBar = nullptr;
    m_rightToolBar = nullptr;
}

void Workbench2D::shutdown()
{
    deactivate();
    m_services = UiServices{};
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
#include "Ui/MainWindow/MainWindow3D.h"
#include "Ui/LeftToolBar/LeftToolBar3D.h"
#include "Ui/MenuManager/MenuManager3D.h"
#include "Ui/MenuManager/FileMenu3D.h"
#include "Ui/MenuManager/EditMenu3D.h"
#include "Ui/MenuManager/ViewMenu3D.h"
#include "Ui/MenuManager/HelpMenu3D.h"
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

// 1 — 初始化，存储服务引用
bool Workbench3D::initialize(const UiServices& services)
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

Workbench3D::~Workbench3D() = default;

// 2 — 构建 3D 工作台 UI
// 核心架构：Viewport3D -> IRenderer3D -> RenderWidget3DAdapter -> RenderWidget3D
// 通过 Viewport3D 统一视图宿主，Renderer 通过外部注入
void Workbench3D::build3DWorkbenchUi(WorkbenchWindow& window)
{
    // 隐藏骨架停靠面板（SceneDock / PropertiesDock），3D 工作台不需要这些面板
    window.setSkeletonDocksVisible(false);

    // ========== 创建所有 3D 服务 ==========
    SY_INFO("[Workbench3D] Creating 3D services...");
    m_serviceOwner = std::make_unique<ServiceOwner>();
    auto& own = *m_serviceOwner;

    own.operationBus = std::make_unique<OperationBus3D>(nullptr);
    own.documentManager = std::make_unique<DocumentManager3D>(nullptr);
    own.undoRedoManager = std::make_unique<UndoRedoManager3D>(nullptr);
    own.sceneEditService = std::make_unique<SceneEditService3D>(
        m_sceneManager3D, own.undoRedoManager.get(), own.documentManager.get());
    own.sceneMonitor = std::make_unique<SceneMonitor3D>(nullptr);
    own.shortcutManager = std::make_unique<ShortcutManager3D>(nullptr);
    own.navigationConfig = std::make_unique<NavigationConfig3D>();
    own.sceneDocument = std::make_unique<SceneDocument3D>(m_sceneManager3D);
    own.sceneDocumentAdapter = std::make_unique<SceneDocument3DAdapter>();
    // 使用非拥有的 shared_ptr 别名包装 m_sceneManager3D
    auto sceneManagerAlias = std::shared_ptr<Eg::SceneManager3D>(
        m_sceneManager3D, [](Eg::SceneManager3D*) {});
    own.sceneDocumentAdapter->setEngineScene(sceneManagerAlias);
    own.cameraController = std::make_unique<CameraController3D>();
    own.algorithmService = std::make_unique<AlgorithmApplicationService>(nullptr);
    own.settingsCoordinator = std::make_unique<SettingsUiCoordinator3D>(nullptr, own.navigationConfig.get());

#ifdef ENABLE_GEOMODELCORE
    own.brepModelService = std::make_unique<BRepModelService3D>(m_sceneManager3D);
#endif

    if (own.undoRedoManager)
        own.undoRedoManager->setSceneManager(m_sceneManager3D);
    if (own.sceneMonitor)
        own.sceneMonitor->rewatch(m_sceneManager3D);

    AlgorithmTaskRegistration3D::registerAll(*own.algorithmService);

    // Populate ServicePack3D
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

#ifdef ENABLE_GEOMODELCORE
    m_services3D.brepModelService = own.brepModelService.get();
#endif

    // Adopt to ServiceLocator3D for backward compatibility
    ServiceLocator3D::adopt(m_services3D);
    SY_INFO("[Workbench3D] 3D services created and adopted");

    SY_INFO("[Workbench3D] Creating MainWindow3D wrapper...");
    m_mainWindow3D = std::make_unique<MainWindow3D>(m_services3D, &window);
    SY_INFO("[Workbench3D] MainWindow3D created");

    // 连接切换到2D的信号到工作台切换
    connect(m_mainWindow3D.get(), &MainWindow3D::sigSwitchTo2D, &window, [&window]() {
        SY_INFO("[Workbench3D] sigSwitchTo2D triggered, switching to 2D workbench");
        window.triggerWorkbench(QStringLiteral("2D"));
        });

    // ========== Phase 0A: 使用 Viewport3D + RenderWidget3DAdapter ==========
    // 创建标准 3D 视图宿主 Viewport3D
    SY_INFO("[Workbench3D] Creating Viewport3D...");
    auto* viewport = new Viewport3D(&window);
    SY_INFOF("[Workbench3D] Viewport3D created: %p", viewport);

    // 通过统一工厂创建默认 renderer（兼容链）
    // 所有 IRenderer3D 实例必须通过 Renderer3DFactory 创建，禁止在宿主层分散 new
    SY_INFO("[Workbench3D] Creating renderer via Renderer3DFactory...");
    auto renderer = Renderer3DFactory::createDefault();
    SY_INFO("[Workbench3D] Renderer created via factory");

    // 通过组合根注入 renderer（当前使用兼容链，未来可无缝切换到新渲染后端）
    viewport->setRenderer(std::move(renderer));

    // 获取内部的 RenderWidget3D，以便设置场景管理器和连接信号
    auto* adapter = dynamic_cast<RenderWidget3DAdapter*>(viewport->renderer());
    if (adapter)
    {
        auto* renderWidget = adapter->widget();
        if (renderWidget)
        {
            // 设置场景管理器（RenderWidget3D 的原有接口）
            renderWidget->setSceneManager(m_sceneManager3D);
            SY_INFO("[Workbench3D] SceneManager3D set to RenderWidget3D");

            // 通过 ServicePack3D 注册 RenderWidget3D
            m_services3D.renderWidget = renderWidget;
            SY_INFO("[Workbench3D] RenderWidget3D registered in ServiceLocator3D");

            // 连接 RenderWidget3D 的信号到状态栏 — 通过 UiStateCenter 统一路由
            connect(renderWidget, &RenderWidget3D::sigCursorWorldPosition,
                [stateCenter = m_services.stateCenter](float x, float y, float z, bool valid) {
                    if (!stateCenter) return;
                    QVariantMap meta;
                    if (valid)
                        meta[QStringLiteral("positionText")] = QObject::tr("Position: (%1, %2, %3) mm")
                        .arg(x, 0, 'f', 2).arg(y, 0, 'f', 2).arg(z, 0, 'f', 2);
                    else
                        meta[QStringLiteral("positionText")] = QObject::tr("Position: -");
                    stateCenter->setMetadata(meta);
                });

            connect(renderWidget, &RenderWidget3D::sigSelectionChanged,
                [stateCenter = m_services.stateCenter](const std::vector<Eg::SyMeshEntity*>& entities) {
                    if (!stateCenter) return;
                    int count = static_cast<int>(entities.size());
                    QString modelName;
                    if (count > 0 && entities[0])
                        modelName = QString::number(entities[0]->id);

                    QVariantMap meta;
                    meta[QStringLiteral("3d_selCount")] = count;
                    meta[QStringLiteral("3d_modelName")] = modelName;

                    if (count > 0)
                        stateCenter->setSelectionContext(
                            QObject::tr("3D-Viewport"),
                            QObject::tr("%1 entities selected").arg(count));
                    else
                        stateCenter->setSelectionContext(
                            QObject::tr("3D-Viewport"),
                            QStringLiteral("none"));
                    stateCenter->setMetadata(meta);
                });

            // 连接 Delete/Backspace 键删除选中对象（渲染 widget 有焦点时生效）
            connect(renderWidget, &RenderWidget3D::sigKeyPressed,
                this, [this](int key, Qt::KeyboardModifiers) {
                    if (key == Qt::Key_Delete || key == Qt::Key_Backspace)
                    {
                        if (m_services3D.operationBus)
                        {
                            SY_INFO("[Workbench3D] Delete key pressed (renderWidget signal), running Edit_Delete operation");
                            m_services3D.operationBus->run(OperationId3D::Edit_Delete);
                        }
                    }
                });

            // 全局 Delete/Backspace 快捷键（渲染 widget 无焦点时也生效）
            // 通过 WorkbenchWindow::registerShortcut 注册，切换时自动清理
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
        else
        {
            SY_ERROR("[Workbench3D] RenderWidget3DAdapter::widget() returned null");
        }
    }
    else
    {
        SY_ERROR("[Workbench3D] Failed to cast renderer to RenderWidget3DAdapter");
    }

    // 设置 SceneDocument3DAdapter
    viewport->setSceneDocument(m_serviceOwner->sceneDocumentAdapter.get());
    SY_INFOF("[Workbench3D] SceneDocument3DAdapter set to Viewport3D: %p", m_serviceOwner->sceneDocumentAdapter.get());

    // 设置 CameraController3D
    viewport->setCameraController(own.cameraController.get());
    SY_INFO("[Workbench3D] CameraController3D set to Viewport3D");

    // 隐藏默认视口占位符并设置自定义 Viewport3D 为中央部件
    window.setCentralWidget(viewport);
    SY_INFO("[Workbench3D] Viewport3D set as central widget");

    // ========== 创建需要 MainWindow3D 的服务 ==========
    auto* mainWindow3D = m_mainWindow3D.get();
    mainWindow3D->hide();

    own.commandActionHub = std::make_unique<CommandActionHub3D>(mainWindow3D);
    own.algorithmRunner = std::make_unique<AlgorithmRunner3D>(
        own.algorithmService.get(), m_sceneManager3D,
        own.sceneEditService.get(), mainWindow3D);
    m_services3D.commandActionHub = own.commandActionHub.get();
    m_services3D.algorithmRunner = own.algorithmRunner.get();
    if (own.settingsCoordinator)
        own.settingsCoordinator->init();

    SY_INFO("[Workbench3D] Calling CommandRegistry3D::registerAll()...");
    CommandRegistry3D::registerAll(mainWindow3D);
    SY_INFO("[Workbench3D] CommandRegistry3D::registerAll() completed");

    // 初始化菜单管理器
    // MenuManager3D 自动创建 File/Edit/View/Help 菜单
    m_menuManager3D = std::make_unique<MenuManager3D>(mainWindow3D);
    m_menuManager3D->createMenus(mainWindow3D->menuBar());
    m_menuManager3D->connectMenuSignals();
    SY_INFO("[Workbench3D] MenuManager3D initialized");

    // 配置 CommandActionHub3D：注入操作总线并绑定菜单
    own.commandActionHub->setOperationBus(own.operationBus.get());
    if (auto* fm = qobject_cast<FileMenu3D*>(m_menuManager3D->fileMenu()))
        own.commandActionHub->bindFileMenu(fm);
    if (auto* em = m_menuManager3D->editMenu())
        own.commandActionHub->bindEditMenu(em);
    if (auto* vm = m_menuManager3D->viewMenu())
        own.commandActionHub->bindViewMenu(vm);
    own.commandActionHub->rebuildAll(own.shortcutManager.get());
    SY_INFO("[Workbench3D] CommandActionHub3D configured");

    // 连接菜单动作信号到操作总线
    connect(m_menuManager3D.get(), &MenuManager3D::sigMenuAction,
        this, &Workbench3D::onMenuAction);

    SY_INFO("[Workbench3D] 3D workbench UI build completed");
}

void Workbench3D::onMenuAction(int actionId, const QVariantMap& params)
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
void Workbench3D::attachToWindow(WorkbenchWindow& window)
{
    build3DWorkbenchUi(window);
}

// 4 — 激活工作台，应用初始状态
void Workbench3D::activate()
{
    // 从状态快照恢复（与 2D 保持一致）
    const auto& snapshot = m_savedState.viewMode.isEmpty()
        ? m_initialState
        : m_savedState;
    restoreFromSnapshot(snapshot);

    if (m_services.stateCenter)
    {
        m_services.stateCenter->setCurrentWorkbenchId(id());
    }
}

// 5 — 停用工作台，保存状态并清理资源
void Workbench3D::deactivate()
{
    m_savedState = currentSnapshot();

    SY_INFO("[Workbench3D] Deactivating, releasing services...");
    m_serviceOwner.reset();
    m_services3D = ServicePack3D{};
    ServiceLocator3D::shutdown();
    SY_INFO("[Workbench3D] Service released");

    // 快捷键已由 clearAllShortcuts() 删除，此处置空避免悬空指针
    m_deleteShortcut = nullptr;
    m_backspaceShortcut = nullptr;

    SY_INFO("[Workbench3D] Destroying MenuManager3D...");
    m_menuManager3D.reset();
    SY_INFO("[Workbench3D] MenuManager3D destroyed");

    SY_INFO("[Workbench3D] Destroying MainWindow3D...");
    m_mainWindow3D.reset();
    SY_INFO("[Workbench3D] MainWindow3D destroyed");
}

// 6 — 关闭工作台，清理所有资源
void Workbench3D::shutdown()
{
    deactivate();
    m_services = UiServices{};
}
#endif