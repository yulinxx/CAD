#pragma once

/**
 * @file UiClientConfigBase.h
 * @brief 客户化 UI 配置数据结构定义
 *
 * 与 Docs/01-当前架构/UI定制变更设计方案.md 第 6.1 节对应。
 * 所有 JSON 配置解析后的 C++ 数据结构在此统一声明，
 * 供 UiConfigLoader 填充、UiLayoutBuilder 消费。
 */

#include <QString>
#include <QStringList>
#include <vector>
#include <variant>

/// 菜单项类型
enum class MenuItemType
{
    Action,     // 动作项（触发命令）
    Separator,  // 分隔符
    SubMenu     // 子菜单
};

/// 菜单动作定义
struct MenuActionDef
{
    QString id;               // 动作唯一标识
    QString label;            // 显示文本
    QString commandId;        // 关联的命令 ID
    QString iconName;         // 图标资源路径（可空）
    QString shortcut;         // 快捷键
    QString feature;          // 可选：需要的许可功能 ID（空表示无许可限制）
    QStringList workbenches;  // 可见工作台列表；为空表示全部可见。
    QString visibilityScope;  // 显式可见域：空=自动，shared=跨工作台共享，2D/3D=仅对应工作台。
    // 3D / 2D 共用项建议显式写入 ["2D", "3D"] 并设置 visibilityScope=shared，避免配置继承后误露出到错误工作台。
    bool visible = true;
    bool checkable = false;
    bool checked = false;
};

/// 子菜单定义
struct SubMenuDef
{
    QString id;
    QString label;
    QString iconName;
    QStringList workbenches;  // 可见工作台列表；为空表示全部可见。
    QString visibilityScope;  // 显式可见域：空=自动，shared=跨工作台共享，2D/3D=仅对应工作台。
    bool visible = true;
    bool checkable = false;
    bool checked = false;
    std::vector<std::variant<MenuActionDef, SubMenuDef, MenuItemType>> items;
};

/// 顶层菜单定义
struct MenuDef
{
    QString id;
    QString label;
    QString iconName;
    QStringList workbenches;  // 可见工作台列表；为空表示全部可见。
    QString visibilityScope;  // 显式可见域：空=自动，shared=跨工作台共享，2D/3D=仅对应工作台。
    // 顶层菜单建议按工作台拆分；如果确实要共用，优先放入 visibilityScope=shared 的公共分组。
    bool visible = true;
    std::vector<std::variant<MenuActionDef, SubMenuDef, MenuItemType>> items;
};

/// 工具栏位置
enum class ToolBarPosition
{
    Top,
    Left,
    Right,
    Bottom
};

/// 工具栏动作定义
struct ToolBarActionDef
{
    QString id;
    QString label;
    QString iconName;
    QString commandId;
    QString shortcut;
    QString feature;
    bool checkable = false;
};

/// 工具栏定义
struct ToolBarDef
{
    QString id;
    QString title;
    ToolBarPosition position;
    QString workbenchId;  // "2D", "3D", "global"
    QString feature;
    std::vector<std::variant<ToolBarActionDef, MenuItemType>> items;
};

/// Dock 面板位置
enum class DockPosition
{
    Left,
    Right,
    Top,
    Bottom
};

/// Dock 面板定义
struct DockDef
{
    QString id;
    QString title;
    DockPosition position;
    QString widgetType;  // 面板类型（由 UiPanelRegistry 解析）
    bool visible = true;
};

/// 快捷键定义
struct ShortcutDef
{
    QString commandId;
    QString keySequence;
};

/// 客户配置元数据
struct UiConfigMeta
{
    QString clientId;
    QString clientName;
    QString version;
};

/// 完整配置数据（所有客户共享的数据结构）
struct UiConfigData
{
    UiConfigMeta meta;
    std::vector<MenuDef> menus;
    std::vector<ToolBarDef> toolBars;
    std::vector<DockDef> docks;
    std::vector<ShortcutDef> shortcuts;
    QString themeStyle;  // 主题标识或 QSS 路径
};
