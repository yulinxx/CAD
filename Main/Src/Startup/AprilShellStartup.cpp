#include "AprilShellStartup.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSurfaceFormat>
#include <QString>
#include <QStringList>

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>

#include "AppPathManager.h"
#include "Common/AppInitializer.h"
#include "Common/CrashHandlerBootstrap.h"
#include "Composition/ApplicationCompositionRoot.h"
#include "Log/AprLogger.h"
#include "Platform/MacMenuCleanup.h"
#include "Runtime/LicensingBootstrap.h"
#include "UI/Settings/SettingsService.h"
#include "Ut/BundleResources.h"
#include "VersionInfo.h"

namespace
{
    bool g_quitHookInstalled = false;

    QString productName()
    {
        const QString name = QCoreApplication::applicationName();
        return name.isEmpty() ? QStringLiteral("April") : name;
    }

    void printHelp()
    {
        const QByteArray name = productName().toUtf8();
        std::printf("Usage: %s [options] [file]\n\n", name.constData());
        std::printf("Options:\n");
        std::printf("  -v, --version           Show version information\n");
        std::printf("  -h, --help              Show this help message\n");
        std::printf("  --profile=<path>        Start with an external UiShell profile\n");
        std::printf("  --lang=<id>             Start with the given UI language\n");
        std::printf("  --smoke-exit-ms=<ms>    Quit automatically after startup\n\n");
        std::printf("File Options:\n");
        std::printf("  [file]                  Open specified file on startup\n");
    }

    void printVersion(bool full, bool all)
    {
        const QByteArray name = productName().toUtf8();
        const QByteArray version = QCoreApplication::applicationVersion().toUtf8();
        std::printf("%s %s\n", name.constData(), version.constData());
        if (!full && !all)
            return;

        std::printf("\nApplication: %s\n", name.constData());
        std::printf("Version: %s\n", version.constData());
        std::printf("Organization: %s\n", QCoreApplication::organizationName().toUtf8().constData());
        std::printf("Organization Domain: %s\n", QCoreApplication::organizationDomain().toUtf8().constData());
        if (all)
        {
            std::printf("\nBuild Information:\n");
            std::printf("  Build Date: %s\n", MainApp::buildDate().c_str());
            std::printf("  Build Time: %s (UTC)\n", MainApp::buildTime().c_str());
            std::printf("  Build Type: %s\n", MainApp::buildType().c_str());
        }
    }

    bool parseSmokeDelay(const QString& arg, int* smokeExitMs)
    {
        const QString prefix = QStringLiteral("--smoke-exit-ms=");
        if (!arg.startsWith(prefix))
            return false;

        const QString value = arg.mid(prefix.size());
        bool ok = false;
        const int ms = value.toInt(&ok);
        if (!ok || ms < 1 || ms > 600000)
        {
            std::fprintf(stderr, "[smoke] warning: ignored invalid %s (expected 1..600000)\n",
                arg.toUtf8().constData());
            return true;
        }
        if (smokeExitMs)
            *smokeExitMs = ms;
        return true;
    }

    void installQuitHook()
    {
        if (g_quitHookInstalled)
            return;
        QCoreApplication* app = QCoreApplication::instance();
        if (!app)
            return;

        g_quitHookInstalled = true;
        QObject::connect(app, &QCoreApplication::aboutToQuit, app, []() {
            auto* root = ApplicationCompositionRoot::instance();
            if (!root || !root->isBuilt())
                return;
            if (auto* settings = root->settingsService())
                settings->saveCurrentCommonSettings();
            root->saveCurrentWorkbenchSettings();
        });
    }
}  // namespace

void aprilShellPreAppInit(int argc, char** argv)
{
    Q_UNUSED(argc);
    Q_UNUSED(argv);

#ifdef Q_OS_MACOS
    // Qt on macOS: disable Input Method Kit to avoid IMKCFRunLoopWakeUpReliable crashes.
    qputenv("QT_IM_MODULE", "plain");
#endif

    QSurfaceFormat format;
#ifdef Q_OS_MACOS
    format.setVersion(4, 1);
#else
    format.setVersion(4, 6);
#endif
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24);
    format.setStencilBufferSize(8);
    format.setSamples(0);
    format.setSwapInterval(1);
    format.setRenderableType(QSurfaceFormat::OpenGL);
    QSurfaceFormat::setDefaultFormat(format);
}

int aprilShellHandleCliFlags(int argc, char** argv, int* smokeExitMs)
{
    const QStringList args = QCoreApplication::arguments();
    for (int i = 1; i < args.size(); ++i)
    {
        const QString& arg = args.at(i);
        if (arg == QLatin1String("--help") || arg == QLatin1String("-h") || arg == QLatin1String("-?"))
        {
            printHelp();
            return 0;
        }
        if (arg == QLatin1String("--version") || arg == QLatin1String("-v")
            || arg == QLatin1String("--version-long"))
        {
            printVersion(false, false);
            return 0;
        }
        if (arg == QLatin1String("--version-full"))
        {
            printVersion(true, false);
            return 0;
        }
        if (arg == QLatin1String("--version-all"))
        {
            printVersion(false, true);
            return 0;
        }
        if (arg == QLatin1String("--authors"))
        {
            std::printf("%s\nAuthors:\n  April Development Team <dev@apr-cad.com>\n",
                productName().toUtf8().constData());
            return 0;
        }
        if (arg == QLatin1String("--license"))
        {
            std::printf("%s - License Information\nThis program is provided for authorized use only.\n",
                productName().toUtf8().constData());
            return 0;
        }
        if (parseSmokeDelay(arg, smokeExitMs))
            continue;
    }

    Q_UNUSED(argc);
    Q_UNUSED(argv);
    return -1;
}

int aprilShellPostAppInit(int argc, char** argv)
{
    Q_UNUSED(argc);
    Q_UNUSED(argv);

    Ut::BundleResources::initialize();

    const QString appRoot = AppPathManager::appRootDir();
    if (!appRoot.isEmpty())
        QDir::setCurrent(appRoot);

    AppInitializer::initialize();

    if (qEnvironmentVariableIntValue("APR_DISABLE_CRASH_HANDLER") != 0)
    {
        APR_DEBUG("[AprilShellStartup] CrashHandler disabled");
    }
    else if (!CrashHandlerBootstrap::initialize(MainApp::appName(),
                 QString::fromStdString(MainApp::appVersion()).toStdString()))
    {
        APR_WARN("[AprilShellStartup] CrashHandler initialization failed, continuing without crash capture");
    }

    const int licensingExit = LicensingBootstrap::runGate(AppPathManager::configDir());
    if (licensingExit != 0)
        return licensingExit;

    return 0;
}

void aprilShellAfterBootstrap()
{
    // 客户配置自检属于旧 ClientConfig 的测试专用路径；UiShell profile 的
    // 诊断由 StartupSession::printDiagnostics() 在加载阶段统一输出。
    installQuitHook();

    // 原生菜单栏由 UiShell 构建后，Qt 只把 QIcon 同步到 NSMenuItem.image；
    // macOS 27+ 默认隐藏该图片。旧路径在 WorkbenchMenuManager 里修过，M6 后
    // 那段代码只由测试编译，生产 April 必须在这里补回，否则菜单没有图标。
    ensureMacMenuImagesVisible();
}

void aprilShellShutdown()
{
    CrashHandlerBootstrap::shutdown();
    AppInitializer::shutdown();
}
