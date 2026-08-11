/**
 * @file AppPathManager.cpp
 * @brief 应用程序路径管理器实现
 *
 * 所有应用数据路径都从 appLocalDataDir() 派生，确保路径统一管理。
 */

#include "AppPathManager.h"

#include "VersionInfo.h"

#include <QDir>
#include <QCoreApplication>
#include <QStandardPaths>

 /// 应用名称常量（唯一路径控制源）
static const QString kAppName = QStringLiteral("SanYiCAD");

/**
 * @brief 获取应用程序本地数据根目录（唯一路径控制源）
 *
 * 所有应用数据（配置、日志、崩溃dump）都从这个目录派生。
 *
 * Windows: C:/Users/<user>/AppData/Local/SanYiCAD/
 * macOS:   ~/Library/Application Support/SanYiCAD/
 * Linux:   ~/.local/share/SanYiCAD/
 */
QString AppPathManager::appLocalDataDir()
{
#ifdef _WIN32
    const QString baseDir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    if (baseDir.isEmpty())
    {
        return QCoreApplication::applicationDirPath() + QStringLiteral("/") + kAppName;
    }
    return baseDir + QStringLiteral("/") + kAppName;
#else
    const QString baseDir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (baseDir.isEmpty())
    {
        return QCoreApplication::applicationDirPath() + QStringLiteral("/") + kAppName;
    }
    // 移除 Qt 默认添加的组织名/应用名后缀，只保留基础路径
    QString result = baseDir;
    if (result.endsWith(QStringLiteral("/") + kAppName))
    {
        result.chop(kAppName.length() + 1);
    }
    const QString orgName = QString::fromStdString(MainApp::organizationName());
    if (result.endsWith(QStringLiteral("/") + orgName))
    {
        result.chop(orgName.length() + 1);
    }
    return result + QStringLiteral("/") + kAppName;
#endif
}

/**
 * @brief 获取配置文件目录路径
 *
 * 路径构成：appLocalDataDir() / config
 * Windows: C:/Users/<user>/AppData/Local/SanYiCAD/config/
 */
QString AppPathManager::configDir()
{
    return appLocalDataDir() + QStringLiteral("/config");
}

/**
 * @brief 获取资源文件目录路径
 *
 * 路径构成：应用程序可执行文件所在目录 + /resources
 */
QString AppPathManager::resourcesDir()
{
    return QCoreApplication::applicationDirPath() + QStringLiteral("/resources");
}

/**
 * @brief 获取插件目录路径
 *
 * 路径构成：应用程序可执行文件所在目录 + /plugins
 *
 * 典型路径示例（假设 exe 在 C:/Program Files/SanYiCAD/）：
 * - C:/Program Files/SanYiCAD/plugins/
 */
QString AppPathManager::pluginsDir()
{
    return QCoreApplication::applicationDirPath() + QStringLiteral("/plugins");
}

/**
 * @brief 获取临时文件目录路径
 *
 * Windows 路径示例：
 * - C:/Users/用户名/AppData/Local/Temp/
 *
 * macOS 路径示例：
 * - /tmp/
 *
 * Linux 路径示例：
 * - /tmp/
 */
QString AppPathManager::tempDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::TempLocation);
}

/**
 * @brief 获取应用程序可执行文件完整路径
 *
 * 路径示例：
 * - C:/Program Files/SanYiCAD/SanYiCAD.exe
 */
QString AppPathManager::appExecutablePath()
{
    return QCoreApplication::applicationFilePath();
}

/**
 * @brief 获取应用程序根目录（可执行文件所在目录）
 *
 * 路径示例（假设 exe 在 C:/Program Files/SanYiCAD/）：
 * - C:/Program Files/SanYiCAD/
 */
QString AppPathManager::appRootDir()
{
    return QCoreApplication::applicationDirPath();
}

/**
 * @brief 获取崩溃 minidump 存储目录
 *
 * 路径构成：appLocalDataDir() / crashes
 * Windows: C:/Users/<user>/AppData/Local/SanYiCAD/crashes/
 */
QString AppPathManager::crashDumpsDir()
{
    return appLocalDataDir() + QStringLiteral("/crashes");
}

/**
 * @brief 获取日志文件存储目录
 *
 * 路径构成：appLocalDataDir() / logs
 * Windows: C:/Users/<user>/AppData/Local/SanYiCAD/logs/
 */
QString AppPathManager::logsDir()
{
    return appLocalDataDir() + QStringLiteral("/logs");
}

/**
 * @brief 获取数据库文件存储目录
 *
 * 路径构成：appLocalDataDir() / data
 * Windows: C:/Users/<user>/AppData/Local/SanYiCAD/data/
 */
QString AppPathManager::dataDir()
{
    return appLocalDataDir() + QStringLiteral("/data");
}