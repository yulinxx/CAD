#pragma once

#include "UiShell/Contract/WidgetFactory.h"

namespace GL
{
    // 返回 QQuickWidget 的工厂
    class QmlPanelFactory : public RD::IWidgetFactory
    {
    public:
        QWidget* create(const RD::PanelCreateRequest& info) override;
    };
}  // namespace GL
