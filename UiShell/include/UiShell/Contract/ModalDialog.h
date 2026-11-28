#pragma once

#include <QJsonObject>
#include <QPointer>
#include <QString>
#include <QVariant>

#include "UiShell/Contract/DialogRegistry.h"

namespace AA
{
    // 模态方式调用对话框
    template<typename Dialog, typename Collect>
    DialogResult runModalDialog(Dialog* dialog, Collect collect)
    {
        DialogResult result;
        if (!dialog)
            return result;

        QPointer<Dialog> guard(dialog);
        const int code = dialog->exec();
        if (!guard)
        {
            result.status = DialogResult::Rejected;
            return result;
        }

        if (code == Dialog::Accepted)
        {
            result.status = DialogResult::Accepted;
            result.data = collect(guard.data());
        }
        else
        {
            result.status = DialogResult::Rejected;
        }

        delete guard.data();
        return result;
    }

    // 标记话框添
    template<typename Dialog>
    Dialog* markDialog(Dialog* dialog, const QJsonObject& params)
    {
        if (dialog)
        {
            dialog->setProperty("msuiRole", QStringLiteral("dialog"));
            dialog->setProperty("msuiSeries", params.value(QString::fromLatin1(DialogParam::Series)).toString());
        }
        return dialog;
    }

    // 展示型对话框重载
    template<typename Dialog>
    DialogResult runModalDialog(Dialog* dialog)
    {
        return runModalDialog(dialog, [](Dialog*) { return QJsonObject(); });
    }
}  // namespace AA
