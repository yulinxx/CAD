#pragma once

/**
 * @file UiConfigLoader.h
 * @brief 客户化 UI 配置加载器
 *
 * 与 Docs/01-当前架构/UI定制变更设计方案.md 第 6.2 节对应。
 * 从 Qt 资源或文件路径加载 JSON 配置，解析为 C++ 数据结构。
 * 支持 "extends" 继承机制（合并父配置）与运行时字段校验。
 */

#include "UiClientConfigBase.h"

#include <QJsonDocument>
#include <QString>
#include <optional>

/// 客户化 UI 配置加载器
class UiConfigLoader
{
public:
    /// 构造函数
    /// @param resourcePath 配置路径（Qt 资源路径如 ":/configs/san_yi.json"，或文件路径）
    explicit UiConfigLoader(const QString& resourcePath);

    /// 加载并解析配置（含 extends 继承合并）
    /// @return 解析结果，失败时返回 std::nullopt
    std::optional<UiConfigData> load();

    /// 获取加载错误信息
    QString lastError() const;

private:
    /// 解析单个 JSON 文档（不含继承）
    std::optional<UiConfigData> parseConfig(const QJsonDocument& doc);

    /// 递归加载并合并配置（支持 extends 链）
    std::optional<UiConfigData> loadWithInheritance(const QString& path);

    /// 将子配置合并到父配置（同 id 覆盖，新 id 追加）
    static void mergeConfig(UiConfigData& base, const UiConfigData& override);

    /// 各个子解析函数
    std::optional<MenuDef> parseMenu(const QJsonObject& obj);
    std::optional<MenuActionDef> parseMenuAction(const QJsonObject& obj);
    std::optional<SubMenuDef> parseSubMenu(const QJsonObject& obj);
    std::optional<ToolBarDef> parseToolBar(const QJsonObject& obj);
    std::optional<ToolBarActionDef> parseToolBarAction(const QJsonObject& obj);
    std::optional<DockDef> parseDock(const QJsonObject& obj);
    std::optional<ShortcutDef> parseShortcut(const QJsonObject& obj);
    /// 解析状态栏节（"statusBar"）
    StatusBarDef parseStatusBar(const QJsonObject& obj);
    /// 解析单个状态栏槽位
    std::optional<StatusBarSlotDef> parseStatusBarSlot(const QJsonObject& obj);
    /// 解析右键菜单节（"contextMenus"）中的单项
    std::optional<ContextMenuDef> parseContextMenu(const QJsonObject& obj);

    /// 菜单可见性判断（按工作台）
    static bool isVisibleForWorkbench(const QStringList& workbenches, const QString& workbenchId);

    /// 解析菜单/子菜单条目列表（variant 类型）
    bool parseMenuItems(
        const QJsonArray& array, std::vector<std::variant<MenuActionDef, SubMenuDef, MenuItemType>>& out);
    /// 解析工具栏条目列表（variant 类型）
    bool parseToolBarItems(const QJsonArray& array, std::vector<std::variant<ToolBarActionDef, MenuItemType>>& out);

    /// 读取配置文件的原始字节（支持 Qt 资源路径与本地文件）
    static QByteArray readConfigFile(const QString& path);

    QString m_resourcePath;
    QString m_lastError;
};
