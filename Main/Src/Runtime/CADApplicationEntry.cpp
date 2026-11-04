#include "CADApplicationRuntime.h"

#include <QApplication>
#include <QFileInfo>

#include <cstdio>
#include <string>

#include "Log/SyLogger.h"
#include "UI/ClientConfig/UiBrandManager.h"
#include "Ut/BundleResources.h"
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
// smokeExitMs: 输出参数，--smoke-exit-ms=N 的解析结果，0 表示禁用自动退出
// 返回值：>=0 表示退出（返回值为 exit code），<0 表示继续启动 GUI
static int handleCliFlags(int argc, char** argv, int& smokeExitMs)
{
    const std::string productName = UiBrandManager::instance().productName().toStdString();
    const std::string companyName = UiBrandManager::instance().companyName().toStdString();

    // 遍历命令行参数
    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];

        // --help / -h / -?
        if (arg == "--help" || arg == "-h" || arg == "-?")
        {
            std::printf("Usage: %s [options] [file]\n", productName.c_str());
            std::printf("\n");
            std::printf("Options:\n");
            std::printf("  -v, --version           Show version information\n");
            std::printf("  -h, --help              Show this help message\n");
            std::printf("  --client=<id>           Start with the specified client profile\n");
            std::printf("  --smoke-exit-ms=<ms>    Quit automatically after the given startup delay\n");
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
            std::printf("%s %s\n", productName.c_str(), QCoreApplication::applicationVersion().toStdString().c_str());
            std::printf("%s\n", UiBrandManager::instance().copyright().toStdString().c_str());
            std::printf("License: Proprietary\n");
            return 0;
        }

        // --version-full: 详细版本信息
        if (arg == "--version-full")
        {
            std::printf("%s %s\n", productName.c_str(), QCoreApplication::applicationVersion().toStdString().c_str());
            std::printf("\n");
            std::printf("Organization: %s\n", companyName.c_str());
            std::printf("Domain: %s\n", QCoreApplication::organizationDomain().toStdString().c_str());
            std::printf("\n");
            std::printf("This is proprietary software. All rights reserved.\n");
            return 0;
        }

        // --version-all: 最详细版本信息（含构建信息）
        if (arg == "--version-all")
        {
            std::printf("%s %s\n", productName.c_str(), QCoreApplication::applicationVersion().toStdString().c_str());
            std::printf("\n");
            std::printf("Application: %s\n", productName.c_str());
            std::printf("Version: %s\n", QCoreApplication::applicationVersion().toStdString().c_str());
            std::printf("Organization: %s\n", companyName.c_str());
            std::printf("Organization Domain: %s\n", QCoreApplication::organizationDomain().toStdString().c_str());
            std::printf("\n");
            std::printf("Build Information:\n");
            std::printf("  Build Date: %s\n", MainApp::buildDate().c_str());
            std::printf("  Build Time: %s (UTC)\n", MainApp::buildTime().c_str());
            std::printf("  Build Type: %s\n", MainApp::buildType().c_str());
            std::printf("\n");
            std::printf("%s\n", UiBrandManager::instance().copyright().toStdString().c_str());
            return 0;
        }

        // --authors: 显示作者/贡献者信息
        if (arg == "--authors")
        {
            std::printf("%s\n", productName.c_str());
            std::printf("%s\n", UiBrandManager::instance().copyright().toStdString().c_str());
            std::printf("\n");
            std::printf("Authors:\n");
            std::printf("  SanYi Development Team <dev@sanyi-cad.com>\n");
            return 0;
        }

        // --license: 显示许可证信息
        if (arg == "--license")
        {
            std::printf("%s - License Information\n", productName.c_str());
            std::printf("\n");
            std::printf("%s\n", UiBrandManager::instance().copyright().toStdString().c_str());
            std::printf("\n");
            std::printf("This software is proprietary and confidential. Unauthorized copying,\n");
            std::printf("distribution, or use of this software, via any medium, is strictly\n");
            std::printf("prohibited and may result in severe civil and criminal penalties.\n");
            std::printf("\n");
            std::printf("This program is provided for authorized use only. By using this software,\n");
            std::printf("you agree to the terms and conditions of the license agreement.\n");
            return 0;
        }

        // --smoke-exit-ms=N：启动冒烟专用，显式传入时才在启动后自动退出；非法值告警后忽略
        static const std::string smokePrefix = "--smoke-exit-ms=";
        if (arg.rfind(smokePrefix, 0) == 0)
        {
            const std::string value = arg.substr(smokePrefix.size());
            bool valid = !value.empty() && value.size() <= 9;

            // 只接受纯数字，避免把 "--smoke-exit-ms=abc" 当成 0 静默通过
            int parsed = 0;
            for (size_t i = 0; valid && i < value.size(); ++i)
            {
                const char ch = value[i];
                if (ch < '0' || ch > '9')
                {
                    valid = false;
                    break;
                }
                parsed = parsed * 10 + (ch - '0');
            }

            // 上限 600000 ms，防止误传导致进程长时间驻留
            if (valid && parsed > 0 && parsed <= 600000)
            {
                smokeExitMs = parsed;
            }
            else
            {
                std::fprintf(stderr, "[smoke] warning: ignored invalid %s (expected 1..600000)\n", arg.c_str());
            }
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

    // 品牌必须在 QApplication 与任何路径解析之前加载：只依赖 argv / 环境变量 / client.id，
    // 这样 organizationName / applicationName 从第一天就是客户身份。
    auto& brandManager = UiBrandManager::instance();
    brandManager.loadForProcess(argc, argv);
    brandManager.applyApplicationIdentity();

    // 命令行标志优先处理：--version/-v、--help/-h 命中即退出，不启动 GUI
    // 冒烟开关的毫秒值通过输出参数带回，仅在显式传参时非 0
    int smokeExitMs = 0;
    const int cliResult = handleCliFlags(argc, argv, smokeExitMs);
    if (cliResult >= 0)
    {
        return cliResult;
    }

    // QApplication 必须在 buildAppPaths() 之前创建，否则 AppPathManager 在解析路径时
    // 调用 QCoreApplication::applicationDirPath() 会因 QApplication 尚未存在而告警。
    auto app = std::make_unique<QApplication>(argc, argv);

    // 初始化 App Bundle 内部资源路径 (pdftocairo, Ghostscript 等)
    // 必须在 QApplication 创建后调用，以确保 QCoreApplication::applicationDirPath() 可用
    Ut::BundleResources::initialize();

    // 品牌运行期资源：窗口图标、默认主题、图标目录、品牌 QSS
    brandManager.applyRuntimeIdentity();

    auto appPaths = MainApp::buildAppPaths(brandManager.productName().toStdString());
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
    runtime.setSmokeExitMs(smokeExitMs);
    return runtime.run();
}