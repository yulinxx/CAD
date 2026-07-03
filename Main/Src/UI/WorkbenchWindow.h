/**
 * @file Main/Src/UI/WorkbenchWindow.h
 */
#pragma once

#include <functional>
#include <vector>

#include <QMainWindow>

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

/**
 * @file WorkbenchWindow.h
 * @brief 工作台主窗口类定义
 *
 * 定义了 CAD 应用程序的主窗口类，继承自 QMainWindow，负责：
 * - 菜单系统管理（文件、视图、工具）
 * - 工具栏管理
 * - 停靠面板管理
 * - 状态栏管理
 * - 主题切换
 * - 工作台切换
 * - UI 状态同步
 */

 /**
  * @class WorkbenchWindow
  * @brief 工作台主窗口类
  *
  * 应用程序的主窗口，管理所有 UI 组件的布局和交互。
  * 通过状态中心（UiStateCenter）监听状态变化并更新界面。
  * 支持主题切换和工作台切换功能。
  */
class WorkbenchWindow : public QMainWindow
{
    Q_OBJECT

public:
    /// 构造函数
    /// @param parent 父部件
    explicit WorkbenchWindow(QWidget* parent = nullptr);
    ~WorkbenchWindow() override;

    /// 设置状态中心
    /// @param stateCenter UI 状态中心
    void setUiStateCenter(UiStateCenter* stateCenter);
    /// 设置主题服务
    /// @param themeService 主题服务
    void setThemeService(UiThemeService* themeService);
    /// 设置当前工作台
    /// @param workbench 工作台实例
    void setWorkbench(UiWorkbench* workbench);
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
    /// 保存布局快照
    /// @param workbenchId 工作台 ID
    void saveLayoutSnapshot(const QString& workbenchId);
    /// 恢复布局快照
    /// @param workbenchId 工作台 ID
    void restoreLayoutSnapshot(const QString& workbenchId);
    /// 从状态中心刷新界面
    void refreshFromState();
    /// 触发工作台切换
    /// @param workbenchId 工作台 ID
    void triggerWorkbench(const QString& workbenchId);

private:
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
    /// 刷新状态栏文本
    void refreshStatusText();
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

private:
    /// UI 状态中心
    UiStateCenter* m_stateCenter{ nullptr };
    /// 主题服务
    UiThemeService* m_themeService{ nullptr };
    /// 当前工作台
    UiWorkbench* m_workbench{ nullptr };
    /// 文件菜单
    QMenu* m_fileMenu{ nullptr };
    /// 视图菜单
    QMenu* m_viewMenu{ nullptr };
    /// 工具菜单
    QMenu* m_toolsMenu{ nullptr };
    /// 主题子菜单
    QMenu* m_themeMenu{ nullptr };
    /// 工作台子菜单
    QMenu* m_workbenchMenu{ nullptr };
    /// 状态栏
    QStatusBar* m_statusBar{ nullptr };
    /// 主工具栏
    QToolBar* m_mainToolBar{ nullptr };
    /// 左侧停靠面板
    QDockWidget* m_leftDock{ nullptr };
    /// 右侧停靠面板
    QDockWidget* m_rightDock{ nullptr };
    /// 工作台标签（状态栏显示）
    QLabel* m_workbenchLabel{ nullptr };
    /// 繁忙状态标签（状态栏显示）
    QLabel* m_busyLabel{ nullptr };
    /// 场景树停靠面板
    SceneTreeDockWidget* m_sceneTreeDock{ nullptr };
    /// 属性面板
    PropertiesPanelWidget* m_propertiesDock{ nullptr };
    /// 主题切换回调
    std::function<void(const QString&)> m_themeChangeCallback;
    /// 当前工作台 ID
    QString m_workbenchId{ QStringLiteral("default") };
    /// 当前主题 ID
    QString m_themeId{ QStringLiteral("system") };
    /// 是否繁忙
    bool m_busy{ false };
    /// 注册的停靠面板列表
    std::vector<QDockWidget*> m_registeredDocks;
    /// 注册的工具栏列表
    std::vector<QToolBar*> m_registeredToolBars;
};
