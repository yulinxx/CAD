#include "WorkbenchWindow.h"

#include <QAction>
#include <QDockWidget>
#include <QLabel>
#include <QGuiApplication>
#include <QMenuBar>
#include <QProgressBar>
#include <QScreen>
#include <QSettings>
#include <QStatusBar>
#include <QToolBar>
#include <QWidget>

#include <chrono>

#include "VersionInfo.h"
#include "UiLayoutService.h"
#include "UiStateCenter.h"
#include "UiThemeService.h"
#include "UiWorkbench.h"
#include "UiViewWidgets.h"
#include "UiCommandDispatcher.h"

/// 初始化主窗口组件
/// @param parent 父部件
WorkbenchWindow::WorkbenchWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle(QString::fromStdString(MainApp::appName()));
    resize(1440, 900);

    // 默认窗口先居中显示，避免双屏环境下直接落到某个屏幕边缘甚至只露出一部分。
    if (const auto* screen = QGuiApplication::primaryScreen())
    {
        const auto available = screen->availableGeometry();
        const int x = available.x() + (available.width() - width()) / 2;
        const int y = available.y() + (available.height() - height()) / 2;
        move(x, y);
    }

    initializeWorkbenchShell();
}

WorkbenchWindow::~WorkbenchWindow() = default;

/// 设置状态中心
/// @param stateCenter UI 状态中心
void WorkbenchWindow::setUiStateCenter(UiStateCenter* stateCenter)
{
    // 状态中心入口只负责替换源头引用，不在这里做额外状态编排
    unbindStateSignals();
    m_stateCenter = stateCenter;
    m_uiServices.stateCenter = stateCenter;
    bindStateSignals();
}

/// 设置主题服务
/// @param themeService 主题服务
void WorkbenchWindow::setThemeService(UiThemeService* themeService)
{
    // 主题服务入口只替换引用，不在这里主动触发主题加载或界面刷新
    m_themeService = themeService;
    m_uiServices.themeService = themeService;
}

/// 设置命令分发器
/// @param dispatcher 命令分发器
void WorkbenchWindow::setCommandDispatcher(UiCommandDispatcher* dispatcher)
{
    m_commandDispatcher = dispatcher;
    m_uiServices.commandDispatcher = dispatcher;
}

/// 设置撤销栈
/// @param undoStack 撤销栈
void WorkbenchWindow::setUndoStack(IUndoStack* undoStack)
{
    m_undoStack = undoStack;
    m_uiServices.undoStack = undoStack;
}

void WorkbenchWindow::setFrameworkServices(const UiFrameworkServices& services)
{
    // 框架级能力统一从这里注入，后续错误、权限、性能都必须走同一条框架路径
    // 这里仅更新桥接对象，不主动触发任何 UI 刷新或工作台切换
    m_frameworkServices = services;
    // 框架服务更新后不做自动回放，避免入口函数产生隐式副作用
}

void WorkbenchWindow::setUiServices(const UiServices& services)
{
    // UI 服务集合入口只负责转交统一装配流程，避免出现两套依赖装配逻辑
    // 这里不做额外装配分支，保持入口单一
    configureServices(services);
}

void WorkbenchWindow::configureServices(const UiServices& services)
{
    // 统一从服务集合装配依赖，避免窗口层各处手工拼接依赖关系导致框架发散
    // 这里是 UI 服务接入的唯一主入口，后续扩展也应优先回到这里
    unbindStateSignals();
    m_uiServices = services;
    m_stateCenter = services.stateCenter;
    m_themeService = services.themeService;
    if (services.commandDispatcher)
        services.commandDispatcher->setFrameworkServices(m_frameworkServices);

    // 服务重装载后只恢复信号绑定，不在这里触发额外刷新，避免配置入口带副作用
    bindStateSignals();
}

/// 设置当前工作台
/// @param workbench 工作台实例
void WorkbenchWindow::setWorkbench(UiWorkbench* workbench)
{
    // 当前工作台只保存轻量引用，不在这里触发任何装配或切换动作
    m_workbench = workbench;
}

/// 设置工作台切换工厂
/// @param factory 按 ID 返回工作台实例的回调
void WorkbenchWindow::setWorkbenchFactory(WorkbenchFactory factory)
{
    m_workbenchFactory = std::move(factory);
}

/// 绑定状态中心信号
void WorkbenchWindow::bindStateSignals()
{
    // 只绑定状态中心信号，不在这里做任何状态初始化或业务编排
    if (!m_stateCenter)
        return;

    // 信号连接只用于刷新入口，不在这里插入额外的状态派生逻辑
    connect(m_stateCenter, &UiStateCenter::stateChanged, this, [this]() { refreshFromState(); });
    connect(m_stateCenter, &UiStateCenter::busyChanged, this, [this](bool) { refreshFromState(); });
    connect(m_stateCenter, &UiStateCenter::dirtyChanged, this, [this](bool) { refreshFromState(); });
}

