#include "WorkbenchMenuManager.h"
#include "WorkbenchWindow.h"

#include "Log/SyLogger.h"
#include "UI2D/Operation/CommandCatalog.h"
#include "UI2D/Operation/OperationBus.h"
#include "UI2D/Operation/OperationId.h"
#include "UiStateCenter.h"
#include "UiThemeService.h"
#include "UiWorkbench.h"
#include "UiServices.h"
#include "UiFrameworkServices.h"
#include "UI/ClientConfig/UiLayoutBuilder.h"
#include "UI/LanguageManager.h"
#include "UI/ThemeManager.h"
#include "UI/IconHelper.h"
#include "UI/Dlg/LayerManagerDialog.h"
#include "Engine2D/Interaction/LayerManager.h"
#include "Engine2D/Edit/LayerEditService.h"
#include "UI/RightToolBar/RightToolBar.h"
#include "UI/TopToolBar/TopToolBar.h"
#include "UI/StatusBar/StatusBar.h"
#include "UI/StatusBar/StatusBar3D.h"
#include "UI/Widgets/UiSceneTreeDock.h"
#include "UI/Widgets/UiPropertiesPanel.h"
#include "Render3D/RenderWidget3D.h"
#include "ClientConfig/UiClientConfigBase.h"
#ifdef SANYI_ENABLE_CONFIG_DRIVEN_UI
#include "ClientConfig/UiConfigurationManager.h"
#include "ClientConfig/UiLayoutBuilder.h"
#include "ClientConfig/UiPanelRegistry.h"
#endif

#include <QAction>
#include <QActionGroup>
#include <QInputDialog>
#include <QLineEdit>
#include <QMenuBar>
#include <QSet>
#include <QSignalBlocker>
#include <QFileInfo>
#include <QKeySequence>
#include <QStringList>
#include <functional>

namespace
{
    inline QAction* setCmdId(QAction* action, const QString& cmdId)
    {
        if (action)
        {
            action->setData(cmdId);
            // 统一命令标识存储：与 UiLayoutBuilder 写入的 property("commandId") 约定一致，
            // 使 bindConfiguredMenuState / 测试 / 日志统一按同一 property 读取，避免新旧路径失配。
            action->setProperty("commandId", cmdId);
        }
        return action;
    }

    // 统一的菜单点击日志，2D 菜单均遵循该格式，便于按 command 检索。
    void logMenuTrigger(const QString& text, const QString& commandId)
    {
        SY_INFOF("[Menu] trigger text='%s' command='%s'", qPrintable(text), qPrintable(commandId));
    }

    // 清空菜单全部动作，并连带删除子菜单对象。
    // 相比 qDeleteAll(menu->actions())，直接删除子菜单 QAction 不会销毁其 QMenu，
    // 会造成孤儿子菜单在多次局部刷新间累积（泄漏）；这里显式 delete 子菜单，一并回收。
    void clearMenuActions(QMenu* menu)
    {
        if (!menu)
            return;
        const auto actions = menu->actions();
        for (QAction* act : actions)
        {
            if (!act)
                continue;
            if (QMenu* sub = act->menu())
            {
                delete sub;   // 子菜单析构会连带删除其 menuAction，不可再 delete act
            }
            else
            {
                delete act;
            }
        }
    }

    // 为 2D 传统菜单项解析图标：优先取 CommandCatalog 中注册的资源，否则使用显式回退路径。
    void applyMenuIcon(QAction* action, const QString& commandId, const char* fallbackResource = nullptr)
    {
        if (!action)
            return;
        QString res;
        if (!commandId.isEmpty())
        {
            const CommandEntry2D* entry =
                CommandCatalog::findByOperation(CommandCatalog::operationForCommandId(commandId));
            if (entry && entry->iconResource)
                res = QString::fromUtf8(entry->iconResource);
        }
        if (res.isEmpty() && fallbackResource)
            res = QString::fromUtf8(fallbackResource);
        if (!res.isEmpty())
            IconHelper::setThemedIcon(action, res);
    }

#ifdef SANYI_ENABLE_CONFIG_DRIVEN_UI
    QString currentWorkbenchId(const UiStateCenter* stateCenter)
    {
        return stateCenter ? stateCenter->currentWorkbenchId() : QStringLiteral("2D");
    }
#endif

    bool commandEnabledForWorkbench(const QStringList& workbenches, const QString& workbenchId)
    {
        if (workbenches.isEmpty())
            return true;
        for (const auto& wb : workbenches)
        {
            if (wb.compare(workbenchId, Qt::CaseInsensitive) == 0)
                return true;
        }
        return false;
    }

    void logFilteredCommand(const QString& commandId, const QString& workbenchId, const char* reason)
    {
        SY_DEBUGF("[WorkbenchMenuManager] Filtered command='%s' workbench='%s' reason=%s",
            qPrintable(commandId), qPrintable(workbenchId), reason);
    }
}

// 配置驱动菜单的命令分发器：文件作用域定义，便于 WorkbenchMenuManager 以成员方式持有，
// 保证 UiLayoutBuilder 在 QAction 触发回调中取到的分发器指针长期有效（不可用栈上临时对象）。
struct MenuDispatcher final : public IUiCommandDispatcher
{
    WorkbenchMenuManager* self = nullptr;
    UiWorkbench* workbench = nullptr;

    // 工作台切换命令属于窗口级动作（不进入命令总线/工作台命令目录），
    // 在分发器层面放行，保证 JSON 菜单中的 Switch to 2D/3D 始终可点击。
    static bool isWorkbenchSwitchCommand(const QString& commandId)
    {
        return commandId == QLatin1String("view.switch_to_2d")
            || commandId == QLatin1String("view.switch_to_3d");
    }

    bool isCommandRegistered(const QString& commandId) const override
    {
        if (isWorkbenchSwitchCommand(commandId))
            return true;
        return workbench && workbench->isCommandRegistered(commandId);
    }

    void dispatch(const QString& commandId) override
    {
        // 工作台切换统一由主窗口 triggerWorkbench 处理（含防重复切换保护），
        // 与 legacy 路径 View 菜单的 Switch 动作行为保持一致。
        if (isWorkbenchSwitchCommand(commandId) && self && self->workbenchWindow())
        {
            const QString target = commandId == QLatin1String("view.switch_to_3d")
                ? QStringLiteral("3D") : QStringLiteral("2D");
            SY_INFOF("[WorkbenchMenuManager] dispatch workbench-switch command='%s' target='%s'",
                qPrintable(commandId), qPrintable(target));
            self->workbenchWindow()->triggerWorkbench(target);
            return;
        }
        if (!workbench)
        {
            SY_WARNF("[WorkbenchMenuManager] No active workbench for command='%s'",
                qPrintable(commandId));
            return;
        }
        SY_INFOF("[Menu] dispatch workbench='%s' command='%s'",
            qPrintable(workbench->id()), qPrintable(commandId));
        workbench->dispatchCommand(commandId);
    }
};

WorkbenchMenuManager::WorkbenchMenuManager(WorkbenchWindow* window, QObject* parent)
    : QObject(parent)
    , m_window(window)
{
}

WorkbenchMenuManager::~WorkbenchMenuManager() = default;

void WorkbenchMenuManager::setOperationBus(OperationBus* bus)
{
    m_operationBus = bus;
}

void WorkbenchMenuManager::setStateCenter(UiStateCenter* stateCenter)
{
    m_stateCenter = stateCenter;
}

void WorkbenchMenuManager::setThemeService(UiThemeService* themeService)
{
    m_themeService = themeService;
}

void WorkbenchMenuManager::setFrameworkServices(const UiFrameworkServices* services)
{
    m_frameworkServices = services;
}

void WorkbenchMenuManager::setUiServices(const UiServices* services)
{
    m_uiServices = services;
}

void WorkbenchMenuManager::setWorkbench(UiWorkbench* workbench)
{
    m_workbench = workbench;
}

void WorkbenchMenuManager::dispatchCommandSafely(const QString& commandId)
{
    if (m_workbench)
    {
        m_workbench->dispatchCommand(commandId);
        return;
    }
    if (m_operationBus)
    {
        const OperationId opId = CommandCatalog::operationForCommandId(commandId);
        if (opId != OperationId::None)
            m_operationBus->run(opId);
    }
}

QAction* WorkbenchMenuManager::addMenuAction(QMenu* menu, const QString& text,
    const QString& commandId, const QString& fallbackIcon, int options)
{
    QAction* action = menu->addAction(text);
    if (options & MenuActionOption_Checkable)
        action->setCheckable(true);

    // 主题切换类菜单项：commandId 即主题 ID，不进入命令总线
    if (options & MenuActionOption_Theme)
    {
        connect(action, &QAction::triggered, this, [this, text, commandId]() {
            logMenuTrigger(text, commandId);
            m_window->triggerTheme(commandId);
            });
        return action;
    }

    setCmdId(action, commandId);
    applyMenuIcon(action, commandId, fallbackIcon.isEmpty() ? nullptr : fallbackIcon.toUtf8().constData());

    // 统一分发：全部走 dispatchCommandSafely（优先当前工作台 dispatchCommand，无工作台时回退 OperationBus）。
    // 2D 绘图工具（toolName 类型 commandId）也由 Workbench2D::dispatchCommand 兜底调用 operationForToolName，
    // 与左侧工具栏/右键菜单保持同一条命令路径，不再在菜单层单独直连 OperationBus。
    connect(action, &QAction::triggered, this, [this, text, commandId]() {
        logMenuTrigger(text, commandId);
        dispatchCommandSafely(commandId);
        });
    return action;
}

