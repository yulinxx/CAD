#pragma once

#include <memory>
#include <vector>

#include "UI/UiCommandDispatcher.h"
#include "UI/UiCommandHandler.h"
#include "UI/UiLayoutService.h"
#include "UI/UiShellHost.h"
#include "UI/UiStateCenter.h"
#include "UI/UiThemeService.h"

/**
 * @class ApplicationCompositionRoot
 * @brief 应用程序组合根类
 * 
 * 负责创建和组装所有核心服务，包括状态中心、主题服务、
 * 布局服务、命令分发器和 UI Shell 宿主。
 */
class ApplicationCompositionRoot
{
public:
    ApplicationCompositionRoot();

public:
    /// 获取 UI Shell 宿主
    UiShellHost* shellHost();

    /// 获取状态中心
    UiStateCenter* stateCenter();

    /// 获取主题服务
    UiThemeService* themeService();

    /// 获取布局服务
    UiLayoutService* layoutService();

    /// 获取命令分发器
    UiCommandDispatcher* commandDispatcher();

    /// 获取撤销栈
    IUndoStack* undoStack();

private:
    /// 注册所有命令处理器
    void registerCommands();

private:
    /// UI Shell 宿主
    std::unique_ptr<UiShellHost> m_shellHost;

    /// UI 状态中心
    std::unique_ptr<UiStateCenter> m_stateCenter;

    /// 主题服务
    std::unique_ptr<UiThemeService> m_themeService;

    /// 布局服务
    std::unique_ptr<UiLayoutService> m_layoutService;

    /// 命令分发器
    std::unique_ptr<UiCommandDispatcher> m_commandDispatcher;

    /// 撤销栈
    std::unique_ptr<IUndoStack> m_undoStack;

    /// 命令处理器实例集合
    std::vector<std::unique_ptr<ICommandHandler>> m_commandHandlers;
};
