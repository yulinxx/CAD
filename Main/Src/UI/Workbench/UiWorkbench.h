#pragma once

#include <QString>
#include <QObject>
#include <QPointer>
#include <QVector>

#include <memory>

#include "UIServices.h"
#include "UI/Service/ToolBarContextManager.h"
#include "Services/UiStateCenter.h"
#include "ClientConfig/UiLayoutBuilder.h"    // IUiCommandDispatcher：工作台直接实现该接口

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
    #include "UI3D/Service/ServicePack3D.h"    // 值成员需要完整定义
    #include "UI/MainWindow/MainWindow3D.h"    // unique_ptr 成员，MOC 需要完整类型


namespace Eg
{
    class SceneManager3D;
}
class OperationBus3D;
class DocumentManager3D;
class UndoRedoManager3D;
class SceneEditService3D;
class SceneMonitor3D;
class ShortcutManager3D;
class SceneDocument3D;
class CameraController3D;
class SettingsUiCoordinator3D;
class CommandActionHub3D;
class AlgorithmRunner3D;
class AlgorithmApplicationService;
class SceneDocument3DAdapter;
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

    /// IUiCommandDispatcher 入口，等价于 dispatchCommand。
    ///
    /// 工作台自身就是分发器 —— 不要再为右键菜单／布局构建器另建适配器对象：
    /// UiLayoutBuilder 会把 dispatcher 裸指针捕进 QAction 的 triggered 闭包，
    /// 闭包的寿命跟随 QMenu，比任何局部适配器都长。历史上
    /// buildConfiguredContextMenu 在栈上建适配器再传地址，函数返回即失效，
    /// 点右键菜单项时 dispatch 打在已回收的栈帧上（3D 删除必崩）。
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
    virtual bool initialize(const UiServices& services) = 0;

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
    virtual void refreshCommandUiState()
    {
    }


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
    /// UI 服务副本（避免持有外部临时引用）
    UiServices m_services;
    /// 初始化时缓存的状态，供首次激活使用
    UiStateSnapshot m_initialState;
    /// 上次停用前保存的状态快照，供下次激活时恢复
    UiStateSnapshot m_savedState;
    /// 共享 SettingsService singleton（app-level 共享，非每工作台私有）
    SettingsService* m_settingsService{ nullptr };
    /// 当前挂载的工作台窗口（组合根绑定点：属性面板推送、菜单管理器访问）
    /// 由各子类在 attachToWindow 中赋值；2D/3D 都需要，故提到基类
    WorkbenchWindow* m_workbenchWindow{ nullptr };
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
    bool initialize(const UiServices& services) override;
    void attachToWindow(WorkbenchWindow& window) override;
    void activate() override;
    void deactivate() override;
    void shutdown() override;

    // 框架层委托接口
    void releaseCentralWidgetGLResources(QWidget* centralWidget) const override;

    bool showSettingsDialog(QWidget* parent) override;

public:
    /// 设置左右面板（Draw Tools / Layers）的承载样式（默认 Dock）
    void setPanelHostStyle(PanelHostStyle style);
    /// 当前左右面板承载样式
    PanelHostStyle panelHostStyle() const { return m_panelHostStyle; }

    /// 重新抓取选择上下文快照并驱动全部命令 UI（工具栏/菜单栏/右键菜单/面板/状态栏）
    /// 唯一刷新入口，由 UiStateBridge2D 在各触发源上统一调用，
    /// 框架层重建菜单后也会经基类虚接口回调到这里。
    void refreshCommandUiState() override;


