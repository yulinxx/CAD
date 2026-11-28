#include "Assembly/Commands/CommandToolBar.h"

#include <QAction>
#include <QDebug>
#include <QJsonArray>
#include <QSize>
#include <QToolBar>
#include <QToolButton>

#include "UiShell/Contract/IShellHost.h"

namespace AA
{
    namespace
    {
        const QLatin1String kSeparator("|");

        struct StyleName
        {
            const char* name;
            Qt::ToolButtonStyle style;
        };

        // "auto" 不在表里：按每个按钮有没有图标单独决定。
        const StyleName kStyles[] = {
            { "icon", Qt::ToolButtonIconOnly },
            { "text", Qt::ToolButtonTextOnly },
            { "textBesideIcon", Qt::ToolButtonTextBesideIcon },
            { "textUnderIcon", Qt::ToolButtonTextUnderIcon },
        };

        bool styleFromName(const QString& name, Qt::ToolButtonStyle* style)
        {
            for (const StyleName& entry : kStyles)
            {
                if (name == QLatin1String(entry.name))
                {
                    *style = entry.style;
                    return true;
                }
            }
            return false;
        }
    }  // namespace

    PropsValidation CommandToolBarFactory::validateProps(const QJsonObject& props) const
    {
        PropsValidation result;
        const QJsonValue commands = props.value(QStringLiteral("commands"));
        if (!commands.isArray())
        {
            result.bValid = false;
            result.strMsg = QStringLiteral("toolbar 'commands' must be an array of command ids or \"|\"");
            return result;
        }
        for (const QJsonValue& item : commands.toArray())
        {
            if (!item.isString() || item.toString().trimmed().isEmpty())
            {
                result.bValid = false;
                result.strMsg = QStringLiteral("toolbar 'commands' entries must be non-empty strings");
                return result;
            }
        }

        const QString style = props.value(QStringLiteral("style")).toString(QStringLiteral("auto"));
        Qt::ToolButtonStyle ignored = Qt::ToolButtonIconOnly;
        if (style != QLatin1String("auto") && !styleFromName(style, &ignored))
        {
            result.bValid = false;
            result.strMsg = QStringLiteral("toolbar 'style' must be auto / icon / text / textBesideIcon / textUnderIcon");
            return result;
        }

        const QJsonValue iconSize = props.value(QStringLiteral("iconSize"));
        if (!iconSize.isUndefined() && (!iconSize.isDouble() || iconSize.toInt() < 8 || iconSize.toInt() > 128))
        {
            result.bValid = false;
            result.strMsg = QStringLiteral("toolbar 'iconSize' must be an integer in 8..128");
        }
        return result;
    }

    QWidget* CommandToolBarFactory::create(const PanelCreateRequest& info)
    {
        auto* toolBar = new QToolBar(info.parent);
        toolBar->setObjectName(QStringLiteral("panel:") + info.strPanelId);
        toolBar->setProperty("msuiRole", QStringLiteral("toolbar"));  // 配色交给皮肤 QSS
        toolBar->setMovable(false);
        toolBar->setFloatable(false);

        const int iconSize = info.props.value(QStringLiteral("iconSize")).toInt(0);
        if (iconSize > 0)
            toolBar->setIconSize(QSize(iconSize, iconSize));

        Qt::ToolButtonStyle fixedStyle = Qt::ToolButtonIconOnly;
        const bool autoStyle =
            !styleFromName(info.props.value(QStringLiteral("style")).toString(QStringLiteral("auto")), &fixedStyle);

        for (const QJsonValue& item : info.props.value(QStringLiteral("commands")).toArray())
        {
            const QString commandId = item.toString().trimmed();
            if (commandId == kSeparator)
            {
                toolBar->addSeparator();
                continue;
            }

            // 标题 / 快捷键 / 可用 / 勾选状态自动一致
            QAction* action = info.host ? info.host->commandAction(commandId) : nullptr;
            if (!action)
            {
                qWarning().noquote() << QStringLiteral("toolbar '%1': unknown command '%2', skipped")
                    .arg(info.strPanelId, commandId);
                continue;
            }

            toolBar->addAction(action);
            if (auto* button = qobject_cast<QToolButton*>(toolBar->widgetForAction(action)))
            {
                button->setObjectName(QStringLiteral("msui.toolbutton.") + commandId);
                const Qt::ToolButtonStyle style =
                    autoStyle ? (action->icon().isNull() ? Qt::ToolButtonTextOnly : Qt::ToolButtonIconOnly) : fixedStyle;
                button->setToolButtonStyle(style);
            }
        }

        return toolBar;
    }
}  // namespace AA