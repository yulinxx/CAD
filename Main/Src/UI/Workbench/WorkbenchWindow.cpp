#include "WorkbenchWindow.h"
#include "WorkbenchMenuManager.h"
#include "WorkbenchLayoutManager.h"
#include "WorkbenchActionManager.h"
#include "WorkbenchStateManager.h"
#include "FileDropHandler.h"

#include "Manager/UnitManager/UnitManager.h"

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
#include "UI/StatusBarBase.h"
#include <QAction>
#include <QDateTime>
#include <QEvent>
#include <QFileInfo>
#include <functional>

#include <QCoreApplication>
#include <QDockWidget>
#include <QLabel>
#include <QGuiApplication>
#include <QInputDialog>
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
#include "UI/ThemeManager.h"
#include "UiWorkbench.h"
#include "UiSceneTreePanel2D.h"
#include "UiPropertiesPanel.h"
#include "Engine2D/Edit/LayerEditService.h"
#include "Engine2D/Edit/SceneEditService.h"
#include "Engine2D/Interaction/LayerManager.h"
#include "UI/Dlg/LayerManagerDialog.h"
#include "UI/Interaction/UiInteractionGate.h"

#include <QCloseEvent>
#include <QMessageBox>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDragLeaveEvent>
#include <QDropEvent>

WorkbenchWindow::WorkbenchWindow(QWidget* parent)
    : QMainWindow(parent)
    , m_fileDropHandler(std::make_unique<FileDropHandler>(this))
{
    SY_DEBUG("[WorkbenchWindow] Creating main window");
    setWindowTitle(QString::fromStdString(MainApp::appName()));
    resize(1440, 900);
    // 启用文件拖放，2D/3D 工作台共用统一 FileDropHandler
    setAcceptDrops(true);
    // QOpenGLWidget 在 macOS/Windows 是原生子窗口，拖放事件可能不冒泡到本窗口。
    // 额外安装应用级事件过滤器兜底，确保拖放视口/子窗口任意位置都能触发导入。
    if (m_fileDropHandler)
    {
        m_fileDropHandler->installAppEventFilter();
    }

    // 注册主界面根部件到总开关（UiInteractionGate），
    // 供长时算法/批量操作在运行期间整体禁用/恢复主界面
    UiInteractionGate::instance().setRootWidget(this);

    if (const auto* screen = QGuiApplication::primaryScreen())
    {
        const auto available = screen->availableGeometry();
        const int x = available.x() + (available.width() - width()) / 2;
        const int y = available.y() + (available.height() - height()) / 2;
        move(x, y);
    }

    m_menuManager = new WorkbenchMenuManager(this, this);
    m_layoutManager = std::make_unique<WorkbenchLayoutManager>(this, m_menuManager);
    m_actionManager = std::make_unique<WorkbenchActionManager>();
    m_stateManager = std::make_unique<WorkbenchStateManager>(this, m_menuManager, m_layoutManager.get());
    // 启用鼠标追踪，确保 3D 视口中的 QOpenGLWidget 能收到无按键 mouseMoveEvent
    setMouseTracking(true);
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

    // 语言切换时重建菜单文案。
    // 配置驱动是唯一路径（P0-1），菜单统一由 WorkbenchMenuManager 从客户 JSON 生成，
    // 因此语言切换后始终需要重建以刷新文案；不再区分「工作台自管菜单」的 legacy 分支。
    if (m_menuManager)
    {
        m_menuManager->rebuildAllMenus();
    }

    refreshStatusText();
    SY_DEBUG("[WorkbenchWindow] retranslateUi completed");
}

/// 设置状态中心
/// @param stateCenter UI 状态中心
void WorkbenchWindow::setUiStateCenter(UiStateCenter* stateCenter)
{
    // 状态中心入口只负责替换源头引用，不在这里做额外状态编排
    m_stateCenter = stateCenter;
    m_uiServices.stateCenter = stateCenter;
    if (m_actionManager)
    {
        m_actionManager->setStateCenter(stateCenter);
    }
    if (m_stateManager)
    {
        m_stateManager->setUiStateCenter(stateCenter);
    }
}

