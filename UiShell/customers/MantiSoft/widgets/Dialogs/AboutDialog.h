#ifndef MS_ABOUT_DIALOG_H
#define MS_ABOUT_DIALOG_H

#include "UiDialogs/AboutDialogs.h"
#include "UiShell/Contract/DialogRegistry.h"

class QWidget;

namespace MS
{
    class AboutDialogFactory final : public AA::IDialogFactory
    {
    public:
        AA::DialogResult openDialog(QWidget* parent, const QJsonObject& params = {}) override;
    };
}  // namespace MS

#endif  // MS_ABOUT_DIALOG_H
