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

#include <QSet>
#include <QVariantMap>
#include <vector>

class QAction;
class QMainWindow;
class QMenu;
class QShortcut;
class QStatusBar;
class QToolBar;
class QWidget;
class UiPanelRegistry;
class UiShortcutRegistry;

/// 命令分发器抽象接口
/// 适配现有 CommandCatalog + OperationBus 体系，同时允许单元测试注入假实现
class IUiCommandDispatcher
{
public:
    virtual ~IUiCommandDispatcher() = default;

    /// 命令是否已注册
    virtual bool isCommandRegistered(const QString& commandId) const = 0;

    /// 分发命令（唯一入口）
    /// @param params 调用方给出的参数（如最近文件项的 path），实现负责透传到操作总线；
    ///        实现里的 enrichParams 只能补齐缺失项，不得整体覆盖。
    virtual void dispatch(const QString& commandId, const QVariantMap& params) = 0;

    /// 无参分发的便捷写法。非虚，只是转调上面那一条通路，不构成第二个入口。
    void dispatch(const QString& commandId)
    {
        dispatch(commandId, QVariantMap{});
    }
};

/// 数据驱动的布局构建器
class UiLayoutBuilder
{
public:
    UiLayoutBuilder(QMainWindow* window, IUiCommandDispatcher* dispatcher, UiPanelRegistry* panelRegistry);

    /// 销毁本次构建创建的 QShortcut（见 buildShortcuts 的注释）。
    /// Dock / 工具栏 / 状态栏槽位不在此销毁：那些由上层布局管理器统一回收。
    ~UiLayoutBuilder();

    void buildMenus(const std::vector<MenuDef>& menus);
    void buildToolBars(const std::vector<ToolBarDef>& toolBars);
    void buildDocks(const std::vector<DockDef>& docks);
    void buildShortcuts(const std::vector<ShortcutDef>& shortcuts);

    /// 构建状态栏槽位（P0-2a）
    /// 槽位控件由 UiPanelRegistry 按 widgetType 创建，与 Dock 使用同一套面板工厂。
    /// @param statusBar 状态栏配置
    void buildStatusBar(const StatusBarDef& statusBar);

    /// 按配置构建一个右键菜单（P0-2b）
    /// 调用方负责 popup 与生命周期（通常用 QMenu::exec 后 deleteLater）。
    /// @param def 右键菜单配置
    /// @param parent 菜单父对象
    /// @return 构建好的菜单；无可用条目时返回 nullptr（调用方据此决定不弹出）
    QMenu* buildContextMenu(const ContextMenuDef& def, QWidget* parent);

    void clearBuiltLayout();

    /// 挂入快捷键台账（可为空）。挂上后：配置里的键会先叠加用户覆盖再生效，
    /// 且每个菜单动作/全局快捷键都登记进台账，供设置页的快捷键页编辑。
    /// 必须在 buildMenus/buildShortcuts 之前设置。
    void setShortcutRegistry(UiShortcutRegistry* registry)
    {
        m_shortcutRegistry = registry;
    }


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
    void releaseBuiltShortcuts();

    QMainWindow* m_window;
    IUiCommandDispatcher* m_dispatcher;
    UiPanelRegistry* m_panelRegistry;
    /// 快捷键台账（非拥有；由 WorkbenchMenuManager 持有，寿命长于本 builder）
    UiShortcutRegistry* m_shortcutRegistry{ nullptr };
    std::vector<QWidget*> m_builtDocks;
    std::vector<QToolBar*> m_builtToolBars;
    std::vector<QWidget*> m_builtStatusBarSlots;
    /// 本次构建创建的全局快捷键，由本类拥有并销毁
    std::vector<QShortcut*> m_builtShortcuts;
    /// 本次构建中已被菜单项占用的键序列（QKeySequence::toString() 归一化后的文本）。
    /// buildShortcuts 靠它跳过重复定义，见 buildShortcuts 的注释。
    QSet<QString> m_menuShortcutKeys;
};
