#include "UiWorkbench.h"

#include <QAction>
#include <QSizePolicy>
#include <QTextEdit>
#include <QToolBar>
#include <QWidget>

#include "UiCommandDispatcher.h"
#include "UiEntities.h"
#include "UiServices.h"
#include "UiStateCenter.h"
#include "UiViewWidgets.h"
#include "DrawToolBarWidget.h"
#include "SceneBuilder2D.h"
#include "SceneBuilder3D.h"
#include "WorkbenchWindow.h"
#include "UI2D/Operation/OperationBus.h"
#include "UI2D/Operation/OperationId.h"
#include "ViewWidgetAdapter.h"

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

        // 3D selection 单向流 — 视口选中时只更新 selection 和属性面板
        // 树控件通过 SceneDocument3D::selection() 监听来刷新，不再由视口回调触发
        viewport->setSelectionCallback([&services, properties](const QString& nodeId) {
            if (properties)
                properties->setSelectionText(QStringLiteral("Selected node: %1").arg(nodeId));
            if (services.stateCenter)
                services.stateCenter->setSelectionContext(QStringLiteral("3D-Viewport"), QStringLiteral("3D node: %1").arg(nodeId));
        });
    }

    /// 配置 2D 视口
    void configure2DViewport(CanvasViewport2D* viewport, const UiServices& services,
        EntityDocument2D* document, PropertiesPanelWidget* properties)
    {
        if (!viewport)
            return;

        viewport->setDocument(document);
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
    /// 当前由工作台拼装数据，后续应由 OperationResult 提供
    void update2DDetails(PropertiesPanelWidget* panel, EntityDocument2D* doc, const QString& selectedId)
    {
        if (!panel || !doc)
            return;

        auto line = doc->lineById(selectedId);
        if (!line)
            return;

        panel->setObjectDetails(QStringLiteral("Line %1").arg(line->id()), {
            QStringLiteral("Start: %1,%2").arg(line->start().x()).arg(line->start().y()),
            QStringLiteral("End: %1,%2").arg(line->end().x()).arg(line->end().y()),
            QStringLiteral("Bounds: %1,%2 -> %3,%4").arg(line->bounds().left()).arg(line->bounds().top())
                .arg(line->bounds().right()).arg(line->bounds().bottom())
        });
    }

    /// 更新 3D 对象详情到属性面板
    /// 当前由工作台拼装数据，后续应由 OperationResult 提供
    void update3DDetails(PropertiesPanelWidget* panel, SceneDocument3D* doc, const QString& selectedId)
    {
        if (!panel || !doc)
            return;

        auto node = doc->nodeById(selectedId);
        if (!node)
            return;

        panel->setObjectDetails(QStringLiteral("Node %1").arg(node->id()), {
            QStringLiteral("Name: %1").arg(node->name()),
            QStringLiteral("Children: %1").arg(node->children().size()),
            QStringLiteral("Path: %1").arg(node->pathNamesRecursive().join(QStringLiteral(" / "))),
            QStringLiteral("Selected: yes")
        });
    }
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

QString Workbench2D::id() const { return QStringLiteral("2D"); }
QString Workbench2D::displayName() const { return QStringLiteral("2D Workbench"); }

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
    window.registerDockWidget(QStringLiteral("2D Draw Tools"), drawToolBar, Qt::LeftDockWidgetArea);

    // 步骤2.3: 创建文档
    auto sceneResult = SceneBuilder2D::createDefaultScene();
    m_document = sceneResult.document;
    auto firstLine = sceneResult.primaryLine;
    auto secondLine = sceneResult.secondaryLine;

    // 步骤2.4: 创建属性面板
    auto* properties = new PropertiesPanelWidget(&window);
    properties->setWorkbenchMode(PropertiesPanelWidget::WorkbenchMode::TwoD);
    configureWorkbenchPanels(properties, firstLine, secondLine);
    window.registerDockWidget(QStringLiteral("2D Properties"), properties, Qt::RightDockWidgetArea);

    // 步骤2.5: 创建命令面板
    auto* commandPanel = createPanelWidget(QStringLiteral("Command panel"), &window);
    window.registerDockWidget(QStringLiteral("2D Command"), commandPanel, Qt::BottomDockWidgetArea);

    // 步骤2.6: 创建工具栏并绑定操作
    auto* mainBar = window.registerToolBar(QStringLiteral("2D Main"));
    auto* viewBar = window.registerToolBar(QStringLiteral("2D View"));
    configureWorkbenchActions(mainBar, viewBar);

    // 步骤2.7: 创建视口并设置为中央组件
    window.setCentralWidget(createCentralViewport(window, properties));
    // 当前由工作台拼装属性数据，后续应由 OperationResult 提供
    if (properties)
    {
        properties->setSelectionText(QStringLiteral("Selected: %1, %2").arg(firstLine->id(), secondLine->id()));
        properties->setObjectDetails(QStringLiteral("2D Selection"), {
            QStringLiteral("Primary: %1").arg(firstLine->id()),
            QStringLiteral("Secondary: %1").arg(secondLine->id()),
            QStringLiteral("Mode: %1").arg(QStringLiteral("2D"))
        });
    }

    m_initialState.viewMode = QStringLiteral("2D Canvas");
    m_initialState.layerId = QStringLiteral("Default");
    m_initialState.documentId = QStringLiteral("2D Document");
    m_initialState.selectionSource = QStringLiteral("2D-Init");
    m_initialState.selectionText = QStringLiteral("Selected: %1, %2").arg(firstLine->id(), secondLine->id());
    m_initialState.selectionType = QStringLiteral("2D");
    m_initialState.viewportType = QStringLiteral("2D");
    m_initialState.viewportStatus = QStringLiteral("2D ready");
    m_initialState.dirty = false;
}

