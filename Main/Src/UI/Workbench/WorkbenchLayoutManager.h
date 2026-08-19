#pragma once

#include <vector>

#include <QMainWindow>
#include <QPointer>
#include <QProgressBar>

#ifdef SANYI_ENABLE_CONFIG_DRIVEN_UI
    #include <memory>
#endif

class QDockWidget;
class QLabel;
class QShortcut;
class QStatusBar;
class QToolBar;
class QWidget;
class WorkbenchMenuManager;
class PersistenceService;
class SceneTreePanel2D;
class PropertiesPanelWidget;
#ifdef SANYI_ENABLE_CONFIG_DRIVEN_UI
class UiConfigurationManager;
class UiPanelRegistry;
#endif

/// 面板状态：集中管理状态栏、工具栏与停靠面板指针
/// 从 WorkbenchWindow::PanelState 提升为独立类型，供 WorkbenchLayoutManager 使用
/// 注意：posLabel/selLabel/msgLabel 已移除 —— 这些由 StatusBarBase 子类管理
struct PanelState
{
    /// 状态栏（QMainWindow 内置）
    QStatusBar* statusBar{ nullptr };
    /// 左侧停靠面板
    QDockWidget* leftDock{ nullptr };
    /// 右侧停靠面板
    QDockWidget* rightDock{ nullptr };
    /// 场景树停靠面板
    SceneTreePanel2D* sceneTreeDock{ nullptr };
    /// 属性面板
    PropertiesPanelWidget* propertiesDock{ nullptr };
};

/// 工作台布局管理器：管理工具栏/Dock/状态栏的创建、注册、清理、布局快照
/// 从 WorkbenchWindow 中拆分，遵循单一职责原则
/// 使用 deleteLater() 清理控件，避免 QOpenGLWidget 析构时访问 INVALID_HANDLE_VALUE 崩溃
class WorkbenchLayoutManager
{
public:
    /// @param parent 主窗口指针（QMainWindow 用于 addDockWidget/addToolBar）
    /// @param menuManager 菜单管理器（用于 clearWorkbenchContent 时重建菜单）
    explicit WorkbenchLayoutManager(QMainWindow* parent, WorkbenchMenuManager* menuManager);

    ~WorkbenchLayoutManager();

    // ==================== 骨架初始化 ====================

    /// 初始化工具栏骨架
    void initializeToolBarSkeleton();
    /// 构建工具栏
    void buildToolBars();
    /// 初始化停靠区骨架
    void initializeDockAreaSkeleton();
    /// 构建停靠区域
    void buildDockAreas();
#ifdef SANYI_ENABLE_CONFIG_DRIVEN_UI
    /// 由 JSON 配置驱动构建停靠区域
    /// @return 是否成功应用配置（失败时调用方回退到硬编码骨架）
    bool buildDockAreasFromConfig();
    /// 载入客户配置并初始化配置相关缓存（工具栏/Dock 共用）
    bool ensureConfigLoaded();
#endif
    /// 初始化状态栏骨架
    void initializeStatusBarSkeleton();
    /// 构建状态栏
    void buildStatusBar();
    /// 创建初始占位中央控件
    QWidget* createInitialCentralWidget();

    // ==================== 注册与清理 ====================

    /// 注册停靠面板
    QDockWidget* registerDockWidget(const QString& title, QWidget* widget, Qt::DockWidgetArea area);
    /// 注册工具栏
    QToolBar* registerToolBar(const QString& title);
    /// 清空工作台内容（移除所有注册的面板和工具栏）
    /// 使用 deleteLater() 延迟删除，避免 QOpenGLWidget 析构时崩溃
    void clearLayoutContent();

    // ==================== 布局快照 ====================

    /// 设置持久化服务（用于数据库级布局快照存储）
    void setPersistenceService(PersistenceService* ps);

    /// 保存布局快照
    void saveLayoutSnapshot(const QString& workbenchId);
    /// 恢复布局快照
    void restoreLayoutSnapshot(const QString& workbenchId);
    /// 重新设置 dock 标题
    void restoreDockWidgetTitles();
    /// 设置骨架面板可见性
    void setSkeletonDocksVisible(bool visible);

    // ==================== 繁忙指示器 ====================

    /// 更新繁忙进度条
    void updateBusyIndicator(bool busy);

    // ==================== 数据访问 ====================

    PanelState& panelState()
    {
        return m_panelState;
    }

    const PanelState& panelState() const
    {
        return m_panelState;
    }

    const std::vector<QDockWidget*>& registeredDocks() const
    {
        return m_registeredDocks;
    }

    const std::vector<QToolBar*>& registeredToolBars() const
    {
        return m_registeredToolBars;
    }

    /// 获取繁忙进度条指针（供 WorkbenchWindow 清理时使用）
    QProgressBar* busyProgressBar() const
    {
        return m_busyProgressBar;
    }

private:
    QMainWindow* m_parent;
    WorkbenchMenuManager* m_menuManager;
    PersistenceService* m_persistenceService{ nullptr };

    PanelState m_panelState;
    std::vector<QDockWidget*> m_registeredDocks;
    std::vector<QToolBar*> m_registeredToolBars;
    QPointer<QProgressBar> m_busyProgressBar;
#ifdef SANYI_ENABLE_CONFIG_DRIVEN_UI
    /// 客户化 UI 配置管理器（Dock 骨架配置化）
    std::unique_ptr<UiConfigurationManager> m_configManager;
    /// 面板工厂注册表（Dock 配置化）
    std::unique_ptr<UiPanelRegistry> m_panelRegistry;
    /// 配置驱动布局是否已经构建，避免工具栏/Dock 重复加载
    bool m_configDrivenLayoutBuilt{ false };
#endif
};