void WorkbenchMenuManager::setWorkbenchFactory(WorkbenchFactory factory)
{
    m_workbenchFactory = std::move(factory);
}

void WorkbenchMenuManager::setViewportZoomHandler(std::function<void(const QString&)> handler)
{
    m_viewportZoomHandler = std::move(handler);
}

void WorkbenchMenuManager::rebuildAllMenus()
{
    // 先清理旧的全局快捷键动作，防止 2D 的 Undo/Redo 泄漏到 3D 模式
    clearGlobalShortcuts();
    if (auto* mb = m_window->menuBar())
        mb->clear();
    m_menuState = {};

#ifdef SANYI_ENABLE_CONFIG_DRIVEN_UI
    // 配置驱动菜单优先：如果可用，则直接由 JSON 菜单树生成。
    // 这样 2D / 3D 菜单就共享同一套 schema、图标、工作台可见性和日志行为。
    rebuildMenusFromConfig();
    bindConfiguredMenuState();
#else
    buildMenus();
#endif

    bindMenuCommands();
    // 全局 Undo/Redo 快捷键仅旧路径需要（配置驱动模式的快捷键由 JSON 统一生成）
#ifndef SANYI_ENABLE_CONFIG_DRIVEN_UI
    QString wbId = m_stateCenter ? m_stateCenter->currentWorkbenchId() : QStringLiteral("2D");
    if (wbId.compare(QStringLiteral("2D"), Qt::CaseInsensitive) == 0)
        bindShortcuts();
#endif
}

#ifdef SANYI_ENABLE_CONFIG_DRIVEN_UI
void WorkbenchMenuManager::rebuildMenusFromConfig()
{
    // 配置驱动菜单的原则：同一份 UiConfigData 同时驱动菜单、工具栏、Dock、快捷键。
    // 菜单部分由 UiLayoutBuilder 负责生成，WorkbenchMenuManager 负责接入工作台上下文与回退策略。
    if (!m_menuConfigManager)
        m_menuConfigManager = std::make_unique<UiConfigurationManager>();
    if (!m_menuPanelRegistry)
    {
        m_menuPanelRegistry = std::make_unique<UiPanelRegistry>();
        m_menuPanelRegistry->registerPanel(QStringLiteral("SceneTreePanel"),
            [](QWidget* parent) { return static_cast<QWidget*>(new SceneTreeDockWidget(parent)); });
        m_menuPanelRegistry->registerPanel(QStringLiteral("PropertiesPanel"),
            [](QWidget* parent) { return static_cast<QWidget*>(new PropertiesPanelWidget(parent)); });
    }

    const QString clientId = qEnvironmentVariableIsSet("SANYI_CLIENT_ID")
        ? QString::fromUtf8(qgetenv("SANYI_CLIENT_ID"))
        : QStringLiteral("san_yi");
    const QString resourcePath = QStringLiteral(":/configs/%1.json").arg(clientId);

    // 这里使用配置管理器统一加载客户配置；失败则回退到 san_yi.json
    const bool loaded = m_menuConfigManager->applyConfiguration(resourcePath, ConfigFallbackPolicy::Fallback);
    const UiConfigData* config = m_menuConfigManager->configData();
    if (!loaded || !config)
    {
        // 直接回退到旧路径（不能调用 buildMenus()，否则配置模式下会与本站形成递归）
        SY_WARNF("[WorkbenchMenuManager] Config menu build failed, fallback to legacy menu path. resource=%s",
            qPrintable(resourcePath));
        buildLegacyMenus();
        return;
    }

    // 命令分发器随 WorkbenchMenuManager 生命周期持有（成员 m_dispatcher）：
    // UiLayoutBuilder 会把该指针存入 QAction 触发回调并长期解引用，必须保证指针在菜单存在期间有效。
    if (!m_dispatcher)
    {
        m_dispatcher = std::make_unique<MenuDispatcher>();
        m_dispatcher->self = this;
    }
    m_dispatcher->workbench = m_workbench;
    m_menuLayoutBuilder = std::make_unique<UiLayoutBuilder>(m_window, m_dispatcher.get(), m_menuPanelRegistry.get());
    m_menuLayoutBuilder->clearBuiltLayout();

    // 菜单/工具栏由同一个配置对象生成；菜单项会根据 workbenches 字段与当前工作台命令目录双重过滤。
    const QString wbId = currentWorkbenchId(m_stateCenter);
    // 菜单完整性策略：JSON 声明即展示（配合 workbenches / visibilityScope 过滤）。
    // 未注册的命令由 UiLayoutBuilder::bindAction 置灰，而不是整体隐藏，保证 2D/3D 结构一致。
    const auto commandAvailable = [this](const QString& commandId) {
        if (commandId.isEmpty())
            return false;
        if (!m_workbench)
            return true;
        return m_workbench->isCommandRegistered(commandId);
        };

    std::vector<MenuDef> filteredMenus = filterMenusForWorkbench(
        config->menus,
        wbId,
        commandAvailable,
        m_workbench ? m_workbench->id() : QString());

    // 职责划分：WorkbenchMenuManager 只负责菜单与快捷键；
    // 工具栏 / Dock 由 WorkbenchLayoutManager（buildToolBars / buildDockAreasFromConfig）统一构建，
    // 避免同一份配置被两处重复消费导致清理与快照的数据竞争。
    std::vector<ShortcutDef> filteredShortcuts;
    filteredShortcuts.reserve(config->shortcuts.size());
    for (const auto& shortcut : config->shortcuts)
    {
        if (!shortcut.commandId.isEmpty() && commandAvailable(shortcut.commandId))
            filteredShortcuts.push_back(shortcut);
    }

    m_menuLayoutBuilder->buildMenus(filteredMenus);
    m_menuLayoutBuilder->buildShortcuts(filteredShortcuts);

    SY_INFOF("[WorkbenchMenuManager] Config-driven menus built: client='%s', workbench='%s', menus=%zu, shortcuts=%zu",
        qPrintable(clientId), qPrintable(wbId), filteredMenus.size(), filteredShortcuts.size());
}
#endif

void WorkbenchMenuManager::createBaseMenus()
{
    initializeMenuSkeleton();
}

void WorkbenchMenuManager::initializeMenuSkeleton()
{
#ifdef SANYI_ENABLE_CONFIG_DRIVEN_UI
    rebuildMenusFromConfig();
#else
    buildMenus();
#endif
}

void WorkbenchMenuManager::buildMenus()
{
#ifdef SANYI_ENABLE_CONFIG_DRIVEN_UI
    rebuildMenusFromConfig();
    bindConfiguredMenuState();
#else
    buildLegacyMenus();
#endif
}

void WorkbenchMenuManager::buildLegacyMenus()
{
    // 旧路径保留：用于未启用配置驱动时的兼容构建。
    // 顶层菜单统一为 File / Edit / View / Tools / Help，
    // Draw / Modify / Algorithm 收敛为子菜单，与配置驱动的 schema 保持一致（2D/3D 同一结构）。
    m_menuState.fileMenu = m_window->menuBar()->addMenu(m_window->tr("File"));
    m_menuState.editMenu = m_window->menuBar()->addMenu(m_window->tr("Edit"));
    m_menuState.viewMenu = m_window->menuBar()->addMenu(m_window->tr("View"));
    m_menuState.toolsMenu = m_window->menuBar()->addMenu(m_window->tr("Tools"));
    m_menuState.helpMenu = m_window->menuBar()->addMenu(m_window->tr("Help"));

    // Draw / Algorithm 作为 Tools 的子菜单；Modify 由 refreshEditMenuForWorkbench 建立到 Edit 下。
    m_menuState.drawMenu = m_menuState.toolsMenu->addMenu(m_window->tr("Draw"));
    m_menuState.algorithmMenu = m_menuState.toolsMenu->addMenu(m_window->tr("Algorithm"));
    m_menuState.toolsMenu->addSeparator();

    buildFileMenu();
    buildViewMenu();
    buildHelpMenu();
    QString initialWorkbenchId = m_stateCenter ? m_stateCenter->currentWorkbenchId() : QStringLiteral("2D");
    // Modify 子菜单由 refreshEditMenuForWorkbench 一并建立，避免重复刷新
    refreshEditMenuForWorkbench(initialWorkbenchId);
    refreshDrawMenuForWorkbench(initialWorkbenchId);
    refreshAlgorithmMenuForWorkbench(initialWorkbenchId);
}

