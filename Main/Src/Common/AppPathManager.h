#pragma once

#include <string>
#include <memory>

namespace Ut
{
    class AppPathManager
    {
    public:
        static AppPathManager& instance();

        bool initialize(const std::string& appName);

        std::string getAppRootPath() const;
        std::string getLogsPath() const;
        std::string getCrashDumpsPath() const;
        std::string getDatabasePath() const;
        std::string getSettingsPath() const;
        std::string getCachePath() const;
        std::string getResourcesPath() const;

        std::string getDatabaseFilePath() const;

        void setCustomRootPath(const std::string& customPath);

        bool isInitialized() const;

    private:
        AppPathManager();
        ~AppPathManager();
        AppPathManager(const AppPathManager&) = delete;
        AppPathManager& operator=(const AppPathManager&) = delete;

        bool createDirectories();

        std::string m_appName;
        std::string m_appRootPath;
        std::string m_logsPath;
        std::string m_crashDumpsPath;
        std::string m_databasePath;
        std::string m_settingsPath;
        std::string m_cachePath;
        std::string m_resourcesPath;
        bool m_initialized = false;
    };
}
