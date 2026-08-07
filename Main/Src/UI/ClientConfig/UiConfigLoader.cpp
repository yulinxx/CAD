#include "UiConfigLoader.h"

#include "Log/SyLogger.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>

#include <algorithm>

namespace
{
    ToolBarPosition parseToolBarPosition(const QString& value)
    {
        if (value.compare(QStringLiteral("left"), Qt::CaseInsensitive) == 0)
            return ToolBarPosition::Left;
        if (value.compare(QStringLiteral("right"), Qt::CaseInsensitive) == 0)
            return ToolBarPosition::Right;
        if (value.compare(QStringLiteral("bottom"), Qt::CaseInsensitive) == 0)
            return ToolBarPosition::Bottom;
        return ToolBarPosition::Top;
    }

    DockPosition parseDockPosition(const QString& value)
    {
        if (value.compare(QStringLiteral("left"), Qt::CaseInsensitive) == 0)
            return DockPosition::Left;
        if (value.compare(QStringLiteral("top"), Qt::CaseInsensitive) == 0)
            return DockPosition::Top;
        if (value.compare(QStringLiteral("bottom"), Qt::CaseInsensitive) == 0)
            return DockPosition::Bottom;
        return DockPosition::Right;
    }
}

UiConfigLoader::UiConfigLoader(const QString& resourcePath)
    : m_resourcePath(resourcePath)
{
}

QString UiConfigLoader::lastError() const
{
    return m_lastError;
}

QByteArray UiConfigLoader::readConfigFile(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}

std::optional<UiConfigData> UiConfigLoader::load()
{
    m_lastError.clear();
    return loadWithInheritance(m_resourcePath);
}

std::optional<UiConfigData> UiConfigLoader::loadWithInheritance(const QString& path)
{
    const QByteArray raw = readConfigFile(path);
    if (raw.isEmpty())
    {
        m_lastError = QStringLiteral("Failed to read config file: %1").arg(path);
        SY_ERRORF("[UiConfigLoader] %s", qPrintable(m_lastError));
        return std::nullopt;
    }

    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(raw, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject())
    {
        m_lastError = QStringLiteral("Invalid JSON in %1: %2")
                          .arg(path, parseError.errorString());
        SY_ERRORF("[UiConfigLoader] %s", qPrintable(m_lastError));
        return std::nullopt;
    }

    const QJsonObject root = doc.object();

    // 先解析当前配置
    auto config = parseConfig(doc);
    if (!config)
        return std::nullopt;

    // 处理 extends 继承：父配置在前，子配置覆盖/追加
    const QString extends = root.value(QStringLiteral("extends")).toString();
    if (!extends.isEmpty())
    {
        // 根据当前路径推导父配置的完整路径
        QString parentPath = extends;
        if (!parentPath.startsWith(QStringLiteral(":/")) && !QFileInfo::exists(parentPath))
        {
            const int slash = path.lastIndexOf(QChar('/'));
            const QString dir = slash >= 0 ? path.left(slash + 1) : QStringLiteral(":/configs/");
            parentPath = dir + extends;
        }

        auto parent = loadWithInheritance(parentPath);
        if (!parent)
        {
            m_lastError = QStringLiteral("Failed to load parent config '%1' referenced by %2")
                              .arg(extends, path);
            SY_ERRORF("[UiConfigLoader] %s", qPrintable(m_lastError));
            return std::nullopt;
        }

        mergeConfig(*parent, *config);
        // 子配置的 meta 覆盖父配置（客户 ID 以子配置为准）
        if (!config->meta.clientId.isEmpty())
            parent->meta.clientId = config->meta.clientId;
        if (!config->meta.clientName.isEmpty())
            parent->meta.clientName = config->meta.clientName;
        if (!config->meta.version.isEmpty())
            parent->meta.version = config->meta.version;
        return parent;
    }

    return config;
}

