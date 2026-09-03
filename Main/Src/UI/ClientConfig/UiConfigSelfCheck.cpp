/**
 * @file UiConfigSelfCheck.cpp
 * @brief 客户配置可信性启动自检的实现
 */

#include "UiConfigSelfCheck.h"

#include "BuildConfig.h"
#include "UiClientConfigBase.h"
#include "UiClientContext.h"
#include "UiConfigurationManager.h"
#include "UiFeatureGate.h"
#include "UI/Workbench/WorkbenchMenuManager.h"
#include "UI2D/Operation/CommandCatalog.h"

#if BUILD_UI3D
#include "UI3D/Operation/CommandCatalog3D.h"
#endif

#include "Log/SyLogger.h"

#include <variant>

namespace
{
    using MenuItemVariant = std::variant<MenuActionDef, SubMenuDef, MenuItemType>;

    /// 把 workbenchId 单值（"2D" / "3D" / "global"）折算成 workbenches 列表
    QStringList workbenchScope(const QString& workbenchId)
    {
        if (workbenchId.isEmpty() || workbenchId.compare(QLatin1String("global"), Qt::CaseInsensitive) == 0)
        {
            return {};
        }
        return { workbenchId };
    }

    /// 递归收集菜单/子菜单/右键菜单里的命令项。
    /// workbenches 缺省时继承上层（JSON 里子项普遍不重复声明）。
    void collectMenuCommands(const std::vector<MenuItemVariant>& items,
        const QString& parentPath,
        const QStringList& inheritedWorkbenches,
        QVector<UiConfigCommandRef>& out)
    {
        for (const MenuItemVariant& item : items)
        {
            if (const auto* action = std::get_if<MenuActionDef>(&item))
            {
                UiConfigCommandRef ref;
                ref.commandId = action->commandId;
                ref.path = parentPath + QLatin1Char('/') + action->id;
                ref.workbenches = action->workbenches.isEmpty() ? inheritedWorkbenches : action->workbenches;
                out.append(ref);
            }
            else if (const auto* sub = std::get_if<SubMenuDef>(&item))
            {
                const QStringList scope = sub->workbenches.isEmpty() ? inheritedWorkbenches : sub->workbenches;
                collectMenuCommands(sub->items, parentPath + QLatin1Char('/') + sub->id, scope, out);
            }
            // MenuItemType（分隔符）不携带命令，跳过
        }
    }

    /// 递归收集菜单树里的 feature 引用
    void collectMenuFeatures(
        const std::vector<MenuItemVariant>& items, const QString& parentPath, QVector<UiConfigFeatureRef>& out)
    {
        for (const MenuItemVariant& item : items)
        {
            if (const auto* action = std::get_if<MenuActionDef>(&item))
            {
                if (!action->feature.isEmpty())
                {
                    out.append({ action->feature, parentPath + QLatin1Char('/') + action->id });
                }
            }
            else if (const auto* sub = std::get_if<SubMenuDef>(&item))
            {
                collectMenuFeatures(sub->items, parentPath + QLatin1Char('/') + sub->id, out);
            }
        }
    }

    bool resolvableIn2D(const QString& commandId)
    {
        return CommandCatalog::operationForCommandId(commandId) != OperationId::None;
    }

    bool resolvableIn3D(const QString& commandId)
    {
#if BUILD_UI3D
        return CommandCatalog3D::operationForCommandId(commandId) != OperationId3D::None;
#else
        Q_UNUSED(commandId);
        // 未编译 3D 时目录不存在，无从校验：这类缺口由 workbenchNotCompiled 单独报告
        return true;
#endif
    }

    /// 3D 目录里被几何内核编译开关门控的命令。
    /// JSON 是编译无关的、始终声明这些项；关掉 OCCT 时它们从目录里消失属于预期裁剪，
    /// 与"接线漏了"必须分开报告，否则每次裁剪构建都会刷出一堆假告警。
    bool gatedOutByGeoModelCore(const QString& commandId)
    {
        if (BuildConfig::kGeoModelCore)
        {
            return false;
        }
        static const QStringList kGeoModelOnly = {
            QStringLiteral("file.import_step"),
            QStringLiteral("file.export_step"),
            QStringLiteral("file.open_step"),
            QStringLiteral("file.save_brep_step"),
            QStringLiteral("file.save_brep_step_as"),
            QStringLiteral("model.make_box"),
            QStringLiteral("model.make_sphere"),
            QStringLiteral("model.make_cylinder"),
            QStringLiteral("model.boolean_fuse"),
            QStringLiteral("model.boolean_cut"),
            QStringLiteral("model.split_half_x"),
            QStringLiteral("model.split_half_y"),
            QStringLiteral("model.split_half_z"),
            QStringLiteral("model.split_by_pick_plane"),
        };
        return kGeoModelOnly.contains(commandId);
    }

}  // namespace

