#pragma once

#include "UiShell/Contract/WidgetFactory.h"

class QWidget;

namespace Apr
{
    // M2：April 客户的中央视图。
    // 现阶段是占位的空 QWidget；M3 改为返回现有 CAD 工作台/视口宿主。
    class CenterView : public RD::ICenterViewProvider
    {
    public:
        QWidget* createCenterView(QWidget* parent) override;
    };
}  // namespace Apr
