/**
 * @file AppPathManager.cpp
 * @brief 应用程序路径管理器实现
 */

#include "AppPathManager.h"

#include <QCoreApplication>
#include <QStandardPaths>

QString AppPathManager::configDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
}

QString AppPathManager::resourcesDir()
{
    return QCoreApplication::applicationDirPath() + QStringLiteral("/resources");
}

QString AppPathManager::pluginsDir()
{
    return QCoreApplication::applicationDirPath() + QStringLiteral("/plugins");
}

QString AppPathManager::tempDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::TempLocation);
}

QString AppPathManager::appExecutablePath()
{
    return QCoreApplication::applicationFilePath();
}

QString AppPathManager::appRootDir()
{
    return QCoreApplication::applicationDirPath();
}