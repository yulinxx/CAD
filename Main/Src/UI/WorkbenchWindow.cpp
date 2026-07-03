/**
 * @file WorkbenchWindow.cpp
 * @brief 工作台主窗口实现
 */

#include "WorkbenchWindow.h"

#include <QAction>
#include <QDockWidget>
#include <QLabel>
#include <QMenuBar>
#include <QProgressBar>
#include <QSettings>
#include <QStatusBar>
#include <QToolBar>
#include <QWidget>

#include "UiLayoutService.h"
#include "UiStateCenter.h"
#include "UiThemeService.h"
#include "UiWorkbench.h"
#include "UiViewWidgets.h"

 /// 构造函数，初始化主窗口组件
 /// @param parent 父部件
WorkbenchWindow::WorkbenchWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("SanYiCAD"));
    resize(1440, 900);

    buildMenus();
    buildToolBars();
    buildDockAreas();
    buildStatusBar();
    buildThemeMenu();
    buildWorkbenchMenu();
    bindStateSignals();
    setCentralWidget(new QWidget(this));
    refreshStatusText();
}

WorkbenchWindow::~WorkbenchWindow() = default;

/// 设置状态中心
/// @param stateCenter UI 状态中心
void WorkbenchWindow::setUiStateCenter(UiStateCenter* stateCenter)
{
    m_stateCenter = stateCenter;
    bindStateSignals();
}

/// 设置主题服务
/// @param themeService 主题服务
void WorkbenchWindow::setThemeService(UiThemeService* themeService)
{
    m_themeService = themeService;
}

/// 设置当前工作台
/// @param workbench 工作台实例
void WorkbenchWindow::setWorkbench(UiWorkbench* workbench)
{
    m_workbench = workbench;
}

/// 绑定状态中心信号
void WorkbenchWindow::bindStateSignals()
{
    if (!m_stateCenter)
        return;

    connect(m_stateCenter, &UiStateCenter::stateChanged, this, [this]() { refreshFromState(); });
    connect(m_stateCenter, &UiStateCenter::busyChanged, this, [this](bool) { refreshFromState(); });
    connect(m_stateCenter, &UiStateCenter::dirtyChanged, this, [this](bool) { refreshFromState(); });
}

/// 构建菜单系统（文件、视图、工具）
void WorkbenchWindow::buildMenus()
{
    m_fileMenu = menuBar()->addMenu(QStringLiteral("File"));
    m_viewMenu = menuBar()->addMenu(QStringLiteral("View"));
    m_toolsMenu = menuBar()->addMenu(QStringLiteral("Tools"));
}

/// 构建工具栏
void WorkbenchWindow::buildToolBars()
{
    m_mainToolBar = addToolBar(QStringLiteral("Main"));
    m_mainToolBar->setMovable(true);
}

/// 构建停靠区域（左侧项目面板、右侧属性面板）
void WorkbenchWindow::buildDockAreas()
{
    m_leftDock = new QDockWidget(QStringLiteral("Project"), this);
    m_leftDock->setWidget(new QWidget(m_leftDock));
    addDockWidget(Qt::LeftDockWidgetArea, m_leftDock);

    m_rightDock = new QDockWidget(QStringLiteral("Properties"), this);
    m_rightDock->setWidget(new QWidget(m_rightDock));
    addDockWidget(Qt::RightDockWidgetArea, m_rightDock);
}

/// 构建状态栏
void WorkbenchWindow::buildStatusBar()
{
    m_statusBar = statusBar();
    m_workbenchLabel = new QLabel(this);
    m_busyLabel = new QLabel(this);
    m_statusBar->addPermanentWidget(m_workbenchLabel, 1);
    m_statusBar->addPermanentWidget(m_busyLabel);
}

