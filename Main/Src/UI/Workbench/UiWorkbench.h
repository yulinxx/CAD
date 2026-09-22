#pragma once

#include <QString>
#include <QObject>
#include <QPointer>
#include <QVector>

#include <cstdint>
#include <memory>

#include "UiServiceGroups.h"
#include "UI/Service/ToolBarContextManager.h"
#include "Services/UiStateCenter.h"
#include "ClientConfig/UiLayoutBuilder.h"  // IUiCommandDispatcher：工作台直接实现该接口

class QAction;
class QWidget;
class QToolBar;
class WorkbenchWindow;
class PropertiesPanelWidget;
class RenderViewport2D;
class StatusBar;
class SettingsService;
class SettingsUiCoordinator2D;
class SceneTreePanel;
class SceneTreeSceneObserver2D;
class SceneMonitor;
struct UiStateSnapshot;
struct CommandUiSnapshot;

// 3D 类型前向声明（避免头文件膨胀，实际 include 下沉到 .cpp）
#if BUILD_UI3D
    #include <QShortcut>
    #include "UI3D/Service/ServicePack3D.h"  // 值成员需要完整定义
    #include "UI/MainWindow/MainWindow3D.h"  // unique_ptr 成员，MOC 需要完整类型

namespace Eg
{
    class SceneManager3D;
}
class OperationBus3D;
class DocumentManager3D;
class UndoRedoManager3D;
class SceneEditService3D;
class SceneMonitor3D;
class SceneDocument3D;
class CameraController3D;
class SettingsUiCoordinator3D;
class CommandActionHub3D;
class AlgorithmRunner3D;
class AlgorithmApplicationService;
class StatusBar3D;
    #ifdef ENABLE_GEOMODELCORE
class BRepModelService3D;
    #endif
#endif

/**
 * @file UiWorkbench.h
 * @brief 工作台接口定义
 *
 * 定义了 UI 工作台接口及其实现类，包括 2D 和 3D 工作台。
 */

// ============================================================
/**
 * @class UiWorkbench
 * @brief 工作台抽象接口
 *
 * 定义工作台的生命周期管理：初始化、附加到窗口、激活、停用、关闭。
 * 工作台切换时通过状态快照机制保存和恢复状态。
 *
 * 基类提供状态快照的通用实现，子类只需在 attachToWindow 中填充 m_initialState。
 */
class UiWorkbench : public QObject, public IUiCommandDispatcher
{
    Q_OBJECT

public:
    explicit UiWorkbench(QObject* parent = nullptr)
        : QObject(parent)
    {
    }

    ~UiWorkbench() override = default;

public:
    /// 获取工作台 ID
    virtual QString id() const = 0;

    /// 判断当前工作台是否提供命令
    /// 菜单构建器通过该接口复用 2D / 3D 各自的命令目录。
    /// 同时实现 IUiCommandDispatcher::isCommandRegistered。
    bool isCommandRegistered(const QString& commandId) const override;

    /// 从当前工作台命令目录分发命令
    /// 菜单层只传递 commandId 与参数，不直接依赖具体 OperationBus 类型。
    /// @param params 调用方参数（如动态最近文件项的 path），透传到操作总线
    virtual void dispatchCommand(const QString& commandId, const QVariantMap& params);

    /// IUiCommandDispatcher 入口。
    /// 工作台自身就是分发器，避免为右键菜单/布局构建器另建适配器对象导致悬空指针。
    void dispatch(const QString& commandId, const QVariantMap& params) override
    {
        dispatchCommand(commandId, params);
    }

    /// 派生类里声明了两参数 dispatch 会隐藏基类的无参便捷重载，显式引入
    using IUiCommandDispatcher::dispatch;

    /// 获取工作台的命令显示名和图标等元数据
    virtual QString commandText(const QString& commandId) const;

    /// 获取工作台显示名称
    virtual QString displayName() const = 0;

    /// 初始化工作台
    /// @param services UI 服务集合
    /// @return 是否初始化成功
    virtual bool initialize(const WorkbenchServices& services) = 0;

