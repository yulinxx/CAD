/**
 * @file UiWorkbench.cpp
 * @brief 工作台实现 — 2D/3D 工作台的初始化、窗口绑定和生命周期管理
 */

#include "UiWorkbench.h"

#include <QAction>
#include <QTextEdit>
#include <QToolBar>
#include <QWidget>

#include "UiCommandDispatcher.h"
#include "UiEntities.h"
#include "UiServices.h"
#include "UiStateCenter.h"
#include "UiViewWidgets.h"
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
    /// @param stateCenter 状态中心
    /// @param workbenchId 工作台 ID
    /// @param viewMode 视图模式
    /// @param layerId 图层 ID
    /// @param documentId 文档 ID
    /// @param selectionSource 选择来源
    /// @param selectionText 选择文本
    /// @param selectionType 选择类型
    /// @param viewportType 视口类型
    /// @param viewportStatus 视口状态
    void applyWorkbenchState(UiStateCenter* stateCenter,
        const QString& workbenchId,
        const QString& viewMode,
        const QString& layerId,
        const QString& documentId,
        const QString& selectionSource,
        const QString& selectionText,
        const QString& selectionType,
        const QString& viewportType,
        const QString& viewportStatus)
    {
        if (!stateCenter)
            return;

        stateCenter->setCurrentWorkbenchId(workbenchId);
        stateCenter->setCurrentViewMode(viewMode);
        stateCenter->setCurrentLayerId(layerId);
        stateCenter->setCurrentDocumentId(documentId);
        stateCenter->setDirty(false);
        stateCenter->setSelectionContext(selectionSource, selectionText);
        stateCenter->setMetadata({
            { QStringLiteral("workbenchId"), workbenchId },
            { QStringLiteral("documentType"), workbenchId },
            { QStringLiteral("selectionSource"), selectionSource },
            { QStringLiteral("selectionText"), selectionText },
            { QStringLiteral("selectionType"), selectionType },
            { QStringLiteral("viewportType"), viewportType },
            { QStringLiteral("commandOwner"), QStringLiteral("none") },
            { QStringLiteral("commandType"), QStringLiteral("none") },
            { QStringLiteral("commandState"), QStringLiteral("idle") },
            { QStringLiteral("viewportStatus"), viewportStatus }
            });
    }

    /// 配置 2D 视口
    /// @param viewport 2D 视口
    /// @param services UI 服务集合
    /// @param document 2D 实体文档
    /// @param properties 属性面板
    void configure2DViewport(CanvasViewport2D* viewport, const UiServices& services,
        EntityDocument2D* document, PropertiesPanelWidget* properties)
    {
        if (!viewport)
            return;
        viewport->setDocument(document);
        viewport->setStateCenter(services.stateCenter);
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
    }

    /// 配置 3D 视口
    /// @param viewport 3D 视口
    /// @param services UI 服务集合
    /// @param document 3D 场景文档
    /// @param controller 相机控制器
    /// @param properties 属性面板
    /// @param tree 场景树面板
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

    /// 更新 2D 对象详情到属性面板
    /// @param panel 属性面板
    /// @param doc 2D 实体文档
    /// @param selectedId 选中实体 ID
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
    /// @param panel 属性面板
    /// @param doc 3D 场景文档
    /// @param selectedId 选中节点 ID
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
            QStringLiteral("Path Depth: %1").arg(node->pathNamesRecursive().size()),
            QStringLiteral("Selected: yes")
            });
    }
}

// Workbench2D 实现

/// 获取工作台 ID
QString Workbench2D::id() const
{
    return QStringLiteral("2D");
}

/// 获取工作台显示名称
QString Workbench2D::displayName() const
{
    return QStringLiteral("2D Workbench");
}

/// 初始化工作台
/// @param services UI 服务集合
/// @return 是否初始化成功
bool Workbench2D::initialize(const UiServices& services)
{
    if (!services.stateCenter || !services.commandDispatcher)
        return false;
    m_services = &services;
    return true;
}

