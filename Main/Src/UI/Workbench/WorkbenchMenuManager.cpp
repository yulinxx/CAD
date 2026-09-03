#include "WorkbenchMenuManager.h"
#include "WorkbenchWindow.h"

#include "Log/SyLogger.h"
#include "Manager/UnitManager/UnitManager.h"
#include "Manager/UnitManager/UnitSelectionMenu.h"
#include "Platform/MacMenuCleanup.h"
#include "UI2D/Operation/CommandActionHub.h"
#include "UI2D/Operation/CommandCatalog.h"
#if BUILD_UI3D
    #include "UI3D/Operation/CommandActionHub3D.h"
#endif

#include "UI2D/Operation/OperationBus.h"
#include "UI2D/Operation/OperationId.h"

#include "UiStateCenter.h"
#include "UiWorkbench.h"
#include "UIServices.h"
#include "UiFrameworkServices.h"
#include "Composition/ApplicationCompositionRoot.h"
#include "UI/Settings/SettingsService.h"
#include "UI/Services/IRecentFileService.h"
#include "UI/ClientConfig/UiLayoutBuilder.h"
#include "UI/LanguageManager.h"
#include "UI/ThemeManager.h"
#include "UI/IconHelper.h"
#include "UI2D/Dlg/LayerManagerDialog.h"
#include "UI2D/ToolBar/RightToolBar.h"
#include "UI2D/ToolBar/TopToolBar.h"
#include "UI2D/StatusBar/StatusBar.h"
#include "UI/StatusBar/StatusBar3D.h"
#include "UI/Widgets/UiSceneTreePanel.h"
#include "UI/Widgets/UiPropertiesPanel.h"
#include "Render3D/RenderWidget3D.h"
#include "ClientConfig/UiBuiltinPanels.h"
#include "ClientConfig/UiClientConfigBase.h"
#include "ClientConfig/UiClientContext.h"
#include "ClientConfig/UiConfigurationManager.h"
#include "ClientConfig/UiContextMenuService.h"
#include "ClientConfig/UiFeatureGate.h"
#include "ClientConfig/UiLayoutBuilder.h"
#include "ClientConfig/UiPanelRegistry.h"
#include "ClientConfig/UiShortcutRegistry.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QFileInfo>
#include <QKeySequence>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QSet>
#include <QSignalBlocker>
#include <QStringList>
#include <QTimer>
#include <QToolBar>
#include <QVariantMap>
#include <algorithm>
#include <functional>

namespace
{
    QString currentWorkbenchId(const UiStateCenter* stateCenter)
    {
        return stateCenter ? stateCenter->currentWorkbenchId() : QStringLiteral("2D");
    }

    bool commandEnabledForWorkbench(const QStringList& workbenches, const QString& workbenchId)

