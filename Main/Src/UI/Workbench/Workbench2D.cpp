#include "Workbench2D.h"
#include "WorkbenchTiming.h"

#include <QAction>
#include <QActionGroup>
#include <QMenu>
#include <QDockWidget>
#include <QShortcut>
#include <QSizePolicy>
#include <QTextEdit>
#include <QPlainTextEdit>
#include <QLineEdit>
#include <QApplication>
#include <QToolBar>
#include <QWidget>
#include <QEvent>
#include <QKeyEvent>
#include <QTimer>
#include <QStatusBar>
#include <QFileInfo>
#include <QDesktopServices>

#include <QUrl>
#include <string>
#include <vector>
#include <functional>

#include "Composition/ApplicationCompositionRoot.h"
#include "SceneDocument2D.h"
#include "UiServices.h"
#include "UiStateCenter.h"
#include "UI/Services/ViewportActionHub.h"
#include "UI/Services/HelpDialogService.h"
#include "UI/Services/ISelectionService.h"
#include "UI/Service/ViewCaptureService.h"
#include "UiSceneTreePanel.h"
#include "SceneTreeModel2D.h"
#include "SceneTreeBuilder2D.h"


#include "UiPropertiesPanel.h"
#include "RenderViewport2D.h"
#include "FileDropHandler.h"

#include <optional>
#include "DrawToolBarWidget.h"
#include "DrawToolSwitchRegistry.h"
#include "WorkbenchWindow.h"
#include "WorkbenchMenuManager.h"
#include "WorkbenchLayoutManager.h"

#include "ClientConfig/UiClientConfigBase.h"
#include "ClientConfig/UiConfigurationManager.h"
#include "ClientConfig/UiContextMenuService.h"
#include "ClientConfig/UiLayoutBuilder.h"

#include "UiStateBridge2D.h"
#include "UiDockIds.h"
#include "RenderWidget.h"

#include "UI2D/Operation/CommandActionHub.h"
#include "UI2D/Operation/OperationBus.h"
#include "UI2D/Operation/OperationId.h"
#include "UI2D/Operation/OperationRouting.h"
#include "UI2D/Operation/CommandCatalog.h"
#include "UI2D/Edit/QtLayerManagerBridge.h"
#include "UI2D/Dlg/LayerManagerDialog.h"
#include "UI2D/Service/EntityPropertyModel2D.h"
#include "UI2D/Service/EntityPropertyEditSession2D.h"

#include "UI2D/Service/SceneMonitor.h"
#include "UI2D/Operation/CommandCatalog.h"
#include "UI2D/Operation/OperationId.h"
#include "UI2D/Operation/OperationBus.h"
#include "UI2D/Settings/SettingsUiCoordinator2D.h"
#include "UI2D/StatusBar/StatusBar.h"
#include "UI2D/ToolBar/RightToolBar.h"
#include "UI2D/ToolBar/TopToolBar.h"
#include "UI2D/ToolBar/TextFontToolBar.h"
#include "UI2D/DrawTools/ToolManager.h"

#include "UI/Settings/SettingsService.h"
#include "UI/DrawTools/TextEditTool.h"
#include "UI/UiMetrics.h"
#include "UI/ThemeManager.h"
#include "UI/Service/ToolBarContextManager.h"

#include "Engine2D/Edit/LayerEditService.h"
#include "Engine2D/Edit/SceneEditService.h"
#include "Engine2D/Interaction/LayerManager.h"
#include "Engine2D/Core/SceneManager.h"
#include "Engine2D/Core/SceneManager.h"
#include "Engine/Edit/IUndoRedoManager.h"

#include "Engine/EntityIdUtils.h"
#include "Engine/SyEntity/SyEntity.h"

#include "UiStateCenter.h"

#include "Import/ImportService.h"
#include "Color/Color.hpp"
#include "Log/SyLogger.h"

// ==================== Workbench2D helpers ====================

