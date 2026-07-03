/**
 * @file Main/Src/Common/AppInitializer.h
 */
#pragma once

 /**
  * @file AppInitializer.h
  * @brief 应用程序初始化器定义
  *
  * 定义了应用程序初始化器类，负责在启动前执行必要的初始化操作。
  */

  /**
   * @class AppInitializer
   * @brief 应用程序初始化器类
   *
   * 负责执行应用程序启动前的初始化操作，包括：
   * - 初始化 Qt 应用程序属性
   * - 设置应用程序路径
   * - 注册资源文件
   * - 初始化日志系统
   */
class AppInitializer
{
public:
    /// 执行应用程序初始化
    static void initialize();
};
