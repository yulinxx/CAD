#include "PanelFactory.h"

#include <QWidget>

#include "UiShell/Contract/Retranslatable.h"

#include "../qml/QmlPanelWidget.h"

namespace GL
{
    QWidget* PanelFactory::create(const RD::PanelCreateRequest& info)
    {
        // 面板用 QML
        const bool isStatus = (info.strPanelId == QLatin1String("status"));
        auto* panel = new QmlPanelWidget(info.context, info.strPanelId, info.parent);
        panel->setObjectName(QStringLiteral("panel:") + info.strPanelId);

        RD::markPanel(panel,
            isStatus ? QStringLiteral("panelStatus") : QStringLiteral("panel"),
            QStringLiteral("goodlaser"));

        return panel;
    }

    // CenterView

    QWidget* CenterView::createCenterView(QWidget* parent)
    {
        auto* view = new QmlPanelWidget(RD::HostContext(),
            QStringLiteral("center"), parent);
        view->setObjectName(QStringLiteral("center.view"));
        view->setMinimumSize(420, 300);

        RD::markPanel(view, QStringLiteral("centerView"), QStringLiteral("goodlaser"));
        return view;
    }
}  // namespace GL