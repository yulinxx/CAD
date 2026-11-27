#include "Assembly/Host/ThemeController.h"

#include <QApplication>
#include <QColor>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QStringList>
#include <QStyle>

#include "UiShell/Contract/HostContext.h"

namespace RD
{
    namespace
    {
        // 读样式表
        QString readStyleSheet(const QString& path)
        {
            QFile file(path);
            if (!file.open(QIODevice::ReadOnly))
                return QString();

            return QString::fromUtf8(file.readAll());
        }
    }  // namespace

    QPalette ThemeController::paletteFor(const QString& themeId)
    {
        if (themeId != QLatin1String(ThemeId::Dark))
            return QApplication::style()->standardPalette();

        QPalette palette;
        palette.setColor(QPalette::Window, QColor(QStringLiteral("#1b212a")));
        palette.setColor(QPalette::WindowText, QColor(QStringLiteral("#e6ebf2")));
        palette.setColor(QPalette::Base, QColor(QStringLiteral("#1b222c")));
        palette.setColor(QPalette::AlternateBase, QColor(QStringLiteral("#202834")));
        palette.setColor(QPalette::Text, QColor(QStringLiteral("#e6ebf2")));
        palette.setColor(QPalette::Button, QColor(QStringLiteral("#2a3341")));
        palette.setColor(QPalette::ButtonText, QColor(QStringLiteral("#e6ebf2")));
        palette.setColor(QPalette::Highlight, QColor(QStringLiteral("#2f4a6b")));
        palette.setColor(QPalette::HighlightedText, QColor(QStringLiteral("#ffffff")));
        palette.setColor(QPalette::ToolTipBase, QColor(QStringLiteral("#202834")));
        palette.setColor(QPalette::ToolTipText, QColor(QStringLiteral("#e6ebf2")));
        palette.setColor(QPalette::Disabled, QPalette::WindowText, QColor(QStringLiteral("#6b7684")));
        palette.setColor(QPalette::Disabled, QPalette::Text, QColor(QStringLiteral("#6b7684")));
        palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(QStringLiteral("#6b7684")));
        return palette;
    }

    bool ThemeController::apply(const QString& themeId,
                                const QString& customPrefix,
                                const QString& profileDir)
    {
        auto* app = qobject_cast<QApplication*>(QCoreApplication::instance());
        if (!app)
            return false;

        // 皮肤有 框架 与 客户 两個
        QStringList sheets;

        const QString frameworkSheet = readStyleSheet(
            QStringLiteral(":/UiShell/styles/") + themeId + QStringLiteral(".qss"));

        if (!frameworkSheet.isEmpty())
            sheets.append(frameworkSheet);

        if (!customPrefix.isEmpty())
        {
            QString strPrefix = customPrefix;

            if (!strPrefix.startsWith(QLatin1Char(':')))
                strPrefix.prepend(QLatin1Char(':'));

            const QString customerSheet = readStyleSheet(
                strPrefix + QStringLiteral("/styles/") + themeId + QStringLiteral(".qss"));

            if (!customerSheet.isEmpty())
                sheets.append(customerSheet);
        }

        // 外置 profile：<profileDir>/styles/<themeId>.qss 覆盖内置与客户 qrc 皮肤。
        if (!profileDir.isEmpty())
        {
            const QString externalSheet = readStyleSheet(
                QDir(profileDir).absoluteFilePath(
                    QStringLiteral("styles/") + themeId + QStringLiteral(".qss")));
            if (!externalSheet.isEmpty())
                sheets.append(externalSheet);
        }

        if (sheets.isEmpty())
            return false;

        // 先换调色板再换样式表,两处颜色成对维护
        app->setPalette(paletteFor(themeId));
        app->setStyleSheet(sheets.join(QStringLiteral("\n")));
        return true;
    }
}  // namespace RD