QWidget* Workbench2D::createCentralViewport(WorkbenchWindow& window, PropertiesPanelWidget* properties)
{
    if (m_useLegacyCanvasViewport)
    {
        auto* legacyViewport = new CanvasViewport2D(&window);
        configureLegacyViewport(legacyViewport, properties);
        legacyViewport->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        legacyViewport->setMinimumSize(800, 600);
        return legacyViewport;
    }

    auto* viewport = new CanvasViewport2D(&window);
    configureModernViewport(viewport);
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

void Workbench2D::configureWorkbenchPanels(PropertiesPanelWidget* properties,
    const std::shared_ptr<LineEntity2D>& firstLine,
    const std::shared_ptr<LineEntity2D>& secondLine) const
{
    if (!properties || !firstLine || !secondLine)
        return;

    PropertiesPanelWidget::PropertiesData data;
    data.mode = PropertiesPanelWidget::WorkbenchMode::TwoD;
    data.stateText = QStringLiteral("2D ready");
    data.selectionText = QStringLiteral("Selected: %1, %2").arg(firstLine->id(), secondLine->id());
    data.documentType = QStringLiteral("EntityDocument2D");
    data.documentStatus = QStringLiteral("Ready");
    data.modeSpecificFields = {
        QStringLiteral("Mode: 2D Canvas"),
        QStringLiteral("Layers: Default"),
        QStringLiteral("View: Canvas")
    };
    data.objectTitle = QStringLiteral("2D Selection");
    data.objectLines = {
        QStringLiteral("Primary: %1").arg(firstLine->id()),
        QStringLiteral("Secondary: %1").arg(secondLine->id()),
        QStringLiteral("Mode: %1").arg(QStringLiteral("2D"))
    };

    properties->setPropertiesData(data);

    if (m_document)
        update2DDetails(properties, m_document.get(), firstLine->id());
}

void Workbench2D::configureWorkbenchActions(QToolBar* mainBar, QToolBar* viewBar) const
{
    if (!mainBar || !viewBar)
        return;

    // 工具栏绑定 — 优先使用 OperationBus，旧 dispatcher 仅作过渡兼容
    auto* drawLine = addWorkbenchAction(mainBar, QStringLiteral("Draw Line"));
    auto* drawPolyline = addWorkbenchAction(mainBar, QStringLiteral("Draw Polyline"));
    auto* measure = addWorkbenchAction(mainBar, QStringLiteral("Measure"));
    auto* deleteEntity = addWorkbenchAction(mainBar, QStringLiteral("Delete"));
    auto* editEntity = addWorkbenchAction(mainBar, QStringLiteral("Edit"));
    auto* selectEntity = addWorkbenchAction(mainBar, QStringLiteral("Select"));
    auto* zoomExtents = addWorkbenchAction(viewBar, QStringLiteral("Zoom Extents"));
    auto* pan = addWorkbenchAction(viewBar, QStringLiteral("Pan"));

    // 新操作优先通过 OperationBus 绑定
    if (m_services.operationBus)
    {
        QObject::connect(drawLine, &QAction::triggered, [this]() {
            m_services.operationBus->run(OperationId::Tool_Line, {}, OperationSource::TopToolbar);
        });
        QObject::connect(drawPolyline, &QAction::triggered, [this]() {
            m_services.operationBus->run(OperationId::Tool_Polyline, {}, OperationSource::TopToolbar);
        });
        QObject::connect(measure, &QAction::triggered, [this]() {
            m_services.operationBus->run(OperationId::Tool_Select, {}, OperationSource::TopToolbar);
        });
        QObject::connect(deleteEntity, &QAction::triggered, [this]() {
            m_services.operationBus->run(OperationId::Edit_Delete, {}, OperationSource::TopToolbar);
        });
        QObject::connect(selectEntity, &QAction::triggered, [this]() {
            m_services.operationBus->run(OperationId::Tool_Select, {}, OperationSource::TopToolbar);
        });
        QObject::connect(zoomExtents, &QAction::triggered, [this]() {
            m_services.operationBus->run(OperationId::View_ZoomFit, {}, OperationSource::TopToolbar);
        });
    }

    // 过渡期：保留旧 dispatcher 绑定作为兜底
    if (m_services.commandDispatcher)
    {
        m_services.commandDispatcher->bindAction(drawLine, QStringLiteral("2d.draw_line"));
        m_services.commandDispatcher->bindAction(drawPolyline, QStringLiteral("2d.draw_polyline"));
        m_services.commandDispatcher->bindAction(measure, QStringLiteral("2d.measure"));
        m_services.commandDispatcher->bindAction(zoomExtents, QStringLiteral("2d.zoom_extents"));
        m_services.commandDispatcher->bindAction(pan, QStringLiteral("2d.pan"));
        m_services.commandDispatcher->bindAction(deleteEntity, QStringLiteral("2d.delete"));
        m_services.commandDispatcher->bindAction(editEntity, QStringLiteral("2d.edit"));
        m_services.commandDispatcher->bindAction(selectEntity, QStringLiteral("2d.select"));
    }
}

SceneTreeDockWidget* Workbench2D::createLayersDock(WorkbenchWindow& window) const
{
    auto* sceneDock = new SceneTreeDockWidget(&window);
    sceneDock->setObjectName(QStringLiteral("SceneTreeDock"));
    window.registerDockWidget(QStringLiteral("2D Layers"), sceneDock, Qt::LeftDockWidgetArea);
    return sceneDock;
}

void Workbench2D::configureLegacyViewport(CanvasViewport2D* viewport, PropertiesPanelWidget* properties)
{
    configure2DViewport(viewport, m_services, m_document.get(), properties);
    // 传递操作总线引用给视口
    if (viewport && m_services.operationBus)
        viewport->setOperationBus(m_services.operationBus);

    // 创建 ViewWidget 适配器，让 OperationBus 能在旧系统中工作
    if (viewport && m_services.operationBus)
    {
        m_viewWidgetAdapter = std::make_unique<ViewWidgetAdapter>(viewport, nullptr);
        // TODO: ctx.viewWidget 需要 ViewWidget* 类型，ViewWidgetAdapter 暂未继承 ViewWidget
        // auto& ctx = m_services.operationBus->context();
        // ctx.viewWidget = m_viewWidgetAdapter.get();
    }
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

// Workbench3D 实现
// 统一工作台初始化模板
// 流程：initialize → attachToWindow → activate ↔ deactivate → shutdown

QString Workbench3D::id() const { return QStringLiteral("3D"); }
QString Workbench3D::displayName() const { return QStringLiteral("3D Workbench"); }

// 步骤1 — 初始化，存储服务引用
bool Workbench3D::initialize(const UiServices& services)
{
    if (!services.stateCenter || !services.commandDispatcher)
        return false;
    m_services = services;
    m_camera.reset();
    m_savedState = WorkbenchStateSnapshot{};
    m_initialState = WorkbenchStateSnapshot{};
    return true;
}

// 步骤2.1: 创建场景面板（属性面板 + 场景树 + 历史面板）
void Workbench3D::build3DScenePanels(WorkbenchWindow& window, PropertiesPanelWidget*& properties, SceneTreeDockWidget*& sceneDock, QString& rootNodeId)
{
    auto sceneResult = SceneBuilder3D::createDefaultScene(rootNodeId);
    m_scene = sceneResult;

    properties = new PropertiesPanelWidget(&window);
    properties->setObjectName(QStringLiteral("PropertiesPanel3D"));
    properties->setWindowTitle(QStringLiteral("3D Inspector"));

    PropertiesPanelWidget::PropertiesData data;
    data.mode = PropertiesPanelWidget::WorkbenchMode::ThreeD;
    data.stateText = QStringLiteral("3D ready");
    data.selectionText = QStringLiteral("Root node: %1").arg(rootNodeId);
    data.documentType = QStringLiteral("SceneDocument3D");
    data.documentStatus = QStringLiteral("Ready");
    data.modeSpecificFields = {
        QStringLiteral("Mode: 3D Viewport"),
        QStringLiteral("Transform: Position/Rotation/Scale"),
        QStringLiteral("Material: Default")
    };
    properties->setPropertiesData(data);
    update3DDetails(properties, m_scene.get(), rootNodeId);
    window.registerDockWidget(QStringLiteral("3D Inspector"), properties, Qt::RightDockWidgetArea);

    sceneDock = new SceneTreeDockWidget(&window);
    sceneDock->setSceneDocument(m_scene.get());
    sceneDock->setObjectName(QStringLiteral("SceneTreeDock3D"));
    sceneDock->setWindowTitle(QStringLiteral("3D Scene"));
    // 树选中时的单向流 — 用户点击树节点 → 更新 selection → 刷新视口和属性面板
    sceneDock->setSelectionCallback([this, sceneDock, properties, &window](const QString& nodeId) {
        onSceneTreeSelection(nodeId, sceneDock, properties, window);
    });
    window.registerDockWidget(QStringLiteral("3D Scene"), sceneDock, Qt::LeftDockWidgetArea);

    auto* history = createPanelWidget(QStringLiteral("Operation history"), &window);
    window.registerDockWidget(QStringLiteral("3D History"), history, Qt::BottomDockWidgetArea);
}

// 3D 树选择回调 — 单向流：树节点 → selection → 视口 + 属性面板
void Workbench3D::onSceneTreeSelection(const QString& nodeId, SceneTreeDockWidget* sceneDock,
                                        PropertiesPanelWidget* properties, WorkbenchWindow& window)
{
    if (!m_scene)
        return;

    // 第一步：更新 selection，这是唯一的选择写入点
    m_scene->selection().clear();
    auto node = m_scene->nodeById(nodeId);
    if (node)
        m_scene->selection().add(node);

    // 第二步：树控件自身已通过 itemClicked 更新，不再重复 refresh
    // 避免循环刷新

    // 第三步：更新属性面板
    if (properties)
    {
        if (node)
            properties->setObjectDetails(QStringLiteral("Node %1").arg(node->id()), {
                QStringLiteral("Name: %1").arg(node->name()),
                QStringLiteral("Children: %1").arg(node->children().size()),
                QStringLiteral("Path: %1").arg(node->pathNamesRecursive().join(QStringLiteral(" / "))),
                QStringLiteral("Selected: yes")
            });
    }

    // 第四步：更新 UI 状态
    if (m_services.stateCenter)
        m_services.stateCenter->setSelectionContext(QStringLiteral("3D-Tree"), QStringLiteral("3D node: %1").arg(nodeId));

    // 第五步：同步视口选中状态
    if (auto* viewport = qobject_cast<Viewport3D*>(window.centralWidget()))
        viewport->selectNodeById(nodeId);
}

// 步骤2.2: 创建工具栏并绑定操作
void Workbench3D::build3DToolBars(WorkbenchWindow& window)
{
    auto* mainBar = window.registerToolBar(QStringLiteral("3D Main"));
    auto* orbit = addWorkbenchAction(mainBar, QStringLiteral("Orbit"));
    auto* orbitSelected = addWorkbenchAction(mainBar, QStringLiteral("Orbit Selected"));
    auto* measure = addWorkbenchAction(mainBar, QStringLiteral("Measure"));
    auto* selectEntity = addWorkbenchAction(mainBar, QStringLiteral("Select"));

    auto* navBar = window.registerToolBar(QStringLiteral("3D Navigation"));
    auto* top = addWorkbenchAction(navBar, QStringLiteral("Top"));
    auto* front = addWorkbenchAction(navBar, QStringLiteral("Front"));
    auto* right = addWorkbenchAction(navBar, QStringLiteral("Right"));

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

// 步骤2.3: 创建视口并配置联动
QWidget* Workbench3D::build3DViewport(WorkbenchWindow& window, PropertiesPanelWidget* properties, SceneTreeDockWidget* sceneDock)
{
    auto* viewport = new Viewport3D(&window);
    configure3DViewport(viewport, m_services, m_scene.get(), &m_camera, properties, sceneDock);
    // 路径变更时刷新树和属性面板
    viewport->setPathCallback([properties, sceneDock](const QStringList& pathNames) {
        if (properties)
            properties->setObjectDetails(QStringLiteral("3D Path"), pathNames);
        if (sceneDock)
            sceneDock->refresh();
    });
    viewport->setOrbitMode(true);
    viewport->setMeasureMode(false);
    return viewport;
}

// 步骤2.4: 设置初始状态
void Workbench3D::init3DInitialState(const SceneDocument3D& scene, const QString& rootNodeId)
{
    Q_UNUSED(scene);
    m_initialState.viewMode = QStringLiteral("3D Viewport");
    m_initialState.layerId = QStringLiteral("Scene");
    m_initialState.documentId = QStringLiteral("3D Scene");
    m_initialState.selectionSource = QStringLiteral("3D-Init");
    m_initialState.selectionText = QStringLiteral("Root node: %1").arg(rootNodeId);
    m_initialState.selectionType = QStringLiteral("3D");
    m_initialState.viewportType = QStringLiteral("3D");
    m_initialState.viewportStatus = QStringLiteral("3D ready");
    m_initialState.dirty = false;
}

// 步骤2: 组装完整3D工作台 UI
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

// 步骤2: 附加到窗口
void Workbench3D::attachToWindow(WorkbenchWindow& window)
{
    build3DWorkbenchUi(window);
}

// 步骤3 — 激活工作台，应用状态
void Workbench3D::activate()
{
    applyWorkbenchState(m_services.stateCenter, id(), m_initialState);
}

// 步骤4 — 停用工作台，保存状态
void Workbench3D::deactivate()
{
    m_savedState = WorkbenchStateSnapshot{};
}

// 步骤5 — 关闭工作台，清理资源
void Workbench3D::shutdown()
{
    m_services = UiServices{};
    m_scene.reset();
}
