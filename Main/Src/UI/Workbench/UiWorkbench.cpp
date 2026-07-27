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
#include "UiSceneTreeDock.h"
#include "UiPropertiesPanel.h"
#include "RenderViewport2D.h"
#include "DrawToolBarWidget.h"
#include "WorkbenchWindow.h"

#include "UI2D/Operation/OperationBus.h"
#include "UI2D/Operation/OperationId.h"
#include "UI2D/Operation/CommandCatalog.h"
#include "UI2D/Edit/QtLayerManagerBridge.h"
#include "UI2D/ToolBar/RightToolBar.h"
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
        auto* widget = new QTextEdit(parent);
        widget->setPlainText(text);
        widget->setReadOnly(true);
        return widget;
    }

    /// 添加工作台动作到工具栏
    /// @param bar 工具栏
    /// @param text 动作文本
    QAction* addWorkbenchAction(QToolBar* bar, const QString& text)
    {
        return bar->addAction(text);
    }

    /// 从 CommandCatalog 的 commands 中提取绘图工具定义
    /// 保证工具栏和菜单使用同一套命令 ID（单一命令定义源）
    QVector<DrawToolEntry> buildDrawToolEntries()
    {
        QVector<DrawToolEntry> entries;
        for (const CommandEntry2D& catEntry : CommandCatalog::commands())
        {
            if (!catEntry.toolName)
                continue;
            if (!hasSurface(catEntry.surfaces, CommandSurface2D::LeftToolbar))
                continue;

            DrawToolEntry entry;
            entry.commandId = QString::fromUtf8(catEntry.toolName);
            entry.displayName = QObject::tr(catEntry.text);
            entry.tooltip = QObject::tr("Draw %1").arg(QObject::tr(catEntry.text));
            // 从 shortcutId 中提取快捷键提示
            if (catEntry.shortcutId)
            {
                const QString sid = QString::fromUtf8(catEntry.shortcutId);
                const int dotIdx = sid.lastIndexOf(QLatin1Char('.'));
                entry.shortcut = (dotIdx >= 0) ? sid.mid(dotIdx + 1).toUpper() : sid.toUpper();
            }
            entries.append(entry);
        }
        return entries;
    }

    /// 统一设置工作台初始状态
    void applyWorkbenchState(UiStateCenter* stateCenter,
        const QString& workbenchId,
        const WorkbenchStateSnapshot& snapshot)
    {
        if (!stateCenter)
            return;

        stateCenter->setCurrentWorkbenchId(workbenchId);
        stateCenter->setCurrentViewMode(snapshot.viewMode);
        stateCenter->setCurrentLayerId(snapshot.layerId);
        // 只在快照有真实文档 ID 时才覆盖，避免用占位符覆盖已打开的文件路径
        if (!snapshot.documentId.isEmpty())
            stateCenter->setCurrentDocumentId(snapshot.documentId);

        stateCenter->setDirty(snapshot.dirty);
        stateCenter->setSelectionContext(snapshot.selectionSource, snapshot.selectionText);

        stateCenter->setMetadata({
            { QStringLiteral("workbenchId"), workbenchId },
            { QStringLiteral("documentType"), workbenchId },
            { QStringLiteral("selectionSource"), snapshot.selectionSource },
            { QStringLiteral("selectionText"), snapshot.selectionText },
            { QStringLiteral("selectionType"), snapshot.selectionType },
            { QStringLiteral("commandType"), QStringLiteral("none") },
            { QStringLiteral("viewportType"), snapshot.viewportType },
            { QStringLiteral("commandOwner"), QStringLiteral("none") },
            { QStringLiteral("commandState"), QStringLiteral("idle") },
            { QStringLiteral("viewportStatus"), snapshot.viewportStatus }
            });
    }

#if BUILD_UI3D
    /// 配置 3D 视口
    void configure3DViewport(Viewport3D* viewport, const UiServices& services,
        SceneDocument3D* document, CameraController3D* controller,
        PropertiesPanelWidget* properties, SceneTreeDockWidget* tree)
    {
        if (!viewport)
            return;

        viewport->setSceneDocument(document);
        viewport->setCameraController(controller);

        viewport->setStatusCallback([&services, properties](const QString& status) {
            if (services.stateCenter)
                services.stateCenter->setMetadata({
                    { QStringLiteral("viewportStatus"), status },
                    { QStringLiteral("viewportType"), QStringLiteral("3D") }
                    });
            if (properties)
                properties->setStateText(status);
            });

        viewport->setSelectionCallback([&services, properties, tree](const QString& nodeId) {
            if (properties)
                properties->setSelectionText(QObject::tr("Selected node: %1").arg(nodeId));

            if (services.stateCenter)
                services.stateCenter->setSelectionContext(
                    QObject::tr("3D-Viewport"),
                    QObject::tr("3D node: %1").arg(nodeId));

            if (tree)
                tree->refresh();
            });
    }
