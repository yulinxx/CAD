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
    /// 动态段提供者 ID 列表，按顺序追加到静态条目之后。
    /// 与 ContextMenuDef::dynamicSections 是同一套机制、同一个注册表
    /// （UiContextMenuService 的段注册表），JSON 里的键名也相同 —— 不要另造概念。
    /// 典型用途是「File ▸ Recent Files」：条目按运行时最近文件列表生成。
    /// 注意主菜单只在工作台重建时构建一次，所以声明了本字段的子菜单会在
    /// aboutToShow 时重填（见 UiLayoutBuilder::buildMenuItem）。
    QStringList dynamicSections;
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

/// 状态栏槽位对齐方式
/// Left      —— addWidget，随窗口左侧排列，可被临时消息覆盖
/// Permanent —— addPermanentWidget，固定在右侧，不被 showMessage 覆盖
enum class StatusBarSlotAlign
{
    Left,
    Permanent
};

/// 状态栏槽位定义（P0-2a：状态栏纳入配置驱动）
/// 与 Dock 同构：JSON 只声明 widgetType，实际控件由 UiPanelRegistry 工厂创建，
/// 客户新增状态栏指示器只需注册一个工厂 + 改 JSON，不必改 C++ 布局代码。
struct StatusBarSlotDef
{
    QString id;
    QString widgetType;                                        // 槽位控件类型（UiPanelRegistry 解析）
    StatusBarSlotAlign align{ StatusBarSlotAlign::Left };       // 对齐方式
    int stretch{ 0 };                                          // 伸缩因子（仅 Left 有效）
    int minimumWidth{ 0 };                                     // 最小宽度，0 表示不限制
    QString feature;                                           // 可选：需要的许可功能 ID
    bool visible = true;
};

/// 状态栏定义
struct StatusBarDef
{
    /// JSON 中是否显式声明了 statusBar 节。
    /// 继承合并时用它区分「子配置没写」与「子配置写了个空状态栏」：
    /// 没写 → 沿用父配置；写了空的 → 覆盖为空（客户可借此彻底移除状态栏内容）。
    bool declared = false;
    bool visible = true;
    /// 是否显示 QSizeGrip（右下角缩放手柄）
    bool sizeGripEnabled = true;
    // 注意：成员名不可用 slots —— Qt 把 slots 定义为空宏，会直接破坏结构体声明
    std::vector<StatusBarSlotDef> items;
};

/// 右键菜单定义（P0-2b：右键菜单纳入配置驱动）
/// id 由业务侧在弹出时按上下文选择，例如：
///   "canvas.2d.selection"  —— 2D 画布有选中时
///   "canvas.2d.empty"      —— 2D 画布空白处
///   "canvas.3d.selection"  —— 3D 视口有选中时
/// items 复用菜单的 variant 结构，因此子菜单/分隔符/feature 门控行为与主菜单完全一致。
struct ContextMenuDef
{
    QString id;
    QString workbenchId;  // "2D" / "3D" / "global"
    QString feature;
    std::vector<std::variant<MenuActionDef, SubMenuDef, MenuItemType>> items;
    /// 动态段提供者 ID 列表，按顺序追加到静态条目之后。
    /// 右键菜单里有一部分内容无法静态描述（例如「设为当前图层 / 移动到图层…」
    /// 需要按运行时图层列表生成），这类内容由 C++ 侧注册的提供者填充，
    /// JSON 只声明「在这里插入哪个动态段、以什么顺序」。
    QStringList dynamicSections;
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
    StatusBarDef statusBar;
    std::vector<ContextMenuDef> contextMenus;
    QString themeStyle;  // 主题标识或 QSS 路径
};
