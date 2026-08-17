#include "UiWorkbench.h"

#include <QAction>
#include <QActionGroup>
#include <QDockWidget>
#include <QShortcut>
#include <QSizePolicy>
#include <QTextEdit>
#include <QToolBar>
#include <QWidget>
#include <QEvent>
#include <QKeyEvent>
#include <QTimer>
#include <QStatusBar>

#include "Composition/ApplicationCompositionRoot.h"
#include "SceneDocument2D.h"
#include "UiServices.h"
#include "UiStateCenter.h"
#include "UI/Services/ViewportActionHub.h"
#include "UI/Services/ISelectionService.h"
#include "Engine2D/Edit/IUndoRedoManager.h"
#include "UiSceneTreePanel2D.h"
#include "SceneTreeModel2D.h"
#include "SceneTreeBuilder2D.h"
#include "Engine2D/Core/SceneManager.h"
#include "UiPropertiesPanel.h"
#include "RenderViewport2D.h"
#include "UI/TestView/TestViewWindow.h"
#include "FileDropHandler.h"

#include <optional>
#include "DrawToolBarWidget.h"
#include "DrawToolSwitchRegistry.h"
#include "WorkbenchWindow.h"
#include "WorkbenchMenuManager.h"
#include "RenderWidget.h"

#include "UI2D/Operation/CommandActionHub.h"
#include "UI2D/Operation/OperationBus.h"
#include "UI2D/Operation/OperationId.h"
#include "UI2D/Operation/OperationRouting.h"
#include "UI2D/Operation/CommandCatalog.h"
#include "UI2D/Edit/QtLayerManagerBridge.h"

#include "UI/RightToolBar/RightToolBar.h"
#include "UI/Service/ToolBarContextManager.h"
#include "UI/TopToolBar/TopToolBar.h"
#include "UI/TopToolBar/TextFontToolBar.h"
#include "UI/DrawTools/TextEditTool.h"
#include "UI/DrawTools/ToolManager.h"
#include "UI/UiMetrics.h"
#include "UI/Dlg/LayerManagerDialog.h"
#include "UI2D/Service/EntityPropertyModel2D.h"
#include "UI2D/Service/EntityPropertyEditSession2D.h"
#include "UI2D/Service/SceneMonitor.h"

#include "Engine2D/Edit/LayerEditService.h"
#include "Engine2D/Edit/SceneEditService.h"
#include "Engine2D/Interaction/LayerManager.h"
#include "Engine2D/Core/SceneManager.h"
#include "Engine/EntityIdUtils.h"
#include "Engine/SyEntity/SyEntity.h"
#include <string>
#include <vector>
#include <functional>
#include "UI2D/Operation/CommandCatalog.h"
#include "UI2D/Operation/OperationId.h"
#include "UI2D/Operation/OperationBus.h"
#include "UiStateCenter.h"

#include "Import/ImportService.h"
#include "Color/Color.hpp"
#include "Log/SyLogger.h"
#include "UI/StatusBar/StatusBar.h"

#include "UI/Settings/SettingsService.h"
#include "UI2D/Settings/SettingsUiCoordinator2D.h"

#if BUILD_UI3D
    #include "UiEntities.h"
    #include "UiViewport3D.h"
    #include "SceneBuilder3D.h"

    // 3D 服务与操作头文件（从 UiWorkbench.h 前向声明下沉）
    #include "UI/TopToolBar/TopToolBar3D.h"
    #include "UI/StatusBar/StatusBar3D.h"
    #include "UI/Settings/SettingsService.h"
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
}  // namespace

namespace
{
    /// ISelectionService::visitSelectedIds 的计数 visitor（POD 安全）
    void countSelectedIds(const char* /*id*/, void* context)
    {
        if (context)
        {
            ++*static_cast<int*>(context);
        }
    }

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
        // 工具/输入状态（工作台切换时恢复）
        snapshot.activeToolId = snap.activeToolId;
        snapshot.inputFocusWidget = snap.inputFocusWidget;
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
        // 工具/输入状态恢复（仅写入状态中心，子类 activate() 负责应用到视口）
        m_services.stateCenter->setActiveToolId(snapshot.activeToolId);
        m_services.stateCenter->setInputFocusWidget(snapshot.inputFocusWidget);
    }
}

// ==================== 框架层委托接口（默认实现） ====================

bool UiWorkbench::isCommandRegistered(const QString& /*commandId*/) const
{
    return false;
}

void UiWorkbench::dispatchCommand(const QString& /*commandId*/)
{
    SY_WARN("[UiWorkbench] Command dispatch requested without a workbench adapter");
}

QString UiWorkbench::commandText(const QString& /*commandId*/) const
{
    return QString();
}

void UiWorkbench::releaseCentralWidgetGLResources(QWidget* /*centralWidget*/) const
{
    // 默认不释放任何资源，子类按需重写
}

bool UiWorkbench::requiresSkeletonDocks() const
{
    return true;
}

bool UiWorkbench::managesOwnMenus() const
{
    return false;
}

bool UiWorkbench::showSettingsDialog(QWidget* /*parent*/)
{
    return false;
}

// 场景树场景观察者：捕获绕过操作总线的直接编辑（如视口 Delete 键删除），
// 只关心场景变化，由 Workbench2D 依据图元数量变化判断是否需要重建树。
class SceneTreeSceneObserver2D final : public Eg::SceneNotifier::IObserver
{
public:
    using Callback = std::function<void()>;

    explicit SceneTreeSceneObserver2D(Callback cb)
        : m_cb(std::move(cb))
    {
    }

    void onSceneChanged() override
    {
        if (m_cb)
        {
            m_cb();
        }
    }

private:
    Callback m_cb;
};

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
    return CommandCatalog::operationForCommandId(commandId) != OperationId::None;
}

void Workbench2D::dispatchCommand(const QString& commandId)
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
        SY_INFOF("[Workbench2D] Dispatch command='%s' menuId=%d source=Menu",
            qPrintable(commandId),
            static_cast<int>(menuId));
        OperationRouting::dispatch(menuId);
        return;
    }

    // 无法精确定位菜单项（工具名、无目录条目的命令）时回退到 OperationId 直接分发
    const OperationId operation = CommandCatalog::operationForCommandId(commandId);
    if (operation == OperationId::None)
    {
        // 绘图工具菜单项以 toolName（如 "LineTool"）作为命令 ID 分发，
        // 与左侧工具栏 DrawToolBarWidget 保持一致：用 operationForToolName 解析到已注册的 Tool_* 操作，
        // 经 OperationBus 激活视口对应工具，从而与工具栏形成同一条分发/联动路径。
        const OperationId toolOperation = CommandCatalog::operationForToolName(commandId);
        if (toolOperation == OperationId::None)
        {
            SY_WARNF("[Workbench2D] Unknown command: %s", qPrintable(commandId));
            return;
        }
        SY_INFOF("[Workbench2D] Dispatch tool command='%s'", qPrintable(commandId));
        m_services.operationBus->run(toolOperation, {}, OperationSource::Menu);
        return;
    }

    SY_INFOF("[Workbench2D] Dispatch command='%s'", qPrintable(commandId));
    m_services.operationBus->run(operation, {}, OperationSource::Menu);
}

QString Workbench2D::commandText(const QString& commandId) const
{
    const auto operation = CommandCatalog::operationForCommandId(commandId);
    const auto* entry = CommandCatalog::findByOperation(operation);
    return entry && entry->text ? QString::fromUtf8(entry->text) : QString();
}

Workbench2D::Workbench2D() = default;
Workbench2D::~Workbench2D() = default;

QString Workbench2D::displayName() const
{
    return QObject::tr("2D Workbench");
}