#endif

    /// 配置 2D 视口（使用 Renderx 渲染路径）
    void configure2DViewport(RenderViewport2D* viewport, const UiServices& services,
        SceneDocument2D* document, PropertiesPanelWidget* properties,
        SceneEditService* sceneEditService = nullptr)
    {
        if (!viewport)
            return;

        viewport->setDocument(document);
        viewport->setSelectionService(services.selectionService);
        viewport->setInteractionDispatcher(services.interactionDispatcher);
        // 传递操作总线引用给视口
        if (services.operationBus)
            viewport->setOperationBus(services.operationBus);
        // 设置场景编辑服务（工具提交图元时使用）
        if (sceneEditService)
            viewport->setSceneEditService(sceneEditService);

        viewport->setStatusCallback([&services, properties](const QString& status) {
            if (services.stateCenter)
                services.stateCenter->setMetadata({
                    { QStringLiteral("viewportStatus"), status },
                    { QStringLiteral("viewportType"), QStringLiteral("2D") }
                    });
            if (properties)
                properties->setStateText(status);
            });

        viewport->setSelectionCallback([&services](const QString& context, const QString& text) {
            if (services.stateCenter)
            {
                services.stateCenter->setSelectionContext(context, text);
                services.stateCenter->setMetadata({
                    { QStringLiteral("selectionSource"), context },
                    { QStringLiteral("selectionText"), text },
                    { QStringLiteral("selectionType"), QStringLiteral("2D") }
                    });
            }
            });
        viewport->setCommandStageCallback([&services](const QString& stage) {
            if (services.stateCenter)
            {
                services.stateCenter->setCurrentCommandPhase(stage);
                services.stateCenter->setCurrentCommandOwner(QStringLiteral("2D"));
                services.stateCenter->setCurrentCommandType(QStringLiteral("2D"));
                services.stateCenter->setMetadata({
                    { QStringLiteral("commandPhase"), stage },
                    { QStringLiteral("commandOwner"), QStringLiteral("2D") },
                    { QStringLiteral("commandType"), QStringLiteral("2D") }
                    });
            }
            });
    }

    /// 更新 2D 对象详情到属性面板
    void update2DDetails(PropertiesPanelWidget* panel, SceneDocument2D* doc, const QString& selectedId)
    {
        if (!panel || !doc)
            return;

        auto* entity = doc->entityByStringId(selectedId);
        if (!entity || entity->eType != Eg::EType::LINE)
            return;
        auto* line = static_cast<Eg::SyLine*>(entity);
        if (line->vPoints.size() < 2)
            return;

        panel->setObjectDetails(QObject::tr("Line %1").arg(selectedId), {
            QObject::tr("Start: %1,%2").arg(line->vPoints[0].x()).arg(line->vPoints[0].y()),
            QObject::tr("End: %1,%2").arg(line->vPoints[1].x()).arg(line->vPoints[1].y()),
            });
    }

#if BUILD_UI3D
    /// 更新 3D 对象详情到属性面板
    void update3DDetails(PropertiesPanelWidget* panel, SceneDocument3D* doc, const QString& selectedId)
    {
        if (!panel || !doc)
            return;

        auto node = doc->nodeById(selectedId.toStdString());
        if (!node)
            return;

        const auto pathNames = node->pathNamesRecursive();
        QString pathStr;
        for (size_t i = 0; i < pathNames.size(); ++i)
        {
            if (i > 0)
                pathStr += QObject::tr(" / ");
            pathStr += QString::fromStdString(pathNames[i]);
        }

        panel->setObjectDetails(QObject::tr("Node %1").arg(QString::fromStdString(node->id())), {
            QObject::tr("Name: %1").arg(QString::fromStdString(node->name())),
            QObject::tr("Children: %1").arg(node->children().size()),
            QObject::tr("Path: %1").arg(pathStr),
            QObject::tr("Selected: yes")
            });
    }
#endif
}

// UiWorkbench 基类实现

WorkbenchStateSnapshot UiWorkbench::currentSnapshot() const
{
    WorkbenchStateSnapshot snapshot;

    if (m_services.stateCenter)
    {
        const auto state = m_services.stateCenter->snapshot();
        snapshot.viewMode = state.currentViewMode.isEmpty() ? m_initialState.viewMode : state.currentViewMode;
        snapshot.layerId = state.currentLayerId.isEmpty() ? m_initialState.layerId : state.currentLayerId;
        snapshot.documentId = state.currentDocumentId.isEmpty() ? m_initialState.documentId : state.currentDocumentId;
        snapshot.selectionSource = state.currentSelectionSource.isEmpty() ? m_initialState.selectionSource : state.currentSelectionSource;
        snapshot.selectionText = state.currentSelectionText.isEmpty() ? m_initialState.selectionText : state.currentSelectionText;
        snapshot.selectionType = state.currentSelectionType.isEmpty() ? m_initialState.selectionType : state.currentSelectionType;

        const auto& metadata = state.metadata;
        snapshot.viewportType = metadata.contains(QStringLiteral("viewportType"))
            ? metadata.value(QStringLiteral("viewportType")).toString()
            : m_initialState.viewportType;
        snapshot.viewportStatus = metadata.contains(QStringLiteral("viewportStatus"))
            ? metadata.value(QStringLiteral("viewportStatus")).toString()
            : m_initialState.viewportStatus;

        snapshot.dirty = state.dirty;
    }
    else
    {
        snapshot = m_initialState;
    }

    return snapshot;
}