    /// 附加到主窗口
    /// @param window 工作台窗口
    virtual void attachToWindow(WorkbenchWindow& window) = 0;

    /// 激活工作台
    /// 从状态快照恢复之前保存的状态，或使用初始化时的缓存状态
    virtual void activate() = 0;

    /// 停用工作台
    /// 将当前状态保存到状态快照，供下次激活时恢复
    virtual void deactivate() = 0;

    /// 关闭工作台
    virtual void shutdown() = 0;

    // ==================== 框架层委托接口 ====================
    // 以下虚函数提供默认实现，子类按需重写。
    // 框架层（WorkbenchWindow / WorkbenchStateManager）通过这些接口
    // 将 2D/3D 差异化逻辑委托给各工作台，避免框架层直接依赖具体类型。

    /// 释放中央视口的 OpenGL 资源（工作台切换时调用）
    /// @param centralWidget 当前中央视口 widget
    virtual void releaseCentralWidgetGLResources(QWidget* centralWidget) const;

    /// 重新抓取命令 UI 快照并刷新所有命令面（工具栏 / 菜单栏 / 右键菜单 / 场景树）。
    /// 框架层在重建菜单后调用，使新建的 QAction 立即得到正确启用态；
    /// 未接入命令中枢的工作台保持空实现。
    virtual void refreshCommandUiState() {}

    /// 是否需要显示骨架停靠面板（SceneDock / PropertiesDock）
    /// 2D 工作台返回 true（默认），3D 工作台返回 false
    virtual bool requiresSkeletonDocks() const;

    /// 是否自行管理菜单（跳过 WorkbenchMenuManager 的菜单重建）
    /// 2D 工作台返回 false（默认），3D 工作台返回 true
    virtual bool managesOwnMenus() const;

    /// 显示设置对话框（Help → Settings），由活动工作台接管
    /// @param parent 父窗口
    /// @return true=工作台已处理，false=未处理（调用方退化为兜底提示）
    virtual bool showSettingsDialog(QWidget* parent);

    /// 保存当前运行时设置到数据库（退出时调用，兜底防崩溃丢失）
    /// 默认空实现，子类重写调用各自的 SettingsUiCoordinator::saveCurrentSettings()
    virtual void saveCurrentSettings() {}

    /// 获取工作台的共享 SettingsService singleton（app-level）
    SettingsService* settingsService() const
    {
        return m_settingsService;
    }

protected:
    /// 获取当前状态快照
    /// 从状态中心读取当前状态，若无状态中心则使用初始化时的缓存状态
    /// @return 当前状态快照
    virtual UiStateSnapshot currentSnapshot() const;

    /// 恢复状态快照
    /// @param snapshot 要恢复的状态快照
    virtual void restoreFromSnapshot(const UiStateSnapshot& snapshot);

protected:
    /// UI 服务分组（由 initialize 从 UiServices 拆出后缓存）
    UiStateServices m_uiState;
    CommandServices m_commands;
    SceneServices m_scene;
    PersistenceServices m_persistence;
    ViewServices m_view;
    UiStateSnapshot m_initialState;  ///< 初始化时缓存的状态
    UiStateSnapshot m_savedState;    ///< 上次停用前保存的状态快照
    SettingsService* m_settingsService{ nullptr };  ///< app-level singleton
    WorkbenchWindow* m_workbenchWindow{ nullptr };  ///< 当前挂载的工作台窗口
};

// ============================================================
/**
 * @class Workbench2D
 * @brief 2D 工作台实现
 *
 * 提供 2D 绘图功能，包括线条绘制、测量、选择等操作。
 */

/// 2D 左右面板（Draw Tools / Layers）的承载样式
enum class PanelHostStyle
{
    Toolbar,  ///< 固定工具栏（QToolBar 停靠在左右两侧）
    Dock      ///< Dock 停靠面板（QDockWidget，默认）
};

