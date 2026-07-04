#pragma once

#include <functional>
#include <vector>

#include <QMainWindow>
#include <QPointer>

#include "UiFrameworkServices.h"
#include "UiServices.h"

class QAction;
class QDockWidget;
class QLabel;
class QMenu;
class QProgressBar;
class QStatusBar;
class QToolBar;
class UiStateCenter;
class UiThemeService;
class UiWorkbench;
class SceneTreeDockWidget;
class PropertiesPanelWidget;

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

public:
    /// 设置状态中心
    /// @param stateCenter UI 状态中心
    void setUiStateCenter(UiStateCenter* stateCenter);
    /// 设置主题服务
    /// @param themeService 主题服务
    void setThemeService(UiThemeService* themeService);
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
    /// @param factory 工作台工厂回调
    void setWorkbenchFactory(WorkbenchFactory factory);
    /// 应用样式表
    /// @param styleSheet 样式表内容
    void applyTheme(const QString& styleSheet);
    /// 设置主题切换回调
    /// @param callback 主题切换回调函数
    void setThemeChangeCallback(std::function<void(const QString&)> callback);

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
    /// 从状态中心刷新界面
    void refreshFromState();
    /// 触发工作台切换
    /// @param workbenchId 工作台 ID
    void triggerWorkbench(const QString& workbenchId);

private:
    /// 创建基础菜单项，降低 buildMenus 的耦合度
    void createBaseMenus();
    /// 创建文件、视图、工具的基础菜单骨架
    void initializeMenuSkeleton();
    /// 创建窗口初始占位内容，作为工作台首次挂接前的安全兜底
    QWidget* createInitialCentralWidget();
    /// 创建工具栏基础骨架，便于后续拆出更多工具栏分组
    void initializeToolBarSkeleton();
    /// 创建停靠区域基础骨架，便于后续拆出更多 dock 分组
    void initializeDockAreaSkeleton();
    /// 创建状态栏骨架，统一承接状态文本与繁忙指示
    void initializeStatusBarSkeleton();
    /// 创建主题菜单骨架，便于后续把主题项与主题服务分离
    void initializeThemeMenuSkeleton();
    /// 创建工作台切换菜单骨架，便于后续收口工作台入口
    void initializeWorkbenchMenuSkeleton();
    /// 构建菜单系统（文件、视图、工具）
    void buildMenus();
    /// 构建工具栏
    void buildToolBars();
    /// 构建停靠区域（左侧项目面板、右侧属性面板）
    void buildDockAreas();
    /// 构建状态栏
    void buildStatusBar();
    /// 构建主题菜单
    void buildThemeMenu();
    /// 构建工作台切换菜单
    void buildWorkbenchMenu();
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
    /// 触发主题切换
    /// @param themeId 主题 ID
    void triggerTheme(const QString& themeId);
    /// 刷新主题菜单选中状态
    /// @param themeId 当前主题 ID
    void refreshThemeMenuChecks(const QString& themeId);
    /// 刷新工作台菜单选中状态
    /// @param workbenchId 当前工作台 ID
    void refreshWorkbenchMenuChecks(const QString& workbenchId);
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
    /// UI 服务集合
    UiServices m_uiServices;
    /// 框架级服务桥接
    UiFrameworkServices m_frameworkServices;
    /// 当前工作台
    UiWorkbench* m_workbench{ nullptr };
    /// 主题切换回调
    std::function<void(const QString&)> m_themeChangeCallback;
    /// 窗口状态：只保存与窗口语义直接相关的高层状态
    struct WindowState
    {
        /// 当前工作台 ID
        QString workbenchId{ QStringLiteral("default") };
        /// 当前主题 ID
        QString themeId{ QStringLiteral("system") };
        /// 当前是否处于繁忙状态
        bool busy{ false };
        /// 当前选择文本
        QString selectionText;
        /// 当前选择来源
        QString selectionSource;
        /// 当前选择类型
        QString selectionType;
    } m_windowState;
    /// 菜单状态：集中管理菜单对象指针，避免散落在类成员中
    struct MenuState
    {
        /// 文件菜单
        QMenu* fileMenu{ nullptr };
        /// 视图菜单
        QMenu* viewMenu{ nullptr };
        /// 工具菜单
        QMenu* toolsMenu{ nullptr };
        /// 主题子菜单
        QMenu* themeMenu{ nullptr };
        /// 工作台切换子菜单
        QMenu* workbenchMenu{ nullptr };
    } m_menuState;
    /// 面板状态：集中管理状态栏、工具栏与停靠面板指针
    struct PanelState
    {
        /// 状态栏
        QStatusBar* statusBar{ nullptr };
        /// 主工具栏
        QToolBar* mainToolBar{ nullptr };
        /// 左侧停靠面板
        QDockWidget* leftDock{ nullptr };
        /// 右侧停靠面板
        QDockWidget* rightDock{ nullptr };
        /// 状态栏中的工作台标签
        QLabel* workbenchLabel{ nullptr };
        /// 状态栏中的繁忙标签
        QLabel* busyLabel{ nullptr };
        /// 场景树停靠面板
        SceneTreeDockWidget* sceneTreeDock{ nullptr };
        /// 属性面板
        PropertiesPanelWidget* propertiesDock{ nullptr };
    } m_panelState;
    /// 注册的停靠面板列表
    std::vector<QDockWidget*> m_registeredDocks;
    /// 注册的工具栏列表
    std::vector<QToolBar*> m_registeredToolBars;
    /// 工作台切换工厂
    WorkbenchFactory m_workbenchFactory;
    /// 繁忙进度条（避免 findChild 级联查找）
    QPointer<QProgressBar> m_busyProgressBar;
};