#include "UiBrandManager.h"

#include "UiClientContext.h"

#include "Log/SyLogger.h"
#include "UI/IconHelper.h"
#include "UI/ThemeManager.h"
#include "VersionInfo.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QIcon>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QStringList>

namespace
{
QString normalizedPath(const QString& path)
{
    return QDir::cleanPath(path);
}

QString joinPath(const QString& dir, const QString& relative)
{
    if (dir.isEmpty() || relative.isEmpty())
    {
        return relative;
    }
    if (dir.endsWith(QChar('/')))
    {
        return dir + relative;
    }
    return dir + QStringLiteral("/") + relative;
}

QString resourceDirectory(const QString& filePath)
{
    const int slash = filePath.lastIndexOf(QChar('/'));
    return slash > 0 ? filePath.left(slash) : QString();
}

AppTheme themeFromName(const QString& name)
{
    if (name == QLatin1String("dark"))
    {
        return AppTheme::Dark;
    }
    if (name == QLatin1String("blue"))
    {
        return AppTheme::Blue;
    }
    if (name == QLatin1String("slate"))
    {
        return AppTheme::Slate;
    }
    if (name == QLatin1String("high_contrast"))
    {
        return AppTheme::HighContrast;
    }
    if (name == QLatin1String("system"))
    {
        return AppTheme::System;
    }
    return AppTheme::Light;
}
}  // namespace

UiBrandManager& UiBrandManager::instance()
{
    static UiBrandManager manager;
    return manager;
}

QString UiBrandManager::executableDirectory(int argc, char** argv)
{
    if (argc > 0 && argv && argv[0])
    {
        const QFileInfo info(QString::fromLocal8Bit(argv[0]));
        const QString dir = info.absolutePath();
        if (!dir.isEmpty())
        {
            return dir;
        }
    }
    if (QCoreApplication::instance())
    {
        return QCoreApplication::applicationDirPath();
    }
    return {};
}

QString UiBrandManager::resolveClientIdFromProcess(int argc, char** argv)
{
    for (int i = 1; i < argc; ++i)
    {
        const QString arg = QString::fromLocal8Bit(argv[i]);
        if (arg == QLatin1String("--client") && i + 1 < argc)
        {
            const QString value = QString::fromLocal8Bit(argv[i + 1]).trimmed();
            if (!value.isEmpty())
            {
                return value;
            }
        }
        if (arg.startsWith(QLatin1String("--client=")))
        {
            const QString value = arg.mid(QStringLiteral("--client=").size()).trimmed();
            if (!value.isEmpty())
            {
                return value;
            }
        }
    }

    if (qEnvironmentVariableIsSet("SANYI_CLIENT_ID"))
    {
        const QString fromEnv = QString::fromUtf8(qgetenv("SANYI_CLIENT_ID")).trimmed();
        if (!fromEnv.isEmpty())
        {
            return fromEnv;
        }
    }

    const QString fromFile = UiClientContext::readClientIdFile(executableDirectory(argc, argv));
    if (!fromFile.isEmpty())
    {
        return fromFile;
    }

    if (QCoreApplication::instance())
    {
        return UiClientContext::instance().clientId();
    }
    return QString::fromUtf8(UiClientContext::kDefaultClientId);
}

bool UiBrandManager::loadForProcess(int argc, char** argv)
{
    const QString clientId = resolveClientIdFromProcess(argc, argv);
    const QString appDir = executableDirectory(argc, argv);

    const QString envDir = QString::fromLocal8Bit(qgetenv("SANYI_BRAND_DIR")).trimmed();
    if (!envDir.isEmpty() && loadFromFile(joinPath(envDir, QStringLiteral("brand.json")), clientId))
    {
        SY_INFOF("[UiBrand] Loaded brand from SANYI_BRAND_DIR: %s", qPrintable(m_brandDirectory));
        return true;
    }

    if (!appDir.isEmpty() &&
        loadFromFile(joinPath(appDir, QStringLiteral("branding/%1/brand.json").arg(clientId)), clientId))
    {
        SY_INFOF("[UiBrand] Loaded external brand for client '%s'", qPrintable(clientId));
        return true;
    }

    if (loadFromFile(QStringLiteral(":/brand/%1/brand.json").arg(clientId), clientId))
    {
        SY_INFOF("[UiBrand] Loaded embedded brand for client '%s'", qPrintable(clientId));
        return true;
    }

    if (loadFromFile(QStringLiteral(":/brand/default/brand.json"), clientId))
    {
        SY_INFO("[UiBrand] Client brand missing, using embedded default brand");
        return true;
    }

    applyFallback(clientId);
    return false;
}

bool UiBrandManager::loadForClient(const QString& clientId, const QString& explicitBrandDir)
{
    const QString id = clientId.trimmed().isEmpty() ? QString::fromUtf8(UiClientContext::kDefaultClientId)
                                                    : clientId.trimmed();
    if (!explicitBrandDir.isEmpty() &&
        loadFromFile(joinPath(explicitBrandDir, QStringLiteral("brand.json")), id))
    {
        return true;
    }
    if (loadFromFile(QStringLiteral(":/brand/%1/brand.json").arg(id), id))
    {
        return true;
    }
    if (loadFromFile(QStringLiteral(":/brand/default/brand.json"), id))
    {
        return true;
    }
    applyFallback(id);
    return false;
}

