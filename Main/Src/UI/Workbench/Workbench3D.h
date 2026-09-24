#pragma once

#include "UiWorkbench.h"

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
struct CommandUiSnapshot3D;
    #ifdef ENABLE_GEOMODELCORE
class BRepModelService3D;
    #endif

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
    void bind3DSelectionSignal();
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
    class ServicePack3D m_services3D{};

    std::unique_ptr<MainWindow3D> m_mainWindow3D;

    /// 3D 状态栏 widget。所有权与复用规则同 m_statusBar2D（见那里的注释）。
    /// deactivate() 里刻意**不**清空它：清空等于每次切回 3D 都新建一个，
    /// 而旧的仍挂在 QStatusBar 上没人删。
    QPointer<StatusBar3D> m_statusBar3D;

    /// 3D 场景树面板（与 2D 共享统一面板）
    SceneTreePanel* m_scenePanel3D{ nullptr };

    Eg::SceneManager3D* m_sceneManager3D{ nullptr };

    QShortcut* m_deleteShortcut{ nullptr };
    QShortcut* m_backspaceShortcut{ nullptr };

    /// 3D 场景树重建防抖定时器（合并批量增删，避免每步 O(N) 重建）
    QTimer* m_sceneTree3DRefreshTimer{ nullptr };
    /// 3D 场景树结构签名：用于判定是否需要重建
    uint64_t m_lastSceneTree3DStructureRevision{ 0 };
};
#endif