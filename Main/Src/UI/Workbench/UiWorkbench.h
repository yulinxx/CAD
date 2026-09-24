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
    UiStateSnapshot m_initialState;                 ///< 初始化时缓存的状态
    UiStateSnapshot m_savedState;                   ///< 上次停用前保存的状态快照
    SettingsService* m_settingsService{ nullptr };  ///< app-level singleton
    WorkbenchWindow* m_workbenchWindow{ nullptr };  ///< 当前挂载的工作台窗口
};

// ============================================================