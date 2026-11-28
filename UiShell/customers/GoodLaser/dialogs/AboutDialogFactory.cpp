#include "AboutDialogFactory.h"

#include <QCoreApplication>
#include <QDebug>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

#include "AboutDialog.h"
#include "UiDialogs/AboutDialogs.h"
#include "UiShell/Contract/HostContext.h"
#include "UiShell/Contract/ModalDialog.h"

namespace GL
{
    namespace
    {
        // QML 的 Repeater 逐行显示
        QVariantMap row(const char* key, const QString& label, const QString& value, const QString& link = QString())
        {
            QVariantMap item;
            item.insert(QStringLiteral("key"), QString::fromLatin1(key));
            item.insert(QStringLiteral("label"), label);
            item.insert(QStringLiteral("value"), value);
            item.insert(QStringLiteral("link"), link);
            return item;
        }

        QString logoUrl(const QString& path)
        {
            if (path.isEmpty())
                return QString();
            if (path.startsWith(QLatin1Char(':')))
                return QStringLiteral("qrc") + path;
            return QUrl::fromLocalFile(path).toString();
        }

        QString websiteLink(const QString& website)
        {
            return website.contains(QLatin1String("://")) ? website : QStringLiteral("https://") + website;
        }

        // 翻译数据
        AA::about::AboutDlgData makeAboutData()
        {
            AA::about::AboutDlgData data;
            data.product = QCoreApplication::translate("about", "about.c.product");
            data.version = QStringLiteral("2.0.0");
            data.company = QCoreApplication::translate("about", "about.c.company");
            data.copyright = QCoreApplication::translate("about", "about.c.copyright");
            return data;
        }

        // params → QML 的 about 对象
        QVariantMap buildView(const AA::about::AboutDlgData& info, const QJsonObject& params)
        {
            QVariantMap view;
            view.insert(QStringLiteral("windowTitle"),
                info.title.isEmpty() ? QCoreApplication::translate("GoodLaserAbout", "About") : info.title);

            view.insert(QStringLiteral("banner"),
                QCoreApplication::translate("GoodLaserAbout", "Good Laser custom About dialog")); // GoodLaser 自定义关于对话框

            view.insert(QStringLiteral("product"), info.product);

            view.insert(QStringLiteral("version"), info.version.isEmpty()
                ? QString()
                : QCoreApplication::translate("GoodLaserAbout", "Version %1").arg(info.version)); // 版本 %1

            view.insert(QStringLiteral("logo"), logoUrl(info.logoPath));
            view.insert(QStringLiteral("closeText"), QCoreApplication::translate("GoodLaserAbout", "Close"));

            // 皮肤
            const bool dark = params.value(QString::fromLatin1(AA::DialogParam::Theme)).toString() == QLatin1String(AA::ThemeId::Dark);
            view.insert(QStringLiteral("theme"), QString::fromLatin1(dark ? AA::ThemeId::Dark : AA::ThemeId::Light));

            QVariantList rows;
            if (!info.versionDate.isEmpty())
                rows.append(row(AA::about::Param::VersionDate, QCoreApplication::translate("GoodLaserAbout", "Released"), info.versionDate));

            if (!info.company.isEmpty())
                rows.append(row(AA::about::Param::Company, QCoreApplication::translate("GoodLaserAbout", "Company"), info.company));

            if (!info.copyright.isEmpty())
                rows.append(row(AA::about::Param::Copyright, QCoreApplication::translate("GoodLaserAbout", "Copyright"), info.copyright));

            if (!info.website.isEmpty())
            {
                rows.append(row(AA::about::Param::Website, QCoreApplication::translate("GoodLaserAbout", "Website"),
                    info.website, websiteLink(info.website)));
            }

            if (!info.email.isEmpty())
            {
                rows.append(row(AA::about::Param::Email, QCoreApplication::translate("GoodLaserAbout", "Email"),
                    info.email, QStringLiteral("mailto:") + info.email));
            }

            if (!info.license.isEmpty())
                rows.append(row(AA::about::Param::License, QCoreApplication::translate("GoodLaserAbout", "License"), info.license));

            // GoodLaser 专属的两行说明。
            rows.append(row("diagnostics", QString(),
                QCoreApplication::translate("GoodLaserAbout", "Diagnostics: implemented by Good Laser"))); // 诊断信息

            rows.append(row("channel", QString(),
                QCoreApplication::translate("GoodLaserAbout", "Build channel: internal preview"))); // 构建通道

            view.insert(QStringLiteral("rows"), rows);
            return view;
        }
    }  // namespace

    AA::DialogResult AboutDialogFactory::openDialog(QWidget* parent, const QJsonObject& params)
    {
        AA::about::AboutDlgData info = makeAboutData();
        info.applyParams(params); 

        auto* dialog = AA::markDialog(new AboutDialog(buildView(info, params), parent), params);
        if (!dialog->isLoaded())
        {
            // QML 错误
            qWarning().noquote() << "GoodLaser: About dialog QML failed to load:" << dialog->loadError();
            delete dialog;
            return AA::DialogResult{};
        }

        // 展示型对话框：只回传关闭方式（Rejected），没有数据。
        return AA::runModalDialog(dialog);
    }
}  // namespace GL