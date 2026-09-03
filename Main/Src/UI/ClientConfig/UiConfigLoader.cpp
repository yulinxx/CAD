#include "UiConfigLoader.h"

#include "Log/SyLogger.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>
#include <QVariant>

#include <algorithm>

namespace
{
    ToolBarPosition parseToolBarPosition(const QString& value)
    {
        if (value.compare(QStringLiteral("left"), Qt::CaseInsensitive) == 0)
        {
            return ToolBarPosition::Left;
        }
        if (value.compare(QStringLiteral("right"), Qt::CaseInsensitive) == 0)
        {
            return ToolBarPosition::Right;
        }
        if (value.compare(QStringLiteral("bottom"), Qt::CaseInsensitive) == 0)
        {
            return ToolBarPosition::Bottom;
        }
        return ToolBarPosition::Top;
    }

    DockPosition parseDockPosition(const QString& value)
    {
        if (value.compare(QStringLiteral("left"), Qt::CaseInsensitive) == 0)
        {
            return DockPosition::Left;
        }
        if (value.compare(QStringLiteral("top"), Qt::CaseInsensitive) == 0)
        {
            return DockPosition::Top;
        }
        if (value.compare(QStringLiteral("bottom"), Qt::CaseInsensitive) == 0)
        {
            return DockPosition::Bottom;
        }
        return DockPosition::Right;
    }

    QString parseVisibilityScope(const QJsonObject& obj)
    {
        return obj.value(QStringLiteral("visibilityScope")).toString();
    }

    /// 状态栏槽位对齐解析：permanent/right → Permanent，其余 → Left
    StatusBarSlotAlign parseStatusBarAlign(const QString& value)
    {
        if (value.compare(QStringLiteral("permanent"), Qt::CaseInsensitive) == 0 ||
            value.compare(QStringLiteral("right"), Qt::CaseInsensitive) == 0)
        {
            return StatusBarSlotAlign::Permanent;
        }
        return StatusBarSlotAlign::Left;
    }


    QString normalizeVisibilityScope(const QString& explicitScope, const QStringList& workbenches)
    {
        if (!explicitScope.trimmed().isEmpty())
        {
            return explicitScope.trimmed();
        }

        const bool has2D = workbenches.contains(QStringLiteral("2D"), Qt::CaseInsensitive);
        const bool has3D = workbenches.contains(QStringLiteral("3D"), Qt::CaseInsensitive);
        if (has2D && has3D)
        {
            return QStringLiteral("shared");
        }
        if (has2D)
        {
            return QStringLiteral("2D");
        }
        if (has3D)
        {
            return QStringLiteral("3D");
        }
        return QString();
    }
}  // namespace

bool UiConfigLoader::isVisibleForWorkbench(const QStringList& workbenches, const QString& workbenchId)
{
    if (workbenches.isEmpty())
    {
        return true;
    }
    for (const auto& wb : workbenches)
    {
        if (wb.compare(workbenchId, Qt::CaseInsensitive) == 0)
        {
            return true;
        }
    }
    return false;
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
    {
        return {};
    }
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
        m_lastError = QStringLiteral("Invalid JSON in %1: %2").arg(path, parseError.errorString());
        SY_ERRORF("[UiConfigLoader] %s", qPrintable(m_lastError));
        return std::nullopt;
    }

    const QJsonObject root = doc.object();

    // 先解析当前配置
    auto config = parseConfig(doc);
    if (!config)
    {
        return std::nullopt;
    }

    // 处理 extends / inherits 继承：父配置在前，子配置覆盖/追加。
    // 兼容两种写法：顶层键（"extends": "base"）或 meta 内键（"meta": { "inherits": "base" }）。
    const QJsonObject meta = root.value(QStringLiteral("meta")).toObject();
    QString extends = root.value(QStringLiteral("extends")).toString();
    if (extends.isEmpty())
    {
        extends = meta.value(QStringLiteral("extends")).toString();
    }
    if (extends.isEmpty())
    {
        extends = root.value(QStringLiteral("inherits")).toString();
    }
    if (extends.isEmpty())
    {
        extends = meta.value(QStringLiteral("inherits")).toString();
    }
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

        // 父配置省略扩展名时补齐 .json（如 "inherits/extends: base" → ":/configs/base.json"），
        // 避免父配置资源加载失败导致整个配置回落为空
        if (!parentPath.endsWith(QStringLiteral(".json"), Qt::CaseInsensitive))
        {
            parentPath += QStringLiteral(".json");
        }

        auto parent = loadWithInheritance(parentPath);
        if (!parent)
        {
            m_lastError = QStringLiteral("Failed to load parent config '%1' referenced by %2").arg(extends, path);
            SY_ERRORF("[UiConfigLoader] %s", qPrintable(m_lastError));
            return std::nullopt;
        }

        mergeConfig(*parent, *config);
        // 子配置的 meta 覆盖父配置（客户 ID 以子配置为准）
        if (!config->meta.clientId.isEmpty())
        {
            parent->meta.clientId = config->meta.clientId;
        }
        if (!config->meta.clientName.isEmpty())
        {
            parent->meta.clientName = config->meta.clientName;
        }
        if (!config->meta.version.isEmpty())
        {
            parent->meta.version = config->meta.version;
        }
        return parent;
    }

    return config;
}