bool Workbench2D::initialize(const UiServices& services)
{
    if (!services.stateCenter || !services.interactionDispatcher)
    {
        return false;
    }
    m_services = services;

    // 阶段1收口：SelectionService 已由组合根创建并经 UiServices.selectionService 注入，
    // 工作台不再直接触碰底层 SceneManager

    m_initialState = WorkbenchStateSnapshot{};
    m_savedState = WorkbenchStateSnapshot{};

    // 使用应用共享 SettingsService singleton（2D/3D 逻辑一致）
    m_settingsCoordinator = std::make_unique<SettingsUiCoordinator2D>(ApplicationCompositionRoot::getSettingsService());
    m_services.settingsService = ApplicationCompositionRoot::getSettingsService();

    // 注册 2D 专属设置表，确保保存时表已存在
    if (m_settingsCoordinator)
    {
        m_settingsCoordinator->init();
    }

    return true;
}

void Workbench2D::attachToWindow(WorkbenchWindow& window)
{
    m_workbenchWindow = &window;

    auto* viewport = createCentralViewport(window, nullptr);
    if (!viewport)
    {
        return;
    }

    window.setCentralWidget(viewport);

    auto* vp = qobject_cast<RenderViewport2D*>(viewport);
    if (vp)
    {
        m_viewport = vp;
        setupViewportServices(vp, window);
        vp->initializeTools();
        setupImportCallbacks(vp, window);
        vp->setActiveTool(QStringLiteral("SelectTool"));
    }

    // 属性被编辑后（已入撤销栈）延迟重建模型，避免在内联编辑器提交过程中重入
    if (auto* props = window.propertiesDock())
    {
        QObject::connect(props, &PropertiesPanelWidget::sigPropertyEdited, this, [this]() {
            QTimer::singleShot(0, this, [this]() {
                refreshPropertiesPanel();
            });
        });
    }

    // 视口动作中枢：注入当前视口，供菜单 Zoom 子菜单与右键菜单 View_* 操作统一分发
    if (m_services.viewportActionHub)
    {
        m_services.viewportActionHub->setViewport(m_viewport);
        window.setViewportZoomHandler([hub = m_services.viewportActionHub](const QString& action) {
            if (hub)
            {
                hub->handle(action);
            }
        });
    }

    createToolbars(window);
    setupSceneTree(window);

    // 启动时从数据库加载并应用已保存的 2D 专属设置（画布/网格/标尺/捕捉）
    if (m_settingsCoordinator && m_viewport)
    {
        m_settingsCoordinator->loadAndApplySettings(
            m_viewport->renderWidget(), m_viewport->gridSnapManager(), m_services.unitManager);
    }

    // 创建 2D 状态栏 widget 并挂载到窗口
    // StatusBar 封装了坐标/选择/消息/状态信息的显示，与 3D StatusBar3D 完全独立
    if (!m_statusBar2D)
    {
        m_statusBar2D = new StatusBar(&window);
        // Position 标签弹出单位选择 → 复用与视图菜单完全相同的命令分发路径
        m_statusBar2D->setUnitManager(m_services.unitManager);
        connect(m_statusBar2D, &StatusBar::sigUnitCommandRequested, this, &Workbench2D::dispatchCommand);
        SY_INFO("[Workbench2D] StatusBar created");
    }
    window.mountStatusBar(m_statusBar2D);
}

bool Workbench2D::showSettingsDialog(QWidget* /*parent*/)
{
    if (!m_settingsCoordinator || !m_viewport)
    {
        return false;
    }

    // 2D 捕捉设置由视口持有的 GridSnapManager 驱动（coordinator 已做空指针保护）
    RenderWidget* widget = m_viewport->renderWidget();
    return m_settingsCoordinator->showSettingsDialog(widget, m_viewport->gridSnapManager(), m_services.unitManager);
}

QWidget* Workbench2D::createCentralViewport(WorkbenchWindow& window, PropertiesPanelWidget* properties)
{
    Q_UNUSED(window);
    Q_UNUSED(properties);
    auto* viewport = new RenderViewport2D();
    return viewport;
}

