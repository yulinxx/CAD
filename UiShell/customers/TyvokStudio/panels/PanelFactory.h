#pragma once

#include "UiShell/Contract/WidgetFactory.h"

class QWidget;

namespace TS
{
    // TyvokStudio 的面板工厂（绿色工作台风格 + 右侧列表）和中央区占位视图。
    // 只依赖 contract 的 AA::PanelCreateRequest，不引用装配层或具体区域类型。
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
}  // namespace TS
