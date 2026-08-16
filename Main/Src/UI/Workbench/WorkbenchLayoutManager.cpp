#include "WorkbenchLayoutManager.h"
#include "WorkbenchMenuManager.h"
#include "UiSceneTreePanel2D.h"
#include "UiPropertiesPanel.h"
#include "Persistence/PersistenceService.h"
#include "Persistence/Repositories/WorkspaceSnapshotRepository.h"
#include "Persistence/Models/WorkspaceSnapshotRecord.h"
#include "Render/RenderViewport2D.h"
#include "Render/UiViewport3D.h"
#include "Log/SyLogger.h"

#include <QDateTime>
#include <QDockWidget>
#include <QLabel>
#include <QMenuBar>
#include <QProgressBar>
#include <QSettings>
#include <QStatusBar>
#include <QToolBar>
#include <QDateTime>
#include <QDockWidget>
#include <QLabel>
#include <QMenuBar>
#include <QProgressBar>
#include <QSettings>
#include <QStatusBar>
#include <QToolBar>
#include <QWidget>
#include <QPointer>

#ifdef SANYI_ENABLE_CONFIG_DRIVEN_UI
    #include "ClientConfig/UiConfigLoader.h"
    #include "ClientConfig/UiConfigurationManager.h"
    #include "ClientConfig/UiLayoutBuilder.h"
    #include "ClientConfig/UiPanelRegistry.h"

namespace
{
    /// 客户配置资源路径（编译期由 SANYI_CLIENT_ID 决定）
    QString clientConfigResourcePath()
    {
    #ifndef SANYI_CLIENT_ID
        return QStringLiteral(":/configs/san_yi.json");
    #else
        return QStringLiteral(":/configs/%1.json").arg(QString::fromUtf8(SANYI_CLIENT_ID));
    #endif
    }
}  // namespace
#endif

WorkbenchLayoutManager::WorkbenchLayoutManager(QMainWindow* parent, WorkbenchMenuManager* menuManager)
    : m_parent(parent)
    , m_menuManager(menuManager)
{
}

WorkbenchLayoutManager::~WorkbenchLayoutManager() = default;

// ==================== 骨架初始化 ====================

void WorkbenchLayoutManager::initializeToolBarSkeleton()
{
    // 工具栏骨架只创建承载容器，不在初始化绑定具体动作
    buildToolBars();
}

void WorkbenchLayoutManager::buildToolBars()
{
    // 工具栏骨架只创建承载容器，不在初始化绑定具体动作
}

void WorkbenchLayoutManager::initializeDockAreaSkeleton()
{
    // 停靠区骨架先只创建左右容器，不在这里挂接具体工作台面板
    buildDockAreas();
}

void WorkbenchLayoutManager::buildDockAreas()
{
#ifdef SANYI_ENABLE_CONFIG_DRIVEN_UI
    // 配置驱动优先：成功则由 JSON 构建骨架停靠面板
    if (buildDockAreasFromConfig())
    {
        SY_INFO("[WorkbenchLayoutManager] Dock areas built from client config");
        return;
    }
    SY_WARNF("[WorkbenchLayoutManager] Config-driven dock build failed, "
             "falling back to hardcoded skeleton");
#endif

    // 场景树面板
    m_panelState.sceneTreeDock = new SceneTreePanel2D(m_parent);
    m_panelState.leftDock = new QDockWidget(m_parent->tr("Scene"), m_parent);  // 场景
    m_panelState.leftDock->setObjectName(QStringLiteral("SceneDock"));
    m_panelState.leftDock->setWidget(m_panelState.sceneTreeDock);
    m_panelState.leftDock->setMinimumWidth(170);
    m_panelState.leftDock->setMaximumWidth(280);
    m_parent->addDockWidget(Qt::LeftDockWidgetArea, m_panelState.leftDock);

    // 属性面板
    m_panelState.propertiesDock = new PropertiesPanelWidget(m_parent);
    m_panelState.rightDock = new QDockWidget(m_parent->tr("Properties"), m_parent);  // 属性
    m_panelState.rightDock->setObjectName(QStringLiteral("PropertiesDock"));
    m_panelState.rightDock->setWidget(m_panelState.propertiesDock);
    m_panelState.rightDock->setMinimumWidth(200);
    m_panelState.rightDock->setMaximumWidth(340);
    m_parent->addDockWidget(Qt::RightDockWidgetArea, m_panelState.rightDock);

    // 设置初始面板宽度，避免两侧面板默认过宽挤压中间的视图
    m_parent->resizeDocks({ m_panelState.leftDock, m_panelState.rightDock }, { 200, 300 },
                          Qt::Horizontal);
}