void UiWorkbench::restoreFromSnapshot(const WorkbenchStateSnapshot& snapshot)
{
    m_savedState = snapshot;
}

// Workbench2D 实现
// 统一工作台初始化模板
// 流程：initialize → attachToWindow → activate ↔ deactivate → shutdown
// 2D/3D 差异集中在配置，不散在流程里

QString Workbench2D::id() const
{
    return QStringLiteral("2D");
}
QString Workbench2D::displayName() const
{
    return QObject::tr("2D Workbench");
}

// 步骤1 — 初始化，存储服务引用
bool Workbench2D::initialize(const UiServices& services)
{
    if (!services.stateCenter || !services.interactionDispatcher)
        return false;
    m_services = services;
    return true;
}

// 步骤2 — 附加到窗口，创建 UI 组件
// 子流程：创建文档 → 创建属性面板 → 创建视口 → 绑定回调 → 注册工具栏和 dock → 设置初始状态
void Workbench2D::attachToWindow(WorkbenchWindow& window)
{
    // 步骤2.1: 创建场景 dock
    auto* sceneDock = createLayersDock(window);
    SceneTreeDockWidget* sceneTreeDock = sceneDock;

    // 步骤2.2: 创建绘制工具栏，从 CommandCatalog 注入工具定义
    auto* drawToolBar = new DrawToolBarWidget(&window);
    drawToolBar->setToolDefinitions(buildDrawToolEntries());
    if (m_services.operationBus)
        drawToolBar->setOperationBus(m_services.operationBus);
    window.registerDockWidget(QObject::tr("2D Draw Tools"), drawToolBar, Qt::LeftDockWidgetArea);

    // 步骤2.3: 使用全局共享的 document2D（与 ImportService 共用同一个 SceneManager）
    // 注意：document2D 由 ApplicationCompositionRoot 初始化，
    // 使用全局 SceneManager，确保导入的图元和视口渲染使用同一数据源
    // 从 window 获取最新的 UI 服务，避免切换过程中 m_services 失效
    const auto& windowServices = window.uiServices();
    SceneDocument2D* document = windowServices.document2D;
    if (!document)
    {
        SY_ERROR("[Workbench2D] document2D is null in window.uiServices(), falling back to m_services");
        document = m_services.document2D;
    }
    if (!document)
    {
        SY_ERROR("[Workbench2D] document2D is null in both window.uiServices() and m_services");
        return;
    }
    // 创建选择服务并注入，将选择状态从文档中分离
    m_selectionService = std::make_unique<SelectionService>(document->sceneManager());
    // 初始演示线（如果场景为空，则创建两条示例线）
    QString primaryId;
    QString secondaryId;
    if (document && document->allEntityIdsQ().isEmpty())
    {
        primaryId = document->createLine(QPointF(-120, -80), QPointF(160, 100));
        secondaryId = document->createLine(QPointF(-160, 120), QPointF(100, 180));
        if (m_selectionService && !primaryId.isEmpty())
            m_selectionService->selectEntity(primaryId);
    }

    // 步骤2.4: 创建属性面板
    auto* properties = new PropertiesPanelWidget(&window);
    properties->setWorkbenchMode(PropertiesPanelWidget::WorkbenchMode::TwoD);
    configureWorkbenchPanels(properties);
    window.registerDockWidget(QObject::tr("2D Properties"), properties, Qt::RightDockWidgetArea);

    // 步骤2.5: 创建命令面板
    auto* commandPanel = createPanelWidget(QObject::tr("Command panel"), &window); // 命令面板
    window.registerDockWidget(QObject::tr("2D Command"), commandPanel, Qt::BottomDockWidgetArea); // 2D 命令

    // 步骤2.6: 创建工具栏并绑定操作
    auto* mainBar = window.registerToolBar(QObject::tr("2D Main")); // 2D 主工具栏
    auto* viewBar = window.registerToolBar(QObject::tr("2D View")); // 2D 视图工具栏
    configureWorkbenchActions(mainBar, viewBar);

    // 步骤2.6b: 创建右侧图层色块工具栏，通过 LayerEditService 统一操作入口
    auto* rightToolBar = new RightToolBar(&window);
    if (windowServices.layerManager)
    {
        rightToolBar->setLayerManager(windowServices.layerManager, windowServices.layerManagerBridge);
        // 点击色块切换当前图层 — 走 LayerEditService 统一入口，支持撤销
        // 同时同步到状态中心，确保状态栏和属性面板能实时反映当前图层
        QObject::connect(rightToolBar, &RightToolBar::sigLayerSelected,
            [les = windowServices.layerEditService, sc = windowServices.stateCenter](int layerId) {
                if (les)
                {
                    les->setCurrentLayer(layerId);
                    SY_INFOF("[Workbench2D] Current layer switched to %d via LayerEditService", layerId);
                }
                // 同步图层 ID 到状态中心，作为 UI 单一展示来源
                if (sc)
                {
                    sc->setCurrentLayerId(QString::number(layerId));
                    sc->setMetadata({
                        { QStringLiteral("layerId"), layerId },
                        { QStringLiteral("layerSource"), QStringLiteral("RightToolBar") }
                        });
                }
            });
        // 双击色块打开图层管理对话框
        QObject::connect(rightToolBar, &RightToolBar::sigLayerDoubleClicked,
            [les = windowServices.layerEditService, &window](int layerId) {
                Q_UNUSED(layerId);
                if (les)
                    LayerManagerDialog::showDialog(les, &window);
            });
    }
    window.addToolBar(Qt::RightToolBarArea, rightToolBar);

    // 步骤2.7: 创建视口并设置为中央组件
    window.setCentralWidget(createCentralViewport(window, properties));
    auto* viewport = qobject_cast<RenderViewport2D*>(window.centralWidget());
    const QString currentWorkbenchId = windowServices.stateCenter ? windowServices.stateCenter->currentWorkbenchId() : QStringLiteral("2D");
    // 将缩放菜单操作转发到视口（缩放是全局环境能力，不经过命令系统）
    if (viewport)
    {
        window.setViewportZoomHandler([viewport](const QString& action) {
            if (action == QStringLiteral("zoom_in"))
                viewport->zoomIn();
            else if (action == QStringLiteral("zoom_out"))
                viewport->zoomOut();
            else if (action == QStringLiteral("zoom_fit"))
                viewport->zoomToFit();
            else if (action == QStringLiteral("zoom_selection"))
                viewport->zoomToSelection();
            else if (action == QStringLiteral("reset"))
                viewport->resetView();
            });
    }
    // 导入完成后，强制把 2D 导入内容同步到当前工作台显示链路
    if (windowServices.importService && viewport)
    {
        windowServices.importService->setWorkbenchSwitchCallback([&window, currentWorkbenchId](const QString& workbenchId) {
            if (!currentWorkbenchId.isEmpty() && currentWorkbenchId.compare(workbenchId, Qt::CaseInsensitive) == 0)
                return;
            window.triggerWorkbench(workbenchId);
            });
        windowServices.importService->setViewportFitCallback([viewport]() {
            if (!viewport)
                return;

            std::function<void()> refreshAfterReady;
            refreshAfterReady = [viewport, &refreshAfterReady]() {
                if (!viewport)
                    return;

                const int vpW = viewport->width();
                const int vpH = viewport->height();
                if (vpW <= 0 || vpH <= 0)
                {
                    QTimer::singleShot(16, viewport, refreshAfterReady);
                    return;
                }

                auto* document = viewport->document();
                SY_INFOF("[Workbench2D] Import viewport refresh: viewport=%p document=%p size=%dx%d",
                    viewport, document, vpW, vpH);

                viewport->setDocument(document);
                viewport->resetView();
                QTimer::singleShot(0, viewport, [viewport]() {
                    if (!viewport)
                        return;
                    viewport->zoomToFit();
                    viewport->requestSceneRefresh();
                    });
                };

            QTimer::singleShot(0, viewport, refreshAfterReady);
            });
        windowServices.importService->setTreeRebuildCallback([sceneTreeDock]() {
            if (sceneTreeDock)
                sceneTreeDock->refresh();
            });
        windowServices.importService->setPropertyRefreshCallback([properties]() {
            if (properties)
                properties->refresh();
            });
        // 设置状态栏更新回调
        windowServices.importService->setStatusBarUpdateCallback([&window](const QString& message) {
            // 不使用 showMessage，它会临时覆盖状态栏永久 widget（导致坐标标签被覆盖）
            // 导入状态已通过 stateCenter->statusPrompt -> msgLabel 展示
            SY_INFOF("[Import] Status: %s", message.toUtf8().constData());
            });
    }
    if (properties)
    {
        properties->setSelectionText(QObject::tr("Selected: %1, %2").arg(primaryId, secondaryId)); // 已选: %1, %2
        properties->setObjectDetails(QObject::tr("2D Selection"), { // 2D 选择
            QObject::tr("Primary: %1").arg(primaryId), // 主选: %1
            QObject::tr("Secondary: %1").arg(secondaryId), // 次选: %1
            QObject::tr("Mode: %1").arg(QObject::tr("2D")) // 模式: 2D
            });
    }

    m_initialState.viewMode = QObject::tr("2D Canvas"); // 2D 画布
    m_initialState.layerId = QObject::tr("Default"); // 默认
    m_initialState.documentId.clear(); // 初始无文档，由 ApplicationCompositionRoot 在文件打开时设置
    m_initialState.selectionSource = QObject::tr("2D-Init"); // 2D-初始化
    m_initialState.selectionText = QObject::tr("Selected: %1, %2").arg(primaryId, secondaryId); // 已选: %1, %2
    m_initialState.selectionType = QObject::tr("2D"); // 2D
    m_initialState.viewportType = QObject::tr("2D"); // 2D
    m_initialState.viewportStatus = QObject::tr("2D ready"); // 2D 就绪
    m_initialState.dirty = false;
}

