#pragma once

#include <QString>
#include <QObject>

#include <memory>

#include "UiServices.h"
#include "SelectionService.h"

class QWidget;
class QToolBar;
class WorkbenchWindow;
class PropertiesPanelWidget;
class SceneTreeDockWidget;

#if BUILD_UI3D
#include "UiEntities.h"
#include "Ui/MainWindow/MainWindow3D.h"
#include "Ui/MenuManager/MenuManager3D.h"
#include "Engine3D/SceneManager3D.h"
#include "UI3D/Service/ServicePack3D.h"
#include "UI3D/Operation/OperationBus3D.h"
#include "UI3D/Manager/DocumentManager3D.h"
#include "UI3D/Edit/UndoRedoManager3D.h"
#include "UI3D/Edit/SceneEditService3D.h"
#include "UI3D/Service/SceneMonitor3D.h"
#include "UI3D/Shortcut/ShortcutManager3D.h"
#include "UI3D/Navigation/NavigationConfig3D.h"
#include "UI3D/Service/SceneDocument3D.h"
#include "UI3D/Service/CameraController3D.h"
#include "UI3D/Settings/SettingsUiCoordinator3D.h"
#include "UI3D/Operation/CommandActionHub3D.h"
#include "UI3D/Operation/AlgorithmRunner3D.h"
#include "UI/Algorithm/AlgorithmApplicationService.h"
#ifdef ENABLE_GEOMODELCORE
#include "UI3D/Service/BRepModelService3D.h"
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
    explicit UiWorkbench(QObject* parent = nullptr) : QObject(parent)
    {
    }
    ~UiWorkbench() override = default;

public:
    /// 获取工作台 ID
    virtual QString id() const = 0;

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
};

// ============================================================
/**
 * @class Workbench2D
 * @brief 2D 工作台实现
 *
 * 提供 2D 绘图功能，包括线条绘制、测量、选择等操作。
 */
class Workbench2D final : public UiWorkbench
{
public:
    Workbench2D() = default;
    ~Workbench2D() override;

    QString id() const override;
    QString displayName() const override;
    bool initialize(const UiServices& services) override;
    void attachToWindow(WorkbenchWindow& window) override;
    void activate() override;
    void deactivate() override;
    void shutdown() override;

private:
    /// 创建当前工作台应使用的中央视口
    /// @param window 主窗口
    /// @param properties 属性面板，用于旧版视口的状态回写
    /// @return 可直接设置为 centralWidget 的视口部件
    QWidget* createCentralViewport(WorkbenchWindow& window, PropertiesPanelWidget* properties);
    /// 配置新版本 ViewWidget 的最小运行状态
    /// @param viewport 新版 2D 视图控件
    void configureModernViewport(QWidget* viewport) const;
    /// 配置工作台的默认对象与状态面板内容
    /// @param properties 属性面板
    void configureWorkbenchPanels(PropertiesPanelWidget* properties) const;
    /// 创建并注册 2D 工具面板
    /// @param window 主窗口
    /// @return 创建后的停靠面板指针
    SceneTreeDockWidget* createLayersDock(WorkbenchWindow& window) const;
    /// 配置工具栏动作与命令分发绑定
    /// @param mainBar 主工具栏
    /// @param viewBar 视图工具栏
    void configureWorkbenchActions(QToolBar* mainBar, QToolBar* viewBar) const;
    /// 配置工作台初始状态与属性面板文本
    /// @param properties 属性面板
    void configureInitialWorkbenchState(PropertiesPanelWidget* properties) const;

private:
    std::unique_ptr<SelectionService> m_selectionService;

    /// 命令动作中枢：管理所有 QAction 的创建、绑定、刷新（裸指针，生命周期由 deactivate/shutdown 管理）
    class CommandActionHub* m_commandHub{ nullptr };
    /// 顶部工具栏（编辑命令）
    class TopToolBar* m_topToolBar{ nullptr };
    /// 右侧工具栏（颜色/图层）
    class RightToolBar* m_rightToolBar{ nullptr };
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
    bool initialize(const UiServices& services) override;
    void attachToWindow(WorkbenchWindow& window) override;
    void activate() override;
    void deactivate() override;
    void shutdown() override;

private:
    void build3DWorkbenchUi(WorkbenchWindow& window);
    void onMenuAction(int actionId, const QVariantMap& params);

private:
    struct ServiceOwner
    {
        std::unique_ptr<OperationBus3D> operationBus;
        std::unique_ptr<DocumentManager3D> documentManager;
        std::unique_ptr<UndoRedoManager3D> undoRedoManager;
        std::unique_ptr<SceneEditService3D> sceneEditService;
        std::unique_ptr<SceneMonitor3D> sceneMonitor;
        std::unique_ptr<ShortcutManager3D> shortcutManager;
        std::unique_ptr<NavigationConfig3D> navigationConfig;
        std::unique_ptr<SceneDocument3D> sceneDocument;
        std::unique_ptr<SceneDocument3DAdapter> sceneDocumentAdapter;
        std::unique_ptr<CameraController3D> cameraController;
        std::unique_ptr<AlgorithmApplicationService> algorithmService;
        std::unique_ptr<SettingsUiCoordinator3D> settingsCoordinator;
        std::unique_ptr<CommandActionHub3D> commandActionHub;
        std::unique_ptr<AlgorithmRunner3D> algorithmRunner;

#ifdef ENABLE_GEOMODELCORE
        std::unique_ptr<BRepModelService3D> brepModelService;
#endif
    };

    std::unique_ptr<ServiceOwner> m_serviceOwner;
    ServicePack3D m_services3D{};

    std::unique_ptr<class MainWindow3D> m_mainWindow3D;
    std::unique_ptr<class MenuManager3D> m_menuManager3D;

    Eg::SceneManager3D* m_sceneManager3D{ nullptr };

    QShortcut* m_deleteShortcut{ nullptr };
    QShortcut* m_backspaceShortcut{ nullptr };
};
#endif

// ============================================================
using Workbench2DMain = Workbench2D;