void WorkbenchMenuManager::buildFileMenu()
{
    if (!m_menuState.fileMenu)
        return;

    auto* newAction = m_menuState.fileMenu->addAction(m_window->tr("New"));
    newAction->setShortcut(QKeySequence::New);
    auto* openAction = m_menuState.fileMenu->addAction(m_window->tr("Open..."));
    openAction->setShortcut(QKeySequence::Open);
    setCmdId(newAction, QStringLiteral("file.new"));
    setCmdId(openAction, QStringLiteral("file.open"));
    IconHelper::setThemedIcon(newAction, QStringLiteral(":/ui/common/Icons/File/new.svg"));
    IconHelper::setThemedIcon(openAction, QStringLiteral(":/ui/common/Icons/File/open.svg"));
    connect(newAction, &QAction::triggered, this, [this]() {
        logMenuTrigger(m_window->tr("New"), QStringLiteral("file.new"));
        dispatchCommandSafely(QStringLiteral("file.new"));
        });
    connect(openAction, &QAction::triggered, this, [this]() {
        logMenuTrigger(m_window->tr("Open..."), QStringLiteral("file.open"));
        dispatchCommandSafely(QStringLiteral("file.open"));
        });
    m_menuState.fileMenu->addSeparator();

    auto* saveAction = m_menuState.fileMenu->addAction(m_window->tr("Save"));
    saveAction->setShortcut(QKeySequence::Save);
    auto* saveAsAction = m_menuState.fileMenu->addAction(m_window->tr("Save As..."));
    saveAsAction->setShortcut(QKeySequence::SaveAs);
    setCmdId(saveAction, QStringLiteral("file.save"));
    setCmdId(saveAsAction, QStringLiteral("file.save_as"));
    IconHelper::setThemedIcon(saveAction, QStringLiteral(":/ui/common/Icons/File/save.svg"));
    IconHelper::setThemedIcon(saveAsAction, QStringLiteral(":/ui/common/Icons/File/save_as.svg"));
    connect(saveAction, &QAction::triggered, this, [this]() {
        logMenuTrigger(m_window->tr("Save"), QStringLiteral("file.save"));
        dispatchCommandSafely(QStringLiteral("file.save"));
        });
    connect(saveAsAction, &QAction::triggered, this, [this]() {
        logMenuTrigger(m_window->tr("Save As..."), QStringLiteral("file.save_as"));
        dispatchCommandSafely(QStringLiteral("file.save_as"));
        });
    m_menuState.fileMenu->addSeparator();

    refreshFileMenuForWorkbench(m_stateCenter ? m_stateCenter->currentWorkbenchId() : QStringLiteral("2D"));

    m_menuState.fileMenu->addSeparator();
    m_menuState.recentFilesMenu = m_menuState.fileMenu->addMenu(m_window->tr("Recent Files"));
    m_menuState.recentFilesMenu->setIcon(IconHelper::themedIcon(QStringLiteral(":/ui/common/Icons/File/recent.svg")));
    m_window->populateRecentFilesMenu();

    m_menuState.fileMenu->addSeparator();
    auto* exitAction = m_menuState.fileMenu->addAction(m_window->tr("Exit"));
    exitAction->setShortcut(QKeySequence::Quit);
    IconHelper::setThemedIcon(exitAction, QStringLiteral(":/ui/common/Icons/File/exit.svg"));
    QObject::connect(exitAction, &QAction::triggered, m_window, &QWidget::close);
}

void WorkbenchMenuManager::refreshFileMenuForWorkbench(const QString& workbenchId)
{
    if (!m_menuState.fileMenu)
        return;

    if (m_menuState.importMenu)
    {
        m_menuState.fileMenu->removeAction(m_menuState.importMenu->menuAction());
        delete m_menuState.importMenu;
        m_menuState.importMenu = nullptr;
    }
    if (m_menuState.exportMenu)
    {
        m_menuState.fileMenu->removeAction(m_menuState.exportMenu->menuAction());
        delete m_menuState.exportMenu;
        m_menuState.exportMenu = nullptr;
    }

    // legacy 旧路径下 3D 菜单由 Workbench3D / MenuManager3D 自理（managesOwnMenus=true），
    // 本函数只服务 2D；3D 相关目录过滤（CommandCatalog3D）已随死分支删除，见架构文档。
    if (workbenchId.compare(QStringLiteral("3D"), Qt::CaseInsensitive) == 0)
    {
        SY_DEBUGF("[WorkbenchMenuManager] refreshFileMenuForWorkbench skip 3D (managed by MenuManager3D)");
        return;
    }

    m_menuState.importMenu = m_menuState.fileMenu->addMenu(m_window->tr("Import"));
    m_menuState.importMenu->setIcon(IconHelper::themedIcon(QStringLiteral(":/ui/common/Icons/File/import.svg")));

    const QStringList importFormats = {
        m_window->tr("DXF (*.dxf)"), m_window->tr("PLT (*.plt, *.hpgl)"),
        m_window->tr("SVG (*.svg)"), m_window->tr("PDF (*.pdf)")
    };

    const QStringList importCmdIds = {
        QStringLiteral("file.import_dxf"), QStringLiteral("file.import_plt"),
        QStringLiteral("file.import_svg"), QStringLiteral("file.import_pdf")
    };

    for (int i = 0; i < importFormats.size(); ++i)
    {
        addMenuAction(m_menuState.importMenu, importFormats[i], importCmdIds[i]);
    }

    m_menuState.importMenu->addSeparator();
    addMenuAction(m_menuState.importMenu, m_window->tr("Image..."), QStringLiteral("file.import_image"));

    m_menuState.exportMenu = m_menuState.fileMenu->addMenu(m_window->tr("Export"));
    m_menuState.exportMenu->setIcon(IconHelper::themedIcon(QStringLiteral(":/ui/common/Icons/File/export.svg")));

    const QStringList exportFormats = {
        m_window->tr("DXF (*.dxf)"), m_window->tr("SVG (*.svg)"), m_window->tr("PLT (*.plt)"),
        m_window->tr("BMP (*.bmp)"), m_window->tr("PNG (*.png)")
    };
    const QStringList exportCmdIds = {
        QStringLiteral("file.export_dxf"), QStringLiteral("file.export_svg"),
        QStringLiteral("file.export_plt"), QStringLiteral("file.export_bmp"),
        QStringLiteral("file.export_png")
    };
    for (int i = 0; i < exportFormats.size(); ++i)
    {
        addMenuAction(m_menuState.exportMenu, exportFormats[i], exportCmdIds[i]);
    }
}