/// 附加到主窗口，构建 2D 工作台 UI
/// @param window 工作台窗口
void Workbench2D::attachToWindow(WorkbenchWindow& window)
{
    auto* sceneDock = new SceneTreeDockWidget(&window);
    sceneDock->setSceneDocument(nullptr);
    window.registerDockWidget(QStringLiteral("2D Layers"), sceneDock, Qt::LeftDockWidgetArea);

    m_document = std::make_shared<EntityDocument2D>();
    auto firstLine = m_document->createLine(QPointF(-120, -80), QPointF(160, 100));
    auto secondLine = m_document->createLine(QPointF(-160, 120), QPointF(100, 180));
    m_document->selection().add(firstLine);

    auto* properties = new PropertiesPanelWidget(&window);
    properties->setEntityDocument(m_document.get());
    properties->setStateText(QStringLiteral("2D ready"));
    properties->setSelectionText(QStringLiteral("Selected: %1, %2").arg(firstLine->id(), secondLine->id()));
    update2DDetails(properties, m_document.get(), firstLine->id());
    window.registerDockWidget(QStringLiteral("2D Properties"), properties, Qt::RightDockWidgetArea);

    auto* commandPanel = createPanelWidget(QStringLiteral("Command panel"), &window);
    window.registerDockWidget(QStringLiteral("2D Command"), commandPanel, Qt::BottomDockWidgetArea);

    auto* mainBar = window.registerToolBar(QStringLiteral("2D Main"));
    auto* drawLine = addWorkbenchAction(mainBar, QStringLiteral("Draw Line"));
    auto* drawPolyline = addWorkbenchAction(mainBar, QStringLiteral("Draw Polyline"));
    auto* measure = addWorkbenchAction(mainBar, QStringLiteral("Measure"));
    auto* deleteEntity = addWorkbenchAction(mainBar, QStringLiteral("Delete"));
    auto* editEntity = addWorkbenchAction(mainBar, QStringLiteral("Edit"));
    auto* selectEntity = addWorkbenchAction(mainBar, QStringLiteral("Select"));

    auto* viewBar = window.registerToolBar(QStringLiteral("2D View"));
    auto* zoomExtents = addWorkbenchAction(viewBar, QStringLiteral("Zoom Extents"));
    auto* pan = addWorkbenchAction(viewBar, QStringLiteral("Pan"));

    if (m_services && m_services->commandDispatcher)
    {
        m_services->commandDispatcher->bindAction(drawLine, QStringLiteral("2d.draw_line"));
        m_services->commandDispatcher->bindAction(drawPolyline, QStringLiteral("2d.draw_polyline"));
        m_services->commandDispatcher->bindAction(measure, QStringLiteral("2d.measure"));
        m_services->commandDispatcher->bindAction(zoomExtents, QStringLiteral("2d.zoom_extents"));
        m_services->commandDispatcher->bindAction(pan, QStringLiteral("2d.pan"));
        m_services->commandDispatcher->bindAction(deleteEntity, QStringLiteral("2d.delete"));
        m_services->commandDispatcher->bindAction(editEntity, QStringLiteral("2d.edit"));
        m_services->commandDispatcher->bindAction(selectEntity, QStringLiteral("2d.select"));
    }

    auto* viewport = new CanvasViewport2D(&window);
    configure2DViewport(viewport, *m_services, m_document.get(), properties);
    window.setCentralWidget(viewport);
    if (properties)
    {
        properties->setSelectionText(QStringLiteral("Selected: %1, %2").arg(firstLine->id(), secondLine->id()));
        properties->setObjectDetails(QStringLiteral("2D Selection"), {
            QStringLiteral("Primary: %1").arg(firstLine->id()),
            QStringLiteral("Secondary: %1").arg(secondLine->id()),
            QStringLiteral("Mode: %1").arg(QStringLiteral("2D"))
            });
    }

    applyWorkbenchState(m_services ? m_services->stateCenter : nullptr,
        id(),
        QStringLiteral("2D Canvas"),
        QStringLiteral("Default"),
        QStringLiteral("2D Document"),
        QStringLiteral("2D-Init"),
        QStringLiteral("Selected: %1, %2").arg(firstLine->id(), secondLine->id()),
        QStringLiteral("2D"),
        QStringLiteral("2D"),
        QStringLiteral("2D ready"));

    viewport->setDrawingEnabled(false);
    viewport->setMeasureMode(false);
}

/// 激活工作台
void Workbench2D::activate()
{
    if (m_services && m_services->stateCenter)
        m_services->stateCenter->setCurrentWorkbenchId(id());
}

/// 停用工作台
void Workbench2D::deactivate()
{
}

/// 关闭工作台
void Workbench2D::shutdown()
{
    m_services = nullptr;
}

// Workbench3D 实现

/// 获取工作台 ID
QString Workbench3D::id() const
{
    return QStringLiteral("3D");
}

/// 获取工作台显示名称
QString Workbench3D::displayName() const
{
    return QStringLiteral("3D Workbench");
}

