/**
 * @file Main/Src/UI/UiServices.h
 */
#pragma once

#include <memory>

class UiCommandDispatcher;
class UiLayoutService;
class UiStateCenter;
class UiThemeService;

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
  * 布局服务和命令分发器。通过组合模式统一管理服务依赖。
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
};
