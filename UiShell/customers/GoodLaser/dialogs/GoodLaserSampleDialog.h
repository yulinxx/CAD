#pragma once

#include <QJsonObject>

#include "UiShell/Contract/DialogRegistry.h"

class QWidget;

namespace GL
{
    // 示例参数对话框
    class GoodLaserSampleDialogFactory final : public RD::IDialogFactory
    {
    public:
        RD::DialogResult openDialog(QWidget* parent, const QJsonObject& params = {}) override;
    };
}  // namespace GL
