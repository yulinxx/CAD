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
class UiThemeService;
class UiWorkbench;
class WorkbenchMenuManager;
class WorkbenchLayoutManager;
class WorkbenchActionManager;
class WorkbenchStateManager;
class SceneTreePanel2D;
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
 * 通过状态中心（UiStateCenter）监听状态变化并更新界面。
 * - 菜单系统管理（文件、视图、工具）
 * - 工具栏管理
 * - 停靠面板管理
 * - 状态栏管理
 * - 主题切换
 * - 工作台切换
 * - UI 状态同步
 */
class WorkbenchWindow : public QMainWindow
{
    Q_OBJECT

public:
    /// @param parent 父部件
    explicit WorkbenchWindow(QWidget* parent = nullptr);
    ~WorkbenchWindow() override;

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
    /// 设置主题服务
    /// @param themeService 主题服务
    void setThemeService(UiThemeService* themeService);
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
    /// 获取当前 UI 服务集合
    /// @return UI 服务集合引用
    const UiServices& uiServices() const;
    /// 设置当前工作台
    /// @param workbench 工作台实例
    void setWorkbench(UiWorkbench* workbench);
    /// 设置工作台切换工厂
    /// @param factory 工作台工厂回调
    void setWorkbenchFactory(WorkbenchFactory factory);
    /// 应用样式表
    /// @param styleSheet 样式表内容
    void applyTheme(const QString& styleSheet);
    /// 设置主题切换回调
    /// @param callback 主题切换回调函数
    void setThemeChangeCallback(std::function<void(const QString&)> callback);
    /// 设置视口缩放操作回调（Zoom In/Out/Fit/Selection/Reset）
    /// 由工作台在创建视口后注入，将菜单缩放操作转发到视口
    /// @param handler 缩放操作处理函数，参数为 "zoom_in"/"zoom_out"/"zoom_fit"/"zoom_selection"/"reset"
    void setViewportZoomHandler(std::function<void(const QString&)> handler);
    /// 更新状态栏鼠标坐标显示
    /// @param x 世界坐标 X（毫米）
    /// @param y 世界坐标 Y（毫米）
    void updatePositionLabel(double x, double y);

    // ==================== 最近文件菜单 ====================

    /// 将文件路径添加到最近文件列表
    /// @param filePath 文件完整路径
    void addRecentFile(const QString& filePath);
    /// 从设置中加载最近文件列表
    QStringList loadRecentFiles() const;
    /// 将最近文件列表保存到设置
    void saveRecentFiles(const QStringList& files) const;
    /// 填充最近文件子菜单
    void populateRecentFilesMenu();

    /// 注册停靠面板
    /// @param title 面板标题
    /// @param widget 面板内容部件
    /// @param area 停靠区域
    QDockWidget* registerDockWidget(const QString& title, QWidget* widget, Qt::DockWidgetArea area);
    /// 注册工具栏
    /// @param title 工具栏标题
    QToolBar* registerToolBar(const QString& title);
    /// 清空工作台内容（移除所有注册的面板和工具栏）
    void clearWorkbenchContent();

    // ==================== 状态栏挂载/卸载 ====================

    /// 挂载工作台状态栏 widget 到 QStatusBar
    /// 由 Workbench2D / Workbench3D 在 attachToWindow 时调用，
    /// 将各自独立的 StatusBarBase 子类实例挂载到窗口状态栏。
    /// @param statusBarWidget 工作台状态栏 widget（生命周期由调用方管理）
    void mountStatusBar(StatusBarBase* statusBarWidget);
    /// 卸载当前工作台状态栏 widget，从 QStatusBar 移除
    /// 由 clearWorkbenchContent 在工作台切换时调用
    void unmountStatusBar();

    /// 获取当前挂载的工作台状态栏 widget
    StatusBarBase* activeStatusBar() const
    {
        return m_activeStatusBar;
    }

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
    /// 刷新主题菜单选中状态
    /// @param themeId 当前主题 ID
    void refreshThemeMenuChecks(const QString& themeId);

