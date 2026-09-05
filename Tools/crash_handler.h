/**
 * crash_handler.h - Windows 崩溃自动上报
 * 
 * 在程序初始化时调用 InitCrashHandler()，崩溃时会自动:
 * 1. 生成 .dmp 文件到临时目录
 * 2. 调用 Python 脚本上传到分析服务器
 * 3. 显示崩溃信息对话框
 * 
 * 用法:
 *   #include "crash_handler.h"
 *   int main() {
 *       InitCrashHandler(L"http://your-server:8080");
 *       // ... 你的代码
 *   }
 * 
 * 编译: 需要链接 dbghelp.lib (Windows SDK 自带)
 */

#pragma once

#ifdef _WIN32

#include <windows.h>
#include <minidumpapiset.h>
#include <cstdio>
#include <string>

namespace CrashHandler {

static std::wstring g_serverUrl;
static std::wstring g_dumpPath;

// 生成 .dmp 文件路径
inline std::wstring GetDumpPath() {
    wchar_t tempPath[MAX_PATH];
    GetTempPathW(MAX_PATH, tempPath);
    
    SYSTEMTIME st;
    GetLocalTime(&st);
    
    wchar_t path[MAX_PATH];
    swprintf_s(path, L"%sCrash_%04d%02d%02d_%02d%02d%02d.dmp",
        tempPath,
        st.wYear, st.wMonth, st.wDay,
        st.wHour, st.wMinute, st.wSecond);
    return path;
}

// 定位 crash_reporter.py 所在目录：
// 崩溃发生时 cwd 未必是安装目录（Explorer 直接启动、服务进程等场景），
// 硬编码 L"." 会找不到脚本。改为可执行文件所在目录。
inline std::wstring GetScriptDir() {
    wchar_t buf[MAX_PATH];
    const DWORD n = GetModuleFileNameW(NULL, buf, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) {
        return L"."; // 失败时回退当前目录（原有行为）
    }
    std::wstring path(buf, n);
    const size_t slash = path.find_last_of(L"\\/");
    if (slash != std::wstring::npos) {
        path.resize(slash);
    }
    return path;
}

// 调用 Python 脚本上传
inline void UploadDump(const wchar_t* dumpPath) {
    if (g_serverUrl.empty()) return;

    const std::wstring scriptDir = GetScriptDir();

    // 构造命令: python crash_reporter.py <server> <dump>
    wchar_t cmd[MAX_PATH * 3];
    swprintf_s(cmd, 
        L"python.exe \"%s\\crash_reporter.py\" \"%s\" \"%s\"",
        scriptDir.c_str(),
        g_serverUrl.c_str(),
        dumpPath);
    
    // 静默执行，等待完成；
    // 子进程工作目录设为脚本所在目录，避免崩溃时 cwd 不在安装目录导致上传失败
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi = {};
    CreateProcessW(NULL, cmd, NULL, NULL, FALSE, 
        CREATE_NO_WINDOW, NULL, scriptDir.c_str(), &si, &pi);
    
    // 等待上传完成（最多60秒）
    WaitForSingleObject(pi.hProcess, 60000);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
}

// 异常过滤器
inline LONG WINAPI ExceptionFilter(EXCEPTION_POINTERS* ep) {
    // 生成 minidump
    g_dumpPath = GetDumpPath();
    
    HANDLE hFile = CreateFileW(g_dumpPath.c_str(),
        GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL, NULL);
    
    if (hFile != INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION mei = {
            GetCurrentThreadId(), ep, FALSE
        };
        
        MiniDumpWriteDump(
            GetCurrentProcess(),
            GetCurrentProcessId(),
            hFile,
            MiniDumpNormal,
            &mei, NULL, NULL);
        
        CloseHandle(hFile);
    }
    
    // 上传到服务器
    UploadDump(g_dumpPath.c_str());
    
    // 显示崩溃对话框
    wchar_t msg[MAX_PATH * 2];
    swprintf_s(msg,
        L"Program crashed!\n\n"        // 程序崩溃了！
        L"Exception code: 0x%08X\n"    // 异常代码
        L"Crash address: 0x%p\n"       // 崩溃地址
        L"Crash report saved:\n%s\n\n" // 崩溃报告已保存
        L"Please send this file to the developers.",  // 请将此文件发送给开发人员
        ep->ExceptionRecord->ExceptionCode,
        ep->ExceptionRecord->ExceptionAddress,
        g_dumpPath.c_str());
    
    MessageBoxW(NULL, msg, L"SanYiCAD - Crash",  // SanYiCAD - 崩溃
        MB_OK | MB_ICONERROR | MB_TOPMOST);
    
    return EXCEPTION_EXECUTE_HANDLER;
}

/**
 * 初始化崩溃处理器
 * @param serverUrl 分析服务器地址，如 L"http://192.168.1.100:8080"
 */
inline void InitCrashHandler(const wchar_t* serverUrl = NULL) {
    if (serverUrl) g_serverUrl = serverUrl;
    SetUnhandledExceptionFilter(ExceptionFilter);
}

} // namespace CrashHandler

// 便捷宏
#define INIT_CRASH_HANDLER(url) CrashHandler::InitCrashHandler(url)

#endif // _WIN32