namespace
{
    /// ISelectionService::visitSelectedIds 的收集 visitor（POD 安全，收集图元 ID）
    void collectSelectedIds(const char* id, void* context)
    {
        auto* ids = static_cast<std::vector<Eg::EntityId>*>(context);
        if (id)
        {
            if (auto eid = Eg::parseEntityId(std::string(id)))
            {
                ids->push_back(*eid);
            }
        }
    }
}  // namespace

// ============================================================
// Workbench2D 实现
QString Workbench2D::id() const
{
    return QStringLiteral("2D");
}

namespace
{
    bool workbenchFlagEnabled(const QStringList& workbenches, const QString& workbenchId)
    {
        if (workbenches.isEmpty())
        {
            return true;
        }
        for (const auto& wb : workbenches)
        {
            if (wb.compare(workbenchId, Qt::CaseInsensitive) == 0)
            {
                return true;
            }
        }
        return false;
    }
}  // namespace

bool Workbench2D::isCommandRegistered(const QString& commandId) const
{
    // 与 Workbench3D 对齐：直接按命令目录裁决，不依赖任何运行时分发对象，
    // 避免工作台装配时序导致菜单命令被误判为未注册而整批过滤。
    return CommandCatalog::operationForCommandId(commandId) != OperationId::None;
}

void Workbench2D::dispatchCommand(const QString& commandId, const QVariantMap& params)
{
    auto* bus = m_commands.operationBus;
    if (!bus)
    {
        SY_WARNF("[Workbench2D] Cannot dispatch without OperationBus: %s", qPrintable(commandId));
        return;
    }

    // 旋转 90°/180°、对齐方向、单位切换等命令需要按 MenuActionId 精确路由，
    // 由 OperationRouting 携带预设参数；其余命令按 OperationId 常规分发。
    const UI::MenuActionId menuId = CommandCatalog::menuIdForCommandId(commandId);
    if (menuId != static_cast<UI::MenuActionId>(0))
    {
        SY_DEBUGF("[Workbench2D] Dispatch command='%s' menuId=%d source=Menu",
            qPrintable(commandId),
            static_cast<int>(menuId));
        OperationRouting::dispatch(menuId, bus, OperationSource::Menu, params);
        return;
    }

    const OperationId operation = CommandCatalog::operationForCommandId(commandId);
    if (operation == OperationId::None)
    {
        SY_WARNF("[Workbench2D] Unknown command: %s", qPrintable(commandId));
        return;
    }

    SY_DEBUGF("[Workbench2D] Dispatch command='%s'", qPrintable(commandId));
    bus->run(operation, params, OperationSource::Menu);
}

QString Workbench2D::commandText(const QString& commandId) const
{
    const OperationId operation = CommandCatalog::operationForCommandId(commandId);
    const auto* entry = CommandCatalog::findByOperation(operation);
    return entry && entry->text ? QString::fromUtf8(entry->text) : QString();
}

Workbench2D::Workbench2D() = default;
Workbench2D::~Workbench2D() = default;

QString Workbench2D::displayName() const
{
    return QObject::tr("2D Workbench");
}

bool Workbench2D::initialize(const WorkbenchServices& services)
{
    SY_INFO("[Workbench2D] initialize: starting 2D workbench initialization");

    if (!services.uiState.stateCenter)
    {
        SY_ERRORF("[Workbench2D] initialize failed: stateCenter=%p",
            static_cast<void*>(services.uiState.stateCenter));
        return false;
    }
    m_uiState = services.uiState;
    m_commands = services.commands;
    m_scene = services.scene;
    m_persistence = services.persistence;
    m_view = services.view;

    // 使用应用共享 SettingsService singleton，2D/3D 逻辑一致
    m_settingsCoordinator = std::make_unique<SettingsUiCoordinator2D>(ApplicationCompositionRoot::getSettingsService());

    // 注册 2D 专属设置项，确保设置页已存在
    if (m_settingsCoordinator)
    {
        m_settingsCoordinator->init();
    }

    SY_DEBUG("[Workbench2D] initialize: 2D workbench initialized successfully");
    return true;
}

