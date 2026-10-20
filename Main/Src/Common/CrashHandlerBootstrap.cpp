/**
 * @file CrashHandlerBootstrap.cpp
 * @brief 崩溃处理器启动实现
 *
 * 崩溃抓栈链路（按执行先后）：
 *   1. VEH（Vectored Exception Handler） —— SEH 最早阶段，异常线程仍在运行，
 *      CaptureStackBackTrace 直接在崩溃线程上抓栈并符号化到 stderr。
 *      返回 EXCEPTION_CONTINUE_SEARCH 让异常继续分发到 breakpad。
 *   2. Breakpad 写 minidump —— 在独立线程里执行，onCrashCallback 此时
 *      所有其他线程已被 suspend，OpenThread 会失败（err=87），因此不
 *      能靠它枚举其他线程的栈。
 */
#include "CrashHandlerBootstrap.h"

#include "AppPathManager.h"

#include "CrashHandler/CrashHandlerDLL.h"

#include "Log/SyLogger.h"

#include <QDir>
#include <QFileInfo>
#include <cstdio>
#include <cstdlib>
#include <exception>

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
    static PVOID g_vehHandle = nullptr;
    static std::terminate_handler g_oldTerminate = nullptr;

    /// Vectored Exception Handler：SEH 异常分发最早期被调用。
    /// 此时崩溃线程仍在运行（未被 breakpad suspend），CaptureStackBackTrace
    /// 直接在崩溃线程上抓栈，拿到的就是真正的业务栈。
    LONG WINAPI vectoredExceptionHandler(EXCEPTION_POINTERS* pExInfo)
    {
        const DWORD code = pExInfo->ExceptionRecord->ExceptionCode;

        // 过滤 Qt/Windows 内部正常流转的异常码，只打印真正的崩溃。
        // 0xe06d7363 = C++ EH（Qt 字体回退等内部大量使用）
        // 0x8001010d = RPC 代理通知；0x40010004/0x406D1388 = 调试器约定消息
        if (code == 0xe06d7363 || code == 0x8001010d ||
            code == 0x40010004 || code == 0x406D1388)
        {
            return EXCEPTION_CONTINUE_SEARCH;
        }

        // 第一时间刷日志队列
        SyLogger::GetInstance().Flush();

        const HANDLE process = GetCurrentProcess();
        SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
        SymInitialize(process, nullptr, TRUE);

        void* frames[64] = {};
        const USHORT captured = CaptureStackBackTrace(0, 64, frames, nullptr);

        // AV 异常：ExceptionInformation[0]=操作(0读/1写/8执行)，[1]=目标地址。
        // 目标地址能直接区分「空指针+成员偏移」和「野指针/已释放内存」。
        const bool isAv = code == EXCEPTION_ACCESS_VIOLATION &&
                          pExInfo->ExceptionRecord->NumberParameters >= 2;
        const ULONG_PTR avOp = isAv ? pExInfo->ExceptionRecord->ExceptionInformation[0] : 0;
        const ULONG_PTR avTarget = isAv ? pExInfo->ExceptionRecord->ExceptionInformation[1] : 0;

        std::fprintf(stderr,
            "\n"
            "--------------------------------------------------------------------------\n"
            "  [CrashHandler] VEH: exception code=0x%08lx thread=%u address=0x%llx\n",
            code,
            GetCurrentThreadId(),
            reinterpret_cast<unsigned long long>(pExInfo->ExceptionRecord->ExceptionAddress));
        if (isAv)
        {
            const char* opName = (avOp == 0) ? "READ" : (avOp == 1) ? "WRITE" : (avOp == 8) ? "EXECUTE" : "OTHER";
            std::fprintf(stderr,
                "  [CrashHandler] VEH: AV %s of target address 0x%llx\n",
                opName, static_cast<unsigned long long>(avTarget));
        }

        std::fprintf(stderr,
            "--------------------------------------------------------------------------\n");

        alignas(SYMBOL_INFO) char symbolBuffer[sizeof(SYMBOL_INFO) + 1024] = {};
        auto* symbol = reinterpret_cast<SYMBOL_INFO*>(symbolBuffer);
        symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
        symbol->MaxNameLen = 1023;

        IMAGEHLP_LINE64 line = {};
        line.SizeOfStruct = sizeof(line);
        DWORD lineDisplacement = 0;

        for (USHORT i = 0; i < captured; ++i)
        {
            const DWORD64 address = reinterpret_cast<DWORD64>(frames[i]);

            DWORD64 displacement = 0;
            const bool haveSymbol = SymFromAddr(process, address, &displacement, symbol) != FALSE;
            const bool haveLine = SymGetLineFromAddr64(process, address, &lineDisplacement, &line) != FALSE;

            if (haveSymbol && haveLine)
            {
                std::fprintf(stderr,
                    "    #%02u %s + 0x%llx  (%s:%lu)\n",
                    i, symbol->Name,
                    static_cast<unsigned long long>(displacement),
                    line.FileName, line.LineNumber);
            }
            else if (haveSymbol)
            {
                std::fprintf(stderr,
                    "    #%02u %s + 0x%llx\n",
                    i, symbol->Name,
                    static_cast<unsigned long long>(displacement));
            }
            else
            {
                std::fprintf(stderr, "    #%02u 0x%llx\n", i, static_cast<unsigned long long>(address));
            }
        }
        std::fprintf(stderr, "--------------------------------------------------------------------------\n\n");
        std::fflush(stderr);

        SymCleanup(process);

        // 让异常继续分发——breakpad 仍能接到并写 minidump
        return EXCEPTION_CONTINUE_SEARCH;
    }

    /// C++ terminate handler：捕获未处理 C++ 异常、纯虚函数调用、堆栈展开期间
    /// 抛异常等。这类崩溃不走 SEH，VEH 不会被调。
    void crashTerminateHandler()
    {
        SyLogger::GetInstance().Flush();

        const HANDLE process = GetCurrentProcess();
        SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
        SymInitialize(process, nullptr, TRUE);

        void* frames[64] = {};
        const USHORT captured = CaptureStackBackTrace(0, 64, frames, nullptr);

        std::fprintf(stderr,
            "\n"
            "--------------------------------------------------------------------------\n"
            "  [CrashHandler] std::terminate called! thread=%u\n"
            "--------------------------------------------------------------------------\n",
            GetCurrentThreadId());

        alignas(SYMBOL_INFO) char symbolBuffer[sizeof(SYMBOL_INFO) + 1024] = {};
        auto* symbol = reinterpret_cast<SYMBOL_INFO*>(symbolBuffer);
        symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
        symbol->MaxNameLen = 1023;

        IMAGEHLP_LINE64 line = {};
        line.SizeOfStruct = sizeof(line);
        DWORD lineDisplacement = 0;

        for (USHORT i = 0; i < captured; ++i)
        {
            const DWORD64 address = reinterpret_cast<DWORD64>(frames[i]);

            DWORD64 displacement = 0;
            const bool haveSymbol = SymFromAddr(process, address, &displacement, symbol) != FALSE;
            const bool haveLine = SymGetLineFromAddr64(process, address, &lineDisplacement, &line) != FALSE;

            if (haveSymbol && haveLine)
            {
                std::fprintf(stderr,
                    "    #%02u %s + 0x%llx  (%s:%lu)\n",
                    i, symbol->Name,
                    static_cast<unsigned long long>(displacement),
                    line.FileName, line.LineNumber);
            }
            else if (haveSymbol)
            {
                std::fprintf(stderr,
                    "    #%02u %s + 0x%llx\n",
                    i, symbol->Name,
                    static_cast<unsigned long long>(displacement));
            }
            else
            {
                std::fprintf(stderr, "    #%02u 0x%llx\n", i, static_cast<unsigned long long>(address));
            }
        }
        std::fprintf(stderr, "--------------------------------------------------------------------------\n\n");
        std::fflush(stderr);

        SymCleanup(process);
        std::abort();
    }

    /// 工具：符号化指定线程（OpenThread 成功时可用）
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
            if (!StackWalk64(type, process, threadHandle, &frame, &ctx, nullptr,
                    SymFunctionTableAccess64, SymGetModuleBase64, nullptr))
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
                std::fprintf(stderr, "      #%02d 0x%llx\n", i, static_cast<unsigned long long>(frame.AddrPC.Offset));
            }
        }
    }

    /// onCrashCallback 里的兜底：枚举所有线程尝试打印栈。
    /// 注意：breakpad 在独立线程里回调，此时其他线程已被 suspend/terminate，
    /// OpenThread THREAD_ALL_ACCESS 会返回 err=87 (ERROR_INVALID_PARAMETER)。
    /// 所以这个函数大概率只能枚举到自己的线程，真正的崩溃栈已经由 VEH 先打印了。
    void tryPrintAllThreadBacktraces()
    {
        const HANDLE process = GetCurrentProcess();
        SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
        if (!SymInitialize(process, nullptr, TRUE))
        {
            return;
        }

        const HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
        if (snap == INVALID_HANDLE_VALUE)
        {
            SymCleanup(process);
            return;
        }

        std::fprintf(stderr, "  [CrashHandler] (fallback) attempting all-thread dump...\n");

        THREADENTRY32 te = {};
        te.dwSize = sizeof(te);
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
                        std::fprintf(stderr, "    thread %u: OpenThread failed (err=%lu, expected after breakpad suspends threads)\n",
                            te.th32ThreadID, GetLastError());
                    }
                }
            } while (Thread32Next(snap, &te));
        }

        CloseHandle(snap);
        SymCleanup(process);
    }
