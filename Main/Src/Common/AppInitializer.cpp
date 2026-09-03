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

// 渲染 DLL 的唯一公共头。这里只用它做后端名字符串化。
#include "render/renderx.h"

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
}  // namespace

void AppInitializer::initialize()
{
    SyLogger::SetLogPathCallback(&appLogPathThunk, nullptr);

    SyLogger::GetInstance().Initialize(MainApp::appName().c_str(), SyLogLevel::Debug, true, true);

    SY_INFO("==============================================================");
    SY_INFOF("Starting %s v%s", MainApp::appName().c_str(), MainApp::appVersion().c_str());
    SY_INFO("==============================================================");
    SY_INFOF("[Render] Active render backend: %s",
             Render::RT::rxBackendName(Render::RT::Backend::OpenGL));

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
            // 日志设置由 SettingsService 统一管理（在 UI 模块初始化时应用）
            // 这里只设置默认值，确保启动时日志可用
            // 默认使用 Info 级别，让用户能看到关键信息
            SyLogger::GetInstance().SetLevel(SyLogLevel::Info);
            SY_INFO("[AppInitializer] Default log level set to Info (will be overridden by SettingsService if available)");

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