#pragma once

#include "UiShell/Contract/WidgetFactory.h"

class QWidget;

namespace GL
{
    // 面板工厂
    class PanelFactory : public AA::IWidgetFactory
    {
    public:
        QWidget* create(const AA::PanelCreateRequest& info) override;
    };

    class CenterView : public AA::ICenterViewProvider
    {
    public:
        QWidget* createCenterView(QWidget* parent) override;
    };
}  // namespace GL
