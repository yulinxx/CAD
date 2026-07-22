#include "UiWorkbench.h"

#include <QAction>
#include <QSizePolicy>
#include <QTextEdit>
#include <QToolBar>
#include <QWidget>
#include <QTimer>

#include "SceneDocument2D.h"
#include "SelectionService.h"
#include "UiServices.h"
#include "UiStateCenter.h"
#include "UI2D/Operation/CommandCatalog.h"
#include "UiSceneTreeDock.h"
#include "UiPropertiesPanel.h"
#include "UiViewWidgets.h"
#include "ViewWidgetAdapter.h"
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

    /// 从 CommandCatalog 的 toolCommands 中提取绘图工具定义
    /// 保证工具栏和菜单使用同一套命令 ID（单一命令定义源）
    QVector<DrawToolEntry> buildDrawToolEntries()
    {
        QVector<DrawToolEntry> entries;
        for (const ToolCommandEntry& catEntry : CommandCatalog::toolCommands())
        {
            // 只取标记为 LeftToolbar 表面的工具
            if (!hasSurface(catEntry.surfaces, CommandSurface::LeftToolbar))
                continue;

            DrawToolEntry entry;
            entry.commandId = QString::fromUtf8(catEntry.toolName);
            entry.displayName = QObject::tr(catEntry.menuText);
            entry.tooltip = QObject::tr("Draw %1").arg(QObject::tr(catEntry.menuText));
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
        SceneDocument2D* document, PropertiesPanelWidget* properties)
    {
        if (!viewport)
            return;

        viewport->setDocument(document);
        viewport->setSelectionService(services.selectionService);
        viewport->setInteractionDispatcher(services.interactionDispatcher);
        // 传递操作总线引用给视口
        if (services.operationBus)
            viewport->setOperationBus(services.operationBus);

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
    // 使用全局 SceneManager，确保导入的实体和视口渲染使用同一数据源
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
    configure2DViewport(viewport, m_services, m_services.document2D, properties);
    // 连接鼠标位置回调到状态栏 posLabel
    viewport->setPositionCallback([&window](double x, double y) {
        window.updatePositionLabel(x, y);
    });
    viewport->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    viewport->setMinimumSize(800, 600);
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
// 3D/2D 差异集中在配置，不散在流程里

QString Workbench3D::id() const
{
    return QStringLiteral("3D");
}
QString Workbench3D::displayName() const
{
    return QObject::tr("3D Workbench");
}

// 步骤1 — 初始化，存储服务引用
bool Workbench3D::initialize(const UiServices& services)
{
    if (!services.stateCenter || !services.interactionDispatcher)
        return false;
    m_services = services;
    m_savedState = WorkbenchStateSnapshot{};
    m_initialState = WorkbenchStateSnapshot{};
    return true;
}

// 步骤2 — 构建 3D 场景面板（属性面板 + 场景树 + 操作历史）
void Workbench3D::build3DScenePanels(WorkbenchWindow& window, PropertiesPanelWidget*& properties, SceneTreeDockWidget*& sceneDock, QString& rootNodeId)
{
    auto sceneResult = SceneBuilder3D::createDefaultScene(rootNodeId);
    m_scene = sceneResult;

    properties = new PropertiesPanelWidget(&window);
    properties->setObjectName(QStringLiteral("PropertiesPanel3D"));
    properties->setWindowTitle(QObject::tr("3D Inspector"));

    PropertiesPanelWidget::PropertiesData data;
    data.mode = PropertiesPanelWidget::WorkbenchMode::ThreeD;
    data.stateText = QObject::tr("3D ready");
    data.selectionText = QObject::tr("Root node: %1").arg(rootNodeId);
    data.documentType = QObject::tr("SceneDocument3D");
    data.documentStatus = QObject::tr("Ready");
    data.modeSpecificFields = {
        QObject::tr("Mode: 3D Viewport"),
        QObject::tr("Transform: Position/Rotation/Scale"),
        QObject::tr("Material: Default")
    };
    properties->setPropertiesData(data);
    update3DDetails(properties, m_scene.get(), rootNodeId);
    window.registerDockWidget(QObject::tr("3D Inspector"), properties, Qt::RightDockWidgetArea);

    sceneDock = new SceneTreeDockWidget(&window);
    sceneDock->setSceneDocument(m_scene.get());
    sceneDock->setObjectName(QStringLiteral("SceneTreeDock3D"));
    sceneDock->setWindowTitle(QObject::tr("3D Scene"));
    sceneDock->setSelectionCallback([this, sceneDock, properties, &window](const QString& nodeId) {
        onSceneTreeSelection(nodeId, sceneDock, properties, window);
        });

    window.registerDockWidget(QObject::tr("3D Scene"), sceneDock, Qt::LeftDockWidgetArea);

    auto* history = createPanelWidget(QObject::tr("Operation history"), &window);
    window.registerDockWidget(QObject::tr("3D History"), history, Qt::BottomDockWidgetArea);
}

// 场景树选中回调：同步选中状态到文档、属性面板和视口
void Workbench3D::onSceneTreeSelection(const QString& nodeId, SceneTreeDockWidget* sceneDock,
    PropertiesPanelWidget* properties, WorkbenchWindow& window)
{
    if (!m_scene)
        return;

    m_scene->selection().clear();
    auto node = m_scene->nodeById(nodeId.toStdString());
    if (node)
        m_scene->selection().add(node);

    if (properties && node)
    {
        const auto pathNames = node->pathNamesRecursive();
        QString pathStr;
        for (size_t i = 0; i < pathNames.size(); ++i)
        {
            if (i > 0)
                pathStr += QObject::tr(" / ");
            pathStr += QString::fromStdString(pathNames[i]);
        }

        properties->setObjectDetails(QObject::tr("Node %1").arg(QString::fromStdString(node->id())), {
            QObject::tr("Name: %1").arg(QString::fromStdString(node->name())),
            QObject::tr("Children: %1").arg(node->children().size()),
            QObject::tr("Path: %1").arg(pathStr),
            QObject::tr("Selected: yes")
            });
    }

    if (m_services.stateCenter)
        m_services.stateCenter->setSelectionContext(QObject::tr("3D-Tree"), QObject::tr("3D node: %1").arg(nodeId));

    if (auto* viewport = qobject_cast<Viewport3D*>(window.centralWidget()))
        viewport->selectNodeById(nodeId);
}

// 步骤3 — 构建 3D 工具栏（主操作栏 + 视角导航栏）
void Workbench3D::build3DToolBars(WorkbenchWindow& window)
{
    auto* mainBar = window.registerToolBar(QObject::tr("3D Main"));
    auto* orbit = addWorkbenchAction(mainBar, QObject::tr("Orbit"));
    auto* orbitSelected = addWorkbenchAction(mainBar, QObject::tr("Orbit Selected"));
    auto* measure = addWorkbenchAction(mainBar, QObject::tr("Measure"));
    auto* selectEntity = addWorkbenchAction(mainBar, QObject::tr("Select"));

    auto* navBar = window.registerToolBar(QObject::tr("3D Navigation"));
    auto* top = addWorkbenchAction(navBar, QObject::tr("Top"));
    auto* front = addWorkbenchAction(navBar, QObject::tr("Front"));
    auto* right = addWorkbenchAction(navBar, QObject::tr("Right"));

    if (m_services.operationBus)
    {
        QObject::connect(orbit, &QAction::triggered, [this]() { m_services.operationBus->run(CommandCatalog::operationForCommandId(QStringLiteral("3d.orbit"))); });
        QObject::connect(orbitSelected, &QAction::triggered, [this]() { m_services.operationBus->run(CommandCatalog::operationForCommandId(QStringLiteral("3d.orbit_selected"))); });
        QObject::connect(measure, &QAction::triggered, [this]() { m_services.operationBus->run(CommandCatalog::operationForCommandId(QStringLiteral("3d.measure"))); });
        QObject::connect(selectEntity, &QAction::triggered, [this]() { m_services.operationBus->run(CommandCatalog::operationForCommandId(QStringLiteral("3d.select"))); });
        QObject::connect(top, &QAction::triggered, [this]() { m_services.operationBus->run(CommandCatalog::operationForCommandId(QStringLiteral("3d.view_top"))); });
        QObject::connect(front, &QAction::triggered, [this]() { m_services.operationBus->run(CommandCatalog::operationForCommandId(QStringLiteral("3d.view_front"))); });
        QObject::connect(right, &QAction::triggered, [this]() { m_services.operationBus->run(CommandCatalog::operationForCommandId(QStringLiteral("3d.view_right"))); });
    }
}

// 步骤4 — 构建 3D 视口并配置回调
QWidget* Workbench3D::build3DViewport(WorkbenchWindow& window, PropertiesPanelWidget* properties, SceneTreeDockWidget* sceneDock)
{
    auto* viewport = new Viewport3D(&window);
    configure3DViewport(viewport, m_services, m_scene.get(), nullptr, properties, sceneDock);
    viewport->setPathCallback([properties, sceneDock](const QStringList& pathNames) {
        if (properties)
            properties->setObjectDetails(QObject::tr("3D Path"), pathNames);
        if (sceneDock)
            sceneDock->refresh();
        });
    viewport->setOrbitMode(true);
    viewport->setMeasureMode(false);
    return viewport;
}

// 步骤5 — 初始化 3D 工作台状态快照
void Workbench3D::init3DInitialState(const SceneDocument3D& scene, const QString& rootNodeId)
{
    Q_UNUSED(scene);
    m_initialState.viewMode = QObject::tr("3D Viewport");
    m_initialState.layerId = QObject::tr("Scene");
    m_initialState.documentId.clear(); // 初始无文档，由 ApplicationCompositionRoot 在文件打开时设置
    m_initialState.selectionSource = QObject::tr("3D-Init");
    m_initialState.selectionText = QObject::tr("Root node: %1").arg(rootNodeId);
    m_initialState.selectionType = QObject::tr("3D");
    m_initialState.viewportType = QObject::tr("3D");
    m_initialState.viewportStatus = QObject::tr("3D ready");
    m_initialState.dirty = false;
}

// 步骤6 — 组装 3D 工作台 UI（面板 + 工具栏 + 视口）
void Workbench3D::build3DWorkbenchUi(WorkbenchWindow& window)
{
    PropertiesPanelWidget* properties = nullptr;
    SceneTreeDockWidget* sceneDock = nullptr;
    QString rootNodeId;
    build3DScenePanels(window, properties, sceneDock, rootNodeId);
    build3DToolBars(window);
    auto* viewport = build3DViewport(window, properties, sceneDock);
    window.setCentralWidget(viewport);
    if (m_scene)
        init3DInitialState(*m_scene, rootNodeId);
}

// 步骤7 — 附加到窗口，触发 UI 构建
void Workbench3D::attachToWindow(WorkbenchWindow& window)
{
    build3DWorkbenchUi(window);
}

// 步骤8 — 激活工作台，应用初始状态
void Workbench3D::activate()
{
    applyWorkbenchState(m_services.stateCenter, id(), m_initialState);
}

// 步骤9 — 停用工作台，清空状态
void Workbench3D::deactivate()
{
    m_savedState = WorkbenchStateSnapshot{};
}

// 步骤10 — 关闭工作台，清理资源
void Workbench3D::shutdown()
{
    m_services = UiServices{};
    m_scene.reset();
}
#endif