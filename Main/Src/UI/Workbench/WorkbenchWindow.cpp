#include "WorkbenchWindow.h"
#include "WorkbenchMenuManager.h"

/**
 * @file WorkbenchWindow.cpp
 * @brief 工作台主窗口 — UI 框架的顶层容器
 *
 * ============================================================================
 * 命令系统分类: 哪些动作走命令系统，哪些属于全局环境能力
 * ============================================================================
 *
 * 一、必须走命令系统 (OperationBus) 的操作:
 *   这些操作会修改文档状态、产生 Undo/Redo 记录，或需要统一的权限检查。
 *
 *   [绘图工具] - 创建新图元
 *     Line, Circle, Arc, Polyline, Polygon, Rectangle, Ellipse, Triangle,
 *     Bezier, Bezier2, Spline, NURBS, SmartLine, Text, Barcode, QRCode, Image
 *     路由: UI → OperationBus → 交互式命令
 *
 *   [编辑操作] - 修改已有图元
 *     Delete, Copy, Move, Rotate, Mirror, Trim, Extend, Fillet, Chamfer,
 *     Scale, Offset, Boolean Union/Intersection/Difference/Xor, Group, Align
 *     路由: UI → OperationBus → 编辑操作
 *
 *   [文件操作] - 文档生命周期
 *     New, Open, Save, SaveAs, Import(DXF/SVG/PLT/STEP/PDF/AI/Image),
 *     Export(DXF/SVG/PLT/BMP/PNG)
 *     路由: UI → OperationBus → 文件操作
 *
 *   [选择操作] - 选择状态变更
 *     SelectAll, InvertSelection, ClearSelection (Deselect)
 *     路由: UI → OperationBus → 选择操作
 *
 *   [算法操作] - 计算密集型
 *     Fill, Nesting, Array, ReliefEngraving
 *     路由: UI → OperationBus → 算法操作
 *
 *   [帮助操作] - 信息展示
 *     About, Settings, Documentation, KeyboardShortcuts
 *     路由: UI → OperationBus → 帮助操作
 *
 *   [撤销/重做] - 全局快捷键，但走命令系统
 *     Undo(Ctrl+Z), Redo(Ctrl+Y)
 *     路由: 快捷键 → UndoRedoManager
 *
 * 二、属于全局环境能力的操作 (不经过命令系统):
 *   这些操作不修改文档状态，不产生 Undo 记录，是视口或框架的固有行为。
 *
 *   [视口操作] - 纯视图变换，不修改数据
 *     缩放 (Zoom In/Out/Wheel)、平移 (Pan/MiddleButton)、框选 (Box Select)、
 *     点击选择 (HitTest)、视图重置 (Reset View)、缩放到适合 (Zoom to Fit)
 *     处理者: RenderViewport2D 鼠标/滚轮/键盘事件直接处理
 *
 *   [工作台切换] - 框架级生命周期管理
 *     2D ↔ 3D 切换
 *     处理者: WorkbenchWindow::triggerWorkbench()
 *
 *   [主题切换] - 外观设置
 *     Light/Dark/System/Blue 主题
 *     处理者: WorkbenchWindow::triggerTheme() → ThemeManager
 *
 *   [语言切换] - 国际化
 *     中文/English 切换
 *     处理者: LanguageManager → WorkbenchWindow::retranslateUi()
 *
 *   [图层切换] - 全局面板操作（通过 LayerEditService 统一入口）
 *     当前图层切换、图层管理对话框
 *     处理者: RightToolBar → LayerEditService::setCurrentLayer() + UiStateCenter 同步
 *
 *   [网格/捕捉/正交] - 视口辅助配置
 *     Show Grid, Snap Enabled, Ortho Mode, Angle Snap
 *     处理者: 视口状态配置，通过状态中心同步
 *
 * 三、状态中心 (UiStateCenter) 作为 UI 单一展示来源:
 *   - 所有 UI 状态（工作台、命令、选择、图层、视图、繁忙、脏标记）统一由状态中心管理
 *   - WorkbenchWindow 通过 syncWindowStateFromStateCenter() 同步本地镜像
 *   - 状态栏、属性面板、标题栏等展示层统一从状态中心快照读取
 *   - 工作台切换时通过 WorkbenchStateSnapshot 保存/恢复状态
 *
 * ============================================================================
 */

#include "Log/SyLogger.h"
#include <QAction>
#include <QDateTime>
#include <QEvent>
#include <QFileInfo>
#include <functional>

