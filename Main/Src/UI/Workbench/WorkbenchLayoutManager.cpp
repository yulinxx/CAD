#include "WorkbenchLayoutManager.h"
#include "WorkbenchMenuManager.h"
#include "UiSceneTreePanel.h"
#include "UiPropertiesPanel.h"
#include "Persistence/PersistenceService.h"
#include "Persistence/Repositories/WorkspaceSnapshotRepository.h"
#include "Persistence/Models/WorkspaceSnapshotRecord.h"
#include "UiWorkbench.h"
#include "Log/SyLogger.h"

#include "ClientConfig/UiBuiltinPanels.h"
#include "ClientConfig/UiClientContext.h"
#include "ClientConfig/UiConfigLoader.h"
#include "ClientConfig/UiConfigurationManager.h"
#include "ClientConfig/UiLayoutBuilder.h"
#include "ClientConfig/UiPanelRegistry.h"

#include <QDateTime>
#include <QDockWidget>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QProgressBar>
#include <QSettings>
#include <QStatusBar>
#include <QToolBar>
#include <QWidget>
#include <QPointer>

namespace
{
    /// Dock 构建用的空命令分发器。
    /// Dock 只需要容器与面板控件，不含命令动作，因此给出一个不注册任何命令的实现。
    ///
    /// 注意：工具栏**不能**用它。工具栏里全是命令动作，而 UiLayoutBuilder::bindAction
    /// 在构建期就按 isCommandRegistered() 一次性决定「连 triggered 信号」还是
    /// 「setEnabled(false) + commandUnavailable=true 永久禁用」。用空分发器构建工具栏，
    /// 结果就是所有按钮永久变灰、启动日志刷屏 Unknown command id ——
    /// 而且没有任何后续流程会回来重连（buildToolBars 有 m_configDrivenLayoutBuilt 幂等守卫，
    /// refreshCommandStates 也只遍历菜单栏、并且会主动跳过 commandUnavailable 项）。
    ///
    /// 另外必须放在文件作用域。历史实现把它定义在 buildDockAreasFromConfig() 内部，
    /// 却在 buildToolBars() 里引用，导致配置驱动路径一旦启用就无法编译——
    /// 这也是该路径长期"写好但从未跑通"的直接原因。
    struct NullDispatcher final : public IUiCommandDispatcher
    {
        bool isCommandRegistered(const QString&) const override
        {
            return false;
        }

        void dispatch(const QString&, const QVariantMap&) override {}
    };
}  // namespace

WorkbenchLayoutManager::WorkbenchLayoutManager(QMainWindow* parent, WorkbenchMenuManager* menuManager)
    : m_parent(parent)
    , m_menuManager(menuManager)
{
}

WorkbenchLayoutManager::~WorkbenchLayoutManager() = default;

// ==================== 骨架初始化 ====================

void WorkbenchLayoutManager::setCommandDispatcher(IUiCommandDispatcher* dispatcher)
{
    m_commandDispatcher = dispatcher;
}

void WorkbenchLayoutManager::initializeToolBarSkeleton()
{
    // 骨架阶段只做容器准备，真正内容由统一入口填充。
    buildToolBars();
}

void WorkbenchLayoutManager::buildToolBars()
{
    // 配置驱动是唯一路径：不再保留硬编码回退分支。
    // 保留两条路径的代价是硬编码分支会持续接收新功能，而配置分支永远追不上，
    // 最终两边行为分叉——这是 P0-1 要消除的核心风险。
    if (!ensureConfigLoaded())
    {
        SY_ERROR("[WorkbenchLayoutManager] Toolbar build aborted: client config unavailable");
        return;
    }

    if (m_configDrivenLayoutBuilt)
    {
        return;
    }

    // 分发器尚未注入 → 推迟构建。
    // 主窗口构造期（initializeToolBarSkeleton）还没有工作台，命令目录无从查询；
    // 此时若硬着头皮构建，每个动作都会被判定为"命令未注册"而永久禁用。
    // WorkbenchWindow::setWorkbench 注入分发器后会再调一次本函数。
    if (!m_commandDispatcher)
    {
        SY_DEBUG("[WorkbenchLayoutManager] Toolbar build deferred: command dispatcher not injected yet");
        return;
    }

    const UiConfigData* config = m_configManager->configData();
    if (!config)
    {
        SY_ERROR("[WorkbenchLayoutManager] Toolbar build aborted: config data is null");
        return;
    }

    UiLayoutBuilder builder(m_parent, m_commandDispatcher, m_panelRegistry.get());
    builder.buildToolBars(config->toolBars);

    for (QToolBar* tb : builder.builtToolBars())
    {
        if (tb)
        {
            m_registeredToolBars.push_back(tb);
        }
    }
    m_configDrivenLayoutBuilt = true;
    SY_DEBUGF("[WorkbenchLayoutManager] Tool bars built from client config: count=%lld", static_cast<long long>(m_registeredToolBars.size()));
}

