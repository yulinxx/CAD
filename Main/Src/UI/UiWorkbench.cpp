#include "UiWorkbench.h"

#include <QAction>
#include <QSizePolicy>
#include <QTextEdit>
#include <QToolBar>
#include <QWidget>
#include "SceneDocument2D.h"
#include "SelectionService.h"
#include "UiCommandDispatcher.h"
#include "UiServices.h"
#include "UiStateCenter.h"
#include "UiSceneTreeDock.h"
#include "UiPropertiesPanel.h"
#include "UiViewWidgets.h"
#include "Engine2D/SyEntity/SyLine.h"
#include "DrawToolBarWidget.h"
#include "SceneBuilder2D.h"
#include "WorkbenchWindow.h"
#include "UI2D/Operation/OperationBus.h"
#include "UI2D/Operation/OperationId.h"
#include "ViewWidgetAdapter.h"

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

    /// 配置 2D 视口
    void configure2DViewport(Viewport2D* viewport, const UiServices& services,
        SceneDocument2D* document, PropertiesPanelWidget* properties)
    {
        if (!viewport)
            return;

        viewport->setDocument(document);
        viewport->setSelectionService(services.selectionService);
        viewport->setCommandDispatcher(services.commandDispatcher);
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
    if (!services.stateCenter || !services.commandDispatcher)
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

    // 步骤2.2: 创建绘制工具栏
    auto* drawToolBar = new DrawToolBarWidget(&window);
    if (m_services.commandDispatcher)
        drawToolBar->setCommandDispatcher(m_services.commandDispatcher);
    window.registerDockWidget(QObject::tr("2D Draw Tools"), drawToolBar, Qt::LeftDockWidgetArea);

    // 步骤2.3: 创建文档
    auto sceneResult = SceneBuilder2D::createDefault2DScene();
    m_document = std::move(sceneResult.document);
    // 把文档注入到服务集合，使命令系统能通过 services.document2D 访问文档
    m_services.document2D = m_document.get();
    // 创建选择服务并注入，将选择状态从文档中分离
    m_selectionService = std::make_unique<SelectionService>(m_document->sceneManager());
    m_services.selectionService = m_selectionService.get();
    // 同步到命令分发器，使其内部的 m_uiServices 也包含文档引用
    // （Dispatcher 在 AppBootstrapper 构造时设置的 m_uiServices 不含 document2D）
    if (m_services.commandDispatcher)
        m_services.commandDispatcher->setUiServices(m_services);
    auto primaryId = sceneResult.primaryLineId;
    auto secondaryId = sceneResult.secondaryLineId;

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

    // 步骤2.7: 创建视口并设置为中央组件
    window.setCentralWidget(createCentralViewport(window, properties));
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
    m_initialState.documentId = QObject::tr("2D Document"); // 2D 文档
    m_initialState.selectionSource = QObject::tr("2D-Init"); // 2D-初始化
    m_initialState.selectionText = QObject::tr("Selected: %1, %2").arg(primaryId, secondaryId); // 已选: %1, %2
    m_initialState.selectionType = QObject::tr("2D"); // 2D
    m_initialState.viewportType = QObject::tr("2D"); // 2D
    m_initialState.viewportStatus = QObject::tr("2D ready"); // 2D 就绪
    m_initialState.dirty = false;
}

QWidget* Workbench2D::createCentralViewport(WorkbenchWindow& window, PropertiesPanelWidget* properties)
{
    auto* viewport = new Viewport2D(&window);
    // 接入命令系统：注入 document/dispatcher/interactionDispatcher/operationBus 和回调
    configure2DViewport(viewport, m_services, m_document.get(), properties);
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

    // 统一走 commandDispatcher（阶段 3：消除两条命令路径并存）
    auto* dispatcher = m_services.commandDispatcher;
    if (dispatcher)
    {
        QObject::connect(moveEntity, &QAction::triggered, [dispatcher]() {
            dispatcher->execute(QStringLiteral("2d.move"));
            });
        QObject::connect(copyEntity, &QAction::triggered, [dispatcher]() {
            dispatcher->execute(QStringLiteral("2d.copy"));
            });
        QObject::connect(rotateEntity, &QAction::triggered, [dispatcher]() {
            dispatcher->execute(QStringLiteral("2d.rotate"));
            });
        QObject::connect(mirrorEntity, &QAction::triggered, [dispatcher]() {
            dispatcher->execute(QStringLiteral("2d.mirror"));
            });
        QObject::connect(deleteEntity, &QAction::triggered, [dispatcher]() {
            dispatcher->execute(QStringLiteral("2d.delete"));
            });
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
void Workbench2D::activate()
{
    applyWorkbenchState(m_services.stateCenter, id(), m_initialState);
}

// 步骤4 — 停用工作台，保存状态
void Workbench2D::deactivate()
{
    m_savedState = WorkbenchStateSnapshot{};
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
    if (!services.stateCenter || !services.commandDispatcher)
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

    if (m_services.commandDispatcher)
    {
        m_services.commandDispatcher->bindAction(orbit, QStringLiteral("3d.orbit"));
        m_services.commandDispatcher->bindAction(orbitSelected, QStringLiteral("3d.orbit_selected"));
        m_services.commandDispatcher->bindAction(measure, QStringLiteral("3d.measure"));
        m_services.commandDispatcher->bindAction(selectEntity, QStringLiteral("3d.select"));
        m_services.commandDispatcher->bindAction(top, QStringLiteral("3d.view_top"));
        m_services.commandDispatcher->bindAction(front, QStringLiteral("3d.view_front"));
        m_services.commandDispatcher->bindAction(right, QStringLiteral("3d.view_right"));
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
    m_initialState.documentId = QObject::tr("3D Scene");
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