#ifdef SANYI_ENABLE_CONFIG_DRIVEN_UI
bool WorkbenchLayoutManager::buildDockAreasFromConfig()
{
    // 懒加载配置管理器与面板注册表
    if (!m_configManager)
    {
        m_configManager = std::make_unique<UiConfigurationManager>();
        m_panelRegistry = std::make_unique<UiPanelRegistry>();

        // 注册内置面板工厂：与 JSON 中的 widgetType 对应
        m_panelRegistry->registerPanel(QStringLiteral("SceneTreePanel"), [](QWidget* parent) {
            return static_cast<QWidget*>(new SceneTreePanel2D(parent));
        });
        m_panelRegistry->registerPanel(QStringLiteral("PropertiesPanel"), [](QWidget* parent) {
            return static_cast<QWidget*>(new PropertiesPanelWidget(parent));
        });
    }

    const QString resourcePath = clientConfigResourcePath();
    if (!m_configManager->applyConfiguration(resourcePath, ConfigFallbackPolicy::Strict))
    {
        SY_ERRORF("[WorkbenchLayoutManager] Failed to load client config: %s", qPrintable(resourcePath));
        return false;
    }

    const UiConfigData* config = m_configManager->configData();
    if (!config)
    {
        return false;
    }

    // 数据驱动构建 Dock（命令分发器此处不参与，仅为构造签名提供空实现）
    struct NullDispatcher : public IUiCommandDispatcher
    {
        bool isCommandRegistered(const QString&) const override
        {
            return false;
        }

        void dispatch(const QString&) override {}
    };

    NullDispatcher dispatcher;
    UiLayoutBuilder builder(m_parent, &dispatcher, m_panelRegistry.get());
    builder.buildDocks(config->docks);

    // 将构建出的 Dock widget 挂入布局管理器注册表，与硬编码路径行为一致
    // 便于统一清理（clearLayoutContent）与布局快照（restoreLayoutSnapshot）
    for (QWidget* dockWidget : builder.builtDocks())
    {
        if (auto* dock = qobject_cast<QDockWidget*>(dockWidget))
        {
            dock->setProperty("_workbench_dock_title", dock->windowTitle());
            m_registeredDocks.push_back(dock);

            // 同步面板状态引用，保持与硬编码路径一致的对外接口
            const QString dockId = dock->objectName();
            if (dockId == QStringLiteral("SceneDock"))
            {
                m_panelState.leftDock = dock;
            }
            else if (dockId == QStringLiteral("PropertiesDock"))
            {
                m_panelState.rightDock = dock;
            }

            if (auto* tree = qobject_cast<SceneTreePanel2D*>(dock->widget()))
            {
                m_panelState.sceneTreeDock = tree;
            }
            if (auto* props = qobject_cast<PropertiesPanelWidget*>(dock->widget()))
            {
                m_panelState.propertiesDock = props;
            }
        }
    }

    return !m_registeredDocks.empty();
}
#endif

void WorkbenchLayoutManager::initializeStatusBarSkeleton()
{
    // 状态栏骨架只承载全局状态展示，不提前填入业务语义
    buildStatusBar();
}

void WorkbenchLayoutManager::buildStatusBar()
{
    // 构建框架级状态栏标签（始终存在，不随工作台切换而销毁）
    // 工作台级内容（坐标/选择/消息）由 StatusBarBase 子类管理，
    // 通过 WorkbenchWindow::mountStatusBar/unmountStatusBar 在工作台切换时挂载/卸载
    m_panelState.statusBar = m_parent->statusBar();
}

QWidget* WorkbenchLayoutManager::createInitialCentralWidget()
{
    auto* widget = new QWidget(m_parent);
    widget->setObjectName(QStringLiteral("WorkbenchCentralPlaceholder"));
    return widget;
}

// ==================== 注册与清理 ====================

