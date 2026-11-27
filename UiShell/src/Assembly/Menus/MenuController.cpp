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
#include "UiShell/Contract/HostContext.h"
#include "UiShell/i18n/ITranslationService.h"

namespace RD
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

        // 将 MenuNodeInfo 追加到上一级 QMenu
        std::function<void(QMenu*, const MenuNodeInfo&)> funcAppendNode;
        funcAppendNode = [&](QMenu* parentMenu, const MenuNodeInfo& info) -> void {
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
                    sub->menuAction()->setIcon(RD::themedIcon(info.strIconKey));

                for (const MenuNodeInfo& child : info.children)
                    funcAppendNode(sub, child);
            }
            break;
            }
            };

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
                    menu->menuAction()->setIcon(RD::themedIcon(menuInfo.strIconKey));

                for (const MenuNodeInfo& child : menuInfo.children)
                    funcAppendNode(menu, child);
            }
        }

        return menuBar;
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
            langMenu->menuAction()->setIcon(RD::themedIcon(iconKey));

        auto* group = new QActionGroup(langMenu);
        group->setExclusive(true);
        const QString current = translations->language();

        for (const QString& langId : languages)
        {
            QString label = QLocale(langId).nativeLanguageName();
            if (label.isEmpty())
                label = langId;
            auto* action = langMenu->addAction(label);
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
            themeMenu->menuAction()->setIcon(RD::themedIcon(iconKey));

        auto* group = new QActionGroup(themeMenu);
        group->setExclusive(true);
        const QString current = (m_shellServices && !m_shellServices->strThemeId.isEmpty())
            ? m_shellServices->strThemeId
            : QString::fromLatin1(ThemeId::Light);

        for (const ThemeCatalog::Entry& entry : ThemeCatalog::entries())
        {
            const QString label = QCoreApplication::translate("ThemeMenu", entry.label);
            auto* action = themeMenu->addAction(label);
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
}  // namespace RD