void WorkbenchLayoutManager::initializeDockAreaSkeleton()
{
    // 停靠区骨架先只创建左右容器，不在这里挂接具体工作台面板
    buildDockAreas();
}

void WorkbenchLayoutManager::buildDockAreas()
{
    // 配置驱动是唯一路径。加载失败时 UiClientContext::configResourcePath()
    // 已经把资源回退到默认客户配置，因此这里失败意味着连默认配置也不可用，
    // 属于打包缺失级别的问题，必须以 ERROR 暴露而不是静默降级。
    if (buildDockAreasFromConfig())
    {
        SY_DEBUGF("[WorkbenchLayoutManager] Dock areas built from client config: count=%lld", static_cast<long long>(m_registeredDocks.size()));
        return;
    }

    SY_ERROR("[WorkbenchLayoutManager] error code=ui.dock_config_build_failed message=Failed to build dock areas from "
             "client config; check that configs.qrc is packaged and the client JSON declares 'docks'");
}

bool WorkbenchLayoutManager::ensureConfigLoaded()
{
    if (m_configManager && m_panelRegistry)
    {
        return m_configManager->configData() != nullptr;
    }

    // 客户配置取自进程级共享实例（P0-1）：与 WorkbenchMenuManager、右键菜单服务
    // 消费同一份 UiConfigData，客户 ID 由 UiClientContext 在运行时统一解析。
    // 本类只借用指针，不拥有生命周期。
    m_configManager = &UiConfigurationManager::shared();

    m_panelRegistry = std::make_unique<UiPanelRegistry>();
    // 内置面板/状态栏槽位工厂集中注册，避免与菜单侧注册表内容漂移
    registerBuiltinUiPanels(*m_panelRegistry);

    if (!m_configManager->configData())
    {
        SY_ERRORF("[WorkbenchLayoutManager] Shared client config unavailable (client='%s')",
            qPrintable(UiClientContext::instance().clientId()));
        return false;
    }

    SY_DEBUGF("[WorkbenchLayoutManager] Client config ready: client='%s'",
        qPrintable(UiClientContext::instance().clientId()));
    return true;
}

bool WorkbenchLayoutManager::buildDockAreasFromConfig()
{
    if (!ensureConfigLoaded())
    {
        return false;
    }

    const UiConfigData* config = m_configManager->configData();
    if (!config)
    {
        return false;
    }

    // 数据驱动构建 Dock（命令分发器此处不参与，仅为构造签名提供空实现）
    NullDispatcher dispatcher;
    UiLayoutBuilder builder(m_parent, &dispatcher, m_panelRegistry.get());
    builder.buildDocks(config->docks);

    // 将构建出的 Dock widget 挂入布局管理器注册表，统一清理（clearLayoutContent）
    // 与布局快照（restoreLayoutSnapshot）
    for (QWidget* dockWidget : builder.builtDocks())
    {
        if (auto* dock = qobject_cast<QDockWidget*>(dockWidget))
        {
            dock->setProperty("_workbench_dock_title", dock->windowTitle());
            m_registeredDocks.push_back(dock);

            // 同步面板状态引用，保持对外接口稳定（setSkeletonDocksVisible 等依赖它）
            const QString dockId = dock->objectName();
            if (dockId == QStringLiteral("SceneDock"))
            {
                m_panelState.leftDock = dock;
            }
            else if (dockId == QStringLiteral("PropertiesDock"))
            {
                m_panelState.rightDock = dock;
            }

            if (auto* tree = qobject_cast<SceneTreePanel*>(dock->widget()))
            {
                m_panelState.sceneTreeDock = tree;
            }
            if (auto* props = qobject_cast<PropertiesPanelWidget*>(dock->widget()))
            {
                m_panelState.propertiesDock = props;
            }
        }
    }

    // 初始面板宽度：默认窄一些，不挤压中间视图；不限制最大宽度，用户可手动拖宽。
    // 仅在两个骨架 Dock 都存在时执行，配置里删掉其中一个也不会崩。
    // 右侧面板 (PropertiesDock) 宽一些，因为属性行经常有较长的值（坐标、名称等）。
    if (m_panelState.leftDock && m_panelState.rightDock)
    {
        m_parent->resizeDocks({ m_panelState.leftDock.data(), m_panelState.rightDock.data() },
            { 180, 380 }, Qt::Horizontal);
    }

    return !m_registeredDocks.empty();
}