void UiConfigLoader::mergeConfig(UiConfigData& base, const UiConfigData& override)
{
    // 菜单合并：同 id 覆盖，新 id 追加
    for (const auto& menu : override.menus)
    {
        auto it = std::find_if(base.menus.begin(), base.menus.end(), [&menu](const MenuDef& m) {
            return m.id == menu.id;
        });
        if (it != base.menus.end())
        {
            *it = menu;
        }
        else
        {
            base.menus.push_back(menu);
        }
    }

    // 工具栏合并
    for (const auto& tb : override.toolBars)
    {
        auto it = std::find_if(base.toolBars.begin(), base.toolBars.end(), [&tb](const ToolBarDef& t) {
            return t.id == tb.id;
        });
        if (it != base.toolBars.end())
        {
            *it = tb;
        }
        else
        {
            base.toolBars.push_back(tb);
        }
    }

    // Dock 合并
    for (const auto& dock : override.docks)
    {
        auto it = std::find_if(base.docks.begin(), base.docks.end(), [&dock](const DockDef& d) {
            return d.id == dock.id;
        });
        if (it != base.docks.end())
        {
            *it = dock;
        }
        else
        {
            base.docks.push_back(dock);
        }
    }

    // 快捷键合并（按 commandId 覆盖）
    for (const auto& sc : override.shortcuts)
    {
        auto it = std::find_if(base.shortcuts.begin(), base.shortcuts.end(), [&sc](const ShortcutDef& s) {
            return s.commandId == sc.commandId;
        });
        if (it != base.shortcuts.end())
        {
            *it = sc;
        }
        else
        {
            base.shortcuts.push_back(sc);
        }
    }

    if (!override.themeStyle.isEmpty())
    {
        base.themeStyle = override.themeStyle;
    }

    // 状态栏合并（整节替换）：状态栏槽位顺序本身就是布局语义，
    // 逐槽位覆盖会让客户难以预测最终排列，因此约定「子配置写了就整节接管」。
    if (override.statusBar.declared)
    {
        base.statusBar = override.statusBar;
    }

    // 右键菜单合并：同 id 覆盖，新 id 追加（与菜单一致）
    for (const auto& cm : override.contextMenus)
    {
        auto it = std::find_if(base.contextMenus.begin(), base.contextMenus.end(), [&cm](const ContextMenuDef& c) {
            return c.id == cm.id;
        });
        if (it != base.contextMenus.end())
        {
            *it = cm;
        }
        else
        {
            base.contextMenus.push_back(cm);
        }
    }
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
        {
            continue;
        }
        auto menu = parseMenu(value.toObject());
        if (menu)
        {
            data.menus.push_back(std::move(*menu));
        }
        else
        {
            m_lastError = QStringLiteral("Invalid menu definition");
        }
    }

    // toolbars
    const QJsonArray toolbars = root.value(QStringLiteral("toolbars")).toArray();
    for (const auto& value : toolbars)
    {
        if (!value.isObject())
        {
            continue;
        }
        auto tb = parseToolBar(value.toObject());
        if (tb)
        {
            data.toolBars.push_back(std::move(*tb));
        }
    }

    // docks
    const QJsonArray docks = root.value(QStringLiteral("docks")).toArray();
    for (const auto& value : docks)
    {
        if (!value.isObject())
        {
            continue;
        }
        auto dock = parseDock(value.toObject());
        if (dock)
        {
            data.docks.push_back(std::move(*dock));
        }
    }

    // shortcuts
    const QJsonArray shortcuts = root.value(QStringLiteral("shortcuts")).toArray();
    for (const auto& value : shortcuts)
    {
        if (!value.isObject())
        {
            continue;
        }
        auto sc = parseShortcut(value.toObject());
        if (sc)
        {
            data.shortcuts.push_back(std::move(*sc));
        }
    }

    // statusBar（P0-2a）：整节缺省时保留默认值（可见 + 无槽位），
    // 由 UiLayoutBuilder 构建出一个空状态栏容器，行为与历史硬编码骨架一致。
    if (root.contains(QStringLiteral("statusBar")))
    {
        data.statusBar = parseStatusBar(root.value(QStringLiteral("statusBar")).toObject());
    }

    // contextMenus（P0-2b）
    const QJsonArray contextMenus = root.value(QStringLiteral("contextMenus")).toArray();
    for (const auto& value : contextMenus)
    {
        if (!value.isObject())
        {
            continue;
        }
        auto cm = parseContextMenu(value.toObject());
        if (cm)
        {
            data.contextMenus.push_back(std::move(*cm));
        }
    }

    return data;
}

