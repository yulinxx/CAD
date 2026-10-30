#pragma once

#include "WorkbenchBase.h"
#include "UI/Render/Camera2D.h"

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
class Workbench2D final : public WorkbenchBase
{
public:
    Workbench2D();
    ~Workbench2D() override;

public:
    QString id() const override;
    QString displayName() const override;
    bool isCommandRegistered(const QString& commandId) const override;
    void dispatchCommand(const QString& commandId, const QVariantMap& params) override;
    Cmd::OperationResultBase dispatchCommandForResult(const QString& commandId, const QVariantMap& params) override;
    bool hasUnsavedChanges() const override;
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
    void applySceneTreeIncremental(const char* src = nullptr) override;

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
    void refreshPropertiesPanel() override;
    /// 属性面板编辑后刷新场景树选中行（Name 等不推进结构签名）
    void refreshSceneTreeRowsForSelection2D();
    /// 应用选择上下文到各 UI 组件
    void applySelectionContext(const CommandUiSnapshot& snapshot);
    /// 单击图层色块的延迟业务动作：无选中则选中该图层全部图元，有选中则把选中图元移到该图层
    void applyLayerClick(int layerId);

private:
    std::unique_ptr<CommandActionHub> m_commandHub;
    void onViewportContextMenu(QContextMenuEvent* event);
    QMenu* buildConfiguredContextMenu(const QString& contextMenuId, bool hasSelection);
    TopToolBar* m_topToolBar{ nullptr };
    QToolBar* m_textFontToolBar{ nullptr };
    TextFontToolBar* m_textFontToolBarWidget{ nullptr };
    RightToolBar* m_rightToolBar{ nullptr };
    int m_layerClickSequence{ 0 };
    PanelHostStyle m_panelHostStyle{ PanelHostStyle::Toolbar };
    RenderViewport2D* m_viewport{ nullptr };
    /// 上次 deactivate 时的相机（缩放 + 平移）。
    /// RenderViewport2D 每次 attach 都新建，构造函数把相机硬编码成默认台面视图
    /// （panOffset = (-600,-400)）；不抄出来，2D↔3D 往返一次用户的缩放/平移就没了。
    Camera2D m_savedCamera;
    bool m_hasSavedCamera{ false };
    SceneTreePanel* m_scenePanel2D{ nullptr };
    std::unique_ptr<SceneTreeSceneObserver2D> m_sceneTreeObserver;
    SceneMonitor* m_sceneMonitor{ nullptr };
    QPointer<QObject> m_uiStateConnections;
    const char* m_sceneTreeRefreshSource{ nullptr };
    bool m_sceneTreeForceRefresh{ false };
    bool m_treeRefreshPending{ false };
    uint64_t m_lastSceneTreeTopologyRevision{ 0 };
    QPointer<StatusBar> m_statusBar2D;

    /// 交互提示刷新定时器：200ms 刷一次当前活动工具的 interactionHint()
    /// 覆盖所有状态变化（切换工具、进入绘制、取消、完成），不需要在事件分发器里插钩子
    QTimer* m_interactionHintTimer{ nullptr };
    /// 上一次真正写入状态栏的交互提示：只在提示变化时更新，
    /// 避免 200ms 定时器把导入/导出/保存等临时状态消息冲掉。
    QString m_lastInteractionHint;
    bool m_interactionHintInitialized{ false };

    std::unique_ptr<SettingsUiCoordinator2D> m_settingsCoordinator;
    std::unique_ptr<ToolBarContextManager> m_contextManager;
    QMetaObject::Connection m_gridVisibilityMetadataConn;

    ToolBarContext determineContextFromSelection(const CommandUiSnapshot& snapshot) const;
};