QWidget* Workbench2D::createCentralViewport(WorkbenchWindow& window, PropertiesPanelWidget* properties)
{
    // 使用基于 Renderx 的 2D 渲染视口
    auto* viewport = new RenderViewport2D(&window);
    // 接入命令系统：注入 document/dispatcher/interactionDispatcher/operationBus 和回调
    configure2DViewport(viewport, m_services, m_services.document2D, properties, m_services.sceneEditService);
    // 连接鼠标位置回调到状态栏 posLabel
    viewport->setPositionCallback([&window](double x, double y) {
        window.updatePositionLabel(x, y);
        });
    viewport->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    viewport->setMinimumSize(800, 600);
    // 初始化工具系统
    viewport->initializeTools();
    return viewport;
}

void Workbench2D::configureModernViewport(QWidget* viewport) const
{
    if (!viewport)
        return;
    viewport->setObjectName(QStringLiteral("Modern2DViewport"));
}

void Workbench2D::configureWorkbenchPanels(PropertiesPanelWidget* properties) const
{
    if (!properties)
        return;

    PropertiesPanelWidget::PropertiesData data;
    data.mode = PropertiesPanelWidget::WorkbenchMode::TwoD;
    data.stateText = QObject::tr("2D ready"); // 2D 就绪
    data.documentType = QObject::tr("SceneDocument2D"); // 场景文档2D
    data.documentStatus = QObject::tr("Ready"); // 就绪
    data.modeSpecificFields = {
        QObject::tr("Mode: 2D Canvas"), // 模式: 2D 画布
        QObject::tr("Layers: Default"), // 图层: 默认
        QObject::tr("View: Canvas") // 视图: 画布
    };

    properties->setPropertiesData(data);
}

