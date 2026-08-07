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
#include <vector>
#include <variant>

/// 菜单项类型
enum class MenuItemType
{
    Action,    // 动作项（触发命令）
    Separator, // 分隔符
    SubMenu    // 子菜单
};

/// 菜单动作定义
struct MenuActionDef
{
    QString id;        // 动作唯一标识
    QString label;     // 显示文本
    QString commandId; // 关联的命令 ID
    QString shortcut;  // 快捷键
    QString feature;   // 可选：需要的许可功能 ID（空表示无许可限制）
    bool checkable = false;
};

/// 子菜单定义
struct SubMenuDef
{
    QString id;
    QString label;
    std::vector<std::variant<MenuActionDef, SubMenuDef, MenuItemType>> items;
};

/// 顶层菜单定义
struct MenuDef
{
    QString id;
    QString label;
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
    QString workbenchId; // "2D", "3D", "global"
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
    QString widgetType; // 面板类型（由 UiPanelRegistry 解析）
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
    QString themeStyle; // 主题标识或 QSS 路径
};
