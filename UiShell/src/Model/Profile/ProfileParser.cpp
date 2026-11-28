#include "ProfileParser.h"

#include <algorithm>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSet>

#include "Model/Json/Jsonc.h"
#include "Model/Json/JsoncInternal.h"
#include "Model/Profile/ProfileDiagnosticUtil.h"
#include "ProfileSchema.h"
#include "ProfileValidator.h"

namespace AA
{
    namespace
    {
        QString quoted(const char* key)
        {
            return QString::fromLatin1(key);
        }
    }  // namespace

    ProfileLoadResult ProfileParser::loadFromJson(const QByteArray& utf8Json)
    {
        ProfileLoadResult result;
        QList<Diagnostic>& diagnostics = result.diagnostics;

        // 大小拦截
        if (utf8Json.size() > ProfileLimits::maxProfileBytes)
        {
            addError(diagnostics,
                QStringLiteral("$"), QStringLiteral("profile exceeds maximum size (%1 bytes, limit %2)")
                .arg(utf8Json.size()).arg(ProfileLimits::maxProfileBytes));

            return result;
        }

        const JsoncStripResult stripped = stripJsoncComments(utf8Json);
        if (!stripped.ok)
        {
            Diagnostic diagnostic = makeDiagnostic(Diagnostic::Level::Error,
                QStringLiteral("$"),
                QStringLiteral("line %1: %2").arg(stripped.errorLine).arg(stripped.error));

            diagnostic.nLine = stripped.errorLine;
            diagnostics.append(diagnostic);

            return result;
        }

        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(stripped.json, &parseError);
        if (parseError.error != QJsonParseError::NoError)
        {
            int line = 0;
            int column = 0;

            // 报错偏移是字节偏移；转成行列号后用户才能直接定位到原文件位置。
            jsoncLineColumn(stripped.json, parseError.offset, &line, &column);

            Diagnostic diagnostic = makeDiagnostic(Diagnostic::Level::Error,
                QStringLiteral("$"),
                QStringLiteral("line %1, column %2: %3").arg(line).arg(column).arg(parseError.errorString()));

            diagnostic.nLine = line;
            diagnostic.nColumn = column;
            diagnostics.append(diagnostic);

            return result;
        }

        if (!doc.isObject())
        {
            addError(diagnostics, QStringLiteral("$"), QStringLiteral("root must be a JSON object"));
            return result;
        }

        const QJsonObject root = doc.object();
        ProfileModel model;

        model.nVersion = root.value(QStringLiteral("version")).toInt(-1);

        // 缺失/非整数按 -1 处理，避免旧格式被误读。
        if (model.nVersion != kProfileVersion)
        {
            if (model.nVersion > kProfileVersion)
            {
                addError(diagnostics,
                    QStringLiteral("$.version"),
                    QStringLiteral("unsupported version %1: profile was written for a newer version (this build "
                        "supports %2)")
                    .arg(model.nVersion).arg(kProfileVersion));
            }
            else
            {
                addError(diagnostics,
                    QStringLiteral("$.version"),
                    QStringLiteral("unsupported version %1 (expected %2)")
                    .arg(model.nVersion).arg(kProfileVersion));
            }
        }

        // 未知顶层字段只告警不失败：为未来新增字段保留前向兼容，
        // 同时让拼写错误（如 "menues"）在诊断里可见。
        const QSet<QString> kTopLevel{ QStringLiteral("version"),
            QStringLiteral("profileId"),
            QStringLiteral("brand"),
            QStringLiteral("titleBar"),
            QStringLiteral("window"),
            QStringLiteral("regions"),
            QStringLiteral("panels"),
            QStringLiteral("menus"),
            QStringLiteral("contextMenus") };

        for (auto it = root.constBegin(); it != root.constEnd(); ++it)
        {
            if (!kTopLevel.contains(it.key()))
            {
                addWarning(diagnostics,
                    QStringLiteral("$.%1").arg(it.key()),
                    QStringLiteral("unknown top-level field '%1' (ignored)").arg(it.key()));
            }
        }

        model.strProfileId = root.value(QStringLiteral("profileId")).toString().trimmed();

        // ---- brand（M5：外置 profile 驱动品牌/外观）----
        const QJsonValue brandValue = root.value(QStringLiteral("brand"));
        if (!brandValue.isUndefined() && !brandValue.isObject())
        {
            addError(diagnostics, QStringLiteral("$.brand"), QStringLiteral("must be an object"));
        }
        else if (brandValue.isObject())
        {
            const QJsonObject brand = brandValue.toObject();
            model.brand.strProduct = brand.value(QStringLiteral("product")).toString().trimmed();
            model.brand.strOrganization = brand.value(QStringLiteral("organization")).toString().trimmed();
            model.brand.strIcon = brand.value(QStringLiteral("icon")).toString().trimmed();
            model.brand.strTheme = brand.value(QStringLiteral("theme")).toString().trimmed().toLower();
            model.brand.strLanguage = brand.value(QStringLiteral("language")).toString().trimmed();
            model.brand.strCopyright = brand.value(QStringLiteral("copyright")).toString().trimmed();
            model.brand.strWebsite = brand.value(QStringLiteral("website")).toString().trimmed();
            model.brand.strEmail = brand.value(QStringLiteral("email")).toString().trimmed();
            model.brand.strLicense = brand.value(QStringLiteral("license")).toString().trimmed();
            model.brand.strLogo = brand.value(QStringLiteral("logo")).toString().trimmed();
        }

        // ---- titleBar ----
        const QJsonValue titleBarValue = root.value(QStringLiteral("titleBar"));
        if (!titleBarValue.isUndefined() && !titleBarValue.isObject())
        {
            addError(diagnostics, QStringLiteral("$.titleBar"), QStringLiteral("must be an object"));
        }
        else if (titleBarValue.isObject())
        {
            const QJsonObject object = titleBarValue.toObject();
            model.titleBar.bEnabled = object.value(QStringLiteral("enabled")).toBool(true);
            model.titleBar.strTitle = object.value(QStringLiteral("title")).toString();
            model.titleBar.strTitleKey = object.value(QStringLiteral("titleKey")).toString().trimmed();
            model.titleBar.strIcon = object.value(QStringLiteral("icon")).toString();
            model.titleBar.nHeight = object.value(QStringLiteral("height")).toInt(36);
            // 拖拽与双击最大化恒可用，不再从 profile 读取开关。
            model.titleBar.bShowTitle = object.value(QStringLiteral("showTitle")).toBool(true);
            model.titleBar.strBkgColor = object.value(QStringLiteral("backgroundColor")).toString();
            model.titleBar.strTextColor = object.value(QStringLiteral("textColor")).toString();

            const QJsonValue buttonsValue = object.value(QStringLiteral("buttons"));
            if (!buttonsValue.isUndefined() && !buttonsValue.isArray())
            {
                addError(diagnostics, QStringLiteral("$.titleBar.buttons"), QStringLiteral("must be an array"));
            }
            else if (buttonsValue.isArray())
            {
                // 显式空数组表示"不要按钮"；未声明时保留默认的最小化/最大化/关闭。
                model.titleBar.listButtons.clear();
                const QJsonArray buttons = buttonsValue.toArray();
                for (int i = 0; i < buttons.size(); ++i)
                {
                    if (!buttons.at(i).isString())
                    {
                        addWarning(diagnostics,
                            QStringLiteral("$.titleBar.buttons[%1]").arg(i),
                            QStringLiteral("button id must be a string"));

                        continue;
                    }
                    model.titleBar.listButtons.append(buttons.at(i).toString().trimmed());
                }
            }
        }

        // ---- window ----
        const QJsonValue windowValue = root.value(QStringLiteral("window"));
        if (!windowValue.isUndefined() && !windowValue.isObject())
        {
            addError(diagnostics, QStringLiteral("$.window"), QStringLiteral("must be an object"));
        }
        else if (windowValue.isObject())
        {
            const QJsonObject object = windowValue.toObject();
            model.window.minWidth = object.value(QStringLiteral("minWidth")).toInt(800);
            model.window.minHeight = object.value(QStringLiteral("minHeight")).toInt(600);
            // 标题栏形态由平台编译期固定（Windows 自绘、macOS/Linux 原生）；
            // 菜单栏形态由 nativeMenuBar 控制（macOS 屏幕顶部 vs 窗口内）。
            const QJsonValue nativeMenuBarVal = object.value(QStringLiteral("nativeMenuBar"));
            if (nativeMenuBarVal.isBool())
                model.window.nativeMenuBar = nativeMenuBarVal.toBool();
        }

        // ---- regions ----
        const QJsonValue regionsValue = root.value(QStringLiteral("regions"));
        if (!regionsValue.isArray())
        {
            addError(diagnostics, QStringLiteral("$.regions"), QStringLiteral("must be an array"));
        }
        else
        {
            QSet<QString> regionIds;
            QSet<QString> paneAreaIds;
            const QJsonArray regions = regionsValue.toArray();

            const int regionCount = static_cast<int>(std::min<qsizetype>(regions.size(), ProfileLimits::maxRegions));
            if (regions.size() > ProfileLimits::maxRegions)
            {
                addError(diagnostics,
                    QStringLiteral("$.regions"),
                    QStringLiteral("too many regions (%1, limit %2)").arg(regions.size()).arg(ProfileLimits::maxRegions));
            }

            for (int i = 0; i < regionCount; ++i)
            {
                const QString regionPath = QStringLiteral("$.regions[%1]").arg(i);
                if (!regions.at(i).isObject())
                {
                    addError(diagnostics, regionPath, QStringLiteral("must be an object"));
                    continue;
                }

                const QJsonObject object = regions.at(i).toObject();
                RegionSpec region;

                region.strId = object.value(QStringLiteral("id")).toString().trimmed();
                if (region.strId.isEmpty())
                {
                    addError(diagnostics, regionPath + QStringLiteral(".id"), QStringLiteral("region id is required"));
                }
                else if (regionIds.contains(region.strId))
                {
                    addError(diagnostics,
                        regionPath + QStringLiteral(".id"),
                        QStringLiteral("duplicate region id '%1'").arg(region.strId));
                }
                else
                {
                    regionIds.insert(region.strId);
                }

                const QString typeName = object.value(QStringLiteral("type")).toString();
                if (!getRegionType(typeName, &region.type))
                {
                    addError(diagnostics,
                        regionPath + QStringLiteral(".type"),
                        QStringLiteral("unknown region type '%1'").arg(typeName));
                }

                region.order = object.value(QStringLiteral("order")).toInt(100);
                region.defaultVisible = object.value(QStringLiteral("visible")).toBool(true);

                const QJsonValue paneAreasValue = object.value(QStringLiteral("areas"));
                if (!paneAreasValue.isUndefined() && !paneAreasValue.isArray())
                {
                    addError(diagnostics, regionPath + QStringLiteral(".areas"), QStringLiteral("must be an array"));
                }

                if (paneAreasValue.isArray())
                {
                    const QJsonArray paneAreaArray = paneAreasValue.toArray();
                    const int paneAreaCount = static_cast<int>(std::min<qsizetype>(paneAreaArray.size(), ProfileLimits::maxPaneAreasPerRegion));
                    if (paneAreaArray.size() > ProfileLimits::maxPaneAreasPerRegion)
                    {
                        addError(diagnostics,
                            regionPath + QStringLiteral(".areas"),
                            QStringLiteral("too many areas in region '%1' (%2, limit %3)")
                            .arg(region.strId)
                            .arg(paneAreaArray.size())
                            .arg(ProfileLimits::maxPaneAreasPerRegion));
                    }

                    for (int j = 0; j < paneAreaCount; ++j)
                    {
                        const QString paneAreaPath = QStringLiteral("%1.areas[%2]").arg(regionPath).arg(j);
                        if (!paneAreaArray.at(j).isObject())
                        {
                            addError(diagnostics, paneAreaPath, QStringLiteral("must be an object"));
                            continue;
                        }

                        const QJsonObject paneAreaObject = paneAreaArray.at(j).toObject();
                        PaneAreaSpec area;
                        area.strId = paneAreaObject.value(QStringLiteral("id")).toString().trimmed();
                        if (area.strId.isEmpty())
                        {
                            addError(
                                diagnostics, paneAreaPath + QStringLiteral(".id"), QStringLiteral("area id is required"));
                        }
                        else if (paneAreaIds.contains(area.strId))
                        {
                            addError(diagnostics,
                                paneAreaPath + QStringLiteral(".id"),
                                QStringLiteral("duplicate area id '%1'").arg(area.strId));
                        }
                        else
                        {
                            paneAreaIds.insert(area.strId);
                        }

                        area.order = paneAreaObject.value(QStringLiteral("order")).toInt(100);
                        area.stretch = paneAreaObject.value(QStringLiteral("stretch")).toInt(0);
                        area.preferredWidth = paneAreaObject.value(QStringLiteral("preferredWidth")).toInt(0);
                        area.preferredHeight = paneAreaObject.value(QStringLiteral("preferredHeight")).toInt(0);
                        region.paneAreas.append(area);
                    }
                }

                // 未声明子区域的区域自动生成 <regionId>.main，便于最小 profile 使用。
                if (region.paneAreas.isEmpty() && !region.strId.isEmpty())
                {
                    PaneAreaSpec area;
                    area.strId = region.strId + QStringLiteral(".main");
                    area.order = 100;
                    area.stretch = (region.type == RegionType::CenterView) ? 1 : 0;
                    if (paneAreaIds.contains(area.strId))
                    {
                        addError(diagnostics,
                            regionPath + QStringLiteral(".areas"),
                            QStringLiteral("auto area '%1' conflicts with an existing area").arg(area.strId));
                    }
                    else
                    {
                        paneAreaIds.insert(area.strId);
                        region.paneAreas.append(area);
                    }
                }

                model.regions.append(region);
            }
        }

        // ---- panelsFactory ----
        const QJsonValue panelsValue = root.value(QStringLiteral("panels"));
        if (!panelsValue.isUndefined() && !panelsValue.isArray())
        {
            addError(diagnostics, QStringLiteral("$.panels"), QStringLiteral("must be an array"));
        }
        else if (panelsValue.isArray())
        {
            const QJsonArray panels = panelsValue.toArray();
            const int nPanelCount = static_cast<int>(std::min<qsizetype>(panels.size(), ProfileLimits::maxPanels));

            if (panels.size() > ProfileLimits::maxPanels)
            {
                addError(diagnostics,
                    QStringLiteral("$.panels"),
                    QStringLiteral("too many panels (%1, limit %2)").arg(panels.size()).arg(ProfileLimits::maxPanels));
            }

            for (int i = 0; i < nPanelCount; ++i)
            {
                const QString panelPath = QStringLiteral("$.panels[%1]").arg(i);
                if (!panels.at(i).isObject())
                {
                    addError(diagnostics, panelPath, QStringLiteral("must be an object"));
                    continue;
                }

                const QJsonObject object = panels.at(i).toObject();
                PanelSpec panel;
                panel.strId = object.value(QStringLiteral("id")).toString().trimmed();
                panel.factory = object.value(QStringLiteral("factory")).toString().trimmed();
                panel.paneAreaId = object.value(QStringLiteral("area")).toString().trimmed();
                panel.order = object.value(QStringLiteral("order")).toInt(100);
                panel.defaultVisible = object.value(QStringLiteral("defaultVisible")).toBool(true);
                panel.preferredWidth = object.value(QStringLiteral("preferredWidth")).toInt(0);
                panel.preferredHeight = object.value(QStringLiteral("preferredHeight")).toInt(0);
                panel.props = object.value(QStringLiteral("props")).toObject();

                if (panel.strId.isEmpty())
                    addError(diagnostics, panelPath + QStringLiteral(".id"), QStringLiteral("panel id is required"));

                if (panel.factory.isEmpty())
                {
                    addError(diagnostics,
                        panelPath + QStringLiteral(".factory"),
                        QStringLiteral("panel factory is required"));
                }

                if (panel.paneAreaId.isEmpty())
                    addError(diagnostics, panelPath + QStringLiteral(".area"), QStringLiteral("panel area is required"));

                const QJsonValue featuresValue = object.value(QStringLiteral("requiredCapabilities"));
                if (!featuresValue.isUndefined() && !featuresValue.isArray())
                {
                    addError(diagnostics,
                        panelPath + QStringLiteral(".requiredCapabilities"),
                        QStringLiteral("must be an array"));
                }
                else if (featuresValue.isArray())
                {
                    const QJsonArray features = featuresValue.toArray();
                    for (const QJsonValue& value : features)
                        panel.requiredFeatures.append(value.toString());
                }

                model.panels.append(panel);
            }
        }

        // ---- menus：递归解析 Command / Submenu / Separator ----
        int menuNodeCount = 0;
        bool menuNodeLimitReported = false;
        std::function<MenuNodeSpec(const QJsonValue&, const QString&, int)> parseMenuNode;
        parseMenuNode = [&](const QJsonValue& val, const QString& nodePath, int depth) -> MenuNodeSpec {
            MenuNodeSpec node;
            node.type = MenuNodeType::Command;

            if (++menuNodeCount > ProfileLimits::maxMenuNodes)
            {
                if (!menuNodeLimitReported)
                {
                    addError(diagnostics, nodePath,
                        QStringLiteral("too many menu nodes (limit %1)").arg(ProfileLimits::maxMenuNodes));

                    menuNodeLimitReported = true;
                }
                return node;
            }

            if (val.isString())
            {
                node.type = MenuNodeType::Command;
                node.commandId = val.toString().trimmed();
            }
            else if (val.isObject())
            {
                const QJsonObject obj = val.toObject();

                node.strId = obj.value(QStringLiteral("id")).toString().trimmed();
                node.order = obj.value(QStringLiteral("order")).toInt(100);
                // strIconKey 对所有节点类型通用：Command 可由 catalog 兜底，Submenu/Builtin 直接使用。
                node.iconKey = obj.value(QStringLiteral("iconKey")).toString();

                const QString typeStr = obj.value(QStringLiteral("type")).toString().trimmed();
                if (typeStr == QLatin1String("separator"))
                {
                    node.type = MenuNodeType::Separator;
                }
                else if (typeStr == QLatin1String("submenu"))
                {
                    node.type = MenuNodeType::Submenu;
                }
                else if (typeStr == QLatin1String("command"))
                {
                    node.type = MenuNodeType::Command;
                }
                else if (typeStr == QLatin1String("builtin"))
                {
                    node.type = MenuNodeType::Builtin;
                }
                else
                {
                    addError(diagnostics,
                        nodePath + QStringLiteral(".type"),
                        QStringLiteral("required, must be one of command / submenu / separator / builtin"));
                }

                if (node.type == MenuNodeType::Submenu)
                {
                    node.titleKey = obj.value(QStringLiteral("titleKey")).toString();

                    const QJsonValue childrenVal = obj.value(QStringLiteral("children"));
                    if (childrenVal.isArray())
                    {
                        const QJsonArray arr = childrenVal.toArray();
                        if (!arr.isEmpty() && depth >= ProfileLimits::maxMenuDepth)
                        {
                            addError(diagnostics,
                                nodePath + QStringLiteral(".children"),
                                QStringLiteral("menu nesting exceeds maximum depth %1").arg(ProfileLimits::maxMenuDepth));
                        }
                        else
                        {
                            for (int j = 0; j < arr.size(); ++j)
                            {
                                const QString childPath = nodePath + QStringLiteral(".children[%1]").arg(j);
                                node.children.append(parseMenuNode(arr.at(j), childPath, depth + 1));
                            }
                        }
                    }
                }
                else if (node.type == MenuNodeType::Command)
                {
                    node.commandId = obj.value(QStringLiteral("commandId")).toString().trimmed();
                    node.titleKey = obj.value(QStringLiteral("titleKey")).toString();
                    node.checkable = obj.value(QStringLiteral("checkable")).toBool(false);
                    node.groupKey = obj.value(QStringLiteral("groupKey")).toString();
                }
                else if (node.type == MenuNodeType::Separator)
                {
                    // no extra fields
                }
                else if (node.type == MenuNodeType::Builtin)
                {
                    if (node.strId != QLatin1String(BuiltinMenuId::Language) && node.strId != QLatin1String(BuiltinMenuId::Theme))
                    {
                        addError(diagnostics,
                            nodePath + QStringLiteral(".id"),
                            QStringLiteral("builtin menu id must be one of language / theme"));
                    }
                }
            }
            else
            {
                addError(diagnostics, nodePath, QStringLiteral("must be string or object"));
            }
            return node;
            };

        const QJsonValue menusValue = root.value(QStringLiteral("menus"));
        if (!menusValue.isUndefined() && !menusValue.isArray())
        {
            addError(diagnostics, QStringLiteral("$.menus"), QStringLiteral("must be an array"));
        }
        else if (menusValue.isArray())
        {
            const QJsonArray menus = menusValue.toArray();
            const int menuCount = static_cast<int>(std::min<qsizetype>(menus.size(), ProfileLimits::maxTopLevelMenus));
            if (menus.size() > ProfileLimits::maxTopLevelMenus)
            {
                addError(diagnostics,
                    QStringLiteral("$.menus"),
                    QStringLiteral("too many top-level menus (%1, limit %2)")
                    .arg(menus.size())
                    .arg(ProfileLimits::maxTopLevelMenus));
            }
            for (int i = 0; i < menuCount; ++i)
            {
                const QString menuPath = QStringLiteral("$.menus[%1]").arg(i);
                MenuNodeSpec menu = parseMenuNode(menus.at(i), menuPath, 0);
                if (menu.type != MenuNodeType::Separator)
                    menu.type = MenuNodeType::Submenu;

                if (menu.strId.isEmpty())
                    addError(diagnostics, menuPath + QStringLiteral(".id"), QStringLiteral("menu id is required"));

                model.menus.append(menu);
            }
        }

        // ---- contextMenus：与 menus 共用节点语法，但第一层是“右键菜单 id → 菜单项” ----
        // 右键不再由控件自建 QAction：这里只产出数据，由 MenuController 复用命令动作池构建。
        const QJsonValue contextMenusValue = root.value(QStringLiteral("contextMenus"));
        if (!contextMenusValue.isUndefined() && !contextMenusValue.isArray())
        {
            addError(diagnostics, QStringLiteral("$.contextMenus"), QStringLiteral("must be an array"));
        }
        else if (contextMenusValue.isArray())
        {
            const QJsonArray contextMenus = contextMenusValue.toArray();
            const int menuCount = static_cast<int>(std::min<qsizetype>(contextMenus.size(), ProfileLimits::maxTopLevelMenus));
            if (contextMenus.size() > ProfileLimits::maxTopLevelMenus)
            {
                addError(diagnostics,
                    QStringLiteral("$.contextMenus"),
                    QStringLiteral("too many context menus (%1, limit %2)")
                    .arg(contextMenus.size())
                    .arg(ProfileLimits::maxTopLevelMenus));
            }
            for (int i = 0; i < menuCount; ++i)
            {
                const QString menuPath = QStringLiteral("$.contextMenus[%1]").arg(i);
                MenuNodeSpec menu = parseMenuNode(contextMenus.at(i), menuPath, 0);
                if (menu.type != MenuNodeType::Separator)
                    menu.type = MenuNodeType::Submenu;

                if (menu.strId.isEmpty())
                    addError(diagnostics, menuPath + QStringLiteral(".id"), QStringLiteral("context menu id is required"));

                model.contextMenus.append(menu);
            }
        }

        diagnostics.append(ProfileValidator::validate(model));

        // 只要出现过 Error 就终止
        bool hasError = false;
        for (const Diagnostic& diagnostic : diagnostics)
        {
            if (diagnostic.level == Diagnostic::Level::Error)
            {
                hasError = true;
                break;
            }
        }

        if (!hasError)
            result.profileModel = model;

        return result;
    }
}  // namespace AA