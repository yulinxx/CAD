#include "ProfileLocator.h"

#include <QDir>
#include <QFileInfo>

namespace
{
    // QFileInfo 同样适用于 ":/..." 资源路径。
    bool isFile(const QString& path)
    {
        const QFileInfo info(path);
        return info.exists() && info.isFile();
    }
}  // namespace

ProfileInfo ProfileLocator::locate(const QString& appDir, const QString& embeddedPath)
{
    ProfileInfo resInfo;

    if (!appDir.isEmpty())
    {
        const QString path = deployedPath(appDir);
        resInfo.tried.append(path);
        if (isFile(path))
        {
            resInfo.path = path;
            resInfo.source = ProfileSource::AppDir;
            return resInfo;
        }
    }

    if (!embeddedPath.isEmpty())
    {
        resInfo.tried.append(embeddedPath);
        if (isFile(embeddedPath))
        {
            resInfo.path = embeddedPath;
            resInfo.source = ProfileSource::Resource;
            return resInfo;
        }
    }

    QStringList shown;
    for (const QString& path : resInfo.tried)
        shown.append(QStringLiteral("  ") + QDir::toNativeSeparators(path));

    resInfo.error = shown.isEmpty()
        ? QStringLiteral("no UI profile location available")
        : QStringLiteral("no UI profile found, tried:\n%1").arg(shown.join(QLatin1Char('\n')));

    return resInfo;
}

QString ProfileLocator::deployedPath(const QString& appDir)
{
    return QDir(appDir).absoluteFilePath(QStringLiteral("profile/ui.json"));
}

QString ProfileLocator::sourceName(ProfileSource source)
{
    switch (source)
    {
    case ProfileSource::AppDir:
        return QStringLiteral("appDir");
    case ProfileSource::Resource:
        return QStringLiteral("embedded");
    case ProfileSource::None:
        break;
    }
    return QStringLiteral("none");
}