void WorkbenchWindow::unbindStateSignals()
{
    // 解绑只做信号断开，避免把清理逻辑混进来
    if (!m_stateCenter)
        return;

    disconnect(m_stateCenter, nullptr, this, nullptr);
}

/// 初始化工作台窗口骨架
void WorkbenchWindow::initializeWorkbenchShell()
{
    // 按固定顺序装配主窗口骨架，避免初始化顺序隐式耦合
    // 这里必须保持“先骨架、后业务”的框架原则，不能把工作台逻辑提前塞进来
    createBaseMenus();
    initializeToolBarSkeleton();
    initializeDockAreaSkeleton();
    initializeStatusBarSkeleton();
    initializeThemeMenuSkeleton();
    initializeWorkbenchMenuSkeleton();
    bindStateSignals();
    bindShortcuts();
    setCentralWidget(createInitialCentralWidget());
    updateWindowTitle();
    refreshStatusText();
}

void WorkbenchWindow::createBaseMenus()
{
    // 先搭菜单骨架，再逐步填充具体菜单项
    // 这里不要直接塞业务命令，避免菜单层和工作台层耦合过早
    initializeMenuSkeleton();
}

void WorkbenchWindow::initializeMenuSkeleton()
{
    // 菜单骨架先只建立顶层容器，避免初始化阶段引入具体菜单逻辑
    buildMenus();
}

QWidget* WorkbenchWindow::createInitialCentralWidget()
{
    auto* widget = new QWidget(this);
    widget->setObjectName(QStringLiteral("WorkbenchCentralPlaceholder"));
    return widget;
}

void WorkbenchWindow::buildMenus()
{
    // 顶层菜单只负责建立菜单容器，不在这里混入业务逻辑
    m_menuState.fileMenu = menuBar()->addMenu(QStringLiteral("File"));
    m_menuState.viewMenu = menuBar()->addMenu(QStringLiteral("View"));
    m_menuState.toolsMenu = menuBar()->addMenu(QStringLiteral("Tools"));
}

/// 构建工具栏
void WorkbenchWindow::initializeToolBarSkeleton()
{
    // 工具栏骨架只创建承载容器，不在初始化阶段绑定具体动作
    buildToolBars();
}

void WorkbenchWindow::buildToolBars()
{
    // 主工具栏只负责提供基础承载，不在这里绑定具体动作
    // 这里保持空骨架，避免工具栏直接承担工作台业务编排
    m_panelState.mainToolBar = addToolBar(QStringLiteral("Main"));
    m_panelState.mainToolBar->setObjectName(QStringLiteral("MainToolBar"));
    m_panelState.mainToolBar->setMovable(true);
}

void WorkbenchWindow::bindShortcuts()
{
    auto* undoAction = new QAction(QStringLiteral("Undo"), this);
    undoAction->setShortcut(QKeySequence::Undo);
    connect(undoAction, &QAction::triggered, this, [this]() {
        if (m_commandDispatcher)
            m_commandDispatcher->undo();
    });
    addAction(undoAction);

    auto* redoAction = new QAction(QStringLiteral("Redo"), this);
    redoAction->setShortcut(QKeySequence::Redo);
    connect(redoAction, &QAction::triggered, this, [this]() {
        if (m_commandDispatcher)
            m_commandDispatcher->redo();
    });
    addAction(redoAction);
}

/// 构建停靠区域（左侧项目面板、右侧属性面板）
void WorkbenchWindow::initializeDockAreaSkeleton()
{
    // 停靠区骨架先只创建左右容器，不在这里挂接具体工作台面板
    buildDockAreas();
}

void WorkbenchWindow::buildDockAreas()
{
    // 场景树面板
    m_panelState.sceneTreeDock = new SceneTreeDockWidget(this);
    m_panelState.leftDock = new QDockWidget(QStringLiteral("Scene"), this);
    m_panelState.leftDock->setObjectName(QStringLiteral("SceneDock"));
    m_panelState.leftDock->setWidget(m_panelState.sceneTreeDock);
    addDockWidget(Qt::LeftDockWidgetArea, m_panelState.leftDock);

    // 属性面板
    m_panelState.propertiesDock = new PropertiesPanelWidget(this);
    m_panelState.rightDock = new QDockWidget(QStringLiteral("Properties"), this);
    m_panelState.rightDock->setObjectName(QStringLiteral("PropertiesDock"));
    m_panelState.rightDock->setWidget(m_panelState.propertiesDock);
    addDockWidget(Qt::RightDockWidgetArea, m_panelState.rightDock);
}

/// 构建状态栏
void WorkbenchWindow::initializeStatusBarSkeleton()
{
    // 状态栏骨架只承载全局状态展示，不提前填入业务语义
    buildStatusBar();
}