void Workbench2D::attachToWindow(WorkbenchWindow& window)
{
    m_workbenchWindow = &window;

    auto* viewport = createCentralViewport(window, nullptr);
    if (!viewport)
    {
        SY_ERROR("[Workbench2D] attachToWindow failed: createCentralViewport returned null");
        return;
    }

    window.setCentralWidget(viewport);

    auto* vp = qobject_cast<RenderViewport2D*>(viewport);
    if (vp)
    {
        m_viewport = vp;
        setupViewportServices(vp, window);
        vp->initializeTools();
        // LOD 参数调用链收敛：设置面板经 ViewRenderCoordinator 读写曲线精度
        // （initializeTools 之后 coordinator 才创建）
        if (m_settingsCoordinator)
        {
            m_settingsCoordinator->setRenderCoordinator(vp->renderCoordinator());
        }
        setupImportCallbacks(vp, window);
        vp->setActiveTool(QStringLiteral("SelectTool"));
    }

    // 属性被编辑后（已入撤销栈）延迟重建模型，避免在内联编辑器提交过程中重入
    if (auto* props = window.propertiesDock())
    {
        auto conn = QObject::connect(props, &PropertiesPanelWidget::sigPropertyEdited, this, [this]() {
            QTimer::singleShot(0, this, [this]() {
                if (m_propertiesManager)
                {
                    m_propertiesManager->refresh();
                }
            });
        });
        m_workbenchConnections.push_back(conn);
    }

    // 视口动作中枢：注入当前视口，供菜单 Zoom 子菜单与右键菜单 View_* 操作统一分发。
    // 分发不经过窗口层回调 —— View_* 命令在 CoreOperationRegistry 里直接消费本中枢。
    if (m_view.viewportActionHub)
    {
        m_view.viewportActionHub->setViewport(m_viewport);
    }

    createToolbars(window);
    setupSceneTree(window);

    // 启动时从数据库加载并应用已保存的 2D 专属设置（画布/网格/标尺/捕捉）
    if (m_settingsCoordinator && m_viewport)
    {
        m_settingsCoordinator->loadAndApplySettings(
            m_viewport->renderWidget(), m_viewport->gridSnapManager(), m_view.unitManager);
    }

    // 创建 2D 状态栏 widget 并挂载到窗口
    // StatusBar 封装了坐标/选择/消息/状态信息的显示，与 3D StatusBar3D 完全独立
    if (!m_statusBar2D)
    {
        m_statusBar2D = new StatusBar(&window);
        // Position 标签弹出单位选择 → 复用与视图菜单完全相同的命令分发路径
        m_statusBar2D->setUnitManager(m_view.unitManager);
        // dispatchCommand 现在带 params，信号只给 commandId，这里用 lambda 补空参数
        connect(m_statusBar2D, &StatusBar::sigUnitCommandRequested, this, [this](const QString& commandId) {
            dispatchCommand(commandId, QVariantMap{});
        });
        SY_DEBUG("[Workbench2D] StatusBar created");
    }
    window.mountStatusBar(m_statusBar2D);

    // 命令 UI 刷新触发源集中挂载（选择/图层锁/场景变化含图元锁/撤销栈/操作完成）。
    // 放在最后：此时视口、工具栏、场景树、状态栏均已就绪，install 内的首次刷新可覆盖全部 UI。
    // 返回的句柄必须留到 deactivate 销毁：bus / layerBridge 跨工作台长寿，
    // 连线两端都不死时 Qt 不会自动回收，往返切换 N 次就会刷 N 次。
    m_uiStateConnections = UiStateBridge2D::install(
        this, m_viewport, m_commands.operationBus, m_scene.layerManagerBridge, m_sceneMonitor);
}