void WorkbenchMenuManager::buildViewMenu()
{
    if (!m_menuState.viewMenu)
        return;

    m_menuState.viewMenu->addSeparator();

    QString currentWorkbenchId = m_stateCenter ? m_stateCenter->currentWorkbenchId() : QStringLiteral("2D");

    if (currentWorkbenchId == QStringLiteral("2D"))
    {
        m_menuState.workbench3DAction = m_menuState.viewMenu->addAction(m_window->tr("Switch to 3D"));
        m_menuState.workbench3DAction->setCheckable(true);
        IconHelper::setThemedIcon(m_menuState.workbench3DAction, QStringLiteral(":/ui/common/Icons/View/switch_to_3d.svg"));
        QObject::connect(m_menuState.workbench3DAction, &QAction::triggered, this, [this]() {
            logMenuTrigger(m_window->tr("Switch to 3D"), QStringLiteral("view.switch_to_3d"));
            m_window->triggerWorkbench(QStringLiteral("3D"));
            });
    }
    else if (currentWorkbenchId == QStringLiteral("3D"))
    {
        m_menuState.workbench2DAction = m_menuState.viewMenu->addAction(m_window->tr("Switch to 2D"));
        m_menuState.workbench2DAction->setCheckable(true);
        IconHelper::setThemedIcon(m_menuState.workbench2DAction, QStringLiteral(":/ui/common/Icons/View3D/switch_to_2d.svg"));
        QObject::connect(m_menuState.workbench2DAction, &QAction::triggered, this, [this]() {
            logMenuTrigger(m_window->tr("Switch to 2D"), QStringLiteral("view.switch_to_2d"));
            m_window->triggerWorkbench(QStringLiteral("2D"));
            });
    }

    m_menuState.viewMenu->addSeparator();

    m_menuState.layerMenu = m_menuState.viewMenu->addMenu(m_window->tr("Layer"));
    m_menuState.layerMenu->setIcon(IconHelper::themedIcon(QStringLiteral(":/ui/common/Icons/View/layers.svg")));
    auto* layerMgr = m_menuState.layerMenu->addAction(m_window->tr("Layer Manager..."));
    IconHelper::setThemedIcon(layerMgr, QStringLiteral(":/ui/common/Icons/View/layers.svg"));
    QObject::connect(layerMgr, &QAction::triggered, this, [this]() {
        logMenuTrigger(m_window->tr("Layer Manager..."), QStringLiteral("view.layer_manager"));
        if (m_uiServices && m_uiServices->layerEditService)
        {
            auto* w = qobject_cast<WorkbenchWindow*>(m_window);
            if (w)
                LayerManagerDialog::showDialog(m_uiServices->layerEditService, w);
        }
        });
    m_menuState.layerMenu->addSeparator();
    auto* newLayer = m_menuState.layerMenu->addAction(m_window->tr("New Layer"));
    IconHelper::setThemedIcon(newLayer, QStringLiteral(":/ui/common/Icons/View/layer_new.svg"));
    QObject::connect(newLayer, &QAction::triggered, this, [this]() {
        logMenuTrigger(m_window->tr("New Layer"), QStringLiteral("view.layer_new"));
        if (!m_uiServices || !m_uiServices->layerEditService)
            return;
        int id = m_uiServices->layerEditService->createLayer();
        if (id >= 0)
            SY_INFOF("[WorkbenchMenuManager] New layer created, id=%d", id);
        else
            SY_ERRORF("[WorkbenchMenuManager] Failed to create layer, id=%d", id);
        });
    auto* delLayer = m_menuState.layerMenu->addAction(m_window->tr("Delete Layer"));
    IconHelper::setThemedIcon(delLayer, QStringLiteral(":/ui/common/Icons/View/layer_delete.svg"));
    QObject::connect(delLayer, &QAction::triggered, this, [this]() {
        logMenuTrigger(m_window->tr("Delete Layer"), QStringLiteral("view.layer_delete"));
        if (!m_uiServices || !m_uiServices->layerEditService || !m_uiServices->layerManager)
            return;
        int currentId = m_uiServices->layerManager->currentLayerId();
        if (currentId < 0)
            return;
        m_uiServices->layerEditService->deleteLayer(currentId);
        });

    m_menuState.layerMenu->addSeparator();
    auto* layerCtxMenu = new QMenu(m_window->tr("More Layer Operations"), m_window);
    layerCtxMenu->setIcon(IconHelper::themedIcon(QStringLiteral(":/ui/common/Icons/View/layers.svg")));
    m_menuState.layerMenu->addMenu(layerCtxMenu);
    auto* renameLayer = layerCtxMenu->addAction(m_window->tr("Rename Layer"));
    IconHelper::setThemedIcon(renameLayer, QStringLiteral(":/ui/common/Icons/View/layer_rename.svg"));
    QObject::connect(renameLayer, &QAction::triggered, this, [this]() {
        logMenuTrigger(m_window->tr("Rename Layer"), QStringLiteral("view.layer_rename"));
        if (!m_uiServices || !m_uiServices->layerEditService || !m_uiServices->layerManager)
            return;
        int currentId = m_uiServices->layerManager->currentLayerId();
        if (currentId < 0)
            return;
        auto* w = qobject_cast<WorkbenchWindow*>(m_window);
        if (!w)
            return;
        bool ok = false;
        QString newName = QInputDialog::getText(w, m_window->tr("Rename Layer"),
            m_window->tr("New name:"), QLineEdit::Normal, QString(), &ok);
        if (ok && !newName.isEmpty())
        {
            m_uiServices->layerEditService->renameLayer(currentId, newName.toStdString());
            SY_INFOF("[WorkbenchMenuManager] Layer renamed: id=%d", currentId);
        }
        });

    auto* toggleLock = layerCtxMenu->addAction(m_window->tr("Toggle Lock"));
    IconHelper::setThemedIcon(toggleLock, QStringLiteral(":/ui/common/Icons/View/layer_lock.svg"));
    QObject::connect(toggleLock, &QAction::triggered, this, [this]() {
        logMenuTrigger(m_window->tr("Toggle Lock"), QStringLiteral("view.layer_toggle_lock"));
        if (!m_uiServices || !m_uiServices->layerManager)
            return;
        int currentId = m_uiServices->layerManager->currentLayerId();
        if (currentId < 0)
            return;
        bool locked = m_uiServices->layerManager->isLayerLocked(currentId);
        m_uiServices->layerManager->setLayerLocked(currentId, !locked);
        });

    auto* toggleVisible = layerCtxMenu->addAction(m_window->tr("Toggle Visibility"));
    IconHelper::setThemedIcon(toggleVisible, QStringLiteral(":/ui/common/Icons/View/layer_visible.svg"));
    QObject::connect(toggleVisible, &QAction::triggered, this, [this]() {
        logMenuTrigger(m_window->tr("Toggle Visibility"), QStringLiteral("view.layer_toggle_visible"));
        if (!m_uiServices || !m_uiServices->layerManager)
            return;
        int currentId = m_uiServices->layerManager->currentLayerId();
        if (currentId < 0)
            return;
        bool visible = m_uiServices->layerManager->isLayerVisible(currentId);
        m_uiServices->layerManager->setLayerVisible(currentId, !visible);
        });

    m_menuState.viewMenu->addSeparator();

    m_menuState.unitMenu = m_menuState.viewMenu->addMenu(m_window->tr("Unit"));
    m_menuState.unitMenu->setIcon(IconHelper::themedIcon(QStringLiteral(":/ui/common/Icons/View/unit.svg")));
    m_menuState.unitActionGroup = new QActionGroup(m_window);
    m_menuState.unitActionGroup->setExclusive(true);

    const struct
    {
        const char* text; const char* cmdId; bool checked;
    } units[] = {
{ "mm", "view.unit_mm", true },
{ "cm", "view.unit_cm", false },
{ "inch", "view.unit_inch", false }
    };
    for (const auto& u : units)
    {
        auto* act = m_menuState.unitMenu->addAction(m_window->tr(u.text));
        act->setCheckable(true);
        act->setChecked(u.checked);
        m_menuState.unitActionGroup->addAction(act);
        QString cmdId = QString::fromLatin1(u.cmdId);
        setCmdId(act, cmdId);
        connect(act, &QAction::triggered, this, [this, cmdId, u]() {
            logMenuTrigger(QString::fromUtf8(u.text), cmdId);
            dispatchCommandSafely(cmdId);
            });
    }

    m_menuState.viewMenu->addSeparator();

    m_menuState.gridSnapMenu = m_menuState.viewMenu->addMenu(m_window->tr("Grid && Snap"));
    m_menuState.gridSnapMenu->setIcon(IconHelper::themedIcon(QStringLiteral(":/ui/common/Icons/View/snap.svg")));
    auto* showGrid = m_menuState.gridSnapMenu->addAction(m_window->tr("Show Grid"));
    showGrid->setCheckable(true);
    IconHelper::setThemedIcon(showGrid, QStringLiteral(":/ui/common/Icons/View3D/grid.svg"));
    QObject::connect(showGrid, &QAction::toggled, this, [this](bool checked) {
        logMenuTrigger(m_window->tr("Show Grid"), QStringLiteral("view.grid_visible"));
        if (m_stateCenter)
            m_stateCenter->setMetadata({ { QStringLiteral("gridVisible"), checked } });
        });
    auto* snapEnabled = m_menuState.gridSnapMenu->addAction(m_window->tr("Snap Enabled"));
    snapEnabled->setCheckable(true);
    IconHelper::setThemedIcon(snapEnabled, QStringLiteral(":/ui/common/Icons/View/snap.svg"));
    QObject::connect(snapEnabled, &QAction::toggled, this, [this](bool checked) {
        logMenuTrigger(m_window->tr("Snap Enabled"), QStringLiteral("view.snap_enabled"));
        if (m_stateCenter)
            m_stateCenter->setMetadata({ { QStringLiteral("snapEnabled"), checked } });
        });
    auto* orthoMode = m_menuState.gridSnapMenu->addAction(m_window->tr("Ortho Mode"));
    orthoMode->setCheckable(true);
    IconHelper::setThemedIcon(orthoMode, QStringLiteral(":/ui/common/Icons/View/ortho.svg"));
    QObject::connect(orthoMode, &QAction::toggled, this, [this](bool checked) {
        logMenuTrigger(m_window->tr("Ortho Mode"), QStringLiteral("view.ortho_mode"));
        if (m_stateCenter)
            m_stateCenter->setMetadata({ { QStringLiteral("orthoMode"), checked } });
        });
    auto* angleSnap = m_menuState.gridSnapMenu->addAction(m_window->tr("Angle Snap"));
    angleSnap->setCheckable(true);
    IconHelper::setThemedIcon(angleSnap, QStringLiteral(":/ui/common/Icons/View/angle_snap.svg"));
    QObject::connect(angleSnap, &QAction::toggled, this, [this](bool checked) {
        logMenuTrigger(m_window->tr("Angle Snap"), QStringLiteral("view.angle_snap"));
        if (m_stateCenter)
            m_stateCenter->setMetadata({ { QStringLiteral("angleSnap"), checked } });
        });

    m_menuState.viewMenu->addSeparator();

    m_menuState.zoomMenu = m_menuState.viewMenu->addMenu(m_window->tr("Zoom"));
    m_menuState.zoomMenu->setIcon(IconHelper::themedIcon(QStringLiteral(":/ui/common/Icons/View/zoom_in.svg")));
    auto* zoomIn = m_menuState.zoomMenu->addAction(m_window->tr("Zoom In"));
    zoomIn->setShortcut(QKeySequence::ZoomIn);
    IconHelper::setThemedIcon(zoomIn, QStringLiteral(":/ui/common/Icons/View/zoom_in.svg"));
    QObject::connect(zoomIn, &QAction::triggered, this, [this]() {
        logMenuTrigger(m_window->tr("Zoom In"), QStringLiteral("view.zoom_in"));
        if (m_viewportZoomHandler)
            m_viewportZoomHandler(QStringLiteral("zoom_in"));
        });
    auto* zoomOut = m_menuState.zoomMenu->addAction(m_window->tr("Zoom Out"));
    zoomOut->setShortcut(QKeySequence::ZoomOut);
    IconHelper::setThemedIcon(zoomOut, QStringLiteral(":/ui/common/Icons/View/zoom_out.svg"));
    QObject::connect(zoomOut, &QAction::triggered, this, [this]() {
        logMenuTrigger(m_window->tr("Zoom Out"), QStringLiteral("view.zoom_out"));
        if (m_viewportZoomHandler)
            m_viewportZoomHandler(QStringLiteral("zoom_out"));
        });
    m_menuState.zoomMenu->addSeparator();
    auto* zoomFit = m_menuState.zoomMenu->addAction(m_window->tr("Zoom to Fit"));
    zoomFit->setShortcut(QStringLiteral("Ctrl+F"));
    IconHelper::setThemedIcon(zoomFit, QStringLiteral(":/ui/common/Icons/View3D/fit.svg"));
    QObject::connect(zoomFit, &QAction::triggered, this, [this]() {
        logMenuTrigger(m_window->tr("Zoom to Fit"), QStringLiteral("view.zoom_fit"));
        if (m_viewportZoomHandler)
            m_viewportZoomHandler(QStringLiteral("zoom_fit"));
        });
    auto* zoomSel = m_menuState.zoomMenu->addAction(m_window->tr("Zoom to Selection"));
    zoomSel->setShortcut(QStringLiteral("Ctrl+Shift+F"));
    IconHelper::setThemedIcon(zoomSel, QStringLiteral(":/ui/common/Icons/View/zoom_selection.svg"));
    QObject::connect(zoomSel, &QAction::triggered, this, [this]() {
        logMenuTrigger(m_window->tr("Zoom to Selection"), QStringLiteral("view.zoom_selection"));
        if (m_viewportZoomHandler)
            m_viewportZoomHandler(QStringLiteral("zoom_selection"));
        });
    m_menuState.zoomMenu->addSeparator();
    auto* resetView = m_menuState.zoomMenu->addAction(m_window->tr("Reset View"));
    resetView->setShortcut(QStringLiteral("Ctrl+0"));
    IconHelper::setThemedIcon(resetView, QStringLiteral(":/ui/common/Icons/View3D/reset.svg"));
    QObject::connect(resetView, &QAction::triggered, this, [this]() {
        logMenuTrigger(m_window->tr("Reset View"), QStringLiteral("view.reset"));
        if (m_viewportZoomHandler)
            m_viewportZoomHandler(QStringLiteral("reset"));
        });
}

