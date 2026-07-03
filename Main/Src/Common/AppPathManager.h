/**
 * @file Main/Src/Common/AppPathManager.h
 */
#pragma once

#include <QString>

 /**
  * @file AppPathManager.h
  * @brief 应用程序路径管理器定义
  *
  * 定义了应用程序路径管理类，负责管理配置文件、资源文件、插件等路径。
  */

  /**
   * @class AppPathManager
   * @brief 应用程序路径管理器类
   *
   * 提供应用程序相关路径的统一管理，包括：
   * - 配置文件路径
   * - 资源文件路径
   * - 插件目录路径
   * - 临时文件路径
   */
class AppPathManager
{
public:
    /// 获取配置文件目录路径
    /// @return 配置文件目录绝对路径
    static QString configDir();
    /// 获取资源文件目录路径
    /// @return 资源文件目录绝对路径
    static QString resourcesDir();
    /// 获取插件目录路径
    /// @return 插件目录绝对路径
    static QString pluginsDir();
    /// 获取临时文件目录路径
    /// @return 临时文件目录绝对路径
    static QString tempDir();
    /// 获取应用程序可执行文件路径
    /// @return 可执行文件绝对路径
    static QString appExecutablePath();

    /// 获取应用程序根目录
    /// @return 根目录绝对路径
    static QString appRootDir();
};
