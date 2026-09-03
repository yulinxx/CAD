#include "UiLayoutBuilder.h"
#include "UiContextMenuService.h"
#include "UiFeatureGate.h"
#include "UiPanelRegistry.h"
#include "UiShortcutRegistry.h"

#include "UI2D/Operation/CommandCatalog.h"
#include "Log/SyLogger.h"

#include <QAction>
#include <QCoreApplication>
#include <QDockWidget>
#include <QIcon>
#include <QKeySequence>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QShortcut>
#include <QStatusBar>
#include <QToolBar>
#include <QWidget>

namespace
{
    Qt::ToolBarArea toToolBarArea(ToolBarPosition position)
    {
        switch (position)
        {
        case ToolBarPosition::Left:
            return Qt::LeftToolBarArea;
        case ToolBarPosition::Right:
            return Qt::RightToolBarArea;
        case ToolBarPosition::Bottom:
            return Qt::BottomToolBarArea;
        case ToolBarPosition::Top:
        default:
            return Qt::TopToolBarArea;
        }
    }

    /// 授权闸门统一入口：feature 为空或已授权时放行
    /// 未授权的 UI 项直接不创建（而非禁用），避免向客户暴露未购买的功能入口
    bool featureAllowed(const QString& feature)
    {
        return UiFeatureGate::instance().isAllowed(feature);
    }

    /// 按稳定的 action id 显式声明 macOS 菜单角色。
    /// QAction 默认是 TextHeuristicRole：Qt 按**文本**（Exit/Quit/About/Settings/Preferences/
    /// Options）猜测角色，命中就把该动作搬进 macOS 应用菜单。文本一旦被翻译成"退出"/"关于"
    /// 启发即失效，于是同一台 Mac 上切个语言菜单结构就变，这种不一致比平台差异更难排查。
    /// 这里按 id 显式声明，与文本、语言彻底解耦；其余项一律 NoRole，禁止 Qt 擅自搬迁。
    /// 非 macOS 平台 Qt 忽略 menuRole，设置本身无副作用。
    QAction::MenuRole menuRoleForActionId(const QString& actionId)
    {
        if (actionId == QLatin1String("file.exit"))
        {
            return QAction::QuitRole;
        }
        if (actionId == QLatin1String("help.about"))
        {
            return QAction::AboutRole;
        }
        if (actionId == QLatin1String("help.settings"))
        {
            return QAction::PreferencesRole;
        }
        return QAction::NoRole;
    }


    Qt::DockWidgetArea toDockArea(DockPosition position)
    {
        switch (position)
        {
        case DockPosition::Left:
            return Qt::LeftDockWidgetArea;
        case DockPosition::Top:
            return Qt::TopDockWidgetArea;
        case DockPosition::Bottom:
            return Qt::BottomDockWidgetArea;
        case DockPosition::Right:
        default:
            return Qt::RightDockWidgetArea;
        }
    }

    QString actionLabel(const QString& label, const QString& id)
    {
        if (!label.isEmpty())
        {
            // 优先从 WorkbenchMenuManager 上下文翻译（已包含 File/Edit/View/Draw/Help 等顶层菜单文案），
            // 回退到 UiLayoutBuilder。这样 JSON 菜单标签能复用 legacy 路径的翻译条目。
            QString translated = QCoreApplication::translate("WorkbenchMenuManager", label.toUtf8().constData());
            if (translated != label)
            {
                return translated;
            }
            translated = QCoreApplication::translate("MainWindow", label.toUtf8().constData());
            if (translated != label)
            {
                return translated;
            }
            return QCoreApplication::translate("UiLayoutBuilder", label.toUtf8().constData());
        }
        return id;
    }

    QString resolveIconFromCatalog(const QString& commandId)
    {
        if (commandId.isEmpty())
        {
            return QString();
        }
        const OperationId opId = CommandCatalog::operationForCommandId(commandId);
        if (opId != OperationId::None)
        {
            const CommandEntry2D* entry = CommandCatalog::findByOperation(opId);
            if (entry && entry->iconResource)
            {
                return QString::fromUtf8(entry->iconResource);
            }
        }
        return QString();
    }
}  // namespace