void WorkbenchMenuManager::refreshDrawMenuForWorkbench(const QString& workbenchId)
{
    if (!m_menuState.drawMenu)
        return;

    // 旧路径下 Draw 菜单仅服务 2D（3D 由 MenuManager3D 自理，见架构文档），
    // 旧有的 CommandCatalog3D "model." 前缀过滤分支属死代码，已删除。
    if (workbenchId.compare(QStringLiteral("3D"), Qt::CaseInsensitive) == 0)
    {
        SY_DEBUGF("[WorkbenchMenuManager] refreshDrawMenuForWorkbench skip 3D (managed by MenuManager3D)");
        return;
    }

    clearMenuActions(m_menuState.drawMenu);

    bool firstSeparatorAdded = false;
    for (const CommandEntry2D& cmdEntry : CommandCatalog::commands())
    {
        if (!cmdEntry.toolName)
            continue;
        if (!hasSurface(cmdEntry.surfaces, CommandSurface2D::Menu))
            continue;
        if (!firstSeparatorAdded && cmdEntry.menuId != UI::MenuActionId::Draw_Select)
        {
            m_menuState.drawMenu->addSeparator();
            firstSeparatorAdded = true;
        }
        const QString iconRes = cmdEntry.iconResource ? QString::fromUtf8(cmdEntry.iconResource) : QString();
        // 绘图工具命令使用 toolName 作为 commandId，由 Workbench2D::dispatchCommand
        // 兜底按 operationForToolName 解析，与左侧工具栏/右键菜单为同一条分发路径。
        addMenuAction(m_menuState.drawMenu, m_window->tr(cmdEntry.text),
            QString::fromUtf8(cmdEntry.toolName), iconRes);
    }
}

void WorkbenchMenuManager::refreshEditMenuForWorkbench(const QString& workbenchId)
{
    if (!m_menuState.editMenu)
        return;

    // 旧路径下 Edit 菜单仅服务 2D（3D 由 MenuManager3D 自理，见架构文档）。
    if (workbenchId.compare(QStringLiteral("3D"), Qt::CaseInsensitive) == 0)
    {
        SY_DEBUGF("[WorkbenchMenuManager] refreshEditMenuForWorkbench skip 3D (managed by MenuManager3D)");
        return;
    }

    // clearMenuActions 会连带删除 Modify/Rotate/Mirror/Align/Path Operations 等子菜单对象，
    // 避免只删 menuAction 造成孤儿子菜单累积（原 qDeleteAll 的泄漏点）。
    clearMenuActions(m_menuState.editMenu);

    auto* undoAction = m_menuState.editMenu->addAction(m_window->tr("Undo"));
    undoAction->setShortcut(QKeySequence::Undo);
    auto* redoAction = m_menuState.editMenu->addAction(m_window->tr("Redo"));
    redoAction->setShortcut(QKeySequence::Redo);
    applyMenuIcon(undoAction, QStringLiteral("edit.undo"));
    applyMenuIcon(redoAction, QStringLiteral("edit.redo"));
    connect(undoAction, &QAction::triggered, this, [this]() {
        logMenuTrigger(m_window->tr("Undo"), QStringLiteral("edit.undo"));
        dispatchCommandSafely(QStringLiteral("edit.undo"));
        });
    connect(redoAction, &QAction::triggered, this, [this]() {
        logMenuTrigger(m_window->tr("Redo"), QStringLiteral("edit.redo"));
        dispatchCommandSafely(QStringLiteral("edit.redo"));
        });
    m_menuState.editMenu->addSeparator();

    addMenuAction(m_menuState.editMenu, m_window->tr("Select All"), QStringLiteral("edit.select_all"));
    addMenuAction(m_menuState.editMenu, m_window->tr("Invert Selection"), QStringLiteral("edit.invert_selection"));
    addMenuAction(m_menuState.editMenu, m_window->tr("Deselect"), QStringLiteral("edit.deselect"));
    m_menuState.editMenu->addSeparator();

    addMenuAction(m_menuState.editMenu, m_window->tr("Cut"), QStringLiteral("edit.cut"));
    addMenuAction(m_menuState.editMenu, m_window->tr("Copy"), QStringLiteral("edit.copy"));
    addMenuAction(m_menuState.editMenu, m_window->tr("Paste"), QStringLiteral("edit.paste"));
    m_menuState.editMenu->addSeparator();

    addMenuAction(m_menuState.editMenu, m_window->tr("Delete"), QStringLiteral("edit.delete"));
    m_menuState.editMenu->addSeparator();

    // Modify（几何变换）菜单收敛到 Edit 下，作为子菜单（2D：交互式修改工具；3D：配置模式走 JSON edit.transform）
    m_menuState.modifyMenu = m_menuState.editMenu->addMenu(m_window->tr("Modify"));
    m_menuState.modifyMenu->setIcon(IconHelper::themedIcon(QStringLiteral(":/ui/common/Icons/Actions/move.svg")));
    refreshModifyMenuForWorkbench(workbenchId);
    m_menuState.editMenu->addSeparator();

    m_menuState.rotateMenu = m_menuState.editMenu->addMenu(m_window->tr("Rotate"));
    m_menuState.rotateMenu->setIcon(IconHelper::themedIcon(QStringLiteral(":/ui/common/Icons/Actions/rotate.svg")));
    addMenuAction(m_menuState.rotateMenu, m_window->tr("Rotate 90 CW"), QStringLiteral("edit.rotate_90cw"),
        QStringLiteral(":/ui/common/Icons/Actions/rotate.svg"));
    addMenuAction(m_menuState.rotateMenu, m_window->tr("Rotate 90 CCW"), QStringLiteral("edit.rotate_90ccw"),
        QStringLiteral(":/ui/common/Icons/Actions/rotate.svg"));
    addMenuAction(m_menuState.rotateMenu, m_window->tr("Rotate 180"), QStringLiteral("edit.rotate_180"),
        QStringLiteral(":/ui/common/Icons/Actions/rotate.svg"));

    m_menuState.mirrorMenu = m_menuState.editMenu->addMenu(m_window->tr("Mirror"));
    m_menuState.mirrorMenu->setIcon(IconHelper::themedIcon(QStringLiteral(":/ui/common/Icons/Actions/mirror_h.svg")));
    addMenuAction(m_menuState.mirrorMenu, m_window->tr("Mirror Horizontal"), QStringLiteral("edit.mirror_horizontal"));
    addMenuAction(m_menuState.mirrorMenu, m_window->tr("Mirror Vertical"), QStringLiteral("edit.mirror_vertical"));

    m_menuState.alignMenu = m_menuState.editMenu->addMenu(m_window->tr("Align"));
    m_menuState.alignMenu->setIcon(IconHelper::themedIcon(QStringLiteral(":/ui/common/Icons/Actions/align_left.svg")));
    addMenuAction(m_menuState.alignMenu, m_window->tr("Align Left"), QStringLiteral("edit.align_left"));
    addMenuAction(m_menuState.alignMenu, m_window->tr("Align Right"), QStringLiteral("edit.align_right"));
    addMenuAction(m_menuState.alignMenu, m_window->tr("Align Center H"), QStringLiteral("edit.align_center_h"));
    addMenuAction(m_menuState.alignMenu, m_window->tr("Align Top"), QStringLiteral("edit.align_top"));
    addMenuAction(m_menuState.alignMenu, m_window->tr("Align Bottom"), QStringLiteral("edit.align_bottom"));
    addMenuAction(m_menuState.alignMenu, m_window->tr("Align Center V"), QStringLiteral("edit.align_center_v"));
    m_menuState.editMenu->addSeparator();

    addMenuAction(m_menuState.editMenu, m_window->tr("Group"), QStringLiteral("edit.group"),
        QString(), MenuActionOption_Checkable);
    addMenuAction(m_menuState.editMenu, m_window->tr("Ungroup"), QStringLiteral("edit.ungroup"));

    m_menuState.editMenu->addSeparator();

    m_menuState.pathOpsMenu = m_menuState.editMenu->addMenu(m_window->tr("Path Operations"));
    m_menuState.pathOpsMenu->setIcon(IconHelper::themedIcon(QStringLiteral(":/ui/common/Icons/Actions/offset.svg")));
    addMenuAction(m_menuState.pathOpsMenu, m_window->tr("Offset"), QStringLiteral("edit.offset"));
    addMenuAction(m_menuState.pathOpsMenu, m_window->tr("Boolean Union"), QStringLiteral("edit.boolean_union"));
    addMenuAction(m_menuState.pathOpsMenu, m_window->tr("Boolean Intersection"), QStringLiteral("edit.boolean_intersection"));
    addMenuAction(m_menuState.pathOpsMenu, m_window->tr("Boolean Difference"), QStringLiteral("edit.boolean_difference"));
    addMenuAction(m_menuState.pathOpsMenu, m_window->tr("Boolean Xor"), QStringLiteral("edit.boolean_xor"));
}

