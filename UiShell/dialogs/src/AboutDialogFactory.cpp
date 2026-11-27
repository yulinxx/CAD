#include "UiDialogs/AboutDialogs.h"

#include "AboutDialog.h"
#include "UiShell/Contract/ModalDialog.h"

namespace RD::about
{
    DialogResult AboutDialogFactory::openDialog(QWidget* parent, const QJsonObject& params)
    {
        // 工厂负责数据（params → AboutDlgData），对话框只负责显示。
        // 展示型对话框：只回传真实的关闭方式，没有数据。
        return runModalDialog(markDialog(new AboutDialog(AboutDlgData::setData(params), parent), params));
    }
}  // namespace RD::about