UiLayoutBuilder::UiLayoutBuilder(QMainWindow* window, IUiCommandDispatcher* dispatcher, UiPanelRegistry* panelRegistry)
    : m_window(window)
    , m_dispatcher(dispatcher)
    , m_panelRegistry(panelRegistry)
{
}

UiLayoutBuilder::~UiLayoutBuilder()
{
    releaseBuiltShortcuts();
}

void UiLayoutBuilder::releaseBuiltShortcuts()
{
    for (QShortcut* shortcut : m_builtShortcuts)
    {
        delete shortcut;
    }
    m_builtShortcuts.clear();
}

void UiLayoutBuilder::clearBuiltLayout()
{
    m_builtDocks.clear();
    m_builtToolBars.clear();
    m_builtStatusBarSlots.clear();
    m_menuShortcutKeys.clear();
    // Dock / 工具栏 / 状态栏槽位的销毁由上层（WorkbenchLayoutManager）负责，
    // 这里只丢引用；快捷键没有别的持有者，必须本类销毁。
    releaseBuiltShortcuts();
}

void UiLayoutBuilder::bindAction(QAction* action, const QString& commandId)
{
    bindAction(action, commandId, QString(), QString(), QString());
}

void UiLayoutBuilder::bindAction(QAction* action,
    const QString& commandId,
    const QString& text,
    const QString& iconResource,
    const QString& workbenchId)
{
    if (!action || commandId.isEmpty())
    {
        return;
    }

    if (!text.isEmpty())
    {
        action->setText(text);
    }
    QString resolvedIcon = iconResource;
    if (resolvedIcon.isEmpty())
    {
        resolvedIcon = resolveIconFromCatalog(commandId);
    }
    if (!resolvedIcon.isEmpty())
    {
        action->setIcon(QIcon(resolvedIcon));
    }
    if (!workbenchId.isEmpty())
    {
        action->setProperty("workbenchId", workbenchId);
    }
    action->setProperty("commandId", commandId);

    // objectName 由 buildMenuItem 在调用本函数前写入 action id（工具栏/右键菜单可能为空）。
    // menuRole 对非菜单动作无效，统一设置不会有副作用。
    action->setMenuRole(menuRoleForActionId(action->objectName()));


    if (m_dispatcher && m_dispatcher->isCommandRegistered(commandId))
    {
        // 捕获 dispatcher 而不是 this：本 builder 每次 rebuildMenusFromConfig 都被整体
        // 替换（WorkbenchMenuManager::m_menuLayoutBuilder 是 unique_ptr），而它建出来的
        // QAction 挂在 menubar 子树上、活得更久。捕获 this 的话，切一次工作台之后
        // 触发这些 action 就会解引用已析构的 builder。
        // dispatcher 由 WorkbenchMenuManager 持有且只创建一次，寿命足够。
        QObject::connect(action, &QAction::triggered,
            [dispatcher = m_dispatcher, action, commandId](bool) {
                SY_DEBUGF("[Menu] trigger text='%s' command='%s'",
                    action->text().toUtf8().constData(),
                    commandId.toUtf8().constData());
                dispatcher->dispatch(commandId);
            });
    }

    else
    {
        // 命令未注册 → 按钮禁用 + 提示
        action->setEnabled(false);
        // 标记为"永久不可用"：这是构建期的静态结论（命令根本没注册），
        // 与"当前选择/剪贴板/撤销栈不满足条件"是两回事。
        // 快照驱动的启用态刷新（WorkbenchMenuManager::refreshCommandStates）必须跳过这类项，
        // 否则会把未实现的功能重新点亮。
        action->setProperty("commandUnavailable", true);
        action->setToolTip(
            QCoreApplication::translate("UiLayoutBuilder", "Feature not enabled (command: %1)").arg(commandId));
        SY_WARNF("[UiLayoutBuilder] Unknown command id: %s", qPrintable(commandId));
    }
}

