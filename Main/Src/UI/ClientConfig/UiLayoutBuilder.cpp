#include "UiLayoutBuilder.h"
#include "UiPanelRegistry.h"

#include "Log/SyLogger.h"

#include <QAction>
#include <QCoreApplication>
#include <QDockWidget>
#include <QIcon>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QMenuBar>
#include <QShortcut>
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
            return QCoreApplication::translate("UiLayoutBuilder", label.toUtf8().constData());
        }
        return id;
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
    if (!iconResource.isEmpty())
    {
        action->setIcon(QIcon(iconResource));
    }
    if (!workbenchId.isEmpty())
    {
        action->setProperty("workbenchId", workbenchId);
    }
    action->setProperty("commandId", commandId);

    if (m_dispatcher && m_dispatcher->isCommandRegistered(commandId))
    {
        QObject::connect(action, &QAction::triggered, [this, action, commandId](bool) {
            SY_INFOF("[Menu] trigger text='%s' command='%s'", qPrintable(action->text()), qPrintable(commandId));
            m_dispatcher->dispatch(commandId);
        });
    }
    else
    {
        // 命令未注册 → 按钮禁用 + 提示
        action->setEnabled(false);
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

        SY_INFOF("[UiLayoutBuilder] Menu built id='%s' workbench='%s' items=%d",
            qPrintable(menu.id),
            qPrintable(menu.workbenches.join(QStringLiteral(","))),
            qMenu->actions().size());
    }
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
    }
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
    }
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