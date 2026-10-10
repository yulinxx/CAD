#pragma once

#include <functional>
#include <memory>

#include <QMainWindow>
#include <QMetaObject>
#include <QPointer>

#include "UiFrameworkServices.h"
#include "UiServices.h"

class QAction;
class QActionGroup;
class QDockWidget;
class QLabel;
class QMenu;
class QProgressBar;
class QShortcut;
class QStatusBar;
class QToolBar;
class OperationBus;
class StatusBarBase;
class UnitManager;
class UiStateCenter;
class UiWorkbench;
class WorkbenchMenuManager;
class WorkbenchLayoutManager;
class WorkbenchActionManager;
class WorkbenchStateManager;
class SceneTreePanel;
class PropertiesPanelWidget;
class FileDropHandler;
class QDragEnterEvent;
class QDragMoveEvent;
class QDragLeaveEvent;
class QDropEvent;

/// 工作台切换工厂：按 ID 返回对应的工作台实例
using WorkbenchFactory = std::function<UiWorkbench*(const QString& workbenchId)>;

/**
 * @class WorkbenchWindow
 * @brief 工作台主窗口类
 *
 * 应用程序的主窗口，管理所有 UI 组件的布局和交互。
 */
class WorkbenchWindow : public QMainWindow
{
    Q_OBJECT

public:
    /// @param parent 父部件
    explicit WorkbenchWindow(QWidget* parent = nullptr);
    ~WorkbenchWindow() override;

    /// 文件拖放处理器（供上层注入坐标转换等回调）
    class FileDropHandler* fileDropHandler() const
    {
        return m_fileDropHandler.get();
    }

protected:
    /// 语言切换事件处理
    void changeEvent(QEvent* event) override;
    /// 窗口关闭事件处理（拦截未保存更改）
    void closeEvent(QCloseEvent* event) override;
    /// 文件拖放进入事件（对接 FileDropHandler → ImportService）
    void dragEnterEvent(QDragEnterEvent* event) override;
    /// 文件拖放移动事件
    void dragMoveEvent(QDragMoveEvent* event) override;
    /// 文件拖放离开事件
    void dragLeaveEvent(QDragLeaveEvent* event) override;
    /// 文件拖放释放事件（对接 FileDropHandler → ImportService）
    void dropEvent(QDropEvent* event) override;

public:
    /// 设置状态中心
    /// @param stateCenter UI 状态中心
    void setUiStateCenter(UiStateCenter* stateCenter);
    /// 设置操作总线
    /// @param bus 操作总线
    void setOperationBus(OperationBus* bus);
    /// 统一设置服务依赖，作为主装配入口
    /// @param services UI 服务集合
    void configureServices(const UiServices& services);
    /// 初始化工作台窗口骨架
    /// 仅负责顶层容器与基础入口，不在此处挂接具体工作台业务
    void initializeWorkbenchShell();
    /// 设置框架级服务桥接
    /// @param services 框架级服务集合
    void setFrameworkServices(const UiFrameworkServices& services);
    /// 设置 UI 服务集合，并同步到底层框架桥接
    /// @param services UI 服务集合
    void setUiServices(const UiServices& services);
    /// 设置当前工作台
    /// @param workbench 工作台实例
    void setWorkbench(UiWorkbench* workbench);
    /// 设置工作台切换工厂
    /// @param factory 工作台切换工厂回调
    void setWorkbenchFactory(WorkbenchFactory factory);

    /// 更新状态栏鼠标坐标显示
    /// @param x 世界坐标 X（毫米）
    /// @param y 世界坐标 Y（毫米）
    void updatePositionLabel(double x, double y);

    /// 注册停靠面板
    /// @param title 面板标题
    /// @param widget 面板内容部件
    /// @param area 停靠区域
    QDockWidget* registerDockWidget(const QString& title, QWidget* widget, Qt::DockWidgetArea area);
    /// 注册工具栏
    /// @param title 工具栏标题
    QToolBar* registerToolBar(const QString& title);
    /// 清空工作台内容
    void clearWorkbenchContent();

    // ==================== 状态栏挂载/卸载 ====================

    /// 挂载工作台状态栏 widget
    void mountStatusBar(StatusBarBase* statusBarWidget);
    /// 卸载当前工作台状态栏 widget
    void unmountStatusBar();

    /// 获取当前挂载的工作台状态栏 widget（可能为空；QPointer 与工作台侧同策略，
    /// widget 由 QStatusBar 持有所有权，对端销毁时自动置空）。
    /// 定义在 .cpp：QPointer 的 T* 转换需要 StatusBarBase 完整类型。
    StatusBarBase* activeStatusBar() const;

    /// 获取当前工作台实例
    UiWorkbench* currentWorkbench() const
    {
        return m_workbench;
    }