bool Workbench2D::showSettingsDialog(QWidget* /*parent*/)
{
    SY_DEBUGF("[Workbench2D] showSettingsDialog: m_settingsCoordinator=%p m_viewport=%p",
        static_cast<void*>(m_settingsCoordinator.get()),
        static_cast<void*>(m_viewport));

    if (!m_settingsCoordinator || !m_viewport)
    {
        SY_WARNF("[Workbench2D] showSettingsDialog: failed - coordinator or viewport is null");
        return false;
    }

    // 2D 捕捉设置由视口持有的 GridSnapManager 驱动（coordinator 已做空指针保护）
    RenderWidget* widget = m_viewport->renderWidget();
    // 快捷键页的数据来自配置驱动菜单的台账（菜单管理器持有），2D/3D 同一份实现
    IShortcutSettingsModel* shortcutModel = m_workbenchWindow && m_workbenchWindow->menuManager()
        ? m_workbenchWindow->menuManager()->shortcutSettingsModel()
        : nullptr;
    return m_settingsCoordinator->showSettingsDialog(
        widget, m_viewport->gridSnapManager(), m_view.unitManager, shortcutModel);
}

void Workbench2D::saveCurrentSettings()
{
    if (!m_settingsCoordinator || !m_viewport)
    {
        return;
    }
    m_settingsCoordinator->saveCurrentSettings(
        m_viewport->renderWidget(), m_viewport->gridSnapManager(), m_view.unitManager);
}

QWidget* Workbench2D::createCentralViewport(WorkbenchWindow& window, PropertiesPanelWidget* properties)
{
    if (!m_viewportSetup)
    {
        m_viewportSetup = std::make_unique<Workbench2DViewportSetup>();
    }
    return m_viewportSetup->createCentralViewport(window, properties);
}

void Workbench2D::setupViewportServices(RenderViewport2D* vp, WorkbenchWindow& window)
{
    if (m_viewportSetup)
    {
        m_viewportSetup->setupViewportServices(vp, window, m_commands.operationBus,
            m_uiState.stateCenter,
            m_scene.selectionService, m_scene.document2D,
            m_scene.sceneEditService, m_scene.layerManager,
            m_scene.sceneEditService ? m_scene.sceneEditService->sceneManager() : nullptr);
    }

    if (!m_shortcutManager)
    {
        m_shortcutManager = std::make_unique<Workbench2DShortcutManager>(this);
    }
    m_shortcutManager->registerGlobalShortcuts(window, vp, m_commands.operationBus, m_scene.selectionService);
}

void Workbench2D::setupImportCallbacks(RenderViewport2D* vp, WorkbenchWindow& window)
{
    if (m_viewportSetup)
    {
        m_viewportSetup->setupImportCallbacks(vp, window, m_persistence.importService);
    }
}

void Workbench2D::setPanelHostStyle(PanelHostStyle style)
{
    if (m_panelHostStyle == style)
    {
        return;
    }
    m_panelHostStyle = style;
}

