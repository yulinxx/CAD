#pragma once

#include "UiShell/Contract/WidgetFactory.h"

class QWidget;

namespace GL
{
    // 面板工厂
    class PanelFactory : public RD::IWidgetFactory
    {
    public:
        QWidget* create(const RD::PanelCreateRequest& info) override;
    };

    class CenterView : public RD::ICenterViewProvider
    {
    public:
        QWidget* createCenterView(QWidget* parent) override;
    };
}  // namespace GL
