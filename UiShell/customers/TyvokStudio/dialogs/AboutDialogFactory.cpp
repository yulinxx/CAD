#include "AboutDialogFactory.h"
#include "AboutDialog.h"
#include "UiDialogs/AboutDialogs.h"
#include "UiShell/Contract/ModalDialog.h"

#include <QCoreApplication>

namespace TS
{
    namespace
    {
        // 工厂自翻译的默认数据（取代原 ui.json dialogBindings 的 about params）。
        // 文本由工厂翻译；logoPath 仍由 params 传入（相对路径，由 resolveParams 按部署方式解析）。
        AA::about::AboutDlgData makeAboutData()
        {
            AA::about::AboutDlgData data;
            data.product = QCoreApplication::translate("about", "about.b.product");
            data.version = QStringLiteral("5.0.1");
            data.versionDate = QStringLiteral("2026-02-18");
            data.copyright = QCoreApplication::translate("about", "about.b.copyright");
            data.company = QCoreApplication::translate("about", "about.b.company");
            data.website = QStringLiteral("https://example.com/tyvokstudio");
            data.email = QStringLiteral("support@tyvokstudio.example");
            data.license = QCoreApplication::translate("about", "about.b.license");
            return data;
        }
    }  // namespace

    AA::DialogResult AboutDialogFactory::openDialog(QWidget* parent, const QJsonObject& params)
    {
        AA::about::AboutDlgData info = makeAboutData();
        info.applyParams(params);  // params 覆盖默认值（测试传参 / 宿主注入 logoPath）。
        return AA::runModalDialog(AA::markDialog(new AboutDialog(info, parent), params));
    }
}  // namespace TS