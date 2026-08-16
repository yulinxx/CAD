/**
 * @file AppInitializer.cpp
 * @brief 应用程序初始化器实现
 */

#include "AppInitializer.h"

#include "AppPathManager.h"
#include "CrashHandlerBootstrap.h"
#include "VersionInfo.h"

#include "Log/SyLogger.h"
#include "UI/LanguageManager.h"
#include "UI/FontManager.h"
#include "Persistence/PersistenceService.h"
#include "Persistence/Repositories/SettingsRepository.h"
#include "UI/Settings/SettingsKeysCommon.h"

#include "render/render_types.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFont>
#include <QSettings>

#include <cstring>

namespace
{
    // 全局持久化服务指针（AppInitializer 创建，CompositionRoot 获取）
    static PersistenceService* s_persistenceService = nullptr;

    // SyLogger 日志目录回调 thunk：C 函数指针 + void* ctx，
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

    // 语言配置项在 QSettings 中的键名（与 ConfigManager 保持一致）
    constexpr const char* kLanguageSettingKey = "General/Language";
}  // namespace

void AppInitializer::initialize()
{
    SyLogger::SetLogPathCallback(&appLogPathThunk, nullptr);

    SyLogger::GetInstance().Initialize(MainApp::appName().c_str(), SyLogLevel::Debug, true, true);

    SY_INFOF("Starting %s v%s", MainApp::appName().c_str(), MainApp::appVersion().c_str());
    SY_INFOF("[Render] Active render backend: %s", render::backendName(render::BackendType::OpenGL));

    CrashHandlerBootstrap::logPendingDumps();

    auto* languageManager = LanguageManager::instance();
    const QString translationsDir = QCoreApplication::applicationDirPath() + QStringLiteral("/translations");
    languageManager->setTranslationsDir(translationsDir);

    // 启动语言解析：
    //   1) 若用户已显式保存语言设置项（QSettings 的 General/Language），则采用之；
    //   2) 否则（首次使用）根据系统语言解析，匹配不到时由 LanguageManager 回退英文。
    // 通过默认构造的 QSettings 读取，与运行时设置的 org/app 名保持一致，从而与
    // ConfigManager 共用同一份配置存储，避免跨模块硬耦合。
    const QString storedLanguageCode = QSettings().value(QString::fromLatin1(kLanguageSettingKey)).toString();
    if (storedLanguageCode.isEmpty())
    {
        SY_INFO("[AppInitializer] No saved language setting (first launch), resolving from system language");
    }
    const AppLanguage language = LanguageManager::resolveStartupLanguage(storedLanguageCode);
    languageManager->setLanguage(language, translationsDir);

    FontConfig fontConfig;
    fontConfig.fontSize = 9;
    FontManager::apply(fontConfig);
    SY_INFOF("Language set to: %s (dir: %s)",
        languageManager->currentLanguageName().toUtf8().constData(),
        translationsDir.toUtf8().constData());

    // 初始化数据库持久化服务
    // 数据库文件存放在 AppPathManager::dataDir() 下，确保目录存在
    const QString dataDir = AppPathManager::dataDir();
    QDir().mkpath(dataDir);
    const QString dbPath = dataDir + QStringLiteral("/cad_database.sqlite");
    auto* persistenceService = new PersistenceService();

    if (persistenceService->initialize(dbPath.toStdString()))
    {
        SY_INFOF("[AppInitializer] Database initialized: %s", dbPath.toUtf8().constData());

        // 读取并应用用户保存的日志设置
        auto* settingsRepo = persistenceService->settings();
        if (settingsRepo)
        {
            // 读取 log_enabled (默认 true)
            std::string logEnabledStr = settingsRepo->loadValue("common", "log_enabled", "true");
            bool logEnabled = (logEnabledStr == "true" || logEnabledStr == "1");
            SyLogger::GetInstance().SetEnabled(logEnabled);

            // 读取 log_level (默认 Debug = 1)
            std::string logLevelStr = settingsRepo->loadValue("common", "log_level", "1");
            int logLevelInt = std::stoi(logLevelStr);
            SyLogger::GetInstance().SetLevel(static_cast<SyLogLevel>(logLevelInt));

            SY_INFOF("[AppInitializer] Log settings applied: enabled=%d, level=%d", logEnabled, logLevelInt);
        }
    }
    else
    {
        SY_ERRORF("[AppInitializer] Failed to initialize database: %s", persistenceService->lastError().c_str());
        // 数据库初始化失败不阻塞启动，UI 层将回退到 QSettings
    }

    // 将 PersistenceService 所有权转移给全局指针（供 CompositionRoot 获取）
    s_persistenceService = persistenceService;
}

void AppInitializer::shutdown()
{
    SY_INFO("Application shutting down");
    delete s_persistenceService;
    s_persistenceService = nullptr;
    SyLogger::GetInstance().Shutdown();
}

PersistenceService* AppInitializer::persistenceService()
{
    return s_persistenceService;
}