    {
        if (workbenches.isEmpty())
        {
            return true;
        }
        for (const auto& wb : workbenches)
        {
            if (wb.compare(workbenchId, Qt::CaseInsensitive) == 0)
            {
                return true;
            }
        }
        return false;
    }

}  // namespace

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
        return commandId == QLatin1String("view.switch_to_2d") || commandId == QLatin1String("view.switch_to_3d");
    }

    // 主题切换命令（theme.*）：与工作台切换同理，直接在分发器层面放行。
    static bool isThemeCommand(const QString& commandId)
    {
        return commandId.startsWith(QLatin1String("theme."));
    }

    // 语言切换命令（language.*）：同上。语言由 LanguageManager 统一管理，
    // 不注册在 2D/3D 命令目录中，需要分发器识别才可点击。
    static bool isLanguageCommand(const QString& commandId)
    {
        return commandId.startsWith(QLatin1String("language."));
    }

    bool isCommandRegistered(const QString& commandId) const override
    {
        if (WorkbenchMenuManager::isWindowLevelCommand(commandId))
        {
            return true;
        }
        return workbench && workbench->isCommandRegistered(commandId);
    }


    void dispatch(const QString& commandId, const QVariantMap& params) override
    {
        // 工作台切换统一由主窗口 triggerWorkbench 处理（含防重复切换保护）。

        if (isWorkbenchSwitchCommand(commandId) && self && self->workbenchWindow())
        {
            const QString target =
                commandId == QLatin1String("view.switch_to_3d") ? QStringLiteral("3D") : QStringLiteral("2D");
            self->workbenchWindow()->triggerWorkbench(target);
            return;
        }
        // 主题切换：直接走主窗口主题切换，不进命令总线。

        if (isThemeCommand(commandId) && self && self->workbenchWindow())
        {
            self->workbenchWindow()->triggerTheme(commandId);
            return;
        }
        // 关于对话框：help.about 是窗口级动作，不应进入命令目录。
        if (commandId == QLatin1String("help.about") && self && self->workbenchWindow())
        {
            QMessageBox::about(self->workbenchWindow(),
                QObject::tr("About"),
                QObject::tr("SanYiCAD\nVersion: %1").arg(qApp ? qApp->applicationVersion() : QString()));
            return;
        }
        // 语言切换：language.<code> → AppLanguage，落盘后由 SettingsService 应用。
        if (isLanguageCommand(commandId))
        {
            QString code = commandId.mid(QStringLiteral("language.").size());
            AppLanguage lang = AppLanguage::English;
            if (const auto parsed = LanguageManager::fromCode(code); parsed.has_value())
            {
                lang = *parsed;
            }
            else
            {
                SY_WARNF("[MenuDispatcher] unknown language code '%s', ignore", qPrintable(code));
                return;
            }
            // 必须落盘：只调 LM->setLanguage 的话语言只活在内存里，一旦有人再走
            // applyCommonSettings（设置对话框确定、SettingsService 懒初始化）就会被
            // 库里的 common/language 覆盖回去，表现为「切个工作台语言就丢了」。
            // SettingsService::setLanguage 内部会写库并调 LM->setLanguage。
            if (auto* settings = ApplicationCompositionRoot::getSettingsService(); settings && settings->isInitialized())
            {
                settings->setLanguage(lang);
            }
            else
            {
                LM->setLanguage(lang);
            }
            // 语言切换成功会经 LanguageChange → retranslateUi → rebuildAllMenus 回填勾选；
            // 但 .qm 缺失时不发 LanguageChange，这里补一次，保证勾选态与实际语言一致。
            if (self)
            {
                self->refreshConfiguredMenuState();
            }
            return;
        }
        if (!workbench)
        {
            SY_WARNF("[WorkbenchMenuManager] No active workbench for command='%s'", qPrintable(commandId));
            return;
        }
        workbench->dispatchCommand(commandId, params);
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



void WorkbenchMenuManager::rebuildAllMenus()
{
    // Clear old menus and shortcuts
    clearGlobalShortcuts();
    if (auto* mb = m_window->menuBar())
    {
        // 同 WorkbenchLayoutManager::clearLayoutContent：clear() 不销毁 QMenu 本体，
        // 必须显式回收，否则每次重建（切工作台/切语言/重载客户配置）都泄漏一棵菜单树。
        QList<QMenu*> staleMenus;
        for (QAction* act : mb->actions())
        {
            if (act && act->menu())
            {
                staleMenus.append(act->menu());
            }
        }
        mb->clear();
        for (QMenu* sub : staleMenus)
        {
            // 重建常由菜单项自身的 triggered 触发，QMenu 仍在调用栈上 —— 延后删除。
            sub->setParent(nullptr);
            sub->deleteLater();
        }
    }
    // 菜单只有一条构建路径：JSON 配置驱动。
    rebuildMenusFromConfig();
    bindConfiguredMenuState();

    // 重建产出的 QAction 一律是 checked=false（JSON 里没写 checked）。状态中心的信号只在
    // 状态"变化"时才发，重建后不会补发，所以必须在这里主动回填一次勾选态，
    // 否则语言/网格/吸附等勾选项在每次工作台切换、语言切换后都是空的。
    refreshConfiguredMenuState();


    // 菜单重建产出的是全新 QAction，enabled 默认为 true。
    // 必须请工作台重推一次命令 UI 快照，否则「无选中却能点删除/对齐」会在每次
    // 工作台切换、语言切换、客户配置重载后复现。
    if (m_workbench)
    {
        m_workbench->refreshCommandUiState();
    }

    // macOS 原生菜单栏收尾：移除系统注入到 Edit 菜单的「表情与符号 / 开始听写」。
    // 必须排到事件循环下一轮——Qt 是延迟把 QMenuBar 同步到 NSMenu 的，同步调用会扫到空菜单。
    // 也必须每次重建都做：上面刚把整棵菜单树销毁重建，NSMenu 是全新的，启动时清一次不够。
    // 非 macOS 平台该函数是头文件里的内联空实现，无需在调用点做条件编译。
    QTimer::singleShot(0, m_window, [] { cleanupMacEditMenuSystemItems(); });
}


bool WorkbenchMenuManager::isWindowLevelCommand(const QString& commandId)
{
    // 唯一真相：MenuDispatcher 短路处理的命令集合。改这里即同时改变
    // 「按钮是否可点」（isCommandRegistered）与「契约测试是否放行」两处行为。
    return MenuDispatcher::isWorkbenchSwitchCommand(commandId) || MenuDispatcher::isThemeCommand(commandId)
        || MenuDispatcher::isLanguageCommand(commandId);
}

IUiCommandDispatcher* WorkbenchMenuManager::commandDispatcher()
{
    if (!m_dispatcher)
    {
        m_dispatcher = std::make_unique<MenuDispatcher>();
        m_dispatcher->self = this;
    }
    // 每次取用都同步工作台：菜单/工具栏的构建时机早于 setWorkbench 的场景是常态，
    // 而 isCommandRegistered() 要靠当前工作台的命令目录才能给出正确答案。
    m_dispatcher->workbench = m_workbench;
    return m_dispatcher.get();
}

IShortcutSettingsModel* WorkbenchMenuManager::shortcutSettingsModel() const
{
    return m_shortcutSettingsModel.get();
}


void WorkbenchMenuManager::registerRecentFilesSection()
{
    // 菜单里最多列多少条最近文件。
    // 服务的 QSettings 兜底路径自己截到 10 条，但数据库路径（RecentFileRepository::loadAll）
    // 不截断，所以菜单侧必须自己卡一次，否则库里攒了几百条会撑出一个滚动到屏幕外的菜单。
    constexpr int kMaxRecentMenuItems = 10;

    UiContextMenuService::instance().registerDynamicSection(
        QStringLiteral("file.recent"), [this](QMenu* menu) {
            if (!menu)
            {
                return;
            }

            IRecentFileService* recentFiles = m_uiServices ? m_uiServices->recentFileService : nullptr;
            const QStringList files = recentFiles ? recentFiles->loadRecentFiles() : QStringList{};

            // 服务缺失与列表为空给同一种反馈：一个禁用占位项。
            // 不能一条都不加 —— 空子菜单在 Qt 里点开是个空白小方块，看起来像菜单坏了。
            if (files.isEmpty())
            {
                QAction* placeholder = menu->addAction(tr("No Recent Files"));
                placeholder->setEnabled(false);
                return;
            }

            IUiCommandDispatcher* dispatcher = commandDispatcher();
            int index = 0;
            for (const QString& filePath : files)
            {
                if (index >= kMaxRecentMenuItems)
                {
                    break;
                }
                ++index;

                // 只显示文件名，完整路径进 tooltip/statusTip：最近文件路径动辄很长，
                // 直接当菜单文本会把 File 菜单撑到半屏宽。
                QAction* action = menu->addAction(
                    QStringLiteral("&%1  %2").arg(index).arg(QFileInfo(filePath).fileName()));
                action->setToolTip(filePath);
                action->setStatusTip(filePath);
                // 刻意不设 property("commandId")：启用态刷新会按 commandId 去当前工作台
                // 命令目录反查规则，而 file.open_recent 只在 2D 目录里；3D 下会变成每次
                // 刷新都告警一遍。这批条目本身恒可用（目录里 File_OpenRecent 也是 Always），
                // 无需纳入启用态联动。

                // 路径经 dispatch 的 QVariantMap 透传：file.open_recent → OperationId::File_OpenRecent
                // 是 ParamLambdaOperation，读的正是 params["filePath"]。
                // 接收者用 action 而不是 this：菜单重填会删掉这批 action，连接随之断开。
                QObject::connect(action, &QAction::triggered, action, [dispatcher, filePath]() {
                    if (!dispatcher)
                    {
                        SY_WARN("[WorkbenchMenuManager] Recent file triggered without dispatcher");
                        return;
                    }
                    dispatcher->dispatch(QStringLiteral("file.open_recent"),
                        QVariantMap{ { QStringLiteral("filePath"), filePath } });
                });
            }
        });
}

void WorkbenchMenuManager::rebuildMenusFromConfig()
{
    // 配置驱动菜单的原则：同一份 UiConfigData 同时驱动菜单、工具栏、Dock、状态栏、右键菜单。
    // 菜单部分由 UiLayoutBuilder 负责生成，WorkbenchMenuManager 负责接入工作台上下文与回退策略。
    if (!m_menuPanelRegistry)
    {
        m_menuPanelRegistry = std::make_unique<UiPanelRegistry>();
        // 内置面板/状态栏槽位工厂集中注册，与 WorkbenchLayoutManager 使用同一份实现，
        // 避免两个注册表内容漂移（P0-1）
        registerBuiltinUiPanels(*m_menuPanelRegistry);
    }

    // 客户配置取自进程级共享实例（P0-1）：菜单、工具栏、Dock、状态栏、右键菜单
    // 全部消费同一份 UiConfigData，客户 ID 由 UiClientContext 在运行时统一解析。
    // 历史实现这里自己 new 了一个 UiConfigurationManager 并读环境变量，
    // 而布局侧另 new 一个并读编译期宏，两者可能解析出不同客户。
    UiConfigurationManager& configManager = UiConfigurationManager::shared();
    const QString clientId = UiClientContext::instance().clientId();
    const UiConfigData* config = configManager.configData();
    if (!config)
    {
        // 配置缺失时不再退回硬编码菜单：legacy 那条链已删除。
        // 菜单一栏都建不出来是装配级故障，必须显性报错，而不是悄悄换一套结构。
        SY_ERRORF("[WorkbenchMenuManager] Shared client config unavailable (client='%s'), menus not built",
            qPrintable(clientId));
        return;
    }


    // 命令分发器随 WorkbenchMenuManager 生命周期持有（成员 m_dispatcher）：
    // UiLayoutBuilder 会把该指针存入 QAction 触发回调并长期解引用，必须保证指针在菜单存在期间有效。
    m_menuLayoutBuilder = std::make_unique<UiLayoutBuilder>(m_window, commandDispatcher(), m_menuPanelRegistry.get());

    // 注册 File ▸ Recent Files 动态段，必须在建菜单之前 ——
    // UiLayoutBuilder 建到该子菜单时会立刻填一次。
    registerRecentFilesSection();

    m_menuLayoutBuilder->clearBuiltLayout();

    // 菜单/工具栏由同一个配置对象生成；菜单项会根据 workbenches 字段与当前工作台命令目录双重过滤。
    const QString wbId = currentWorkbenchId(m_stateCenter);

    // 快捷键台账按工作台分作用域：用户覆盖在构建期叠加到配置默认值上，
    // 同时把这一轮建出的 QAction/QShortcut 登记进去，供设置页的快捷键页直接编辑。
    if (!m_shortcutRegistry)
    {
        m_shortcutRegistry = std::make_unique<UiShortcutRegistry>();
        m_shortcutSettingsModel = std::make_unique<UiShortcutSettingsModel>(m_shortcutRegistry.get());
    }
    m_shortcutRegistry->beginBuild(wbId);
    m_menuLayoutBuilder->setShortcutRegistry(m_shortcutRegistry.get());

    const auto commandAvailable = [this](const QString& commandId) {
        if (commandId.isEmpty())
        {
            return false;
        }
        // 工作台切换 / 主题 / 语言属于窗口级动作命令（由 MenuDispatcher 直接处理），
        // 不注册在 2D/3D 命令目录中，过滤时必须放行，否则对应菜单项会被移除。
        if (MenuDispatcher::isWorkbenchSwitchCommand(commandId) || MenuDispatcher::isThemeCommand(commandId) ||
            MenuDispatcher::isLanguageCommand(commandId) || commandId == QLatin1String("help.about") ||
            commandId.startsWith(QStringLiteral("language."), Qt::CaseInsensitive))
        {
            return true;
        }
        if (!m_workbench)
        {
            return true;
        }
        const bool registered = m_workbench->isCommandRegistered(commandId);
        return registered;
    };

    std::vector<MenuDef> filteredMenus =
        filterMenusForWorkbench(config->menus, wbId, commandAvailable, m_workbench ? m_workbench->id() : QString());

    // 职责划分：WorkbenchMenuManager 只负责菜单与快捷键；
    // 工具栏 / Dock 由 WorkbenchLayoutManager（buildToolBars / buildDockAreasFromConfig）统一构建。
    std::vector<ShortcutDef> filteredShortcuts;
    filteredShortcuts.reserve(config->shortcuts.size());
    for (const auto& shortcut : config->shortcuts)
    {
        if (!shortcut.commandId.isEmpty() && commandAvailable(shortcut.commandId))
        {
            filteredShortcuts.push_back(shortcut);
        }
    }

    m_menuLayoutBuilder->buildMenus(filteredMenus);
    m_menuLayoutBuilder->buildShortcuts(filteredShortcuts);

    SY_DEBUGF("[WorkbenchMenuManager] Config-driven menus built: client='%s', workbench='%s', menus=%lld, shortcuts=%lld",
        qPrintable(clientId),
        qPrintable(wbId),
        static_cast<long long>(filteredMenus.size()),
        static_cast<long long>(filteredShortcuts.size()));
}

void WorkbenchMenuManager::buildMenus()

{
    rebuildMenusFromConfig();
    bindConfiguredMenuState();
}

void WorkbenchMenuManager::clearGlobalShortcuts()

{
    for (QAction* action : m_editShortcuts)
    {
        if (action)
        {
            m_window->removeAction(action);
            delete action;
        }
    }
    m_editShortcuts.clear();
}

void WorkbenchMenuManager::bindShortcuts()
{
    // 先清理旧动作，防止重复叠加
    clearGlobalShortcuts();

    // 这里**不再**自建 Undo / Redo 的窗口级 QAction。
    //
    // 快捷键只保留一条注册路径：客户 JSON。Edit 菜单项自带 "shortcut": "Ctrl+Z" /
    // "Ctrl+Y"（configs/base.json），UiLayoutBuilder::buildMenuItem 会把它设到菜单
    // QAction 上；shortcuts[] 节里的重复定义也已清理，并且 buildShortcuts 还有一道
    // "菜单已声明过该键序列就不再建 QShortcut" 的守卫。
    //
    // 此前这里额外建的窗口级 QAction 用的是 QKeySequence::Undo / Redo，在 Windows 上
    // 正是 Ctrl+Z / Ctrl+Y —— 与菜单项同键、同为窗口范围，Qt 判定 ambiguous 后两个
    // 接收者都不触发，表现为"按 Ctrl+Z 有时没反应"，且只在特定焦点链下出现。
    //
    // 启用态并未因此丢失：菜单 QAction 带 commandId，本来就在 forEachCommandAction
    // 的遍历范围内，撤销栈为空时会被快照刷新灰掉，比窗口级动作的覆盖面更完整。
    //
    // clearGlobalShortcuts 与 m_editShortcuts 保留：工作台切换时仍需清理上一批
    // 由其他路径注册的窗口级动作（当前为空是正常状态）。

    // 菜单/工具栏的启用态需按当前快照校正一次（重建菜单后新动作默认 enabled=true）
    if (m_workbench)
    {
        m_workbench->refreshCommandUiState();
    }
}



std::vector<MenuDef> WorkbenchMenuManager::filterMenusForWorkbench(const std::vector<MenuDef>& menus,

    const QString& workbenchId,
    const std::function<bool(const QString&)>& commandAvailable,
    const QString& workbenchKind)
{
    // 分隔符归一化：JSON 里的分隔符是按“全部菜单项都在”排版的，
    // 过滤掉不属于当前工作台的动作后会留下开头/结尾/连续的空分隔线，
    // 这会让 3D 菜单看起来像一堆断裂的空行。这里在数据层收敛，构建层无需关心。
    const auto normalizeSeparators = [](std::vector<std::variant<MenuActionDef, SubMenuDef, MenuItemType>>& items) {
        const auto isSeparator = [](const std::variant<MenuActionDef, SubMenuDef, MenuItemType>& item) {
            return std::holds_alternative<MenuItemType>(item) &&
                std::get<MenuItemType>(item) == MenuItemType::Separator;
        };
        std::vector<std::variant<MenuActionDef, SubMenuDef, MenuItemType>> normalized;

        normalized.reserve(items.size());
        for (const auto& item : items)
        {
            if (isSeparator(item) && (normalized.empty() || isSeparator(normalized.back())))
            {
                continue;
            }
            normalized.push_back(item);
        }
        while (!normalized.empty() && isSeparator(normalized.back()))
        {
            normalized.pop_back();
        }
        items.swap(normalized);
    };

    const auto visibilityAllowed = [&](const QString& scope) {

        if (scope.isEmpty())
        {
            return true;
        }
        if (workbenchKind.compare(QStringLiteral("3D"), Qt::CaseInsensitive) == 0)
        {
            return scope.compare(QStringLiteral("3D"), Qt::CaseInsensitive) == 0 ||
                scope.compare(QStringLiteral("shared"), Qt::CaseInsensitive) == 0;
        }
        return scope.compare(QStringLiteral("2D"), Qt::CaseInsensitive) == 0 ||
            scope.compare(QStringLiteral("shared"), Qt::CaseInsensitive) == 0;
    };

    std::function<bool(const MenuActionDef&, MenuActionDef&)> filterAction = [&](const MenuActionDef& action,
                                                                                 MenuActionDef& outAction) -> bool {
        if (!action.visible || !commandEnabledForWorkbench(action.workbenches, workbenchId))
        {
            return false;
        }
        if (!visibilityAllowed(action.visibilityScope))
        {
            return false;
        }
        if (!commandAvailable(action.commandId))
        {
            return false;
        }

        outAction = action;
        return true;
    };

    std::function<bool(const SubMenuDef&, SubMenuDef&)> filterSubMenu = [&](const SubMenuDef& sub,
                                                                            SubMenuDef& outSub) -> bool {
        if (!sub.visible || !commandEnabledForWorkbench(sub.workbenches, workbenchId))
        {
            return false;
        }
        if (!visibilityAllowed(sub.visibilityScope))
        {
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
                {
                    outSub.items.push_back(filteredAction);
                }
            }
            else if (std::holds_alternative<SubMenuDef>(subItem))
            {
                SubMenuDef filteredSub;
                if (filterSubMenu(std::get<SubMenuDef>(subItem), filteredSub))
                {
                    outSub.items.push_back(filteredSub);
                }
            }
            else if (std::holds_alternative<MenuItemType>(subItem))
            {
                outSub.items.push_back(subItem);
            }
        }
        normalizeSeparators(outSub.items);
        // 声明了 dynamicSections 的子菜单不能因为"静态条目为空"被裁掉 ——
        // 这类子菜单的条目本来就在运行时才生成（File ▸ Recent Files 是纯动态的，
        // 静态条目一个都没有）。裁掉的话 UiLayoutBuilder 根本看不到它，动态段永远填不进去。
        return !outSub.items.empty() || !outSub.dynamicSections.isEmpty();
    };


    std::vector<MenuDef> filteredMenus;
    filteredMenus.reserve(menus.size());
    for (const auto& menu : menus)
    {
        if (!menu.visible || !commandEnabledForWorkbench(menu.workbenches, workbenchId))
        {
            continue;
        }
        if (!visibilityAllowed(menu.visibilityScope))
        {
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
                {
                    menuCopy.items.push_back(filteredAction);
                }
            }
            else if (std::holds_alternative<SubMenuDef>(item))
            {
                SubMenuDef filteredSub;
                if (filterSubMenu(std::get<SubMenuDef>(item), filteredSub))
                {
                    menuCopy.items.push_back(filteredSub);
                }
            }
            else if (std::holds_alternative<MenuItemType>(item))
            {
                menuCopy.items.push_back(item);
            }
        }
        normalizeSeparators(menuCopy.items);
        if (!menuCopy.items.empty())

        {
            filteredMenus.push_back(menuCopy);
        }
    }
    return filteredMenus;
}