void Workbench2D::createToolbars(WorkbenchWindow& window)
{
    if (!m_toolbarFactory)
    {
        m_toolbarFactory = std::make_unique<Workbench2DToolbarFactory>();
    }

    // 命令动作中枢由工作台持有：工具栏工厂、右键菜单、属性面板、UI 刷新都直接依赖它。
    // 每次 attach 随窗口重建（deactivate 中 reset），工厂只借用裸指针，不接管所有权。
    if (!m_commandHub)
    {
        m_commandHub = std::make_unique<CommandActionHub>();
    }

    // 工具切换操作注册到 OperationBus。lambda 捕获指向 m_viewport 成员的间接引用
    // （RenderViewport2D**），切台后成员被置空/重指，操作始终跟随当前视口；
    // 不能在工厂里对 createToolbars 的栈参数取址，函数返回即悬空。
    DrawToolSwitchRegistry(m_commands.operationBus, &m_viewport).registerAll();

    m_toolbarFactory->createToolbars(window, m_viewport, m_commands.operationBus,
        m_uiState.stateCenter, m_commandHub.get(), m_panelHostStyle,
        m_commands.undoManager, m_scene.clipboard,
        m_scene.selectionService, m_scene.layerManager, m_scene.sceneEditService);

    setupRightToolbarLayers();

    // 右键菜单构建器与属性面板管理器持有中枢裸指针，必须随中枢一起重建，
    // 并在 deactivate 中先于中枢 reset
    m_contextMenuBuilder = std::make_unique<Workbench2DContextMenuBuilder>(
        m_commandHub.get(), m_scene.layerManager, &window);
    m_propertiesManager = std::make_unique<Workbench2DPropertiesPanelManager>();
    m_propertiesManager->setup(&window, m_scene.sceneEditService, m_commandHub.get());

    // 视口右键菜单请求：交给命令中枢统一构建并弹出（含选择/锁定实时联动）
    if (m_viewport)
    {
        connect(m_viewport, &RenderViewport2D::contextMenuRequested, this, &Workbench2D::onViewportContextMenu);
    }

    // 撤销/重做会恢复/移除/重建图元（场景拓扑变化），增量刷新可能遗漏，
    // 使用 requestLightRefresh() 经调度器节流触发重绘，避免全量几何重建
    if (m_commands.operationBus)
    {
        connect(m_commands.operationBus, &OperationBus::operationCompleted, this,
            [this](OperationId id, bool success) {
                if (success && m_viewport && (id == OperationId::Edit_Undo || id == OperationId::Edit_Redo))
                {
                    m_viewport->requestLightRefresh();
                }
            });
    }

    // 命令中枢广播的选择上下文快照 → applySelectionContext 单一扇出点
    connect(m_commandHub.get(), &CommandActionHub::selectionContextChanged, this,
        [this](const CommandUiSnapshot& snapshot) {
            applySelectionContext(snapshot);
        });
}

void Workbench2D::setupRightToolbarLayers()
{
    RightToolBar* rightToolBar = m_toolbarFactory ? m_toolbarFactory->rightToolBar() : nullptr;
    if (!rightToolBar)
    {
        return;
    }

    // 右侧图层面板数据源（颜色/图层名/锁定态）
    if (m_scene.layerManager)
    {
        rightToolBar->setLayerManager(m_scene.layerManager, m_scene.layerManagerBridge);
    }

    if (m_uiState.stateCenter)
    {
        QVariantMap meta = m_uiState.stateCenter->metadata();
        meta.insert(QStringLiteral("rightPanelSource"), QStringLiteral("LayerManager"));
        m_uiState.stateCenter->setMetadata(meta);
    }

    // 单击色块 → 若已选中未锁定图元则将其移动到该图层（可撤销），并设为当前图层
    connect(rightToolBar, &RightToolBar::sigLayerSelected, this, [this](int layerId) {
        // 色块是 QPushButton 而非 QAction，无法靠 enableRule 灰显，
        // 按同一份快照做前置判定，避免点色块绕过锁定改动被锁图元。
        // 「设为当前图层」不受锁定影响，始终执行，故只 gate 移动这半段。
        const bool selectionLocked = m_commandHub && m_commandHub->currentSnapshot().anyLocked();
        if (!selectionLocked && m_scene.selectionService && m_scene.layerEditService)
        {
            std::vector<Eg::EntityId> selectedIds;
            m_scene.selectionService->visitSelectedIds(&collectSelectedIds, &selectedIds);
            if (!selectedIds.empty())
            {
                m_scene.layerEditService->assignEntitiesToLayer(selectedIds, layerId, "Move selected to layer");
            }
        }
        if (m_scene.layerManager)
        {
            m_scene.layerManager->setCurrentLayer(layerId);
        }
        // 图层变更后刷新视图和动作状态
        refreshCommandUiState();
        if (m_viewport)
        {
            m_viewport->requestFullRefresh();
        }
    });

    // 双击色块 → 打开图层管理对话框（其中设置当前图层会通过 sigCurrentLayerChanged 同步回右侧色块）
    connect(rightToolBar, &RightToolBar::sigLayerDoubleClicked, this, [this](int /*layerId*/) {
        if (m_scene.layerEditService)
        {
            LayerManagerDialog::showDialog(m_scene.layerEditService,
                m_commandHub ? m_commandHub->mainWindow() : nullptr,
                m_scene.layerManagerBridge);
        }
    });
}