void WorkbenchLayoutManager::initializeStatusBarSkeleton()
{
    // 状态栏骨架只承载全局状态展示，不提前填入业务语义
    buildStatusBar();
}

void WorkbenchLayoutManager::buildStatusBar()
{
    // 框架级状态栏：容器始终存在，不随工作台切换而销毁。
    // 槽位内容（客户标识、授权状态等跨工作台恒定信息）由客户 JSON 的 statusBar 节声明；
    // 工作台级内容（坐标/选择/消息）仍由 StatusBarBase 子类通过
    // WorkbenchWindow::mountStatusBar / unmountStatusBar 挂载，两者互不干扰。
    m_panelState.statusBar = m_parent->statusBar();

    if (!ensureConfigLoaded())
    {
        SY_WARN("[WorkbenchLayoutManager] Status bar slots skipped: client config unavailable");
        return;
    }

    const UiConfigData* config = m_configManager->configData();
    if (!config)
    {
        return;
    }

    // 重建前先回收上一轮槽位，避免工作台反复切换时状态栏堆积重复控件
    clearStatusBarSlots();

    NullDispatcher dispatcher;
    UiLayoutBuilder builder(m_parent, &dispatcher, m_panelRegistry.get());
    builder.buildStatusBar(config->statusBar);
    for (QWidget* slot : builder.builtStatusBarSlots())
    {
        if (slot)
        {
            m_statusBarSlots.emplace_back(slot);
        }
    }
}

