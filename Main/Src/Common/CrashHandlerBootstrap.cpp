#include "CrashHandlerBootstrap.h"

#include "AppPathManager.h"

#include "CrashHandler/CrashHandlerDLL.h"

#include "Log/SyLogger.h"

#include <QDir>
#include <QFileInfo>
#include <cstdio>

namespace
{
    std::string g_dumpPath;
    std::string g_appName;
    std::string g_appVersion;

    int onCrashCallback(const char* dumpPath, int succeeded, void* /*userData*/)
    {
        std::fprintf(stderr,
            "\n"
            "==========================================================================\n"
            "  *** SanYiCAD CRASH ***  The application has crashed!\n"
            "==========================================================================\n");
        if (succeeded && dumpPath && dumpPath[0] != '\0')
        {
            std::fprintf(stderr, "  [CrashHandler] minidump saved: %s\n", dumpPath);
        }
        else
        {
            std::fprintf(stderr, "  [CrashHandler] failed to write minidump\n");
        }
        std::fprintf(stderr,
            "==========================================================================\n"
            "  Please report the above info and the .dmp file to technical support.\n"
            "==========================================================================\n\n");
        return succeeded;
    }

    bool ensureCrashDumpDirectory(const QString& path)
    {
        QDir dir(path);
        if (dir.exists())
        {
            return true;
        }
        return dir.mkpath(QStringLiteral("."));
    }
}  // namespace

bool CrashHandlerBootstrap::initialize(const std::string& appName, const std::string& appVersion)
{
    if (CrashHandler_IsInitialized())
    {
        return true;
    }

    const QString dumpDir = AppPathManager::crashDumpsDir();
    if (!ensureCrashDumpDirectory(dumpDir))
    {
        std::fprintf(stderr, "[CrashHandler] failed to create dump directory: %s\n", dumpDir.toUtf8().constData());
        return false;
    }

    g_dumpPath = dumpDir.toUtf8().constData();
    g_appName = appName;
    g_appVersion = appVersion;

    CrashHandlerConfig config;
    CrashHandler_ConfigInit(&config);
    config.dumpPath = g_dumpPath.c_str();
    config.appName = g_appName.c_str();
    config.appVersion = g_appVersion.c_str();
    config.maxDumpFiles = 10;
    config.dumpType = CRASHHANDLER_DUMP_NORMAL;

    CrashHandler_SetCrashCallback(onCrashCallback, nullptr);

    const int result = CrashHandler_Initialize(&config);
    if (result != CRASHHANDLER_OK)
    {
        char err[256] = {};
        CrashHandler_GetLastErrorMessage(err, sizeof(err));
        std::fprintf(stderr, "[CrashHandler] initialize failed (%d): %s\n", result, err);
        return false;
    }

    return true;
}

void CrashHandlerBootstrap::shutdown()
{
    if (CrashHandler_IsInitialized())
    {
        CrashHandler_Shutdown();
    }
}

bool CrashHandlerBootstrap::isInitialized()
{
    return CrashHandler_IsInitialized() != 0;
}

void CrashHandlerBootstrap::logPendingDumps()
{
    if (!CrashHandler_IsInitialized())
    {
        return;
    }

    const int count = CrashHandler_GetDumpFileCount();
    if (count <= 0)
    {
        SY_INFO("[CrashHandler] no previous crash dumps found");
        return;
    }

    SY_CRITICAL("==========================================================================");
    SY_CRITICAL("  *** SanYiCAD CRASH REPORT ***  A previous crash was detected");
    SY_CRITICALF("[CrashHandler] found %d previous crash dump(s) in: %s", count, g_dumpPath.c_str());

    for (int i = 0; i < count; ++i)
    {
        char dumpPath[512] = {};
        if (CrashHandler_GetDumpFilePath(i, dumpPath, sizeof(dumpPath)) == CRASHHANDLER_OK)
        {
            const QFileInfo info(QString::fromUtf8(dumpPath));
            SY_CRITICALF("[CrashHandler] dump[%d]: %s (size=%lld bytes, modified=%s)",
                i,
                dumpPath,
                static_cast<long long>(info.size()),
                info.lastModified().toString(Qt::ISODate).toUtf8().constData());
        }
    }

    SY_CRITICAL("  Please report the above info and the .dmp file to technical support.");
    SY_CRITICAL("==========================================================================");
}