void Workbench2D::configureWorkbenchActions(QToolBar* mainBar, QToolBar* viewBar) const
{
    if (!mainBar || !viewBar)
        return;

    // 顶部工具栏只放编辑操作（处理已有图形）
    auto* moveEntity = addWorkbenchAction(mainBar, QObject::tr("Move"));
    auto* copyEntity = addWorkbenchAction(mainBar, QObject::tr("Copy"));
    auto* rotateEntity = addWorkbenchAction(mainBar, QObject::tr("Rotate"));
    auto* mirrorEntity = addWorkbenchAction(mainBar, QObject::tr("Mirror"));
    auto* deleteEntity = addWorkbenchAction(mainBar, QObject::tr("Delete"));
    auto* zoomExtents = addWorkbenchAction(viewBar, QObject::tr("Zoom Extents"));
    auto* pan = addWorkbenchAction(viewBar, QObject::tr("Pan"));

    // 使用 OperationBus 绑定操作
    moveEntity->setData(QStringLiteral("2d.move"));
    copyEntity->setData(QStringLiteral("2d.copy"));
    rotateEntity->setData(QStringLiteral("2d.rotate"));
    mirrorEntity->setData(QStringLiteral("2d.mirror"));
    deleteEntity->setData(QStringLiteral("2d.delete"));
    if (m_services.operationBus)
    {
        QObject::connect(moveEntity, &QAction::triggered, [this]() { m_services.operationBus->run(CommandCatalog::operationForCommandId(QStringLiteral("2d.move"))); });
        QObject::connect(copyEntity, &QAction::triggered, [this]() { m_services.operationBus->run(CommandCatalog::operationForCommandId(QStringLiteral("2d.copy"))); });
        QObject::connect(rotateEntity, &QAction::triggered, [this]() { m_services.operationBus->run(CommandCatalog::operationForCommandId(QStringLiteral("2d.rotate"))); });
        QObject::connect(mirrorEntity, &QAction::triggered, [this]() { m_services.operationBus->run(CommandCatalog::operationForCommandId(QStringLiteral("2d.mirror"))); });
        QObject::connect(deleteEntity, &QAction::triggered, [this]() { m_services.operationBus->run(CommandCatalog::operationForCommandId(QStringLiteral("2d.delete"))); });
    }

    // 视图操作仍走 OperationBus（非业务命令）
    if (m_services.operationBus)
    {
        QObject::connect(zoomExtents, &QAction::triggered, [this]() {
            m_services.operationBus->run(OperationId::View_ZoomFit, {}, OperationSource::TopToolbar);
            });
        QObject::connect(pan, &QAction::triggered, [this]() {
            m_services.operationBus->run(OperationId::View_Pan, {}, OperationSource::TopToolbar);
            });
    }
}

SceneTreeDockWidget* Workbench2D::createLayersDock(WorkbenchWindow& window) const
{
    auto* sceneDock = new SceneTreeDockWidget(&window);
    sceneDock->setObjectName(QStringLiteral("SceneTreeDock"));
    window.registerDockWidget(QObject::tr("2D Layers"), sceneDock, Qt::LeftDockWidgetArea);
    return sceneDock;
}

// 步骤3 — 激活工作台，应用状态
// 优先使用上次停用前保存的快照，首次激活使用初始状态
void Workbench2D::activate()
{
    const auto& snapshot = m_savedState.viewMode.isEmpty()
        ? m_initialState
        : m_savedState;
    applyWorkbenchState(m_services.stateCenter, id(), snapshot);
}

// 步骤4 — 停用工作台，保存当前状态
void Workbench2D::deactivate()
{
    // 从状态中心保存当前快照，供下次激活时恢复
    m_savedState = currentSnapshot();
}