/// 设置操作总线
/// @param bus 操作总线
void WorkbenchWindow::setOperationBus(OperationBus* bus)
{
    m_operationBus = bus;
    m_uiServices.operationBus = bus;
    if (m_actionManager)
    {
        m_actionManager->setOperationBus(bus);
    }
}

void WorkbenchWindow::setFrameworkServices(const UiFrameworkServices& services)
{
    // 框架级能力统一从这里注入，后续错误、权限、性能都必须走同一条框架路径
    // 这里仅更新桥接对象，不主动触发任何 UI 刷新或工作台切换
    if (m_actionManager)
    {
        m_actionManager->setFrameworkServices(services);
    }
    if (m_stateManager)
    {
        m_stateManager->setFrameworkServices(services);
    }
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
    m_uiServices = services;
    m_stateCenter = services.stateCenter;

    // 单位管理器：状态栏坐标按当前显示单位换算，切换单位时实时刷新
    if (m_unitManager && m_unitManager != services.unitManager)
    {
        disconnect(m_unitManager, &UnitManager::sigDisplayUnitChanged, this, &WorkbenchWindow::refreshPositionLabel);
    }
    m_unitManager = services.unitManager;
    if (m_unitManager)
    {
        connect(m_unitManager, &UnitManager::sigDisplayUnitChanged, this, &WorkbenchWindow::refreshPositionLabel);
    }

    // 注入导入服务到文件拖放处理器（2D/3D 工作台共用统一导入入口）
    if (m_fileDropHandler)
    {
        m_fileDropHandler->setImportService(services.importService);
        // 图片/位图拖放导入需要访问 2D 场景管理器
        if (services.sceneEditService)
        {
            m_fileDropHandler->setSceneManager(services.sceneEditService->sceneManager());
        }
        // 注入图层管理器，用于将拖放的位图分配到位图图层
        if (services.layerManager)
        {
            m_fileDropHandler->setLayerManager(services.layerManager);
        }
    }

    if (m_actionManager)
    {
        m_actionManager->setStateCenter(services.stateCenter);
    }

    if (m_stateManager)
    {
        m_stateManager->setUiStateCenter(services.stateCenter);
    }

    if (m_layoutManager)
    {
        m_layoutManager->setPersistenceService(services.persistenceService);
    }

    if (m_menuManager)
    {
        m_menuManager->setOperationBus(services.operationBus);
        m_menuManager->setStateCenter(services.stateCenter);
        m_menuManager->setUiServices(&m_uiServices);
        m_menuManager->setWorkbench(m_workbench);
        if (m_workbench)
        {
            m_menuManager->rebuildAllMenus();
        }
    }
}

/// 设置当前工作台
/// @param workbench 工作台实例
void WorkbenchWindow::setWorkbench(UiWorkbench* workbench)
{
    m_workbench = workbench;
    if (m_menuManager)
    {
        m_menuManager->setWorkbench(workbench);
        // 构造函数中因无工作台跳过了菜单构建，注入工作台后必须重建，
        // 否则命令目录为空导致全部动作被禁用。
        if (workbench && !workbench->managesOwnMenus())
        {
            m_menuManager->rebuildAllMenus();
        }
    }
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
    // 信号绑定下沉到 WorkbenchStateManager，统一管理连接上下文与解绑范围
    if (m_stateManager)
    {
        m_stateManager->bindStateSignals();
    }
}

void WorkbenchWindow::unbindStateSignals()
{
    // 解绑下沉到 WorkbenchStateManager，避免与窗口其他连接互相干扰
    if (m_stateManager)
    {
        m_stateManager->unbindStateSignals();
    }
}