void WorkbenchWindow::buildStatusBar()
{
    // 状态栏只承载全局状态展示，不直接承载工作台业务逻辑
    // 这里要防止状态栏继续膨胀成“全局业务面板”
    m_panelState.statusBar = statusBar();
    m_panelState.workbenchLabel = new QLabel(this);
    m_panelState.busyLabel = new QLabel(this);
    m_panelState.statusBar->addPermanentWidget(m_panelState.workbenchLabel, 1);
    m_panelState.statusBar->addPermanentWidget(m_panelState.busyLabel);
}

/// 构建主题菜单
void WorkbenchWindow::initializeThemeMenuSkeleton()
{
    // 主题菜单骨架先建立入口，主题策略和加载逻辑留给后续流程
    buildThemeMenu();
}

void WorkbenchWindow::buildThemeMenu()
{
    // 主题菜单挂在工具菜单下，保持入口层次清晰
    // 这里仅负责入口，不处理主题加载策略，避免框架职责变宽
    m_menuState.themeMenu = m_menuState.toolsMenu->addMenu(QStringLiteral("Theme"));

    const auto addThemeAction = [this](const QString& text, const QString& themeId) {
        QAction* action = m_menuState.themeMenu->addAction(text);
        action->setCheckable(true);
        connect(action, &QAction::triggered, this, [this, themeId]() {
            triggerTheme(themeId);
            });
        };

    addThemeAction(QStringLiteral("System"), QStringLiteral("system"));
    addThemeAction(QStringLiteral("Light"), QStringLiteral("light"));
    addThemeAction(QStringLiteral("Dark"), QStringLiteral("dark"));
    addThemeAction(QStringLiteral("Blue"), QStringLiteral("blue"));
}

/// 构建工作台切换菜单
void WorkbenchWindow::initializeWorkbenchMenuSkeleton()
{
    // 工作台入口先挂在视图菜单下，具体切换行为交给工作台链处理
    buildWorkbenchMenu();
}

void WorkbenchWindow::buildWorkbenchMenu()
{
    // 工作台切换入口挂在视图菜单下，统一处理工作台切换
    // 这里只负责入口挂载，不直接碰工作台生命周期，避免职责越界
    m_menuState.workbenchMenu = m_menuState.viewMenu->addMenu(QStringLiteral("Workbench"));

    const auto addWorkbenchAction = [this](const QString& text, const QString& workbenchId) {
        QAction* action = m_menuState.workbenchMenu->addAction(text);
        action->setCheckable(true);
        connect(action, &QAction::triggered, this, [this, workbenchId]() {
            triggerWorkbench(workbenchId);
            });
        };

    addWorkbenchAction(QStringLiteral("2D"), QStringLiteral("2D"));
    addWorkbenchAction(QStringLiteral("3D"), QStringLiteral("3D"));
}

/// 同步窗口本地状态与状态中心，避免本地状态与全局状态漂移
void WorkbenchWindow::syncWindowStateFromStateCenter()
{
    if (!m_stateCenter)
        return;

    const auto state = m_stateCenter->snapshot();
    // 以状态中心为准同步窗口层状态，避免窗口本地状态与全局状态漂移
    m_windowState.workbenchId = state.currentWorkbenchId;
    m_windowState.themeId = state.currentThemeId;
    m_windowState.busy = state.busy;
    // 本地状态同步只做镜像，不在这里引入额外判断，避免状态边界再次膨胀
}

void WorkbenchWindow::syncWorkbenchSelectionFromStateCenter()
{
    if (!m_stateCenter)
        return;

    const auto state = m_stateCenter->snapshot();
    // 选择上下文单独同步，避免刷新状态栏时把选择语义和窗口语义混在一起
    m_windowState.selectionText = state.currentSelectionText;
    m_windowState.selectionSource = state.currentSelectionSource;
    m_windowState.selectionType = state.currentSelectionType;
    // 这里不做任何展示更新，只更新本地镜像供幂等分支使用
}

void WorkbenchWindow::refreshStatusText()
{
    // 这里只刷新全局状态展示，不在此处拼接工作台业务流程
    // 如果后续状态栏内容继续变复杂，应该继续往状态对象里收，而不是把窗口层写厚
    // 这里不做状态写入，只做展示更新，状态写入由同步函数统一负责
    // 这里是展示层刷新，不是状态编排层，边界必须保持清晰
    if (m_stateCenter)
    {
        syncWindowStateFromStateCenter();
        const auto state = m_stateCenter->snapshot();

        if (m_panelState.workbenchLabel)
            m_panelState.workbenchLabel->setText(QStringLiteral("WB:%1 | Doc:%2 | Cmd:%3(%4) | Layer:%5 | View:%6 | Dirty:%7")
                .arg(state.currentWorkbenchId)
                .arg(state.currentDocumentId)
                .arg(state.currentCommandId)
                .arg(state.currentCommandPhase)
                .arg(state.currentLayerId)
                .arg(state.currentViewMode)
                .arg(state.dirty ? QStringLiteral("Y") : QStringLiteral("N")));

        if (m_panelState.busyLabel)
            m_panelState.busyLabel->setText(state.busy ? QStringLiteral("Busy") : QStringLiteral("Idle"));

        // 这里仍然只做展示，不把状态写回状态中心，避免循环同步
        updateBusyIndicator(state.busy);
        updateWindowTitle();
        return;
    }

    if (m_panelState.workbenchLabel)
        m_panelState.workbenchLabel->setText(QStringLiteral("Workbench: %1").arg(m_windowState.workbenchId));
    if (m_panelState.busyLabel)
        m_panelState.busyLabel->setText(m_windowState.busy ? QStringLiteral("Busy") : QStringLiteral("Idle"));
    updateBusyIndicator(m_windowState.busy);
    updateWindowTitle();
}

