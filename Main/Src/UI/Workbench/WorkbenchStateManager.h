#pragma once

#include <QString>
#include <QPointer>
#include <QTimer>

#include "Services/UiFrameworkServices.h"
#include "Services/UiStateCenter.h"

class QMainWindow;
class UiStateCenter;
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
    /// 设置框架级服务
    void setFrameworkServices(const UiFrameworkServices& services);
    /// 设置当前挂载的工作台状态栏 widget（由 WorkbenchWindow 在 mount/unmount 时同步）
    /// 定义在 .cpp：QPointer 的赋值/访问需要 StatusBarBase 完整类型
    void setActiveStatusBar(StatusBarBase* statusBarWidget);

    // ==================== 状态同步 ====================

    /// 绑定状态中心信号
    void bindStateSignals();
    /// 解除状态中心信号绑定
    void unbindStateSignals();

    /// 同步窗口本地状态与状态中心
    void syncWindowStateFromStateCenter(const UiStateSnapshot& state);
    /// 无参版本（从状态中心取 snapshot 后调用带参版本）
    void syncWindowStateFromStateCenter();
    /// 同步选择语义到窗口本地镜像
    void syncWorkbenchSelectionFromStateCenter(const UiStateSnapshot& state);
    /// 无参版本
    void syncWorkbenchSelectionFromStateCenter();

    // ==================== UI 刷新 ====================

    /// 从状态中心刷新界面（状态栏、标题、属性面板、菜单等）
    void refreshFromState();
    /// 刷新状态栏文本
    void refreshStatusText(const UiStateSnapshot& state);
    /// 无参版本
    void refreshStatusText();
    /// 更新窗口标题
    void updateWindowTitle(const UiStateSnapshot& state);
    /// 无参版本
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

    /// 获取框架级服务引用
    const UiFrameworkServices& frameworkServices() const
    {
        return m_frameworkServices;
    }

private:
    /// 统一写入工作台切换阶段，避免直接操作 metadata
    void setWorkbenchTransitionState(const QString& phase, const QString& status);

    /// 延迟刷新的实际实现（由 m_refreshCoalescer 定时器调用，复用单个 snapshot）
    void doRefreshFromState();

    WorkbenchWindow* m_parent;
    WorkbenchMenuManager* m_menuManager;
    WorkbenchLayoutManager* m_layoutManager;

    /// UI 状态中心
    UiStateCenter* m_stateCenter{ nullptr };
    /// 框架级服务桥接
    UiFrameworkServices m_frameworkServices;

    /// 窗口状态镜像
    UiStateSnapshot m_windowState;
    /// 当前挂载的工作台状态栏 widget（由 WorkbenchWindow 在 mount/unmount 时同步；
    /// 与 WorkbenchWindow::m_activeStatusBar 同为 QPointer，对端销毁自动置空）
    QPointer<StatusBarBase> m_activeStatusBar;

    /// 刷新去重：多个信号在同一事件循环内密集触发时，合并为一次刷新
    QTimer m_refreshCoalescer;
};