void WorkbenchWindow::initializeWorkbenchShell()
{
    if (m_menuManager)
    {
        // 无工作台时先不构建菜单：命令目录为空会导致全部动作被禁用并刷屏 Unknown command id，
        // 待 setWorkbench 注入工作台后由 rebuildAllMenus 统一构建。
        if (m_workbench)
        {
            m_menuManager->buildMenus();
        }
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
    QString initialWorkbenchId =
        m_stateManager ? m_stateManager->windowState().currentWorkbenchId : QStringLiteral("default");
    if (m_stateCenter)
    {
        initialWorkbenchId = m_stateCenter->currentWorkbenchId();
    }
    if (initialWorkbenchId.isEmpty() || initialWorkbenchId == QStringLiteral("default"))
    {
        initialWorkbenchId = QStringLiteral("2D");
    }
    if (m_menuManager)
    {
        m_menuManager->refreshWorkbenchMenuChecks(initialWorkbenchId);
    }
    updateWindowTitle();
}

QWidget* WorkbenchWindow::createInitialCentralWidget()
{
    return m_layoutManager->createInitialCentralWidget();
}

SceneTreePanel2D* WorkbenchWindow::sceneTreeDock() const
{
    return m_layoutManager->panelState().sceneTreeDock;
}

PropertiesPanelWidget* WorkbenchWindow::propertiesDock() const
{
    return m_layoutManager->panelState().propertiesDock;
}

void WorkbenchWindow::initializeToolBarSkeleton()
{
    m_layoutManager->initializeToolBarSkeleton();
}

void WorkbenchWindow::buildToolBars()
{
    m_layoutManager->buildToolBars();
}

void WorkbenchWindow::initializeDockAreaSkeleton()
{
    m_layoutManager->initializeDockAreaSkeleton();
}

void WorkbenchWindow::buildDockAreas()
{
    m_layoutManager->buildDockAreas();
}

/// 构建状态栏
void WorkbenchWindow::initializeStatusBarSkeleton()
{
    m_layoutManager->initializeStatusBarSkeleton();
}

void WorkbenchWindow::buildStatusBar()
{
    m_layoutManager->buildStatusBar();
}

// ==================== 状态栏挂载/卸载 ====================

void WorkbenchWindow::mountStatusBar(StatusBarBase* statusBarWidget)
{
    if (!statusBarWidget)
    {
        SY_WARN("[WorkbenchWindow] mountStatusBar: null widget, ignoring");
        return;
    }

    // 先卸载旧的，防止重复挂载
    if (m_activeStatusBar)
    {
        SY_WARN("[WorkbenchWindow] mountStatusBar: previous status bar still mounted, unmounting first");
        unmountStatusBar();
    }

    if (!m_layoutManager->panelState().statusBar)
    {
        m_layoutManager->panelState().statusBar = statusBar();
    }

    m_activeStatusBar = statusBarWidget;
    m_layoutManager->panelState().statusBar->addWidget(m_activeStatusBar, 1);
    m_activeStatusBar->show();
    if (m_stateManager)
    {
        m_stateManager->setActiveStatusBar(m_activeStatusBar);
    }

    SY_INFOF("[WorkbenchWindow] StatusBar mounted: %s", m_activeStatusBar->metaObject()->className());
}

void WorkbenchWindow::unmountStatusBar()
{
    if (!m_activeStatusBar)
    {
        return;
    }

    if (m_layoutManager->panelState().statusBar)
    {
        m_layoutManager->panelState().statusBar->removeWidget(m_activeStatusBar);
    }

    // 卸载时清空旧状态栏内容，避免切换 2D/3D 后残留上一次的显示
    m_activeStatusBar->clearAll();

    // 不在这里 delete —— StatusBarBase 的生命周期由创建它的 Workbench 负责
    // Workbench2D/Workbench3D 在析构时会清理自己创建的 StatusBar
    m_activeStatusBar = nullptr;
    if (m_stateManager)
    {
        m_stateManager->setActiveStatusBar(nullptr);
    }

    SY_DEBUG("[WorkbenchWindow] StatusBar unmounted");
}

/// 同步窗口本地状态与状态中心
void WorkbenchWindow::syncWindowStateFromStateCenter()
{
    if (m_stateManager)
    {
        m_stateManager->syncWindowStateFromStateCenter();
    }
}

void WorkbenchWindow::syncWorkbenchSelectionFromStateCenter()
{
    if (m_stateManager)
    {
        m_stateManager->syncWorkbenchSelectionFromStateCenter();
    }
}

void WorkbenchWindow::refreshStatusText()
{
    // 这里只刷新全局状态展示，不在此处拼接工作台业务流程
    // 具体实现已下沉到 WorkbenchStateManager，本方法保留为窗口的稳定入口
    if (m_stateManager)
    {
        m_stateManager->refreshStatusText();
    }
}

/// 从状态中心刷新界面
void WorkbenchWindow::refreshFromState()
{
    // 这里是框架层的总刷新入口，不把工作台实现逻辑写进来
    // 具体实现已下沉到 WorkbenchStateManager，本方法保留为窗口的稳定入口
    if (m_stateManager)
    {
        m_stateManager->refreshFromState();
    }
}

void WorkbenchWindow::updateWindowTitle()
{
    if (m_stateManager)
    {
        m_stateManager->updateWindowTitle();
    }
}

/// 应用样式表
/// @param styleSheet 样式表内容
void WorkbenchWindow::closeEvent(QCloseEvent* event)
{
    if (m_stateCenter && m_stateCenter->dirty())
    {
        auto result = QMessageBox::question(this,
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
            {
                // [E8-P1 修复] 同步等待保存完成，而非 fire-and-forget。
                // 旧代码直接 accept() 导致保存未完成窗口即关闭，数据静默丢失。
                OperationResult saveResult = m_uiServices.operationBus->run(OperationId::File_Save, {});
                if (!saveResult.success)
                {
                    // 保存失败：弹对话框让用户选择重试/放弃
                    auto retryResult = QMessageBox::warning(this,
                        tr("Save Failed"),
                        tr("Failed to save: %1\nDo you want to retry?").arg(saveResult.message),
                        QMessageBox::Retry | QMessageBox::Discard);
                    if (retryResult == QMessageBox::Retry)
                    {
                        event->ignore();
                        return;  // 用户选择重试，不关闭
                    }
                    // 用户选择放弃，继续关闭
                }
            }
        }
    }
    event->accept();
}

void WorkbenchWindow::dragEnterEvent(QDragEnterEvent* event)
{
    if (m_fileDropHandler && m_fileDropHandler->handleDragEnter(event))
    {
        return;
    }
    event->ignore();
}

void WorkbenchWindow::dragMoveEvent(QDragMoveEvent* event)
{
    if (m_fileDropHandler && m_fileDropHandler->handleDragMove(event))
    {
        return;
    }
    event->ignore();
}

void WorkbenchWindow::dragLeaveEvent(QDragLeaveEvent* event)
{
    if (m_fileDropHandler)
    {
        m_fileDropHandler->handleDragLeave(event);
        return;
    }
    event->accept();
}

void WorkbenchWindow::dropEvent(QDropEvent* event)
{
    if (m_fileDropHandler && m_fileDropHandler->handleDrop(event))
    {
        return;
    }
    event->ignore();
}

/// 设置视口缩放操作回调
/// @param handler 缩放操作处理函数
void WorkbenchWindow::setViewportZoomHandler(std::function<void(const QString&)> handler)
{
    if (m_menuManager)
    {
        m_menuManager->setViewportZoomHandler(std::move(handler));
    }
}

void WorkbenchWindow::setTestViewHandler(std::function<void()> handler)
{
    if (m_menuManager)
    {
        m_menuManager->setTestViewHandler(std::move(handler));
    }
}

void WorkbenchWindow::updatePositionLabel(double x, double y)
{
    m_lastMouseX = x;
    m_lastMouseY = y;
    m_hasMousePosition = true;
    refreshPositionLabel();
}

void WorkbenchWindow::refreshPositionLabel()
{
    if (!m_activeStatusBar || !m_hasMousePosition)
    {
        return;
    }

    if (m_unitManager)
    {
        const UnitManager::Unit unit = m_unitManager->displayUnit();
        const double dx = m_unitManager->fromBaseUnit(m_lastMouseX, unit);
        const double dy = m_unitManager->fromBaseUnit(m_lastMouseY, unit);
        m_activeStatusBar->setPositionText(
            tr("Position: (%1, %2) %3").arg(dx, 0, 'f', 2).arg(dy, 0, 'f', 2).arg(UnitManager::unitSymbol(unit)));
    }
    else
    {
        m_activeStatusBar->setPositionText(
            tr("Position: (%1, %2) mm").arg(m_lastMouseX, 0, 'f', 2).arg(m_lastMouseY, 0, 'f', 2));
    }
}

// ==================== 最近文件菜单实现 ====================

/// 将文件路径添加到最近文件列表
/// @param filePath 文件完整路径
void WorkbenchWindow::addRecentFile(const QString& filePath)
{
    if (filePath.isEmpty())
    {
        return;
    }

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
    {
        files.removeLast();
    }
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
            {
                result.append(QString::fromStdString(rec.filePath));
            }
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
    {
        return;
    }

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
    return m_layoutManager->registerDockWidget(title, widget, area);
}

/// 注册工具栏
/// @param title 工具栏标题
QToolBar* WorkbenchWindow::registerToolBar(const QString& title)
{
    return m_layoutManager->registerToolBar(title);
}

/// 清空工作台内容（移除所有注册的面板和工具栏）
void WorkbenchWindow::clearWorkbenchContent()
{
    const auto start = std::chrono::steady_clock::now();
    SY_DEBUGF("[WorkbenchWindow] clearing workbench content: toolbars=%d docks=%d shortcuts=%d",
        m_layoutManager->registeredToolBars().size(),
        m_layoutManager->registeredDocks().size(),
        m_actionManager ? m_actionManager->shortcutCount() : 0);

    // 1: 清理所有注册的全局快捷键（Qt::ApplicationShortcut 不会随父窗口销毁）
    // 必须在 UI 清理之前执行，避免快捷键仍指向已销毁的对象
    clearAllShortcuts();

    // 2: 卸载工作台状态栏 widget（由 StatusBarBase 子类管理，不在此处 delete）
    unmountStatusBar();

    // 3: 清理繁忙进度条（布局管理器只负责创建，框架在此显式回收）
    if (auto* sb = statusBar())
    {
        if (auto* progress = m_layoutManager->busyProgressBar())
        {
            sb->removeWidget(progress);
            progress->deleteLater();
        }
    }

    // 4: 委托布局管理器清理工具栏/菜单栏/停靠面板/中央控件并重建占位控件
    // 传入当前（即将被替换的）工作台：中央视口的 GL 释放是 2D/3D 差异化逻辑，
    // 由工作台自己的 releaseCentralWidgetGLResources 承担。
    // triggerWorkbench 在第 6 步才把 m_workbench 指向新工作台，所以此刻它仍是旧的。
    m_layoutManager->clearLayoutContent(m_workbench);

    // 5: 强制处理所有排队的 DeferredDelete 事件
    // 旧中央控件（如 RenderViewport2D）内部包含 QOpenGLWidget，
    // 延迟删除会导致其析构滞后于新视口（如 Viewport3D）的创建，
    // 两个 OpenGL widget 共存期间调用 makeCurrent() 易引发访问冲突。
    // 在此处立即刷出延迟删除队列，确保旧渲染资源在新视口初始化前彻底释放。
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);

    recordPerformance(QStringLiteral("WorkbenchWindow::clearWorkbenchContent"),
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count());
}

