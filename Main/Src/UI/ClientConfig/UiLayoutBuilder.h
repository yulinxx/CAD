#pragma once

/**
 * @file UiLayoutBuilder.h
 * @brief 数据驱动的 UI 布局构建器
 *
 * 与 Docs/01-当前架构/UI定制变更设计方案.md 第 6.4 节对应。
 * 根据 UiConfigData 构建实际的 Qt Widgets（菜单/工具栏/Dock/快捷键）。
 * 命令绑定通过 IUiCommandDispatcher 抽象，与具体命令系统解耦。
 */

#include "UiClientConfigBase.h"

#include <vector>

class QAction;
class QMainWindow;
class QMenu;
class QToolBar;
class QWidget;
class UiPanelRegistry;

/// 命令分发器抽象接口
/// 适配现有 CommandCatalog + OperationBus 体系，同时允许单元测试注入假实现
class IUiCommandDispatcher
{
public:
    virtual ~IUiCommandDispatcher() = default;

    /// 命令是否已注册
    virtual bool isCommandRegistered(const QString& commandId) const = 0;

    /// 分发命令
    virtual void dispatch(const QString& commandId) = 0;
};

/// 数据驱动的布局构建器
class UiLayoutBuilder
{
public:
    UiLayoutBuilder(QMainWindow* window,
        IUiCommandDispatcher* dispatcher,
        UiPanelRegistry* panelRegistry);

    void buildMenus(const std::vector<MenuDef>& menus);
    void buildToolBars(const std::vector<ToolBarDef>& toolBars);
    void buildDocks(const std::vector<DockDef>& docks);
    void buildShortcuts(const std::vector<ShortcutDef>& shortcuts);

    /// 本次构建创建的 Dock widget（供上层注册到布局管理器，统一清理）
    const std::vector<QWidget*>& builtDocks() const { return m_builtDocks; }
    /// 本次构建创建的工具栏（供上层注册到布局管理器，统一清理）
    const std::vector<QToolBar*>& builtToolBars() const { return m_builtToolBars; }

    /// 将动作绑定到命令；命令未注册时禁用动作并给出提示
    void bindAction(QAction* action, const QString& commandId);

private:
    void buildMenuItem(QMenu* parent,
        const std::variant<MenuActionDef, SubMenuDef, MenuItemType>& item);

    QMainWindow* m_window;
    IUiCommandDispatcher* m_dispatcher;
    UiPanelRegistry* m_panelRegistry;
    std::vector<QWidget*> m_builtDocks;
    std::vector<QToolBar*> m_builtToolBars;
};
