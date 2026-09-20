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
    /// Windows: C:/Users/<user>/AppData/Local/SanYiCAD/config/
    /// macOS:   ~/Library/Application Support/SanYiCAD/config/
    /// Linux:   ~/.local/share/SanYiCAD/config/
    static QString configDir();

    /// 获取资源文件目录路径（相对于 exe 所在目录）
    /// Windows: C:/Program Files/SanYiCAD/resources/
    /// macOS:   /Applications/SanYiCAD.app/Contents/Resources/
    /// Linux:   /opt/SanYiCAD/resources/
    static QString resourcesDir();

    /// 获取插件目录路径（相对于 exe 所在目录）
    /// Windows: C:/Program Files/SanYiCAD/plugins/
    /// macOS:   /Applications/SanYiCAD.app/Contents/PlugIns/
    /// Linux:   /opt/SanYiCAD/plugins/
    static QString pluginsDir();

    /// 获取临时文件目录路径（系统临时目录）
    /// Windows: C:/Users/<user>/AppData/Local/Temp/
    /// macOS:   /tmp/
    /// Linux:   /tmp/
    static QString tempDir();

    /// 获取应用程序可执行文件完整路径
    /// Windows: C:/Program Files/SanYiCAD/SanYiCAD.exe
    /// macOS:   /Applications/SanYiCAD.app/Contents/MacOS/SanYiCAD
    /// Linux:   /opt/SanYiCAD/bin/SanYiCAD
    static QString appExecutablePath();

    /// 获取应用程序根目录（可执行文件所在目录）
    /// Windows: C:/Program Files/SanYiCAD/
    /// macOS:   /Applications/SanYiCAD.app/Contents/MacOS/
    /// Linux:   /opt/SanYiCAD/bin/
    static QString appRootDir();

    /// 获取崩溃 minidump 存储目录
    /// Windows: C:/Users/<user>/AppData/Local/SanYiCAD/crashes/
    /// macOS:   ~/Library/Application Support/SanYiCAD/crashes/
    /// Linux:   ~/.local/share/SanYiCAD/crashes/
    static QString crashDumpsDir();

    /// 获取日志文件存储目录
    /// Windows: C:/Users/<user>/AppData/Local/SanYiCAD/logs/
    /// macOS:   ~/Library/Application Support/SanYiCAD/logs/
    /// Linux:   ~/.local/share/SanYiCAD/logs/
    static QString logsDir();

    /// 获取数据库文件存储目录
    /// Windows: C:/Users/<user>/AppData/Local/SanYiCAD/data/
    /// macOS:   ~/Library/Application Support/SanYiCAD/data/
    /// Linux:   ~/.local/share/SanYiCAD/data/
    static QString dataDir();

    /// 获取安全模块配置文件路径
    /// Windows: C:/Users/<user>/AppData/Local/SanYiCAD/config/safety_users.conf
    /// macOS:   ~/Library/Application Support/SanYiCAD/config/safety_users.conf
    /// Linux:   ~/.local/share/SanYiCAD/config/safety_users.conf
    static QString safetyConfigPath();

    /// 获取材质数据库文件路径
    /// Windows: C:/Users/<user>/AppData/Local/SanYiCAD/config/materials.json
    /// macOS:   ~/Library/Application Support/SanYiCAD/config/materials.json
    /// Linux:   ~/.local/share/SanYiCAD/config/materials.json
    static QString materialDatabasePath();

    /// 获取缓存目录路径
    /// Windows: C:/Users/<user>/AppData/Local/SanYiCAD/cache/
    /// macOS:   ~/Library/Application Support/SanYiCAD/cache/
    /// Linux:   ~/.local/share/SanYiCAD/cache/
    static QString cacheDir();
};