void WorkbenchLayoutManager::clearStatusBarSlots()
{
    QStatusBar* bar = m_panelState.statusBar;
    for (auto& slot : m_statusBarSlots)
    {
        if (!slot)
        {
            continue;
        }
        if (bar)
        {
            // removeWidget 只是取消托管，控件仍需显式销毁
            bar->removeWidget(slot);
        }
        slot->deleteLater();
    }
    m_statusBarSlots.clear();
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

    // 设置 objectName 以便后续 setSkeletonDocksVisible / setSceneDockVisible 等方法能正确识别
    // 根据 widget 类型推断 dock id
    QString dockId;
    if (qobject_cast<SceneTreePanel*>(widget))
    {
        dockId = QStringLiteral("SceneDock");
    }
    else if (qobject_cast<PropertiesPanelWidget*>(widget))
    {
        dockId = QStringLiteral("PropertiesDock");
    }
    else
    {
        dockId = title;
    }
    dock->setObjectName(dockId);

    dock->setWidget(widget);
    m_parent->addDockWidget(area, dock);
    m_registeredDocks.push_back(dock);

    // 保存标题到 dock widget 的属性中，以便 restoreLayoutSnapshot 后重新设置
    // restoreState() 会覆盖 dock 标题，需要在恢复后重新设置
    dock->setProperty("_workbench_dock_title", title);

    // 同步面板状态引用，保持对外接口稳定（setSkeletonDocksVisible 等依赖它）
    if (dockId == QStringLiteral("SceneDock"))
    {
        m_panelState.leftDock = dock;
    }
    else if (dockId == QStringLiteral("PropertiesDock"))
    {
        m_panelState.rightDock = dock;
    }

    // 仅更新面板状态引用，方便后续统一刷新与清理
    if (auto* tree = qobject_cast<SceneTreePanel*>(widget))
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

void WorkbenchLayoutManager::clearLayoutContent(const UiWorkbench* oldWorkbench)
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
        // QMenuBar::clear() 只摘掉顶层 QAction，菜单本体（QMenu 及其整棵子树）依然
        // 作为 QMainWindow 的子对象存活。每切一次工作台就再堆一棵完整菜单树，
        // 快捷键与 objectName 也会撞车。因此先记下各顶层 menu()，clear 后统一销毁。
        QList<QMenu*> staleMenus;
        for (auto* act : actions)
        {
            if (act)
            {
                if (QMenu* sub = act->menu())
                {
                    staleMenus.append(sub);
                }
                act->disconnect();
            }
        }
        mb->clear();
        for (QMenu* sub : staleMenus)
        {
            // 本函数可能由菜单项自身的 triggered 回调触发（语言切换、工作台切换），
            // 此时 QMenu 还在调用栈上，只能延后删除；同时脱离父对象，避免 findChild
            // 在删除生效前捞到僵尸菜单。
            sub->setParent(nullptr);
            sub->deleteLater();
        }
    }

    // 3: 清理所有停靠面板
    for (auto* dock : m_registeredDocks)
    {
        m_parent->removeDockWidget(dock);
        delete dock;
    }
    m_registeredDocks.clear();

    // 不需要手工置空 m_panelState 里的 Dock 指针：它们是 QPointer，
    // 上面的 delete 已经让它们自动变成 null（见 PanelState 的注释）。

    // 3.1: 清理配置驱动的状态栏槽位。
    // 容器（QStatusBar）保留，只回收槽位控件，否则工作台反复切换会堆积重复标签。
    clearStatusBarSlots();

    // 工具栏/Dock 已全部销毁，下一次 buildToolBars 必须重新构建
    m_configDrivenLayoutBuilt = false;


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
    //
    // 「怎么释放」交给旧工作台的 releaseCentralWidgetGLResources：
    // 布局管理器是框架层，不该认识 RenderViewport2D / Viewport3D 这些具体类型。
    // 这个虚接口一直存在（UiWorkbench.h），只是此前被这里的 qobject_cast 绕过，
    // 三份实现全是死代码。
    auto* oldCentral = m_parent->centralWidget();
    if (oldCentral)
    {
        SY_DEBUG("[clearLayoutContent] Step A: releasing GL resources");
        if (oldWorkbench)
        {
            oldWorkbench->releaseCentralWidgetGLResources(oldCentral);
        }
        else
        {
            SY_WARN("[clearLayoutContent] 旧工作台为空，跳过中央视口 GL 释放");
        }

        SY_DEBUG("[clearLayoutContent] Step B: setCentralWidget(nullptr)");
        m_parent->setCentralWidget(nullptr);

        SY_DEBUG("[clearLayoutContent] Step C: hide + deleteLater");
        oldCentral->hide();
        oldCentral->deleteLater();
    }
    SY_DEBUG("[clearLayoutContent] Step D: creating placeholder");
    m_parent->setCentralWidget(createInitialCentralWidget());
    SY_DEBUG("[clearLayoutContent] Step E: done");
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
        SY_DEBUGF("[WorkbenchLayoutManager] Saved layout snapshot to database: %s", rec.workbenchId.c_str());
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
            SY_DEBUGF("[WorkbenchLayoutManager] Loaded layout snapshot from database: %s", rec.workbenchId.c_str());
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

void WorkbenchLayoutManager::setSceneDockVisible(bool visible)
{
    // 查找 SceneDock
    QDockWidget* sceneDock = m_panelState.leftDock.data();
    if (!sceneDock)
    {
        for (auto* dock : m_registeredDocks)
        {
            if (dock && dock->objectName() == QStringLiteral("SceneDock"))
            {
                sceneDock = dock;
                m_panelState.leftDock = dock;
                break;
            }
        }
    }

    if (sceneDock)
    {
        // 设置宽度限制（与 2D 模式一致）
        sceneDock->setMinimumWidth(180);
        sceneDock->setMaximumWidth(300);
        sceneDock->setVisible(visible);
    }
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