void Workbench2D::setupSceneTree(WorkbenchWindow& window)
{
    m_sceneTreeManager = std::make_unique<Workbench2DSceneTreeManager>();
    m_sceneTreeManager->setup(window, m_viewport, m_commands.operationBus,
        m_scene.sceneEditService, m_scene.selectionService, m_uiState.stateCenter,
        [this]() { refreshCommandUiState(); });
}

void Workbench2D::refreshSceneTree()
{
    if (m_sceneTreeManager)
    {
        m_sceneTreeManager->refreshSceneTree();
    }
}

void Workbench2D::refreshSceneTreeIfNeeded(const char* src)
{
    if (m_sceneTreeManager)
    {
        m_sceneTreeManager->refreshSceneTreeIfNeeded(src);
    }
}

void Workbench2D::applySceneTreeIncremental(const char* src)
{
    if (m_sceneTreeManager)
    {
        m_sceneTreeManager->applySceneTreeIncremental(src);
    }
}

void Workbench2D::syncSceneTreeSelection()
{
    if (m_sceneTreeManager)
    {
        m_sceneTreeManager->syncSceneTreeSelection();
    }
}

void Workbench2D::applySceneTreeSelection(const QStringList& ids)
{
    if (m_sceneTreeManager)
    {
        m_sceneTreeManager->applySceneTreeSelection(ids);
    }
}

void Workbench2D::toggleEntityVisibility(const QString& id, bool visible)
{
    if (m_sceneTreeManager)
    {
        m_sceneTreeManager->toggleEntityVisibility(id, visible);
    }
}

void Workbench2D::renameEntity(const QString& id, const QString& newName)
{
    if (m_sceneTreeManager)
    {
        m_sceneTreeManager->renameEntity(id, newName);
    }
}

void Workbench2D::deleteSceneTreeSelection(const QStringList& ids)
{
    if (m_sceneTreeManager)
    {
        m_sceneTreeManager->deleteSceneTreeSelection(ids);
    }
}

void Workbench2D::setSceneTreeVisibility(const QVector<qint64>& ids, bool visible)
{
    if (m_sceneTreeManager)
    {
        m_sceneTreeManager->setSceneTreeVisibility(ids, visible);
    }
}

void Workbench2D::setSceneTreeLock(const QVector<qint64>& ids, bool locked)
{
    if (m_sceneTreeManager)
    {
        m_sceneTreeManager->setSceneTreeLock(ids, locked);
    }
}

void Workbench2D::onViewportContextMenu(QContextMenuEvent* event)
{
    if (m_contextMenuBuilder)
    {
        m_contextMenuBuilder->onViewportContextMenu(event);
    }
}

void Workbench2D::refreshCommandUiState()
{
    if (!m_commandHub)
    {
        return;
    }

    // 一次抓取，多处消费：refreshActionStates 内部会广播 selectionContextChanged，
    // 由 applySelectionContext 扇出到属性面板/状态栏/场景树/工具栏上下文。
    m_commandHub->refreshActionStates(m_commandHub->captureSnapshot(m_commandHub->mainWindow()));
}