/// 注册全局快捷键（由工作台调用，切换时自动清理）
void WorkbenchWindow::registerShortcut(QShortcut* shortcut)
{
    if (m_actionManager)
    {
        m_actionManager->registerShortcut(shortcut);
    }
}

/// 注销全局快捷键
void WorkbenchWindow::unregisterShortcut(QShortcut* shortcut)
{
    if (m_actionManager)
    {
        m_actionManager->unregisterShortcut(shortcut);
    }
}

/// 清理所有注册的快捷键
void WorkbenchWindow::clearAllShortcuts()
{
    if (m_actionManager)
    {
        m_actionManager->clearAllShortcuts();
    }
}

void WorkbenchWindow::resetCommandStateToIdle()
{
    if (m_stateManager)
    {
        m_stateManager->resetCommandStateToIdle();
    }
}

void WorkbenchWindow::resetWorkbenchLocalMirror()
{
    if (m_stateManager)
    {
        m_stateManager->resetWorkbenchLocalMirror();
    }
}

void WorkbenchWindow::clearSelectionState()
{
    if (m_stateManager)
    {
        m_stateManager->clearSelectionState();
    }
}

void WorkbenchWindow::setWorkbenchSwitchContext(const QString& workbenchId, const QString& switchContextText)
{
    if (m_stateManager)
    {
        m_stateManager->setWorkbenchSwitchContext(workbenchId, switchContextText);
    }
}

