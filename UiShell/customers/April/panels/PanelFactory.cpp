#include "PanelFactory.h"

#include <QString>
#include <QWidget>

namespace Apr
{
    QWidget* CenterView::createCenterView(QWidget* parent)
    {
        auto* view = new QWidget(parent);
        view->setObjectName(QStringLiteral("center.view"));
        // M2：空中央视图。现有 CAD 工作台在 M3 接入。
        return view;
    }
}  // namespace Apr