std::optional<MenuDef> UiConfigLoader::parseMenu(const QJsonObject& obj)
{
    MenuDef menu;
    menu.id = obj.value(QStringLiteral("id")).toString();
    menu.label = obj.value(QStringLiteral("label")).toString();
    menu.iconName = obj.value(QStringLiteral("icon")).toString();
    menu.visible = obj.value(QStringLiteral("visible")).toBool(true);
    const auto workbenchList = obj.value(QStringLiteral("workbenches")).toVariant().toStringList();
    menu.workbenches = workbenchList;
    menu.visibilityScope = normalizeVisibilityScope(parseVisibilityScope(obj), menu.workbenches);
    if (menu.id.isEmpty())
    {
        return std::nullopt;
    }

    if (!parseMenuItems(obj.value(QStringLiteral("items")).toArray(), menu.items))
    {
        return std::nullopt;
    }
    return menu;
}

std::optional<MenuActionDef> UiConfigLoader::parseMenuAction(const QJsonObject& obj)
{
    MenuActionDef action;
    action.id = obj.value(QStringLiteral("id")).toString();
    action.label = obj.value(QStringLiteral("label")).toString();
    action.commandId = obj.value(QStringLiteral("commandId")).toString();
    if (action.commandId.isEmpty())
    {
        action.commandId = obj.value(QStringLiteral("command")).toString();
    }
    action.iconName = obj.value(QStringLiteral("icon")).toString();
    action.shortcut = obj.value(QStringLiteral("shortcut")).toString();
    action.feature = obj.value(QStringLiteral("feature")).toString();
    action.visible = obj.value(QStringLiteral("visible")).toBool(true);
    action.checkable = obj.value(QStringLiteral("checkable")).toBool(false);
    action.workbenches = obj.value(QStringLiteral("workbenches")).toVariant().toStringList();
    action.visibilityScope = normalizeVisibilityScope(parseVisibilityScope(obj), action.workbenches);
    if (action.commandId.isEmpty())
    {
        return std::nullopt;
    }
    return action;
}

std::optional<SubMenuDef> UiConfigLoader::parseSubMenu(const QJsonObject& obj)
{
    SubMenuDef sub;
    sub.id = obj.value(QStringLiteral("id")).toString();
    sub.label = obj.value(QStringLiteral("label")).toString();
    sub.iconName = obj.value(QStringLiteral("icon")).toString();
    sub.visible = obj.value(QStringLiteral("visible")).toBool(true);
    sub.workbenches = obj.value(QStringLiteral("workbenches")).toVariant().toStringList();
    sub.visibilityScope = normalizeVisibilityScope(parseVisibilityScope(obj), sub.workbenches);
    // 与 contextMenus 用同一个键名解析同一套机制，见 SubMenuDef::dynamicSections
    sub.dynamicSections = obj.value(QStringLiteral("dynamicSections")).toVariant().toStringList();
    if (sub.id.isEmpty())
    {
        return std::nullopt;
    }

    if (!parseMenuItems(obj.value(QStringLiteral("items")).toArray(), sub.items))
    {
        return std::nullopt;
    }
    return sub;
}

