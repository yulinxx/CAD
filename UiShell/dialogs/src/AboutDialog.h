#pragma once

#include <QDialog>

#include "UiDialogs/AboutDialogs.h"

namespace AA::about
{
    // 默认 关于对话框
    class AboutDialog : public QDialog
    {
    public:
        explicit AboutDialog(const AboutDlgData& info, QWidget* parent = nullptr);
    };
}  // namespace AA::about