// 步骤5 — 关闭工作台，清理资源
void Workbench2D::shutdown()
{
    m_services = UiServices{};
}

#if BUILD_UI3D
// Workbench3D 实现
// 统一工作台初始化模板
// 流程：initialize → attachToWindow → activate ↔ deactivate → shutdown
// 使用 MainWindow3D + ServiceLocator3D 架构
// 渲染链路：Viewport3D -> IRenderer3D -> RenderWidget3DAdapter -> RenderWidget3D

#include "UI3D/Service/ServiceLocator3D.h"
#include "UI3D/Operation/OperationBus3D.h"
#include "UI3D/Operation/CommandCatalog3D.h"
#include "UI3D/Operation/CommandActionHub3D.h"
#include "UI3D/Operation/CommandRegistry3D.h"
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

// 步骤1 — 初始化，存储服务引用并初始化 ServiceLocator3D
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

    SY_INFO("[Workbench3D] Initializing ServiceLocator3D...");
    ServiceLocator3D::initialize(m_sceneManager3D, nullptr, nullptr);
    SY_INFO("[Workbench3D] ServiceLocator3D initialized");

    m_savedState = WorkbenchStateSnapshot{};
    m_initialState = WorkbenchStateSnapshot{};
    return true;
}

// 步骤2 — 构建 3D 工作台 UI
// 核心架构：Viewport3D -> IRenderer3D -> RenderWidget3DAdapter -> RenderWidget3D
// 通过 Viewport3D 统一视图宿主，Renderer 通过外部注入
void Workbench3D::build3DWorkbenchUi(WorkbenchWindow& window)
{
    // ServiceLocator3D 初始化（如果尚未初始化或已关闭，重新初始化）
    // 注：initialize() 内部会判断是否已初始化，防止重复创建
    ServiceLocator3D::initialize(m_sceneManager3D, nullptr, nullptr);

    // 隐藏骨架停靠面板（SceneDock / PropertiesDock），3D 工作台不需要这些面板
    // 这些面板在 initializeWorkbenchShell() 中创建，不在 m_registeredDocks 中，
    // clearWorkbenchContent() 无法清除它们，需要手动隐藏
    window.setSkeletonDocksVisible(false);

    SY_INFO("[Workbench3D] Creating MainWindow3D wrapper...");
    m_mainWindow3D = std::make_unique<MainWindow3D>(&window);
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

            // 通过 ServiceLocator3D 注册 RenderWidget3D，保持向后兼容性
            ServiceLocator3D::instance().setRenderWidget(renderWidget);
            SY_INFO("[Workbench3D] RenderWidget3D registered in ServiceLocator3D");

            // 连接 RenderWidget3D 的信号到状态栏（通过适配器的回调机制）
            connect(renderWidget, &RenderWidget3D::sigCursorWorldPosition,
                [mainWindow3D = m_mainWindow3D.get()](float x, float y, float z, bool valid) {
                    if (mainWindow3D->statusBar3D())
                    {
                        if (valid)
                            mainWindow3D->statusBar3D()->setPositionText(
                                QObject::tr("Position: (%1, %2, %3) mm").arg(x, 0, 'f', 2).arg(y, 0, 'f', 2).arg(z, 0, 'f', 2));
                        else
                            mainWindow3D->statusBar3D()->setPositionText(QObject::tr("Position: -"));
                    }
                });

            connect(renderWidget, &RenderWidget3D::sigSelectionChanged,
                [mainWindow3D = m_mainWindow3D.get()](const std::vector<Eg::SyMeshEntity*>& entities) {
                    if (mainWindow3D->statusBar3D())
                    {
                        int count = static_cast<int>(entities.size());
                        QString modelName;
                        int triangleCount = 0;
                        if (count > 0 && entities[0])
                        {
                            modelName = QString::number(entities[0]->id);
                            triangleCount = static_cast<int>(entities[0]->vertices.size() / 3);
                        }
                        mainWindow3D->statusBar3D()->setSelectionInfo(count, modelName, triangleCount);
                    }
                });

            // 连接 Delete/Backspace 键删除选中对象（渲染 widget 有焦点时生效）
            connect(renderWidget, &RenderWidget3D::sigKeyPressed,
                this, [this](int key, Qt::KeyboardModifiers) {
                    if (key == Qt::Key_Delete || key == Qt::Key_Backspace)
                    {
                        auto* bus = ServiceLocator3D::instance().operationBus();
                        if (bus)
                        {
                            SY_INFO("[Workbench3D] Delete key pressed (renderWidget signal), running Edit_Delete operation");
                            bus->run(OperationId3D::Edit_Delete);
                        }
                    }
                });

            // 全局 Delete/Backspace 快捷键（渲染 widget 无焦点时也生效）
            // 存储到成员变量，以便在 shutdown() 中清理，避免切换到 2D 后仍触发 3D 操作
            m_deleteShortcut = new QShortcut(QKeySequence(Qt::Key_Delete), &window);
            m_deleteShortcut->setContext(Qt::ApplicationShortcut);
            connect(m_deleteShortcut, &QShortcut::activated, this, [this]() {
                auto* bus = ServiceLocator3D::instance().operationBus();
                if (bus)
                {
                    SY_INFO("[Workbench3D] Delete shortcut activated, running Edit_Delete operation");
                    bus->run(OperationId3D::Edit_Delete);
                }
            });
            m_backspaceShortcut = new QShortcut(QKeySequence(Qt::Key_Backspace), &window);
            m_backspaceShortcut->setContext(Qt::ApplicationShortcut);
            connect(m_backspaceShortcut, &QShortcut::activated, this, [this]() {
                auto* bus = ServiceLocator3D::instance().operationBus();
                if (bus)
                {
                    SY_INFO("[Workbench3D] Backspace shortcut activated, running Edit_Delete operation");
                    bus->run(OperationId3D::Edit_Delete);
                }
            });
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

    // 设置 SceneDocument（通过 ServiceLocator3D 获取）
    auto* sceneDocument = ServiceLocator3D::instance().sceneDocument();
    viewport->setSceneDocument(sceneDocument);
    SY_INFOF("[Workbench3D] SceneDocument3D set to Viewport3D: %p", sceneDocument);

    // 设置 CameraController（通过 ServiceLocator3D 获取）
    auto* cameraController = ServiceLocator3D::instance().cameraController();
    viewport->setCameraController(cameraController);
    SY_INFOF("[Workbench3D] CameraController3D set to Viewport3D: %p", cameraController);

    // 将 Viewport3D 设为中央组件，并设置最小尺寸避免被工具栏挤压到不可见
    viewport->setMinimumSize(400, 300);
    window.setCentralWidget(viewport);
    SY_INFO("[Workbench3D] Viewport3D set as central widget");

    // ========== 工具栏和状态栏设置 ==========
    auto* mainWindow3D = m_mainWindow3D.get();

    // 把 MainWindow3D 创建的左侧工具栏提取到 WorkbenchWindow
    window.addToolBar(Qt::LeftToolBarArea, mainWindow3D->leftToolBar());

    // 把顶部工具栏添加到 WorkbenchWindow
    if (mainWindow3D->topToolBar())
        window.addToolBar(Qt::TopToolBarArea, mainWindow3D->topToolBar());

    // 使用 MainWindow3D 的状态栏
    if (mainWindow3D->statusBar3D())
    {
        auto* sb = window.statusBar();
        if (sb)
        {
            sb->clearMessage();
            sb->addWidget(mainWindow3D->statusBar3D(), 1);
        }
    }

    // MainWindow3D 本身不需要可见（它只是工具栏的宿主和协调器）
    mainWindow3D->hide();

    SY_INFO("[Workbench3D] Calling ServiceLocator3D::initializeDeferred()...");
    ServiceLocator3D::instance().initializeDeferred(mainWindow3D);
    SY_INFO("[Workbench3D] ServiceLocator3D::initializeDeferred() completed");

    SY_INFO("[Workbench3D] Calling CommandRegistry3D::registerAll()...");
    CommandRegistry3D::registerAll(mainWindow3D);
    SY_INFO("[Workbench3D] CommandRegistry3D::registerAll() completed");

    auto* menuBar = window.menuBar();
    if (!menuBar)
    {
        menuBar = new QMenuBar(&window);
        window.setMenuBar(menuBar);
    }

    SY_INFO("[Workbench3D] Creating MenuManager3D...");
    m_menuManager3D = std::make_unique<MenuManager3D>(mainWindow3D);
    SY_INFO("[Workbench3D] MenuManager3D created");

    m_menuManager3D->createMenus(menuBar);

    auto* fileMenu = static_cast<FileMenu3D*>(m_menuManager3D->fileMenu());
    auto* editMenu = m_menuManager3D->editMenu();
    auto* viewMenu = m_menuManager3D->viewMenu();

    auto* hub = ServiceLocator3D::instance().commandActionHub();
    if (hub)
    {
        // 注入 OperationBus，使 wireAction 能够正确连接 QAction 信号
        hub->setOperationBus(ServiceLocator3D::instance().operationBus());

        SY_INFO("[Workbench3D] Calling CommandActionHub3D::rebuildAll()...");
        hub->rebuildAll(mainWindow3D->shortcutManager3D());
        SY_INFO("[Workbench3D] CommandActionHub3D::rebuildAll() completed");

        hub->bindFileMenu(fileMenu);
        hub->bindEditMenu(editMenu);
        hub->bindViewMenu(viewMenu);
        hub->bindLeftToolBar(mainWindow3D->leftToolBar());
        hub->bindTopToolBar(mainWindow3D->topToolBar());
    }
    else
    {
        SY_ERROR("[Workbench3D] CommandActionHub3D is null, menu binding skipped");
    }

    m_menuManager3D->connectMenuSignals();

    connect(m_menuManager3D.get(), &MenuManager3D::sigMenuAction,
        this, &Workbench3D::onMenuAction);

    // 设置 Viewport3D 的状态回调和选择回调（通过统一接口）
    viewport->setStatusCallback([mainWindow3D](const QString& status) {
        SY_INFOF("[Viewport3D] Status callback: %s", status.toUtf8().constData());
        if (mainWindow3D->statusBar3D())
        {
            mainWindow3D->statusBar3D()->setMessageText(status);
        }
    });

    viewport->setSelectionCallback([mainWindow3D](const QString& nodeId) {
        SY_INFOF("[Viewport3D] Selection callback: nodeId=%s", nodeId.toUtf8().constData());
        if (!nodeId.isEmpty())
        {
            mainWindow3D->statusBar3D()->setSelectionInfo(1, nodeId, 0);
        }
        else
        {
            mainWindow3D->statusBar3D()->setSelectionInfo(0, QString(), 0);
        }
    });

    m_initialState.viewMode = QObject::tr("3D Viewport");
    m_initialState.layerId = QObject::tr("Scene");
    m_initialState.documentId.clear();
    m_initialState.selectionSource = QObject::tr("3D-Init");
    m_initialState.selectionText = QObject::tr("Ready");
    m_initialState.selectionType = QObject::tr("3D");
    m_initialState.viewportType = QObject::tr("3D");
    m_initialState.viewportStatus = QObject::tr("3D ready");
    m_initialState.dirty = false;
}