class Workbench2D final : public UiWorkbench
{
public:
    Workbench2D();
    ~Workbench2D() override;

public:
    QString id() const override;
    QString displayName() const override;
    bool isCommandRegistered(const QString& commandId) const override;
    void dispatchCommand(const QString& commandId, const QVariantMap& params) override;
    QString commandText(const QString& commandId) const override;
    bool initialize(const WorkbenchServices& services) override;
    void attachToWindow(WorkbenchWindow& window) override;
    void activate() override;
    void deactivate() override;
    void shutdown() override;

    // 框架层委托接口
    void releaseCentralWidgetGLResources(QWidget* centralWidget) const override;

    bool showSettingsDialog(QWidget* parent) override;

    void saveCurrentSettings() override;

public:
    /// 设置左右面板（Draw Tools / Layers）的承载样式（默认 Dock）
    void setPanelHostStyle(PanelHostStyle style);

    /// 当前左右面板承载样式
    PanelHostStyle panelHostStyle() const
    {
        return m_panelHostStyle;
    }

    /// 重新抓取选择上下文快照并驱动全部命令 UI
    void refreshCommandUiState() override;

    /// 重建场景树模型并推送到面板
    void refreshSceneTree();

    /// 只在结构签名变化时重建场景树
    void refreshSceneTreeIfNeeded(const char* src = nullptr);

    /// 增量刷新场景树
    void applySceneTreeIncremental(const char* src = nullptr);

private:
    /// 创建中央视口
    QWidget* createCentralViewport(WorkbenchWindow& window, PropertiesPanelWidget* properties);
    /// 注入服务到视口
    void setupViewportServices(RenderViewport2D* vp, WorkbenchWindow& window);
    /// 设置导入服务回调
    void setupImportCallbacks(RenderViewport2D* vp, WorkbenchWindow& window);
    /// 创建工具栏
    void createToolbars(WorkbenchWindow& window);
    /// 构建绘图工具栏动作
    QVector<QAction*> buildDrawToolActions();
    /// 构建支持的导入格式列表
    static QStringList buildSupportedImportFormats(const QString& workbenchId);
    /// 绑定并填充 2D 场景树面板
    void setupSceneTree(WorkbenchWindow& window);
    /// 引擎场景变更兜底
    void onSceneTreeSceneChanged();
    /// 同步面板选中高亮
    void syncSceneTreeSelection();
    /// 将面板选择同步到引擎选择
    void applySceneTreeSelection(const QStringList& ids);
    /// 切换图元可见性
    void toggleEntityVisibility(const QString& id, bool visible);
    /// 重命名图元
    void renameEntity(const QString& id, const QString& newName);
    /// 延迟重建场景树
    void scheduleTreeRefresh();
    /// 从场景树批量删除图元
    void deleteSceneTreeSelection(const QStringList& ids);
    /// 从场景树批量设置可见性（整数 id，避免字符串转换开销）
    void setSceneTreeVisibility(const QVector<qint64>& ids, bool visible);
    /// 从场景树批量设置锁定（整数 id）
    void setSceneTreeLock(const QVector<qint64>& ids, bool locked);
    /// 刷新属性面板
    void refreshPropertiesPanel();
    /// 应用选择上下文到各 UI 组件
    void applySelectionContext(const CommandUiSnapshot& snapshot);

private:
    /// 命令动作中枢
    std::unique_ptr<class CommandActionHub> m_commandHub;
    /// 视口右键菜单请求
    void onViewportContextMenu(QContextMenuEvent* event);
    /// 按客户配置构建 2D 右键菜单
    QMenu* buildConfiguredContextMenu(const QString& contextMenuId, bool hasSelection);
    class TopToolBar* m_topToolBar{ nullptr };              ///< 顶部编辑工具栏
    class QToolBar* m_textFontToolBar{ nullptr };           ///< 文字编辑字体工具栏
    class TextFontToolBar* m_textFontToolBarWidget{ nullptr };
    class RightToolBar* m_rightToolBar{ nullptr };          ///< 右侧工具栏
    PanelHostStyle m_panelHostStyle{ PanelHostStyle::Toolbar };  ///< 左右面板承载样式
    class RenderViewport2D* m_viewport{ nullptr };         ///< 2D 渲染视口
    class SceneTreePanel* m_scenePanel2D{ nullptr };        ///< 2D 场景树面板
    std::unique_ptr<SceneTreeSceneObserver2D> m_sceneTreeObserver;
    SceneMonitor* m_sceneMonitor{ nullptr };
    QPointer<QObject> m_uiStateConnections;                ///< 命令 UI 刷新连线句柄
    class QTimer* m_sceneTreeRefreshTimer{ nullptr };      ///< 场景树重建防抖定时器
    const char* m_sceneTreeRefreshSource{ nullptr };        ///< 场景树重建触发来源
    uint64_t m_sceneTreeCursor{ 0 };                       ///< 场景树增量游标
    bool m_sceneTreeForceRefresh{ false };                  ///< 强制全量刷新标志
    /// applySceneTreeIncremental 重入标志：分类/追加期间会触发嵌套的场景通知，
    /// 嵌套调用读到的游标还是外层尚未推进的旧值，会把同一批变更整表再扫一遍。
    bool m_sceneTreeIncrementalBusy{ false };
    /// 场景树延迟重建标记（setData 回调链中 scheduleTreeRefresh 合并，避免 delete this）
    bool m_treeRefreshPending{ false };
    /// 命令 UI 状态刷新的节流冷却定时器 + 尾包标记。
    /// 拖动这类「每个鼠标移动都改一次场景」的路径会高频打 refreshCommandUiState（唯一入口），
    /// 每次都要重算全部命令/菜单的启用态并重建属性面板；这里用「首次立即 + 冷却窗口内合并 +
    /// 窗口末尾补一次」把频率压到约 10Hz，最终态一定是最后一次变化的结果。
    class QTimer* m_commandUiRefreshTimer{ nullptr };
    bool m_commandUiRefreshPending{ false };
    /// 真正执行一次命令 UI 状态刷新
    void applyCommandUiState();
    /// 场景树结构签名
    std::size_t m_lastSceneTreeEntityCount{ 0 };
    uint64_t m_lastSceneTreeStructureRevision{ 0 };
    uint64_t m_lastSceneTreeTopologyRevision{ 0 };
    QPointer<StatusBar> m_statusBar2D;  ///< 2D 状态栏 widget

