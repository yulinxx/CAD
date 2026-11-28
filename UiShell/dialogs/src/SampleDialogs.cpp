#include "UiDialogs/SampleDialogs.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDialog>
#include <QMessageBox>

#include "SampleParamsDialog.h"
#include "UiShell/Contract/ModalDialog.h"

namespace AA::samples
{
    DialogResult SampleParamsDialogFactory::openDialog(QWidget* parent, const QJsonObject& params)
    {
        // 确定时把表单读回 SampleParams，作为 DialogResult.data 交给命令。
        // markDialog：按 params 里的品牌系列套用客户配色（共享对话框自己不知道属于哪个客户）。
        return runModalDialog(markDialog(new SampleParamsDialog(params, parent), params),
            [](SampleParamsDialog* dialog) { return dialog->values().toJson(); });
    }

    QString methodDisplayName(const QString& methodId)
    {
        if (methodId == QLatin1String("bisection"))
            return QCoreApplication::translate("SampleDialogs", "Bisection");
        return QCoreApplication::translate("SampleDialogs", "Newton");
    }

    QString describeResult(const SampleParams& params, const SampleResult& result)
    {
        if (!result.valid)
            return QCoreApplication::translate("SampleDialogs", "Invalid parameters: %1").arg(result.error);

        const QString state = result.converged
            ? QCoreApplication::translate("SampleDialogs", "converged")
            : QCoreApplication::translate("SampleDialogs", "not converged");

        return QCoreApplication::translate("SampleDialogs", "sqrt(%1) = %2\nMethod: %3, iterations: %4, %5")
            .arg(QString::number(params.target, 'g', 10))
            .arg(QString::number(result.value, 'g', 12))
            .arg(methodDisplayName(params.method))
            .arg(result.iterations)
            .arg(state);
    }

    bool runSampleCommand(const QString& commandId, const QJsonObject& args, QWidget* parent)
    {
        if (commandId != QLatin1String(kSampleRunCmdId))
            return false;

        // 算法只认参数：args 来自对话框的 DialogResult.data。
        const SampleParams params = SampleParams::fromJson(args);
        const SampleResult result = solveSquareRoot(params);

        QMessageBox::information(parent ? parent : QApplication::activeWindow(),
            QCoreApplication::translate("SampleDialogs", "Sample algorithm"),
            describeResult(params, result));
        return true;
    }
}  // namespace AA::samples