QVector<UiConfigCommandRef> UiConfigSelfCheck::collectCommandRefs(const UiConfigData& config)
{
    QVector<UiConfigCommandRef> refs;

    for (const MenuDef& menu : config.menus)
    {
        collectMenuCommands(menu.items, QStringLiteral("menus/") + menu.id, menu.workbenches, refs);
    }

    for (const ToolBarDef& toolBar : config.toolBars)
    {
        const QStringList scope = workbenchScope(toolBar.workbenchId);
        for (const auto& item : toolBar.items)
        {
            if (const auto* action = std::get_if<ToolBarActionDef>(&item))
            {
                refs.append({ action->commandId,
                    QStringLiteral("toolBars/") + toolBar.id + QLatin1Char('/') + action->id,
                    scope });
            }
        }
    }

    for (const ContextMenuDef& menu : config.contextMenus)
    {
        collectMenuCommands(
            menu.items, QStringLiteral("contextMenus/") + menu.id, workbenchScope(menu.workbenchId), refs);
    }

    for (const ShortcutDef& shortcut : config.shortcuts)
    {
        // 快捷键节没有工作台字段：只要求"至少一侧能解析"，用空 scope 表达
        refs.append({ shortcut.commandId, QStringLiteral("shortcuts/") + shortcut.keySequence, QStringList() });
    }

    return refs;
}

QVector<UiConfigFeatureRef> UiConfigSelfCheck::collectFeatureRefs(const UiConfigData& config)
{
    QVector<UiConfigFeatureRef> refs;

    for (const MenuDef& menu : config.menus)
    {
        collectMenuFeatures(menu.items, QStringLiteral("menus/") + menu.id, refs);
    }

    for (const ToolBarDef& toolBar : config.toolBars)
    {
        if (!toolBar.feature.isEmpty())
        {
            refs.append({ toolBar.feature, QStringLiteral("toolBars/") + toolBar.id });
        }
        for (const auto& item : toolBar.items)
        {
            if (const auto* action = std::get_if<ToolBarActionDef>(&item))
            {
                if (!action->feature.isEmpty())
                {
                    refs.append({ action->feature,
                        QStringLiteral("toolBars/") + toolBar.id + QLatin1Char('/') + action->id });
                }
            }
        }
    }

    for (const ContextMenuDef& menu : config.contextMenus)
    {
        if (!menu.feature.isEmpty())
        {
            refs.append({ menu.feature, QStringLiteral("contextMenus/") + menu.id });
        }
        collectMenuFeatures(menu.items, QStringLiteral("contextMenus/") + menu.id, refs);
    }

    for (const StatusBarSlotDef& slot : config.statusBar.items)
    {
        if (!slot.feature.isEmpty())
        {
            refs.append({ slot.feature, QStringLiteral("statusBar/") + slot.id });
        }
    }

    return refs;
}

bool UiConfigSelfCheck::isFeatureCompiledIn(const QString& featureId, bool& known)
{
    // featureId 是"卖点"命名（License 侧口径），构建开关是"模块"命名，两者不同名，
    // 这张表就是唯一的对应关系。新增授权功能时必须同步这里，否则自检对它无感。
    struct FeatureBuildSwitch
    {
        const char* featureId;
        bool compiled;
    };
    static const FeatureBuildSwitch kMap[] = {
        { "nesting", BuildConfig::kNesting },
        { "vision", BuildConfig::kVision },
        { "relief3d", BuildConfig::kEngraving },
        { "hardware", BuildConfig::kHardware },
        { "3d", BuildConfig::kUi3D },
    };

    const QString normalized = featureId.trimmed().toLower();
    for (const FeatureBuildSwitch& entry : kMap)
    {
        if (normalized == QLatin1String(entry.featureId))
        {
            known = true;
            return entry.compiled;
        }
    }
    known = false;
    return false;
}