void UiConfigLoader::mergeConfig(UiConfigData& base, const UiConfigData& override)
{
    // 菜单合并：同 id 覆盖，新 id 追加
    for (const auto& menu : override.menus)
    {
        auto it = std::find_if(base.menus.begin(), base.menus.end(),
            [&menu](const MenuDef& m) { return m.id == menu.id; });
        if (it != base.menus.end())
            *it = menu;
        else
            base.menus.push_back(menu);
    }

    // 工具栏合并
    for (const auto& tb : override.toolBars)
    {
        auto it = std::find_if(base.toolBars.begin(), base.toolBars.end(),
            [&tb](const ToolBarDef& t) { return t.id == tb.id; });
        if (it != base.toolBars.end())
            *it = tb;
        else
            base.toolBars.push_back(tb);
    }

    // Dock 合并
    for (const auto& dock : override.docks)
    {
        auto it = std::find_if(base.docks.begin(), base.docks.end(),
            [&dock](const DockDef& d) { return d.id == dock.id; });
        if (it != base.docks.end())
            *it = dock;
        else
            base.docks.push_back(dock);
    }

    // 快捷键合并（按 commandId 覆盖）
    for (const auto& sc : override.shortcuts)
    {
        auto it = std::find_if(base.shortcuts.begin(), base.shortcuts.end(),
            [&sc](const ShortcutDef& s) { return s.commandId == sc.commandId; });
        if (it != base.shortcuts.end())
            *it = sc;
        else
            base.shortcuts.push_back(sc);
    }

    if (!override.themeStyle.isEmpty())
        base.themeStyle = override.themeStyle;
}

std::optional<UiConfigData> UiConfigLoader::parseConfig(const QJsonDocument& doc)
{
    const QJsonObject root = doc.object();

    UiConfigData data;

    // meta
    const QJsonObject meta = root.value(QStringLiteral("meta")).toObject();
    data.meta.clientId = meta.value(QStringLiteral("clientId")).toString();
    data.meta.clientName = meta.value(QStringLiteral("clientName")).toString();
    data.meta.version = meta.value(QStringLiteral("version")).toString();
    data.themeStyle = root.value(QStringLiteral("themeStyle")).toString();

    // menus
    const QJsonArray menus = root.value(QStringLiteral("menus")).toArray();
    for (const auto& value : menus)
    {
        if (!value.isObject())
            continue;
        auto menu = parseMenu(value.toObject());
        if (menu)
            data.menus.push_back(std::move(*menu));
        else
            m_lastError = QStringLiteral("Invalid menu definition");
    }

    // toolbars
    const QJsonArray toolbars = root.value(QStringLiteral("toolbars")).toArray();
    for (const auto& value : toolbars)
    {
        if (!value.isObject())
            continue;
        auto tb = parseToolBar(value.toObject());
        if (tb)
            data.toolBars.push_back(std::move(*tb));
    }

    // docks
    const QJsonArray docks = root.value(QStringLiteral("docks")).toArray();
    for (const auto& value : docks)
    {
        if (!value.isObject())
            continue;
        auto dock = parseDock(value.toObject());
        if (dock)
            data.docks.push_back(std::move(*dock));
    }

    // shortcuts
    const QJsonArray shortcuts = root.value(QStringLiteral("shortcuts")).toArray();
    for (const auto& value : shortcuts)
    {
        if (!value.isObject())
            continue;
        auto sc = parseShortcut(value.toObject());
        if (sc)
            data.shortcuts.push_back(std::move(*sc));
    }

    return data;
}

std::optional<MenuDef> UiConfigLoader::parseMenu(const QJsonObject& obj)
{
    MenuDef menu;
    menu.id = obj.value(QStringLiteral("id")).toString();
    menu.label = obj.value(QStringLiteral("label")).toString();
    if (menu.id.isEmpty())
        return std::nullopt;

    if (!parseMenuItems(obj.value(QStringLiteral("items")).toArray(), menu.items))
        return std::nullopt;
    return menu;
}

std::optional<MenuActionDef> UiConfigLoader::parseMenuAction(const QJsonObject& obj)
{
    MenuActionDef action;
    action.id = obj.value(QStringLiteral("id")).toString();
    action.label = obj.value(QStringLiteral("label")).toString();
    action.commandId = obj.value(QStringLiteral("command")).toString();
    action.shortcut = obj.value(QStringLiteral("shortcut")).toString();
    action.feature = obj.value(QStringLiteral("feature")).toString();
    action.checkable = obj.value(QStringLiteral("checkable")).toBool(false);
    if (action.commandId.isEmpty())
        return std::nullopt;
    return action;
}