/// 构建主题菜单
void WorkbenchWindow::buildThemeMenu()
{
    m_themeMenu = m_toolsMenu->addMenu(QStringLiteral("Theme"));

    const auto addThemeAction = [this](const QString& text, const QString& themeId) {
        QAction* action = m_themeMenu->addAction(text);
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
void WorkbenchWindow::buildWorkbenchMenu()
{
    m_workbenchMenu = m_viewMenu->addMenu(QStringLiteral("Workbench"));

    const auto addWorkbenchAction = [this](const QString& text, const QString& workbenchId) {
        QAction* action = m_workbenchMenu->addAction(text);
        action->setCheckable(true);
        connect(action, &QAction::triggered, this, [this, workbenchId]() {
            triggerWorkbench(workbenchId);
            });
        };

    addWorkbenchAction(QStringLiteral("2D"), QStringLiteral("2D"));
    addWorkbenchAction(QStringLiteral("3D"), QStringLiteral("3D"));
}

/// 刷新状态栏文本
void WorkbenchWindow::refreshStatusText()
{
    if (m_stateCenter)
    {
        const auto state = m_stateCenter->snapshot();
        setWindowTitle(QStringLiteral("SanYiCAD - %1 - %2").arg(state.currentWorkbenchId, state.currentViewMode));

        if (m_workbenchLabel)
            m_workbenchLabel->setText(QStringLiteral("WB:%1 | Doc:%2 | Cmd:%3(%4) | Layer:%5 | View:%6 | Dirty:%7")
                .arg(state.currentWorkbenchId)
                .arg(state.currentDocumentId)
                .arg(state.currentCommandId)
                .arg(state.currentCommandPhase)
                .arg(state.currentLayerId)
                .arg(state.currentViewMode)
                .arg(state.dirty ? QStringLiteral("Y") : QStringLiteral("N")));

        if (m_busyLabel)
            m_busyLabel->setText(state.busy ? QStringLiteral("Busy") : QStringLiteral("Idle"));

        updateBusyIndicator(state.busy);
        return;
    }

    if (m_workbenchLabel)
        m_workbenchLabel->setText(QStringLiteral("Workbench: %1").arg(m_workbenchId));
    if (m_busyLabel)
        m_busyLabel->setText(m_busy ? QStringLiteral("Busy") : QStringLiteral("Idle"));
    setWindowTitle(QStringLiteral("SanYiCAD - %1").arg(m_workbenchId));
    updateBusyIndicator(m_busy);
}

/// 从状态中心刷新界面
void WorkbenchWindow::refreshFromState()
{
    refreshStatusText();

    if (m_propertiesDock && m_stateCenter)
    {
        const auto state = m_stateCenter->snapshot();
        m_propertiesDock->setStateText(QStringLiteral("WB=%1 | View=%2 | Cmd=%3(%4) | Dirty=%5 | Layer=%6 | Doc=%7 | Busy=%8")
            .arg(state.currentWorkbenchId)
            .arg(state.currentViewMode)
            .arg(state.currentCommandId)
            .arg(state.currentCommandPhase)
            .arg(state.dirty ? QStringLiteral("Y") : QStringLiteral("N"))
            .arg(state.currentLayerId)
            .arg(state.currentDocumentId)
            .arg(state.busy ? QStringLiteral("Y") : QStringLiteral("N")));
        m_propertiesDock->setSelectionText(QStringLiteral("Sel=%1 | SelSrc=%2 | CmdSrc=%3 | SelType=%4 | CmdType=%5")
            .arg(state.currentSelectionText)
            .arg(state.currentSelectionSource)
            .arg(state.currentCommandOwner)
            .arg(state.currentSelectionType)
            .arg(state.currentCommandType));
    }
}

/// 应用样式表
/// @param styleSheet 样式表内容
void WorkbenchWindow::applyTheme(const QString& styleSheet)
{
    setStyleSheet(styleSheet);
}

/// 设置主题切换回调
/// @param callback 主题切换回调函数
void WorkbenchWindow::setThemeChangeCallback(std::function<void(const QString&)> callback)
{
    m_themeChangeCallback = std::move(callback);
}

/// 注册停靠面板
/// @param title 面板标题
/// @param widget 面板内容部件
/// @param area 停靠区域
QDockWidget* WorkbenchWindow::registerDockWidget(const QString& title, QWidget* widget, Qt::DockWidgetArea area)
{
    auto* dock = new QDockWidget(title, this);
    dock->setWidget(widget);
    addDockWidget(area, dock);
    m_registeredDocks.push_back(dock);

    if (auto* tree = qobject_cast<SceneTreeDockWidget*>(widget))
        m_sceneTreeDock = tree;
    if (auto* props = qobject_cast<PropertiesPanelWidget*>(widget))
        m_propertiesDock = props;

    return dock;
}

/// 注册工具栏
/// @param title 工具栏标题
QToolBar* WorkbenchWindow::registerToolBar(const QString& title)
{
    auto* toolBar = addToolBar(title);
    m_registeredToolBars.push_back(toolBar);
    return toolBar;
}

/// 清空工作台内容（移除所有注册的面板和工具栏）
void WorkbenchWindow::clearWorkbenchContent()
{
    for (auto* dock : m_registeredDocks)
        removeDockWidget(dock);
    m_registeredDocks.clear();

    for (auto* toolBar : m_registeredToolBars)
        removeToolBar(toolBar);
    m_registeredToolBars.clear();

    m_sceneTreeDock = nullptr;
    m_propertiesDock = nullptr;
    setCentralWidget(new QWidget(this));
}

void WorkbenchWindow::resetWorkbenchTransientState()
{
    if (m_stateCenter)
    {
        m_stateCenter->setBusy(false);
        m_stateCenter->setCurrentCommandId(QStringLiteral("idle"));
        m_stateCenter->setCurrentCommandPhase(QStringLiteral("idle"));
        m_stateCenter->setCurrentCommandOwner(QStringLiteral("none"));
        m_stateCenter->setCurrentCommandType(QStringLiteral("none"));
        m_stateCenter->setMetadata({
            { QStringLiteral("workbenchId"), QStringLiteral("none") },
            { QStringLiteral("commandType"), QStringLiteral("none") },
            { QStringLiteral("commandState"), QStringLiteral("idle") },
            { QStringLiteral("selectionSource"), QStringLiteral("none") },
            { QStringLiteral("selectionText"), QString() },
            { QStringLiteral("selectionType"), QStringLiteral("none") },
            { QStringLiteral("viewportStatus"), QStringLiteral("Idle") }
            });
        m_stateCenter->setCurrentSelectionText(QString());
        m_stateCenter->setSelectionContext(QStringLiteral("none"), QString());
        m_stateCenter->setDirty(false);
    }
}

void WorkbenchWindow::syncWorkbenchStateFromStateCenter()
{
    if (!m_stateCenter)
        return;
    const auto state = m_stateCenter->snapshot();
    refreshWorkbenchMenuChecks(state.currentWorkbenchId);
    refreshThemeMenuChecks(state.currentThemeId);
}

/// 保存布局快照
/// @param workbenchId 工作台 ID
void WorkbenchWindow::saveLayoutSnapshot(const QString& workbenchId)
{
    if (workbenchId.isEmpty())
        return;

    QSettings settings;
    settings.beginGroup(QStringLiteral("LayoutSnapshots"));
    settings.setValue(workbenchId + QStringLiteral("/geometry"), saveGeometry());
    settings.setValue(workbenchId + QStringLiteral("/windowState"), saveState());
    settings.endGroup();
}

/// 恢复布局快照
/// @param workbenchId 工作台 ID
void WorkbenchWindow::restoreLayoutSnapshot(const QString& workbenchId)
{
    if (workbenchId.isEmpty())
        return;

    QSettings settings;
    settings.beginGroup(QStringLiteral("LayoutSnapshots"));
    const auto geometry = settings.value(workbenchId + QStringLiteral("/geometry")).toByteArray();
    const auto state = settings.value(workbenchId + QStringLiteral("/windowState")).toByteArray();
    settings.endGroup();

    if (!geometry.isEmpty())
        restoreGeometry(geometry);
    if (!state.isEmpty())
        restoreState(state);
}

/// 更新繁忙指示器
/// @param busy 是否繁忙
void WorkbenchWindow::updateBusyIndicator(bool busy)
{
    if (!m_statusBar)
        return;

    if (busy)
    {
        if (!m_statusBar->findChild<QProgressBar*>(QStringLiteral("BusyProgressBar")))
        {
            auto* progress = new QProgressBar(this);
            progress->setObjectName(QStringLiteral("BusyProgressBar"));
            progress->setRange(0, 0);
            progress->setMaximumWidth(140);
            m_statusBar->addPermanentWidget(progress);
        }
    }
    else
    {
        if (auto* progress = m_statusBar->findChild<QProgressBar*>(QStringLiteral("BusyProgressBar")))
        {
            m_statusBar->removeWidget(progress);
            progress->deleteLater();
        }
    }
}

/// 刷新主题菜单选中状态
/// @param themeId 当前主题 ID
void WorkbenchWindow::refreshThemeMenuChecks(const QString& themeId)
{
    m_themeId = themeId;
    for (QAction* action : m_themeMenu->actions())
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
    m_workbenchId = workbenchId;
    refreshStatusText();
    for (QAction* action : m_workbenchMenu->actions())
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
    if (m_themeChangeCallback)
        m_themeChangeCallback(themeId);

    if (m_themeService && m_themeService->loadThemeFromId(themeId))
        applyTheme(m_themeService->styleSheet());

    if (m_stateCenter)
        m_stateCenter->setCurrentThemeId(themeId);

    refreshThemeMenuChecks(themeId);
}

/// 触发工作台切换
/// @param workbenchId 工作台 ID
namespace
{
    QString workbenchSwitchText(const QString& workbenchId)
    {
        return QStringLiteral("Switching to %1").arg(workbenchId);
    }
}

void WorkbenchWindow::triggerWorkbench(const QString& workbenchId)
{
    if (!m_workbench)
    {
        if (m_stateCenter)
            m_stateCenter->setCurrentWorkbenchId(workbenchId);
        refreshWorkbenchMenuChecks(workbenchId);
        return;
    }

    if (workbenchId.compare(m_workbenchId, Qt::CaseInsensitive) == 0)
    {
        refreshWorkbenchMenuChecks(workbenchId);
        return;
    }

    const auto previousWorkbenchId = m_workbenchId;
    const auto switchContextText = workbenchSwitchText(workbenchId);

    if (m_stateCenter)
    {
        saveLayoutSnapshot(previousWorkbenchId);
        m_stateCenter->setBusy(true);
        m_stateCenter->setCurrentCommandId(QStringLiteral("idle"));
        m_stateCenter->setCurrentCommandPhase(QStringLiteral("idle"));
        m_stateCenter->setCurrentCommandOwner(QStringLiteral("none"));
        m_stateCenter->setCurrentCommandType(QStringLiteral("none"));
        m_stateCenter->setSelectionContext(QStringLiteral("Workbench-Switch"), switchContextText);
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

    m_workbench->deactivate();
    resetWorkbenchTransientState();
    clearWorkbenchContent();
    refreshStatusText();

    if (m_stateCenter)
        m_stateCenter->setCurrentWorkbenchId(workbenchId);

    m_workbench->attachToWindow(*this);
    restoreLayoutSnapshot(workbenchId);
    m_workbench->activate();
    refreshWorkbenchMenuChecks(workbenchId);
    refreshFromState();

    if (m_stateCenter)
        m_stateCenter->setBusy(false);
}