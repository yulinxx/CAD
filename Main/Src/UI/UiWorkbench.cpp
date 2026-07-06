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
#include "WorkbenchWindow.h"

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

        viewport->setSelectionCallback([tree, properties, &services](const QString& nodeId) {
            if (tree)
                tree->refresh();
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

QString Workbench2D::id() const { return QStringLiteral("2D"); }
QString Workbench2D::displayName() const { return QStringLiteral("2D Workbench"); }

bool Workbench2D::initialize(const UiServices& services)
{
    if (!services.stateCenter || !services.commandDispatcher)
        return false;
    m_services = services;
    return true;
}

void Workbench2D::attachToWindow(WorkbenchWindow& window)
{
    auto* sceneDock = createLayersDock(window);

    auto* drawToolBar = new DrawToolBarWidget(&window);
    if (m_services.commandDispatcher)
        drawToolBar->setCommandDispatcher(m_services.commandDispatcher);
    window.registerDockWidget(QStringLiteral("2D Draw Tools"), drawToolBar, Qt::LeftDockWidgetArea);

    m_document = std::make_shared<EntityDocument2D>();
    auto firstLine = m_document->createLine(QPointF(-120, -80), QPointF(160, 100));
    auto secondLine = m_document->createLine(QPointF(-160, 120), QPointF(100, 180));
    m_document->selection().add(firstLine);

    auto* properties = new PropertiesPanelWidget(&window);
    properties->setWorkbenchMode(PropertiesPanelWidget::WorkbenchMode::TwoD);
    configureWorkbenchPanels(properties, firstLine, secondLine);
    window.registerDockWidget(QStringLiteral("2D Properties"), properties, Qt::RightDockWidgetArea);

    auto* commandPanel = createPanelWidget(QStringLiteral("Command panel"), &window);
    window.registerDockWidget(QStringLiteral("2D Command"), commandPanel, Qt::BottomDockWidgetArea);

    auto* mainBar = window.registerToolBar(QStringLiteral("2D Main"));
    auto* viewBar = window.registerToolBar(QStringLiteral("2D View"));
    configureWorkbenchActions(mainBar, viewBar);

    window.setCentralWidget(createCentralViewport(window, properties));
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
    if (!mainBar || !viewBar || !m_services.commandDispatcher)
        return;

    auto* drawLine = addWorkbenchAction(mainBar, QStringLiteral("Draw Line"));
    auto* drawPolyline = addWorkbenchAction(mainBar, QStringLiteral("Draw Polyline"));
    auto* measure = addWorkbenchAction(mainBar, QStringLiteral("Measure"));
    auto* deleteEntity = addWorkbenchAction(mainBar, QStringLiteral("Delete"));
    auto* editEntity = addWorkbenchAction(mainBar, QStringLiteral("Edit"));
    auto* selectEntity = addWorkbenchAction(mainBar, QStringLiteral("Select"));
    auto* zoomExtents = addWorkbenchAction(viewBar, QStringLiteral("Zoom Extents"));
    auto* pan = addWorkbenchAction(viewBar, QStringLiteral("Pan"));

    m_services.commandDispatcher->bindAction(drawLine, QStringLiteral("2d.draw_line"));
    m_services.commandDispatcher->bindAction(drawPolyline, QStringLiteral("2d.draw_polyline"));
    m_services.commandDispatcher->bindAction(measure, QStringLiteral("2d.measure"));
    m_services.commandDispatcher->bindAction(zoomExtents, QStringLiteral("2d.zoom_extents"));
    m_services.commandDispatcher->bindAction(pan, QStringLiteral("2d.pan"));
    m_services.commandDispatcher->bindAction(deleteEntity, QStringLiteral("2d.delete"));
    m_services.commandDispatcher->bindAction(editEntity, QStringLiteral("2d.edit"));
    m_services.commandDispatcher->bindAction(selectEntity, QStringLiteral("2d.select"));
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
}

void Workbench2D::activate()
{
    applyWorkbenchState(m_services.stateCenter, id(), m_initialState);
}

void Workbench2D::deactivate()
{
    m_savedState = WorkbenchStateSnapshot{};
}

void Workbench2D::shutdown()
{
    m_services = UiServices{};
}

// Workbench3D 实现

QString Workbench3D::id() const { return QStringLiteral("3D"); }
QString Workbench3D::displayName() const { return QStringLiteral("3D Workbench"); }

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

void Workbench3D::build3DScenePanels(WorkbenchWindow& window, PropertiesPanelWidget*& properties, SceneTreeDockWidget*& sceneDock, QString& rootNodeId)
{
    m_scene = std::make_shared<SceneDocument3D>();

    auto root = m_scene->createNode(QStringLiteral("Root"));
    auto mesh = m_scene->createNode(QStringLiteral("Mesh"));
    auto childA = m_scene->createNode(QStringLiteral("Child A"));
    auto childB = m_scene->createNode(QStringLiteral("Child B"));

    mesh->addChild(childA);
    mesh->addChild(childB);
    root->addChild(mesh);
    m_scene->selection().add(root);
    rootNodeId = root->id();

    properties = new PropertiesPanelWidget(&window);
    properties->setObjectName(QStringLiteral("PropertiesPanel3D"));
    properties->setWindowTitle(QStringLiteral("3D Inspector"));

    PropertiesPanelWidget::PropertiesData data;
    data.mode = PropertiesPanelWidget::WorkbenchMode::ThreeD;
    data.stateText = QStringLiteral("3D ready");
    data.selectionText = QStringLiteral("Root node: %1").arg(root->id());
    data.documentType = QStringLiteral("SceneDocument3D");
    data.documentStatus = QStringLiteral("Ready");
    data.modeSpecificFields = {
        QStringLiteral("Mode: 3D Viewport"),
        QStringLiteral("Transform: Position/Rotation/Scale"),
        QStringLiteral("Material: Default")
    };
    properties->setPropertiesData(data);
    update3DDetails(properties, m_scene.get(), root->id());
    window.registerDockWidget(QStringLiteral("3D Inspector"), properties, Qt::RightDockWidgetArea);

    sceneDock = new SceneTreeDockWidget(&window);
    sceneDock->setSceneDocument(m_scene.get());
    sceneDock->setObjectName(QStringLiteral("SceneTreeDock3D"));
    sceneDock->setWindowTitle(QStringLiteral("3D Scene"));
    sceneDock->setSelectionCallback([this, sceneDock, properties, &window](const QString& nodeId) {
        if (!m_scene)
            return;
        m_scene->selection().clear();
        auto node = m_scene->nodeById(nodeId);
        if (node)
            m_scene->selection().add(node);
        sceneDock->refresh();
        if (properties)
        {
            update3DDetails(properties, m_scene.get(), nodeId);
            if (node)
                properties->setObjectDetails(QStringLiteral("Node %1").arg(node->id()), {
                    QStringLiteral("Name: %1").arg(node->name()),
                    QStringLiteral("Children: %1").arg(node->children().size()),
                    QStringLiteral("Path: %1").arg(node->pathNamesRecursive().join(QStringLiteral(" / "))),
                    QStringLiteral("Selected: yes")
                });
        }

        if (m_services.stateCenter)
            m_services.stateCenter->setSelectionContext(QStringLiteral("3D-Tree"), QStringLiteral("3D node: %1").arg(nodeId));

        if (auto* viewport = qobject_cast<Viewport3D*>(window.centralWidget()))
            viewport->selectNodeById(nodeId);
    });
    window.registerDockWidget(QStringLiteral("3D Scene"), sceneDock, Qt::LeftDockWidgetArea);

    auto* history = createPanelWidget(QStringLiteral("Operation history"), &window);
    window.registerDockWidget(QStringLiteral("3D History"), history, Qt::BottomDockWidgetArea);
}

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

QWidget* Workbench3D::build3DViewport(WorkbenchWindow& window, PropertiesPanelWidget* properties, SceneTreeDockWidget* sceneDock)
{
    auto* viewport = new Viewport3D(&window);
    configure3DViewport(viewport, m_services, m_scene.get(), &m_camera, properties, sceneDock);
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

void Workbench3D::attachToWindow(WorkbenchWindow& window)
{
    build3DWorkbenchUi(window);
}

void Workbench3D::activate()
{
    applyWorkbenchState(m_services.stateCenter, id(), m_initialState);
}

void Workbench3D::deactivate()
{
    m_savedState = WorkbenchStateSnapshot{};
}

void Workbench3D::shutdown()
{
    m_services = UiServices{};
    m_scene.reset();
}