void WorkbenchWindow::resetWorkbenchTransientState()
{
    const auto start = std::chrono::steady_clock::now();
    SY_DEBUG("[WorkbenchWindow] resetting transient workbench state");
    if (m_stateManager)
    {
        m_stateManager->resetWorkbenchTransientState();
    }

    recordPerformance(QStringLiteral("WorkbenchWindow::resetWorkbenchTransientState"),
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count());
}

void WorkbenchWindow::syncWorkbenchStateFromStateCenter()
{
    if (!m_stateCenter)
    {
        return;
    }

    const auto start = std::chrono::steady_clock::now();
    const auto state = m_stateCenter->snapshot();
    if (m_menuManager)
    {
        m_menuManager->refreshWorkbenchMenuChecks(state.currentWorkbenchId);
        m_menuManager->refreshThemeMenuChecks(state.currentThemeId);
    }
    updateWindowTitle();
    recordPerformance(QStringLiteral("WorkbenchWindow::syncWorkbenchStateFromStateCenter"),
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count());
}

/// 保存布局快照
/// @param workbenchId 工作台 ID
void WorkbenchWindow::saveLayoutSnapshot(const QString& workbenchId)
{
    // 布局快照只保存窗口外观，不保存业务状态；业务状态由状态中心负责
    const auto start = std::chrono::steady_clock::now();
    m_layoutManager->saveLayoutSnapshot(workbenchId);
    recordPerformance(QStringLiteral("WorkbenchWindow::saveLayoutSnapshot"),
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count());
}