    /// 清理工作台切换期间的状态
    void resetWorkbenchTransientState();
    /// 同步当前工作台菜单状态
    void syncWorkbenchStateFromStateCenter();
    /// 只同步菜单选中态，不处理工作台生命周期
    /// 这里的菜单同步也只更新选中态，不负责工作台的实际切换
    /// 保存布局快照
    /// @param workbenchId 工作台 ID
    void saveLayoutSnapshot(const QString& workbenchId);
    /// 恢复布局快照
    /// @param workbenchId 工作台 ID
    void restoreLayoutSnapshot(const QString& workbenchId);
    /// 重新设置所有注册的 dock widget 的标题
    void restoreDockWidgetTitles();
    /// 设置骨架停靠面板的可见性（SceneDock / PropertiesDock）
    /// 3D 工作台不需要这些面板，需要隐藏以免挤压视口
    void setSkeletonDocksVisible(bool visible);
    /// 从状态中心刷新界面
    void refreshFromState();
    /// 触发工作台切换
    /// @param workbenchId 工作台 ID
    void triggerWorkbench(const QString& workbenchId);
    /// 触发主题切换
    /// @param themeId 主题 ID
    void triggerTheme(const QString& themeId);
    /// 记录当前主题并刷新窗口标题（菜单勾选态由配置驱动菜单自行同步）
    /// @param themeId 当前主题 ID
    void refreshThemeMenuChecks(const QString& themeId);

    /// 菜单管理器
    WorkbenchMenuManager* menuManager() const
    {
        return m_menuManager;
    }

private:
    /// 更新状态栏鼠标坐标显示
    void refreshPositionLabel();
    /// 创建窗口初始占位内容
    QWidget* createInitialCentralWidget();
    /// 创建工具栏基础骨架
    void initializeToolBarSkeleton();
    /// 创建停靠区域基础骨架
    void initializeDockAreaSkeleton();
    /// 创建状态栏骨架
    void initializeStatusBarSkeleton();
    /// 重新翻译所有 UI 文字
    void retranslateUi();
    /// 构建工具栏
    void buildToolBars();
    /// 构建停靠区域
    void buildDockAreas();
    /// 构建状态栏
    void buildStatusBar();
    /// 绑定状态中心信号
    void bindStateSignals();
    /// 解除状态中心信号绑定
    void unbindStateSignals();
    /// 同步窗口本地状态与状态中心
    void syncWindowStateFromStateCenter();
    /// 同步选择语义到窗口本地镜像
    void syncWorkbenchSelectionFromStateCenter();
    /// 刷新状态栏文本
    void refreshStatusText();
    /// 更新窗口标题
    void updateWindowTitle();
    /// 更新繁忙指示器
    void updateBusyIndicator(bool busy);
    /// 记录性能耗时
    void recordPerformance(const QString& scope, qint64 elapsedMs);
    /// 上报框架错误
    void reportFrameworkError(const QString& errorCode, const QString& message, const QString& context);
    /// 命令执行前的统一权限检查
    bool canExecuteCommand(const QString& commandId, const QString& context) const;
    /// 归零命令状态
    void resetCommandStateToIdle();
    /// 归零工作台相关的本地镜像状态
    void resetWorkbenchLocalMirror();
    /// 清空选择状态
    void clearSelectionState();
    /// 统一写入工作台切换上下文
    void setWorkbenchSwitchContext(const QString& workbenchId, const QString& switchContextText);

private:
    UiStateCenter* m_stateCenter{ nullptr };
    OperationBus* m_operationBus{ nullptr };
    UnitManager* m_unitManager{ nullptr };
    double m_lastMouseX{ 0.0 };
    double m_lastMouseY{ 0.0 };
    bool m_hasMousePosition{ false };
    UiWorkbench* m_workbench{ nullptr };
    /// 当前挂载的工作台状态栏 widget —— 不拥有；所有权在 QStatusBar（addWidget 后
    /// reparent），与 StateManager/工作台侧统一用 QPointer，避免三处裸指针悬空风险。
    QPointer<StatusBarBase> m_activeStatusBar;

    /// 工作台切换工厂
    WorkbenchFactory m_workbenchFactory;
    /// 菜单管理器
    WorkbenchMenuManager* m_menuManager{ nullptr };
    /// 文件拖放处理器（对接 ImportService，2D/3D 工作台共用）
    std::unique_ptr<FileDropHandler> m_fileDropHandler;
    /// 布局管理器：集中管理工具栏、停靠面板、状态栏骨架与布局快照
    std::unique_ptr<WorkbenchLayoutManager> m_layoutManager;
    /// 操作管理器：管理快捷键、命令权限检查、错误上报、性能记录
    std::unique_ptr<WorkbenchActionManager> m_actionManager;
    /// 状态管理器：统一管理状态中心同步、窗口状态镜像、状态栏刷新
    std::unique_ptr<WorkbenchStateManager> m_stateManager;
    /// 是否正在切换工作台（防止重复触发）
    bool m_switchingWorkbench{ false };

public:
    /// 注册全局快捷键（由工作台调用，切换时自动清理）
    /// @param shortcut 快捷键实例
    void registerShortcut(QShortcut* shortcut);
    /// 注销全局快捷键
    /// @param shortcut 快捷键实例
    void unregisterShortcut(QShortcut* shortcut);
    /// 清理所有注册的快捷键
    void clearAllShortcuts();

    /// 获取当前是否正在切换工作台
    bool isSwitchingWorkbench() const
    {
        return m_switchingWorkbench;
    }

    // 面板访问器（供工作台设置回调使用）
    SceneTreePanel* sceneTreeDock() const;
    PropertiesPanelWidget* propertiesDock() const;

    // 布局管理器访问器（供工作台设置场景面板可见性使用）
    WorkbenchLayoutManager* layoutManager() const
    {
        return m_layoutManager.get();
    }
};