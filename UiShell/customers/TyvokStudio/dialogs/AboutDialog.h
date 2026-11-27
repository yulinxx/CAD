#pragma once

#include <QDialog>
#include <QString>

#include "UiDialogs/AboutDialogs.h"

namespace TS
{
    // 关于对话框
    class AboutDialog : public QDialog
    {
    public:
        explicit AboutDialog(const RD::about::AboutDlgData& info, QWidget* parent = nullptr);

    public:
        QString plainText() const;

    private:
        RD::about::AboutDlgData m_info;
    };
}  // namespace TS