/// 恢复布局快照
/// @param workbenchId 工作台 ID
void WorkbenchWindow::restoreLayoutSnapshot(const QString& workbenchId)
{
    // 布局恢复只还原窗口外观，不在这里恢复业务状态，避免状态源不统一
    const auto start = std::chrono::steady_clock::now();
    m_layoutManager->restoreLayoutSnapshot(workbenchId);
    recordPerformance(QStringLiteral("WorkbenchWindow::restoreLayoutSnapshot"),
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count());
}

/// 重新设置所有注册的 dock widget 的标题
/// restoreState() 会覆盖 dock 标题，需要在恢复后调用此方法
void WorkbenchWindow::restoreDockWidgetTitles()
{
    m_layoutManager->restoreDockWidgetTitles();
}

void WorkbenchWindow::setSkeletonDocksVisible(bool visible)
{
    m_layoutManager->setSkeletonDocksVisible(visible);

    // 工作台状态栏 widget 整体显示/隐藏（3D 工作台隐藏，2D 工作台显示）
    if (m_activeStatusBar)
    {
        m_activeStatusBar->setVisible(visible);
    }
}

/// 更新繁忙指示器
/// @param busy 是否繁忙
void WorkbenchWindow::updateBusyIndicator(bool busy)
{
    m_layoutManager->updateBusyIndicator(busy);
}

/// 刷新主题菜单选中状态
/// @param themeId 当前主题 ID
void WorkbenchWindow::refreshThemeMenuChecks(const QString& themeId)
{
    if (m_stateManager)
    {
        m_stateManager->windowState().currentThemeId = themeId;
    }
    updateWindowTitle();
    if (m_menuManager)
    {
        m_menuManager->refreshThemeMenuChecks(themeId);
    }
}