#include <QDockWidget>
#include <QLabel>
#include <QGuiApplication>
#include <QInputDialog>
#include <QMenuBar>
#include <QProgressBar>
#include <QRegularExpression>
#include <QScreen>
#include <QSettings>
#include <QStatusBar>
#include <QToolBar>
#include <QWidget>

#include <chrono>

#include "VersionInfo.h"
#include "UiLayoutService.h"
#include "Persistence/PersistenceService.h"
#include "Persistence/Repositories/WorkspaceSnapshotRepository.h"
#include "Persistence/Repositories/RecentFileRepository.h"
#include "Persistence/Models/RecentFileRecord.h"
#include "Persistence/Models/WorkspaceSnapshotRecord.h"
#include "UI2D/Operation/OperationBus.h"
#include "UI2D/Operation/OperationId.h"
#include "UI2D/Operation/CommandCatalog.h"
#include "UiStateCenter.h"
#include "UiThemeService.h"
#include "UiWorkbench.h"
#include "UiSceneTreeDock.h"
#include "UiPropertiesPanel.h"
#include "Engine2D/Edit/LayerEditService.h"
#include "Engine2D/Interaction/LayerManager.h"
#include "Ui/Dlg/LayerManagerDialog.h"

#include <QCloseEvent>
#include <QMessageBox>

WorkbenchWindow::WorkbenchWindow(QWidget* parent)
    : QMainWindow(parent)
{
    SY_DEBUG("[WorkbenchWindow] Creating main window");
    setWindowTitle(QString::fromStdString(MainApp::appName()));
    resize(1440, 900);

    if (const auto* screen = QGuiApplication::primaryScreen())
    {
        const auto available = screen->availableGeometry();
        const int x = available.x() + (available.width() - width()) / 2;
        const int y = available.y() + (available.height() - height()) / 2;
        move(x, y);
    }

    m_menuManager = new WorkbenchMenuManager(this, this);
    SY_DEBUG("[WorkbenchWindow] Initializing workbench shell");
    initializeWorkbenchShell();
    SY_INFO("[WorkbenchWindow] Main window created successfully");
}

WorkbenchWindow::~WorkbenchWindow() = default;

void WorkbenchWindow::changeEvent(QEvent* event)
{
    if (event->type() == QEvent::LanguageChange)
    {
        retranslateUi();
    }
    QMainWindow::changeEvent(event);
}

void WorkbenchWindow::retranslateUi()
{
    setWindowTitle(QString::fromStdString(MainApp::appName()));

    if (m_menuManager)
        m_menuManager->rebuildAllMenus();

    refreshStatusText();
    SY_DEBUG("[WorkbenchWindow] retranslateUi completed");
}

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

