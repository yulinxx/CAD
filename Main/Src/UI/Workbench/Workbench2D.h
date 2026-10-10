#pragma once

#include "UiWorkbench.h"
#include "Workbench2DSceneTreeManager.h"
#include "Workbench2DContextMenuBuilder.h"
#include "Workbench2DPropertiesPanelManager.h"
#include "Workbench2DShortcutManager.h"
#include "Workbench2DToolbarFactory.h"
#include "Workbench2DViewportSetup.h"

class QAction;
class QMenu;
class QTimer;
class QContextMenuEvent;
class CommandActionHub;
class RenderViewport2D;
class SceneTreePanel;
class SceneMonitor;
class SettingsUiCoordinator2D;
class OperationBus;
struct CommandUiSnapshot;

/**
 * @class Workbench2D
 * @brief 2D 工作台实现
 *
 * 提供 2D 绘图功能，包括线条绘制、测量、选择等操作。
 */
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

    SceneTreePanel* scenePanel() const;

private:
    /// 创建中央视口
    QWidget* createCentralViewport(WorkbenchWindow& window, PropertiesPanelWidget* properties);
    /// 注入服务到视口
    void setupViewportServices(RenderViewport2D* vp, WorkbenchWindow& window);
    /// 设置导入服务回调
    void setupImportCallbacks(RenderViewport2D* vp, WorkbenchWindow& window);
    /// 创建工具栏
    void createToolbars(WorkbenchWindow& window);
    /// 给工厂创建的右侧图层栏注入图层服务并接线
    void setupRightToolbarLayers();
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
    /// 视口右键菜单请求（转发给右键菜单构建器）
    void onViewportContextMenu(QContextMenuEvent* event);
    /// 应用选择上下文到各 UI 组件
    void applySelectionContext(const CommandUiSnapshot& snapshot);

private:
    /// 命令动作中枢
    std::unique_ptr<CommandActionHub> m_commandHub;
    PanelHostStyle m_panelHostStyle{ PanelHostStyle::Toolbar };  ///< 左右面板承载样式
    RenderViewport2D* m_viewport{ nullptr };         ///< 2D 渲染视口
    std::unique_ptr<Workbench2DSceneTreeManager> m_sceneTreeManager;
    std::unique_ptr<Workbench2DContextMenuBuilder> m_contextMenuBuilder;
    std::unique_ptr<Workbench2DPropertiesPanelManager> m_propertiesManager;
    std::unique_ptr<Workbench2DShortcutManager> m_shortcutManager;
    std::unique_ptr<Workbench2DToolbarFactory> m_toolbarFactory;
    std::unique_ptr<Workbench2DViewportSetup> m_viewportSetup;
    SceneMonitor* m_sceneMonitor{ nullptr };
    QPointer<QObject> m_uiStateConnections;                ///< 命令 UI 刷新连线句柄
    QPointer<StatusBar> m_statusBar2D;  ///< 2D 状态栏 widget

    /// 2D 设置协调器
    std::unique_ptr<SettingsUiCoordinator2D> m_settingsCoordinator;

    /// 本工作台在 attach 期间建立的 Qt 连接，deactivate 时整批 disconnect（RAII 批量回收）
    std::vector<QMetaObject::Connection> m_workbenchConnections;
};

inline SceneTreePanel* Workbench2D::scenePanel() const
{
    return m_sceneTreeManager ? m_sceneTreeManager->panel() : nullptr;
}