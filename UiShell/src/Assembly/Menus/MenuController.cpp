#include "Assembly/Menus/MenuController.h"

#include <QAction>
#include <QActionGroup>
#include <QCoreApplication>
#include <QHash>
#include <QIcon>
#include <QLocale>
#include <QMenu>
#include <QMenuBar>
#include <QStringList>
#include <QtGlobal>
#include <functional>
#include <utility>

#include "Assembly/Commands/CommandActions.h"
#include "Assembly/Host/ThemedIcon.h"
#include "Model/Assembly/AssemblyInfo.h"
#include "UiShell/Assembly/ShellServices.h"
#include "UiShell/Contract/Command.h"
#include "UiShell/Contract/HostContext.h"
#include "UiShell/i18n/ITranslationService.h"

namespace AA
{
    MenuController::MenuController(const ShellServices* services, CommandActions& actions,
        CBFunc languageHandler, CBFunc themeHandler)
        : m_shellServices(services), m_actions(actions),
        m_cbLanguage(std::move(languageHandler)), m_cbTheme(std::move(themeHandler))
    {
    }

    QMenuBar* MenuController::build(const AssemblyInfo& info, QWidget* parent)
    {
        if (info.menus.isEmpty() || !m_shellServices)
            return nullptr;

        QMenuBar* menuBar = buildMenuBar(info.menus, parent, info.bNativeMenuBar);
        if (!menuBar)
            return nullptr;

        bindActions(menuBar);

        return menuBar;
    }

    QMenuBar* MenuController::buildMenuBar(const QList<MenuNodeInfo>& menus, QWidget* parent, bool nativeMenuBar) const
    {
        auto* menuBar = new QMenuBar(parent);
        menuBar->setObjectName(QStringLiteral("msui.menubar"));
        menuBar->setNativeMenuBar(nativeMenuBar);

        for (const MenuNodeInfo& menuInfo : menus)
        {
            if (menuInfo.type == MenuNodeType::Submenu)
            {
                const QString title = !menuInfo.strTitle.isEmpty()
                    ? menuInfo.strTitle
                    : (menuInfo.strTitleKey.isEmpty() ? menuInfo.strId : menuInfo.strTitleKey);
                QMenu* menu = menuBar->addMenu(title);
                menu->setObjectName(QStringLiteral("msui.menu.") + menuInfo.strId);

                // 顶层菜单图标
                if (!menuInfo.strIconKey.isEmpty())
                    menu->menuAction()->setIcon(AA::themedIcon(menuInfo.strIconKey));

                for (const MenuNodeInfo& child : menuInfo.children)
                    appendNode(menu, child);
            }
        }

        return menuBar;
    }

    void MenuController::appendNode(QMenu* parentMenu, const MenuNodeInfo& info) const
    {
        switch (info.type)
        {
        case MenuNodeType::Separator:
            parentMenu->addSeparator();
            break;

        case MenuNodeType::Command:
        {
            QAction* action = m_actions.action(info.strCmdId);
            if (!action)
                break;

            // profile 也可以把命令项标成可勾选 / 放进互斥组（groupKey）。
            if (info.bCheckable)
                action->setCheckable(true);
            if (!info.strGroupKey.isEmpty())
                m_actions.addToGroup(action, info.strGroupKey);

            parentMenu->addAction(action);
        }
        break;

        case MenuNodeType::Builtin:
            if (info.strId == QLatin1String(BuiltinMenuId::Language))
                appendLanguageMenu(parentMenu, info.strIconKey);
            else if (info.strId == QLatin1String(BuiltinMenuId::Theme))
                appendThemeMenu(parentMenu, info.strIconKey);
            break;

        case MenuNodeType::Submenu:
        {
            const QString title =
                !info.strTitle.isEmpty() ? info.strTitle : (info.strTitleKey.isEmpty() ? info.strId : info.strTitleKey);
            QMenu* sub = parentMenu->addMenu(title);
            sub->setObjectName(QStringLiteral("msui.menu.") + info.strId);

            if (!info.strIconKey.isEmpty())
                sub->menuAction()->setIcon(AA::themedIcon(info.strIconKey));

            for (const MenuNodeInfo& child : info.children)
                appendNode(sub, child);
        }
        break;
        }
    }

    QMenu* MenuController::buildContextMenu(const QList<MenuNodeInfo>& items, QWidget* parent) const
    {
        if (items.isEmpty())
            return nullptr;

        auto* menu = new QMenu(parent);
        menu->setObjectName(QStringLiteral("msui.contextMenu"));
        for (const MenuNodeInfo& info : items)
            appendNode(menu, info);
        return menu;
    }

    bool MenuController::hasRegisteredCommand(const QString& commandId) const
    {
        return m_shellServices && m_shellServices->cmdCatalog
            && m_shellServices->cmdCatalog->find(commandId) != nullptr;
    }