void WorkbenchMenuManager::refreshModifyMenuForWorkbench(const QString& workbenchId)
{
    if (!m_menuState.modifyMenu)
        return;

    // 旧路径下 Modify 仅服务 2D（3D 变换工具在配置模式走 JSON edit.transform，旧 3D 分支删除）。
    if (workbenchId.compare(QStringLiteral("3D"), Qt::CaseInsensitive) == 0)
    {
        SY_DEBUGF("[WorkbenchMenuManager] refreshModifyMenuForWorkbench skip 3D (managed by MenuManager3D)");
        return;
    }

    clearMenuActions(m_menuState.modifyMenu);

    addMenuAction(m_menuState.modifyMenu, m_window->tr("Move"), QStringLiteral("2d.move"));
    addMenuAction(m_menuState.modifyMenu, m_window->tr("Rotate"), QStringLiteral("2d.rotate"),
        QStringLiteral(":/ui/common/Icons/Actions/rotate.svg"));
    addMenuAction(m_menuState.modifyMenu, m_window->tr("Scale"), QStringLiteral("2d.scale"),
        QStringLiteral(":/ui/common/Icons/View3D/scale.svg"));
    addMenuAction(m_menuState.modifyMenu, m_window->tr("Copy"), QStringLiteral("2d.copy"));
    addMenuAction(m_menuState.modifyMenu, m_window->tr("Mirror"), QStringLiteral("2d.mirror"),
        QStringLiteral(":/ui/common/Icons/Actions/mirror_h.svg"));
    m_menuState.modifyMenu->addSeparator();
    addMenuAction(m_menuState.modifyMenu, m_window->tr("Trim"), QStringLiteral("2d.trim"),
        QStringLiteral(":/ui/common/Icons/Actions/trim.svg"));
    addMenuAction(m_menuState.modifyMenu, m_window->tr("Extend"), QStringLiteral("2d.extend"),
        QStringLiteral(":/ui/common/Icons/Actions/extend.svg"));
    addMenuAction(m_menuState.modifyMenu, m_window->tr("Fillet"), QStringLiteral("2d.fillet"),
        QStringLiteral(":/ui/common/Icons/Actions/fillet.svg"));
    addMenuAction(m_menuState.modifyMenu, m_window->tr("Chamfer"), QStringLiteral("2d.chamfer"),
        QStringLiteral(":/ui/common/Icons/Actions/chamfer.svg"));
    m_menuState.modifyMenu->addSeparator();
    addMenuAction(m_menuState.modifyMenu, m_window->tr("Delete"), QStringLiteral("2d.delete"));
}

void WorkbenchMenuManager::refreshAlgorithmMenuForWorkbench(const QString& workbenchId)
{
    if (!m_menuState.algorithmMenu)
        return;

    // 旧路径下 Algorithm 仅服务 2D（3D algo./process. 命令在配置模式走 JSON tools.model，旧 3D 分支删除）。
    if (workbenchId.compare(QStringLiteral("3D"), Qt::CaseInsensitive) == 0)
    {
        SY_DEBUGF("[WorkbenchMenuManager] refreshAlgorithmMenuForWorkbench skip 3D (managed by MenuManager3D)");
        return;
    }

    clearMenuActions(m_menuState.algorithmMenu);

    addMenuAction(m_menuState.algorithmMenu, m_window->tr("Fill..."), QStringLiteral("algo.fill"),
        QStringLiteral(":/ui/common/Icons/Actions/algo_fill.svg"));
    addMenuAction(m_menuState.algorithmMenu, m_window->tr("Nesting..."), QStringLiteral("algo.nesting"),
        QStringLiteral(":/ui/common/Icons/Actions/algo_nesting.svg"));
    addMenuAction(m_menuState.algorithmMenu, m_window->tr("Array..."), QStringLiteral("algo.array"),
        QStringLiteral(":/ui/common/Icons/Actions/algo_array.svg"));
    addMenuAction(m_menuState.algorithmMenu, m_window->tr("Bitmap Relief Engraving..."), QStringLiteral("algo.relief_engraving"),
        QStringLiteral(":/ui/common/Icons/Actions/algo_relief.svg"));
}

void WorkbenchMenuManager::buildHelpMenu()
{
    if (!m_menuState.helpMenu)
        return;

    auto* docsAction = m_menuState.helpMenu->addAction(m_window->tr("Documentation"));
    setCmdId(docsAction, QStringLiteral("help.docs"));
    IconHelper::setThemedIcon(docsAction, QStringLiteral(":/ui/common/Icons/Help/docs.svg"));
    connect(docsAction, &QAction::triggered, this, [this]() {
        logMenuTrigger(m_window->tr("Documentation"), QStringLiteral("help.docs"));
        dispatchCommandSafely(QStringLiteral("help.docs"));
        });

    auto* shortcutAction = m_menuState.helpMenu->addAction(m_window->tr("Keyboard Shortcuts"));
    shortcutAction->setShortcut(Qt::Key_F1);
    setCmdId(shortcutAction, QStringLiteral("help.shortcuts"));
    IconHelper::setThemedIcon(shortcutAction, QStringLiteral(":/ui/common/Icons/Help/shortcuts.svg"));
    connect(shortcutAction, &QAction::triggered, this, [this]() {
        logMenuTrigger(m_window->tr("Keyboard Shortcuts"), QStringLiteral("help.shortcuts"));
        dispatchCommandSafely(QStringLiteral("help.shortcuts"));
        });
    m_menuState.helpMenu->addSeparator();

    auto* settingsAction = m_menuState.helpMenu->addAction(m_window->tr("Settings..."));
    setCmdId(settingsAction, QStringLiteral("help.settings"));
    IconHelper::setThemedIcon(settingsAction, QStringLiteral(":/ui/common/Icons/Help/settings.svg"));
    connect(settingsAction, &QAction::triggered, this, [this]() {
        logMenuTrigger(m_window->tr("Settings..."), QStringLiteral("help.settings"));
        dispatchCommandSafely(QStringLiteral("help.settings"));
        });
    m_menuState.helpMenu->addSeparator();

    m_menuState.languageMenu = m_menuState.helpMenu->addMenu(m_window->tr("Language"));
    m_menuState.languageMenu->setIcon(IconHelper::themedIcon(QStringLiteral(":/ui/common/Icons/Help/language.svg")));
    auto* langGroup = new QActionGroup(m_window);
    langGroup->setExclusive(true);
    const auto langs = LM->supportedLanguages();
    const auto currentLang = LM->currentLanguage();
    for (const auto& lang : langs)
    {
        auto* act = m_menuState.languageMenu->addAction(LM->languageName(lang));
        act->setCheckable(true);
        act->setChecked(lang == currentLang);
        langGroup->addAction(act);
        connect(act, &QAction::triggered, this, [lang]() {
            logMenuTrigger(LM->languageName(lang), QStringLiteral("help.language"));
            LM->setLanguage(lang);
            });
    }
    if (m_languageChangedConn)
        disconnect(m_languageChangedConn);
    m_languageChangedConn = connect(LM, &LanguageManager::languageChanged, this, [this](AppLanguage newLang) {
        if (!m_menuState.languageMenu)
            return;
        for (QAction* act : m_menuState.languageMenu->actions())
        {
            if (!act->isCheckable())
                continue;
            const auto langs = LM->supportedLanguages();
            int idx = m_menuState.languageMenu->actions().indexOf(act);
            if (idx >= 0 && idx < langs.size())
            {
                QSignalBlocker blocker(act);
                act->setChecked(langs[idx] == newLang);
            }
        }
        });

    m_menuState.helpThemeMenu = m_menuState.helpMenu->addMenu(m_window->tr("Theme"));
    m_menuState.helpThemeMenu->setIcon(IconHelper::themedIcon(QStringLiteral(":/ui/common/Icons/Help/theme.svg")));
    auto* themeGroup = new QActionGroup(m_window);
    themeGroup->setExclusive(true);
    const auto themes = TM->supportedThemes();
    const auto currentTheme = TM->currentTheme();
    for (const auto& theme : themes)
    {
        auto* act = m_menuState.helpThemeMenu->addAction(TM->themeName(theme));
        act->setCheckable(true);
        act->setChecked(theme == currentTheme);
        themeGroup->addAction(act);
        connect(act, &QAction::triggered, this, [theme]() {
            logMenuTrigger(TM->themeName(theme), QStringLiteral("help.theme"));
            TM->setTheme(theme);
            });
    }
    if (m_themeChangedConn)
        disconnect(m_themeChangedConn);
    m_themeChangedConn = connect(TM, &ThemeManager::themeChanged, this, [this](AppTheme newTheme) {
        if (!m_menuState.helpThemeMenu)
            return;
        for (QAction* act : m_menuState.helpThemeMenu->actions())
        {
            if (!act->isCheckable())
                continue;
            const auto themes = TM->supportedThemes();
            int idx = m_menuState.helpThemeMenu->actions().indexOf(act);
            if (idx >= 0 && idx < themes.size())
            {
                QSignalBlocker blocker(act);
                act->setChecked(themes[idx] == newTheme);
            }
        }
        });

    m_menuState.helpMenu->addSeparator();

    auto* aboutAction = m_menuState.helpMenu->addAction(m_window->tr("About"));
    setCmdId(aboutAction, QStringLiteral("help.about"));
    IconHelper::setThemedIcon(aboutAction, QStringLiteral(":/ui/common/Icons/Help/about.svg"));
    connect(aboutAction, &QAction::triggered, this, [this]() {
        logMenuTrigger(m_window->tr("About"), QStringLiteral("help.about"));
        dispatchCommandSafely(QStringLiteral("help.about"));
        });
}

