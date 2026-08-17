#pragma once

#include <QString>
#include <QObject>

#include <memory>

#include "UiServices.h"
#include "UI/Service/ToolBarContextManager.h"

class QWidget;
class QToolBar;
class WorkbenchWindow;
class PropertiesPanelWidget;
class RenderViewport2D;
class StatusBar;
class SettingsService;
class SettingsUiCoordinator2D;
class SceneTreePanel2D;
class SceneTreePanel3D;
class SceneTreeSceneObserver2D;
class SceneMonitor;
struct UiStateSnapshot;

// 3D 类型前向声明（避免头文件膨胀，实际 include 下沉到 .cpp）
#if BUILD_UI3D
    #include <QShortcut>
    #include "UI3D/Service/ServicePack3D.h"    // 值成员需要完整定义
    #include "UI/MainWindow/MainWindow3D.h"    // unique_ptr 成员，MOC 需要完整类型
    #include "UI/MenuManager/MenuManager3D.h"  // unique_ptr 成员，MOC 需要完整类型

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
class NavigationConfig3D;
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
 * @struct WorkbenchStateSnapshot
 * @brief 工作台状态快照
 *
 * 用于工作台切换时保存和恢复状态，避免切换后丢失当前选择、视图模式等信息。
 * 每个工作台在 deactivate() 时保存状态，在 activate() 时恢复状态。
 *
 * 状态字段覆盖：
 *   - 文档/视图：documentId, viewMode, viewportType, viewportStatus
 *   - 图层/选择：layerId, selectionSource, selectionText, selectionType
 *   - 工具/输入：activeToolId, inputFocusWidget
 *   - 脏状态：dirty
 */
struct WorkbenchStateSnapshot
{
    /// 视图模式
    QString viewMode;
    /// 图层 ID
    QString layerId;
    /// 文档 ID
    QString documentId;
    /// 选择来源
    QString selectionSource;
    /// 选择文本
    QString selectionText;
    /// 选择类型
    QString selectionType;
    /// 视口类型
    QString viewportType;
    /// 视口状态
    QString viewportStatus;
    /// 当前激活工具 ID（切换后恢复工具状态）
    QString activeToolId;
    /// 当前输入焦点控件名称（切换后恢复焦点）
    QString inputFocusWidget;
    /// 是否有未保存更改
    bool dirty{ false };
};

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
class UiWorkbench : public QObject
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
    virtual bool isCommandRegistered(const QString& commandId) const;

    /// 从当前工作台命令目录分发命令
    /// 菜单层只传递 commandId，不直接依赖具体 OperationBus 类型。
    virtual void dispatchCommand(const QString& commandId);

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
    virtual WorkbenchStateSnapshot currentSnapshot() const;

    /// 恢复状态快照
    /// @param snapshot 要恢复的状态快照
    virtual void restoreFromSnapshot(const WorkbenchStateSnapshot& snapshot);

protected:
    /// UI 服务副本（避免持有外部临时引用）
    UiServices m_services;
    /// 初始化时缓存的状态，供首次激活使用
    WorkbenchStateSnapshot m_initialState;
    /// 上次停用前保存的状态快照，供下次激活时恢复
    WorkbenchStateSnapshot m_savedState;
    /// 共享 SettingsService singleton（app-level 共享，非每工作台私有）
    SettingsService* m_settingsService{ nullptr };
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
    void dispatchCommand(const QString& commandId) override;
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

private:
    /// 创建中央视口
    QWidget* createCentralViewport(WorkbenchWindow& window, PropertiesPanelWidget* properties);
    /// 注入服务到视口：选择/交互/操作总线 + 编辑服务信号连接 + 状态回调 + 工具初始化
    void setupViewportServices(RenderViewport2D* vp, WorkbenchWindow& window);
    /// 设置导入服务回调：zoomToFit / 场景树刷新 / 属性面板刷新
    void setupImportCallbacks(RenderViewport2D* vp, WorkbenchWindow& window);
    /// 创建左侧绘图工具栏 + 顶部编辑工具栏 + 右侧颜色/图层工具栏
    void createToolbars(WorkbenchWindow& window);
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

private:
    /// 命令动作中枢：管理所有 QAction 的创建、绑定、刷新
    std::unique_ptr<class CommandActionHub> m_commandHub;
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
    /// 2D 场景树面板（可选 UI，配置驱动时可能不存在）
    class SceneTreePanel2D* m_scenePanel2D{ nullptr };
    /// 场景变更观察者（捕获绕过操作总线的直接编辑，如视口 Delete 键）
    std::unique_ptr<SceneTreeSceneObserver2D> m_sceneTreeObserver;
    /// 场景变更监控（IObserver → Qt 信号桥接：捕获拖拽/交互式修改等非操作总线路径的场景变更）
    SceneMonitor* m_sceneMonitor{ nullptr };
    /// 场景树重建防抖定时器（合并批量增删，避免每步 O(N) 重建）
    class QTimer* m_sceneTreeRefreshTimer{ nullptr };
    /// 上次记录的图元数量（判断是否发生结构变更）
    std::size_t m_lastSceneEntityCount{ 0 };
    /// 2D 状态栏 widget — 由 StatusBar 基类管理，通过 mountStatusBar 挂载到 WorkbenchWindow
    StatusBar* m_statusBar2D{ nullptr };

    /// 2D 设置协调器（共享 SettingsService  singleton）
    std::unique_ptr<SettingsUiCoordinator2D> m_settingsCoordinator;

    /// 工具栏上下文管理器：管理不同编辑模式下的工具栏配置与切换
    std::unique_ptr<ToolBarContextManager> m_contextManager;

    /// 根据当前选中图元类型确定应切换到的工具栏上下文
    ToolBarContext determineContextFromSelection() const;

    /// 当前挂载的 2D 工作台窗口（组合根绑定点，用于向属性面板推送模型）
    WorkbenchWindow* m_workbenchWindow{ nullptr };
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
    void dispatchCommand(const QString& commandId) override;
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
    void onMenuAction(int actionId, const QVariantMap& params);
    void create3DViewport(WorkbenchWindow& window);
    void bind3DRenderSignals(ServiceOwner& own);
    void bind3DCursorSignal();
    void bind3DSelectionSignal();
    void bind3DDeleteKeySignal();
    void setup3DDeleteShortcuts(WorkbenchWindow& window);

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

private:
    // PIMPL + 自定义删除器：避免 MOC 编译时需要 ServiceOwner 完整定义
    std::unique_ptr<ServiceOwner, ServiceOwnerDeleter> m_serviceOwner;
    ServicePack3D m_services3D{};

    std::unique_ptr<class MainWindow3D> m_mainWindow3D;
    std::unique_ptr<class MenuManager3D> m_menuManager3D;

    /// 3D 状态栏 widget — 由 StatusBar3D 基类管理，通过 mountStatusBar 挂载到 WorkbenchWindow
    StatusBar3D* m_statusBar3D{ nullptr };

    /// 3D 场景树面板（可选 UI，配置驱动时可能不存在）
    class SceneTreePanel3D* m_scenePanel3D{ nullptr };

    Eg::SceneManager3D* m_sceneManager3D{ nullptr };

    QShortcut* m_deleteShortcut{ nullptr };
    QShortcut* m_backspaceShortcut{ nullptr };
};
#endif

// ============================================================
using Workbench2DMain = Workbench2D;