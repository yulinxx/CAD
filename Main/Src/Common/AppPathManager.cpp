#include "AppPathManager.h"
#include <filesystem>

#ifdef _WIN32
#include <Windows.h>
#include <ShlObj.h>
#endif

namespace fs = std::filesystem;

namespace Ut
{
    AppPathManager& AppPathManager::instance()
    {
        static AppPathManager instance;
        return instance;
    }

    AppPathManager::AppPathManager()
    {
    }

    AppPathManager::~AppPathManager()
    {
    }

    bool AppPathManager::initialize(const std::string& appName)
    {
        if (m_initialized)
            return true;

        m_appName = appName;

        if (m_appRootPath.empty())
        {
#ifdef _WIN32
            wchar_t* path = nullptr;
            if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, NULL, &path)))
            {
                int size = WideCharToMultiByte(CP_UTF8, 0, path, -1, nullptr, 0, nullptr, nullptr);
                m_appRootPath.resize(size - 1);
                WideCharToMultiByte(CP_UTF8, 0, path, -1, &m_appRootPath[0], size, nullptr, nullptr);
                CoTaskMemFree(path);
                // 使用 std::filesystem::path 进行平台无关的路径拼接
                m_appRootPath = (fs::path(m_appRootPath) / appName).string();
            }
            else
            {
                m_appRootPath = (fs::path(".\\data") / appName).string();
            }
#else
            const char* home = getenv("HOME");
            if (home)
            {
                m_appRootPath = (fs::path(home) / ".local/share" / appName).string();
            }
            else
            {
                m_appRootPath = (fs::path("./data") / appName).string();
            }
#endif
        }

        // 使用 std::filesystem::path 进行平台无关的子路径拼接
        fs::path rootPath(m_appRootPath);
        m_logsPath = (rootPath / "logs").string();
        m_crashDumpsPath = (rootPath / "crash_dumps").string();
        m_databasePath = (rootPath / "database").string();
        m_settingsPath = (rootPath / "settings").string();
        m_cachePath = (rootPath / "cache").string();
        m_resourcesPath = (rootPath / "resources").string();

        return createDirectories();
    }

    void AppPathManager::setCustomRootPath(const std::string& customPath)
    {
        m_appRootPath = customPath;
        m_initialized = false;
    }

    bool AppPathManager::createDirectories()
    {
        try
        {
            fs::create_directories(m_logsPath);
            fs::create_directories(m_crashDumpsPath);
            fs::create_directories(m_databasePath);
            fs::create_directories(m_settingsPath);
            fs::create_directories(m_cachePath);
            fs::create_directories(m_resourcesPath);
            m_initialized = true;
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    std::string AppPathManager::getAppRootPath() const
    {
        return m_appRootPath;
    }

    std::string AppPathManager::getLogsPath() const
    {
        return m_logsPath;
    }

    std::string AppPathManager::getCrashDumpsPath() const
    {
        return m_crashDumpsPath;
    }

    std::string AppPathManager::getDatabasePath() const
    {
        return m_databasePath;
    }

    std::string AppPathManager::getSettingsPath() const
    {
        return m_settingsPath;
    }

    std::string AppPathManager::getCachePath() const
    {
        return m_cachePath;
    }

    std::string AppPathManager::getResourcesPath() const
    {
        return m_resourcesPath;
    }

    std::string AppPathManager::getDatabaseFilePath() const
    {
        return (fs::path(m_databasePath) / "cad_database.sqlite").string();
    }

    bool AppPathManager::isInitialized() const
    {
        return m_initialized;
    }
}