void WorkbenchMenuManager::initializeThemeMenuSkeleton()
{
    buildThemeMenu();
}

void WorkbenchMenuManager::buildThemeMenu()
{
    // 主题菜单保留为独立逻辑，后续也可并入菜单配置 schema。
    // 配置驱动模式下 Themes 菜单由 JSON（tools.theme）生成，这里为空兜底。
    if (!m_menuState.toolsMenu)
        return;
    m_menuState.themeMenu = m_menuState.toolsMenu->addMenu(m_window->tr("Theme"));
    m_menuState.themeMenu->setIcon(IconHelper::themedIcon(QStringLiteral(":/ui/common/Icons/Help/theme.svg")));

    addMenuAction(m_menuState.themeMenu, m_window->tr("System"), QStringLiteral("system"),
        QString(), MenuActionOption_Theme | MenuActionOption_Checkable);
    addMenuAction(m_menuState.themeMenu, m_window->tr("Light"), QStringLiteral("light"),
        QString(), MenuActionOption_Theme | MenuActionOption_Checkable);
    addMenuAction(m_menuState.themeMenu, m_window->tr("Dark"), QStringLiteral("dark"),
        QString(), MenuActionOption_Theme | MenuActionOption_Checkable);
    addMenuAction(m_menuState.themeMenu, m_window->tr("Blue"), QStringLiteral("blue"),
        QString(), MenuActionOption_Theme | MenuActionOption_Checkable);
}

void WorkbenchMenuManager::bindMenuCommands()
{
    SY_INFO("[WorkbenchMenuManager] bindMenuCommands: menu actions are now directly connected to OperationBus");
}

void WorkbenchMenuManager::clearGlobalShortcuts()
{
    if (m_undoAction)
    {
        m_window->removeAction(m_undoAction);
        delete m_undoAction;
        m_undoAction = nullptr;
    }
    if (m_redoAction)
    {
        m_window->removeAction(m_redoAction);
        delete m_redoAction;
        m_redoAction = nullptr;
    }
}

void WorkbenchMenuManager::bindShortcuts()
{
    // 先清理旧动作，防止重复叠加
    clearGlobalShortcuts();

    m_undoAction = new QAction(m_window->tr("Undo"), m_window);
    m_undoAction->setShortcut(QKeySequence::Undo);
    connect(m_undoAction, &QAction::triggered, this, [this]() {
        dispatchCommandSafely(QStringLiteral("edit.undo"));
        });
    m_window->addAction(m_undoAction);

    m_redoAction = new QAction(m_window->tr("Redo"), m_window);
    m_redoAction->setShortcut(QKeySequence::Redo);
    connect(m_redoAction, &QAction::triggered, this, [this]() {
        dispatchCommandSafely(QStringLiteral("edit.redo"));
        });
    m_window->addAction(m_redoAction);
}

void WorkbenchMenuManager::refreshWorkbenchMenuChecks(const QString& workbenchId)
{
    const bool is2D = workbenchId.compare(QStringLiteral("2D"), Qt::CaseInsensitive) == 0;
    const bool is3D = workbenchId.compare(QStringLiteral("3D"), Qt::CaseInsensitive) == 0;

    if (is2D)
    {
        if (m_menuState.workbench3DAction)
        {
            m_menuState.workbench3DAction->deleteLater();
            m_menuState.workbench3DAction = nullptr;
        }
        if (m_menuState.workbench2DAction)
        {
            m_menuState.workbench2DAction->deleteLater();
            m_menuState.workbench2DAction = nullptr;
        }
        if (!m_menuState.workbench3DAction && m_menuState.viewMenu)
        {
            m_menuState.workbench3DAction = new QAction(m_window->tr("Switch to 3D"), this);
            m_menuState.workbench3DAction->setCheckable(true);
            m_menuState.viewMenu->insertAction(m_menuState.viewMenu->actions().first(), m_menuState.workbench3DAction);
            QObject::connect(m_menuState.workbench3DAction, &QAction::triggered, this, [this]() {
                m_window->triggerWorkbench(QStringLiteral("3D"));
                });
        }
    }
    else if (is3D)
    {
        if (m_menuState.workbench2DAction)
        {
            m_menuState.workbench2DAction->deleteLater();
            m_menuState.workbench2DAction = nullptr;
        }
        if (m_menuState.workbench3DAction)
        {
            m_menuState.workbench3DAction->deleteLater();
            m_menuState.workbench3DAction = nullptr;
        }
        if (!m_menuState.workbench2DAction && m_menuState.viewMenu)
        {
            m_menuState.workbench2DAction = new QAction(m_window->tr("Switch to 2D"), this);
            m_menuState.workbench2DAction->setCheckable(true);
            m_menuState.viewMenu->insertAction(m_menuState.viewMenu->actions().first(), m_menuState.workbench2DAction);
            QObject::connect(m_menuState.workbench2DAction, &QAction::triggered, this, [this]() {
                m_window->triggerWorkbench(QStringLiteral("2D"));
                });
        }
    }
}

void WorkbenchMenuManager::refreshThemeMenuChecks(const QString& themeId)
{
    if (!m_menuState.themeMenu)
        return;

    for (QAction* action : m_menuState.themeMenu->actions())
    {
        if (!action->isCheckable())
            continue;
        action->setChecked(action->text().compare(themeId, Qt::CaseInsensitive) == 0);
    }
}

bool WorkbenchMenuManager::isMenuGroupAllowedForWorkbench(const QString& commandId, const QString& workbenchKind)
{
    if (commandId.isEmpty())
        return true;
    if (workbenchKind.compare(QStringLiteral("3D"), Qt::CaseInsensitive) != 0)
        return true;

    return commandId.startsWith(QStringLiteral("view."), Qt::CaseInsensitive)
        || commandId.startsWith(QStringLiteral("edit."), Qt::CaseInsensitive)
        || commandId.startsWith(QStringLiteral("model."), Qt::CaseInsensitive)
        || commandId.startsWith(QStringLiteral("process."), Qt::CaseInsensitive)
        || commandId.startsWith(QStringLiteral("algo."), Qt::CaseInsensitive)
        || commandId.startsWith(QStringLiteral("file."), Qt::CaseInsensitive)
        || commandId.startsWith(QStringLiteral("help."), Qt::CaseInsensitive)
        || commandId.startsWith(QStringLiteral("theme."), Qt::CaseInsensitive);
}