/// 从状态中心刷新界面
void WorkbenchWindow::refreshFromState()
{
    const auto start = std::chrono::steady_clock::now();
    // 先同步状态，再刷新各个 UI 片段，避免展示层拿到过期数据
    // 这里是框架层的总刷新入口，不把工作台实现逻辑写进来
    // 这里是“状态同步 + 视图刷新”的总入口，不要把业务逻辑再塞进来
    syncWindowStateFromStateCenter();
    syncWorkbenchSelectionFromStateCenter();
    // 刷新状态栏前先同步本地镜像，避免展示时读到半更新状态
    // 这里不直接读写业务状态，保持“先同步、后展示”的总顺序
    refreshStatusText();
    // 属性面板使用状态中心快照作为输入，不在这里额外拼接窗口本地状态
    // 这里也不读取窗口本地镜像，避免展示层继续依赖两套来源
    if (m_panelState.propertiesDock && m_stateCenter)
    {
        const auto state = m_stateCenter->snapshot();
        // 属性面板同样以状态中心为准，避免单独维护一套展示状态
        m_panelState.propertiesDock->setStateText(QStringLiteral("WB=%1 | View=%2 | Cmd=%3(%4) | Dirty=%5 | Layer=%6 | Doc=%7 | Busy=%8")
            .arg(state.currentWorkbenchId)
            .arg(state.currentViewMode)
            .arg(state.currentCommandId)
            .arg(state.currentCommandPhase)
            .arg(state.dirty ? QStringLiteral("Y") : QStringLiteral("N"))
            .arg(state.currentLayerId)
            .arg(state.currentDocumentId)
            .arg(state.busy ? QStringLiteral("Y") : QStringLiteral("N")));

        QString selectionText = QStringLiteral("Sel=%1 | SelSrc=%2 | CmdSrc=%3 | SelType=%4 | CmdType=%5")
            .arg(state.currentSelectionText)
            .arg(state.currentSelectionSource)
            .arg(state.currentCommandOwner)
            .arg(state.currentSelectionType)
            .arg(state.currentCommandType);
        if (state.currentWorkbenchId.compare(QStringLiteral("3D"), Qt::CaseInsensitive) == 0)
        {
            selectionText = QStringLiteral("3D Sel=%1 | NodeType=%2 | CmdSrc=%3 | CmdType=%4")
                .arg(state.currentSelectionText)
                .arg(state.currentSelectionType)
                .arg(state.currentCommandOwner)
                .arg(state.currentCommandType);
        }
        else if (state.currentWorkbenchId.compare(QStringLiteral("2D"), Qt::CaseInsensitive) == 0)
        {
            selectionText = QStringLiteral("2D Sel=%1 | SelType=%2 | CmdSrc=%3 | CmdType=%4")
                .arg(state.currentSelectionText)
                .arg(state.currentSelectionType)
                .arg(state.currentCommandOwner)
                .arg(state.currentCommandType);
        }
        m_panelState.propertiesDock->setSelectionText(selectionText);
        // 属性面板的选择文本同样只走状态中心快照，不拼窗口本地镜像
    }

    recordPerformance(QStringLiteral("WorkbenchWindow::refreshFromState"),
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count());
}

void WorkbenchWindow::updateWindowTitle()
{
    // 窗口标题只表达当前工作台与视图模式，不在这里混入其他 UI 状态
    if (m_stateCenter)
    {
        const auto state = m_stateCenter->snapshot();
        // 窗口标题统一从状态中心生成，避免不同路径拼出不同标题格式
        // 如果后续需要扩展标题信息，优先扩展状态中心快照字段，不要在这里直接拼业务状态
        setWindowTitle(QString("%1 - %2 - %3").arg(QString::fromStdString(MainApp::appName()), state.currentWorkbenchId, state.currentViewMode));
        return;
    }

    setWindowTitle(QString("%1 - %2").arg(QString::fromStdString(MainApp::appName()), m_windowState.workbenchId));
}

/// 应用样式表
/// @param styleSheet 样式表内容
void WorkbenchWindow::applyTheme(const QString& styleSheet)
{
    // 样式应用只负责设置样式表，不在这里触发主题逻辑或工作台逻辑
    setStyleSheet(styleSheet);
}

