/**
 * @file CrashHandlerBootstrap.cpp
 * @brief 崩溃处理器启动实现
 */
#include "CrashHandlerBootstrap.h"

#include "AppPathManager.h"

#include "CrashHandler/CrashHandlerDLL.h"

#include "Log/SyLogger.h"

#include <QDir>
#include <QFileInfo>
#include <cstdio>
#include <set>

#ifdef _WIN32
    // dbghelp 必须在 windows.h 之后
    #include <windows.h>
    #include <tlhelp32.h>

    #include <dbghelp.h>
#endif

namespace
{
    std::string g_dumpPath;
    std::string g_appName;
    std::string g_appVersion;

#ifdef _WIN32
    /// 单线程符号化栈打印（被 printAllThreadBacktraces 调）
    void printThreadBacktrace(HANDLE process, HANDLE threadHandle, DWORD threadId)
    {
        CONTEXT ctx = {};
        ctx.ContextFlags = CONTEXT_AMD64;
        if (!GetThreadContext(threadHandle, &ctx))
        {
            std::fprintf(stderr, "    thread %u: cannot get context (err=%lu)\n", threadId, GetLastError());
            return;
        }

        STACKFRAME64 frame = {};
        frame.AddrPC.Mode = AddrModeFlat;
        frame.AddrPC.Offset = ctx.Rip;
        frame.AddrStack.Mode = AddrModeFlat;
        frame.AddrStack.Offset = ctx.Rsp;
        frame.AddrFrame.Mode = AddrModeFlat;
        frame.AddrFrame.Offset = ctx.Rbp;

        std::fprintf(stderr, "    === Thread %u ===\n", threadId);

        IMAGEHLP_LINE64 line = {};
        line.SizeOfStruct = sizeof(line);
        DWORD lineDisplacement = 0;

        const WORD type = IMAGE_FILE_MACHINE_AMD64;

        alignas(SYMBOL_INFO) char symbolBuffer[sizeof(SYMBOL_INFO) + 1024] = {};
        auto* symbol = reinterpret_cast<SYMBOL_INFO*>(symbolBuffer);
        symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
        symbol->MaxNameLen = 1023;

        for (int i = 0; i < 32; ++i)
        {
            if (!StackWalk64(type, process, threadHandle, &frame, &ctx, nullptr, SymFunctionTableAccess64, SymGetModuleBase64, nullptr))
            {
                break;
            }
            if (frame.AddrPC.Offset == 0)
            {
                break;
            }

            DWORD64 displacement = 0;
            const bool haveSymbol = SymFromAddr(process, frame.AddrPC.Offset, &displacement, symbol) != FALSE;
            const bool haveLine = SymGetLineFromAddr64(process, frame.AddrPC.Offset, &lineDisplacement, &line) != FALSE;

            if (haveSymbol && haveLine)
            {
                std::fprintf(stderr, "      #%02d %s + 0x%llx  (%s:%lu)\n",
                    i, symbol->Name,
                    static_cast<unsigned long long>(displacement),
                    line.FileName, line.LineNumber);
            }
            else if (haveSymbol)
            {
                std::fprintf(stderr, "      #%02d %s + 0x%llx\n",
                    i, symbol->Name,
                    static_cast<unsigned long long>(displacement));
            }
            else
            {
                std::fprintf(stderr, "      #%02d 0x%llx\n",
                    i, static_cast<unsigned long long>(frame.AddrPC.Offset));
            }
        }
    }

    /// 枚举当前进程所有线程，逐个打印栈。
    /// Breakpad 在独立线程里写 minidump，CaptureStackBackTrace 拿到的是异常
    /// 处理线程的栈，而非真正出错线程的栈。这里用 ToolHelp 枚举 + StackWalk64
    /// 把所有线程的栈都打出来，崩溃线程会夹在中间。
    /// 注意：崩溃处理期间绝不做堆分配——否则堆损坏场景下 handler 自己会崩。
    void printAllThreadBacktraces()
    {
        const HANDLE process = GetCurrentProcess();
        SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
        if (!SymInitialize(process, nullptr, TRUE))
        {
            std::fprintf(stderr, "  [CrashHandler] SymInitialize failed (err=%lu)\n", GetLastError());
            return;
        }

        const HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
        if (snap == INVALID_HANDLE_VALUE)
        {
            std::fprintf(stderr, "  [CrashHandler] CreateToolhelp32Snapshot failed\n");
            SymCleanup(process);
            return;
        }

        THREADENTRY32 te = {};
        te.dwSize = sizeof(te);

        std::fprintf(stderr, "  [CrashHandler] all thread backtraces:\n");

        // 固定数组避免崩溃处理期间做堆分配
        DWORD visited[256] = {};
        int visitedCount = 0;

        if (Thread32First(snap, &te))
        {
            do
            {
                bool skip = false;
                for (int v = 0; v < visitedCount; ++v)
                {
                    if (visited[v] == te.th32ThreadID)
                    {
                        skip = true;
                        break;
                    }
                }
                if (!skip && visitedCount < 256)
                {
                    visited[visitedCount++] = te.th32ThreadID;
                    HANDLE hThread = OpenThread(THREAD_ALL_ACCESS, FALSE, te.th32ThreadID);
                    if (hThread)
                    {
                        printThreadBacktrace(process, hThread, te.th32ThreadID);
                        CloseHandle(hThread);
                    }
                    else
                    {
                        std::fprintf(stderr, "    === Thread %u === (cannot open, err=%lu)\n", te.th32ThreadID, GetLastError());
                    }
                }
            } while (Thread32Next(snap, &te));
        }

        CloseHandle(snap);
        SymCleanup(process);
    }

    /// 旧版：只打当前线程（breakpad 异常处理线程）的栈 —— 保留作兜底
    void printSymbolizedBacktrace()
    {
        printAllThreadBacktraces();
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