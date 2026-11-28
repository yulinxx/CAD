#include "Assembly/Host/ThemeController.h"

#include <QApplication>
#include <QColor>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QStringList>
#include <QStyle>

#include "UiShell/Contract/HostContext.h"

namespace AA
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
        if (themeId != QLatin1String(ThemeId::Dark)
            && themeId != QLatin1String(ThemeId::HighContrast))
        {
            return QApplication::style()->standardPalette();
        }

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

        // 派生皮肤（blue / slate / system / high_contrast）先套基础皮肤，
        // 再叠加差异规则；三层来源都遵循同一顺序，后写入的优先级更高。
        const QString baseId = ThemeCatalog::baseId(themeId);
        const QStringList ids = (baseId == themeId)
            ? QStringList{ themeId } : QStringList{ baseId, themeId };
        QStringList sheets;

        for (const QString& id : ids)
        {
            const QString frameworkSheet = readStyleSheet(
                QStringLiteral(":/UiShell/styles/") + id + QStringLiteral(".qss"));
            if (!frameworkSheet.isEmpty())
                sheets.append(frameworkSheet);
        }

        if (!customPrefix.isEmpty())
        {
            QString strPrefix = customPrefix;
            if (!strPrefix.startsWith(QLatin1Char(':')))
                strPrefix.prepend(QLatin1Char(':'));

            for (const QString& id : ids)
            {
                const QString customerSheet = readStyleSheet(
                    strPrefix + QStringLiteral("/styles/") + id + QStringLiteral(".qss"));
                if (!customerSheet.isEmpty())
                    sheets.append(customerSheet);
            }
        }

        // 外置 profile：<profileDir>/styles/<id>.qss 覆盖内置与客户 qrc 皮肤。
        if (!profileDir.isEmpty())
        {
            for (const QString& id : ids)
            {
                const QString externalSheet = readStyleSheet(
                    QDir(profileDir).absoluteFilePath(
                        QStringLiteral("styles/") + id + QStringLiteral(".qss")));
                if (!externalSheet.isEmpty())
                    sheets.append(externalSheet);
            }
        }

        if (sheets.isEmpty())
            return false;

        // 先换调色板再换样式表,两处颜色成对维护
        app->setPalette(paletteFor(themeId));
        app->setStyleSheet(sheets.join(QStringLiteral("\n")));
        return true;
    }
}  // namespace AA