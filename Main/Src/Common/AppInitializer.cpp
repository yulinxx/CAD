/**
 * @file AppInitializer.cpp
 * @brief 应用程序初始化器实现
 */

#include "AppInitializer.h"

#include "AppPathManager.h"
#include "CrashHandlerBootstrap.h"
#include "VersionInfo.h"

#include "Log/AprLogger.h"
#include "UI/LanguageManager.h"
#include "UI/FontManager.h"
#include "Persistence/PersistenceService.h"
#include "Persistence/Repositories/SettingsRepository.h"
#include "UI/Settings/SettingsKeysCommon.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFont>
#include <QSettings>

#include <cstring>
#include <memory>

namespace
{
    // 持久化服务的创建地与「兜底持有者」：正常流程中由组合根 takePersistenceService()
    // 取走唯一所有权；应用提前退出时由 shutdown() 兜底销毁。
    static std::unique_ptr<PersistenceService> s_persistenceService;

    // AprLogger 日志目录回调 thunk：C 函数指针 + void* ctx，
    // 返回静态缓冲（避免 std::function/std::string 跨 DLL 传递）
    static char s_logPathBuffer[1024];

    const char* appLogPathThunk(void*)
    {
        const QByteArray utf8 = AppPathManager::logsDir().toUtf8();
        const size_t copyLen = (static_cast<size_t>(utf8.size()) < sizeof(s_logPathBuffer))
            ? static_cast<size_t>(utf8.size())
            : sizeof(s_logPathBuffer) - 1;
        std::memcpy(s_logPathBuffer, utf8.constData(), copyLen);
        s_logPathBuffer[copyLen] = '\0';
        return s_logPathBuffer;
    }
}  // namespace

void AppInitializer::initialize()
{
    AprLogger::SetLogPathCallback(&appLogPathThunk, nullptr);

    AprLogger::GetInstance().Initialize(MainApp::appName().c_str(), AprLogLevel::Info, true, true);

    // 检测应用名称是否正确配置（CMake 模板替换失败时 appName() 为空）
    if (MainApp::appName().empty())
    {
        APR_WARN("[AppInitializer] APP_NAME is empty! Please check CMake configuration: "
                "VersionInfo.h.in and Info.plist.in templates must use @APP_NAME@ (not @app_name@)");
    }

    APR_INFO("==============================================================");
    APR_INFOF("================== Starting %s v%s ==================",
        MainApp::appName().c_str(),
        MainApp::appVersion().c_str());
    APR_INFO("==============================================================\n\n");

    CrashHandlerBootstrap::logPendingDumps();

    auto* languageManager = LanguageManager::instance();
    const QString translationsDir = QCoreApplication::applicationDirPath() + QStringLiteral("/translations");
    languageManager->setTranslationsDir(translationsDir);

    // 启动语言只按系统语言解析（匹配不到由 LanguageManager 回退英文）。
    //
    // 这里刻意不再读 QSettings 的 "General/Language"：那个键全仓库没有任何写入方，
    // 永远是空值，只会让人误以为存在第二套语言存储。语言的唯一持久化位置是
    // SQLite 的 common/language，由 SettingsService::init() → applyCommonSettings()
    // 在数据库就绪后覆盖这里的系统语言结果（见 SettingsPersistenceHelper）。
    const AppLanguage language = LanguageManager::resolveStartupLanguage(QString());
    languageManager->setLanguage(language, translationsDir);

    FontConfig fontConfig;
    fontConfig.fontSize = 9;
    FontManager::apply(fontConfig);
    APR_INFOF("Language set to: %s (dir: %s)",
        languageManager->currentLanguageName().toUtf8().constData(),
        translationsDir.toUtf8().constData());

    // 初始化数据库持久化服务
    // 数据库文件存放在 AppPathManager::dataDir() 下，确保目录存在
    const QString dataDir = AppPathManager::dataDir();
    QDir().mkpath(dataDir);
    APR_INFOF("[AppInitializer] Data directory: %s", dataDir.toUtf8().constData());
    const QString dbPath = AppPathManager::databasePath();
    auto persistenceService = std::make_unique<PersistenceService>();

    if (persistenceService->initialize(dbPath.toStdString()))
    {
        APR_INFOF("[AppInitializer] Database initialized: %s", dbPath.toUtf8().constData());
    }
    else
    {
        APR_ERRORF("[AppInitializer] Failed to initialize database: %s", persistenceService->lastError().c_str());
    }

    s_persistenceService = std::move(persistenceService);
}

void AppInitializer::shutdown()
{
    // 正常退出路径上组合根已 takePersistenceService() 取走所有权，此处为空操作；
    // 应用提前退出（组合根从未创建）时由这里兜底销毁。
    // 先执行清理工作，这些操作会产生日志
    s_persistenceService.reset();

    // 等待前面的日志写入完成
    AprLogger::GetInstance().Flush();

    // 最后打印 shutdown 日志
    APR_INFO("==============================================================");
    APR_INFO("Application shutting down");
    APR_INFO("==============================================================\n\n");

    AprLogger::GetInstance().Shutdown();
}

std::unique_ptr<PersistenceService> AppInitializer::takePersistenceService()
{
    return std::move(s_persistenceService);
}