void WorkbenchMenuManager::bindConfiguredMenuState()
{
    if (!m_stateCenter)
    {
        return;
    }
    // 配置驱动模式下每次重建都会调用本站，避免重复连接导致刷新叠加
    if (m_configStateBound)
    {
        return;
    }
    m_configStateBound = true;

    QObject::connect(m_stateCenter, &UiStateCenter::stateChanged, this, [this]() {
        refreshConfiguredMenuState();
    });
    QObject::connect(m_stateCenter, &UiStateCenter::metadataChanged, this, [this]() {
        refreshConfiguredMenuState();
    });
    QObject::connect(m_stateCenter, &UiStateCenter::currentWorkbenchChanged, this, [this](const QString&) {
        refreshConfiguredMenuState();
    });
    QObject::connect(m_stateCenter, &UiStateCenter::currentThemeChanged, this, [this](const QString&) {
        refreshConfiguredMenuState();
    });
    refreshConfiguredMenuState();
}

void WorkbenchMenuManager::forEachCommandAction(
    const std::function<void(QAction*, const QString&)>& visitor) const
{
    if (!visitor)
    {
        return;
    }

    // 同一个 QAction 可能既在菜单里又在工具栏上（中枢托管的 action 就是这样），
    // 去重后再交给 visitor：启用态求值本身是幂等的，但重复访问会让日志和
    // 后续可能加入的"只允许写一次"类断言失效。
    QSet<QAction*> visited;
    auto visitOnce = [&](QAction* action, const QString& cmdId) {
        if (!action || visited.contains(action))
        {
            return;
        }
        visited.insert(action);
        visitor(action, cmdId);
    };

    std::function<void(QMenu*)> walk = [&](QMenu* menu) {
        if (!menu)
        {
            return;
        }
        for (QAction* action : menu->actions())
        {
            if (!action)
            {
                continue;
            }
            // 子菜单标题项自身不携带 commandId，递归进去处理叶子项
            if (QMenu* subMenu = action->menu())
            {
                walk(subMenu);
                continue;
            }
            const QString cmdId = action->property("commandId").toString();
            if (cmdId.isEmpty())
            {
                continue;
            }
            visitOnce(action, cmdId);
        }
    };


    // 菜单一律挂在 QMenuBar 上（JSON 配置驱动构建），遍历菜单栏即可覆盖全部菜单项
    QMenuBar* menuBar = m_window ? m_window->menuBar() : nullptr;
    if (menuBar)
    {
        for (QAction* act : menuBar->actions())
        {
            if (QMenu* menu = act->menu())
            {
                walk(menu);
            }
        }
    }


    // 工具栏也必须遍历。工具栏动作与菜单项来自同一份命令目录、同一份启用规则，
    // 只是挂载位置不同；此前这里只走菜单栏，导致"菜单灰了工具栏还能点"的漂移，
    // 是启用态最容易出现分叉的地方。
    // 注意：配置驱动工具栏由 WorkbenchLayoutManager 建在主窗口上，
    // 因此只能按 findChildren 取，不能依赖成员指针。

    if (m_window)
    {
        for (QToolBar* toolBar : m_window->findChildren<QToolBar*>())
        {
            if (!toolBar)
            {
                continue;
            }
            for (QAction* action : toolBar->actions())
            {
                if (!action)
                {
                    continue;
                }
                const QString cmdId = action->property("commandId").toString();
                if (cmdId.isEmpty())
                {
                    continue;
                }
                visitOnce(action, cmdId);
            }
        }
    }

    // 窗口级快捷键动作（Undo / Redo）不挂在任何菜单上，只能从成员容器取；
    // 它们同样带 commandId，因此走同一个 visitor，启用态与菜单项保持一致。
    for (QAction* action : m_editShortcuts)
    {
        if (!action)
        {
            continue;
        }
        const QString cmdId = action->property("commandId").toString();
        if (cmdId.isEmpty())
        {
            continue;
        }
        visitOnce(action, cmdId);
    }
}


