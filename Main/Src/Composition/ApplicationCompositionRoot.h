#pragma once

#include <memory>
#include <vector>

#include "UI/UiCommandDispatcher.h"
#include "UI/UiCommandHandler.h"
#include "UI/UiInteractionDispatcher.h"
#include "UI/UiLayoutService.h"
#include "UI/UiShellHost.h"
#include "UI/UiStateCenter.h"
#include "UI/UiThemeService.h"
#include "UI/SceneDocument2D.h"
#include "UI2D/Operation/OperationBus.h"

#include "Engine2D/Core/SceneManager.h"
#include "Engine2D/Edit/UndoRedoManager.h"
#include "Engine2D/Edit/SceneEditService.h"

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

    /// 获取交互式命令生命周期分发器
    IInteractionDispatcher* interactionDispatcher();

    /// 获取撤销栈
    IUndoStack* undoStack();

    /// 获取操作总线
    OperationBus* operationBus();

private:
    /// 注册所有命令处理器
    void registerCommands();

    /// 通过 CommandHandlerAdapter 将旧命令注册到 OperationBus
    void registerCommandAdapters();

    /// 注册核心编辑操作到 OperationBus（新命令系统直接实现）
    void registerCoreOperations();

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

    /// 撤销栈（旧系统兼容）
    std::unique_ptr<IUndoStack> m_undoStack;

    /// 命令处理器实例集合
    std::vector<std::unique_ptr<ICommandHandler>> m_commandHandlers;

    /// 操作总线（新命令主线）
    std::unique_ptr<OperationBus> m_operationBus;

    /// 场景管理器（新系统核心）
    std::unique_ptr<Eg::SceneManager> m_sceneManager;

    /// 撤销重做管理器（新系统）
    std::unique_ptr<UndoRedoManager> m_undoRedoManager;

    /// 场景编辑服务（新系统）
    std::unique_ptr<SceneEditService> m_sceneEditService;

    /// 2D 场景文档（依赖 SceneEditService）
    std::unique_ptr<SceneDocument2D> m_document2D;
};
