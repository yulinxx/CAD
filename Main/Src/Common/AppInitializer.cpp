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

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFont>
#include <QLocale>

namespace
{
    // 全局持久化服务指针（AppInitializer 创建，CompositionRoot 获取）
    static PersistenceService* s_persistenceService = nullptr;

    AppLanguage detectSystemLanguage()
    {
        const QString locale = QLocale::system().name();

        struct LocaleMapping
        {
            const char* prefix;
            AppLanguage language;
        };

        static const LocaleMapping mappings[] = {
            { "zh", AppLanguage::Chinese },
            { "ja", AppLanguage::Japanese },
            { "ko", AppLanguage::Korean },
            { "fr", AppLanguage::French },
            { "de", AppLanguage::German },
            { "es", AppLanguage::Spanish },
            { "pt", AppLanguage::Portuguese },
            { "ru", AppLanguage::Russian },
            { "ar", AppLanguage::Arabic },
            { "it", AppLanguage::Italian },
            { "hi", AppLanguage::Hindi },
            { "tr", AppLanguage::Turkish },
            { "vi", AppLanguage::Vietnamese },
        };

        for (const auto& mapping : mappings)
        {
            if (locale.startsWith(QString::fromLatin1(mapping.prefix)))
                return mapping.language;
        }

        return AppLanguage::English;
    }
}

void AppInitializer::initialize()
{
    SyLogger::SetLogPathCallback([]() {
        return AppPathManager::logsDir().toStdString();
    });

    SyLogger::GetInstance().Initialize(
        MainApp::appName().c_str(),
        SyLogLevel::Debug,
        true,
        true);
    SY_INFOF("Starting %s v%s", MainApp::appName().c_str(), MainApp::appVersion().c_str());

    CrashHandlerBootstrap::logPendingDumps();

    auto* languageManager = LanguageManager::instance();
    const QString translationsDir = QCoreApplication::applicationDirPath()
        + QStringLiteral("/translations");
    languageManager->setTranslationsDir(translationsDir);

    const AppLanguage language = detectSystemLanguage();
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
    }
    else
    {
        SY_ERRORF("[AppInitializer] Failed to initialize database: %s",
            persistenceService->lastError().c_str());
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