void WorkbenchMenuManager::refreshCommandStates(const CommandUiSnapshot& snapshot)
{
    // 规则求值与"跳过哪些项"的判定统一由命令中枢的静态应用器负责，
    // 与配置化右键菜单共用同一份实现，本类只负责"遍历到哪些 action"。
    forEachCommandAction([&snapshot](QAction* action, const QString& /*cmdId*/) {
        CommandActionHub::applySnapshotToAction(action, snapshot);
    });
}

#if BUILD_UI3D
void WorkbenchMenuManager::refreshCommandStates3D(const CommandUiSnapshot3D& snapshot)
{
    forEachCommandAction([&snapshot](QAction* action, const QString& /*cmdId*/) {
        CommandActionHub3D::applySnapshotToAction(action, snapshot);
    });
}
#endif



void WorkbenchMenuManager::refreshConfiguredMenuState()
{
    if (!m_stateCenter)
    {
        return;
    }

    const auto state = m_stateCenter->snapshot();
    const QString wbId = state.currentWorkbenchId;
    const auto applyChecked = [](QAction* action, bool checked) {
        if (!action)
        {
            return;
        }
        QSignalBlocker blocker(action);
        action->setChecked(checked);
    };

    forEachCommandAction([&](QAction* action, const QString& cmdId) {
        if (cmdId == QStringLiteral("view.grid") || cmdId == QStringLiteral("view.grid_visible"))
        {
            applyChecked(action, state.metadata.value(QStringLiteral("gridVisible")).toBool());
        }
        else if (cmdId == QStringLiteral("view.snap") || cmdId == QStringLiteral("view.snap_enabled"))
        {
            applyChecked(action, state.metadata.value(QStringLiteral("snapEnabled")).toBool());
        }
        else if (cmdId == QStringLiteral("view.ortho") || cmdId == QStringLiteral("view.ortho_mode"))
        {
            applyChecked(action, state.metadata.value(QStringLiteral("orthoMode")).toBool());
        }
        else if (cmdId == QStringLiteral("view.angle_snap"))
        {
            applyChecked(action, state.metadata.value(QStringLiteral("angleSnap")).toBool());
        }
        else if (cmdId == QStringLiteral("view.wireframe"))
        {
            applyChecked(action, state.metadata.value(QStringLiteral("wireframe")).toBool());
        }
        else if (cmdId == QStringLiteral("view.bbox"))
        {
            applyChecked(action, state.metadata.value(QStringLiteral("bbox")).toBool());
        }
        else if (cmdId == QStringLiteral("view.floor"))
        {
            applyChecked(action, state.metadata.value(QStringLiteral("floor")).toBool());
        }
        else if (cmdId == QStringLiteral("view.unit_mm"))
        {
            applyChecked(action, wbId.compare(QStringLiteral("2D"), Qt::CaseInsensitive) == 0);
        }
        else if (cmdId == QStringLiteral("view.unit_cm"))
        {
            applyChecked(action, false);
        }
        else if (cmdId == QStringLiteral("view.unit_inch"))
        {
            applyChecked(action, false);
        }
        // 语言勾选态：菜单每次重建都是全新 QAction（checked 默认 false），
        // 必须按 LanguageManager 的当前语言回填，否则 2D/3D 切换后勾选态全部消失。
        else if (cmdId.startsWith(QStringLiteral("language.")))
        {
            const QString code = cmdId.mid(QStringLiteral("language.").size());
            const auto lang = LanguageManager::fromCode(code);
            applyChecked(action, lang.has_value() && *lang == LM->currentLanguage());
        }
        // 主题勾选态：来源是 ThemeManager 而不是状态中心，同样要在这里回填。
        // 注意用 System/Default 时勾的是 System/Default 本身，不是它解析出来的实际皮肤。
        else if (cmdId.startsWith(QStringLiteral("theme.")))
        {
            applyChecked(action, ThemeManager::menuIdFor(TM->currentTheme()) == cmdId.toLower());
        }
    });
}
