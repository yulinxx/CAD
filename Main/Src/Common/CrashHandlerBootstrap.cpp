#include "CrashHandlerBootstrap.h"

#include "AppPathManager.h"

#include "CrashHandler/CrashHandlerDLL.h"

#include "Log/SyLogger.h"

#include <QDir>
#include <QFileInfo>
#include <cstdio>

#ifdef _WIN32
// dbghelp 必须在 windows.h 之后
#include <windows.h>

#include <dbghelp.h>
#endif

namespace
{
    std::string g_dumpPath;
    std::string g_appName;
    std::string g_appVersion;

#ifdef _WIN32
    /// 把当前线程的调用栈符号化后打到 stderr。
    ///
    /// 为什么要自己走一遍 dbghelp：现场与开发机上都不一定装得下 WinDbg/cdb，
    /// 只留一个 .dmp 等于没有线索 —— 崩溃排查会退化成猜。Debug 构建的 PDB 就在
    /// DLL 旁边，SymFromAddr/SymGetLineFromAddr64 能直接给出函数名与行号。
    ///
    /// 本函数在 breakpad 的回调里调用，此时仍在出错线程上，异常帧还在栈上，
    /// 所以 CaptureStackBackTrace 拿到的是「本函数 → breakpad → 系统派发 → 真正出错的帧」。
    /// 前几帧是噪声，往下看即可。
    void printSymbolizedBacktrace()
    {
        const HANDLE process = GetCurrentProcess();
        SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
        if (!SymInitialize(process, nullptr, TRUE))
        {
            std::fprintf(stderr, "  [CrashHandler] SymInitialize failed (err=%lu), no backtrace\n", GetLastError());
            return;
        }

        void* frames[64] = {};
        const USHORT captured = CaptureStackBackTrace(0, 64, frames, nullptr);
        std::fprintf(stderr, "  [CrashHandler] backtrace (%u frames, innermost first):\n", captured);

        // SYMBOL_INFO 后面紧跟名字缓冲，必须按 sizeof(SYMBOL_INFO)+MaxNameLen 分配
        alignas(SYMBOL_INFO) char symbolBuffer[sizeof(SYMBOL_INFO) + 1024] = {};
        auto* symbol = reinterpret_cast<SYMBOL_INFO*>(symbolBuffer);
        symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
        symbol->MaxNameLen = 1023;

        for (USHORT i = 0; i < captured; ++i)
        {
            const DWORD64 address = reinterpret_cast<DWORD64>(frames[i]);

            IMAGEHLP_MODULE64 moduleInfo = {};
            moduleInfo.SizeOfStruct = sizeof(moduleInfo);
            const char* moduleName =
                SymGetModuleInfo64(process, address, &moduleInfo) ? moduleInfo.ModuleName : "?";

            DWORD64 displacement = 0;
            const bool haveSymbol = SymFromAddr(process, address, &displacement, symbol) != FALSE;

            IMAGEHLP_LINE64 line = {};
            line.SizeOfStruct = sizeof(line);
            DWORD lineDisplacement = 0;
            const bool haveLine = SymGetLineFromAddr64(process, address, &lineDisplacement, &line) != FALSE;

            if (haveSymbol && haveLine)
            {
                std::fprintf(stderr,
                    "    #%02u %s!%s + 0x%llx  (%s:%lu)\n",
                    i,
                    moduleName,
                    symbol->Name,
                    static_cast<unsigned long long>(displacement),
                    line.FileName,
                    line.LineNumber);
            }
            else if (haveSymbol)
            {
                std::fprintf(stderr,
                    "    #%02u %s!%s + 0x%llx\n",
                    i,
                    moduleName,
                    symbol->Name,
                    static_cast<unsigned long long>(displacement));
            }
            else
            {
                std::fprintf(stderr, "    #%02u %s!0x%llx\n", i, moduleName, static_cast<unsigned long long>(address));
            }
        }

        SymCleanup(process);
    }
#endif  // _WIN32

    int onCrashCallback(const char* dumpPath, int succeeded, void* /*userData*/)
    {
        // 第一件事：把异步日志队列强制落盘。
        // 日志走 spdlog async_logger + flush_on(warn)，崩溃时队列里尚未写出的记录会随
        // 进程一起消失，于是"日志最后一行"根本不是崩溃位置 —— 排查会被这条假线索反复带偏。
        SyLogger::GetInstance().Flush();

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
#ifdef _WIN32
        printSymbolizedBacktrace();
#endif
        std::fprintf(stderr,
            "==========================================================================\n"
            "  Please report the above info and the .dmp file to technical support.\n"
            "==========================================================================\n\n");
        std::fflush(stderr);
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