void Workbench2D::applySelectionContext(const CommandUiSnapshot& snapshot)
{
    // 选择上下文的**唯一扇出点**：入参这份快照就是全部事实来源。
    // 新增需要随选择/锁定联动的组件时，在这里加一次推送，组件自身只渲染、不判定；
    // 反过来，这里任何一处再去问场景或问 Hub 缓存，都会把「规则漂移」放回来。

    // 属性面板：10Hz 节流，避免拖动期间每帧重建（60fps → 10fps）
    // 节流定时器由属性面板管理器内部持有，尾包语义保证窗口结束只跑一次最新状态。
    if (m_propertiesManager)
    {
        m_propertiesManager->scheduleRefresh();
    }

    // 状态栏选择指示器：直接用快照里的 selectionCount，不再二次遍历场景
    if (m_statusBar2D)
    {
        const int n = snapshot.selectionCount;
        m_statusBar2D->setSelectionInfo(n, tr("Selected: %1").arg(n));
    }

    // 场景树右键菜单：与视口右键菜单共用同一份 hasSelection / anyLocked 判定，消除规则漂移。
    // 注意：锁定态当前不在树中可视化，因此 setCommandState 只用于更新右键菜单灰显。
    if (SceneTreePanel* panel = scenePanel())
    {
        panel->setCommandState(snapshot.hasSelection, snapshot.anyLocked());
    }

    // 菜单栏：菜单项是独立于命令中枢创建的 QAction（config-driven 与 legacy 两条构建路径），
    // 中枢的 refreshActionStates 触达不到，故在此按同一份快照统一刷新启用态。
    if (m_workbenchWindow)
    {
        if (WorkbenchMenuManager* menus = m_workbenchWindow->menuManager())
        {
            menus->refreshCommandStates(snapshot);
        }
    }

    // 顶部工具栏上下文：上下文管理器已随工具栏一起归工厂所有，
    // 按同一份快照的 typeMask 切换（Text/QR/Bitmap/Vector/Default）
    if (m_toolbarFactory)
    {
        if (auto* contextManager = m_toolbarFactory->contextManager())
        {
            const ToolBarContext newCtx = m_toolbarFactory->determineContextFromSelection(snapshot);
            if (contextManager->currentContext() != newCtx)
            {
                contextManager->setCurrentContext(newCtx);
            }
        }
    }
}

void Workbench2D::activate()
{
    // 从状态快照恢复（首次激活使用 m_initialState，后续使用 m_savedState）
    const auto& snapshot = m_savedState.currentViewMode.isEmpty() ? m_initialState : m_savedState;
    restoreFromSnapshot(snapshot);

    // 恢复工具状态：将快照中的工具 ID 应用到视口
    if (m_viewport && !snapshot.activeToolId.isEmpty())
    {
        m_viewport->setActiveTool(snapshot.activeToolId);
        SY_DEBUGF("[Workbench2D] Restored tool: %s", qPrintable(snapshot.activeToolId));
    }

    // 激活时刷新一次动作状态（撤销/重做可用性 + 选中项驱动的启用态）
    refreshCommandUiState();
}