QDockWidget* WorkbenchLayoutManager::registerDockWidget(const QString& title, QWidget* widget, Qt::DockWidgetArea area)
{
    // 注册停靠面板只负责把面板挂到指定区域，不在这里注入业务行为
    auto* dock = new QDockWidget(title, m_parent);
    dock->setObjectName(title);
    dock->setWidget(widget);
    m_parent->addDockWidget(area, dock);
    m_registeredDocks.push_back(dock);

    // 保存标题到 dock widget 的属性中，以便 restoreLayoutSnapshot 后重新设置
    // restoreState() 会覆盖 dock 标题，需要在恢复后重新设置
    dock->setProperty("_workbench_dock_title", title);

    // 仅更新面板状态引用，方便后续统一刷新与清理
    if (auto* tree = qobject_cast<SceneTreePanel2D*>(widget))
    {
        m_panelState.sceneTreeDock = tree;
    }
    if (auto* props = qobject_cast<PropertiesPanelWidget*>(widget))
    {
        m_panelState.propertiesDock = props;
    }

    return dock;
}

QToolBar* WorkbenchLayoutManager::registerToolBar(const QString& title)
{
    // 注册工具栏只负责挂载承载容器，不在这里填入具体动作
    auto* toolBar = m_parent->addToolBar(title);
    toolBar->setObjectName(title);
    m_registeredToolBars.push_back(toolBar);
    return toolBar;
}

void WorkbenchLayoutManager::clearLayoutContent()
{
    // 1: 清理所有工具栏（包括通过 addToolBar 直接添加而未注册的工具栏，如3D左侧工具栏）
    // 先收集所有工具栏指针，避免遍历过程中容器被修改
    const auto allToolBars = m_parent->findChildren<QToolBar*>();
    for (auto* toolBar : allToolBars)
    {
        m_parent->removeToolBar(toolBar);
        delete toolBar;
    }
    m_registeredToolBars.clear();

    // 2: 清理菜单栏 - 3D 工作台使用 MenuManager3D 独立管理菜单，
    // 切换到 2D 时需要清空菜单栏，避免 3D 菜单残留导致混乱
    // 注意：mb->clear() 会同步删除 QAction，但旧 QAction 上可能还有
    // lambda/connect 持有引用。先 disconnect 所有 action，再 clear。
    if (auto* mb = m_parent->menuBar())
    {
        const auto actions = mb->actions();
        for (auto* act : actions)
        {
            if (act)
            {
                act->disconnect();
            }
        }
        mb->clear();
    }

    // 3: 清理所有停靠面板
    for (auto* dock : m_registeredDocks)
    {
        m_parent->removeDockWidget(dock);
        delete dock;
    }
    m_registeredDocks.clear();

    m_panelState.sceneTreeDock = nullptr;
    m_panelState.propertiesDock = nullptr;

    // 4: 清理中央控件 — 先释放 GL 资源，再 setCentralWidget(nullptr)
    // 关键流程：releaseGLResources() → setCentralWidget(nullptr) → hide() → deleteLater()
    //
    // 原因：setCentralWidget(nullptr) 内部会调用旧控件的 hide()，
    // 这将销毁 QOpenGLWidget 的 native window handle。
    // 若在此之后才释放 GL 资源（通过 deleteLater → 析构 → makeCurrent()），
    // 将访问已失效的 handle (0xFFFFFFFFFFFFFFFF) 导致访问冲突崩溃。
    //
    // 因此必须先在有效的 native window 上下文中释放 GL 资源，
    // 再销毁 native window。
    auto* oldCentral = m_parent->centralWidget();
    if (oldCentral)
    {
        SY_INFO("[clearLayoutContent] Step A: releasing GL resources");
        // 2D 视口：释放 RenderWidget (QOpenGLWidget) 的 GL 资源
        if (auto* vp2d = qobject_cast<RenderViewport2D*>(oldCentral))
        {
            vp2d->releaseGLResources();
        }
        // 3D 视口：关闭渲染器持有的 GL 资源
        else if (auto* vp3d = qobject_cast<Viewport3D*>(oldCentral))
        {
            vp3d->releaseGLResources();
        }

        SY_INFO("[clearLayoutContent] Step B: setCentralWidget(nullptr)");
        m_parent->setCentralWidget(nullptr);

        SY_INFO("[clearLayoutContent] Step C: hide + deleteLater");
        oldCentral->hide();
        oldCentral->deleteLater();
    }
    SY_INFO("[clearLayoutContent] Step D: creating placeholder");
    m_parent->setCentralWidget(createInitialCentralWidget());
    SY_INFO("[clearLayoutContent] Step E: done");
}

// ==================== 布局快照 ====================

void WorkbenchLayoutManager::setPersistenceService(PersistenceService* ps)
{
    m_persistenceService = ps;
}

