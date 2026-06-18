#include <cstring>
#include <QApplication>
#include <QSurfaceFormat>
#include <QDebug>
#include <QDir>
#include "App/CADApplication.h"
#include "App/ModeSwitcher.h"
#include "GLVerDef.h"
#include "Log/SyLogger.h"
#include "Ut/AppPathManager.h"
#include "Engine2D/StorageManager.h"

// ==================== Qt 消息 → SyLogger 桥接 ====================

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
                SY_DEBUGF("[Qt] %s (%s:%d, %s)", qPrintable(msg), file, context.line, function.c_str());
            else
                SY_DEBUGF("[Qt] %s (%s:%d)", qPrintable(msg), file, context.line);
            break;
        case QtInfoMsg:
            SY_INFOF("[Qt] %s", qPrintable(msg));
            break;
        case QtWarningMsg:
            if (!function.empty())
                SY_WARNF("[Qt] %s (%s:%d, %s)", qPrintable(msg), file, context.line, function.c_str());
            else
                SY_WARNF("[Qt] %s (%s:%d)", qPrintable(msg), file, context.line);
            break;
        case QtCriticalMsg:
            if (!function.empty())
                SY_ERRORF("[Qt] %s (%s:%d, %s)", qPrintable(msg), file, context.line, function.c_str());
            else
                SY_ERRORF("[Qt] %s (%s:%d)", qPrintable(msg), file, context.line);
            break;
        case QtFatalMsg:
            if (!function.empty())
                SY_CRITICALF("[Qt] FATAL: %s (%s:%d, %s)", qPrintable(msg), file, context.line, function.c_str());
            else
                SY_CRITICALF("[Qt] FATAL: %s (%s:%d)", qPrintable(msg), file, context.line);
            break;
    }
}

// ==================== 主入口 ====================

int main(int argc, char* argv[])
{
    // 0. 初始化统一路径管理（最早，在日志和数据库初始化之前）
    const std::string appName = "CAD";
    if (!Ut::AppPathManager::instance().initialize(appName))
    {
        fprintf(stderr, "Failed to initialize AppPathManager\n");
        return -1;
    }

    // 设置日志系统使用统一路径（必须在日志初始化之前调用）
    SyLogger::SetDefaultLogPath(Ut::AppPathManager::instance().getLogsPath());

    // 1. 初始化日志系统（使用统一路径）
    SyLogConfig logConfig;
    logConfig.logName = appName;
    logConfig.level = SyLogLevel::Info;
    logConfig.consoleEnable = true;
    logConfig.fileEnable = true;
    logConfig.maxAgeDays = 30;
    SyLogger::GetInstance().Initialize(logConfig);

    SY_INFO("================================");
    SY_INFO("=== CAD Application Starting ===");
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

    // 4. 创建应用
    CAD::CADApplication app(argc, argv);

    SY_INFOF("Qt Version: %s", QT_VERSION_STR);
    SY_INFO("CAD Application - Unified 2D/3D System");

    // 5. 初始化存储管理器（使用统一路径）
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
    SY_INFO("===========================================");

    // 显式卸载 Qt 消息处理器（避免在 logger 已销毁后，Qt 静态对象析构时仍触发消息）
    qInstallMessageHandler(nullptr);

    // 显式关闭日志系统（在 app 对象和 GL context 销毁之前完成，防止单例析构顺序问题）
    // 注意：SyLogger 本身不依赖 GL context，但需要在其他静态对象之前关闭
    SyLogger::GetInstance().Shutdown();

    return result;
}