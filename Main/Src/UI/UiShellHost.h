#pragma once

#include <memory>

#include "UiCommandHandler.h"
#include "UiFrameworkServices.h"
#include "UiServices.h"

class UiCommandDispatcher;
class UiStateCenter;
class UiThemeService;
class UiWorkbench;
class WorkbenchWindow;

/**
 * @class UiShellHost
 * @brief UI Shell 宿主类
 *
 * 负责创建和管理主窗口，初始化工作台，以及处理主题切换和布局管理。
 * 作为 UI 层的入口点，协调各个服务组件的初始化和交互。
 * 宿主层只做编排，不负责窗口内部状态细节。
 */
class UiShellHost
{
public:

    UiShellHost();
    ~UiShellHost();

public:
    /// 设置状态中心
    /// @param stateCenter UI 状态中心
    /// 仅转发引用，不做额外初始化
    void setStateCenter(UiStateCenter* stateCenter);

    /// 设置主题服务
    /// @param themeService 主题服务
    /// 仅转发引用，不提前加载主题
    void setThemeService(UiThemeService* themeService);

    /// 设置命令分发器
    /// @param dispatcher 命令分发器
    void setCommandDispatcher(UiCommandDispatcher* dispatcher);

    /// 设置撤销栈
    /// @param undoStack 撤销栈
    void setUndoStack(IUndoStack* undoStack);

    /// 设置 UI 服务集合
    /// @param services UI 服务集合
    void setUiServices(const UiServices& services);

    /// 设置框架级横切服务（错误上报、权限、性能采样）
    void setFrameworkServices(const UiFrameworkServices& services);

    /// 设置工作台
    /// @param workbench 工作台实例
    /// 仅转发引用，不触发切换流程
    void setWorkbench(UiWorkbench* workbench);

    /// 初始化并显示主窗口
    void initializeAndShow();

    /// 切换工作台（按 ID）
    /// 惰性创建 3D 工作台
    void switchWorkbench(const QString& workbenchId);

    /// 切换工作台（直接传入实例）
    void switchWorkbench(UiWorkbench* workbench);

    /// 获取主窗口指针
    WorkbenchWindow* mainWindow();

    /// 关闭宿主并释放工作台关联
    void shutdown();

private:
    /// 按 ID 解析工作台（惰性创建 3D 工作台）
    UiWorkbench* resolveWorkbench(const QString& workbenchId);

    /// 主窗口
    std::unique_ptr<WorkbenchWindow> m_mainWindow;
    /// UI 状态中心引用
    UiStateCenter* m_stateCenter{ nullptr };
    /// 主题服务引用
    UiThemeService* m_themeService{ nullptr };
    /// 命令分发器引用
    UiCommandDispatcher* m_commandDispatcher{ nullptr };
    /// 撤销栈引用
    IUndoStack* m_undoStack{ nullptr };
    /// UI 服务集合
    UiServices m_services;
    /// 当前工作台引用
    UiWorkbench* m_workbench{ nullptr };
    /// 3D 工作台（惰性创建，宿主拥有）
    std::unique_ptr<UiWorkbench> m_workbench3D;
};
