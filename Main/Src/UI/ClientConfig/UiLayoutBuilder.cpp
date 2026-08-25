#include "UiLayoutBuilder.h"
#include "UiFeatureGate.h"
#include "UiPanelRegistry.h"

#include "UI2D/Operation/CommandCatalog.h"
#include "Log/SyLogger.h"

#include <QAction>
#include <QCoreApplication>
#include <QDockWidget>
#include <QIcon>
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

    /// 工作台可见性判断：列表为空表示全部工作台可见
    bool visibleForWorkbench(const QStringList& workbenches, const QString& workbenchId)
    {
        if (workbenches.isEmpty() || workbenchId.isEmpty())
        {
            return true;
        }
        for (const QString& wb : workbenches)
        {
            if (wb.compare(workbenchId, Qt::CaseInsensitive) == 0)
            {
                return true;
            }
        }
        return false;
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

void UiLayoutBuilder::clearBuiltLayout()
{
    m_builtDocks.clear();
    m_builtToolBars.clear();
    m_builtStatusBarSlots.clear();
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

    if (m_dispatcher && m_dispatcher->isCommandRegistered(commandId))
    {
        QObject::connect(action, &QAction::triggered, [this, action, commandId](bool) {
            SY_INFOF("[Menu] trigger text='%s' command='%s'",
                action->text().toUtf8().constData(),
                commandId.toUtf8().constData());
            m_dispatcher->dispatch(commandId);
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
        SY_INFOF("[UiLayoutBuilder] Menu built id='%s' label='%s' workbench='%s' items=%d enabled=%d/%d",
            qPrintable(menu.id),
            qPrintable(actionLabel(menu.label, menu.id)),
            qPrintable(menu.workbenches.join(QStringLiteral(","))),
            static_cast<int>(menu.items.size()),
            enabledCount,
            qMenu->actions().size());
    }

    SY_INFOF("[UiLayoutBuilder] All menus built: total=%d topLevelMenus=%d",
        static_cast<int>(menus.size()),
        m_window->menuBar() ? m_window->menuBar()->actions().size() : 0);
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
        SY_INFOF("[UiLayoutBuilder] Skip action id='%s' command='%s' — feature '%s' not licensed",
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
    if (!actionDef.shortcut.isEmpty())
    {
        action->setShortcut(QKeySequence(actionDef.shortcut));
    }
    bindAction(
        action, actionDef.commandId, actionDef.label, actionDef.iconName, actionDef.workbenches.join(QStringLiteral(",")));
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
            SY_INFOF("[UiLayoutBuilder] Skip toolbar id='%s' — feature '%s' not licensed",
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
                SY_INFOF("[UiLayoutBuilder] Skip toolbar action id='%s' — feature '%s' not licensed",
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

        SY_INFOF("[UiLayoutBuilder] ToolBar built id='%s' title='%s' position='%s' items=%d",
            qPrintable(tb.id),
            qPrintable(actionLabel(tb.title, tb.id)),
            qPrintable(tb.workbenchId),
            toolBar->actions().size());
    }

    SY_INFOF("[UiLayoutBuilder] All toolbars built: total=%d", static_cast<int>(toolBars.size()));
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
        SY_INFOF("[UiLayoutBuilder] Dock built id='%s' title='%s' widget='%s' position='%s' visible=%d",
            qPrintable(dock.id),
            qPrintable(actionLabel(dock.title, dock.id)),
            qPrintable(dock.widgetType),
            dockPos,
            dock.visible ? 1 : 0);
    }

    SY_INFOF("[UiLayoutBuilder] All docks built: total=%d", static_cast<int>(docks.size()));
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

        auto* shortcut = new QShortcut(QKeySequence(sc.keySequence), m_window);
        shortcut->setContext(Qt::ApplicationShortcut);
        QObject::connect(shortcut, &QShortcut::activated, [this, commandId = sc.commandId]() {
            SY_INFOF("[Menu] shortcut command='%s'", qPrintable(commandId));
            m_dispatcher->dispatch(commandId);
        });
    }
}

void UiLayoutBuilder::buildStatusBar(const StatusBarDef& statusBarDef, const QString& workbenchId)
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
        SY_INFO("[UiLayoutBuilder] StatusBar hidden by client config");
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
        if (!visibleForWorkbench(slotDef.workbenches, workbenchId))
        {
            SY_DEBUGF("[UiLayoutBuilder] Skip status slot id='%s' — not for workbench '%s'",
                qPrintable(slotDef.id),
                qPrintable(workbenchId));
            continue;
        }
        // 授权门控（P0-3）：例如「视觉定位坐标显示」这类选装功能的状态栏指示器
        if (!featureAllowed(slotDef.feature))
        {
            SY_INFOF("[UiLayoutBuilder] Skip status slot id='%s' — feature '%s' not licensed",
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

        SY_INFOF("[UiLayoutBuilder] Status slot built id='%s' widget='%s' align='%s' stretch=%d",
            qPrintable(slotDef.id),
            qPrintable(slotDef.widgetType),
            slotDef.align == StatusBarSlotAlign::Permanent ? "permanent" : "left",
            slotDef.stretch);
    }

    SY_INFOF("[UiLayoutBuilder] StatusBar built: workbench='%s' slots=%d/%d",
        qPrintable(workbenchId),
        built,
        static_cast<int>(statusBarDef.items.size()));
}

QMenu* UiLayoutBuilder::buildContextMenu(const ContextMenuDef& def, QWidget* parent)
{
    // 整个右键菜单级授权门控：未授权则不弹出
    if (!featureAllowed(def.feature))
    {
        SY_INFOF("[UiLayoutBuilder] Context menu id='%s' suppressed — feature '%s' not licensed",
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
        SY_INFOF("[UiLayoutBuilder] Context menu id='%s' has no available action, not shown", qPrintable(def.id));
        menu->deleteLater();
        return nullptr;
    }

    SY_INFOF("[UiLayoutBuilder] Context menu built id='%s' workbench='%s' actions=%d",
        qPrintable(def.id),
        qPrintable(def.workbenchId),
        static_cast<int>(menu->actions().size()));
    return menu;
}