/// 初始化工作台
/// @param services UI 服务集合
/// @return 是否初始化成功
bool Workbench3D::initialize(const UiServices& services)
{
    if (!services.stateCenter || !services.commandDispatcher)
        return false;
    m_services = &services;
    return true;
}

/// 附加到主窗口，构建 3D 工作台 UI
/// @param window 工作台窗口
void Workbench3D::attachToWindow(WorkbenchWindow& window)
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

    auto* properties = new PropertiesPanelWidget(&window);
    properties->setSceneDocument(m_scene.get());
    properties->setStateText(QStringLiteral("3D ready"));
    properties->setSelectionText(QStringLiteral("Root node: %1").arg(root->id()));
    update3DDetails(properties, m_scene.get(), root->id());
    window.registerDockWidget(QStringLiteral("3D Inspector"), properties, Qt::RightDockWidgetArea);

    auto* sceneDock = new SceneTreeDockWidget(&window);
    sceneDock->setSceneDocument(m_scene.get());
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
        if (m_services && m_services->stateCenter)
            m_services->stateCenter->setSelectionContext(QStringLiteral("3D-Tree"), QStringLiteral("3D node: %1").arg(nodeId));
        if (auto* viewport = qobject_cast<Viewport3D*>(window.centralWidget()))
            viewport->selectNodeById(nodeId);
        });
    window.registerDockWidget(QStringLiteral("3D Scene"), sceneDock, Qt::LeftDockWidgetArea);

    auto* history = createPanelWidget(QStringLiteral("Operation history"), &window);
    window.registerDockWidget(QStringLiteral("3D History"), history, Qt::BottomDockWidgetArea);

    auto* mainBar = window.registerToolBar(QStringLiteral("3D Main"));
    auto* orbit = addWorkbenchAction(mainBar, QStringLiteral("Orbit"));
    auto* orbitSelected = addWorkbenchAction(mainBar, QStringLiteral("Orbit Selected"));
    auto* measure = addWorkbenchAction(mainBar, QStringLiteral("Measure"));
    auto* selectEntity = addWorkbenchAction(mainBar, QStringLiteral("Select"));

    auto* navBar = window.registerToolBar(QStringLiteral("3D Navigation"));
    auto* top = addWorkbenchAction(navBar, QStringLiteral("Top"));
    auto* front = addWorkbenchAction(navBar, QStringLiteral("Front"));
    auto* right = addWorkbenchAction(navBar, QStringLiteral("Right"));

    if (m_services && m_services->commandDispatcher)
    {
        m_services->commandDispatcher->bindAction(orbit, QStringLiteral("3d.orbit"));
        m_services->commandDispatcher->bindAction(orbitSelected, QStringLiteral("3d.orbit_selected"));
        m_services->commandDispatcher->bindAction(measure, QStringLiteral("3d.measure"));
        m_services->commandDispatcher->bindAction(selectEntity, QStringLiteral("3d.select"));
        m_services->commandDispatcher->bindAction(top, QStringLiteral("3d.view_top"));
        m_services->commandDispatcher->bindAction(front, QStringLiteral("3d.view_front"));
        m_services->commandDispatcher->bindAction(right, QStringLiteral("3d.view_right"));
    }

    auto* viewport = new Viewport3D(&window);
    configure3DViewport(viewport, *m_services, m_scene.get(), &m_camera, properties, sceneDock);
    viewport->setPathCallback([properties, sceneDock](const QStringList& pathNames) {
        if (properties)
            properties->setObjectDetails(QStringLiteral("3D Path"), pathNames);
        if (sceneDock)
            sceneDock->refresh();
        });
    window.setCentralWidget(viewport);

    applyWorkbenchState(m_services ? m_services->stateCenter : nullptr,
        id(),
        QStringLiteral("3D Viewport"),
        QStringLiteral("Scene"),
        QStringLiteral("3D Scene"),
        QStringLiteral("3D-Init"),
        QStringLiteral("Root node: %1").arg(root->id()),
        QStringLiteral("3D"),
        QStringLiteral("3D"),
        QStringLiteral("3D ready"));

    viewport->setOrbitMode(true);
    viewport->setMeasureMode(false);
}

/// 激活工作台
void Workbench3D::activate()
{
    if (m_services && m_services->stateCenter)
        m_services->stateCenter->setCurrentWorkbenchId(id());
}

/// 停用工作台
void Workbench3D::deactivate()
{
}

/// 关闭工作台
void Workbench3D::shutdown()
{
    m_services = nullptr;
}