    /// 2D 设置协调器
    std::unique_ptr<SettingsUiCoordinator2D> m_settingsCoordinator;

    /// 工具栏上下文管理器
    std::unique_ptr<ToolBarContextManager> m_contextManager;

    /// 网格显隐 metadata 连接
    QMetaObject::Connection m_gridVisibilityMetadataConn;

    /// 由选择上下文快照推导应切换到的工具栏上下文
    ToolBarContext determineContextFromSelection(const CommandUiSnapshot& snapshot) const;
};

#if BUILD_UI3D
// ============================================================
/**
 * @class Workbench3D
 * @brief 3D 工作台实现
 * 使用 MainWindow3D + ServiceLocator3D 架构
 */
class Workbench3D final : public UiWorkbench
{
    Q_OBJECT

public:
    ~Workbench3D() override;

public:
    QString id() const override;
    QString displayName() const override;
    bool isCommandRegistered(const QString& commandId) const override;
    void dispatchCommand(const QString& commandId, const QVariantMap& params) override;
    QString commandText(const QString& commandId) const override;
    bool initialize(const WorkbenchServices& services) override;
    void attachToWindow(WorkbenchWindow& window) override;
    void activate() override;
    void deactivate() override;
    void shutdown() override;

    // 框架层委托接口
    void releaseCentralWidgetGLResources(QWidget* centralWidget) const override;
    bool requiresSkeletonDocks() const override;
    bool managesOwnMenus() const override;
    /// 重新抓取 3D 快照并驱动全部命令 UI（中枢托管动作 + 配置化菜单栏）
    void refreshCommandUiState() override;

    // 3D 工作台接管设置对话框，避免 CoreOperationRegistry 兜底弹出冗余提示
    bool showSettingsDialog(QWidget* parent) override;

    void saveCurrentSettings() override;

private:
    // ServiceOwner 定义在 .cpp 中（PIMPL 模式，避免头文件引入 20+ 3D 依赖）
    struct ServiceOwner;

