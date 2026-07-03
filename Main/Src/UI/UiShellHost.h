/**
 * @file Main/Src/UI/UiShellHost.h
 */
#pragma once

#include <memory>

class UiStateCenter;
class UiThemeService;
class UiWorkbench;
class WorkbenchWindow;

/**
 * @file UiShellHost.h
 * @brief UI Shell 宿主类定义
 *
 * 定义了 UI Shell 宿主类，负责管理主窗口和工作台的生命周期。
 */

 /**
  * @class UiShellHost
  * @brief UI Shell 宿主类
  *
  * 负责创建和管理主窗口，初始化工作台，以及处理主题切换和布局管理。
  * 作为 UI 层的入口点，协调各个服务组件的初始化和交互。
  */
class UiShellHost
{
public:
    /// 构造函数
    UiShellHost();
    ~UiShellHost();

    /// 设置状态中心
    /// @param stateCenter UI 状态中心
    void setStateCenter(UiStateCenter* stateCenter);

    /// 设置主题服务
    /// @param themeService 主题服务
    void setThemeService(UiThemeService* themeService);

    /// 设置工作台
    /// @param workbench 工作台实例
    void setWorkbench(UiWorkbench* workbench);

    /// 初始化并显示主窗口
    void initializeAndShow();

    /// 切换工作台
    void switchWorkbench(UiWorkbench* workbench);

    /// 获取主窗口指针
    WorkbenchWindow* mainWindow();

private:
    /// 主窗口
    std::unique_ptr<WorkbenchWindow> m_mainWindow;
    /// UI 状态中心引用
    UiStateCenter* m_stateCenter{ nullptr };
    /// 主题服务引用
    UiThemeService* m_themeService{ nullptr };
    /// 当前工作台引用
    UiWorkbench* m_workbench{ nullptr };
};
