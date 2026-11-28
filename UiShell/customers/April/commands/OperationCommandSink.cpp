#include "OperationCommandSink.h"

#include <QCoreApplication>
#include <QVariantMap>

#include "Log/AprLogger.h"
#include "UI2D/Operation/CommandCatalog.h"
#include "UI2D/Operation/OperationBus.h"
#include "UI2D/Operation/OperationRouting.h"
#include "UiDialogs/AboutDialogs.h"
#include "UiShell/Assembly/IShellRuntime.h"

namespace
{
    bool isShellCommand(const QString& commandId)
    {
        return commandId.startsWith(QLatin1String("theme."))
            || commandId.startsWith(QLatin1String("language."))
            || commandId == QLatin1String("help.about")
            || commandId == QLatin1String("view.switch_to_2d")
            || commandId == QLatin1String("view.switch_to_3d");
    }

    QString themeIdOf(const QString& commandId)
    {
        const QString name = commandId.mid(QStringLiteral("theme.").size());
        if (name == QLatin1String("dark"))
            return QStringLiteral("dark");
        if (name == QLatin1String("blue"))
            return QStringLiteral("blue");
        if (name == QLatin1String("slate"))
            return QStringLiteral("slate");
        if (name == QLatin1String("highcontrast"))
            return QStringLiteral("high_contrast");
        if (name == QLatin1String("system"))
            return QStringLiteral("system");
        return QStringLiteral("light");  // light / default / unknown
    }
}  // namespace

namespace Apr
{
    void OperationCommandSink::dispatch(const QString& commandId, const QVariantMap& params)
    {
        AA::IShellRuntime* runtime = m_shellRuntime;

        if (commandId.startsWith(QLatin1String("theme.")))
        {
            if (runtime)
                runtime->switchTheme(themeIdOf(commandId));
            return;
        }
        if (commandId.startsWith(QLatin1String("language.")))
        {
            if (runtime)
                runtime->switchLanguage(commandId.mid(QStringLiteral("language.").size()));
            return;
        }
        if (commandId == QLatin1String("help.about"))
        {
            if (runtime)
                runtime->openDialog(QString::fromLatin1(AA::about::kAboutDlgId), nullptr);
            return;
        }
        if (commandId == QLatin1String("view.switch_to_2d") || commandId == QLatin1String("view.switch_to_3d"))
        {
            if (!m_workbenchSwitch)
            {
                APR_WARNF("[OperationCommandSink] Workbench switch has no host: %s", qPrintable(commandId));
                return;
            }
            // Single owner: the command sink changes customer state first, then asks
            // the shell (which owns the widget tree) to mount the new viewport once.
            m_workbenchSwitch(commandId);
            if (runtime)
                runtime->refreshCenterView();
            return;
        }

        if (!m_bus)
        {
            APR_WARNF("[OperationCommandSink] Cannot dispatch command without OperationBus: %s",
                qPrintable(commandId));
            return;
        }

        // 与旧 Workbench2D::dispatchCommand 同一条路径：先按 MenuActionId 路由，
        // 以便旋转/对齐等参数不被丢弃；无法定位菜单项时回退到 OperationId。
        const UI::MenuActionId menuId = CommandCatalog::menuIdForCommandId(commandId);
        if (menuId != static_cast<UI::MenuActionId>(0))
        {
            OperationRouting::dispatch(menuId, m_bus, OperationSource::Menu, params);
            return;
        }
        const OperationId operation = CommandCatalog::operationForCommandId(commandId);
        if (operation == OperationId::None)
        {
            APR_WARNF("[OperationCommandSink] Unknown command: %s", qPrintable(commandId));
            return;
        }
        m_bus->run(operation, params, OperationSource::Menu);
    }

    bool OperationCommandSink::isRegistered(const QString& commandId) const
    {
        if (isShellCommand(commandId))
            return true;
        return CommandCatalog::operationForCommandId(commandId) != OperationId::None;
    }
}  // namespace Apr