    /// 自定义删除器：声明在此，定义在 .cpp（ServiceOwner 完整定义处）
    struct ServiceOwnerDeleter
    {
        void operator()(ServiceOwner*) const;
    };

    void build3DWorkbenchUi(WorkbenchWindow& window);
    void create3DServices();
    void setup3DViewportAndSignals(WorkbenchWindow& window);
    void setup3DMenuAndShortcuts(WorkbenchWindow& window);
    void create3DViewport(WorkbenchWindow& window);
    void bind3DRenderSignals(ServiceOwner& own);
    void bind3DCursorSignal();
    void bind3DSelectionSignal();
    void setup3DDeleteShortcuts(WorkbenchWindow& window);
    /// 3D 视口右键菜单请求：基于命令中枢快照构建并弹出（与 2D 统一的单一事实来源）
    void on3DContextMenuRequested(const QPoint& globalPos);
    /// 按客户配置构建 3D 右键菜单
    /// @param contextMenuId JSON contextMenus 节中的菜单 ID，例如 "canvas.3d"
    /// @return 配置菜单；未配置时返回 nullptr，调用方回退到内建路径。
    ///         返回的菜单归调用方所有，且必须在同一作用域内 delete（命令分发器是栈对象）。
    QMenu* buildConfiguredContextMenu(const QString& contextMenuId);

    // ---- 3D 场景树（数据/算法/UI 分离，UI 可定制/可缺失） ----
    /// 绑定并填充 3D 场景树面板（数据经算法层由引擎场景生成）
    void setupSceneTree3D(WorkbenchWindow& window);
    /// 重建场景树模型并推送到面板（结构性变化：导入/撤销/增删）
    void refreshSceneTree3D();

    /// 只在结构签名变化时重建场景树（批量操作防抖）
    void refreshSceneTree3DIfNeeded();

    /// 仅同步面板选中高亮（选择变化，避免重建树导致折叠丢失）
    void syncSceneTreeSelection3D();
    /// 将面板选择同步到引擎选择
    void applySceneTreeSelection3D(const QStringList& ids);
    /// 切换图元可见性（直接写引擎并刷新）
    void toggleEntityVisibility3D(const QString& id, bool visible);
    /// 重命名图元（直接写引擎并刷新）
    void renameEntity3D(const QString& id, const QString& newName);
    /// 批量设置图元可见性（整数 id）
    void setSceneTreeVisibility3D(const QVector<qint64>& ids, bool visible);
    /// 批量设置图元锁定状态（整数 id）
    void setSceneTreeLock3D(const QVector<qint64>& ids, bool locked);
    /// 删除选中的图元
    void deleteSceneTreeSelection3D(const QStringList& ids);

private:
    // PIMPL + 自定义删除器：避免 MOC 编译时需要 ServiceOwner 完整定义
    std::unique_ptr<ServiceOwner, ServiceOwnerDeleter> m_serviceOwner;
    ServicePack3D m_services3D{};

    std::unique_ptr<class MainWindow3D> m_mainWindow3D;

    /// 3D 状态栏 widget。所有权与复用规则同 m_statusBar2D（见那里的注释）。
    /// deactivate() 里刻意**不**清空它：清空等于每次切回 3D 都新建一个，
    /// 而旧的仍挂在 QStatusBar 上没人删。
    QPointer<StatusBar3D> m_statusBar3D;

    /// 3D 场景树面板（与 2D 共享统一面板）
    class SceneTreePanel* m_scenePanel3D{ nullptr };

    Eg::SceneManager3D* m_sceneManager3D{ nullptr };

    QShortcut* m_deleteShortcut{ nullptr };
    QShortcut* m_backspaceShortcut{ nullptr };

    /// 3D 场景树重建防抖定时器（合并批量增删，避免每步 O(N) 重建）
    class QTimer* m_sceneTree3DRefreshTimer{ nullptr };
    /// 3D 场景树结构签名：用于判定是否需要重建
    uint64_t m_lastSceneTree3DStructureRevision{ 0 };
};
#endif

// ============================================================
using Workbench2DMain = Workbench2D;