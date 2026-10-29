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

    /// 取走持久化服务的唯一所有权（由 ApplicationCompositionRoot 接管）。
    ///
    /// 单一所有权契约：本类只负责创建，销毁时机由持有方决定。
    /// 若应用提前退出（如许可闸门拒绝）组合根从未创建，对象留在本处，
    /// 由 shutdown() 在进程退出时销毁，保证任何路径下都恰好析构一次。
    static std::unique_ptr<PersistenceService> takePersistenceService();
};
