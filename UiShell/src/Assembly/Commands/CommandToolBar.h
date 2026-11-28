#pragma once

#include "UiShell/Contract/WidgetFactory.h"

namespace AA
{
    // shell 自带的通用工具栏面板
    inline constexpr const char* kToolBarFactoryId = "msui.toolbar";

    class CommandToolBarFactory final : public IWidgetFactory
    {
    public:
        QWidget* create(const PanelCreateRequest& info) override;
        PropsValidation validateProps(const QJsonObject& props) const override;
    };
}  // namespace AA