void UiLayoutBuilder::buildMenus(const std::vector<MenuDef>& menus)
{
    // 每轮重建都从空集开始：菜单按当前工作台重新过滤，上一轮占用的键序列不能带进来。
    m_menuShortcutKeys.clear();
    if (!m_window)
    {
        return;
    }
    QMenuBar* menuBar = m_window->menuBar();
    if (!menuBar)
    {
        return;
    }
    if (!menuBar)
    {
        return;
    }

    for (const auto& menu : menus)
    {
        if (menu.id.isEmpty() || !menu.visible)
        {
            SY_DEBUGF("[UiLayoutBuilder] Skip menu id='%s' visible=%d", qPrintable(menu.id), menu.visible ? 1 : 0);
            continue;
        }
        QMenu* qMenu = menuBar->addMenu(actionLabel(menu.label, menu.id));
        if (!qMenu)
        {
            continue;
        }
        qMenu->setObjectName(menu.id);
        if (!menu.iconName.isEmpty())
        {
            qMenu->menuAction()->setIcon(QIcon(menu.iconName));
        }
        qMenu->setProperty("workbenches", menu.workbenches);
        for (const auto& item : menu.items)
        {
            buildMenuItem(qMenu, item);
        }

        int enabledCount = 0;
        for (QAction* act : qMenu->actions())
        {
            if (act && act->isEnabled())
            {
                ++enabledCount;
            }
        }
        SY_DEBUGF("[UiLayoutBuilder] Menu built id='%s' label='%s' workbench='%s' items=%d enabled=%d/%d",
            qPrintable(menu.id),
            qPrintable(actionLabel(menu.label, menu.id)),
            qPrintable(menu.workbenches.join(QStringLiteral(","))),
            static_cast<int>(menu.items.size()),
            enabledCount,
            static_cast<int>(qMenu->actions().size()));
    }

    SY_DEBUGF("[UiLayoutBuilder] All menus built: total=%d topLevelMenus=%d",
        static_cast<int>(menus.size()),
        m_window->menuBar() ? static_cast<int>(m_window->menuBar()->actions().size()) : 0);
}