/// 设置主题切换回调
/// @param callback 主题切换回调函数
void WorkbenchWindow::setThemeChangeCallback(std::function<void(const QString&)> callback)
{
    // 主题回调只作为外部扩展点，不在这里绑定额外业务行为
    // 回调一旦注入，就应被视为主题链的唯一扩展入口之一
    // 这里不触发立即执行，避免回调设置阶段产生副作用
    m_themeChangeCallback = std::move(callback);
}

/// 注册停靠面板
/// @param title 面板标题
/// @param widget 面板内容部件
/// @param area 停靠区域
QDockWidget* WorkbenchWindow::registerDockWidget(const QString& title, QWidget* widget, Qt::DockWidgetArea area)
{
    // 注册停靠面板只负责把面板挂到指定区域，不在这里注入业务行为
    auto* dock = new QDockWidget(title, this);
    dock->setObjectName(title);
    dock->setWidget(widget);
    addDockWidget(area, dock);
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

/// 注册工具栏
/// @param title 工具栏标题
QToolBar* WorkbenchWindow::registerToolBar(const QString& title)
{
    // 注册工具栏只负责挂载承载容器，不在这里填入具体动作
    auto* toolBar = addToolBar(title);
    toolBar->setObjectName(title);
    m_registeredToolBars.push_back(toolBar);
    return toolBar;
}

/// 清空工作台内容（移除所有注册的面板和工具栏）
void WorkbenchWindow::clearWorkbenchContent()
{
    // 清空工作台内容时只做容器层收尾，不在这里恢复业务状态
    const auto start = std::chrono::steady_clock::now();

    for (auto* dock : m_registeredDocks)
        removeDockWidget(dock);
    m_registeredDocks.clear();

    for (auto* toolBar : m_registeredToolBars)
        removeToolBar(toolBar);
    m_registeredToolBars.clear();

    m_panelState.sceneTreeDock = nullptr;
    m_panelState.propertiesDock = nullptr;
    setCentralWidget(createInitialCentralWidget());

    recordPerformance(QStringLiteral("WorkbenchWindow::clearWorkbenchContent"),
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count());
}

void WorkbenchWindow::resetCommandStateToIdle()
{
    if (!m_stateCenter)
        return;

    // 统一把命令状态清回 idle，避免切换和收尾流程各自写一套
    // 这里不要顺手做别的收尾动作，保持职责单一
    m_stateCenter->setCurrentCommandId(QStringLiteral("idle"));
    m_stateCenter->setCurrentCommandPhase(QStringLiteral("idle"));
    m_stateCenter->setCurrentCommandOwner(QStringLiteral("none"));
    m_stateCenter->setCurrentCommandType(QStringLiteral("none"));
}

void WorkbenchWindow::resetWorkbenchLocalMirror()
{
    // 本地镜像只做清空，不向状态中心写额外语义
    m_windowState.busy = false;
    m_windowState.workbenchId = QStringLiteral("default");
    m_windowState.themeId = QStringLiteral("system");
    m_windowState.selectionText.clear();
    m_windowState.selectionSource.clear();
    m_windowState.selectionType.clear();
}

void WorkbenchWindow::clearSelectionState()
{
    if (!m_stateCenter)
        return;

    // 清空选择相关状态，避免工作台切换后沿用旧选择文本
    m_stateCenter->setCurrentSelectionText(QString());
    m_stateCenter->setSelectionContext(QStringLiteral("none"), QString());
    m_stateCenter->setMetadata({
        { QStringLiteral("selectionSource"), QStringLiteral("none") },
        { QStringLiteral("selectionText"), QString() },
        { QStringLiteral("selectionType"), QStringLiteral("none") }
    });
}

void WorkbenchWindow::setWorkbenchSwitchContext(const QString& workbenchId, const QString& switchContextText)
{
    if (!m_stateCenter)
        return;

    // 工作台切换上下文统一在这里写入，避免 triggerWorkbench 里散落重复设置
    // 这里只写切换语义，不混入命令态和主题态
    m_stateCenter->setCurrentWorkbenchId(workbenchId);
    m_stateCenter->setSelectionContext(QStringLiteral("Workbench-Switch"), switchContextText);
    m_stateCenter->setMetadata({
        { QStringLiteral("selectionSource"), QStringLiteral("Workbench-Switch") },
        { QStringLiteral("selectionText"), switchContextText },
        { QStringLiteral("selectionType"), QStringLiteral("none") }
    });
}

void WorkbenchWindow::resetWorkbenchTransientState()
{
    const auto start = std::chrono::steady_clock::now();
    m_windowState.busy = false;

    if (m_stateCenter)
    {
        // 工作台切换收尾只做“清空/归零”，不在这里引入新的状态来源
        m_stateCenter->setBusy(false);
        resetCommandStateToIdle();
        m_stateCenter->setMetadata({
            { QStringLiteral("workbenchId"), QStringLiteral("none") },
            { QStringLiteral("commandType"), QStringLiteral("none") },
            { QStringLiteral("commandState"), QStringLiteral("idle") },
            { QStringLiteral("selectionSource"), QStringLiteral("none") },
            { QStringLiteral("selectionText"), QString() },
            { QStringLiteral("selectionType"), QStringLiteral("none") },
            { QStringLiteral("viewportStatus"), QStringLiteral("Idle") }
        });
        clearSelectionState();
        m_stateCenter->setDirty(false);
    }
    // 本地镜像收尾单独处理，避免状态中心清理和窗口镜像清理混在一起
    resetWorkbenchLocalMirror();

    recordPerformance(QStringLiteral("WorkbenchWindow::resetWorkbenchTransientState"),
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count());
}

void WorkbenchWindow::syncWorkbenchStateFromStateCenter()
{
    if (!m_stateCenter)
        return;

    const auto start = std::chrono::steady_clock::now();
    // 这里只同步菜单选中状态，不在这里做任何重建或业务切换动作
    const auto state = m_stateCenter->snapshot();
    refreshWorkbenchMenuChecks(state.currentWorkbenchId);
    refreshThemeMenuChecks(state.currentThemeId);
    recordPerformance(QStringLiteral("WorkbenchWindow::syncWorkbenchStateFromStateCenter"),
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count());
}

/// 保存布局快照
/// @param workbenchId 工作台 ID
void WorkbenchWindow::saveLayoutSnapshot(const QString& workbenchId)
{
    // 布局快照只保存窗口外观，不保存业务状态；业务状态由状态中心负责
    // 这里不应该引入任何额外状态来源，避免快照与状态中心分叉
    if (workbenchId.isEmpty())
        return;

    const auto start = std::chrono::steady_clock::now();
    QSettings settings;
    settings.beginGroup(QStringLiteral("LayoutSnapshots"));
    settings.setValue(workbenchId + QStringLiteral("/geometry"), saveGeometry());
    settings.setValue(workbenchId + QStringLiteral("/windowState"), saveState());
    settings.endGroup();
    recordPerformance(QStringLiteral("WorkbenchWindow::saveLayoutSnapshot"),
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count());
}

/// 恢复布局快照
/// @param workbenchId 工作台 ID
void WorkbenchWindow::restoreLayoutSnapshot(const QString& workbenchId)
{
    // 布局恢复只还原窗口外观，不在这里恢复业务状态，避免状态源不统一
    // 注意：不恢复窗口几何尺寸（geometry），保持当前窗口位置和大小不变
    // 只恢复 dock 布局状态（windowState），但会被后续重新设置的 dock 标题覆盖
    if (workbenchId.isEmpty())
        return;

    const auto start = std::chrono::steady_clock::now();
    QSettings settings;
    settings.beginGroup(QStringLiteral("LayoutSnapshots"));
    const auto state = settings.value(workbenchId + QStringLiteral("/windowState")).toByteArray();
    settings.endGroup();

    if (!state.isEmpty())
        restoreState(state);

    // restoreState() 会覆盖 dock widget 的标题，需要重新设置为当前工作台的标题
    restoreDockWidgetTitles();

    recordPerformance(QStringLiteral("WorkbenchWindow::restoreLayoutSnapshot"),
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count());
}

/// 重新设置所有注册的 dock widget 的标题
/// restoreState() 会覆盖 dock 标题，需要在恢复后调用此方法
void WorkbenchWindow::restoreDockWidgetTitles()
{
    for (auto* dock : m_registeredDocks)
    {
        const auto title = dock->property("_workbench_dock_title").toString();
        if (!title.isEmpty())
            dock->setWindowTitle(title);
    }
}

/// 更新繁忙指示器
/// @param busy 是否繁忙
void WorkbenchWindow::updateBusyIndicator(bool busy)
{
    // 繁忙指示器只表达忙闲状态，不承载命令细节或工作台切换细节
    // 这里是状态栏的局部 UI 更新点，不要把切换流程或命令流程塞进来
    // 工业级要求：UI 指示和业务状态必须分层，不能互相污染
    if (!m_panelState.statusBar)
        return;

    if (busy)
    {
        if (!m_busyProgressBar)
        {
            auto* progress = new QProgressBar(this);
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

/// 刷新主题菜单选中状态
/// @param themeId 当前主题 ID
void WorkbenchWindow::refreshThemeMenuChecks(const QString& themeId)
{
    // 主题菜单仅同步选中状态，不在这里触发主题加载
    // 这里是菜单状态刷新点，不是主题执行点，职责必须分开
    m_windowState.themeId = themeId;
    updateWindowTitle();
    if (!m_menuState.themeMenu)
        return;

    for (QAction* action : m_menuState.themeMenu->actions())
    {
        if (!action->isCheckable())
            continue;
        
        action->setChecked(action->text().compare(themeId, Qt::CaseInsensitive) == 0);
    }
}

/// 刷新工作台菜单选中状态
/// @param workbenchId 当前工作台 ID
void WorkbenchWindow::refreshWorkbenchMenuChecks(const QString& workbenchId)
{
    // 工作台菜单仅同步选中状态，不在这里触发工作台切换
    // 这里是菜单状态刷新点，不是工作台执行点，职责必须分开
    m_windowState.workbenchId = workbenchId;
    refreshStatusText();
    if (!m_menuState.workbenchMenu)
        return;

    for (QAction* action : m_menuState.workbenchMenu->actions())
    {
        if (!action->isCheckable())
            continue;
        action->setChecked(action->text().compare(workbenchId, Qt::CaseInsensitive) == 0);
    }
}

/// 触发主题切换
/// @param themeId 主题 ID
void WorkbenchWindow::triggerTheme(const QString& themeId)
{
    // 主题切换只负责调度，不在这里扩展额外 UI 行为，避免主题逻辑和工作台逻辑互相污染
    if (m_themeChangeCallback)
        m_themeChangeCallback(themeId);

    if (m_themeService && m_themeService->loadThemeFromId(themeId))
        applyTheme(m_themeService->styleSheet());
    else
    {
        // 主题加载失败时只做错误上报，不在这里尝试额外回退逻辑，避免主题链复杂化
        reportFrameworkError(QStringLiteral("theme.load_failed"), QStringLiteral("Failed to load theme %1").arg(themeId), QStringLiteral("WorkbenchWindow::triggerTheme"));
    }

    if (m_stateCenter)
    {
        m_stateCenter->setCurrentThemeId(themeId);
        // 主题状态变化后同步本地状态，避免窗口与状态中心短时间不一致
        m_windowState.themeId = themeId;
    }
    updateWindowTitle();

    refreshThemeMenuChecks(themeId);
}

void WorkbenchWindow::recordPerformance(const QString& scope, qint64 elapsedMs)
{
    if (m_frameworkServices.recordPerformance)
    {
        // 性能上报只负责透传耗时数据，不在这里做额外统计聚合，避免框架层职责膨胀
        m_frameworkServices.recordPerformance(scope, elapsedMs);
    }
}

void WorkbenchWindow::reportFrameworkError(const QString& errorCode, const QString& message, const QString& context)
{
    // 错误统一走框架通道；如果没有通道，至少落到状态中心元数据里
    if (m_frameworkServices.reportError)
        m_frameworkServices.reportError(errorCode, message, context);
    else if (m_stateCenter)
        m_stateCenter->setMetadata({
            { QStringLiteral("lastErrorCode"), errorCode },
            { QStringLiteral("lastErrorMessage"), message },
            { QStringLiteral("lastErrorContext"), context }
        });
}

bool WorkbenchWindow::canExecuteCommand(const QString& commandId, const QString& context) const
{
    // 权限检查只做判定，不在这里扩展额外策略或副作用
    if (m_frameworkServices.canExecuteCommand)
        return m_frameworkServices.canExecuteCommand(commandId, context);

    // 没有权限回调时默认放行，保持框架的最小可用性
    return true;
}

/// 触发工作台切换
/// @param workbenchId 工作台 ID
namespace
{
    // 统一工作台切换时展示的上下文文本，避免切换链中多处拼接文案
    QString workbenchSwitchText(const QString& workbenchId)
    {
        return QStringLiteral("Switching to %1").arg(workbenchId);
    }
}

void WorkbenchWindow::triggerWorkbench(const QString& workbenchId)
{
    // 工作台切换只做协调和编排，不直接承载工作台实现细节
    // 这里如果继续变厚，就会把主窗口又拉回“全能容器”的老路
    // 这里不创建工作台内部 UI，也不直接拼接工作台业务对象，统一交给工作台实现
    const auto start = std::chrono::steady_clock::now();
    if (!canExecuteCommand(QStringLiteral("workbench.switch.%1").arg(workbenchId), QStringLiteral("WorkbenchWindow::triggerWorkbench")))
    {
        reportFrameworkError(QStringLiteral("workbench.switch_denied"), QStringLiteral("Workbench switch denied: %1").arg(workbenchId), QStringLiteral("WorkbenchWindow::triggerWorkbench"));
        recordPerformance(QStringLiteral("WorkbenchWindow::triggerWorkbench.denied"),
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count());
        return;
    }

    // 没有工作台时，只同步状态并更新菜单，不进入完整切换流程
    // 这里保留轻量路径，避免空工作台场景也走完整切换链
    // 轻量路径只处理最少状态，不在这里创建临时占位工作台
    if (!m_workbench)
    {
        // 没有工作台时只做轻量状态同步和菜单刷新，避免无意义地走完整切换链
        if (m_stateCenter)
        {
            m_stateCenter->setCurrentWorkbenchId(workbenchId);
            m_stateCenter->setCurrentViewMode(QStringLiteral("none"));
            m_stateCenter->setSelectionContext(QStringLiteral("Workbench-Switch"), QStringLiteral("Ready"));
        }
        refreshWorkbenchMenuChecks(workbenchId);
        refreshFromState();
        refreshStatusText();
        updateWindowTitle();
        recordPerformance(QStringLiteral("WorkbenchWindow::triggerWorkbench.noWorkbench"),
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count());
        return;
    }

    // 同一个工作台再次点击时，不重复执行停用、清理和恢复布局
    // 这个分支是幂等保护，防止重复点击把界面状态抖乱
    if (workbenchId.compare(m_windowState.workbenchId, Qt::CaseInsensitive) == 0)
    {
        // 同工作台幂等分支：只刷新菜单和状态，不重复执行切换流程
        refreshWorkbenchMenuChecks(workbenchId);
        recordPerformance(QStringLiteral("WorkbenchWindow::triggerWorkbench.sameWorkbench"),
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count());
        return;
    }

    const auto previousWorkbenchId = m_windowState.workbenchId;
    const auto switchContextText = workbenchSwitchText(workbenchId);

    // 切换前先冻结界面状态，避免中间态被其它监听逻辑误读
    // 这里严格按“先冻结、再清理、后重建”的顺序执行，避免出现半切换状态
    if (m_stateCenter)
    {
        // 冻结阶段只负责锁定切换前状态，不在这里做任何重建动作
        saveLayoutSnapshot(previousWorkbenchId);
        m_stateCenter->setBusy(true);
        resetCommandStateToIdle();
        setWorkbenchSwitchContext(workbenchId, switchContextText);
        m_stateCenter->setMetadata({
            { QStringLiteral("commandState"), QStringLiteral("idle") },
            { QStringLiteral("commandOwner"), QStringLiteral("none") },
            { QStringLiteral("commandType"), QStringLiteral("none") },
            { QStringLiteral("selectionSource"), QStringLiteral("Workbench-Switch") },
            { QStringLiteral("selectionText"), switchContextText },
            { QStringLiteral("selectionType"), QStringLiteral("none") },
            { QStringLiteral("viewportStatus"), QStringLiteral("Switching") }
        });
    }

    // 先停用旧工作台，再清空临时 UI 容器，避免旧页面残留
    // 这里不做额外状态写入，避免停用动作和状态编排耦合
    m_workbench->deactivate();
    recordPerformance(QStringLiteral("WorkbenchWindow::triggerWorkbench.deactivate"),
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count());
    resetWorkbenchTransientState();
    clearWorkbenchContent();

    // 切换目标工作台时，提前把状态中心的工作台 ID 更新掉
    // 切换上下文本身已经在上面的冻结阶段写过一次，这里再次调用是为了确保重建阶段结束前状态仍保持一致
    setWorkbenchSwitchContext(workbenchId, switchContextText);
    // 本地工作台 ID 也在这里提前更新，后续幂等分支与状态展示都以它为准
    m_windowState.workbenchId = workbenchId;

    // 通过工厂解析目标工作台实例，支持 2D↔3D 之间切换不同的工作台对象
    // 工厂返回 nullptr 或同一实例时保持当前 m_workbench 不变
    if (m_workbenchFactory)
    {
        auto* target = m_workbenchFactory(workbenchId);
        if (target && target != m_workbench)
            m_workbench = target;
    }

    // 重新挂接新工作台、恢复布局并激活，最后再刷新菜单和状态显示
    // 这里是切换链的重建阶段，任何附加动作都应优先收敛到工作台自身实现中
    // 这里不直接操作工作台内部状态，避免主窗口越界承担工作台细节
    // 重建阶段只恢复工作台承载，不在这里额外处理选择或命令细节
    // 如果后续需要加重建前后钩子，也必须放在工作台链内部，不要散到窗口层
    m_workbench->attachToWindow(*this);
    recordPerformance(QStringLiteral("WorkbenchWindow::triggerWorkbench.attach"),
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count());
    restoreLayoutSnapshot(workbenchId);
    recordPerformance(QStringLiteral("WorkbenchWindow::triggerWorkbench.restoreLayout"),
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count());
    m_workbench->activate();
    recordPerformance(QStringLiteral("WorkbenchWindow::triggerWorkbench.activate"),
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count());
    refreshWorkbenchMenuChecks(workbenchId);
    refreshFromState();
    // 切换链完成后再补一次显式刷新，避免状态中心信号时序导致标题/状态栏停留旧值
    refreshStatusText();
    updateWindowTitle();

    if (m_stateCenter)
        m_stateCenter->setBusy(false);
    // 切换完成后同步本地忙闲状态，保证窗口侧与状态中心一致
    m_windowState.busy = false;
    // 切换完成后再次同步本地工作台 ID，避免任何中间状态沿用旧值
    m_windowState.workbenchId = workbenchId;

    recordPerformance(QStringLiteral("WorkbenchWindow::triggerWorkbench.switch"),
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count());
}
