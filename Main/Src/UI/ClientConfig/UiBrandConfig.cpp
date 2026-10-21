#include "UiBrandConfig.h"

#include <QDate>
#include <QJsonObject>
#include <QJsonValue>
#include <QStringList>

namespace
{
QString readString(const QJsonObject& root, const char* key)
{
    return root.value(QLatin1String(key)).toString().trimmed();
}
}  // namespace

UiBrandConfig UiBrandConfig::fromJson(const QJsonObject& root, const QString& fallbackClientId)
{
    UiBrandConfig config;
    config.clientId = readString(root, "clientId");
    if (config.clientId.isEmpty())
    {
        config.clientId = fallbackClientId;
    }

    config.productName = readString(root, "productName");
    config.companyName = readString(root, "companyName");
    config.appId = readString(root, "appId");
    config.organizationDomain = readString(root, "organizationDomain");
    config.version = readString(root, "version");
    config.windowTitle = readString(root, "windowTitle");
    config.tagline = readString(root, "tagline");
    config.copyright = readString(root, "copyright");
    config.website = readString(root, "website");
    config.supportEmail = readString(root, "supportEmail");
    config.aboutText = readString(root, "aboutText");
    config.appIcon = readString(root, "appIcon");
    config.logo = readString(root, "logo");
    config.aboutLogo = readString(root, "aboutLogo");
    config.splash = readString(root, "splash");
    config.iconDirectory = readString(root, "iconDirectory");
    config.theme = readString(root, "theme").toLower();
    config.styleSheet = readString(root, "styleSheet");

    const int duration = root.value(QStringLiteral("splashDurationMs")).toInt(config.splashDurationMs);
    config.splashDurationMs = duration < 0 ? 0 : duration;

    if (!isValidThemeName(config.theme))
    {
        config.theme.clear();
    }
    return config;
}

bool UiBrandConfig::isValid() const
{
    return !productName.isEmpty();
}

QString UiBrandConfig::effectiveWindowTitle() const
{
    return !windowTitle.isEmpty() ? windowTitle : productName;
}

QString UiBrandConfig::effectiveCopyright() const
{
    if (!copyright.isEmpty())
    {
        return copyright;
    }
    const QString owner = !companyName.isEmpty() ? companyName : productName;
    return QStringLiteral("Copyright (C) %1 %2").arg(QDate::currentDate().year()).arg(owner);
}

QString UiBrandConfig::effectiveAppId() const
{
    if (!appId.isEmpty())
    {
        return appId;
    }

    QString generated = companyName + productName;
    generated.remove(QChar(' '));
    generated.remove(QChar('-'));
    return generated.isEmpty() ? QStringLiteral("SanYiCAD") : generated;
}

bool UiBrandConfig::isValidThemeName(const QString& theme)
{
    static const QStringList kThemes{
        QStringLiteral("light"),
        QStringLiteral("dark"),
        QStringLiteral("blue"),
        QStringLiteral("slate"),
        QStringLiteral("high_contrast"),
        QStringLiteral("system"),
    };
    return theme.isEmpty() || kThemes.contains(theme);
}
