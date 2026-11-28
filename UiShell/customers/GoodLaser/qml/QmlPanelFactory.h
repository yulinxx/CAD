#pragma once

#include "UiShell/Contract/WidgetFactory.h"

namespace GL
{
    // 返回 QQuickWidget 的工厂
    class QmlPanelFactory : public AA::IWidgetFactory
    {
    public:
        QWidget* create(const AA::PanelCreateRequest& info) override;
    };
}  // namespace GL