private:
    /// 创建中央视口
    QWidget* createCentralViewport(WorkbenchWindow& window, PropertiesPanelWidget* properties);
    /// 注入服务到视口：选择/交互/操作总线 + 编辑服务信号连接 + 状态回调 + 工具初始化
    void setupViewportServices(RenderViewport2D* vp, WorkbenchWindow& window);
    /// 设置导入服务回调：zoomToFit / 场景树刷新 / 属性面板刷新
    void setupImportCallbacks(RenderViewport2D* vp, WorkbenchWindow& window);
    /// 创建左侧绘图工具栏 + 顶部编辑工具栏 + 右侧颜色/图层工具栏
    void createToolbars(WorkbenchWindow& window);
    /// 取出左侧绘图工具栏要展示的中枢 QAction（顺序即命令目录顺序）
    QVector<QAction*> buildDrawToolActions();
    /// 从命令目录汇总可支持的导入格式入口（2D/3D 共用视图）
    static QStringList buildSupportedImportFormats(const QString& workbenchId);
    /// 绑定并填充 2D 场景树面板（数据经算法层由引擎场景生成，UI 可定制/可缺失）
    void setupSceneTree(WorkbenchWindow& window);
    /// 重建场景树模型并推送到面板（结构性变化：导入/撤销/增删）
    void refreshSceneTree();
    /// 引擎场景变更兜底：图元数量变化即视为结构变更，防抖后重建树，避免残留
    void onSceneTreeSceneChanged();
    /// 仅同步面板选中高亮（选择变化，避免重建树导致折叠丢失）
    void syncSceneTreeSelection();
    /// 将面板选择同步到引擎选择
    void applySceneTreeSelection(const QStringList& ids);
    /// 切换图元可见性（直接写引擎并刷新）
    void toggleEntityVisibility(const QString& id, bool visible);
    /// 重命名图元（直接写引擎并刷新）
    void renameEntity(const QString& id, const QString& newName);
    /// 从场景树批量删除实体（走编辑服务，可撤销）
    void deleteSceneTreeSelection(const QStringList& ids);
    /// 从场景树批量设置可见性
    void setSceneTreeVisibility(const QStringList& ids, bool visible);
    /// 从场景树批量设置锁定
    void setSceneTreeLock(const QStringList& ids, bool locked);
    /// 将当前选中图元生成为属性模型并推送到属性面板（面板不存在则安全忽略）
    /// 通过 EntityPropertyModel2D（算法层）+ PropertyModel（数据层）解耦，
    /// 本方法仅作为组合根把"数据/算法"绑定到"UI"，面板可随时替换/移除。
    void refreshPropertiesPanel();
    /// 消费命令中枢广播的选择上下文快照，扇出到属性面板/状态栏/场景树/工具栏上下文
    /// （单一事件总线：所有 UI 联动共用同一份快照，避免各自二次遍历导致规则漂移）
    void applySelectionContext(const CommandUiSnapshot& snapshot);