void WorkbenchLayoutManager::saveLayoutSnapshot(const QString& workbenchId)
{
    // 布局快照只保存窗口外观，不保存业务状态；业务状态由状态中心负责
    if (workbenchId.isEmpty())
    {
        return;
    }

    // 优先使用数据库持久化，失败时回退到 QSettings
    if (m_persistenceService && m_persistenceService->isOpen() && m_persistenceService->workspaceSnapshots())
    {
        WorkspaceSnapshotRecord rec;
        rec.workbenchId = workbenchId.toStdString();
        rec.geometry = m_parent->saveGeometry().toBase64().toStdString();
        rec.windowState = m_parent->saveState().toBase64().toStdString();
        rec.updatedAt = QDateTime::currentDateTime().toString(Qt::ISODate).toStdString();
        m_persistenceService->workspaceSnapshots()->save(rec);
        SY_INFOF("[WorkbenchLayoutManager] Saved layout snapshot to database: %s", rec.workbenchId.c_str());
    }

    // QSettings 兜底
    QSettings settings;
    settings.beginGroup(QStringLiteral("LayoutSnapshots"));
    settings.setValue(workbenchId + QStringLiteral("/geometry"), m_parent->saveGeometry());
    settings.setValue(workbenchId + QStringLiteral("/windowState"), m_parent->saveState());
    settings.endGroup();
}

void WorkbenchLayoutManager::restoreLayoutSnapshot(const QString& workbenchId)
{
    // 布局恢复只还原窗口外观，不在这里恢复业务状态，避免状态源不统一
    // 注意：不恢复窗口几何尺寸（geometry），保持当前窗口位置和大小不变
    if (workbenchId.isEmpty())
    {
        return;
    }

    // 优先从数据库加载
    QByteArray state;
    if (m_persistenceService && m_persistenceService->isOpen() && m_persistenceService->workspaceSnapshots())
    {
        auto rec = m_persistenceService->workspaceSnapshots()->load(workbenchId.toStdString());
        if (!rec.windowState.empty())
        {
            state = QByteArray::fromBase64(QByteArray::fromStdString(rec.windowState));
            SY_INFOF("[WorkbenchLayoutManager] Loaded layout snapshot from database: %s", rec.workbenchId.c_str());
        }
    }

    // 数据库未命中时从 QSettings 兜底
    if (state.isEmpty())
    {
        QSettings settings;
        settings.beginGroup(QStringLiteral("LayoutSnapshots"));
        state = settings.value(workbenchId + QStringLiteral("/windowState")).toByteArray();
        settings.endGroup();
    }

    if (!state.isEmpty())
    {
        m_parent->restoreState(state);
    }

    // restoreState() 会覆盖 dock widget 的标题，需要重新设置为当前工作台的标题
    restoreDockWidgetTitles();
}

void WorkbenchLayoutManager::restoreDockWidgetTitles()
{
    for (auto* dock : m_registeredDocks)
    {
        const auto title = dock->property("_workbench_dock_title").toString();
        if (!title.isEmpty())
        {
            dock->setWindowTitle(title);
        }
    }
}

void WorkbenchLayoutManager::setSkeletonDocksVisible(bool visible)
{
    if (m_panelState.leftDock)
    {
        m_panelState.leftDock->setVisible(visible);
    }
    if (m_panelState.rightDock)
    {
        m_panelState.rightDock->setVisible(visible);
    }

    // 注意：posLabel/selLabel/msgLabel 已移除 —— 这些由 StatusBarBase 子类管理
    // 工作台状态栏 widget 的显示/隐藏由 WorkbenchWindow::setSkeletonDocksVisible 控制
}

// ==================== 繁忙指示器 ====================

void WorkbenchLayoutManager::updateBusyIndicator(bool busy)
{
    // 繁忙指示器只表达忙闲状态，不承载命令细节或工作台切换细节
    if (!m_panelState.statusBar)
    {
        return;
    }

    if (busy)
    {
        if (!m_busyProgressBar)
        {
            auto* progress = new QProgressBar(m_parent);
            progress->setObjectName(QStringLiteral("BusyProgressBar"));
            progress->setRange(0, 0);
            progress->setMaximumWidth(140);
            m_panelState.statusBar->addPermanentWidget(progress);
            m_busyProgressBar = progress;
        }
        return;
    }

    if (m_busyProgressBar)
    {
        m_panelState.statusBar->removeWidget(m_busyProgressBar);
        m_busyProgressBar->deleteLater();
        m_busyProgressBar.clear();
    }
}