std::vector<MenuDef> WorkbenchMenuManager::filterMenusForWorkbench(const std::vector<MenuDef>& menus,
    const QString& workbenchId,
    const std::function<bool(const QString&)>& commandAvailable,
    const QString& workbenchKind)
{
    const auto visibilityAllowed = [&](const QString& scope) {
        if (scope.isEmpty())
            return true;
        if (workbenchKind.compare(QStringLiteral("3D"), Qt::CaseInsensitive) == 0)
            return scope.compare(QStringLiteral("3D"), Qt::CaseInsensitive) == 0
            || scope.compare(QStringLiteral("shared"), Qt::CaseInsensitive) == 0;
        return scope.compare(QStringLiteral("2D"), Qt::CaseInsensitive) == 0
            || scope.compare(QStringLiteral("shared"), Qt::CaseInsensitive) == 0;
        };

    const auto commandVisibleByMenuGroup = [&](const QString& commandId) {
        return isMenuGroupAllowedForWorkbench(commandId, workbenchKind);
        };

    std::function<bool(const MenuActionDef&, MenuActionDef&)> filterAction =
        [&](const MenuActionDef& action, MenuActionDef& outAction) -> bool
        {
            if (!action.visible || !commandEnabledForWorkbench(action.workbenches, workbenchId))
            {
                logFilteredCommand(action.commandId, workbenchId, "workbench-filter");
                return false;
            }
            if (!visibilityAllowed(action.visibilityScope))
            {
                logFilteredCommand(action.commandId, workbenchId, "visibility-scope");
                return false;
            }
            if (!commandVisibleByMenuGroup(action.commandId))
            {
                logFilteredCommand(action.commandId, workbenchId, "group-filter");
                return false;
            }
            if (!commandAvailable(action.commandId))
            {
                logFilteredCommand(action.commandId, workbenchId, "command-unavailable");
                return false;
            }
            outAction = action;
            return true;
        };

    std::function<bool(const SubMenuDef&, SubMenuDef&)> filterSubMenu =
        [&](const SubMenuDef& sub, SubMenuDef& outSub) -> bool
        {
            if (!sub.visible || !commandEnabledForWorkbench(sub.workbenches, workbenchId))
            {
                logFilteredCommand(sub.id, workbenchId, "submenu-workbench-filter");
                return false;
            }
            if (!visibilityAllowed(sub.visibilityScope))
            {
                logFilteredCommand(sub.id, workbenchId, "submenu-visibility-scope");
                return false;
            }
            outSub = sub;
            outSub.items.clear();
            for (const auto& subItem : sub.items)
            {
                if (std::holds_alternative<MenuActionDef>(subItem))
                {
                    MenuActionDef filteredAction;
                    if (filterAction(std::get<MenuActionDef>(subItem), filteredAction))
                        outSub.items.push_back(filteredAction);
                }
                else if (std::holds_alternative<SubMenuDef>(subItem))
                {
                    SubMenuDef filteredSub;
                    if (filterSubMenu(std::get<SubMenuDef>(subItem), filteredSub))
                        outSub.items.push_back(filteredSub);
                }
                else if (std::holds_alternative<MenuItemType>(subItem))
                {
                    outSub.items.push_back(subItem);
                }
            }
            return !outSub.items.empty();
        };

    std::vector<MenuDef> filteredMenus;
    filteredMenus.reserve(menus.size());
    for (const auto& menu : menus)
    {
        if (!menu.visible || !commandEnabledForWorkbench(menu.workbenches, workbenchId))
            continue;
        if (!visibilityAllowed(menu.visibilityScope))
        {
            logFilteredCommand(menu.id, workbenchId, "menu-visibility-scope");
            continue;
        }
        MenuDef menuCopy = menu;
        menuCopy.items.clear();
        for (const auto& item : menu.items)
        {
            if (std::holds_alternative<MenuActionDef>(item))
            {
                MenuActionDef filteredAction;
                if (filterAction(std::get<MenuActionDef>(item), filteredAction))
                    menuCopy.items.push_back(filteredAction);
            }
            else if (std::holds_alternative<SubMenuDef>(item))
            {
                SubMenuDef filteredSub;
                if (filterSubMenu(std::get<SubMenuDef>(item), filteredSub))
                    menuCopy.items.push_back(filteredSub);
            }
            else if (std::holds_alternative<MenuItemType>(item))
            {
                menuCopy.items.push_back(item);
            }
        }
        if (!menuCopy.items.empty())
            filteredMenus.push_back(menuCopy);
    }
    return filteredMenus;
}

#ifdef SANYI_ENABLE_CONFIG_DRIVEN_UI
void WorkbenchMenuManager::bindConfiguredMenuState()
{
    if (!m_stateCenter)
        return;
    // 配置驱动模式下每次重建都会调用本站，避免重复连接导致刷新叠加
    if (m_configStateBound)
        return;
    m_configStateBound = true;

    QObject::connect(m_stateCenter, &UiStateCenter::stateChanged,
        this, [this]() { refreshConfiguredMenuState(); });
    QObject::connect(m_stateCenter, &UiStateCenter::metadataChanged,
        this, [this]() { refreshConfiguredMenuState(); });
    QObject::connect(m_stateCenter, &UiStateCenter::currentWorkbenchChanged,
        this, [this](const QString&) { refreshConfiguredMenuState(); });
    QObject::connect(m_stateCenter, &UiStateCenter::currentThemeChanged,
        this, [this](const QString&) { refreshConfiguredMenuState(); });
    refreshConfiguredMenuState();
}

void WorkbenchMenuManager::refreshConfiguredMenuState()
{
    if (!m_stateCenter)
        return;

    const auto state = m_stateCenter->snapshot();
    const QString wbId = state.currentWorkbenchId;
    const auto applyChecked = [](QAction* action, bool checked) {
        if (!action)
            return;
        QSignalBlocker blocker(action);
        action->setChecked(checked);
        };

    auto refreshMenu = [&](QMenu* menu) {
        if (!menu)
            return;
        for (QAction* action : menu->actions())
        {
            if (!action)
                continue;
            const QString cmdId = action->property("commandId").toString();
            if (cmdId.isEmpty())
                continue;

            if (cmdId == QStringLiteral("view.grid") || cmdId == QStringLiteral("view.grid_visible"))
                applyChecked(action, state.metadata.value(QStringLiteral("gridVisible")).toBool());
            else if (cmdId == QStringLiteral("view.snap") || cmdId == QStringLiteral("view.snap_enabled"))
                applyChecked(action, state.metadata.value(QStringLiteral("snapEnabled")).toBool());
            else if (cmdId == QStringLiteral("view.ortho") || cmdId == QStringLiteral("view.ortho_mode"))
                applyChecked(action, state.metadata.value(QStringLiteral("orthoMode")).toBool());
            else if (cmdId == QStringLiteral("view.angle_snap"))
                applyChecked(action, state.metadata.value(QStringLiteral("angleSnap")).toBool());
            else if (cmdId == QStringLiteral("view.wireframe"))
                applyChecked(action, state.metadata.value(QStringLiteral("wireframe")).toBool());
            else if (cmdId == QStringLiteral("view.bbox"))
                applyChecked(action, state.metadata.value(QStringLiteral("bbox")).toBool());
            else if (cmdId == QStringLiteral("view.floor"))
                applyChecked(action, state.metadata.value(QStringLiteral("floor")).toBool());
            else if (cmdId == QStringLiteral("view.unit_mm"))
                applyChecked(action, wbId.compare(QStringLiteral("2D"), Qt::CaseInsensitive) == 0);
            else if (cmdId == QStringLiteral("view.unit_cm"))
                applyChecked(action, false);
            else if (cmdId == QStringLiteral("view.unit_inch"))
                applyChecked(action, false);
        }
    };

    refreshMenu(m_menuState.viewMenu);
    refreshMenu(m_menuState.fileMenu);
    refreshMenu(m_menuState.editMenu);
    refreshMenu(m_menuState.algorithmMenu);
    refreshMenu(m_menuState.helpMenu);
}
#endif

void WorkbenchMenuManager::syncGridSnapMenuState()
{
    if (!m_stateCenter || !m_menuState.gridSnapMenu)
        return;

    for (QAction* act : m_menuState.gridSnapMenu->actions())
    {
        if (!act->isCheckable() || act->isSeparator())
            continue;

        const QString text = act->text();
        const bool checked = act->isChecked();
        if (text.contains(m_window->tr("Grid"), Qt::CaseInsensitive))
            m_stateCenter->setMetadata({ { QStringLiteral("gridVisible"), checked } });
        else if (text.contains(m_window->tr("Snap"), Qt::CaseInsensitive))
            m_stateCenter->setMetadata({ { QStringLiteral("snapEnabled"), checked } });
        else if (text.contains(m_window->tr("Ortho"), Qt::CaseInsensitive))
            m_stateCenter->setMetadata({ { QStringLiteral("orthoMode"), checked } });
        else if (text.contains(m_window->tr("Angle"), Qt::CaseInsensitive))
            m_stateCenter->setMetadata({ { QStringLiteral("angleSnap"), checked } });
    }
}

void WorkbenchMenuManager::refreshGridSnapMenuChecks()
{
    if (!m_stateCenter || !m_menuState.gridSnapMenu)
        return;

    const auto& metadata = m_stateCenter->snapshot().metadata;

    for (QAction* act : m_menuState.gridSnapMenu->actions())
    {
        if (!act->isCheckable() || act->isSeparator())
            continue;

        const QString text = act->text();
        bool checked = act->isChecked();

        if (text.contains(m_window->tr("Grid"), Qt::CaseInsensitive)
            && metadata.contains(QStringLiteral("gridVisible")))
            checked = metadata.value(QStringLiteral("gridVisible")).toBool();
        else if (text.contains(m_window->tr("Snap"), Qt::CaseInsensitive)
            && metadata.contains(QStringLiteral("snapEnabled")))
            checked = metadata.value(QStringLiteral("snapEnabled")).toBool();
        else if (text.contains(m_window->tr("Ortho"), Qt::CaseInsensitive)
            && metadata.contains(QStringLiteral("orthoMode")))
            checked = metadata.value(QStringLiteral("orthoMode")).toBool();
        else if (text.contains(m_window->tr("Angle"), Qt::CaseInsensitive)
            && metadata.contains(QStringLiteral("angleSnap")))
            checked = metadata.value(QStringLiteral("angleSnap")).toBool();

        QSignalBlocker blocker(act);
        act->setChecked(checked);
    }
}