std::optional<SubMenuDef> UiConfigLoader::parseSubMenu(const QJsonObject& obj)
{
    SubMenuDef sub;
    sub.id = obj.value(QStringLiteral("id")).toString();
    sub.label = obj.value(QStringLiteral("label")).toString();
    if (sub.id.isEmpty())
        return std::nullopt;

    if (!parseMenuItems(obj.value(QStringLiteral("items")).toArray(), sub.items))
        return std::nullopt;
    return sub;
}

bool UiConfigLoader::parseMenuItems(const QJsonArray& array,
    std::vector<std::variant<MenuActionDef, SubMenuDef, MenuItemType>>& out)
{
    for (const auto& value : array)
    {
        if (!value.isObject())
        {
            // 纯字符串或非对象项：仅识别 "separator" 文本
            if (value.isString()
                && value.toString().compare(QStringLiteral("separator"), Qt::CaseInsensitive) == 0)
            {
                out.push_back(MenuItemType::Separator);
            }
            continue;
        }

        const QJsonObject item = value.toObject();
        const QString type = item.value(QStringLiteral("type")).toString().toLower();

        if (type == QStringLiteral("separator"))
        {
            out.push_back(MenuItemType::Separator);
        }
        else if (type == QStringLiteral("submenu"))
        {
            auto sub = parseSubMenu(item);
            if (sub)
                out.push_back(std::move(*sub));
            else
                return false;
        }
        else // action（默认）
        {
            auto action = parseMenuAction(item);
            if (action)
                out.push_back(std::move(*action));
            else
                return false;
        }
    }
    return true;
}

std::optional<ToolBarDef> UiConfigLoader::parseToolBar(const QJsonObject& obj)
{
    ToolBarDef tb;
    tb.id = obj.value(QStringLiteral("id")).toString();
    tb.title = obj.value(QStringLiteral("title")).toString();
    tb.position = parseToolBarPosition(obj.value(QStringLiteral("position")).toString());
    tb.workbenchId = obj.value(QStringLiteral("workbench")).toString(QStringLiteral("global"));
    tb.feature = obj.value(QStringLiteral("feature")).toString();
    if (tb.id.isEmpty())
        return std::nullopt;

    const QJsonArray items = obj.value(QStringLiteral("items")).toArray();
    for (const auto& value : items)
    {
        if (!value.isObject())
            continue;
        const QJsonObject item = value.toObject();
        if (item.value(QStringLiteral("type")).toString().toLower() == QStringLiteral("separator"))
        {
            tb.items.push_back(MenuItemType::Separator);
            continue;
        }
        auto action = parseToolBarAction(item);
        if (action)
            tb.items.push_back(std::move(*action));
    }
    return tb;
}

std::optional<ToolBarActionDef> UiConfigLoader::parseToolBarAction(const QJsonObject& obj)
{
    ToolBarActionDef action;
    action.id = obj.value(QStringLiteral("id")).toString();
    action.label = obj.value(QStringLiteral("label")).toString();
    action.iconName = obj.value(QStringLiteral("icon")).toString();
    action.commandId = obj.value(QStringLiteral("command")).toString();
    action.shortcut = obj.value(QStringLiteral("shortcut")).toString();
    action.feature = obj.value(QStringLiteral("feature")).toString();
    action.checkable = obj.value(QStringLiteral("checkable")).toBool(false);
    if (action.commandId.isEmpty())
        return std::nullopt;
    return action;
}

std::optional<DockDef> UiConfigLoader::parseDock(const QJsonObject& obj)
{
    DockDef dock;
    dock.id = obj.value(QStringLiteral("id")).toString();
    dock.title = obj.value(QStringLiteral("title")).toString();
    dock.position = parseDockPosition(obj.value(QStringLiteral("position")).toString());
    dock.widgetType = obj.value(QStringLiteral("widgetType")).toString();
    dock.visible = obj.value(QStringLiteral("visible")).toBool(true);
    if (dock.id.isEmpty() || dock.widgetType.isEmpty())
        return std::nullopt;
    return dock;
}

std::optional<ShortcutDef> UiConfigLoader::parseShortcut(const QJsonObject& obj)
{
    ShortcutDef sc;
    sc.commandId = obj.value(QStringLiteral("command")).toString();
    sc.keySequence = obj.value(QStringLiteral("keys")).toString();
    if (sc.commandId.isEmpty() || sc.keySequence.isEmpty())
        return std::nullopt;
    return sc;
}