void Workbench3D::onMenuAction(int actionId, const QVariantMap& params)
{
    SY_INFOF("[Workbench3D] Menu action triggered: id=%d (0x%x)", actionId, actionId);

    auto* bus = ServiceLocator3D::instance().operationBus();
    if (!bus)
    {
        SY_ERROR("[Workbench3D] OperationBus3D is null");
        return;
    }

    SY_INFOF("[Workbench3D] OperationBus3D=%p, registry size=%zu", bus, bus->registrySize());

    UI3D::MenuActionId3D menuId = static_cast<UI3D::MenuActionId3D>(actionId);
    if (CommandCatalog3D::mapsToOperation(menuId))
    {
        OperationId3D opId = CommandCatalog3D::operationForMenu(menuId);
        SY_INFOF("[Workbench3D] Running operation: menuId=%d (0x%x), opId=%d, hasOp=%d",
            actionId, actionId, static_cast<int>(opId), bus->hasOperation(opId));
        bus->run(opId);
    }
    else
    {
        // 可能 actionId 本身已经是 OperationId3D（来自非 hub action 的直接触发）
        OperationId3D directOpId = static_cast<OperationId3D>(actionId);
        if (bus->hasOperation(directOpId))
        {
            SY_INFOF("[Workbench3D] actionId is direct operationId, running: opId=%d", static_cast<int>(directOpId));
            bus->run(directOpId);
        }
        else
        {
            SY_WARNF("[Workbench3D] No operation found for action id: %d", actionId);
        }
    }
}