void UiLayoutBuilder::buildMenuItem(QMenu* parent, const std::variant<MenuActionDef, SubMenuDef, MenuItemType>& item)
{
    if (!parent)
    {
        return;
    }

    if (std::holds_alternative<MenuItemType>(item))
    {
        if (std::get<MenuItemType>(item) == MenuItemType::Separator)
        {
            parent->addSeparator();
        }
        return;
    }

    if (std::holds_alternative<SubMenuDef>(item))
    {
        const SubMenuDef& sub = std::get<SubMenuDef>(item);
        if (!sub.visible)
        {
            SY_DEBUGF("[UiLayoutBuilder] Skip submenu id='%s' visible=0", qPrintable(sub.id));
            return;
        }
        QMenu* subMenu = parent->addMenu(actionLabel(sub.label, sub.id));
        if (!subMenu)
        {
            return;
        }
        subMenu->setObjectName(sub.id);
        if (!sub.iconName.isEmpty())
        {
            subMenu->menuAction()->setIcon(QIcon(sub.iconName));
        }
        subMenu->setProperty("workbenchId", sub.workbenches.join(QStringLiteral(",")));
        subMenu->setProperty("checkable", sub.checkable);
        subMenu->setProperty("checked", sub.checked);
        for (const auto& subItem : sub.items)
        {
            buildMenuItem(subMenu, subItem);
        }

        // 声明了 dynamicSections 的子菜单：条目按运行时数据生成（如 File ▸ Recent Files）。
        //
        // 这里必须挂 aboutToShow —— 主菜单只在工作台重建时构建一次，而最近文件之类的
        // 运行时数据随时在变；右键菜单不需要这一步，因为每次右键都整块重建。
        // 重填只丢弃静态条目之后的部分，静态条目及其命令绑定/快捷键台账保持不变。
        //
        // 约定：filler 必须新建归本菜单所有的 QAction。共享 QAction（如命令中枢托管的
        // 那批）不能进动态段，否则重填会把别人还在用的对象删掉 —— 下面按 parent 判定，
        // 非本菜单所有的只摘不删。
        if (!sub.dynamicSections.isEmpty())
        {
            const int staticCount = subMenu->actions().size();
            const QStringList sectionIds = sub.dynamicSections;
            const QString ownerId = sub.id;
            auto refill = [subMenu, staticCount, sectionIds, ownerId]() {
                const QList<QAction*> actions = subMenu->actions();
                for (int i = actions.size() - 1; i >= staticCount; --i)
                {
                    QAction* stale = actions.at(i);
                    subMenu->removeAction(stale);
                    if (stale->parent() == subMenu)
                    {
                        stale->deleteLater();
                    }
                }
                UiContextMenuService::instance().fillDynamicSections(subMenu, sectionIds, ownerId);
            };
            // 先填一次：不打开菜单也能通过 actions() 看到内容，契约测试与启用态刷新都依赖这一点
            refill();
            QObject::connect(subMenu, &QMenu::aboutToShow, subMenu, refill);
        }
        return;
    }

    const MenuActionDef& actionDef = std::get<MenuActionDef>(item);
    if (!actionDef.visible)
    {
        SY_DEBUGF("[UiLayoutBuilder] Skip action id='%s' visible=0 command='%s'",
            qPrintable(actionDef.id),
            qPrintable(actionDef.commandId));
        return;
    }
    // 授权门控（P0-3）：未授权功能不创建入口，客户看不到未购买的功能
    if (!featureAllowed(actionDef.feature))
    {
        SY_DEBUGF("[UiLayoutBuilder] Skip action id='%s' command='%s' — feature '%s' not licensed",
            qPrintable(actionDef.id),
            qPrintable(actionDef.commandId),
            qPrintable(actionDef.feature));
        return;
    }
    QAction* action = parent->addAction(actionLabel(actionDef.label, actionDef.id));
    if (!action)
    {
        return;
    }
    action->setObjectName(actionDef.id);
    action->setCheckable(actionDef.checkable);
    action->setChecked(actionDef.checked);
    // 配置里写的键是默认值；用户覆盖（如果有）在此叠加，台账没挂上时就等于配置值。
    const QKeySequence configuredKey =
        actionDef.shortcut.isEmpty() ? QKeySequence() : QKeySequence(actionDef.shortcut);
    const QKeySequence keySequence = m_shortcutRegistry
        ? m_shortcutRegistry->effectiveKey(actionDef.commandId, configuredKey)
        : configuredKey;
    if (!keySequence.isEmpty())
    {
        action->setShortcut(keySequence);
        // 菜单项快捷键即全局快捷键（单一来源）：默认的 WindowShortcut 只在菜单所属窗口激活时生效，
        // 浮动出去的 Dock 是独立顶层窗口，按键就不响应了。历史上这个作用域由配置 shortcuts 节
        // 另建的 ApplicationShortcut QShortcut 兜住，那批重复定义已移除，作用域必须在此补齐。
        action->setShortcutContext(Qt::ApplicationShortcut);
        m_menuShortcutKeys.insert(keySequence.toString());
    }
    bindAction(
        action, actionDef.commandId, actionDef.label, actionDef.iconName, actionDef.workbenches.join(QStringLiteral(",")));

    if (m_shortcutRegistry)
    {
        // 没配快捷键的动作也登记：设置页要能给它分配一个。
        m_shortcutRegistry->recordAction(action, actionDef.commandId, actionDef.id, actionDef.label, configuredKey);
    }
}

