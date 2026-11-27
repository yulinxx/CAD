#pragma once

#include "UiShell/Contract/DialogRegistry.h"

namespace GL
{
    // 关于对话框工厂
    class AboutDialogFactory final : public RD::IDialogFactory
    {
    public:
        RD::DialogResult openDialog(QWidget* parent, const QJsonObject& params = {}) override;
    };
}  // namespace GL
