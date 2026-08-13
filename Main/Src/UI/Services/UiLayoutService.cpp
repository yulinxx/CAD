#include "UiLayoutService.h"

#include <QSettings>

#include "WorkbenchWindow.h"

/// 保存布局快照到 QSettings
/// @param workbenchId 工作台 ID
/// @param window 主窗口指针
void DefaultUiLayoutService::saveLayout(const QString& workbenchId, WorkbenchWindow* window)
{
    if (!window)
    {
        return;
    }

    QSettings settings;
    settings.beginGroup(QStringLiteral("Layout"));
    settings.setValue(QStringLiteral("%1/geometry").arg(workbenchId), window->saveGeometry());
    settings.setValue(QStringLiteral("%1/state").arg(workbenchId), window->saveState());
    settings.endGroup();
}

/// 从 QSettings 恢复布局快照
/// @param workbenchId 工作台 ID
/// @param window 主窗口指针
void DefaultUiLayoutService::restoreLayout(const QString& workbenchId, WorkbenchWindow* window)
{
    if (!window)
    {
        return;
    }

    QSettings settings;
    settings.beginGroup(QStringLiteral("Layout"));

    if (settings.contains(QStringLiteral("%1/geometry").arg(workbenchId)))
    {
        window->restoreGeometry(settings.value(QStringLiteral("%1/geometry").arg(workbenchId)).toByteArray());
    }

    if (settings.contains(QStringLiteral("%1/state").arg(workbenchId)))
    {
        window->restoreState(settings.value(QStringLiteral("%1/state").arg(workbenchId)).toByteArray());
    }

    settings.endGroup();
}