void UiLayoutBuilder::buildToolBars(const std::vector<ToolBarDef>& toolBars)
{
    if (!m_window)
    {
        return;
    }

    for (const auto& tb : toolBars)
    {
        if (tb.id.isEmpty())
        {
            continue;
        }
        // 整条工具栏级授权门控：未授权则整条不创建
        if (!featureAllowed(tb.feature))
        {
            SY_DEBUGF("[UiLayoutBuilder] Skip toolbar id='%s' — feature '%s' not licensed",
                qPrintable(tb.id),
                qPrintable(tb.feature));
            continue;
        }

        QToolBar* toolBar = new QToolBar(actionLabel(tb.title, tb.id), m_window);
        toolBar->setObjectName(tb.id);
        toolBar->setMovable(true);
        toolBar->setProperty("workbenchId", tb.workbenchId);
        m_window->addToolBar(toToolBarArea(tb.position), toolBar);
        m_builtToolBars.push_back(toolBar);

        for (const auto& item : tb.items)
        {
            if (std::holds_alternative<MenuItemType>(item))
            {
                if (std::get<MenuItemType>(item) == MenuItemType::Separator)
                {
                    toolBar->addSeparator();
                }
                continue;
            }

            const ToolBarActionDef& actionDef = std::get<ToolBarActionDef>(item);
            // 单个按钮级授权门控
            if (!featureAllowed(actionDef.feature))
            {
                SY_DEBUGF("[UiLayoutBuilder] Skip toolbar action id='%s' — feature '%s' not licensed",
                    qPrintable(actionDef.id),
                    qPrintable(actionDef.feature));
                continue;
            }
            QAction* action = toolBar->addAction(actionLabel(actionDef.label, actionDef.id));
            if (!action)
            {
                continue;
            }
            action->setObjectName(actionDef.id);
            action->setCheckable(actionDef.checkable);
            if (!actionDef.shortcut.isEmpty())
            {
                action->setShortcut(QKeySequence(actionDef.shortcut));
            }
            if (!actionDef.iconName.isEmpty())
            {
                action->setIcon(QIcon(actionDef.iconName));
            }
            bindAction(action, actionDef.commandId, actionDef.label, actionDef.iconName, tb.workbenchId);
        }

        SY_DEBUGF("[UiLayoutBuilder] ToolBar built id='%s' title='%s' position='%s' items=%d",
            qPrintable(tb.id),
            qPrintable(actionLabel(tb.title, tb.id)),
            qPrintable(tb.workbenchId),
            static_cast<int>(toolBar->actions().size()));
    }

    SY_DEBUGF("[UiLayoutBuilder] All toolbars built: total=%d", static_cast<int>(toolBars.size()));
}

void UiLayoutBuilder::buildDocks(const std::vector<DockDef>& docks)
{
    if (!m_window)
    {
        return;
    }

    for (const auto& dock : docks)
    {
        if (dock.id.isEmpty())
        {
            continue;
        }

        QWidget* panel = m_panelRegistry ? m_panelRegistry->createPanel(dock.widgetType, m_window) : nullptr;
        if (!panel)
        {
            // 面板工厂未注册 → 占位提示，优雅降级
            panel = new QWidget(m_window);
            SY_WARNF("[UiLayoutBuilder] Panel '%s' not registered, using placeholder", qPrintable(dock.widgetType));
        }

        auto* dockWidget = new QDockWidget(actionLabel(dock.title, dock.id), m_window);
        dockWidget->setObjectName(dock.id);
        dockWidget->setWidget(panel);
        dockWidget->setVisible(dock.visible);
        dockWidget->setProperty("widgetType", dock.widgetType);
        m_window->addDockWidget(toDockArea(dock.position), dockWidget);
        m_builtDocks.push_back(dockWidget);

        const char* dockPos = dock.position == DockPosition::Left     ? "left"
            : dock.position == DockPosition::Top                      ? "top"
            : dock.position == DockPosition::Bottom                   ? "bottom"
                                                                      : "right";
        SY_DEBUGF("[UiLayoutBuilder] Dock built id='%s' title='%s' widget='%s' position='%s' visible=%d",
            qPrintable(dock.id),
            qPrintable(actionLabel(dock.title, dock.id)),
            qPrintable(dock.widgetType),
            dockPos,
            dock.visible ? 1 : 0);
    }

    SY_DEBUGF("[UiLayoutBuilder] All docks built: total=%d", static_cast<int>(docks.size()));
}