    void MenuController::appendLanguageMenu(QMenu* parent, const QString& iconKey) const
    {
        ITranslationService* translations = m_shellServices ? m_shellServices->transService : nullptr;
        if (!translations)
            return;

        const QStringList languages = translations->builtinLanguages();
        if (languages.isEmpty())
            return;

        QMenu* langMenu = parent->addMenu(QCoreApplication::translate("LanguageMenu", "Language"));
        langMenu->setObjectName(QStringLiteral("msui.menu.language"));
        if (!iconKey.isEmpty())
            langMenu->menuAction()->setIcon(AA::themedIcon(iconKey));

        auto* group = new QActionGroup(langMenu);
        group->setExclusive(true);
        const QString current = translations->language();

        for (const QString& langId : languages)
        {
            QString label = QLocale(langId).nativeLanguageName();
            if (label.isEmpty())
                label = langId;

            // 目录里登记了 language.<id> 时复用共享动作：执行与状态同步都走命令总线。
            // 没有命令目录时 CommandActions::action() 也会建动作，但它不会接到 invoke()，
            // 因此这里必须先查目录，否则内置语言菜单会静默失效。
            const QString commandId = QStringLiteral("language.") + langId;
            QAction* action = hasRegisteredCommand(commandId) ? m_actions.action(commandId) : nullptr;
            if (action)
            {
                action->setText(label);
                action->setCheckable(true);
                // 勾选态必须在这里初始化：共享动作由 CommandActions 持有，
                // refresh() 只保留 isChecked()，不会替我们推断当前语言。
                action->setChecked(langId == current);
                group->addAction(action);
                langMenu->addAction(action);
                continue;
            }

            // 兼容旧 profile：未登记为命令时保留内置直连动作。
            action = langMenu->addAction(label);
            action->setObjectName(QStringLiteral("msui.action.language.") + langId);
            action->setCheckable(true);
            action->setChecked(langId == current);
            group->addAction(action);
        }
    }

    void MenuController::appendThemeMenu(QMenu* parent, const QString& iconKey) const
    {
        QMenu* themeMenu = parent->addMenu(QCoreApplication::translate("ThemeMenu", "Theme"));
        themeMenu->setObjectName(QStringLiteral("msui.menu.theme"));
        if (!iconKey.isEmpty())
            themeMenu->menuAction()->setIcon(AA::themedIcon(iconKey));

        auto* group = new QActionGroup(themeMenu);
        group->setExclusive(true);
        const QString current = (m_shellServices && !m_shellServices->strThemeId.isEmpty())
            ? m_shellServices->strThemeId
            : QString::fromLatin1(ThemeId::Light);

        for (const ThemeCatalog::Entry& entry : ThemeCatalog::entries())
        {
            const QString label = QCoreApplication::translate("ThemeMenu", entry.label);

            // 与语言菜单一致：登记为命令时只复用共享动作，不再另建 QAction。
            const QString commandId = QStringLiteral("theme.") + entry.strId;
            QAction* action = hasRegisteredCommand(commandId) ? m_actions.action(commandId) : nullptr;
            if (action)
            {
                action->setText(label);
                action->setCheckable(true);
                // 与语言菜单一致：共享动作的勾选态由菜单构建处同步当前皮肤。
                action->setChecked(entry.strId == current);
                group->addAction(action);
                themeMenu->addAction(action);
                continue;
            }

            action = themeMenu->addAction(label);
            action->setObjectName(QStringLiteral("msui.action.theme.") + entry.strId);
            action->setCheckable(true);
            action->setChecked(entry.strId == current);
            group->addAction(action);
        }
    }

    void MenuController::bindActions(QMenuBar* menuBar)
    {
        // 只绑定内置子菜单（语言 / 皮肤）
        const QString languagePrefix = QStringLiteral("msui.action.language.");
        const QString themePrefix = QStringLiteral("msui.action.theme.");

        for (QAction* action : menuBar->actions())
            walkActions(action, languagePrefix, themePrefix);
    }

    void MenuController::walkActions(QAction* action, const QString& strLan, const QString& strTheme)
    {
        if (QMenu* menu = action->menu())
        {
            for (QAction* child : menu->actions())
                walkActions(child, strLan, strTheme);
            return;
        }

        // profile 命令由 CommandActions 的 triggered → ShellRuntime::invoke 统一分发；
        // 这里只接管内置子菜单自己创建的动作，否则 language.* 会被处理两次。
        if (action->property("msui.profileCommand").toBool())
            return;

        const QString name = action->objectName();
        if (name.startsWith(strLan))
        {
            const QString languageId = name.mid(strLan.size());
            QObject::connect(action, &QAction::triggered, action, [handler = m_cbLanguage, languageId]() {
                handler(languageId);
                });
        }
        else if (name.startsWith(strTheme))
        {
            const QString themeId = name.mid(strTheme.size());
            QObject::connect(action, &QAction::triggered, action, [handler = m_cbTheme, themeId]() {
                handler(themeId);
                });
        }
    }
}  // namespace AA