private:
    /// 命令动作中枢：管理所有 QAction 的创建、绑定、刷新
    std::unique_ptr<class CommandActionHub> m_commandHub;
    /// 视口右键菜单请求：基于命令中枢构建并弹出右键菜单，实现选择/锁定的实时联动
    void onViewportContextMenu(QContextMenuEvent* event);
    /// 按客户配置构建 2D 右键菜单（P0-2b）
    /// @param contextMenuId JSON contextMenus 节中的菜单 ID，例如 "canvas.2d"
    /// @param hasSelection 当前是否有选中实体（决定图层动态段是否含「移动到图层…」）
    /// @return 配置菜单；未配置或无可用条目时返回 nullptr，调用方回退到内建路径。
    ///         返回的菜单归调用方所有，且必须在同一作用域内 delete（命令分发器是栈对象）。
    QMenu* buildConfiguredContextMenu(const QString& contextMenuId, bool hasSelection);
    /// 顶部工具栏（编辑命令）— Qt 父对象管理生命周期
    class TopToolBar* m_topToolBar{ nullptr };
    /// 文字编辑字体工具栏（双击文字进入编辑时显示）— Qt 父对象管理生命周期
    class QToolBar* m_textFontToolBar{ nullptr };
    class TextFontToolBar* m_textFontToolBarWidget{ nullptr };
    /// 右侧工具栏（颜色/图层）— Qt 父对象管理生命周期
    class RightToolBar* m_rightToolBar{ nullptr };
    /// 左右面板承载样式（Draw Tools / Layers）
    PanelHostStyle m_panelHostStyle{ PanelHostStyle::Toolbar };
    /// 2D 渲染视口 — Qt 父对象管理生命周期（工作台切换时用于恢复工具状态）
    class RenderViewport2D* m_viewport{ nullptr };
    /// 2D 场景树面板（统一面板，支持 2D 和 3D）
    class SceneTreePanel* m_scenePanel2D{ nullptr };
    /// 场景变更观察者（捕获绕过操作总线的直接编辑，如视口 Delete 键）
    std::unique_ptr<SceneTreeSceneObserver2D> m_sceneTreeObserver;
    /// 场景变更监控（IObserver → Qt 信号桥接：捕获拖拽/交互式修改等非操作总线路径的场景变更）
    SceneMonitor* m_sceneMonitor{ nullptr };
    /// 命令 UI 刷新连线的生命周期句柄（UiStateBridge2D::install 返回）。
    /// 用 QPointer 是因为它 parent 在本对象上，shutdown 路径可能先被父级批量回收；
    /// deactivate 里销毁它即整批断开连线，见 UiStateBridge2D.h「连线的寿命」。
    QPointer<QObject> m_uiStateConnections;
    /// 场景树重建防抖定时器（合并批量增删，避免每步 O(N) 重建）
    class QTimer* m_sceneTreeRefreshTimer{ nullptr };
    /// 上次记录的图元数量（判断是否发生结构变更）
    std::size_t m_lastSceneEntityCount{ 0 };
    /// 2D 状态栏 widget。
    /// 所有权在 Qt 父子关系：创建时挂在 WorkbenchWindow 上，mountStatusBar 里
    /// QStatusBar::addWidget 会把它再 reparent 到 QStatusBar。QStatusBar 跨工作台
    /// 切换一直存在，所以这个对象在 unmountStatusBar 之后仍然活着，下次 attach
    /// 直接复用。用 QPointer 而不是裸指针：一旦哪天它被 Qt 或别处销毁，
    /// `if (!m_statusBar2D) new ...` 这条复用判断会自动走到重建分支，而不是拿着
    /// 悬空指针去 mount。
    QPointer<StatusBar> m_statusBar2D;

    /// 2D 设置协调器（共享 SettingsService  singleton）
    std::unique_ptr<SettingsUiCoordinator2D> m_settingsCoordinator;

    /// 工具栏上下文管理器：管理不同编辑模式下的工具栏配置与切换
    std::unique_ptr<ToolBarContextManager> m_contextManager;

    /// 网格显隐 metadata 连接（切换工作台时需断开，防止悬空视口指针回调）
    QMetaObject::Connection m_gridVisibilityMetadataConn;

    /**
     * @brief 由选择上下文快照推导应切换到的工具栏上下文。
     *
     * 入参而不是内部再去问 Hub：扇出链上所有消费者必须看同一份快照，
     * 否则又回到「依赖 Hub 缓存是否已更新」的顺序耦合。
     */
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
    bool initialize(const UiServices& services) override;
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
    /// 按客户配置构建 3D 右键菜单（P0-2b）
    /// @param contextMenuId JSON contextMenus 节中的菜单 ID，例如 "canvas.3d"
    /// @return 配置菜单；未配置时返回 nullptr，调用方回退到内建路径。
    ///         返回的菜单归调用方所有，且必须在同一作用域内 delete（命令分发器是栈对象）。
    QMenu* buildConfiguredContextMenu(const QString& contextMenuId);

    // ---- 3D 场景树（数据/算法/UI 分离，UI 可定制/可缺失） ----
    /// 绑定并填充 3D 场景树面板（数据经算法层由引擎场景生成）
    void setupSceneTree3D(WorkbenchWindow& window);
    /// 重建场景树模型并推送到面板（结构性变化：导入/撤销/增删）
    void refreshSceneTree3D();
    /// 仅同步面板选中高亮（选择变化，避免重建树导致折叠丢失）
    void syncSceneTreeSelection3D();
    /// 将面板选择同步到引擎选择
    void applySceneTreeSelection3D(const QStringList& ids);
    /// 切换图元可见性（直接写引擎并刷新）
    void toggleEntityVisibility3D(const QString& id, bool visible);
    /// 重命名图元（直接写引擎并刷新）
    void renameEntity3D(const QString& id, const QString& newName);
    /// 批量设置图元可见性
    void setSceneTreeVisibility3D(const QStringList& ids, bool visible);
    /// 批量设置图元锁定状态
    void setSceneTreeLock3D(const QStringList& ids, bool locked);
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
};
#endif

// ============================================================
using Workbench2DMain = Workbench2D;