void UiLayoutBuilder::buildShortcuts(const std::vector<ShortcutDef>& shortcuts)
{
    if (!m_window)
    {
        return;
    }

    for (const auto& sc : shortcuts)
    {
        if (sc.commandId.isEmpty() || sc.keySequence.isEmpty())
        {
            continue;
        }
        if (!m_dispatcher || !m_dispatcher->isCommandRegistered(sc.commandId))
        {
            continue;
        }

        // 单一来源守卫：菜单项已经声明过这个键序列，就不再另建 QShortcut。
        // 两者并存时同一按键有两个接收者，Qt 判 ambiguous 后两边都不触发——
        // 表现为「配置里明明写了快捷键，按下去没反应」。JSON 里的重复定义已清理，
        // 这里再拦一道，防止后续配置改动重新引入。
        // 依赖 buildShortcuts 在 buildMenus 之后调用（见 WorkbenchMenuManager::rebuildMenusFromConfig）。
        const QKeySequence configuredKey(sc.keySequence);
        const QKeySequence keySequence =
            m_shortcutRegistry ? m_shortcutRegistry->effectiveKey(sc.commandId, configuredKey) : configuredKey;
        if (!keySequence.isEmpty() && m_menuShortcutKeys.contains(keySequence.toString()))
        {
            SY_WARNF("[UiLayoutBuilder] Skip duplicated shortcut '%s' for command '%s' — already bound by a menu item",
                qPrintable(keySequence.toString()),
                qPrintable(sc.commandId));
            continue;
        }

        // QShortcut 由本 builder 拥有：它 parent 到窗口，但**不能**靠窗口回收——
        // 每次 rebuildMenusFromConfig 都会按当前工作台的命令目录重新过滤一遍
        // shortcuts，上一批必须先消失。否则：
        //  - 两个工作台都有的键（Ctrl+Z 等）会累积同键 QShortcut，Qt 判 ambiguous
        //    后两个都不触发，表现为「切一次工作台快捷键就失效」；
        //  - 只有 2D 有的键（Ctrl+N / Ctrl+F 等）在 3D 下仍由上一批响应。
        auto* shortcut = new QShortcut(keySequence, m_window);
        shortcut->setContext(Qt::ApplicationShortcut);
        // 同 bindAction：捕获 dispatcher 而不是 this，本 builder 会被整体替换
        QObject::connect(shortcut, &QShortcut::activated,
            [dispatcher = m_dispatcher, commandId = sc.commandId]() {
                SY_DEBUGF("[Menu] shortcut command='%s'", qPrintable(commandId));
                dispatcher->dispatch(commandId);
            });
        m_builtShortcuts.push_back(shortcut);
        if (m_shortcutRegistry)
        {
            m_shortcutRegistry->recordShortcut(shortcut, sc.commandId, configuredKey);
        }
    }
}

