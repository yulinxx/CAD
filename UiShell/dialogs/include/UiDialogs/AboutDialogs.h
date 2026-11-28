#pragma once

#include <QJsonObject>
#include <QString>

#include "UiShell/Contract/DialogRegistry.h"

namespace AA::about
{
    // 关于对话框
    inline constexpr const char* kAboutDlgId = "dialog.about";
    inline constexpr const char* kAboutCmdId = "help.about";

    namespace Param
    {
        // 与 AA::DialogParam 共用品牌字段名，避免 profile 注入与对话框解析漂移。
        constexpr const char* Title = "title";
        constexpr const char* Product = DialogParam::Product;
        constexpr const char* Version = "version";
        constexpr const char* VersionDate = "versionDate";
        constexpr const char* Copyright = DialogParam::Copyright;
        constexpr const char* Company = DialogParam::Company;
        constexpr const char* Website = DialogParam::Website;
        constexpr const char* Email = DialogParam::Email;
        constexpr const char* License = DialogParam::License;
        constexpr const char* LogoPath = DialogParam::LogoPath;
    }  // namespace Param

    // 关于对话框的数据
    struct AboutDlgData
    {
        QString title;
        QString product;
        QString version;
        QString versionDate;
        QString copyright;
        QString company;
        QString website;
        QString email;
        QString license;   // 可含换行。
        QString logoPath;  // 绝对路径或 qrc 路径（":/..."）。

        // 解析 params
        static AboutDlgData setData(const QJsonObject& params)
        {
            const auto text = [&params](const char* key) {
                return params.value(QString::fromLatin1(key)).toString().trimmed();
                };

            AboutDlgData dlgData;
            dlgData.title = text(Param::Title);
            dlgData.product = text(Param::Product);
            dlgData.version = text(Param::Version);

            if (dlgData.version.isEmpty())
                dlgData.version = text(DialogParam::AppVersion);

            dlgData.versionDate = text(Param::VersionDate);
            dlgData.copyright = text(Param::Copyright);
            dlgData.company = text(Param::Company);
            dlgData.website = text(Param::Website);
            dlgData.email = text(Param::Email);
            dlgData.license = text(Param::License);
            dlgData.logoPath = text(Param::LogoPath);

            return dlgData;
        }

        // 用 params 中非空的字段覆盖已有值：工厂自翻译的默认数据可被宿主/测试显式覆盖。
        AboutDlgData& applyParams(const QJsonObject& params)
        {
            const auto text = [&params](const char* key) {
                return params.value(QString::fromLatin1(key)).toString().trimmed();
                };
            const auto setIf = [this, &text](QString& field, const char* key) {
                const QString value = text(key);
                if (!value.isEmpty())
                    field = value;
                };
            setIf(title, Param::Title);
            setIf(product, Param::Product);
            setIf(version, Param::Version);
            setIf(versionDate, Param::VersionDate);
            setIf(copyright, Param::Copyright);
            setIf(company, Param::Company);
            setIf(website, Param::Website);
            setIf(email, Param::Email);
            setIf(license, Param::License);
            setIf(logoPath, Param::LogoPath);
            return *this;
        }
    };

    // 默认实现（Widgets）：不需要自定义界面的客户注册它。无状态，实例由客户的部件持有者持有。
    class AboutDialogFactory final : public IDialogFactory
    {
    public:
        DialogResult openDialog(QWidget* parent, const QJsonObject& params = {}) override;
    };
}  // namespace AA::about
