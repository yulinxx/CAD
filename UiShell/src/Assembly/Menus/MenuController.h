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

        // 右键菜单与菜单栏共用命令动作池：菜单项 / 勾选 / 快捷键 / 文字不会各存一份。
        QMenu* buildContextMenu(const QList<MenuNodeInfo>& items, QWidget* parent) const;

    private:
        // nativeMenuBar：true 时菜单栏交给系统原生菜单栏
        QMenuBar* buildMenuBar(const QList<MenuNodeInfo>& menus, QWidget* parent, bool nativeMenuBar) const;

        void appendNode(QMenu* parentMenu, const MenuNodeInfo& info) const;

        void appendLanguageMenu(QMenu* parent, const QString& iconKey) const;
        void appendThemeMenu(QMenu* parent, const QString& iconKey) const;

        // 仅在命令目录显式登记时才复用共享动作；无目录时保留内置直连回退。
        bool hasRegisteredCommand(const QString& commandId) const;

        void bindActions(QMenuBar* menuBar);

        void walkActions(QAction* action, const QString& strLan, const QString& strTheme);

    private:
        const ShellServices* m_shellServices = nullptr;
        CommandActions& m_actions;

        CBFunc m_cbLanguage;
        CBFunc m_cbTheme;
    };
}  // namespace AA