/// 触发主题切换
/// @param themeId 主题 ID（如 "theme.dark"、"theme.light"、"theme.system"）
void WorkbenchWindow::triggerTheme(const QString& themeId)
{
    // 解析命令 ID（"theme.dark" → AppTheme::Dark）并委托 ThemeManager 应用
    const QString name = themeId.mid(QStringLiteral("theme.").size()).toLower();
    AppTheme appTheme = AppTheme::Light;
    if (name == QStringLiteral("dark"))
        appTheme = AppTheme::Dark;
    else if (name == QStringLiteral("blue"))
        appTheme = AppTheme::Blue;
    else if (name == QStringLiteral("slate"))
        appTheme = AppTheme::Slate;
    else if (name == QStringLiteral("highcontrast"))
        appTheme = AppTheme::HighContrast;
    else if (name == QStringLiteral("system"))
        appTheme = AppTheme::System;
    else if (name == QStringLiteral("default"))
        appTheme = AppTheme::Default;

    TM->setTheme(appTheme);

    if (m_stateCenter)
    {
        m_stateCenter->setCurrentThemeId(themeId);
        // 主题状态变化后同步本地状态，避免窗口与状态中心短时间不一致
        if (m_stateManager)
        {
            m_stateManager->windowState().currentThemeId = themeId;
        }
    }
    updateWindowTitle();

    refreshThemeMenuChecks(themeId);
}

void WorkbenchWindow::recordPerformance(const QString& scope, qint64 elapsedMs)
{
    if (m_actionManager)
    {
        m_actionManager->recordPerformance(scope, elapsedMs);
    }
}

void WorkbenchWindow::reportFrameworkError(const QString& errorCode, const QString& message, const QString& context)
{
    if (m_actionManager)
    {
        m_actionManager->reportFrameworkError(errorCode, message, context);
    }
}

bool WorkbenchWindow::canExecuteCommand(const QString& commandId, const QString& context) const
{
    if (m_actionManager)
    {
        return m_actionManager->canExecuteCommand(commandId, context);
    }

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
}  // namespace

