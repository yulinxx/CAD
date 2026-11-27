#include "Assembly/Menus/MenuPresenter.h"
#include "UiShell/Assembly/IMenuPresenter.h"
#include "UiShell/Contract/HostContext.h"
#include "UiShell/i18n/ITranslationService.h"

#include <QActionGroup>
#include <QCoreApplication>
#include <QHash>
#include <QIcon>
#include <QLocale>
#include <QStringList>
#include <QtGlobal>
#include <functional>

namespace RD
{
    // 默认菜单呈现器
    std::unique_ptr<IMenuPresenter> createMenuPresenter()
    {
        return std::make_unique<MenuPresenter>();
    }

    MenuPresenter::MenuPresenter() = default;
    MenuPresenter::~MenuPresenter() = default;

    QMenuBar* MenuPresenter::buildMenuBar(const QList<MenuNodeInfo>& menus,
        const ICommandCatalog* cmdCatalog,
        const ITextProvider* text,
        const ShellServices& services,
        QWidget* parent) const
    {
        auto* menuBar = new QMenuBar(parent);
        menuBar->setObjectName(QStringLiteral("msui.menubar"));

        FeatureFlagSet flags;
        if (services.featureFlags)
            flags = services.featureFlags->snapshot();

        QHash<QString, QActionGroup*> groupCache;

        // 将 MenuNodeInfo 追加到上一级 QMenu
        std::function<void(QMenu*, const MenuNodeInfo&)> funcAppendNode;
        funcAppendNode = [&](QMenu* parent, const MenuNodeInfo& info) -> void {
            switch (info.type)
            {
            case MenuNodeType::Separator:
                parent->addSeparator();
                break;

            case MenuNodeType::Command:
            {
                const QString title =
                    !info.strTitle.isEmpty() ? info.strTitle : 
                    (info.strTitleKey.isEmpty() ? info.strCmdId : info.strTitleKey);
                
                    QAction* action = parent->addAction(title);
                action->setObjectName(QStringLiteral("msui.action.") + info.strCmdId);

                // 图标
                if (!info.strIconKey.isEmpty())
                    action->setIcon(QIcon::fromTheme(info.strIconKey, QIcon(info.strIconKey)));

                if (info.bCheckable)
                {
                    action->setCheckable(true);
                    if (!info.strGroupKey.isEmpty())
                    {
                        auto it = groupCache.constFind(info.strGroupKey);
                        QActionGroup* group = nullptr;
                        if (it == groupCache.constEnd())
                        {
                            group = new QActionGroup(parent);
                            group->setExclusive(true);
                            groupCache.insert(info.strGroupKey, group);
                        }
                        else
                        {
                            group = it.value();
                        }
                        group->addAction(action);
                    }
                }

                // 功能开关置灰
                if (!info.requiredFeatures.isEmpty())
                {
                    bool allOk = true;
                    for (const QString& req : info.requiredFeatures)
                    {
                        if (!flags.has(req))
                        {
                            allOk = false;
                            break;
                        }
                    }
                    action->setEnabled(allOk);
                }
            }
            break;

            case MenuNodeType::Submenu:
            {
                const QString title =
                    !info.strTitle.isEmpty() ? info.strTitle : 
                    (info.strTitleKey.isEmpty() ? info.strId : info.strTitleKey);

                QMenu* sub = parent->addMenu(title);

                sub->setObjectName(QStringLiteral("msui.menu.") + info.strId);
                if (!info.strIconKey.isEmpty())
                    sub->menuAction()->setIcon(QIcon::fromTheme(info.strIconKey, QIcon(info.strIconKey)));
                
                    for (const MenuNodeInfo& child : info.children)
                    funcAppendNode(sub, child);
            }
            break;

            case MenuNodeType::Builtin:
            {
                // 框架内置子菜单：Language / Theme，按 info.id 生成
                if (info.strId == BuiltinMenuId::Language)
                {
                    const QStringList languages =
                        (services.transService && !services.transService->builtinLanguages().isEmpty())
                        ? services.transService->builtinLanguages()
                        : QStringList{ QStringLiteral("en_US"), QStringLiteral("zh_CN"), QStringLiteral("ja_JP") };

                    QMenu* langMenu =
                        parent->addMenu(QCoreApplication::translate("LanguageMenu", "Language"));
                    langMenu->setObjectName(QStringLiteral("msui.menu.language"));

                    if (!info.strIconKey.isEmpty())
                        langMenu->menuAction()->setIcon(QIcon::fromTheme(info.strIconKey, QIcon(info.strIconKey)));

                    auto* langGroup = new QActionGroup(langMenu);
                    langGroup->setExclusive(true);
                    const QString currentLang =
                        services.transService ? services.transService->language() : QLocale::system().name();

                    for (const QString& langId : languages)
                    {
                        QString label = QLocale(langId).nativeLanguageName();
                        if (label.isEmpty())
                            label = langId;

                        auto* action = langMenu->addAction(label);
                        action->setObjectName(QStringLiteral("msui.action.language.") + langId);
                        action->setCheckable(true);
                        action->setChecked(langId == currentLang);
                        langGroup->addAction(action);
                    }
                }
                else if (info.strId == BuiltinMenuId::Theme)
                {
                    QMenu* themeMenu = parent->addMenu(QCoreApplication::translate("ThemeMenu", "Theme"));
                    themeMenu->setObjectName(QStringLiteral("msui.menu.theme"));
                    if (!info.strIconKey.isEmpty())
                        themeMenu->menuAction()->setIcon(QIcon::fromTheme(info.strIconKey, QIcon(info.strIconKey)));

                    auto* themeGroup = new QActionGroup(themeMenu);
                    themeGroup->setExclusive(true);
                    const QString currentTheme =
                        services.strThemeId.isEmpty() ? QStringLiteral("light") : services.strThemeId;

                    for (const ThemeCatalog::Entry& e : ThemeCatalog::entries())
                    {
                        auto* action = themeMenu->addAction(
                            QCoreApplication::translate("ThemeMenu", e.label));
                        action->setObjectName(QStringLiteral("msui.action.theme.") + e.strId);
                        action->setCheckable(true);
                        action->setChecked(e.strId == currentTheme);
                        themeGroup->addAction(action);
                    }
                }
                // 未知 builtin strId 静默跳过
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

                if (!menuInfo.strIconKey.isEmpty())
                    menu->menuAction()->setIcon(QIcon::fromTheme(menuInfo.strIconKey, QIcon(menuInfo.strIconKey)));
                
                for (const MenuNodeInfo& child : menuInfo.children)
                    funcAppendNode(menu, child);

                // 内置 Help 扩展：profile 没声明 language/theme 子菜单时动态追加；
                // 语言列表优先取 transService->builtinLanguages()，避免硬编码。
                if (menuInfo.strId == QLatin1String("help"))
                {
                    const bool hasLangSub = [&]() {
                        for (QAction* a : menu->actions())
                        {
                            if (a->menu() && a->menu()->objectName() == QLatin1String("msui.menu.language"))
                                return true;
                        }

                        return false;
                        }();
                        
                    const bool hasThemeSub = [&]() {
                        for (QAction* a : menu->actions())
                        {
                            if (a->menu() && a->menu()->objectName() == QLatin1String("msui.menu.theme"))
                                return true;
                        }
                        return false;
                    }();

                    if (!menu->actions().isEmpty() && (!hasLangSub || !hasThemeSub))
                        menu->addSeparator();

                    if (!hasLangSub)
                    {
                        const QStringList languages =
                            (services.transService && !services.transService->builtinLanguages().isEmpty())
                            ? services.transService->builtinLanguages()
                            : QStringList{ QStringLiteral("en_US"), QStringLiteral("zh_CN"), QStringLiteral("ja_JP") };

                        QMenu* langMenu = menu->addMenu(QCoreApplication::translate("LanguageMenu", "Language"));
                        langMenu->setObjectName(QStringLiteral("msui.menu.language"));

                        auto* langGroup = new QActionGroup(langMenu);
                        langGroup->setExclusive(true);
                        const QString currentLang =
                            services.transService ? services.transService->language() : QLocale::system().name();

                        for (const QString& langId : languages)
                        {
                            QString label = QLocale(langId).nativeLanguageName();
                            if (label.isEmpty())
                                label = langId;
                            
                            auto* action = langMenu->addAction(label);
                            action->setObjectName(QStringLiteral("msui.action.language.") + langId);
                            action->setCheckable(true);
                            action->setChecked(langId == currentLang);
                            langGroup->addAction(action);
                        }
                    }

                    if (!hasThemeSub)
                    {
                        QMenu* themeMenu = menu->addMenu(QCoreApplication::translate("ThemeMenu", "Theme"));
                        themeMenu->setObjectName(QStringLiteral("msui.menu.theme"));

                        auto* themeGroup = new QActionGroup(themeMenu);
                        themeGroup->setExclusive(true);
                        const QString currentTheme = services.strThemeId.isEmpty() ? QStringLiteral("light") : services.strThemeId;

                        for (const ThemeCatalog::Entry& e : ThemeCatalog::entries())
                        {
                            auto* action = themeMenu->addAction(
                                QCoreApplication::translate("ThemeMenu", e.label));
                            action->setObjectName(QStringLiteral("msui.action.theme.") + e.strId);
                            action->setCheckable(true);
                            action->setChecked(e.strId == currentTheme);
                            themeGroup->addAction(action);
                        }
                    }
                }
            }
        }

        return menuBar;
    }
}  // namespace RD