bool UiBrandManager::loadFromFile(const QString& filePath, const QString& clientId)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        return false;
    }

    const QByteArray payload = file.readAll();
    QJsonParseError error{};
    const QJsonDocument doc = QJsonDocument::fromJson(payload, &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject())
    {
        SY_WARNF("[UiBrand] Invalid brand json '%s': %s", qPrintable(filePath), qPrintable(error.errorString()));
        return false;
    }

    UiBrandConfig config = UiBrandConfig::fromJson(doc.object(), clientId);
    config.clientId = clientId;
    if (!config.isValid())
    {
        SY_WARNF("[UiBrand] Brand json '%s' has empty productName, ignored", qPrintable(filePath));
        return false;
    }

    m_config = config;
    m_brandDirectory = resourceDirectory(normalizedPath(filePath));
    m_hasBrandFile = true;
    UiClientContext::instance().setClientIdOverride(clientId);
    return true;
}

void UiBrandManager::applyFallback(const QString& clientId)
{
    m_config = UiBrandConfig{};
    m_config.clientId = clientId;
    m_config.productName = QString::fromStdString(MainApp::appName());
    m_config.companyName = QString::fromStdString(MainApp::organizationName());
    m_config.organizationDomain = QString::fromStdString(MainApp::organizationDomain());
    m_config.version = QString::fromStdString(MainApp::appVersion());
    m_config.appId = QString::fromStdString(MainApp::appName());
    m_config.theme.clear();
    m_brandDirectory.clear();
    m_hasBrandFile = false;
    UiClientContext::instance().setClientIdOverride(clientId);
    SY_INFOF("[UiBrand] No brand json for client '%s', using built-in identity", qPrintable(clientId));
}

QString UiBrandManager::productName() const
{
    return m_config.productName.isEmpty() ? QString::fromStdString(MainApp::appName()) : m_config.productName;
}

QString UiBrandManager::companyName() const
{
    return m_config.companyName.isEmpty() ? QString::fromStdString(MainApp::organizationName()) : m_config.companyName;
}

QString UiBrandManager::windowTitle() const
{
    const QString title = m_config.effectiveWindowTitle();
    return title.isEmpty() ? productName() : title;
}

QString UiBrandManager::copyright() const
{
    return m_config.effectiveCopyright();
}

QString UiBrandManager::resolveAssetFile(const QString& relative) const
{
    if (relative.isEmpty())
    {
        return {};
    }
    if (relative.startsWith(QLatin1String(":/")) || relative.startsWith(QLatin1String("qrc:")))
    {
        return relative;
    }
    const QFileInfo info(relative);
    if (info.isAbsolute())
    {
        return relative;
    }

    if (!m_brandDirectory.isEmpty())
    {
        const QString external = QDir(m_brandDirectory).filePath(relative);
        if (QFile::exists(external))
        {
            return external;
        }
    }

    const QString clientAsset = QStringLiteral(":/brand/%1/%2").arg(m_config.clientId, relative);
    if (QFile::exists(clientAsset))
    {
        return clientAsset;
    }

    const QString defaultAsset = QStringLiteral(":/brand/default/%1").arg(relative);
    if (QFile::exists(defaultAsset))
    {
        return defaultAsset;
    }
    return {};
}

QString UiBrandManager::assetPath(const QString& relative) const
{
    if (relative.isEmpty())
    {
        return {};
    }
    const QString resolved = resolveAssetFile(relative);
    return resolved.isEmpty() ? relative : resolved;
}

void UiBrandManager::applyApplicationIdentity() const
{
    QCoreApplication::setOrganizationName(companyName());
    QCoreApplication::setOrganizationDomain(m_config.organizationDomain.isEmpty()
                                                ? QString::fromStdString(MainApp::organizationDomain())
                                                : m_config.organizationDomain);
    QCoreApplication::setApplicationName(m_config.effectiveAppId());
    QCoreApplication::setApplicationVersion(m_config.version.isEmpty() ? QString::fromStdString(MainApp::appVersion())
                                                                       : m_config.version);
}

void UiBrandManager::applyApplicationProperties() const
{
    auto* app = QCoreApplication::instance();
    if (!app)
    {
        return;
    }

    app->setProperty("sanyi.brand.clientId", m_config.clientId);
    app->setProperty("sanyi.brand.productName", productName());
    app->setProperty("sanyi.brand.companyName", companyName());
    app->setProperty("sanyi.brand.copyright", copyright());
    app->setProperty("sanyi.brand.website", m_config.website);
    app->setProperty("sanyi.brand.supportEmail", m_config.supportEmail);
    app->setProperty("sanyi.brand.tagline", m_config.tagline);
    app->setProperty("sanyi.brand.aboutText", m_config.aboutText);
    app->setProperty("sanyi.brand.logo", assetPath(m_config.logo));
    app->setProperty("sanyi.brand.aboutLogo", assetPath(m_config.aboutLogo));
}

void UiBrandManager::applyRuntimeIdentity() const
{
    applyApplicationIdentity();
    applyApplicationProperties();

    QGuiApplication::setApplicationDisplayName(productName());

    if (auto* app = qobject_cast<QApplication*>(QApplication::instance()))
    {
        const QString iconPath = assetPath(m_config.appIcon);
        if (!iconPath.isEmpty())
        {
            const QIcon icon(iconPath);
            if (!icon.isNull())
            {
                app->setWindowIcon(icon);
            }
        }
    }

    if (!m_config.theme.isEmpty())
    {
        TM->setTheme(themeFromName(m_config.theme));
    }

    if (!m_config.iconDirectory.isEmpty())
    {
        IconHelper::setUserIconDirectory(assetPath(m_config.iconDirectory));
    }

    if (!m_config.styleSheet.isEmpty())
    {
        QFile file(assetPath(m_config.styleSheet));
        if (file.open(QIODevice::ReadOnly | QIODevice::Text))
        {
            TM->setBrandStyleSheet(QString::fromUtf8(file.readAll()));
        }
        else
        {
            SY_WARNF("[UiBrand] Brand stylesheet not found: '%s'", qPrintable(m_config.styleSheet));
        }
    }
}