void WorkbenchWindow::triggerWorkbench(const QString& workbenchId)
{
    const auto start = std::chrono::steady_clock::now();
    SY_DEBUGF("[WorkbenchWindow] triggerWorkbench: switching to %s", workbenchId.toUtf8().constData());

    // 保护：防止重复切换（快速连续点击可能导致状态混乱）
    if (m_switchingWorkbench)
    {
        SY_WARN("[WorkbenchWindow] triggerWorkbench: already switching, ignoring request");
        return;
    }

    if (!canExecuteCommand(QStringLiteral("workbench.switch.%1").arg(workbenchId),
            QStringLiteral("WorkbenchWindow::triggerWorkbench")))
    {
        reportFrameworkError(QStringLiteral("workbench.switch_denied"),
            QStringLiteral("Workbench switch denied: %1").arg(workbenchId),
            QStringLiteral("WorkbenchWindow::triggerWorkbench"));

        recordPerformance(QStringLiteral("WorkbenchWindow::triggerWorkbench.denied"),
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count());
        return;
    }

    if (!m_workbench)
    {
        SY_DEBUGF(
            "[WorkbenchWindow] triggerWorkbench: no workbench, setting state to %s", workbenchId.toUtf8().constData());
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

    const QString currentWorkbenchId =
        m_stateManager ? m_stateManager->windowState().currentWorkbenchId : QStringLiteral("default");
    if (workbenchId.compare(currentWorkbenchId, Qt::CaseInsensitive) == 0)
    {
        SY_DEBUGF("[WorkbenchWindow] triggerWorkbench: same workbench %s, skipping", workbenchId.toUtf8().constData());
        if (m_menuManager)
        {
            m_menuManager->refreshWorkbenchMenuChecks(workbenchId);
        }
        recordPerformance(QStringLiteral("WorkbenchWindow::triggerWorkbench.sameWorkbench"),
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count());
        return;
    }

    // 设置切换中标志，防止重复触发
    m_switchingWorkbench = true;

    const auto previousWorkbenchId =
        m_stateManager ? m_stateManager->windowState().currentWorkbenchId : QStringLiteral("default");
    const auto switchContextText = workbenchSwitchText(workbenchId);

    SY_DEBUGF("[WorkbenchWindow] triggerWorkbench: switching from %s to %s",
        previousWorkbenchId.toUtf8().constData(),
        workbenchId.toUtf8().constData());

    // 1: 保存旧工作台布局快照，标记繁忙
    if (m_stateCenter)
    {
        saveLayoutSnapshot(previousWorkbenchId);
        m_stateCenter->setBusy(true);
        m_stateCenter->setMetadata({ { QStringLiteral("viewportStatus"), QStringLiteral("Switching") } });
    }

    // 2: 停用旧工作台（释放资源、清理快捷键等）
    SY_DEBUG("[WorkbenchWindow] triggerWorkbench: deactivating old workbench");
    m_workbench->deactivate();
    recordPerformance(QStringLiteral("WorkbenchWindow::triggerWorkbench.deactivate"),
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count());

    // 3: 清理旧工作台的 transient 状态（命令、选择、镜像状态）
    SY_DEBUG("[WorkbenchWindow] triggerWorkbench: resetting transient state");
    resetWorkbenchTransientState();

    // 4: 清除旧工作台 UI 内容（面板、工具栏、快捷键、中央控件）
    SY_DEBUG("[WorkbenchWindow] triggerWorkbench: clearing workbench content");
    clearWorkbenchContent();

    // 切换时同步清理一些容易残留的 UI 语义，避免 2D/3D 视觉状态串线
    if (m_stateCenter)
    {
        QVariantMap meta = m_stateCenter->metadata();
        meta.remove(QStringLiteral("rightPanelSource"));
        meta.remove(QStringLiteral("drawToolSource"));
        meta.remove(QStringLiteral("activeToolId"));
        m_stateCenter->setMetadata(meta);
    }

    // 5: 设置新工作台上下文（在 attach 之前，确保新工作台能看到正确的状态）
    setWorkbenchSwitchContext(workbenchId, switchContextText);

    // 6: 通过工厂获取或创建新工作台实例
    if (m_workbenchFactory)
    {
        auto* target = m_workbenchFactory(workbenchId);
        if (target && target != m_workbench)
        {
            SY_DEBUGF("[WorkbenchWindow] triggerWorkbench: factory created new workbench %s",
                workbenchId.toUtf8().constData());
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
    // 新工作台自行决定是否需要 WorkbenchMenuManager 刷新菜单
    if (!m_workbench->managesOwnMenus() && m_menuManager)
    {
        m_menuManager->rebuildAllMenus();
    }
    // 状态栏 widget 由各 Workbench 在 attachToWindow 中创建并通过 mountStatusBar 挂载，
    // 此处不再需要手动 buildStatusBar() 重建
    // 新工作台自行决定是否需要骨架停靠面板
    setSkeletonDocksVisible(m_workbench->requiresSkeletonDocks());
    refreshFromState();
    refreshStatusText();
    updateWindowTitle();

    if (m_stateCenter)
    {
        m_stateCenter->setBusy(false);
    }
    if (m_stateManager)
    {
        m_stateManager->windowState().busy = false;
        m_stateManager->windowState().currentWorkbenchId = workbenchId;
    }
    SY_DEBUGF("[WorkbenchWindow] workbench state committed: id=%s busy=0", workbenchId.toUtf8().constData());

    // 取消切换中标志，允许下次切换
    m_switchingWorkbench = false;

    SY_INFOF("[WorkbenchWindow] triggerWorkbench: switch completed to %s", workbenchId.toUtf8().constData());
    recordPerformance(QStringLiteral("WorkbenchWindow::triggerWorkbench.switch"),
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count());
}