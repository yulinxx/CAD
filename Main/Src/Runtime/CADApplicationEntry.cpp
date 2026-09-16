#include "CADApplicationRuntime.h"

#include <QApplication>
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

// 启用高 DPI 缩放（跨平台高分屏支持）
// Qt 5.6+: AA_EnableHighDpiScaling, AA_UseHighDpiPixmaps
// Qt 6+: 默认启用，但显式设置可确保兼容性
static void setupHighDpiScaling()
{
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QCoreApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
#endif
}

// macOS: 使用原生菜单栏
static void setupNativeMenuBar()
{
#ifdef Q_OS_MACOS
    QCoreApplication::setAttribute(Qt::AA_DontUseNativeMenuBar, false);
#endif
}

// 命令行标志处理：--version/-v、--help/-h 命中即退出
// 返回值：>=0 表示退出（返回值为 exit code），<0 表示继续启动 GUI
static int handleCliFlags(int argc, char** argv)
{
    // TODO: 实现命令行参数处理 --version/-v、--help/-h
    // 目前暂时跳过，直接启动 GUI
    Q_UNUSED(argc);
    Q_UNUSED(argv);
    return -1;
}

int runCADApplication(int argc, char** argv)
{
    // 启用高 DPI 缩放（必须在 QApplication 创建前设置）
    setupHighDpiScaling();
    setupNativeMenuBar();
    
    // 命令行标志优先处理：--version/-v、--help/-h 命中即退出，不启动 GUI
    const int cliResult = handleCliFlags(argc, argv);
    if (cliResult >= 0)
    {
        return cliResult;
    }

    // QApplication 必须在 buildAppPaths() 之前创建，否则 AppPathManager 在解析路径时
    // 调用 QCoreApplication::applicationDirPath() 会因 QApplication 尚未存在而告警。
    auto app = std::make_unique<QApplication>(argc, argv);

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

    CADApplicationRuntime runtime(std::move(app), appPaths);
    runtime.setStartWorkbenchId(QStringLiteral("2D"));
    return runtime.run();
}