// 步骤3 — 附加到窗口，触发 UI 构建
void Workbench3D::attachToWindow(WorkbenchWindow& window)
{
    build3DWorkbenchUi(window);
}

// 步骤4 — 激活工作台，应用初始状态
void Workbench3D::activate()
{
    applyWorkbenchState(m_services.stateCenter, id(), m_initialState);
}

// 步骤5 — 停用工作台，保存状态并清理资源
void Workbench3D::deactivate()
{
    m_savedState = currentSnapshot();

    // 清理全局快捷键（Qt::ApplicationShortcut 不会随父窗口销毁），
    // 避免切换到 2D 后仍触发 3D 操作导致崩溃
    delete m_deleteShortcut;
    m_deleteShortcut = nullptr;
    delete m_backspaceShortcut;
    m_backspaceShortcut = nullptr;

    // 先关闭 ServiceLocator3D 释放所有服务（它们可能持有 MainWindow3D 的 QObject parent），
    // 再销毁 MainWindow3D 以避免 Qt parent 机制导致的二次析构崩溃
    SY_INFO("[Workbench3D] Deactivating, shutting down ServiceLocator3D first...");
    ServiceLocator3D::shutdown();
    SY_INFO("[Workbench3D] ServiceLocator3D::shutdown() completed");

    // 注意：MenuManager3D 的父对象是 MainWindow3D，
    // 如果先销毁 MainWindow3D，Qt 会自动销毁 MenuManager3D，
    // 导致 m_menuManager3D.reset() 时双重释放崩溃
    // 所以先重置 m_menuManager3D（解除父子关系），再销毁 MainWindow3D
    SY_INFO("[Workbench3D] Destroying MenuManager3D...");
    m_menuManager3D.reset();
    SY_INFO("[Workbench3D] Destroying MainWindow3D...");
    m_mainWindow3D.reset();
}

// 步骤6 — 关闭工作台，清理资源
void Workbench3D::shutdown()
{
    m_services = UiServices{};
    m_scene.reset();
    // 清理全局快捷键（Qt::ApplicationShortcut 不会随父窗口销毁）
    delete m_deleteShortcut;
    m_deleteShortcut = nullptr;
    delete m_backspaceShortcut;
    m_backspaceShortcut = nullptr;
    // 先关闭 ServiceLocator3D 释放所有服务，再销毁 MainWindow3D
    ServiceLocator3D::shutdown();
    m_menuManager3D.reset();
    m_mainWindow3D.reset();
    // m_sceneManager3D 由 ApplicationCompositionRoot 管理，此处不释放
}
#endif