#include "QmlPanelFactory.h"

#include <QString>
#include <QWidget>

#include "QmlPanelWidget.h"

namespace GL
{
    QWidget* QmlPanelFactory::create(const RD::PanelCreateRequest& info)
    {
        auto* panel = new QmlPanelWidget(info.context, info.strPanelId, info.parent);
        panel->setObjectName(QStringLiteral("panel:") + info.strPanelId);
        return panel;
    }
}  // namespace GL