#pragma once

#include <memory>

#include "UiFrameworkServices.h"

class EntityDocument2D;
class UiCommandDispatcher;
class IInteractionDispatcher;
class UiLayoutService;
class UiStateCenter;
class UiThemeService;
class IUndoStack;
class OperationBus;

/**
 * @file UiServices.h
 * @brief UI 服务集合定义
 *
 * 定义了 UI 层所需的所有服务的聚合结构，便于依赖注入和统一管理。
 */

/**
 * @struct UiServices
 * @brief UI 服务集合
 *
 * 聚合了 UI 层所需的所有服务，包括状态中心、主题服务、
 * 布局服务、命令分发器和撤销栈。通过组合模式统一管理服务依赖。
 */
struct UiServices
{
    /// UI 状态中心
    UiStateCenter* stateCenter{ nullptr };

    /// 主题服务
    UiThemeService* themeService{ nullptr };

    /// 布局服务
    UiLayoutService* layoutService{ nullptr };

    /// 命令分发器
    UiCommandDispatcher* commandDispatcher{ nullptr };

    /// 交互式命令生命周期分发器
    IInteractionDispatcher* interactionDispatcher{ nullptr };

    /// 撤销栈
    IUndoStack* undoStack{ nullptr };

    /// 操作总线（新操作主线）
    OperationBus* operationBus{ nullptr };

    /// 2D 文档（命令系统需要访问文档进行实体操作）
    EntityDocument2D* document2D{ nullptr };

    /// 将框架级桥接信息写入到服务集合中
    /// @param frameworkServices 框架级服务
    /// @return 当前服务集合引用
    UiServices& withFrameworkServices(const UiFrameworkServices& frameworkServices)
    {
        (void)frameworkServices;
        return *this;
    }
};
