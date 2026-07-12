/**
 * @file AppPathManager.cpp
 * @brief 应用程序路径管理器实现
 */

#include "AppPathManager.h"

#include <QCoreApplication>
#include <QDir>
#include <QStandardPaths>

 /**
  * @brief 获取配置文件目录路径
  *
  * Windows 路径示例：
  * - C:/Users/用户名/AppData/Roaming/组织名/应用名/
  *
  * macOS 路径示例：
  * - ~/Library/Application Support/组织名/应用名/
  *
  * Linux 路径示例：
  * - ~/.config/应用名/
  */
QString AppPathManager::configDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
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
 * Windows: C:/Users/<user>/AppData/Local/<Org>/<App>/crashes/
 * macOS:   ~/Library/Application Support/<Org>/<App>/crashes/
 * Linux:   ~/.local/share/<Org>/<App>/crashes/
 */
QString AppPathManager::crashDumpsDir()
{
    const QString baseDir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (baseDir.isEmpty())
    {
        return QCoreApplication::applicationDirPath() + QStringLiteral("/crashes");
    }
    return baseDir + QStringLiteral("/crashes");
}