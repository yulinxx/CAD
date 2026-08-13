#include "CADApplicationRuntime.h"

#include <QFileInfo>

#include <cstdio>
#include <string>

#include "Log/SyLogger.h"
#include "VersionInfo.h"

#ifdef _WIN32
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <windows.h>
#endif

// 附加到父进程控制台（如 cmd/PowerShell），终端启动时 stdout/stderr/stdin 可正常收发
// 仅当 stdout 无有效句柄（GUI 程序未继承标准流）时才附加，避免破坏管道/文件重定向
// GUI 双击启动时无父控制台，AttachConsole 失败则保持原状，不影响图形界面
static void attachParentConsoleIfAny()
{
#ifdef _WIN32
    const HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut != nullptr && hOut != INVALID_HANDLE_VALUE)
    {
        // stdout 已有句柄（控制台/管道/文件），CRT 会通过它输出，无需干预
        return;
    }
    if (AttachConsole(ATTACH_PARENT_PROCESS))
    {
        FILE* out = nullptr;
        (void)freopen_s(&out, "CONOUT$", "w", stdout);
        FILE* err = nullptr;
        (void)freopen_s(&err, "CONOUT$", "w", stderr);
        FILE* in = nullptr;
        (void)freopen_s(&in, "CONIN$", "r", stdin);
    }
#endif
}

// 输出多行详细版本信息到 stdout
static void printVersionInfo()
{
    std::printf("%s %s\n", MainApp::appName().c_str(), MainApp::appVersion().c_str());
    std::printf("Version: %d.%d.%d\n", MainApp::versionMajor(), MainApp::versionMinor(), MainApp::versionPatch());
    std::printf("Organization: %s\n", MainApp::organizationName().c_str());
    std::printf("Domain: %s\n", MainApp::organizationDomain().c_str());
    std::fflush(stdout);
}

// 输出帮助信息到 stdout
static void printHelpInfo()
{
    std::printf("Usage: %s [options] [file...]\n\n", MainApp::appName().c_str());
    std::printf("Options:\n");
    std::printf("  -v, --version     Show version information and exit\n");
    std::printf("  -h, --help        Show this help and exit\n\n");
    std::printf("Without options, the CAD GUI will start.\n");
    std::fflush(stdout);
}

// 命令行标志解析：命中版本/帮助标志则输出并返回 0，否则返回 -1 继续正常启动
static int handleCliFlags(int argc, char** argv)
{
    attachParentConsoleIfAny();

    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i] ? argv[i] : "";
        if (arg == "--version" || arg == "-v")
        {
            printVersionInfo();
            return 0;
        }
        if (arg == "--help" || arg == "-h")
        {
            printHelpInfo();
            return 0;
        }
    }
    return -1;
}

int runCADApplication(int argc, char** argv)
{
    // 命令行标志优先处理：--version/-v、--help/-h 命中即退出，不启动 GUI
    const int cliResult = handleCliFlags(argc, argv);
    if (cliResult >= 0)
    {
        return cliResult;
    }

    auto appPaths = MainApp::buildAppPaths(MainApp::appName());
    if (appPaths.appRootPath.empty())
    {
#ifdef _WIN32
        wchar_t path[MAX_PATH];
        if (GetModuleFileNameW(nullptr, path, MAX_PATH) > 0)
        {
            appPaths.appRootPath = QFileInfo(QString::fromWCharArray(path)).absolutePath().toStdWString();
        }
#else
        if (argc > 0 && argv && argv[0])
        {
            appPaths.appRootPath = QFileInfo(QString::fromLocal8Bit(argv[0])).absolutePath().toStdWString();
        }
#endif
    }

    if (appPaths.appRootPath.empty())
    {
        SyLogger::GetInstance().Initialize("SanYiCAD");
        SY_ERROR("[main] error code=app.root_path_empty message=Application root path is empty");
        SyLogger::GetInstance().Shutdown();
        return -1;
    }

    CADApplicationRuntime runtime(argc, argv, appPaths);
    runtime.setStartWorkbenchId(QStringLiteral("2D"));
    return runtime.run();
}