void Workbench2D::setupViewportServices(RenderViewport2D* vp, WorkbenchWindow& window)
{
    Q_UNUSED(window);
    vp->setSelectionService(m_services.selectionService);
    vp->setInteractionDispatcher(m_services.interactionDispatcher);
    vp->setOperationBus(m_services.operationBus);
    vp->setLayerManager(m_services.layerManager);

    // TestView：打开独立“仅显示”预览窗口，展示当前 2D 场景全部图元
    window.setTestViewHandler([this, &window]() {
        if (!m_services.sceneEditService)
        {
            return;
        }
        auto* scene = m_services.sceneEditService->sceneManager();
        if (!scene)
        {
            return;
        }
        auto* w = new TestViewWindow(scene, true, &window);
        w->setAttribute(Qt::WA_DeleteOnClose);
        w->show();
    });

    // P1: 视口通过信号通知上层，不直接持有编辑服务
    if (m_services.sceneEditService)
    {
        QObject::connect(
            vp, &RenderViewport2D::entitySubmitRequested, [service = m_services.sceneEditService](Eg::SyEntity* e) {
                if (e)
                {
                    service->addEntityFromPointer(e, "Draw");
                }
            });
        QObject::connect(
            vp, &RenderViewport2D::nudgeRequested, [service = m_services.sceneEditService](double dx, double dy) {
                service->nudgeSelected(dx, dy, "Move endpoint");
            });

        // 场景变更监控：捕获拖拽/交互式修改等非操作总线路径的场景变更 → 刷新属性面板
        // 拖拽完成后 commitInteractive → notifySceneChanged → SceneMonitor::sceneChanged → 刷新面板
        if (auto* scene = m_services.sceneEditService->sceneManager())
        {
            m_sceneMonitor = new SceneMonitor(this);
            m_sceneMonitor->watch(scene);
            QObject::connect(m_sceneMonitor, &SceneMonitor::sceneChanged, this, [this]() {
                QTimer::singleShot(0, this, [this]() {
                    refreshPropertiesPanel();
                });
            });
        }
    }

    vp->setDocument(m_services.document2D);

    // 状态回调：将视口状态写入状态中心
    if (m_services.stateCenter)
    {
        vp->setStatusCallback([stateCenter = m_services.stateCenter](const QString& text) {
            stateCenter->setStatusPrompt(text);
        });

        vp->setSelectionCallback([this](const QString& selText, const QString& selType) {
            if (m_services.stateCenter)
            {
                m_services.stateCenter->setSelectionContext(selType, selText);
            }
        });

        // 任何选择变化（点选/框选/绘制后自动选中/撤销等）→ 刷新顶部工具栏动作启用状态
        QObject::connect(vp, &RenderViewport2D::selectionChanged, this, [this]() {
            if (m_commandHub)
            {
                m_commandHub->refreshActionStates(m_commandHub->captureSnapshot(m_commandHub->mainWindow()));
            }
        });

        // 选择变化 → 同步刷新属性面板（仅当面板存在时生效，UI 可定制/移除）
        QObject::connect(vp, &RenderViewport2D::selectionChanged, this, [this]() {
            refreshPropertiesPanel();
        });

        // 选择变化 → 把真实选中数量推给状态栏选择指示器（StatusBarBase 容器内的独立指示器）
        QObject::connect(vp, &RenderViewport2D::selectionChanged, this, [this]() {
            if (m_statusBar2D)
            {
                int n = 0;
                if (m_services.sceneEditService)
                {
                    if (auto* scene = m_services.sceneEditService->sceneManager())
                    {
                        n = static_cast<int>(scene->getSelectedEntities().size());
                    }
                }
                m_statusBar2D->setSelectionInfo(n, tr("Selected: %1").arg(n));
            }
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

void Workbench2D::setupImportCallbacks(RenderViewport2D* vp, WorkbenchWindow& window)
{
    if (!m_services.importService)
    {
        return;
    }

    // 拖放导入时，把图片/位图放到鼠标松开的全局坐标处（世界坐标）
    if (auto* fdh = window.fileDropHandler())
    {
        fdh->setScreenToWorldConverter([vp](const QPoint& globalPos) -> std::optional<QPointF> {
            if (!vp)
            {
                return std::nullopt;
            }
            return vp->mapGlobalToScene(globalPos);
        });
    }

    // 导入后自动 zoomToFit
    m_services.importService->setViewportFitCallback([vp]() {
        QTimer::singleShot(0, vp, [vp]() {
            vp->zoomToFit();
        });
    });

    // 场景树刷新（导入后重建树结构）
    m_services.importService->setTreeRebuildCallback([this]() {
        refreshSceneTree();
    });

    // 属性面板刷新（导入后更新属性显示）
    m_services.importService->setPropertyRefreshCallback([this]() {
        refreshPropertiesPanel();
    });
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
    // 绘图工具内容控件（承载样式无关：无论 Dock 还是 Toolbar 复用同一内容）
    auto* drawWidget = new DrawToolBarWidget(&window);
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
            tool.iconResource = QString::fromUtf8(entry.iconResource ? entry.iconResource : "");
            drawTools.append(tool);
        }
    }
    drawWidget->setToolDefinitions(drawTools);

    // 依据承载样式创建左侧面板（Draw Tools）
    if (m_panelHostStyle == PanelHostStyle::Dock)
    {
        window.registerDockWidget(QObject::tr("Draw Tools"), drawWidget, Qt::LeftDockWidgetArea);
    }
    else
    {
        auto* leftToolBar = new QToolBar(QObject::tr("Draw Tools"), &window);
        leftToolBar->setObjectName(QStringLiteral("DrawToolBar"));
        leftToolBar->setMovable(false);
        window.addToolBar(Qt::LeftToolBarArea, leftToolBar);
        leftToolBar->addWidget(drawWidget);
    }

    // 接通「工具栏 → OperationBus → 视口」：把工具栏展示的 Tool_* 操作注册到操作总线，
    // 点击按钮后经总线转发表驱动的 LambdaOperation 激活视口对应工具。
    // 必须在视口（m_viewport）创建完成后再注册，故放在这里而非组合根。
    DrawToolSwitchRegistry(m_services.operationBus, &m_viewport).registerAll();

    // 双向状态同步：视口切换工具后高亮对应按钮；启动时初始化为当前活动工具（SelectTool）。
    QObject::connect(m_viewport, &RenderViewport2D::activeToolChanged, drawWidget, &DrawToolBarWidget::updateActiveTool);
    drawWidget->updateActiveTool(m_viewport->activeToolName());

    // Draw 菜单与左侧工具栏联动：视口切换工具 → 同步菜单勾选态
    if (auto* menuMgr = window.menuManager())
    {
        QObject::connect(m_viewport,
            &RenderViewport2D::activeToolChanged,
            menuMgr,
            &WorkbenchMenuManager::syncDrawMenuToTool);
    }

    // View → Grid & Snap 菜单的真正生效点：把 stateCenter 元数据映射到网格显隐。
    // 菜单/操作只翻转 metadata(gridVisible)，此处作为唯一消费者同步到视口网格渲染。
    if (m_services.stateCenter && m_viewport)
    {
        const auto applyGridVisibleFromMetadata = [stateCenter = m_services.stateCenter, vp = m_viewport]() {
            if (auto* renderWidget = vp->renderWidget())
            {
                if (auto* env = renderWidget->sceneEnvironment())
                {
                    const bool visible = stateCenter->metadata().value(QStringLiteral("gridVisible")).toBool();
                    env->setGridVisible(visible);
                    env->notifyChanged();
                }
            }
        };
        QObject::connect(m_services.stateCenter, &UiStateCenter::metadataChanged, this, applyGridVisibleFromMetadata);
        applyGridVisibleFromMetadata();
    }

    // CommandActionHub：管理所有 QAction 的创建与绑定
    m_commandHub = std::make_unique<CommandActionHub>();
    m_commandHub->setMainWindow(&window);
    // 注入选择计数提供器：Hub 不依赖 ISelectionService，保持界面层与服务层解耦
    m_commandHub->setSelectionCountProvider([selectionService = m_services.selectionService]() -> int {
        if (!selectionService)
        {
            return 0;
        }
        int count = 0;
        selectionService->visitSelectedIds(&countSelectedIds, &count);
        return count;
    });
    // 注入「选中项位于锁定图层」提供器：Delete/Copy/Mirror/Align 等 RequiresUnlocked* 动作据此禁用
    m_commandHub->setSelectionLockedProvider(
        [selectionService = m_services.selectionService,
            layerManager = m_services.layerManager,
            sceneEditService = m_services.sceneEditService]() -> bool {
            if (!selectionService || !layerManager || !sceneEditService)
            {
                return false;
            }
            Eg::SceneManager* scene = sceneEditService->sceneManager();
            if (!scene)
            {
                return false;
            }
            struct LockedCtx
            {
                Eg::SceneManager* scene;
                LayerManager* layers;
                bool locked{ false };
            };
            LockedCtx ctx{ scene, layerManager, false };
            selectionService->visitSelectedIds(
                [](const char* id, void* v) {
                    auto* c = static_cast<LockedCtx*>(v);
                    if (c->locked)
                    {
                        return;
                    }
                    auto eid = Eg::parseEntityId(std::string(id));
                    if (!eid)
                    {
                        return;
                    }
                    if (Eg::SyEntity* entity = c->scene->findEntityById(*eid))
                    {
                        if (c->layers->isLayerLocked(c->layers->getEntityLayer(entity)))
                        {
                            c->locked = true;
                        }
                    }
                },
                &ctx);
            return ctx.locked;
        });
    // 注入分组切换状态提供器：Group/Ungroup 顶部按钮实时可用
    m_commandHub->setGroupToggleProvider(
        [selectionService = m_services.selectionService,
            layerManager = m_services.layerManager,
            sceneEditService = m_services.sceneEditService]() -> GroupToggleState {
            GroupToggleState state;
            if (!selectionService || !layerManager || !sceneEditService)
            {
                return state;
            }
            Eg::SceneManager* scene = sceneEditService->sceneManager();
            if (!scene)
            {
                return state;
            }
            const auto selected = scene->getSelectedEntities();
            if (selected.empty())
            {
                return state;
            }
            bool onLockedLayer = false;
            bool hasGroup = false;
            for (Eg::SyEntity* e : selected)
            {
                if (!e)
                {
                    continue;
                }
                if (layerManager->isLayerLocked(layerManager->getEntityLayer(e)))
                {
                    onLockedLayer = true;
                }
                if (e->group())
                {
                    hasGroup = true;
                }
            }
            state.on = hasGroup;
            state.enabled = !onLockedLayer;
            return state;
        });
    // 注入撤销/重做状态提供器（OperationBus context 在生产路径不填充 undoManager）
    m_commandHub->setUndoRedoProvider([undoManager = m_services.undoManager]() -> UndoRedoState {
        UndoRedoState state;
        if (undoManager)
        {
            state.canUndo = undoManager->canUndo();
            state.canRedo = undoManager->canRedo();
        }
        return state;
    });
    // 注入剪贴板内容提供器（Paste 按钮启用状态，实时反映剪贴板是否已复制图元）
    m_commandHub->setClipboardProvider([clipboard = m_services.clipboard]() -> bool {
        return clipboard && clipboard->hasContent();
    });
    m_commandHub->rebuildAllActions();

    // 顶部工具栏（编辑命令）— 必须先创建，再由 ContextManager 填充 actions
    m_topToolBar = new TopToolBar(&window);
    m_topToolBar->setObjectName(QStringLiteral("TopToolBar"));
    window.addToolBar(Qt::TopToolBarArea, m_topToolBar);

    // 文字编辑字体工具栏（双击文字进入编辑会话时显示字体族/字号/粗斜下划线）
    // 必须先创建，再注册到 ContextManager
    m_textFontToolBar = new QToolBar(QObject::tr("Text Font"), &window);
    m_textFontToolBar->setObjectName(QStringLiteral("TextFontToolBar"));
    m_textFontToolBar->setMovable(false);
    m_textFontToolBar->setIconSize(QSize(UiMetrics::toolbarIconSizeSmall(), UiMetrics::toolbarIconSizeSmall()));
    m_textFontToolBarWidget = new TextFontToolBar(m_textFontToolBar);
    m_textFontToolBar->addWidget(m_textFontToolBarWidget);
    window.addToolBar(Qt::TopToolBarArea, m_textFontToolBar);
    m_textFontToolBar->setVisible(false);

    // 初始化工具栏上下文管理器
    m_contextManager = std::make_unique<ToolBarContextManager>();

    // 注册 Default 上下文（通用编辑命令）
    m_contextManager->registerContext(ToolBarContext::Default, {
        .context = ToolBarContext::Default,
        .title = tr("Edit"),
        .sections = {
            { "", {
                { .actionId = "Edit_Undo", .displayName = tr("Undo"), .iconResource = ":/ui/common/Icons/Actions/undo.svg" },
                { .actionId = "Edit_Redo", .displayName = tr("Redo"), .iconResource = ":/ui/common/Icons/Actions/redo.svg" },
            }},
            { "", {
                { .actionId = "Edit_MirrorHorizontal", .displayName = tr("Mirror H"), .iconResource = ":/ui/common/Icons/Actions/mirror_h.svg" },
                { .actionId = "Edit_MirrorVertical", .displayName = tr("Mirror V"), .iconResource = ":/ui/common/Icons/Actions/mirror_v.svg" },
            }},
            { "", {
                { .actionId = "Edit_AlignLeft", .displayName = tr("Align Left"), .iconResource = ":/ui/common/Icons/Actions/align_left.svg" },
                { .actionId = "Edit_AlignRight", .displayName = tr("Align Right"), .iconResource = ":/ui/common/Icons/Actions/align_right.svg" },
                { .actionId = "Edit_AlignCenterH", .displayName = tr("Align Center H"), .iconResource = ":/ui/common/Icons/Actions/align_center_h.svg" },
                { .actionId = "Edit_AlignTop", .displayName = tr("Align Top"), .iconResource = ":/ui/common/Icons/Actions/align_top.svg" },
                { .actionId = "Edit_AlignBottom", .displayName = tr("Align Bottom"), .iconResource = ":/ui/common/Icons/Actions/align_bottom.svg" },
                { .actionId = "Edit_AlignCenterV", .displayName = tr("Align Center V"), .iconResource = ":/ui/common/Icons/Actions/align_center_v.svg" },
            }},
            { "", {
                { .actionId = "Edit_SelectAll", .displayName = tr("Select All"), .iconResource = ":/ui/common/Icons/Actions/select_all.svg" },
                { .actionId = "Edit_InvertSelection", .displayName = tr("Invert Selection"), .iconResource = ":/ui/common/Icons/Actions/invert_selection.svg" },
                { .actionId = "Edit_Deselect", .displayName = tr("Deselect"), .iconResource = ":/ui/common/Icons/Actions/deselect.svg" },
            }},
            { "", {
                { .actionId = "Edit_Copy", .displayName = tr("Copy"), .iconResource = ":/ui/common/Icons/Actions/copy.svg" },
                { .actionId = "Edit_Paste", .displayName = tr("Paste"), .iconResource = ":/ui/common/Icons/Actions/paste.svg" },
                { .actionId = "Edit_Delete", .displayName = tr("Delete"), .iconResource = ":/ui/common/Icons/Actions/delete.svg" },
            }},
            { "", {
                { .actionId = "Edit_Group", .displayName = tr("Group"), .iconResource = ":/ui/common/Icons/Actions/group.svg", .checkable = true },
            }},
        },
    });

    // 注册 TextEditing 上下文（文字编辑工具栏）
    m_contextManager->registerContext(ToolBarContext::TextEditing, {
        .context = ToolBarContext::TextEditing,
        .title = tr("Text Format"),
        .sections = {
            { "", {
                { .actionId = "Text_FontFamily", .displayName = tr("Font"), .iconResource = ":/ui/common/Icons/Tools/text.svg" },
                { .actionId = "Text_FontSize", .displayName = tr("Size"), .iconResource = ":/ui/common/Icons/Tools/text.svg" },
                { .actionId = "Text_Bold", .displayName = tr("Bold"), .iconResource = ":/ui/common/Icons/Tools/text.svg", .checkable = true },
                { .actionId = "Text_Italic", .displayName = tr("Italic"), .iconResource = ":/ui/common/Icons/Tools/text.svg", .checkable = true },
                { .actionId = "Text_Underline", .displayName = tr("Underline"), .iconResource = ":/ui/common/Icons/Tools/text.svg", .checkable = true },
            }},
        },
    });

    // 注册 QRCodeEditing 上下文（二维码编辑工具栏）
    m_contextManager->registerContext(ToolBarContext::QREditing, {
        .context = ToolBarContext::QREditing,
        .title = tr("QR Code"),
        .sections = {
            { tr("Content"), {
                { .actionId = "QR_Content", .displayName = tr("Content") },
                { .actionId = "QR_ErrorCorrection", .displayName = tr("Error Correction") },
            }},
            { tr("Appearance"), {
                { .actionId = "QR_Size", .displayName = tr("Size") },
                { .actionId = "QR_Foreground", .displayName = tr("Foreground") },
                { .actionId = "QR_Background", .displayName = tr("Background") },
            }},
            { tr("Advanced"), {
                { .actionId = "QR_Logo", .displayName = tr("Logo") },
            }},
        },
    });

    // 注册 BitmapEditing 上下文（位图编辑工具栏）
    m_contextManager->registerContext(ToolBarContext::BitmapEditing, {
        .context = ToolBarContext::BitmapEditing,
        .title = tr("Bitmap"),
        .sections = {
            { tr("Adjust"), {
                { .actionId = "Bitmap_Crop", .displayName = tr("Crop") },
                { .actionId = "Bitmap_Rotate", .displayName = tr("Rotate") },
                { .actionId = "Bitmap_Brightness", .displayName = tr("Brightness") },
                { .actionId = "Bitmap_Contrast", .displayName = tr("Contrast") },
            }},
            { tr("Filter"), {
                { .actionId = "Bitmap_Filter", .displayName = tr("Filter") },
            }},
        },
    });

    // 注册 VectorEditing 上下文（矢量编辑工具栏）
    m_contextManager->registerContext(ToolBarContext::VectorEditing, {
        .context = ToolBarContext::VectorEditing,
        .title = tr("Vector"),
        .sections = {
            { tr("Path"), {
                { .actionId = "Vector_NodeEdit", .displayName = tr("Node Edit") },
                { .actionId = "Vector_Simplify", .displayName = tr("Simplify") },
                { .actionId = "Vector_Boolean", .displayName = tr("Boolean") },
            }},
            { tr("Style"), {
                { .actionId = "Vector_Stroke", .displayName = tr("Stroke") },
                { .actionId = "Vector_Fill", .displayName = tr("Fill") },
            }},
        },
    });

    // 注册 ImageEditing 上下文（图片编辑工具栏）
    m_contextManager->registerContext(ToolBarContext::ImageEditing, {
        .context = ToolBarContext::ImageEditing,
        .title = tr("Image"),
        .sections = {
            { tr("Transform"), {
                { .actionId = "Image_Crop", .displayName = tr("Crop") },
                { .actionId = "Image_Rotate", .displayName = tr("Rotate") },
                { .actionId = "Image_Flip", .displayName = tr("Flip") },
            }},
            { tr("Adjust"), {
                { .actionId = "Image_Brightness", .displayName = tr("Brightness") },
                { .actionId = "Image_Contrast", .displayName = tr("Contrast") },
            }},
            { tr("Filter"), {
                { .actionId = "Image_Filter", .displayName = tr("Filter") },
            }},
        },
    });

    // 注册自定义文字编辑工具栏
    m_contextManager->registerCustomToolBar(ToolBarContext::TextEditing, m_textFontToolBarWidget, false);

    // 绑定 TopToolBar 动作设置/清空回调
    m_contextManager->setTopToolBarActionSetter([this](const QList<ToolBarAction>& actions) {
        if (m_topToolBar) {
            m_topToolBar->setActions(actions);
        }
    });
    m_contextManager->setTopToolBarClearer([this]() {
        if (m_topToolBar) {
            m_topToolBar->clearActions();
        }
    });

    // 监听上下文切换，同步显隐自定义工具栏
    connect(m_contextManager.get(), &ToolBarContextManager::customToolBarVisibilityChanged,
        this, [this](ToolBarContext ctx, bool visible) {
            if (ctx == ToolBarContext::TextEditing && m_textFontToolBar) {
                m_textFontToolBar->setVisible(visible);
            }
        });

    // 默认进入 Default 上下文
    m_contextManager->setCurrentContext(ToolBarContext::Default);

    // 根据选中图元类型自动切换上下文（监听视口选择变化）
    if (m_viewport) {
        connect(m_viewport, &RenderViewport2D::selectionChanged, this, [this]() {
            if (!m_contextManager) return;
            const ToolBarContext newCtx = determineContextFromSelection();
            if (m_contextManager->currentContext() != newCtx) {
                m_contextManager->setCurrentContext(newCtx);
            }
        });
    }

    // 右侧图层面板（颜色/图层），依据承载样式创建
    m_rightToolBar = new RightToolBar(&window);
    m_rightToolBar->setObjectName(QStringLiteral("RightToolBar"));
    if (m_panelHostStyle == PanelHostStyle::Dock)
    {
        window.registerDockWidget(QObject::tr("Layers"), m_rightToolBar, Qt::RightDockWidgetArea);
    }
    else
    {
        window.addToolBar(Qt::RightToolBarArea, m_rightToolBar);
    }

    if (m_services.layerManager)
    {
        m_rightToolBar->setLayerManager(m_services.layerManager, m_services.layerManagerBridge);
    }

    // 单击色块 → 若已选中图元则将其移动到该图层（可撤销），并设为当前图层
    QObject::connect(m_rightToolBar, &RightToolBar::sigLayerSelected, this, [this](int layerId) {
        // 有选中图元时，把选中图元分配到点击的图层（LayerEditService 带 Undo）
        if (m_services.selectionService && m_services.layerEditService)
        {
            std::vector<Eg::EntityId> selectedIds;
            m_services.selectionService->visitSelectedIds(&collectSelectedIds, &selectedIds);
            if (!selectedIds.empty())
            {
                m_services.layerEditService->assignEntitiesToLayer(selectedIds, layerId, "Move selected to layer");
            }
        }
        if (m_services.layerManager)
        {
            m_services.layerManager->setCurrentLayer(layerId);
        }
        // 图层变更后刷新动作状态（撤销/重做可用性、锁定图层禁用态等）
        if (m_commandHub)
        {
            m_commandHub->refreshActionStates(m_commandHub->captureSnapshot(m_commandHub->mainWindow()));
        }
    });

    // 双击色块 → 打开图层管理对话框（其中设置当前图层会通过 sigCurrentLayerChanged 同步回右侧色块）
    QObject::connect(m_rightToolBar, &RightToolBar::sigLayerDoubleClicked, this, [this](int /*layerId*/) {
        if (m_services.layerEditService)
        {
            LayerManagerDialog::showDialog(m_services.layerEditService, m_commandHub ? m_commandHub->mainWindow() : nullptr);
        }
    });

    // 图层锁定/属性变更 → 实时刷新顶部按钮启用状态（如锁定图层后 Delete/Mirror/Align/Group 变不可用）
    if (m_services.layerManagerBridge && m_commandHub)
    {
        QObject::connect(m_services.layerManagerBridge, &QtLayerManagerBridge::sigLayerChanged, this, [this](int) {
            m_commandHub->refreshActionStates(m_commandHub->captureSnapshot(m_commandHub->mainWindow()));
        });
    }

    // 撤销/重做栈状态变化 → 刷新 Undo/Redo 按钮（含本工作台通过 LayerEditService 直接入栈的图层操作）
    if (m_services.operationBus && m_commandHub)
    {
        QObject::connect(m_services.operationBus, &OperationBus::undoStateChanged, this, [this]() {
            m_commandHub->refreshActionStates(m_commandHub->captureSnapshot(m_commandHub->mainWindow()));
        });
        QObject::connect(m_services.operationBus, &OperationBus::undoStateChanged, this, [this]() {
            refreshPropertiesPanel();
        });
        // 剪贴板操作（Copy/Cut）后立即刷新 Paste 按钮启用状态
        QObject::connect(m_services.operationBus, &OperationBus::operationCompleted, this, [this](OperationId id, bool success) {
            if (success && (id == OperationId::Edit_Copy || id == OperationId::Edit_Cut))
            {
                m_commandHub->refreshActionStates(m_commandHub->captureSnapshot(m_commandHub->mainWindow()));
            }
        });
        // 撤销/重做会恢复/移除/重建图元（场景拓扑变化），增量刷新可能遗漏，
        // 强制全量重建视口，确保回退结果实时可见
        QObject::connect(m_services.operationBus, &OperationBus::operationCompleted, this, [this](OperationId id, bool success) {
            if (success && (id == OperationId::Edit_Undo || id == OperationId::Edit_Redo))
            {
                if (m_viewport)
                {
                    m_viewport->requestFullRefresh();
                }
            }
        });
    }
}

ToolBarContext Workbench2D::determineContextFromSelection() const
{
    if (!m_services.selectionService) {
        return ToolBarContext::Default;
    }

    // 获取选中的图元 ID 列表
    std::vector<Eg::EntityId> selectedIds;
    m_services.selectionService->visitSelectedIds(
        [](const char* id, void* ctx) {
            auto& vec = *static_cast<std::vector<Eg::EntityId>*>(ctx);
            if (auto eid = Eg::parseEntityId(std::string(id))) {
                vec.push_back(*eid);
            }
        },
        &selectedIds);

    if (selectedIds.empty()) {
        return ToolBarContext::Default;
    }

    // 获取场景管理器
    auto* scene = m_services.sceneEditService ? m_services.sceneEditService->sceneManager() : nullptr;
    if (!scene) {
        return ToolBarContext::Default;
    }

    // 统计各类型图元数量
    int textCount = 0;
    int qrCount = 0;
    int bitmapCount = 0;
    int vectorCount = 0;
    int imageCount = 0;
    int otherCount = 0;

    for (Eg::EntityId id : selectedIds) {
        if (auto* entity = scene->findEntityById(id)) {
            switch (entity->eType) {
                case Eg::EType::TEXT:
                    textCount++;
                    break;
                case Eg::EType::QR_CODE:
                    qrCount++;
                    break;
                case Eg::EType::IMAGE:
                    bitmapCount++;
                    break;
                case Eg::EType::LINE:
                case Eg::EType::ARC:
                case Eg::EType::CIRCLE:
                case Eg::EType::ELLIPSE:
                case Eg::EType::SMARTLINE:
                case Eg::EType::POLYGON:
                case Eg::EType::SPLINE:
                case Eg::EType::BEZIER:
                case Eg::EType::BEZIER2:
                    vectorCount++;
                    break;
                default:
                    otherCount++;
                    break;
            }
        }
    }

    // 优先级：专用编辑模式 > 通用模式
    // 单一类型优先
    if (textCount > 0 && qrCount == 0 && bitmapCount == 0 && vectorCount == 0 && imageCount == 0) {
        return ToolBarContext::TextEditing;
    }
    if (qrCount > 0 && textCount == 0 && bitmapCount == 0 && vectorCount == 0 && imageCount == 0) {
        return ToolBarContext::QREditing;
    }
    if (bitmapCount > 0 && textCount == 0 && qrCount == 0 && vectorCount == 0 && imageCount == 0) {
        return ToolBarContext::BitmapEditing;
    }
    if (vectorCount > 0 && textCount == 0 && qrCount == 0 && bitmapCount == 0 && imageCount == 0) {
        return ToolBarContext::VectorEditing;
    }
    if (imageCount > 0 && textCount == 0 && qrCount == 0 && bitmapCount == 0 && vectorCount == 0) {
        return ToolBarContext::ImageEditing;
    }

    // 混合类型：优先返回最专用的上下文
    if (textCount > 0) return ToolBarContext::TextEditing;
    if (qrCount > 0) return ToolBarContext::QREditing;
    if (bitmapCount > 0) return ToolBarContext::BitmapEditing;
    if (vectorCount > 0) return ToolBarContext::VectorEditing;
    if (imageCount > 0) return ToolBarContext::ImageEditing;

    return ToolBarContext::Default;
}

void Workbench2D::setupSceneTree(WorkbenchWindow& window)
{
    // 场景树面板是可选的 UI：配置驱动时可能不存在，因此先探测再绑定。
    auto* panel = window.sceneTreeDock();
    if (!panel)
    {
        // 骨架未提供时安全创建并注册（UI 可定制/可缺失）
        panel = new SceneTreePanel2D(&window);
        panel->setObjectName(QStringLiteral("SceneTreeDock"));
        window.registerDockWidget(QObject::tr("Scene"), panel, Qt::LeftDockWidgetArea);
    }
    m_scenePanel2D = panel;

    // 面板（UI）→ 引擎（业务）：用户操作通过算法层写回引擎
    connect(panel, &SceneTreePanel2D::selectionChanged, this, &Workbench2D::applySceneTreeSelection);
    connect(panel, &SceneTreePanel2D::visibilityToggled, this, &Workbench2D::toggleEntityVisibility);
    connect(panel, &SceneTreePanel2D::renameRequested, this, &Workbench2D::renameEntity);
    connect(panel, &SceneTreePanel2D::deleteRequested, this, &Workbench2D::deleteSceneTreeSelection);
    connect(panel, &SceneTreePanel2D::batchVisibilityRequested, this, &Workbench2D::setSceneTreeVisibility);
    connect(panel, &SceneTreePanel2D::batchLockRequested, this, &Workbench2D::setSceneTreeLock);

    // 引擎/场景（业务）→ 面板（UI）：变化后刷新展示与选中高亮
    if (m_viewport)
    {
        connect(m_viewport, &RenderViewport2D::selectionChanged, this, &Workbench2D::syncSceneTreeSelection);
    }
    if (m_services.operationBus)
    {
        connect(m_services.operationBus, &OperationBus::undoStateChanged, this, &Workbench2D::refreshSceneTree);
        connect(m_services.operationBus, &OperationBus::operationCompleted, this,
            [this](OperationId, bool success) {
                if (success)
                {
                    refreshSceneTree();
                }
            });
    }

    // 引擎场景变更兜底：任何直接修改（如视口 Delete 键删除、导入清空等）
    // 都会经 SceneNotifier 通知这里；依据图元数量变化判断是否结构变更，
    // 防抖后重建树，避免 Scene 列表残留。
    Eg::SceneManager* scene = m_services.sceneEditService ? m_services.sceneEditService->sceneManager() : nullptr;
    if (scene)
    {
        m_lastSceneEntityCount = scene->getEntityCount();
        if (!m_sceneTreeRefreshTimer)
        {
            m_sceneTreeRefreshTimer = new QTimer(this);
            m_sceneTreeRefreshTimer->setSingleShot(true);
            m_sceneTreeRefreshTimer->setInterval(150);
            connect(m_sceneTreeRefreshTimer, &QTimer::timeout, this, &Workbench2D::refreshSceneTree);
        }
        if (!m_sceneTreeObserver)
        {
            m_sceneTreeObserver = std::make_unique<SceneTreeSceneObserver2D>([this]() {
                onSceneTreeSceneChanged();
            });
        }
        scene->addObserver(m_sceneTreeObserver.get());
    }

    // 初始填充
    refreshSceneTree();
}

void Workbench2D::onSceneTreeSceneChanged()
{
    Eg::SceneManager* scene = m_services.sceneEditService ? m_services.sceneEditService->sceneManager() : nullptr;
    if (!scene)
    {
        return;
    }
    const std::size_t count = scene->getEntityCount();
    if (count != m_lastSceneEntityCount)
    {
        m_lastSceneEntityCount = count;
        if (m_sceneTreeRefreshTimer)
        {
            m_sceneTreeRefreshTimer->start();
        }
    }
}

void Workbench2D::refreshSceneTree()
{
    if (!m_scenePanel2D)
    {
        return;
    }
    Eg::SceneManager* scene = m_services.sceneEditService ? m_services.sceneEditService->sceneManager() : nullptr;
    // 拓扑：O(N) 一次生成紧凑索引；群组成员与元数据均懒加载
    const SceneTreeTopology2D topology = SceneTreeBuilder2D::buildTopology(scene);
    m_scenePanel2D->setTopology(
        topology,
        [scene, layers = m_services.layerManager](qint64 id, bool isGroup) {
            return SceneTreeBuilder2D::rowMeta(scene, layers, SceneTreeRow2D{ id, isGroup });
        },
        [scene](qint64 groupId) {
            return SceneTreeBuilder2D::groupMembers(scene, groupId);
        });
}

void Workbench2D::syncSceneTreeSelection()
{
    if (!m_scenePanel2D)
    {
        return;
    }
    Eg::SceneManager* scene = m_services.sceneEditService ? m_services.sceneEditService->sceneManager() : nullptr;
    m_scenePanel2D->setSelectedIds(SceneTreeBuilder2D::selectedIds(scene));
}

void Workbench2D::applySceneTreeSelection(const QStringList& ids)
{
    if (!m_services.selectionService)
    {
        return;
    }
    if (ids.isEmpty())
    {
        m_services.selectionService->clear();
        return;
    }

    std::vector<std::string> storage;
    storage.reserve(ids.size());
    for (const QString& id : ids)
    {
        storage.push_back(id.toStdString());
    }
    std::vector<const char*> cids;
    cids.reserve(storage.size());
    for (const std::string& str : storage)
    {
        cids.push_back(str.c_str());
    }
    m_services.selectionService->selectMultiple(cids.data(), cids.size());
}

void Workbench2D::toggleEntityVisibility(const QString& id, bool visible)
{
    Eg::SceneManager* scene = m_services.sceneEditService ? m_services.sceneEditService->sceneManager() : nullptr;
    if (!scene)
    {
        return;
    }
    const auto eid = Eg::parseEntityId(id.toStdString());
    if (!eid)
    {
        return;
    }
    if (auto* entity = scene->findEntityById(*eid))
    {
        entity->setVisible(visible);
        scene->notifySceneChanged();
    }
}

void Workbench2D::renameEntity(const QString& id, const QString& newName)
{
    Eg::SceneManager* scene = m_services.sceneEditService ? m_services.sceneEditService->sceneManager() : nullptr;
    if (!scene || newName.isEmpty())
    {
        return;
    }
    const auto eid = Eg::parseEntityId(id.toStdString());
    if (!eid)
    {
        return;
    }
    if (auto* entity = scene->findEntityById(*eid))
    {
        const QByteArray utf8 = newName.toUtf8();
        entity->setName(utf8.constData());
        scene->notifySceneChanged();
    }
}

void Workbench2D::deleteSceneTreeSelection(const QStringList& ids)
{
    if (ids.isEmpty() || !m_services.sceneEditService)
    {
        return;
    }
    std::vector<Eg::EntityId> eids;
    eids.reserve(ids.size());
    for (const QString& id : ids)
    {
        const auto eid = Eg::parseEntityId(id.toStdString());
        if (eid)
        {
            eids.push_back(*eid);
        }
    }
    if (eids.empty())
    {
        return;
    }
    m_services.sceneEditService->deleteEntities(eids, "Delete from Scene Tree");
    // 删除后刷新树与选择（deleteEntities 为可撤销路径）
    refreshSceneTree();
    syncSceneTreeSelection();
}

void Workbench2D::setSceneTreeVisibility(const QStringList& ids, bool visible)
{
    Eg::SceneManager* scene = m_services.sceneEditService ? m_services.sceneEditService->sceneManager() : nullptr;
    if (!scene || ids.isEmpty())
    {
        return;
    }
    bool changed = false;
    for (const QString& id : ids)
    {
        const auto eid = Eg::parseEntityId(id.toStdString());
        if (!eid)
        {
            continue;
        }
        if (auto* entity = scene->findEntityById(*eid))
        {
            entity->setVisible(visible);
            changed = true;
        }
    }
    if (changed)
    {
        scene->notifySceneChanged();
    }
}

void Workbench2D::setSceneTreeLock(const QStringList& ids, bool locked)
{
    Eg::SceneManager* scene = m_services.sceneEditService ? m_services.sceneEditService->sceneManager() : nullptr;
    if (!scene || ids.isEmpty())
    {
        return;
    }
    bool changed = false;
    for (const QString& id : ids)
    {
        const auto eid = Eg::parseEntityId(id.toStdString());
        if (!eid)
        {
            continue;
        }
        if (auto* entity = scene->findEntityById(*eid))
        {
            entity->setLocked(locked);
            changed = true;
        }
    }
    if (changed)
    {
        scene->notifySceneChanged();
    }
}

void Workbench2D::refreshPropertiesPanel()
{
    // 属性面板是可选的 UI：配置驱动时可能不存在，因此先探测再绑定。
    if (!m_workbenchWindow)
    {
        return;
    }
    auto* props = m_workbenchWindow->propertiesDock();
    if (!props)
    {
        return;
    }

    // 读取当前选中图元 id（数据来源：引擎场景）
    std::vector<Eg::EntityId> entityIds;
    if (m_services.sceneEditService)
    {
        if (auto* scene = m_services.sceneEditService->sceneManager())
        {
            for (Eg::SyEntity* e : scene->getSelectedEntities())
            {
                if (e)
                {
                    entityIds.push_back(e->id);
                }
            }
        }
    }

    // 创建编辑会话（算法层）：持有图元 id，负责按需解析实体、应用修改并集成撤销。
    auto session = std::make_shared<EntityPropertyEditSession2D>(m_services.sceneEditService, std::move(entityIds));

    // 数据/算法产物推送给 UI 层：模型用于展示，会话作为编辑目标。
    // 面板仅消费 PropertyModel / IPropertyEditTarget，不感知算法与引擎细节。
    props->setEditTarget(session);
    props->setPropertyModel(session->buildModel());
}

void Workbench2D::activate()
{
    // 从状态快照恢复（首次激活使用 m_initialState，后续使用 m_savedState）
    const auto& snapshot = m_savedState.viewMode.isEmpty() ? m_initialState : m_savedState;
    restoreFromSnapshot(snapshot);

    // 恢复工具状态：将快照中的工具 ID 应用到视口
    if (m_viewport && !snapshot.activeToolId.isEmpty())
    {
        m_viewport->setActiveTool(snapshot.activeToolId);
        SY_INFOF("[Workbench2D] Restored tool: %s", qPrintable(snapshot.activeToolId));
    }

    // 激活时刷新一次动作状态（撤销/重做可用性 + 选中项驱动的启用态）
    if (m_commandHub)
    {
        m_commandHub->refreshActionStates(m_commandHub->captureSnapshot(m_commandHub->mainWindow()));
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

    // 视口动作中枢：断开当前视口，避免切换工作台后悬空指针
    if (m_services.viewportActionHub)
    {
        m_services.viewportActionHub->clearViewport();
    }

    // 工具栏指针会随窗口清理而失效（clearWorkbenchContent 会 delete QToolBar）
    m_topToolBar = nullptr;
    m_rightToolBar = nullptr;
    m_textFontToolBar = nullptr;
    m_textFontToolBarWidget = nullptr;
    // 视口指针随窗口清理而失效
    m_viewport = nullptr;
    // 注销场景变更观察者并停用防抖定时器，避免切换工作台后悬空
    if (m_sceneTreeObserver && m_services.sceneEditService)
    {
        if (auto* scene = m_services.sceneEditService->sceneManager())
        {
            scene->removeObserver(m_sceneTreeObserver.get());
        }
    }
    m_sceneTreeObserver.reset();
    if (m_sceneTreeRefreshTimer)
    {
        m_sceneTreeRefreshTimer->stop();
        m_sceneTreeRefreshTimer->deleteLater();
        m_sceneTreeRefreshTimer = nullptr;
    }
    m_lastSceneEntityCount = 0;
    // 场景树面板指针随窗口清理而失效
    m_scenePanel2D = nullptr;
}

void Workbench2D::shutdown()
{
    deactivate();
    m_services = UiServices{};
}

void Workbench2D::releaseCentralWidgetGLResources(QWidget* centralWidget) const
{
    if (auto* vp = qobject_cast<RenderViewport2D*>(centralWidget))
    {
        vp->releaseGLResources();
    }
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
    #include "Engine3D/Selection/SelectionManager3D.h"
    #include "Renderer3DFactory.h"
    #include "Log/SyLogger.h"

    #include "SceneTreeBuilder3D.h"
    #include "UiSceneTreePanel3D.h"
    #include "Engine/EntityIdUtils.h"

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

void Workbench3D::dispatchCommand(const QString& commandId)
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

QString Workbench3D::commandText(const QString& commandId) const
{
    const auto operation = CommandCatalog3D::operationForCommandId(commandId);
    const auto* entry = CommandCatalog3D::findByOperation(operation);
    return entry && entry->text ? QString::fromUtf8(entry->text) : QString();
}

// 1 — 初始化，存储服务引用
bool Workbench3D::initialize(const UiServices& services)
{
    if (!services.stateCenter || !services.interactionDispatcher)
    {
        return false;
    }
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
//   setup3DMenuAndShortcuts()      — 创建 CommandActionHub3D / MenuManager3D / 快捷键
void Workbench3D::build3DWorkbenchUi(WorkbenchWindow& window)
{
    window.setSkeletonDocksVisible(false);

    create3DServices();
    setup3DViewportAndSignals(window);
    setupSceneTree3D(window);
    setup3DMenuAndShortcuts(window);
}

/// 步骤一：创建所有 3D 服务，组装 ServicePack3D，注册到 ServiceLocator3D
void Workbench3D::create3DServices()
{
    SY_INFO("[Workbench3D] Creating 3D services...");
    m_serviceOwner = std::unique_ptr<ServiceOwner, ServiceOwnerDeleter>(new ServiceOwner());
    auto& own = *m_serviceOwner;

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
    auto sceneManagerAlias = std::shared_ptr<Eg::SceneManager3D>(m_sceneManager3D, [](Eg::SceneManager3D*) {});
    own.sceneDocumentAdapter->setEngineScene(sceneManagerAlias);
    own.cameraController = std::make_unique<CameraController3D>();
    own.algorithmService = std::make_unique<AlgorithmApplicationService>(nullptr);
    own.settingsService = ApplicationCompositionRoot::getSettingsService();
    own.settingsService->init();
    own.settingsCoordinator = std::make_unique<SettingsUiCoordinator3D>(own.settingsService, own.navigationConfig.get());

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
    m_services3D.shortcutManager = own.shortcutManager.get();
    m_services3D.navigationConfig = own.navigationConfig.get();
    m_services3D.sceneDocument = own.sceneDocument.get();
    m_services3D.cameraController = own.cameraController.get();
    m_services3D.algorithmService = own.algorithmService.get();
    m_services3D.settingsService = own.settingsService;
    m_services3D.settingsCoordinator = own.settingsCoordinator.get();

    #ifdef ENABLE_GEOMODELCORE
    m_services3D.brepModelService = own.brepModelService.get();
    #endif

    ServiceLocator3D::adopt(m_services3D);
    SY_INFO("[Workbench3D] 3D services created and adopted");
}

/// 步骤二：创建 MainWindow3D、Viewport3D 和渲染链，绑定视口信号
void Workbench3D::setup3DViewportAndSignals(WorkbenchWindow& window)
{
    auto& own = *m_serviceOwner;

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
void Workbench3D::create3DViewport(WorkbenchWindow& window)
{
    SY_INFO("[Workbench3D] Creating Viewport3D...");
    auto* viewport = new Viewport3D(&window);
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
    if (auto* r3dAdapter = dynamic_cast<RenderWidget3DAdapter*>(viewport->renderer()))
    {
        m_services3D.renderWidget = r3dAdapter->widget();
        SY_INFOF("[Workbench3D] RenderWidget3D registered to ServicePack3D: %p", m_services3D.renderWidget);
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
    SY_INFO("[Workbench3D] CameraController3D set to Viewport3D");
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

    bind3DCursorSignal();
    bind3DSelectionSignal();
    bind3DDeleteKeySignal();

    if (auto* viewport = qobject_cast<Viewport3D*>(m_mainWindow3D ? m_mainWindow3D->centralWidget() : nullptr))
    {
        viewport->setInputHandler([this](QEvent* event) {
            if (!event || !m_services3D.operationBus)
            {
                return false;
            }

            if (event->type() == QEvent::KeyPress)
            {
                auto* keyEvent = static_cast<QKeyEvent*>(event);
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
    auto* renderWidget = m_services3D.renderWidget;
    connect(renderWidget,
        &RenderWidget3D::sigCursorWorldPosition,
        [stateCenter = m_services.stateCenter](float x, float y, float z, bool valid) {
            if (!stateCenter)
            {
                return;
            }
            QVariantMap meta;
            if (valid)
            {
                meta[QStringLiteral("positionText")] =
                    QObject::tr("Position: (%1, %2, %3) mm").arg(x, 0, 'f', 2).arg(y, 0, 'f', 2).arg(z, 0, 'f', 2);
            }
            else
            {
                meta[QStringLiteral("positionText")] = QObject::tr("Position: -");
            }
            stateCenter->setMetadata(meta);
        });
}

/// 绑定选中变化信号
void Workbench3D::bind3DSelectionSignal()
{
    auto* renderWidget = m_services3D.renderWidget;
    // 跨 DLL 安全：信号参数改为 POD 指针数组
    connect(renderWidget,
        &RenderWidget3D::sigSelectionChanged,
        [stateCenter = m_services.stateCenter](const Eg::SyMeshEntity** entities, int count) {
            if (!stateCenter)
            {
                return;
            }
            QString modelName;
            if (count > 0 && entities && entities[0])
            {
                modelName = QString::number(entities[0]->id);
            }

            QVariantMap meta;
            meta[QStringLiteral("3d_selCount")] = count;
            meta[QStringLiteral("3d_modelName")] = modelName;

            if (count > 0)
            {
                stateCenter->setSelectionContext(
                    QObject::tr("3D-Viewport"), QObject::tr("%1 entities selected").arg(count));
            }
            else
            {
                stateCenter->setSelectionContext(QObject::tr("3D-Viewport"), QStringLiteral("none"));
            }
            stateCenter->setMetadata(meta);
        });
}

/// 绑定 Delete/Backspace 键盘按键信号
void Workbench3D::bind3DDeleteKeySignal()
{
    auto* renderWidget = m_services3D.renderWidget;
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
void Workbench3D::setup3DDeleteShortcuts(WorkbenchWindow& window)
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
    if (auto* fm = qobject_cast<FileMenu3D*>(m_menuManager3D->fileMenu()))
    {
        own.commandActionHub->bindFileMenu(fm);
    }
    if (auto* em = m_menuManager3D->editMenu())
    {
        own.commandActionHub->bindEditMenu(em);
    }
    if (auto* vm = m_menuManager3D->viewMenu())
    {
        own.commandActionHub->bindViewMenu(vm);
    }
        #ifdef ENABLE_GEOMODELCORE
    if (auto* sm = m_menuManager3D->solidMenu())
    {
        own.commandActionHub->bindSolidMenu(sm);
    }
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

    SY_INFO("[Workbench3D] 3D workbench UI build completed");
}

// ==================== 3D 场景树（数据/算法/UI 分离） ====================

void Workbench3D::setupSceneTree3D(WorkbenchWindow& window)
{
    // 3D 场景树面板是可选的 UI：独立创建并注册（骨架的 SceneTreePanel2D 属 2D，
    // 3D 工作台隐藏骨架 dock，因此这里总是创建自己的 SceneTreePanel3D）。
    auto* created = new SceneTreePanel3D(&window);
    created->setObjectName(QStringLiteral("SceneTreeDock3D"));
    auto* sceneDock = window.registerDockWidget(QObject::tr("Scene"), created, Qt::LeftDockWidgetArea);
    // 仅设置初始宽度（窄一点，不挤压 3D 视图），不限制最大宽度，用户可手动拖宽
    if (sceneDock)
    {
        window.resizeDocks({ sceneDock }, { 220 }, Qt::Horizontal);
    }
    m_scenePanel3D = created;

    if (!m_scenePanel3D)
    {
        return;
    }

    // 面板（UI）→ 引擎（业务）：用户操作通过引擎/算法层写回
    connect(m_scenePanel3D, &SceneTreePanel3D::selectionChanged, this, &Workbench3D::applySceneTreeSelection3D);
    connect(m_scenePanel3D, &SceneTreePanel3D::visibilityToggled, this, &Workbench3D::toggleEntityVisibility3D);
    connect(m_scenePanel3D, &SceneTreePanel3D::renameRequested, this, &Workbench3D::renameEntity3D);

    // 引擎/场景（业务）→ 面板（UI）：变化后刷新展示与选中高亮
    if (m_services3D.renderWidget)
    {
        connect(m_services3D.renderWidget, &RenderWidget3D::sigSelectionChanged, this,
            [this](const Eg::SyMeshEntity** /*entities*/, int /*count*/) {
                syncSceneTreeSelection3D();
            });
    }
    if (m_services3D.sceneMonitor)
    {
        connect(m_services3D.sceneMonitor, &SceneMonitor3D::sceneChanged, this, &Workbench3D::refreshSceneTree3D);
    }

    // 3D 导入完成后显式刷新一次树（导入会触发 markDataChanged → SceneMonitor ，
    // 此处 importFinished 作为兜底与显式接入点，二者叠加安全）
    if (m_services.importService)
    {
        connect(m_services.importService, &ImportService::importFinished, this, &Workbench3D::refreshSceneTree3D);
    }

    // 初始填充
    refreshSceneTree3D();
}

void Workbench3D::refreshSceneTree3D()
{
    if (!m_scenePanel3D)
    {
        return;
    }
    m_scenePanel3D->setModel(SceneTreeBuilder3D::build(m_sceneManager3D));
}

void Workbench3D::syncSceneTreeSelection3D()
{
    if (!m_scenePanel3D)
    {
        return;
    }
    m_scenePanel3D->setSelectedIds(SceneTreeBuilder3D::selectedIds(m_sceneManager3D));
}

void Workbench3D::applySceneTreeSelection3D(const QStringList& ids)
{
    if (!m_sceneManager3D || !m_services3D.renderWidget)
    {
        return;
    }
    auto& sel = m_services3D.renderWidget->selectionManager();
    sel.clearSelection();

    for (const QString& id : ids)
    {
        const auto eid = Eg::parseEntityId(id.toStdString());
        if (!eid)
        {
            continue;
        }
        if (auto* mesh = m_sceneManager3D->findMeshById(*eid))
        {
            sel.addSelect(mesh);
        }
    }

    m_services3D.renderWidget->markSceneDirty();
    syncSceneTreeSelection3D();
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
        refreshSceneTree3D();
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
        refreshSceneTree3D();
    }
}

void Workbench3D::onMenuAction(int actionId, const QVariantMap& params)
{
    Q_UNUSED(params);

    const auto menuId = UI3D::fromCommonMenuId(actionId);

    if (!m_services3D.operationBus)
    {
        return;
    }

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
    const auto& snapshot = m_savedState.viewMode.isEmpty() ? m_initialState : m_savedState;
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

    // 场景树面板随窗口销毁，清空引用避免悬空
    m_scenePanel3D = nullptr;

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
    // 配置驱动模式下由 WorkbenchMenuManager 统一构建 2D/3D 菜单（见 setup3DMenuAndShortcuts），
    // 3D 不再自行管理窗口菜单栏；仅旧路径（MenuManager3D）由本工作台自理。
    #ifdef SANYI_ENABLE_CONFIG_DRIVEN_UI
    return false;
    #else
    return true;
    #endif
}

bool Workbench3D::showSettingsDialog(QWidget* /*parent*/)
{
    if (!m_mainWindow3D)
    {
        SY_WARN("[Workbench3D] showSettingsDialog: MainWindow3D is null");
        return false;
    }
    m_mainWindow3D->showSettingsDialog();
    return true;
}
#endif