UiConfigSelfCheckReport UiConfigSelfCheck::run(const UiConfigData& config)
{
    UiConfigSelfCheckReport report;

    // ---- 1) 命令可解析性 ----
    const QVector<UiConfigCommandRef> commandRefs = collectCommandRefs(config);
    report.checkedCommandCount = static_cast<int>(commandRefs.size());
    for (const UiConfigCommandRef& ref : commandRefs)
    {
        if (ref.commandId.isEmpty())
        {
            continue;
        }
        // 窗口级命令（工作台切换 / 主题 / 语言）刻意不进命令目录，由 MenuDispatcher 短路。
        // 判定复用 WorkbenchMenuManager 的同一个谓词，不在这里再抄一份前缀名单。
        if (WorkbenchMenuManager::isWindowLevelCommand(ref.commandId))
        {
            continue;
        }

        const bool wants2D = ref.workbenches.isEmpty()
            || ref.workbenches.contains(QStringLiteral("2D"), Qt::CaseInsensitive);
        const bool wants3D = ref.workbenches.isEmpty()
            || ref.workbenches.contains(QStringLiteral("3D"), Qt::CaseInsensitive);

        QStringList missing;
        if (wants2D && !resolvableIn2D(ref.commandId))
        {
            missing << QStringLiteral("2D");
        }
        if (wants3D && !resolvableIn3D(ref.commandId))
        {
            missing << QStringLiteral("3D");
        }
        // 空 workbenches 表示"哪边都可能用"，只要有一侧能解析就不算缺口
        if (ref.workbenches.isEmpty() && missing.size() < 2)
        {
            continue;
        }
        if (missing.isEmpty())
        {
            continue;
        }
        const QString detail = QStringLiteral("%1 @ %2 (catalog missing: %3)")
                                   .arg(ref.commandId, ref.path, missing.join(QLatin1Char('/')));
        if (gatedOutByGeoModelCore(ref.commandId))
        {
            report.gatedOutByBuild << detail;
        }
        else
        {
            report.unresolvedCommands << detail;
        }

    }

    // ---- 2) feature：JSON / License / 编译开关 三侧交叉 ----
    const QVector<UiConfigFeatureRef> featureRefs = collectFeatureRefs(config);
    report.checkedFeatureCount = static_cast<int>(featureRefs.size());
    const UiFeatureGate& gate = UiFeatureGate::instance();
    for (const UiConfigFeatureRef& ref : featureRefs)
    {
        bool known = false;
        const bool compiled = isFeatureCompiledIn(ref.featureId, known);
        if (!known)
        {
            // 没有对应构建开关的 feature（纯授权控制项）无法核对，跳过
            continue;
        }
        const bool licensed = gate.isAllowed(ref.featureId);
        if (licensed && !compiled)
        {
            report.licensedButNotCompiled
                << QStringLiteral("%1 @ %2").arg(ref.featureId, ref.path);
        }
        else if (!licensed && compiled)
        {
            report.compiledButNotLicensed << QStringLiteral("%1 @ %2").arg(ref.featureId, ref.path);
        }
    }

    // ---- 3) 工作台声明 vs 构建开关 ----
    if (!BuildConfig::kUi3D)
    {
        for (const MenuDef& menu : config.menus)
        {
            if (menu.workbenches.contains(QStringLiteral("3D"), Qt::CaseInsensitive))
            {
                report.workbenchNotCompiled
                    << QStringLiteral("3D @ menus/%1 (BUILD_UI3D is off)").arg(menu.id);
            }
        }
    }

    return report;
}

void UiConfigSelfCheck::logReport(const UiConfigSelfCheckReport& report, const QString& clientId)
{
    SY_INFOF("[ConfigSelfCheck] client='%s' commands=%d features=%d "
             "unresolved=%d gatedOut=%d licensedNotCompiled=%d compiledNotLicensed=%d workbenchGaps=%d",
        clientId.toUtf8().constData(),
        report.checkedCommandCount,
        report.checkedFeatureCount,
        static_cast<int>(report.unresolvedCommands.size()),
        static_cast<int>(report.gatedOutByBuild.size()),
        static_cast<int>(report.licensedButNotCompiled.size()),
        static_cast<int>(report.compiledButNotLicensed.size()),
        static_cast<int>(report.workbenchNotCompiled.size()));


    // 逐条打印：这些条目本身就是"去改哪一行 JSON / 开哪个构建开关"的答案，
    // 只给个计数等于没说。条数天然很少（正常构建应为 0）。
    for (const QString& item : report.unresolvedCommands)
    {
        SY_ERRORF("[ConfigSelfCheck] command not in catalog, button will never respond: %s",
            item.toUtf8().constData());
    }
    for (const QString& item : report.licensedButNotCompiled)
    {
        SY_ERRORF("[ConfigSelfCheck] feature licensed but not compiled into this build: %s",
            item.toUtf8().constData());
    }
    for (const QString& item : report.workbenchNotCompiled)
    {
        SY_WARNF("[ConfigSelfCheck] workbench declared in config but not compiled: %s",
            item.toUtf8().constData());
    }
    if (!report.gatedOutByBuild.isEmpty())
    {
        SY_DEBUGF("[ConfigSelfCheck] %d command(s) absent because module not compiled (expected trim)",
            report.gatedOutByBuild.size());
    }

    for (const QString& item : report.compiledButNotLicensed)
    {
        SY_INFOF("[ConfigSelfCheck] feature compiled but not licensed (hidden by gate): %s",
            item.toUtf8().constData());
    }

    if (!report.hasBlockingIssue())
    {
        SY_INFO("[ConfigSelfCheck] Config trust check passed: CMake switches, JSON config and license agree");
    }
}

void UiConfigSelfCheck::runAndLogForCurrentClient()
{
    // 复用共享配置实例：自检必须看的是"实际生效的那份"，
    // 重新加载一遍会得到另一份副本，反而可能掩盖装配顺序问题。
    UiConfigurationManager& manager = UiConfigurationManager::shared();
    const UiConfigData* config = manager.configData();
    if (!config)
    {
        SY_ERROR("[ConfigSelfCheck] No client config loaded, self-check skipped "
                 "(menus/toolbars will fall back to empty)");
        return;
    }

    const UiConfigSelfCheckReport report = run(*config);
    logReport(report, config->meta.clientId.isEmpty() ? UiClientContext::instance().clientId()
                                                      : config->meta.clientId);
}
