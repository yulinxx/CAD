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
// Qt 6+: 默认启用，无需手动设置
static void setupHighDpiScaling() {}

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
    // 遍历命令行参数
    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];

        // --help / -h / -?
        if (arg == "--help" || arg == "-h" || arg == "-?")
        {
            std::printf("Usage: %s [options] [file]\n", MainApp::appName().c_str());
            std::printf("\n");
            std::printf("Options:\n");
            std::printf("  -v, --version           Show version information\n");
            std::printf("  -h, --help              Show this help message\n");
            std::printf("\n");
            std::printf("File Options:\n");
            std::printf("  [file]                 Open specified file on startup\n");
            std::printf("\n");
            std::printf("Report bugs to: https://xxxx.cad/issues\n");
            return 0;
        }

        // --version / -v / --version-long
        if (arg == "--version" || arg == "-v" || arg == "--version-long")
        {
            std::printf("%s %s\n", MainApp::appName().c_str(), MainApp::appVersion().c_str());
            std::printf("Copyright (C) 2026 %s\n", MainApp::organizationName().c_str());
            std::printf("License: Proprietary\n");
            return 0;
        }

        // --version-full: 详细版本信息
        if (arg == "--version-full")
        {
            std::printf("%s %d.%d.%d\n",
                MainApp::appName().c_str(),
                MainApp::versionMajor(),
                MainApp::versionMinor(),
                MainApp::versionPatch());
            std::printf("\n");
            std::printf("Organization: %s\n", MainApp::organizationName().c_str());
            std::printf("Domain: %s\n", MainApp::organizationDomain().c_str());
            std::printf("\n");
            std::printf("This is proprietary software. All rights reserved.\n");
            return 0;
        }

        // --version-all: 最详细版本信息（含构建信息）
        if (arg == "--version-all")
        {
            std::printf("%s %d.%d.%d\n",
                MainApp::appName().c_str(),
                MainApp::versionMajor(),
                MainApp::versionMinor(),
                MainApp::versionPatch());
            std::printf("\n");
            std::printf("Application: %s\n", MainApp::appName().c_str());
            std::printf("Version: %s\n", MainApp::appVersion().c_str());
            std::printf("Organization: %s\n", MainApp::organizationName().c_str());
            std::printf("Organization Domain: %s\n", MainApp::organizationDomain().c_str());
            std::printf("\n");
            std::printf("Build Information:\n");
            std::printf("  Build Date: %s\n", MainApp::buildDate().c_str());
            std::printf("  Build Time: %s (UTC)\n", MainApp::buildTime().c_str());
            std::printf("  Build Type: %s\n", MainApp::buildType().c_str());
            std::printf("\n");
            std::printf("Copyright (C) 2026 %s. All rights reserved.\n", MainApp::organizationName().c_str());
            return 0;
        }

        // --authors: 显示作者/贡献者信息
        if (arg == "--authors")
        {
            std::printf("%s\n", MainApp::appName().c_str());
            std::printf("Copyright (C) 2026 %s\n", MainApp::organizationName().c_str());
            std::printf("\n");
            std::printf("Authors:\n");
            std::printf("  SanYi Development Team <dev@sanyi-cad.com>\n");
            return 0;
        }

        // --license: 显示许可证信息
        if (arg == "--license")
        {
            std::printf("%s - License Information\n", MainApp::appName().c_str());
            std::printf("\n");
            std::printf("Copyright (C) 2026 %s\n", MainApp::organizationName().c_str());
            std::printf("\n");
            std::printf("This software is proprietary and confidential. Unauthorized copying,\n");
            std::printf("distribution, or use of this software, via any medium, is strictly\n");
            std::printf("prohibited and may result in severe civil and criminal penalties.\n");
            std::printf("\n");
            std::printf("This program is provided for authorized use only. By using this software,\n");
            std::printf("you agree to the terms and conditions of the license agreement.\n");
            return 0;
        }
    }

    // 无命令行标志，继续启动 GUI
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
            appPaths.appRootPath = QFileInfo(QString::fromWCharArray(path)).absolutePath().toStdString();
        }
#else
        if (argc > 0 && argv && argv[0])
        {
            appPaths.appRootPath = QFileInfo(QString::fromLocal8Bit(argv[0])).absolutePath().toStdString();
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