/// 设置操作总线
/// @param bus 操作总线
void WorkbenchWindow::setOperationBus(OperationBus* bus)
{
    m_operationBus = bus;
    m_uiServices.operationBus = bus;
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

const UiServices& WorkbenchWindow::uiServices() const
{
    return m_uiServices;
}

void WorkbenchWindow::configureServices(const UiServices& services)
{
    unbindStateSignals();
    m_uiServices = services;
    m_stateCenter = services.stateCenter;
    m_themeService = services.themeService;

    if (m_menuManager)
    {
        m_menuManager->setOperationBus(services.operationBus);
        m_menuManager->setStateCenter(services.stateCenter);
        m_menuManager->setThemeService(services.themeService);
        m_menuManager->setUiServices(&m_uiServices);
        m_menuManager->setWorkbench(m_workbench);
        m_menuManager->rebuildAllMenus();
    }

    bindStateSignals();
}

/// 设置当前工作台
/// @param workbench 工作台实例
void WorkbenchWindow::setWorkbench(UiWorkbench* workbench)
{
    m_workbench = workbench;
    if (m_menuManager)
        m_menuManager->setWorkbench(workbench);
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

void WorkbenchWindow::initializeWorkbenchShell()
{
    if (m_menuManager)
    {
        m_menuManager->buildMenus();
        m_menuManager->buildThemeMenu();
        m_menuManager->bindShortcuts();
    }
    initializeToolBarSkeleton();
    initializeDockAreaSkeleton();
    initializeStatusBarSkeleton();
    bindStateSignals();
    setCentralWidget(createInitialCentralWidget());
    updateWindowTitle();
    refreshStatusText();
    QString initialWorkbenchId = m_windowState.workbenchId;
    if (m_stateCenter)
        initialWorkbenchId = m_stateCenter->currentWorkbenchId();
    if (initialWorkbenchId.isEmpty() || initialWorkbenchId == QStringLiteral("default"))
        initialWorkbenchId = QStringLiteral("2D");
    if (m_menuManager)
        m_menuManager->refreshWorkbenchMenuChecks(initialWorkbenchId);
}

QWidget* WorkbenchWindow::createInitialCentralWidget()
{
    auto* widget = new QWidget(this);
    widget->setObjectName(QStringLiteral("WorkbenchCentralPlaceholder"));
    return widget;
}

void WorkbenchWindow::initializeToolBarSkeleton()
{
    // 工具栏骨架只创建承载容器，不在初始化绑定具体动作
    buildToolBars();
}

void WorkbenchWindow::buildToolBars()
{
}

void WorkbenchWindow::initializeDockAreaSkeleton()
{
    // 停靠区骨架先只创建左右容器，不在这里挂接具体工作台面板
    buildDockAreas();
}

void WorkbenchWindow::buildDockAreas()
{
    // 场景树面板
    m_panelState.sceneTreeDock = new SceneTreeDockWidget(this);
    m_panelState.leftDock = new QDockWidget(tr("Scene"), this); // 场景
    m_panelState.leftDock->setObjectName(QStringLiteral("SceneDock"));
    m_panelState.leftDock->setWidget(m_panelState.sceneTreeDock);
    m_panelState.leftDock->setMinimumWidth(180);
    m_panelState.leftDock->setMaximumWidth(400);
    addDockWidget(Qt::LeftDockWidgetArea, m_panelState.leftDock);

    // 属性面板
    m_panelState.propertiesDock = new PropertiesPanelWidget(this);
    m_panelState.rightDock = new QDockWidget(tr("Properties"), this); // 属性
    m_panelState.rightDock->setObjectName(QStringLiteral("PropertiesDock"));
    m_panelState.rightDock->setWidget(m_panelState.propertiesDock);
    m_panelState.rightDock->setMinimumWidth(200);
    m_panelState.rightDock->setMaximumWidth(450);
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
    // 状态栏承载全局状态展示，参考旧框架 StatusBar 设计
    // 包含：鼠标坐标 | 选中信息 | 消息 | 工作台状态 | 繁忙指示
    m_panelState.statusBar = statusBar();

    // 鼠标坐标标签（左侧，固定宽度）
    m_panelState.posLabel = new QLabel(tr("Position: (0.00, 0.00) mm"), this);
    m_panelState.posLabel->setMinimumWidth(280);
    m_panelState.statusBar->addWidget(m_panelState.posLabel, 1);

    // 选中信息标签
    m_panelState.selLabel = new QLabel(tr("Selected: 0"), this);
    m_panelState.selLabel->setMinimumWidth(200);
    m_panelState.statusBar->addWidget(m_panelState.selLabel, 1);
    m_panelState.selLabel->setText(tr("Selected: 0"));

    // 消息标签（居中扩展）
    m_panelState.msgLabel = new QLabel(tr("Ready"), this);
    m_panelState.msgLabel->setMinimumWidth(100);
    m_panelState.statusBar->addWidget(m_panelState.msgLabel, 2);

    // 工作台状态标签（右侧固定）
    m_panelState.workbenchLabel = new QLabel(this);
    m_panelState.workbenchLabel->setMinimumWidth(300);
    m_panelState.statusBar->addPermanentWidget(m_panelState.workbenchLabel, 1);

    // 繁忙标签
    m_panelState.busyLabel = new QLabel(this);
    m_panelState.busyLabel->setMinimumWidth(60);
    m_panelState.statusBar->addPermanentWidget(m_panelState.busyLabel);
}

/// 构建主题菜单
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
        {
            // 从元数据读取统一状态提示
            QString statusPrompt = state.metadata.value(QStringLiteral("statusPrompt")).toString();
            if (statusPrompt.isEmpty())
                statusPrompt = tr("Ready");

            // 提取文件名用于状态栏展示，完整路径放在 tooltip 中
            QString docDisplay = state.currentDocumentId;
            QString docTooltip;
            if (!docDisplay.isEmpty() && docDisplay != QStringLiteral("none"))
            {
                QFileInfo fi(docDisplay);
                docTooltip = docDisplay;
                docDisplay = fi.fileName();
            }

            m_panelState.workbenchLabel->setText(tr("WB:%1 | Doc:%2 | Cmd:%3(%4) | Layer:%5 | View:%6 | Dirty:%7 | %8")
                .arg(state.currentWorkbenchId)
                .arg(docDisplay)
                .arg(state.currentCommandId)
                .arg(state.currentCommandPhase)
                .arg(state.currentLayerId)
                .arg(state.currentViewMode)
                .arg(state.dirty ? tr("Y") : tr("N"))
                .arg(statusPrompt));
            m_panelState.workbenchLabel->setToolTip(docTooltip);
        }

        if (m_panelState.busyLabel)
            m_panelState.busyLabel->setText(state.busy ? tr("Busy") : tr("Idle"));

        // 这里仍然只做展示，不把状态写回状态中心，避免循环同步
        updateBusyIndicator(state.busy);
        updateWindowTitle();
        return;
    }

    if (m_panelState.workbenchLabel)
        m_panelState.workbenchLabel->setText(tr("Workbench: %1").arg(m_windowState.workbenchId));
    if (m_panelState.busyLabel)
        m_panelState.busyLabel->setText(m_windowState.busy ? tr("Busy") : tr("Idle"));
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
    // 这里不直接读写业务状态，保持"先同步、后展示"的总顺序
    refreshStatusText();
    // 保存/新建后脏标记变化会触发 refreshFromState，这里同步更新窗口标题显示脏标记
    updateWindowTitle();

    // 统一更新消息标签（显示当前状态提示，如 Pan/Zoom/Selection/Edit 等）
    if (m_stateCenter && m_panelState.msgLabel)
    {
        const auto& metadata = m_stateCenter->snapshot().metadata;
        QString prompt = metadata.value(QStringLiteral("statusPrompt")).toString();
        if (prompt.isEmpty())
            prompt = tr("Ready");
        m_panelState.msgLabel->setText(prompt);
    }
    if (m_panelState.selLabel && m_stateCenter)
    {
        const auto state = m_stateCenter->snapshot();
        const QString selectionText = state.currentSelectionText.trimmed();
        int selectedCount = 0;

        if (selectionText.isEmpty() || selectionText == QStringLiteral("none"))
        {
            selectedCount = 0;
        }
        else if (selectionText.contains(QStringLiteral("entities selected")))
        {
            QRegularExpression re(QStringLiteral("(\\d+)"));
            auto match = re.match(selectionText);
            if (match.hasMatch())
                selectedCount = match.captured(1).toInt();
        }
        else
        {
            selectedCount = 1;
        }

        m_panelState.selLabel->setText(tr("Selected: %1").arg(selectedCount));
    }

    if (m_menuManager)
        m_menuManager->refreshGridSnapMenuChecks();
    // 属性面板使用状态中心快照作为输入，不在这里额外拼接窗口本地状态
    // 这里也不读取窗口本地镜像，避免展示层继续依赖两套来源
    if (m_panelState.propertiesDock && m_stateCenter)
    {
        const auto state = m_stateCenter->snapshot();
        // 属性面板同样以状态中心为准，避免单独维护一套展示状态
        // 从元数据读取统一状态提示
        QString statusPrompt = state.metadata.value(QStringLiteral("statusPrompt")).toString();
        if (statusPrompt.isEmpty())
            statusPrompt = tr("Ready");
        m_panelState.propertiesDock->setStateText(tr("WB=%1 | View=%2 | Cmd=%3(%4) | Dirty=%5 | Layer=%6 | Doc=%7 | Busy=%8 | %9")
            .arg(state.currentWorkbenchId)
            .arg(state.currentViewMode)
            .arg(state.currentCommandId)
            .arg(state.currentCommandPhase)
            .arg(state.dirty ? tr("Y") : tr("N"))
            .arg(state.currentLayerId)
            .arg(state.currentDocumentId)
            .arg(state.busy ? tr("Y") : tr("N"))
            .arg(statusPrompt));

        QString selectionText = tr("Sel=%1 | SelSrc=%2 | CmdSrc=%3 | SelType=%4 | CmdType=%5")
            .arg(state.currentSelectionText)
            .arg(state.currentSelectionSource)
            .arg(state.currentCommandOwner)
            .arg(state.currentSelectionType)
            .arg(state.currentCommandType);
        if (state.currentWorkbenchId.compare(tr("3D"), Qt::CaseInsensitive) == 0)
        {
            selectionText = tr("3D Sel=%1 | NodeType=%2 | CmdSrc=%3 | CmdType=%4")
                .arg(state.currentSelectionText)
                .arg(state.currentSelectionType)
                .arg(state.currentCommandOwner)
                .arg(state.currentCommandType);
        }
        else if (state.currentWorkbenchId.compare(tr("2D"), Qt::CaseInsensitive) == 0)
        {
            selectionText = tr("2D Sel=%1 | SelType=%2 | CmdSrc=%3 | CmdType=%4")
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
    if (m_stateCenter)
    {
        const auto state = m_stateCenter->snapshot();

        // 提取文档文件名用于窗口标题
        QString docFile;
        QString docId = state.currentDocumentId;
        if (!docId.isEmpty() && docId != QStringLiteral("none"))
        {
            QFileInfo fi(docId);
            docFile = fi.fileName();
        }

        QString title;
        if (docFile.isEmpty())
            title = QStringLiteral("%1 - %2 - %3").arg(
                QString::fromStdString(MainApp::appName()),
                state.currentWorkbenchId,
                state.currentViewMode);
        else
            title = QStringLiteral("%1 - %2 [%3 - %4]").arg(
                docFile,
                QString::fromStdString(MainApp::appName()),
                state.currentWorkbenchId,
                state.currentViewMode);

        if (state.dirty)
            title.prepend(QStringLiteral("* "));
        setWindowTitle(title);
        return;
    }

    setWindowTitle(QStringLiteral("%1 - %2").arg(
        QString::fromStdString(MainApp::appName()),
        m_windowState.workbenchId));
}

/// 应用样式表
/// @param styleSheet 样式表内容
void WorkbenchWindow::closeEvent(QCloseEvent* event)
{
    if (m_stateCenter && m_stateCenter->dirty())
    {
        auto result = QMessageBox::question(
            this,
            tr("Unsaved Changes"),
            tr("Do you want to save changes before closing?"),
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
        if (result == QMessageBox::Cancel)
        {
            event->ignore();
            return;
        }
        if (result == QMessageBox::Save)
        {
            if (m_uiServices.operationBus)
                m_uiServices.operationBus->run(OperationId::File_Save, {});
        }
    }
    event->accept();
}

void WorkbenchWindow::applyTheme(const QString& styleSheet)
{
    setStyleSheet(styleSheet);
}

/// 设置主题切换回调
/// @param callback 主题切换回调函数
void WorkbenchWindow::setThemeChangeCallback(std::function<void(const QString&)> callback)
{
    // 主题回调只作为外部扩展点，不在这里绑定额外业务行为
    // 回调一旦注入，就应被视为主题链的唯一扩展入口之一
    // 这里不触发立即执行，避免回调设置产生副作用
    m_themeChangeCallback = std::move(callback);
}

/// 设置视口缩放操作回调
/// @param handler 缩放操作处理函数
void WorkbenchWindow::setViewportZoomHandler(std::function<void(const QString&)> handler)
{
    m_viewportZoomHandler = handler;
    if (m_menuManager)
        m_menuManager->setViewportZoomHandler(std::move(handler));
}

void WorkbenchWindow::setViewportPositionHandler(std::function<void(double, double)> handler)
{
    m_viewportPositionHandler = std::move(handler);
}

void WorkbenchWindow::updatePositionLabel(double x, double y)
{
    if (m_panelState.posLabel)
        m_panelState.posLabel->setText(tr("Position: (%1, %2) mm").arg(x, 0, 'f', 2).arg(y, 0, 'f', 2));
}

// ==================== 最近文件菜单实现 ====================

/// 将文件路径添加到最近文件列表
/// @param filePath 文件完整路径
void WorkbenchWindow::addRecentFile(const QString& filePath)
{
    if (filePath.isEmpty())
        return;

    // 优先使用数据库持久化，失败时回退到 QSettings
    auto* ps = m_uiServices.persistenceService;
    if (ps && ps->isOpen() && ps->recentFiles())
    {
        QFileInfo fileInfo(filePath);
        RecentFileRecord rec;
        rec.filePath = filePath.toStdString();
        rec.title = fileInfo.fileName().toStdString();
        rec.format = fileInfo.suffix().toUpper().toStdString();
        // 获取当前时间戳
        rec.lastOpenedTime = QDateTime::currentDateTime().toString(Qt::ISODate).toStdString();
        ps->recentFiles()->append(rec);
    }

    // 同时维护 QSettings 作为兜底（与数据库双写，确保降级可用）
    QStringList files = loadRecentFiles();
    files.removeAll(filePath);
    files.prepend(filePath);
    constexpr int kMaxRecentFiles = 10;
    while (files.size() > kMaxRecentFiles)
        files.removeLast();
    saveRecentFiles(files);
    populateRecentFilesMenu();
}

/// 从设置中加载最近文件列表（数据库优先，QSettings 兜底）
QStringList WorkbenchWindow::loadRecentFiles() const
{
    auto* ps = m_uiServices.persistenceService;
    if (ps && ps->isOpen() && ps->recentFiles())
    {
        auto records = ps->recentFiles()->loadAll();
        if (!records.empty())
        {
            QStringList result;
            for (const auto& rec : records)
                result.append(QString::fromStdString(rec.filePath));
            return result;
        }
    }

    // 兜底：从 QSettings 读取
    QSettings settings;
    return settings.value(QStringLiteral("RecentFiles"), QStringList()).toStringList();
}

/// 将最近文件列表保存到设置（数据库优先，QSettings 兜底）
void WorkbenchWindow::saveRecentFiles(const QStringList& files) const
{
    // 数据库端：由 addRecentFile 逐条写入，此处不做批量覆盖
    // QSettings 兜底：保留旧版兼容性
    QSettings settings;
    settings.setValue(QStringLiteral("RecentFiles"), files);
}

/// 填充最近文件子菜单
void WorkbenchWindow::populateRecentFilesMenu()
{
    QMenu* recentMenu = m_menuManager ? m_menuManager->recentFilesMenu() : nullptr;
    if (!recentMenu)
        return;

    qDeleteAll(recentMenu->actions());

    QStringList files = loadRecentFiles();

    if (files.isEmpty())
    {
        auto* empty = recentMenu->addAction(tr("(No recent files)"));
        empty->setEnabled(false);
        return;
    }

    // 添加最近文件菜单项
    int index = 1;
    for (const QString& filePath : files)
    {
        QFileInfo fileInfo(filePath);
        QString displayText = QStringLiteral("%1. %2").arg(index).arg(fileInfo.fileName());

        auto* action = recentMenu->addAction(displayText);
        action->setData(filePath);

        // 点击时打开文件（通过 OperationBus 直接传入路径）
        QObject::connect(action, &QAction::triggered, this, [this, filePath]() {
            auto* bus = m_uiServices.operationBus;
            if (bus && bus->registry().has(OperationId::File_OpenRecent))
            {
                QVariantMap params;
                params[QStringLiteral("filePath")] = filePath;
                bus->run(OperationId::File_OpenRecent, params);
            }
            else if (m_operationBus)
            {
                // 兜底：走文件打开对话框（用户手动选取）
                m_operationBus->run(CommandCatalog::operationForCommandId(QStringLiteral("file.open")));
            }
            });

        ++index;
    }
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
    {
        removeDockWidget(dock);
        delete dock;
    }
    m_registeredDocks.clear();

    // 清理所有工具栏（包括通过 addToolBar 直接添加而未注册的工具栏，如3D左侧工具栏）
    // 先收集所有工具栏指针，避免遍历过程中容器被修改
    QList<QToolBar*> allToolBars = findChildren<QToolBar*>();
    for (auto* toolBar : allToolBars)
    {
        removeToolBar(toolBar);
        delete toolBar;
    }
    m_registeredToolBars.clear();

    // 清理菜单栏 - 3D 工作台使用 MenuManager3D 独立管理菜单，
    // 切换到 2D 时需要清空菜单栏，避免 3D 菜单残留导致混乱
    if (auto* mb = menuBar())
        mb->clear();

    m_panelState.sceneTreeDock = nullptr;
    m_panelState.propertiesDock = nullptr;

    // 先解除旧中央控件绑定，再安排延迟删除，避免 Qt 布局冲突
    auto* oldCentral = centralWidget();
    if (oldCentral)
    {
        setCentralWidget(nullptr);  // 先解除旧中央控件与 QMainWindow 的绑定
        oldCentral->hide();
        oldCentral->deleteLater();
    }
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
    const auto state = m_stateCenter->snapshot();
    if (m_menuManager)
    {
        m_menuManager->refreshWorkbenchMenuChecks(state.currentWorkbenchId);
        m_menuManager->refreshThemeMenuChecks(state.currentThemeId);
    }
    recordPerformance(QStringLiteral("WorkbenchWindow::syncWorkbenchStateFromStateCenter"),
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count());
}

/// 保存布局快照
/// @param workbenchId 工作台 ID
void WorkbenchWindow::saveLayoutSnapshot(const QString& workbenchId)
{
    // 布局快照只保存窗口外观，不保存业务状态；业务状态由状态中心负责
    if (workbenchId.isEmpty())
        return;

    const auto start = std::chrono::steady_clock::now();

    // 优先使用数据库持久化，失败时回退到 QSettings
    auto* ps = m_uiServices.persistenceService;
    if (ps && ps->isOpen() && ps->workspaceSnapshots())
    {
        WorkspaceSnapshotRecord rec;
        rec.workbenchId = workbenchId.toStdString();
        rec.geometry = saveGeometry().toBase64().toStdString();
        rec.windowState = saveState().toBase64().toStdString();
        rec.updatedAt = QDateTime::currentDateTime().toString(Qt::ISODate).toStdString();
        ps->workspaceSnapshots()->save(rec);
        SY_INFOF("[WorkbenchWindow] Saved layout snapshot to database: %s", rec.workbenchId.c_str());
    }

    // QSettings 兜底
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
    if (workbenchId.isEmpty())
        return;

    const auto start = std::chrono::steady_clock::now();

    // 优先从数据库加载
    QByteArray state;
    auto* ps = m_uiServices.persistenceService;
    if (ps && ps->isOpen() && ps->workspaceSnapshots())
    {
        auto rec = ps->workspaceSnapshots()->load(workbenchId.toStdString());
        if (!rec.windowState.empty())
        {
            state = QByteArray::fromBase64(QByteArray::fromStdString(rec.windowState));
            SY_INFOF("[WorkbenchWindow] Loaded layout snapshot from database: %s", rec.workbenchId.c_str());
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

void WorkbenchWindow::setSkeletonDocksVisible(bool visible)
{
    if (m_panelState.leftDock)
        m_panelState.leftDock->setVisible(visible);
    if (m_panelState.rightDock)
        m_panelState.rightDock->setVisible(visible);

    if (m_panelState.posLabel)
        m_panelState.posLabel->setVisible(visible);
    if (m_panelState.selLabel)
        m_panelState.selLabel->setVisible(visible);
    if (m_panelState.msgLabel)
        m_panelState.msgLabel->setVisible(visible);
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
    m_windowState.themeId = themeId;
    updateWindowTitle();
    if (m_menuManager)
        m_menuManager->refreshThemeMenuChecks(themeId);
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
        reportFrameworkError(QStringLiteral("theme.load_failed"),
            QStringLiteral("Failed to load theme %1").arg(themeId), QStringLiteral("WorkbenchWindow::triggerTheme"));
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
    const auto start = std::chrono::steady_clock::now();
    SY_DEBUGF("[WorkbenchWindow] triggerWorkbench: switching to %s", workbenchId.toUtf8().constData());

    if (!canExecuteCommand(QStringLiteral("workbench.switch.%1").arg(workbenchId), QStringLiteral("WorkbenchWindow::triggerWorkbench")))
    {
        reportFrameworkError(QStringLiteral("workbench.switch_denied"), QStringLiteral("Workbench switch denied: %1").arg(workbenchId),
            QStringLiteral("WorkbenchWindow::triggerWorkbench"));

        recordPerformance(QStringLiteral("WorkbenchWindow::triggerWorkbench.denied"),
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count());
        return;
    }

    if (!m_workbench)
    {
        SY_DEBUGF("[WorkbenchWindow] triggerWorkbench: no workbench, setting state to %s", workbenchId.toUtf8().constData());
        if (m_stateCenter)
        {
            m_stateCenter->setCurrentWorkbenchId(workbenchId);
            m_stateCenter->setCurrentViewMode(QStringLiteral("none"));
            m_stateCenter->setSelectionContext(QStringLiteral("Workbench-Switch"), QStringLiteral("Ready"));
        }
        const bool is3DNoWb = workbenchId.compare(QStringLiteral("3D"), Qt::CaseInsensitive) == 0;
        if (!is3DNoWb && m_menuManager)
        {
            m_menuManager->refreshWorkbenchMenuChecks(workbenchId);
            m_menuManager->refreshEditMenuForWorkbench(workbenchId);
            m_menuManager->refreshDrawMenuForWorkbench(workbenchId);
            m_menuManager->refreshModifyMenuForWorkbench(workbenchId);
            m_menuManager->refreshAlgorithmMenuForWorkbench(workbenchId);
            m_menuManager->refreshFileMenuForWorkbench(workbenchId);
        }
        refreshFromState();
        refreshStatusText();
        updateWindowTitle();
        recordPerformance(QStringLiteral("WorkbenchWindow::triggerWorkbench.noWorkbench"),
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count());
        return;
    }

    if (workbenchId.compare(m_windowState.workbenchId, Qt::CaseInsensitive) == 0)
    {
        SY_DEBUGF("[WorkbenchWindow] triggerWorkbench: same workbench %s, skipping", workbenchId.toUtf8().constData());
        if (m_menuManager)
            m_menuManager->refreshWorkbenchMenuChecks(workbenchId);
        recordPerformance(QStringLiteral("WorkbenchWindow::triggerWorkbench.sameWorkbench"),
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count());
        return;
    }

    const auto previousWorkbenchId = m_windowState.workbenchId;
    const auto switchContextText = workbenchSwitchText(workbenchId);

    SY_DEBUGF("[WorkbenchWindow] triggerWorkbench: switching from %s to %s", previousWorkbenchId.toUtf8().constData(), workbenchId.toUtf8().constData());

    // 1: 保存旧工作台布局快照，标记繁忙
    if (m_stateCenter)
    {
        saveLayoutSnapshot(previousWorkbenchId);
        m_stateCenter->setBusy(true);
        m_stateCenter->setMetadata({
            { QStringLiteral("viewportStatus"), QStringLiteral("Switching") }
            });
    }

    // 2: 停用旧工作台
    SY_DEBUG("[WorkbenchWindow] triggerWorkbench: deactivating old workbench");
    m_workbench->deactivate();
    recordPerformance(QStringLiteral("WorkbenchWindow::triggerWorkbench.deactivate"),
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count());

    // 3: 清理旧工作台的 transient 状态（命令、选择、镜像状态）
    SY_DEBUG("[WorkbenchWindow] triggerWorkbench: resetting transient state");
    resetWorkbenchTransientState();

    // 4: 清除旧工作台 UI 内容（面板、工具栏、中央控件）
    SY_DEBUG("[WorkbenchWindow] triggerWorkbench: clearing workbench content");
    clearWorkbenchContent();

    // 5: 设置新工作台上下文（在 attach 之前，确保新工作台能看到正确的状态）
    setWorkbenchSwitchContext(workbenchId, switchContextText);

    // 6: 通过工厂获取或创建新工作台实例
    if (m_workbenchFactory)
    {
        auto* target = m_workbenchFactory(workbenchId);
        if (target && target != m_workbench)
        {
            SY_DEBUGF("[WorkbenchWindow] triggerWorkbench: factory created new workbench %s", workbenchId.toUtf8().constData());
            m_workbench = target;
        }
    }

    // 7: 附加新工作台到窗口（注册面板、工具栏、中央控件）
    SY_DEBUG("[WorkbenchWindow] triggerWorkbench: attaching workbench to window");
    m_workbench->attachToWindow(*this);
    recordPerformance(QStringLiteral("WorkbenchWindow::triggerWorkbench.attach"),
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count());

    // 8: 恢复新工作台布局快照
    SY_DEBUG("[WorkbenchWindow] triggerWorkbench: restoring layout");
    restoreLayoutSnapshot(workbenchId);
    recordPerformance(QStringLiteral("WorkbenchWindow::triggerWorkbench.restoreLayout"),
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count());

    // 9: 激活新工作台（应用初始状态）
    SY_DEBUG("[WorkbenchWindow] triggerWorkbench: activating new workbench");
    m_workbench->activate();
    recordPerformance(QStringLiteral("WorkbenchWindow::triggerWorkbench.activate"),
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count());

    // 10: 刷新 UI 并清除繁忙状态
    // 3D 工作台由 MenuManager3D 独立管理菜单，不需要 WorkbenchMenuManager 刷新
    // 否则 WorkbenchMenuManager 会用 2D OperationBus 连接 3D 菜单项，导致 "Operation not registered" 错误
    const bool is3D = workbenchId.compare(QStringLiteral("3D"), Qt::CaseInsensitive) == 0;
    if (!is3D && m_menuManager)
    {
        // 从3D切换到2D时，菜单栏已被清空，需要完全重建2D菜单
        m_menuManager->rebuildAllMenus();
    }
    // 3D 工作台不需要骨架停靠面板，隐藏以免挤压视口
    // 2D 工作台需要显示骨架停靠面板
    setSkeletonDocksVisible(!is3D);
    refreshFromState();
    refreshStatusText();
    updateWindowTitle();

    if (m_stateCenter)
        m_stateCenter->setBusy(false);
    m_windowState.busy = false;
    m_windowState.workbenchId = workbenchId;

    SY_INFOF("[WorkbenchWindow] triggerWorkbench: switch completed to %s", workbenchId.toUtf8().constData());
    recordPerformance(QStringLiteral("WorkbenchWindow::triggerWorkbench.switch"),
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count());
}