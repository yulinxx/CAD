/**
 * @file Main/Src/Common/AppInitializer.h
 */
#pragma once

#include <memory>

class PersistenceService;

/**
 * @file AppInitializer.h
 * @brief 应用程序初始化器定义
 *
 * 定义了应用程序初始化器类，负责在启动前执行必要的初始化操作。
 */

/**
 * @class AppInitializer
 * @brief 应用程序初始化器类
 */
class AppInitializer
{
public:
    static void initialize();
    static void shutdown();
    static std::shared_ptr<PersistenceService> persistenceService();
};
