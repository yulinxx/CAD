#pragma once

#include "WorkbenchBase.h"

#if BUILD_UI3D
class QShortcut;
class QMenu;
class QTimer;
class MainWindow3D;
class SceneTreePanel;
class StatusBar3D;
class SceneMonitor3D;
class OperationBus3D;
class DocumentManager3D;
class UndoRedoManager3D;
class SceneEditService3D;
class SceneDocument3D;
class CameraController3D;
class SettingsUiCoordinator3D;
class CommandActionHub3D;
class AlgorithmRunner3D;
class RenderWidget3D;
struct CommandUiSnapshot3D;
    #ifdef ENABLE_GEOMODELCORE
class BRepModelService3D;
    #endif

/**
 * @class Workbench3D
 * @brief 3D 工作台实现
 * 使用 MainWindow3D 架构（服务经 ServicePack3D 直接传递，无全局定位器）
 */
class Workbench3D final : public WorkbenchBase
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

    /// 3D 撤销/重做栈（跨工作台切换存活的长寿命对象）。
    /// 供回归测试断言「切台不丢撤销历史」，也供框架层查询撤销能力。
    UndoRedoManager3D* undoRedoManager3D() const;

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
    void bind3DSelectionSignal();
    /// 把当前选择集重新推给 3D 状态栏（切回 3D 时用）。
    /// 新建的 RenderWidget3D 不会补发 sigSelectionChanged，
    /// 而 unmountStatusBar 的 clearAll() 已清空选择/信息区 → 状态栏空白。
    void refreshStatusBarSelection3D();
    void setup3DDeleteShortcuts(WorkbenchWindow& window);
    /// 3D 视口右键菜单请求：基于命令中枢快照构建并弹出（与 2D 统一的单一事实来源）
    void on3DContextMenuRequested(const QPoint& globalPos);
    /// 按客户配置构建 3D 右键菜单
    /// @param contextMenuId JSON contextMenus 节中的菜单 ID，例如 "canvas.3d"
    /// @return 配置菜单；未配置时返回 nullptr，调用方回退到内建路径。
    ///         返回的菜单归调用方所有，可在任意时机销毁（分发器是
    ///         WorkbenchMenuManager 持有的长寿命 MenuDispatcher，不依赖菜单寿命）。
    QMenu* buildConfiguredContextMenu(const QString& contextMenuId);

    // ---- 3D 场景树（数据/算法/UI 分离，UI 可定制/可缺失） ----
    /// 绑定并填充 3D 场景树面板（数据经算法层由引擎场景生成）
    void setupSceneTree3D(WorkbenchWindow& window);
    /// 重建场景树模型并推送到面板（结构性变化：导入/撤销/增删）
    void refreshSceneTree3D();
    /// 消费上次游标以来的变更：纯 Added 追加行，否则回退全量（与 2D applySceneTreeIncremental 同语义）
    void applySceneTreeIncremental3D(const char* src = nullptr);

    // ---- 3D 属性面板（与 2D 同一 PropertiesPanelWidget + PropertyModel 契约） ----
    /// 探测属性面板并绑定编辑回刷（面板可缺失，配置驱动 UI）
    void setupProperties3D(WorkbenchWindow& window);
    /// 按当前选中图元重建属性模型并推送到面板
    void refreshPropertiesPanel3D();
    /// 请求一次属性面板重建（100ms 尾包合并，与 2D 同语义）
    void schedulePropertiesPanelRefresh3D();
    /// 属性面板编辑后把选中图元的 Name/Visible 写回场景树行（结构签名未变，不能走全量）
    void refreshSceneTreeRowsForSelection3D();

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
    struct ServicePack3D m_services3D{};

    std::unique_ptr<MainWindow3D> m_mainWindow3D;

    /// 具体视口（Main 层是视口的创建者与信号连接方，需要具体类型连 Qt 信号）。
    /// 业务代码仍通过 m_services3D.renderWidget（窄接口）访问。
    RenderWidget3D* m_renderWidget3D{ nullptr };

    /// 3D 相机位姿快照（值类型，跨 deactivate 存活）。
    /// RenderWidget3D 每次 attach 都重建，Camera3D 是它的成员 → 位姿随之回到默认值
    /// （eye 150/150/150、center 原点）；不抄出来，切回 3D 时视角就复位。
    /// 用裸 float 而不是 Camera3D：本头文件里 Camera3D 没有完整定义。
    struct CameraPose
    {
        float eye[3]{ 0.0f, 0.0f, 0.0f };
        float center[3]{ 0.0f, 0.0f, 0.0f };
        float up[3]{ 0.0f, 1.0f, 0.0f };
        bool valid{ false };
    };

    CameraPose m_savedCameraPose;

    /// 3D 状态栏 widget。所有权与复用规则同 m_statusBar2D（见那里的注释）。
    /// deactivate() 里刻意**不**清空它：清空等于每次切回 3D 都新建一个，
    /// 而旧的仍挂在 QStatusBar 上没人删。
    QPointer<StatusBar3D> m_statusBar3D;

    /// 3D 场景树面板（与 2D 共享统一面板）
    SceneTreePanel* m_scenePanel3D{ nullptr };

    Eg::SceneManager3D* m_sceneManager3D{ nullptr };

    /// 3D 撤销/重做栈与文档管理器（跨工作台切换存活）。
    ///
    /// 刻意不放进 ServiceOwner：ServiceOwner 随 deactivate() 一起销毁
    /// （m_serviceOwner.reset()），而工作台实例本身跨切换存活。
    /// 放在这里，3D 的撤销栈才能跨「2D → 3D → 2D → 3D」往返保留 ——
    /// 否则每次切走再切回来，3D 里做的修改就再也 Ctrl+Z 不回去了。
    ///
    /// PIMPL：完整定义在 .cpp（自定义删除器同理）。不能直接写
    /// unique_ptr<UndoRedoManager3D> 这类成员 —— 头文件里它们只是前向声明，
    /// 会让别的 TU 在实例化 Workbench3D 隐式默认构造/析构时报「不完整类型」。
    struct SessionState;
    struct SessionStateDeleter
    {
        void operator()(SessionState*) const;
    };
    std::unique_ptr<SessionState, SessionStateDeleter> m_sessionState;

    QShortcut* m_deleteShortcut{ nullptr };
    QShortcut* m_backspaceShortcut{ nullptr };

    /// 3D 场景树重建防抖定时器（合并批量增删，避免每步 O(N) 重建）
    QTimer* m_sceneTree3DRefreshTimer{ nullptr };
    /// 3D 场景树结构签名：图元数量 + 结构修订号
    std::size_t m_lastSceneTree3DEntityCount{ 0 };
    uint64_t m_lastSceneTree3DStructureRevision{ 0 };
    /// 场景树增量游标（ISceneChangeStream；与 2D 同语义）
    uint64_t m_sceneTree3DCursor{ 0 };
    /// applySceneTreeIncremental3D 重入标志（分类/追加期间可能触发嵌套场景通知）
    bool m_sceneTree3DIncrementalBusy{ false };
    /// 属性面板重建节流定时器：active 期间多次请求合并为窗口末尾一次（与 2D 同语义）
    QTimer* m_propertiesRefreshTimer3D{ nullptr };
};
#endif