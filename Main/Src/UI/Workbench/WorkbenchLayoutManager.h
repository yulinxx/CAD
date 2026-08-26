#pragma once

#include <memory>
#include <vector>

#include <QMainWindow>
#include <QPointer>
#include <QProgressBar>

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
class UiConfigurationManager;
class UiPanelRegistry;
class UiWorkbench;

/// 面板状态：集中管理状态栏、工具栏与停靠面板指针
/// 从 WorkbenchWindow::PanelState 提升为独立类型，供 WorkbenchLayoutManager 使用
/// 注意：posLabel/selLabel/msgLabel 已移除 —— 这些由 StatusBarBase 子类管理
///
/// 一律用 QPointer 而非裸指针：这些 widget 的所有权在 QMainWindow（父子关系），
/// 而 clearLayoutContent 在工作台切换时会**同步 delete** 全部 Dock。用裸指针时
/// leftDock/rightDock 会变成悬空指针，随后 setSkeletonDocksVisible 一调用就崩
/// （2D→3D 切换时 Workbench3D::build3DWorkbenchUi 的第一句就是它）。
struct PanelState
{
    /// 状态栏（QMainWindow 内置）
    QPointer<QStatusBar> statusBar;
    /// 左侧停靠面板
    QPointer<QDockWidget> leftDock;
    /// 右侧停靠面板
    QPointer<QDockWidget> rightDock;
    /// 场景树停靠面板
    QPointer<SceneTreePanel2D> sceneTreeDock;
    /// 属性面板
    QPointer<PropertiesPanelWidget> propertiesDock;
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
    /// 构建工具栏（配置驱动，唯一路径）
    void buildToolBars();
    /// 初始化停靠区骨架
    void initializeDockAreaSkeleton();
    /// 构建停靠区域（配置驱动，唯一路径）
    void buildDockAreas();
    /// 由 JSON 配置驱动构建停靠区域
    /// @return 是否成功应用配置
    bool buildDockAreasFromConfig();
    /// 载入客户配置并初始化配置相关缓存（菜单/工具栏/Dock/状态栏共用）
    bool ensureConfigLoaded();
    /// 初始化状态栏骨架
    void initializeStatusBarSkeleton();
    /// 构建状态栏（配置驱动：槽位由 JSON statusBar 节声明）
    void buildStatusBar();
    /// 设置当前工作台 ID（用于按 workbenches 字段过滤状态栏槽位）
    /// 需在 buildStatusBar 之前调用；为空时不做工作台过滤（全部槽位可见）
    void setActiveWorkbenchId(const QString& workbenchId);
    /// 回收由配置构建的状态栏槽位控件
    void clearStatusBarSlots();
    /// 创建初始占位中央控件
    QWidget* createInitialCentralWidget();

    // ==================== 注册与清理 ====================

    /// 注册停靠面板
    QDockWidget* registerDockWidget(const QString& title, QWidget* widget, Qt::DockWidgetArea area);
    /// 注册工具栏
    QToolBar* registerToolBar(const QString& title);
    /// 清空工作台内容（移除所有注册的面板和工具栏）
    /// 使用 deleteLater() 延迟删除，避免 QOpenGLWidget 析构时崩溃
    /// @param oldWorkbench 被替换掉的工作台。中央视口的 GL 资源释放委托给它的
    ///        releaseCentralWidgetGLResources —— 布局管理器不认识 2D/3D 视口的具体类型。
    ///        为空时跳过释放并告警（正常切换流程一定非空）。
    void clearLayoutContent(const UiWorkbench* oldWorkbench);

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

    /// 已加载的客户配置管理器（供右键菜单等按需读取同一份配置）
    UiConfigurationManager* configManager() const
    {
        return m_configManager;
    }

    /// 面板工厂注册表（Dock 与状态栏槽位共用）
    UiPanelRegistry* panelRegistry() const
    {
        return m_panelRegistry.get();
    }

private:
    QMainWindow* m_parent;
    WorkbenchMenuManager* m_menuManager;
    PersistenceService* m_persistenceService{ nullptr };

    PanelState m_panelState;
    std::vector<QDockWidget*> m_registeredDocks;
    std::vector<QToolBar*> m_registeredToolBars;
    /// 由配置构建并挂入状态栏的框架级槽位控件（工作台切换时统一回收）
    std::vector<QPointer<QWidget>> m_statusBarSlots;
    QPointer<QProgressBar> m_busyProgressBar;
    /// 客户化 UI 配置管理器：指向 UiConfigurationManager::shared()，本类不拥有其生命周期
    UiConfigurationManager* m_configManager{ nullptr };
    /// 面板工厂注册表（Dock 与状态栏槽位共用）
    std::unique_ptr<UiPanelRegistry> m_panelRegistry;
    /// 配置驱动布局是否已经构建，避免工具栏/Dock 重复加载
    bool m_configDrivenLayoutBuilt{ false };
    /// 当前工作台 ID，用于状态栏槽位的工作台过滤；空表示不过滤
    QString m_activeWorkbenchId;
};