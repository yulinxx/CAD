#pragma once

#include "UiShell/Contract/DialogRegistry.h"

namespace GL
{
    // 关于对话框工厂
    class AboutDialogFactory final : public AA::IDialogFactory
    {
    public:
        AA::DialogResult openDialog(QWidget* parent, const QJsonObject& params = {}) override;
    };
}  // namespace GL
