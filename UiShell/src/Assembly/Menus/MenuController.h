#pragma once

#include <QList>
#include <QString>
#include <functional>

class QAction;
class QMenu;
class QMenuBar;
class QWidget;

namespace AA
{
    class CommandActions;
    struct AssemblyInfo;
    struct MenuNodeInfo;
    struct ShellServices;

    // 菜单控制器
    class MenuController
    {
    public:
        using CBFunc = std::function<void(const QString&)>;

        MenuController(const ShellServices* services,
            CommandActions& actions,
            CBFunc languageHandler,
            CBFunc themeHandler);

    public:
        // 计划为空或构建失败时返回 nullptr
        QMenuBar* build(const AssemblyInfo& info, QWidget* parent);

    private:
        // nativeMenuBar：true 时菜单栏交给系统原生菜单栏
        QMenuBar* buildMenuBar(const QList<MenuNodeInfo>& menus, QWidget* parent, bool nativeMenuBar) const;

        void appendLanguageMenu(QMenu* parent, const QString& iconKey) const;
        void appendThemeMenu(QMenu* parent, const QString& iconKey) const;

        void bindActions(QMenuBar* menuBar);

        void walkActions(QAction* action, const QString& strLan, const QString& strTheme);

    private:
        const ShellServices* m_shellServices = nullptr;
        CommandActions& m_actions;

        CBFunc m_cbLanguage;
        CBFunc m_cbTheme;
    };
}  // namespace AA
