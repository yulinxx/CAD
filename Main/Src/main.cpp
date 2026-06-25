#include <cstring>
#include <QApplication>
#include <QCoreApplication>
#include <QSurfaceFormat>
#include <QDebug>
#include <QDir>

#include "UI2D/CADApplication.h"
#include "GLVerDef.h"
#include "Log/SyLogger.h"
#include "Common/AppPathManager.h"
#include "Engine2D/Storage/StorageManager.h"
#include "CrashHandler/CrashHandler.h"

#ifdef SANYI_PYTHON_HOST
#include "PythonHost/PythonHost.h"
#include "PythonHost/PythonHostConfigUtil.h"
#endif

static const char* getFileName(const char* fullPath)
{
    if (!fullPath || *fullPath == '\0')
        return "";
    const char* lastSlash = strrchr(fullPath, '/');
    if (!lastSlash)
        lastSlash = strrchr(fullPath, '\\');
    return lastSlash ? lastSlash + 1 : fullPath;
}

static std::string truncateFunction(const char* func, size_t maxLen = 60)
{
    if (!func || *func == '\0')
        return "";
    std::string s(func);
    if (s.length() <= maxLen)
        return s;

    size_t lambdaPos = s.find("lambda");
    if (lambdaPos != std::string::npos)
        return s.substr(0, lambdaPos) + "lambda...";

    return s.substr(0, maxLen - 3) + "...";
}

static void qtMessageHandler(QtMsgType type, const QMessageLogContext& context, const QString& msg)
{
    const char* file = getFileName(context.file ? context.file : "");
    std::string function = truncateFunction(context.function ? context.function : "");

    switch (type)
    {
        case QtDebugMsg:
            if (!function.empty())
                SY_DEBUGF("%s (%s:%d, %s)", qPrintable(msg), file, context.line, function.c_str());
            else
                SY_DEBUGF("%s (%s:%d)", qPrintable(msg), file, context.line);
            break;
        case QtInfoMsg:
            SY_INFOF("%s", qPrintable(msg));
            break;
        case QtWarningMsg:
            if (!function.empty())
                SY_WARNF("%s (%s:%d, %s)", qPrintable(msg), file, context.line, function.c_str());
            else
                SY_WARNF("%s (%s:%d)", qPrintable(msg), file, context.line);
            break;
        case QtCriticalMsg:
            if (!function.empty())
                SY_ERRORF("%s (%s:%d, %s)", qPrintable(msg), file, context.line, function.c_str());
            else
                SY_ERRORF("%s (%s:%d)", qPrintable(msg), file, context.line);
            break;
        case QtFatalMsg:
            if (!function.empty())
                SY_CRITICALF("FATAL: %s (%s:%d, %s)", qPrintable(msg), file, context.line, function.c_str());
            else
                SY_CRITICALF("FATAL: %s (%s:%d)", qPrintable(msg), file, context.line);
            break;
    }
}

// ==================== 主入口 ====================

