#pragma once

#include <QString>

/**
 * @file AppPathManager.h
 * @brief 应用程序路径管理器定义
 *
 * 定义了应用程序路径管理类，负责管理配置文件、资源文件、插件等路径。
 */

class AppPathManager
{
public:
    /// 获取应用程序本地数据根目录（唯一路径控制源）
    /// Windows: C:/Users/<user>/AppData/Local/SanYiCAD/
    /// macOS:   ~/Library/Application Support/SanYiCAD/
    /// Linux:   ~/.local/share/SanYiCAD/
    static QString appLocalDataDir();

    /// 获取配置文件目录路径
    static QString configDir();

    /// 获取资源文件目录路径
    static QString resourcesDir();

    /// 获取插件目录路径
    static QString pluginsDir();

    /// 获取临时文件目录路径
    static QString tempDir();

    /// 获取应用程序可执行文件路径
    static QString appExecutablePath();

    /// 获取应用程序根目录
    /// @return 根目录绝对路径
    static QString appRootDir();

    /// 获取崩溃 minidump 存储目录（跨平台，可写）
    static QString crashDumpsDir();

    /// 获取日志文件存储目录
    static QString logsDir();

    /// 获取数据库文件存储目录
    /// Windows: C:/Users/<user>/AppData/Local/SanYiCAD/data/
    static QString dataDir();
};
