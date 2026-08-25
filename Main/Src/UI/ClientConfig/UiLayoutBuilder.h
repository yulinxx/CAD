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
class QStatusBar;
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
    UiLayoutBuilder(QMainWindow* window, IUiCommandDispatcher* dispatcher, UiPanelRegistry* panelRegistry);

    void buildMenus(const std::vector<MenuDef>& menus);
    void buildToolBars(const std::vector<ToolBarDef>& toolBars);
    void buildDocks(const std::vector<DockDef>& docks);
    void buildShortcuts(const std::vector<ShortcutDef>& shortcuts);

    /// 构建状态栏槽位（P0-2a）
    /// 槽位控件由 UiPanelRegistry 按 widgetType 创建，与 Dock 使用同一套面板工厂。
    /// @param statusBar 状态栏配置
    /// @param workbenchId 当前工作台 ID，用于按 workbenches 字段过滤槽位
    void buildStatusBar(const StatusBarDef& statusBar, const QString& workbenchId);

    /// 按配置构建一个右键菜单（P0-2b）
    /// 调用方负责 popup 与生命周期（通常用 QMenu::exec 后 deleteLater）。
    /// @param def 右键菜单配置
    /// @param parent 菜单父对象
    /// @return 构建好的菜单；无可用条目时返回 nullptr（调用方据此决定不弹出）
    QMenu* buildContextMenu(const ContextMenuDef& def, QWidget* parent);

    void clearBuiltLayout();

    /// 本次构建创建的 Dock widget（供上层注册到布局管理器，统一清理）
    const std::vector<QWidget*>& builtDocks() const
    {
        return m_builtDocks;
    }

    /// 本次构建创建的工具栏（供上层注册到布局管理器，统一清理）
    const std::vector<QToolBar*>& builtToolBars() const
    {
        return m_builtToolBars;
    }

    /// 本次构建挂入状态栏的槽位控件（供上层统一清理）
    const std::vector<QWidget*>& builtStatusBarSlots() const
    {
        return m_builtStatusBarSlots;
    }

    /// 将动作绑定到命令；命令未注册时禁用动作并给出提示
    void bindAction(QAction* action, const QString& commandId);

    /// 带菜单/工作台上下文的动作绑定（用于菜单、子菜单、工具栏统一行为）
    void bindAction(QAction* action,
        const QString& commandId,
        const QString& text,
        const QString& iconResource = QString(),
        const QString& workbenchId = QString());

private:
    void buildMenuItem(QMenu* parent, const std::variant<MenuActionDef, SubMenuDef, MenuItemType>& item);

    QMainWindow* m_window;
    IUiCommandDispatcher* m_dispatcher;
    UiPanelRegistry* m_panelRegistry;
    std::vector<QWidget*> m_builtDocks;
    std::vector<QToolBar*> m_builtToolBars;
    std::vector<QWidget*> m_builtStatusBarSlots;
};