int main(int argc, char* argv[])
{
    // 0. 初始化统一路径管理（最早，在日志和数据库初始化之前）
    const std::string appName = "SanYiCAD";
    if (!Ut::AppPathManager::instance().initialize(appName))
    {
        fprintf(stderr, "Failed to initialize AppPathManager\n");
        return -1;
    }

    // 0.5 初始化崩溃捕获（尽可能早，确保初始化阶段崩溃也能捕获）
    {
        CrashHandler::CrashHandlerConfig crashConfig;
        crashConfig.dumpPath = Ut::AppPathManager::instance().getCrashDumpsPath();
        crashConfig.appName = appName;
        crashConfig.appVersion = "1.0.0";
        crashConfig.maxDumpFiles = 10;
        crashConfig.dumpType = CrashHandler::DumpType::Normal;

        CrashHandler::CrashHandler::instance().setCrashCallback(
            [](const std::string& dumpPath, bool succeeded) -> bool {
                if (succeeded && !dumpPath.empty())
                {
                    fprintf(stderr, "[CrashHandler] Crash dump saved to: %s\n", dumpPath.c_str());
                }
                return succeeded;
            }
        );

        if (!CrashHandler::CrashHandler::instance().initialize(crashConfig))
        {
            fprintf(stderr, "Warning: Failed to initialize CrashHandler\n");
        }
        else
        {
            fprintf(stderr, "[CrashHandler] Initialized, dump path: %s\n",
                crashConfig.dumpPath.c_str());
        }
    }

    // 设置日志系统使用统一路径（必须在日志初始化之前调用）
    SyLogger::SetDefaultLogPath(Ut::AppPathManager::instance().getLogsPath());

    // 1. 初始化日志系统（使用统一路径）
    SyLogConfig logConfig;
    logConfig.logName = appName;
    // logConfig.level = SyLogLevel::Info;
    logConfig.level = SyLogLevel::Debug;
    logConfig.consoleEnable = true;
    logConfig.fileEnable = true;
    logConfig.maxAgeDays = 30;
    logConfig.splitErrorLog = true;
    logConfig.splitDebugLog = true;
    SyLogger::GetInstance().Initialize(logConfig);

    SY_INFO("=====================================");
    SY_INFO("=== SanYiCAD Application Starting ===");
    SY_INFOF("App root path: %s", Ut::AppPathManager::instance().getAppRootPath().c_str());
    SY_INFOF("Log directory: %s", SyLogger::GetInstance().GetLogDirectory().c_str());

    // 2. 将 Qt 消息重定向到 SyLogger
    qInstallMessageHandler(qtMessageHandler);

    // 3. 设置 OpenGL 格式
    QSurfaceFormat format;
    format.setVersion(TARGET_GL_VERSION_MAJOR, TARGET_GL_VERSION_MINOR);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24);
    format.setStencilBufferSize(8);
    format.setSamples(4);
    QSurfaceFormat::setDefaultFormat(format);

    SY_INFOF("QSurfaceFormat: version=%d.%d, profile=Core, depth=24, stencil=8, samples=4",
        TARGET_GL_VERSION_MAJOR, TARGET_GL_VERSION_MINOR);

    // 4. 设置高DPI策略 (Qt6)
    // Qt6 默认启用高DPI支持，以下设置确保最佳效果：
    // - 禁用Qt内部缩放，让Windows系统处理缩放
    // - 启用高分屏图片支持
    QApplication::setAttribute(Qt::AA_DisableHighDpiScaling);
    QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);

    // 4. 创建应用
    CAD::CADApplication app(argc, argv);

    SY_INFOF("Qt Version: %s", QT_VERSION_STR);
    SY_INFO("Welcome to SanYiCAD Application - Unified 2D/3D System\n");

#ifdef SANYI_PYTHON_HOST
    {
        const std::string appDir = QCoreApplication::applicationDirPath().toStdString();
        PyHost::Config pyConfig = PyHost::buildConfigFromApplicationDir(
            appDir, CMAKE_SOURCE_DIR);
        if (!PyHost::PythonHost::instance().initialize(pyConfig))
        {
            SY_WARN("[Main] PythonHost initialization failed; Python tasks will be unavailable");
        }
        else
        {
            const PyHost::RuntimeInfo pyInfo = PyHost::PythonHost::instance().info();
            SY_INFOF("[Main] PythonHost ready: %s (%d tasks)",
                pyInfo.pythonExecutable.c_str(), pyInfo.registeredTaskCount);
        }
    }
#endif

    if (!Eg::StorageManager::instance().initialize(Ut::AppPathManager::instance().getDatabasePath()))
    {
        SY_CRITICALF("StorageManager initialization failed: %s", Eg::StorageManager::instance().lastError().c_str());
        return -1;
    }
    SY_INFOF("Database path: %s", Eg::StorageManager::instance().dbPath().c_str());

    if (!app.initialize())
    {
        SY_CRITICAL("Application initialization failed");
        return -1;
    }

    SY_INFO("Application initialized, entering event loop");

    int result = app.exec();

    SY_INFOF("=== CAD Application exiting (exit code=%d) ===", result);
    SY_INFO("=============================================");

#ifdef SANYI_PYTHON_HOST
    PyHost::PythonHost::instance().shutdown();
#endif

    // 卸载 Qt 消息处理器
    qInstallMessageHandler(nullptr);

    // 关闭日志系统
    SyLogger::GetInstance().Shutdown();

    return result;
}