void UiLayoutBuilder::buildStatusBar(const StatusBarDef& statusBarDef)
{
    if (!m_window)
    {
        return;
    }

    // QMainWindow::statusBar() 首次调用即创建容器；这里始终取到同一个实例，
    // 工作台切换时只重建槽位，不销毁容器（避免状态栏抖动）。
    QStatusBar* bar = m_window->statusBar();
    if (!bar)
    {
        SY_ERROR("[UiLayoutBuilder] statusBar() returned null, cannot build status bar slots");
        return;
    }

    bar->setVisible(statusBarDef.visible);
    bar->setSizeGripEnabled(statusBarDef.sizeGripEnabled);
    if (!statusBarDef.visible)
    {
        SY_DEBUG("[UiLayoutBuilder] StatusBar hidden by client config");
        return;
    }

    int built = 0;
    for (const auto& slotDef : statusBarDef.items)
    {
        if (!slotDef.visible)
        {
            SY_DEBUGF("[UiLayoutBuilder] Skip status slot id='%s' visible=0", qPrintable(slotDef.id));
            continue;
        }
        // 授权门控（P0-3）：例如「视觉定位坐标显示」这类选装功能的状态栏指示器
        if (!featureAllowed(slotDef.feature))
        {
            SY_DEBUGF("[UiLayoutBuilder] Skip status slot id='%s' — feature '%s' not licensed",
                qPrintable(slotDef.id),
                qPrintable(slotDef.feature));
            continue;
        }

        QWidget* widget = m_panelRegistry ? m_panelRegistry->createPanel(slotDef.widgetType, bar) : nullptr;
        if (!widget)
        {
            // 与 Dock 一致的优雅降级：工厂未注册时不创建占位控件，
            // 因为状态栏空间有限，空白占位比缺失更容易误导现场。
            SY_WARNF("[UiLayoutBuilder] Status slot widget '%s' not registered, slot id='%s' skipped",
                qPrintable(slotDef.widgetType),
                qPrintable(slotDef.id));
            continue;
        }

        widget->setObjectName(slotDef.id);
        if (slotDef.minimumWidth > 0)
        {
            widget->setMinimumWidth(slotDef.minimumWidth);
        }

        if (slotDef.align == StatusBarSlotAlign::Permanent)
        {
            // 永久区：靠右且不被 showMessage 的临时消息覆盖
            bar->addPermanentWidget(widget, slotDef.stretch);
        }
        else
        {
            bar->addWidget(widget, slotDef.stretch);
        }
        m_builtStatusBarSlots.push_back(widget);
        ++built;

        SY_DEBUGF("[UiLayoutBuilder] Status slot built id='%s' widget='%s' align='%s' stretch=%d",
            qPrintable(slotDef.id),
            qPrintable(slotDef.widgetType),
            slotDef.align == StatusBarSlotAlign::Permanent ? "permanent" : "left",
            slotDef.stretch);
    }

    SY_DEBUGF("[UiLayoutBuilder] StatusBar built: slots=%d/%d", built, static_cast<int>(statusBarDef.items.size()));
}

QMenu* UiLayoutBuilder::buildContextMenu(const ContextMenuDef& def, QWidget* parent)
{
    // 整个右键菜单级授权门控：未授权则不弹出
    if (!featureAllowed(def.feature))
    {
        SY_DEBUGF("[UiLayoutBuilder] Context menu id='%s' suppressed — feature '%s' not licensed",
            qPrintable(def.id),
            qPrintable(def.feature));
        return nullptr;
    }

    auto* menu = new QMenu(parent);
    menu->setObjectName(def.id);
    // 条目构建完全复用主菜单路径：命令绑定、图标解析、feature 门控、
    // 「命令未注册则禁用并提示」的行为与顶部菜单逐条一致。
    for (const auto& item : def.items)
    {
        buildMenuItem(menu, item);
    }

    // 只剩分隔符（或全被过滤掉）时不弹出空菜单
    bool hasRealAction = false;
    for (QAction* act : menu->actions())
    {
        if (act && !act->isSeparator())
        {
            hasRealAction = true;
            break;
        }
    }
    if (!hasRealAction)
    {
        SY_DEBUGF("[UiLayoutBuilder] Context menu id='%s' has no available action, not shown", qPrintable(def.id));
        menu->deleteLater();
        return nullptr;
    }

    SY_DEBUGF("[UiLayoutBuilder] Context menu built id='%s' workbench='%s' actions=%d",
        qPrintable(def.id),
        qPrintable(def.workbenchId),
        static_cast<int>(menu->actions().size()));
    return menu;
}