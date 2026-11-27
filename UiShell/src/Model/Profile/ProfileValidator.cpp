#include "ProfileValidator.h"

#include <QSet>

#include "Model/Profile/ProfileDiagnosticUtil.h"
#include "Model/Profile/ProfileLoadResult.h"
#include "Model/Profile/ProfileModel.h"

namespace RD
{
    QList<Diagnostic> ProfileValidator::validate(const ProfileModel& model)
    {
        QList<Diagnostic> diagnostics;

        if (model.strProfileId.isEmpty())
            addError(diagnostics, QStringLiteral("$.profileId"), QStringLiteral("profileId is required"));

        if (model.regions.isEmpty())
            addError(diagnostics, QStringLiteral("$.regions"), QStringLiteral("at least one region is required"));

        // 只收集 Parser 未覆盖的状态，所有 strId 唯一性与基础字段校验都在 Parser 里完成。
        QSet<QString> paneAreaIds;
        bool hasCenterView = false;

        for (int i = 0; i < model.regions.size(); ++i)
        {
            const RegionSpec& region = model.regions.at(i);
            const QString path = QStringLiteral("$.regions[%1]").arg(i);

            if (region.type == RegionType::CenterView)
                hasCenterView = true;

            // auto area 生成可能因冲突失败，这里只做"最终状态"检查。
            if (region.paneAreas.isEmpty())
            {
                addError(diagnostics,
                    path + QStringLiteral(".areas"),
                    QStringLiteral("region '%1' has no areas").arg(region.strId));
            }

            // 收集 area strId 用于后面检查 panel 跨引用，不做 Parser 已完成的重复断言。
            for (const PaneAreaSpec& area : region.paneAreas)
                paneAreaIds.insert(area.strId);
        }

        QSet<QString> panelIds;
        for (int i = 0; i < model.panels.size(); ++i)
        {
            const PanelSpec& panel = model.panels.at(i);
            const QString path = QStringLiteral("$.panels[%1]").arg(i);

            if (panel.strId.isEmpty())
            {
                addError(diagnostics, path + QStringLiteral(".id"), QStringLiteral("panel id is required"));
            }
            else if (panelIds.contains(panel.strId))
            {
                addError(
                    diagnostics, path + QStringLiteral(".id"), QStringLiteral("duplicate panel id '%1'").arg(panel.strId));
            }
            else
            {
                panelIds.insert(panel.strId);
            }

            if (panel.factory.isEmpty())
                addError(diagnostics, path + QStringLiteral(".factory"), QStringLiteral("panel factory is required"));

            if (panel.paneAreaId.isEmpty())
            {
                addError(diagnostics, path + QStringLiteral(".area"), QStringLiteral("panel area is required"));
            }
            else if (!paneAreaIds.contains(panel.paneAreaId))
            {
                addError(
                    diagnostics, path + QStringLiteral(".area"), QStringLiteral("unknown area '%1'").arg(panel.paneAreaId));
            }
        }

        // 没有中央视图时只降级为警告：某些纯对话框式界面并不需要 CenterView。
        if (!hasCenterView)
            addWarning(diagnostics, QStringLiteral("$.regions"), QStringLiteral("no CenterView region declared"));

        return diagnostics;
    }
}  // namespace RD