#endif  // _WIN32

    int onCrashCallback(const char* dumpPath, int succeeded, void* /*userData*/)
    {
        // VEH 已经在 SEH 最早阶段刷过日志 + 打了崩溃线程的栈。
        // 这里是 breakpad 写 minidump 后的回调，此时其他线程已被 suspend/terminate。
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
        // 兜底枚举（大概率拿不到其他线程，因为 breakpad 已 suspend）
        tryPrintAllThreadBacktraces();
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

#ifdef _WIN32
    // VEH 必须在 breakpad 之前注册：breakpad 自己也会注册 VEH，
    // 谁先注册谁先被调用。我们的 VEH 返回 EXCEPTION_CONTINUE_SEARCH
    // 让异常继续传递，breakpad 仍能接到并写 minidump。
    g_vehHandle = AddVectoredExceptionHandler(1, vectoredExceptionHandler);
    if (g_vehHandle)
    {
        std::fprintf(stderr, "[CrashHandler] VEH registered OK\n");
    }
    else
    {
        std::fprintf(stderr, "[CrashHandler] VEH registration FAILED (err=%lu)\n", GetLastError());
    }
#endif

    // C++ terminate handler：捕获未处理异常、纯虚函数调用、abort() 等。
    // 这类崩溃不走 SEH，VEH 不会被调。
    g_oldTerminate = std::set_terminate(crashTerminateHandler);
    std::fprintf(stderr, "[CrashHandler] terminate handler installed\n");

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
        std::fprintf(stderr, "[CrashHandler] breakpad init failed (%d): %s\n", result, err);
        return false;
    }

    std::fprintf(stderr, "[CrashHandler] breakpad initialized OK\n");
    return true;
}

void CrashHandlerBootstrap::shutdown()
{
#ifdef _WIN32
    if (g_vehHandle)
    {
        RemoveVectoredExceptionHandler(g_vehHandle);
        g_vehHandle = nullptr;
    }
#endif
    if (g_oldTerminate)
    {
        std::set_terminate(g_oldTerminate);
        g_oldTerminate = nullptr;
    }
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