bool UiConfigLoader::parseMenuItems(
    const QJsonArray& array, std::vector<std::variant<MenuActionDef, SubMenuDef, MenuItemType>>& out)
{
    for (const auto& value : array)
    {
        if (!value.isObject())
        {
            // 纯字符串或非对象项：仅识别 "separator" 文本
            if (value.isString() && value.toString().compare(QStringLiteral("separator"), Qt::CaseInsensitive) == 0)
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
            {
                out.push_back(std::move(*sub));
            }
            else
            {
                return false;
            }
        }
        else  // action（默认）
        {
            auto action = parseMenuAction(item);
            if (action)
            {
                out.push_back(std::move(*action));
            }
            else
            {
                return false;
            }
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
    {
        return std::nullopt;
    }

    const QJsonArray items = obj.value(QStringLiteral("items")).toArray();
    for (const auto& value : items)
    {
        if (!value.isObject())
        {
            continue;
        }
        const QJsonObject item = value.toObject();
        if (item.value(QStringLiteral("type")).toString().toLower() == QStringLiteral("separator"))
        {
            tb.items.push_back(MenuItemType::Separator);
            continue;
        }
        auto action = parseToolBarAction(item);
        if (action)
        {
            tb.items.push_back(std::move(*action));
        }
    }
    return tb;
}

std::optional<ToolBarActionDef> UiConfigLoader::parseToolBarAction(const QJsonObject& obj)
{
    ToolBarActionDef action;
    action.id = obj.value(QStringLiteral("id")).toString();
    action.label = obj.value(QStringLiteral("label")).toString();
    action.iconName = obj.value(QStringLiteral("icon")).toString();
    action.commandId = obj.value(QStringLiteral("commandId")).toString();
    if (action.commandId.isEmpty())
    {
        action.commandId = obj.value(QStringLiteral("command")).toString();
    }
    action.shortcut = obj.value(QStringLiteral("shortcut")).toString();
    action.feature = obj.value(QStringLiteral("feature")).toString();
    action.checkable = obj.value(QStringLiteral("checkable")).toBool(false);
    if (action.commandId.isEmpty())
    {
        return std::nullopt;
    }
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
    {
        return std::nullopt;
    }
    return dock;
}

std::optional<ShortcutDef> UiConfigLoader::parseShortcut(const QJsonObject& obj)
{
    ShortcutDef sc;
    sc.commandId = obj.value(QStringLiteral("command")).toString();
    sc.keySequence = obj.value(QStringLiteral("keys")).toString();
    if (sc.commandId.isEmpty() || sc.keySequence.isEmpty())
    {
        return std::nullopt;
    }
    return sc;
}

StatusBarDef UiConfigLoader::parseStatusBar(const QJsonObject& obj)
{
    StatusBarDef bar;
    bar.declared = true;
    bar.visible = obj.value(QStringLiteral("visible")).toBool(true);
    bar.sizeGripEnabled = obj.value(QStringLiteral("sizeGripEnabled")).toBool(true);

    const QJsonArray items = obj.value(QStringLiteral("items")).toArray();
    for (const auto& value : items)
    {
        if (!value.isObject())
        {
            continue;
        }
        auto slot = parseStatusBarSlot(value.toObject());
        if (slot)
        {
            bar.items.push_back(std::move(*slot));
        }
    }

    SY_DEBUGF("[UiConfigLoader] StatusBar parsed: visible=%d slots=%d",
        bar.visible ? 1 : 0,
        static_cast<int>(bar.items.size()));
    return bar;
}

std::optional<StatusBarSlotDef> UiConfigLoader::parseStatusBarSlot(const QJsonObject& obj)
{
    StatusBarSlotDef slot;
    slot.id = obj.value(QStringLiteral("id")).toString();
    slot.widgetType = obj.value(QStringLiteral("widgetType")).toString();
    slot.align = parseStatusBarAlign(obj.value(QStringLiteral("align")).toString());
    slot.stretch = obj.value(QStringLiteral("stretch")).toInt(0);
    slot.minimumWidth = obj.value(QStringLiteral("minimumWidth")).toInt(0);
    slot.feature = obj.value(QStringLiteral("feature")).toString();
    slot.visible = obj.value(QStringLiteral("visible")).toBool(true);

    // widgetType 是槽位的唯一必填字段：没有它就无法从面板工厂取到控件
    if (slot.widgetType.isEmpty())
    {
        SY_WARNF("[UiConfigLoader] StatusBar slot id='%s' has no widgetType, skipped", qPrintable(slot.id));
        return std::nullopt;
    }
    if (slot.id.isEmpty())
    {
        slot.id = slot.widgetType;
    }
    return slot;
}

std::optional<ContextMenuDef> UiConfigLoader::parseContextMenu(const QJsonObject& obj)
{
    ContextMenuDef cm;
    cm.id = obj.value(QStringLiteral("id")).toString();
    cm.workbenchId = obj.value(QStringLiteral("workbench")).toString(QStringLiteral("global"));
    cm.feature = obj.value(QStringLiteral("feature")).toString();
    cm.dynamicSections = obj.value(QStringLiteral("dynamicSections")).toVariant().toStringList();
    if (cm.id.isEmpty())
    {
        SY_WARN("[UiConfigLoader] Context menu without id, skipped");
        return std::nullopt;
    }

    // 复用菜单条目解析：右键菜单与主菜单共享 action/submenu/separator 语义与 feature 门控
    if (!parseMenuItems(obj.value(QStringLiteral("items")).toArray(), cm.items))
    {
        SY_WARNF("[UiConfigLoader] Context menu id='%s' has invalid items, skipped", qPrintable(cm.id));
        return std::nullopt;
    }

    SY_DEBUGF("[UiConfigLoader] ContextMenu parsed id='%s' workbench='%s' items=%d",
        qPrintable(cm.id),
        qPrintable(cm.workbenchId),
        static_cast<int>(cm.items.size()));
    return cm;
}