#include "WorkbenchLayoutManager.h"
#include "WorkbenchMenuManager.h"
#include "UiSceneTreeDock.h"
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
#include <QWidget>
#include <QPointer>

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
    // 场景树面板
    m_panelState.sceneTreeDock = new SceneTreeDockWidget(m_parent);
    m_panelState.leftDock = new QDockWidget(m_parent->tr("Scene"), m_parent); // 场景
    m_panelState.leftDock->setObjectName(QStringLiteral("SceneDock"));
    m_panelState.leftDock->setWidget(m_panelState.sceneTreeDock);
    m_panelState.leftDock->setMinimumWidth(180);
    m_panelState.leftDock->setMaximumWidth(400);
    m_parent->addDockWidget(Qt::LeftDockWidgetArea, m_panelState.leftDock);

    // 属性面板
    m_panelState.propertiesDock = new PropertiesPanelWidget(m_parent);
    m_panelState.rightDock = new QDockWidget(m_parent->tr("Properties"), m_parent); // 属性
    m_panelState.rightDock->setObjectName(QStringLiteral("PropertiesDock"));
    m_panelState.rightDock->setWidget(m_panelState.propertiesDock);
    m_panelState.rightDock->setMinimumWidth(200);
    m_panelState.rightDock->setMaximumWidth(450);
    m_parent->addDockWidget(Qt::RightDockWidgetArea, m_panelState.rightDock);
}

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

    // 工作台状态标签（右侧固定）— 显示 WB/Doc/Cmd/Layer/View/Dirty 等全局信息
    m_panelState.workbenchLabel = new QLabel(m_parent);
    m_panelState.workbenchLabel->setMinimumWidth(300);
    m_panelState.statusBar->addPermanentWidget(m_panelState.workbenchLabel, 1);

    // 繁忙标签 — 显示 Busy/Idle 状态
    m_panelState.busyLabel = new QLabel(m_parent);
    m_panelState.busyLabel->setMinimumWidth(60);
    m_panelState.statusBar->addPermanentWidget(m_panelState.busyLabel);
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
    if (auto* tree = qobject_cast<SceneTreeDockWidget*>(widget))
        m_panelState.sceneTreeDock = tree;
    if (auto* props = qobject_cast<PropertiesPanelWidget*>(widget))
        m_panelState.propertiesDock = props;

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
                act->disconnect();
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
            vp2d->releaseGLResources();
        // 3D 视口：关闭渲染器持有的 GL 资源
        else if (auto* vp3d = qobject_cast<Viewport3D*>(oldCentral))
            vp3d->releaseGLResources();

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
        return;

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
        return;

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
        m_parent->restoreState(state);

    // restoreState() 会覆盖 dock widget 的标题，需要重新设置为当前工作台的标题
    restoreDockWidgetTitles();
}

void WorkbenchLayoutManager::restoreDockWidgetTitles()
{
    for (auto* dock : m_registeredDocks)
    {
        const auto title = dock->property("_workbench_dock_title").toString();
        if (!title.isEmpty())
            dock->setWindowTitle(title);
    }
}

void WorkbenchLayoutManager::setSkeletonDocksVisible(bool visible)
{
    if (m_panelState.leftDock)
        m_panelState.leftDock->setVisible(visible);
    if (m_panelState.rightDock)
        m_panelState.rightDock->setVisible(visible);

    // 注意：posLabel/selLabel/msgLabel 已移除 —— 这些由 StatusBarBase 子类管理
    // 工作台状态栏 widget 的显示/隐藏由 WorkbenchWindow::setSkeletonDocksVisible 控制
}

// ==================== 繁忙指示器 ====================

void WorkbenchLayoutManager::updateBusyIndicator(bool busy)
{
    // 繁忙指示器只表达忙闲状态，不承载命令细节或工作台切换细节
    if (!m_panelState.statusBar)
        return;

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