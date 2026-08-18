#pragma once

#include <QString>
#include <QPointer>

#include "Services/UiFrameworkServices.h"
#include "Services/UiServices.h"
#include "Services/UiStateCenter.h"

class QMainWindow;
class UiStateCenter;
class UiThemeService;
class StatusBarBase;
class WorkbenchMenuManager;
class WorkbenchLayoutManager;
class WorkbenchWindow;

/// 工作台状态管理器：统一管理状态中心同步、窗口状态镜像、状态栏刷新
/// 从 WorkbenchWindow 中拆分，遵循单一职责原则
/// 负责：状态中心 ↔ 窗口本地镜像的同步，以及基于状态的 UI 刷新
class WorkbenchStateManager
{
public:
    /// @param parent 主窗口指针（用于 setWindowTitle）
    /// @param menuManager 菜单管理器
    /// @param layoutManager 布局管理器（用于访问 PanelState 和 busyIndicator）
    explicit WorkbenchStateManager(
        WorkbenchWindow* parent, WorkbenchMenuManager* menuManager, WorkbenchLayoutManager* layoutManager);

    ~WorkbenchStateManager();

    // ==================== 服务注入 ====================

    /// 设置状态中心
    void setUiStateCenter(UiStateCenter* stateCenter);
    /// 设置主题服务
    void setThemeService(UiThemeService* themeService);
    /// 设置框架级服务
    void setFrameworkServices(const UiFrameworkServices& services);
    /// 设置当前挂载的工作台状态栏 widget（由 WorkbenchWindow 在 mount/unmount 时同步）
    void setActiveStatusBar(StatusBarBase* statusBarWidget);
    /// 统一设置服务依赖，作为主装配入口
    void configureServices(const UiServices& services);

    // ==================== 状态同步 ====================

    /// 绑定状态中心信号
    void bindStateSignals();
    /// 解除状态中心信号绑定
    void unbindStateSignals();

    /// 同步窗口本地状态与状态中心
    void syncWindowStateFromStateCenter();
    /// 同步选择语义到窗口本地镜像
    void syncWorkbenchSelectionFromStateCenter();

    // ==================== UI 刷新 ====================

    /// 从状态中心刷新界面（状态栏、标题、属性面板、菜单等）
    void refreshFromState();
    /// 刷新状态栏文本
    void refreshStatusText();
    /// 更新窗口标题
    void updateWindowTitle();

    // ==================== 工作台切换状态收尾 ====================

    /// 清理工作台切换期间的状态
    void resetWorkbenchTransientState();
    /// 归零命令状态
    void resetCommandStateToIdle();
    /// 归零工作台相关的本地镜像状态
    void resetWorkbenchLocalMirror();
    /// 清空选择状态
    void clearSelectionState();
    /// 统一写入工作台切换上下文
    void setWorkbenchSwitchContext(const QString& workbenchId, const QString& switchContextText);

    /// 窗口本地状态镜像（与状态中心同构，统一使用 UiStateSnapshot）
    UiStateSnapshot& windowState()
    {
        return m_windowState;
    }

    const UiStateSnapshot& windowState() const
    {
        return m_windowState;
    }

    UiStateCenter* stateCenter() const
    {
        return m_stateCenter;
    }

    UiThemeService* themeService() const
    {
        return m_themeService;
    }

    /// 获取框架级服务引用
    const UiFrameworkServices& frameworkServices() const
    {
        return m_frameworkServices;
    }

    /// 获取当前 UI 服务集合
    const UiServices& uiServices() const
    {
        return m_uiServices;
    }

private:
    /// 统一写入工作台切换阶段，避免直接操作 metadata
    void setWorkbenchTransitionState(const QString& phase, const QString& status);

    WorkbenchWindow* m_parent;
    WorkbenchMenuManager* m_menuManager;
    WorkbenchLayoutManager* m_layoutManager;

    /// UI 状态中心
    UiStateCenter* m_stateCenter{ nullptr };
    /// 主题服务
    UiThemeService* m_themeService{ nullptr };
    /// 框架级服务桥接
    UiFrameworkServices m_frameworkServices;
    /// UI 服务集合
    UiServices m_uiServices;

    /// 窗口状态镜像
    UiStateSnapshot m_windowState;
    /// 当前挂载的工作台状态栏 widget（由 WorkbenchWindow 在 mount/unmount 时同步）
    StatusBarBase* m_activeStatusBar{ nullptr };
};