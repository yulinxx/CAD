#pragma once

#include "UiWorkbench.h"

class QAction;
class QMenu;
class QTimer;
class QContextMenuEvent;
class CommandActionHub;
class TopToolBar;
class RightToolBar;
class TextFontToolBar;
class RenderViewport2D;
class SceneTreePanel;
class SceneTreeSceneObserver2D;
class SceneMonitor;
class ToolBarContextManager;
class SettingsUiCoordinator2D;
class OperationBus;
struct CommandUiSnapshot;

/// 2D 左右面板（Draw Tools / Layers）的承载样式
enum class PanelHostStyle
{
    Toolbar,  ///< 固定工具栏（QToolBar 停靠在左右两侧）
    Dock      ///< Dock 停靠面板（QDockWidget，默认）
};

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
    /// 属性面板编辑后刷新场景树选中行（Name 等不推进结构签名）
    void refreshSceneTreeRowsForSelection2D();
    /// 应用选择上下文到各 UI 组件
    void applySelectionContext(const CommandUiSnapshot& snapshot);

private:
    /// 命令动作中枢
    std::unique_ptr<CommandActionHub> m_commandHub;
    /// 视口右键菜单请求
    void onViewportContextMenu(QContextMenuEvent* event);
    /// 按客户配置构建 2D 右键菜单
    QMenu* buildConfiguredContextMenu(const QString& contextMenuId, bool hasSelection);
    TopToolBar* m_topToolBar{ nullptr };              ///< 顶部编辑工具栏
    QToolBar* m_textFontToolBar{ nullptr };           ///< 文字编辑字体工具栏
    TextFontToolBar* m_textFontToolBarWidget{ nullptr };
    RightToolBar* m_rightToolBar{ nullptr };          ///< 右侧工具栏
    PanelHostStyle m_panelHostStyle{ PanelHostStyle::Toolbar };  ///< 左右面板承载样式
    RenderViewport2D* m_viewport{ nullptr };         ///< 2D 渲染视口
    SceneTreePanel* m_scenePanel2D{ nullptr };        ///< 2D 场景树面板
    std::unique_ptr<SceneTreeSceneObserver2D> m_sceneTreeObserver;
    SceneMonitor* m_sceneMonitor{ nullptr };
    QPointer<QObject> m_uiStateConnections;                ///< 命令 UI 刷新连线句柄
    QTimer* m_sceneTreeRefreshTimer{ nullptr };      ///< 场景树重建防抖定时器
    const char* m_sceneTreeRefreshSource{ nullptr };        ///< 场景树重建触发来源
    uint64_t m_sceneTreeCursor{ 0 };                       ///< 场景树增量游标
    bool m_sceneTreeForceRefresh{ false };                  ///< 强制全量刷新标志
    /// applySceneTreeIncremental 重入标志：分类/追加期间会触发嵌套的场景通知，
    /// 嵌套调用读到的游标还是外层尚未推进的旧值，会把同一批变更整表再扫一遍。
    bool m_sceneTreeIncrementalBusy{ false };
    /// 场景树延迟重建标记（setData 回调链中 scheduleTreeRefresh 合并，避免 delete this）
    bool m_treeRefreshPending{ false };
    /// 属性面板重建节流定时器：active 期间的多次请求合并为窗口末尾一次
    QTimer* m_propertiesRefreshTimer{ nullptr };
    /// 请求一次属性面板重建（节流合并，非每请求一个 singleShot）
    void schedulePropertiesPanelRefresh();
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

    /// 本工作台在 attach 期间建立的 Qt 连接，deactivate 时整批 disconnect（RAII 批量回收）
    std::vector<QMetaObject::Connection> m_workbenchConnections;

    /// 由选择上下文快照推导应切换到的工具栏上下文
    ToolBarContext determineContextFromSelection(const CommandUiSnapshot& snapshot) const;
};