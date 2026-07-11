/**
 * @file AppInitializer.cpp
 * @brief 应用程序初始化器实现
 */

#include "AppInitializer.h"

#include "AppPathManager.h"
#include "VersionInfo.h"

#include "Log/SyLogger.h"
#include "UI/LanguageManager.h"

#include <QApplication>
#include <QCoreApplication>
#include <QFont>
#include <QLocale>

namespace
{
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
    SyLogger::GetInstance().Initialize(
        MainApp::appName().c_str(),
        SyLogLevel::Debug,
        true,
        true);
    SY_INFOF("Starting %s v%s", MainApp::appName().c_str(), MainApp::appVersion().c_str());

    QApplication::setFont(QFont(QStringLiteral("Segoe UI"), 9));

    auto* languageManager = LanguageManager::instance();
    const QString translationsDir = QCoreApplication::applicationDirPath()
        + QStringLiteral("/translations");
    languageManager->setTranslationsDir(translationsDir);

    const AppLanguage language = detectSystemLanguage();
    languageManager->setLanguage(language, translationsDir);
    SY_INFOF("Language set to: %s (dir: %s)",
        languageManager->currentLanguageName().toUtf8().constData(),
        translationsDir.toUtf8().constData());
}

void AppInitializer::shutdown()
{
    SY_INFO("Application shutting down");
    SyLogger::GetInstance().Shutdown();
}
