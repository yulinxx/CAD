#include "UiClientContext.h"

#include "Log/SyLogger.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>

const char* const UiClientContext::kDefaultClientId = "san_yi";

UiClientContext& UiClientContext::instance()
{
    static UiClientContext ctx;
    return ctx;
}

void UiClientContext::setClientIdOverride(const QString& clientId)
{
    m_override = clientId.trimmed();
    // 覆盖值变化后必须失效缓存，否则后续查询仍返回旧客户 ID
    m_resolved = false;
    m_cachedClientId.clear();
    if (!m_override.isEmpty())
    {
        SY_DEBUGF("[UiClientContext] Client id override set: '%s'", qPrintable(m_override));
    }
    else
    {
        SY_DEBUG("[UiClientContext] Client id override cleared");
    }
}

QString UiClientContext::readClientIdFile(const QString& appDir)
{
    const QString dir =
        appDir.isEmpty() && QCoreApplication::instance() ? QCoreApplication::applicationDirPath() : appDir;
    if (dir.isEmpty())
    {
        return {};
    }

    QFile file(QDir(dir).filePath(QStringLiteral("client.id")));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        return {};
    }

    const QString firstLine = QString::fromUtf8(file.readLine()).trimmed();
    if (firstLine.startsWith(QChar(0xFEFF)))
    {
        return firstLine.mid(1).trimmed();
    }
    return firstLine;
}

QString UiClientContext::resolveClientId(QString& sourceOut)
{
    // 优先级 2：环境变量，供 CI 与现场排查临时切换客户
    if (qEnvironmentVariableIsSet("SANYI_CLIENT_ID"))
    {
        const QString fromEnv = QString::fromUtf8(qgetenv("SANYI_CLIENT_ID")).trimmed();
        if (!fromEnv.isEmpty())
        {
            sourceOut = QStringLiteral("environment SANYI_CLIENT_ID");
            return fromEnv;
        }
    }

    // 优先级 3：交付包内置的 client.id，是现场部署唯一需要改写的文件
    const QString fromFile = readClientIdFile();
    if (!fromFile.isEmpty())
    {
        sourceOut = QStringLiteral("client.id next to executable");
        return fromFile;
    }

    // 优先级 4：用户设置，兼容旧安装包写入的客户标识
    if (QCoreApplication::instance())
    {
        QSettings settings;
        const QString fromSettings = settings.value(QStringLiteral("Client/Id")).toString().trimmed();
        if (!fromSettings.isEmpty())
        {
            sourceOut = QStringLiteral("QSettings Client/Id");
            return fromSettings;
        }
    }

    // 优先级 5：内置默认
    sourceOut = QStringLiteral("built-in default");
    return QString::fromUtf8(kDefaultClientId);
}

const QString& UiClientContext::clientId() const
{
    if (m_resolved)
    {
        return m_cachedClientId;
    }

    QString source;
    if (!m_override.isEmpty())
    {
        m_cachedClientId = m_override;
        source = QStringLiteral("explicit override");
    }
    else
    {
        m_cachedClientId = resolveClientId(source);
    }

    m_resolved = true;
    // 该日志是排查「客户配置为什么没生效」的第一现场，务必保留
    SY_DEBUGF("[UiClientContext] Active client id='%s' (source: %s)", qPrintable(m_cachedClientId), qPrintable(source));
    return m_cachedClientId;
}

QStringList UiClientContext::configCandidatePaths(const QString& clientId)
{
    QStringList paths;
    if (clientId.trimmed().isEmpty())
    {
        return paths;
    }

    // 1. 用户配置目录：运维可在不改安装包的情况下热修单个客户
    const QString userConfigDir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    if (!userConfigDir.isEmpty())
    {
        paths << QDir(userConfigDir + QStringLiteral("/ui")).filePath(clientId + QStringLiteral(".json"));
    }

    // 2. 交付包目录：exe 同级的 configs/<id>.json
    if (QCoreApplication::instance())
    {
        paths << QDir(QCoreApplication::applicationDirPath() + QStringLiteral("/configs"))
                     .filePath(clientId + QStringLiteral(".json"));
    }

    // 3. 内置资源：随 qrc 编入
    paths << QStringLiteral(":/configs/%1.json").arg(clientId);
    return paths;
}

QString UiClientContext::configResourcePathFor(const QString& clientId)
{
    const QStringList candidates = configCandidatePaths(clientId);
    for (const QString& path : candidates)
    {
        if (QFile::exists(path))
        {
            return path;
        }
    }
    return candidates.isEmpty() ? QString() : candidates.constLast();
}

QString UiClientContext::configResourcePath() const
{
    const QString client = clientId();
    const QString path = configResourcePathFor(client);
    if (!path.isEmpty() && QFile::exists(path))
    {
        return path;
    }

    // 客户 ID 拼写错误或配置未随包发布时，回退到默认客户配置。
    // 这里不能直接失败：UI 构建已无硬编码回退路径，加载失败会导致空窗口。
    const QString fallback = configResourcePathFor(QString::fromUtf8(kDefaultClientId));
    SY_WARNF("[UiClientContext] Client config not found for '%s' (last candidate: '%s'), falling back to '%s'",
        qPrintable(client),
        qPrintable(path),
        qPrintable(fallback));
    return fallback;
}

void UiClientContext::resetCacheForTest()
{
    m_resolved = false;
    m_cachedClientId.clear();
}