#include "StartupSession.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QMainWindow>
#include <QMessageBox>
#include <QString>
#include <QStringList>

#include <cstdio>

#include "Launcher/ProfileLocator.h"

namespace
{
    // README 约定：--profile <path> / --profile=<path> 优先级最高，
    // 文件不存在直接失败，不回退到 appDir / 内嵌 profile。
    QString explicitProfilePath()
    {
        const QStringList args = QCoreApplication::arguments();
        for (int i = 0; i < args.size(); ++i)
        {
            const QString& arg = args.at(i);
            if (arg.startsWith(QStringLiteral("--profile=")))
                return arg.mid(QStringLiteral("--profile=").size());
            if (arg == QStringLiteral("--profile") && i + 1 < args.size())
                return args.at(i + 1);
        }
        return {};
    }
}  // namespace

StartupSession::StartupSession(AA::IShellRuntime* runtime)
    : m_shellRT(runtime)
{
}

bool StartupSession::loadProfile(const QString& path)
{
    if (!m_shellRT)
        return false;

    const QString explicitPath = explicitProfilePath();
    if (!explicitPath.isEmpty())
    {
        const QFileInfo info(explicitPath);
        if (!info.isFile())
        {
            reportError(QCoreApplication::translate("App", "Profile does not exist:\n%1")
                            .arg(QDir::toNativeSeparators(explicitPath)));
            return false;
        }
        const QString path = info.absoluteFilePath();
        std::fprintf(stderr, "App: ui profile = %s (argument)\n", qPrintable(path));
        return loadFromPath(path);
    }

    const ProfileInfo info = ProfileLocator::locate(
        QCoreApplication::applicationDirPath(), path);

    if (!info.isValid())
    {
        reportError(QCoreApplication::translate("App", "Cannot locate UI profile:\n%1").arg(info.error));
        return false;
    }

    std::fprintf(stderr,
        "App: ui profile = %s (%s)\n",
        qPrintable(info.path),
        qPrintable(ProfileLocator::sourceName(info.source)));

    return loadFromPath(info.path);
}

bool StartupSession::loadFromPath(const QString& profilePath)
{
    if (!m_shellRT)
        return false;

    const bool loaded = m_shellRT->loadFromFile(profilePath);

    // 诊断全部写入日志，弹框仅展示首条错误。
    printDiagnostics(profilePath);
    if (loaded)
        return true;

    // Profile 加载失败
    reportError(QCoreApplication::translate("App", "Failed to load UI profile:\n%1\n\n%2")
        .arg(profilePath, m_shellRT->lastError()));

    return false;
}

bool StartupSession::attach(QMainWindow* window)
{
    if (!m_shellRT || !window)
        return false;

    if (m_shellRT->attachWindow(window))
        return true;

    std::fprintf(stderr, "App: attachWindow failed\n");
    return false;
}

void StartupSession::reportError(const QString& message)
{
    std::fprintf(stderr, "App: %s\n", qPrintable(message));
    QMessageBox::critical(nullptr, QStringLiteral("App"), message);
}

void StartupSession::printDiagnostics(const QString& profilePath) const
{
    if (!m_shellRT)
        return;

    const QString file = QDir::toNativeSeparators(profilePath);
    for (const AA::Diagnostic& d : m_shellRT->diagnostics())
    {
        const char* severity = "info";
        if (d.level == AA::Diagnostic::Level::Error)
            severity = "error";
        else if (d.level == AA::Diagnostic::Level::Warning)
            severity = "warning";

        QString where = d.strJsonPath;
        if (d.nLine > 0)
            where += QStringLiteral(" (line %1, column %2)").arg(d.nLine).arg(d.nColumn);

        const QString line =
            QStringLiteral("App: %1: %2: %3: %4").arg(file, QString::fromLatin1(severity), where, d.strMsg);
        std::fprintf(stderr, "%s\n", qPrintable(line));
    }
}