    /// 菜单管理器
    WorkbenchMenuManager* menuManager() const
    {
        return m_menuManager;
    }

private:
    /// 更新状态栏鼠标坐标显示（按当前显示单位换算）
    void refreshPositionLabel();
    /// 创建窗口初始占位内容，作为工作台首次挂接前的安全兜底
    QWidget* createInitialCentralWidget();
    /// 创建工具栏基础骨架，便于后续拆出更多工具栏分组
    void initializeToolBarSkeleton();
    /// 创建停靠区域基础骨架，便于后续拆出更多 dock 分组
    void initializeDockAreaSkeleton();
    /// 创建状态栏骨架，统一承接状态文本与繁忙指示
    void initializeStatusBarSkeleton();
    /// 重新翻译所有 UI 文字（语言切换时调用）
    void retranslateUi();
    /// 构建工具栏
    void buildToolBars();
    /// 构建停靠区域（左侧项目面板、右侧属性面板）
    void buildDockAreas();
    /// 构建状态栏
    void buildStatusBar();
    /// 绑定状态中心信号
    void bindStateSignals();
    /// 解除状态中心信号绑定，避免重复连接
    void unbindStateSignals();
    /// 同步窗口本地状态与状态中心，避免出现两套状态来源
    void syncWindowStateFromStateCenter();
    /// 窗口镜像只在这里同步，不要在别处直接改动本地状态
    /// 同步选择语义到窗口本地镜像，避免展示层直接依赖状态中心快照
    void syncWorkbenchSelectionFromStateCenter();
    /// 刷新状态栏文本
    void refreshStatusText();
    /// 更新窗口标题，避免状态栏刷新时分散拼接标题逻辑
    void updateWindowTitle();
    /// 更新繁忙指示器
    /// @param busy 是否繁忙
    void updateBusyIndicator(bool busy);
    /// 记录性能耗时并统一走框架级入口
    /// @param scope 作用域名称
    /// @param elapsedMs 耗时毫秒
    void recordPerformance(const QString& scope, qint64 elapsedMs);
    /// 上报框架错误
    /// @param errorCode 错误码
    /// @param message 错误信息
    /// @param context 上下文
    void reportFrameworkError(const QString& errorCode, const QString& message, const QString& context);
    /// 命令执行前的统一权限检查
    /// @param commandId 命令 ID
    /// @param context 调用上下文
    /// @return 是否允许执行
    bool canExecuteCommand(const QString& commandId, const QString& context) const;
    /// 归零命令状态，避免各处重复写 idle
    void resetCommandStateToIdle();
    /// 归零工作台相关的本地镜像状态
    void resetWorkbenchLocalMirror();
    /// 清空选择状态，避免工作台切换后遗留旧选择文本
    void clearSelectionState();
    /// 统一写入工作台切换上下文
    void setWorkbenchSwitchContext(const QString& workbenchId, const QString& switchContextText);

private:
    /// UI 状态中心
    UiStateCenter* m_stateCenter{ nullptr };
    /// 主题服务
    UiThemeService* m_themeService{ nullptr };
    /// 操作总线
    OperationBus* m_operationBus{ nullptr };
    /// UI 服务集合
    UiServices m_uiServices;
    /// 单位管理器（非拥有指针，来自 UiServices）
    UnitManager* m_unitManager{ nullptr };
    /// 最近一次鼠标世界坐标（毫米，基单位）
    double m_lastMouseX{ 0.0 };
    double m_lastMouseY{ 0.0 };
    /// 是否已有有效的鼠标坐标
    bool m_hasMousePosition{ false };
    /// 当前工作台
    UiWorkbench* m_workbench{ nullptr };
    /// 主题切换回调
    std::function<void(const QString&)> m_themeChangeCallback;
    /// 当前挂载的工作台状态栏 widget（由 StatusBarBase 子类管理位置/选择/消息显示）
    /// 工作台切换时通过 mountStatusBar/unmountStatusBar 替换
    StatusBarBase* m_activeStatusBar{ nullptr };

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
    SceneTreePanel2D* sceneTreeDock() const;
    PropertiesPanelWidget* propertiesDock() const;
};