void Workbench2D::deactivate()
{
    m_savedState = currentSnapshot();

    // 断开长命对象到本工作台的所有连接。
    // operationBus / layerManagerBridge 跨工作台存活，而 Workbench2D 实例被 UiShellHost
    // 缓存复用、切换时不销毁 —— Qt 不会自动断开，attachToWindow 每次又重新 connect 一遍。
    // 不断的后果不是崩溃而是叠加：N 次 2D↔3D 往返后，一次 2D 操作会触发 N 份场景树重建
    // 与 N 份命令状态刷新。这些连接全部在 attach 期建立（含 UiStateBridge2D::install），
    // 因此按"发送者 + 接收者"整体断开是安全的，下次 attach 会重新装。
    for (auto& conn : m_workbenchConnections)
    {
        disconnect(conn);
    }
    m_workbenchConnections.clear();
    if (m_commands.operationBus)
    {
        QObject::disconnect(m_commands.operationBus, nullptr, this, nullptr);
    }
    if (m_scene.layerManagerBridge)
    {
        QObject::disconnect(m_scene.layerManagerBridge, nullptr, this, nullptr);
    }

    // 清除 ImportService 中持有的视口回调，防止切换后悬空指针

    // ImportService 生命周期长于工作台，不清理会导致 use-after-free
    if (m_persistence.importService)
    {
        m_persistence.importService->setViewportFitCallback(nullptr);
        m_persistence.importService->setTreeRebuildCallback(nullptr);
        m_persistence.importService->setPropertyRefreshCallback(nullptr);
        m_persistence.importService->setDisplayRefreshCallback(nullptr);
    }

    // 同理：FileDropHandler 由 WorkbenchWindow 持有（跨切换永生），它的
    // 屏幕→世界换算回调按值捕获了 RenderViewport2D*。装的是 application 级事件
    // 过滤器，所以切到 3D 之后往窗口拖一张图片同样会走进来 → 解引用已析构的视口。
    if (m_workbenchWindow)
    {
        if (auto* fdh = m_workbenchWindow->fileDropHandler())
        {
            fdh->setScreenToWorldConverter(nullptr);
        }
    }

    // 右键菜单构建器与属性面板管理器持有下面命令中枢的裸指针，必须先于中枢回收，
    // 避免切台后挂起回调解引用悬空指针。
    if (m_propertiesManager)
    {
        // 取消属性面板节流定时器：切台后 propertiesDock 已销毁，挂起的回调不能再跑
        m_propertiesManager->shutdown();
    }
    m_propertiesManager.reset();
    m_contextMenuBuilder.reset();

    // 清理命令动作中枢（unique_ptr 管理生命周期，reset 释放所有权）
    m_commandHub.reset();

    // 视口动作中枢：断开当前视口，避免切换工作台后悬空指针
    if (m_view.viewportActionHub)
    {
        m_view.viewportActionHub->clearViewport();
    }

    // 工具栏工厂内部持有本次窗口的 QToolBar 裸指针，随窗口清理一起失效，
    // shutdown 统一置空并断开 metadata 连线
    if (m_toolbarFactory)
    {
        m_toolbarFactory->shutdown();
    }
    // 视口指针随窗口清理而失效
    m_viewport = nullptr;
    if (m_sceneTreeManager)
    {
        m_sceneTreeManager->shutdown();
    }
    if (m_shortcutManager)
    {
        m_shortcutManager->unregisterGlobalShortcuts();
    }

    // 场景监视器每次 attachToWindow 都会 new 一个（setupViewportServices），
    // 若不在此销毁，往返切换 N 次后会有 N 个监视器同时 watch 同一场景，
    // 一次场景变化触发 N 次 refreshCommandUiState（幂等但白做功，且随使用时长线性增长）。
    if (m_sceneMonitor)
    {
        m_sceneMonitor->deleteLater();
        m_sceneMonitor = nullptr;
    }

    // 命令 UI 连线的生命周期句柄。上面销毁监视器只断得掉 sceneChanged 那一路；
    // bus / layerBridge 是 UiServices 的成员，跨工作台长寿，接收方本对象同样长寿，
    // 两端都不死的连线 Qt 永远不会自动回收——必须显式销毁句柄整批断开。
    if (m_uiStateConnections)
    {
        m_uiStateConnections->deleteLater();
        m_uiStateConnections = nullptr;
    }
}

void Workbench2D::shutdown()
{
    deactivate();
    m_uiState = {};
    m_commands = {};
    m_scene = {};
    m_persistence = {};
    m_view = {};
}

void Workbench2D::releaseCentralWidgetGLResources(QWidget* centralWidget) const
{
    if (auto* vp = qobject_cast<RenderViewport2D*>(centralWidget))
    {
        vp->releaseGLResources();
    }
}

