#pragma once

#include <QString>

#include <memory>

#include "UiEntities.h"
#include "UiViewWidgets.h"
#include "UiServices.h"

class QWidget;
class QToolBar;
class WorkbenchWindow;
class PropertiesPanelWidget;
class CanvasViewport2D;
class SceneTreeDockWidget;
class ViewWidgetAdapter;

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
class UiWorkbench
{
public:
    virtual ~UiWorkbench() = default;

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
    QString id() const override;
    QString displayName() const override;
    bool initialize(const UiServices& services) override;
    void attachToWindow(WorkbenchWindow& window) override;
    void activate() override;
    void deactivate() override;
    void shutdown() override;

    /// 设置是否使用旧版 CanvasViewport2D 作为临时回退路径
    /// @param enabled true 表示回退到旧视口，false 表示默认使用 ViewWidget
    void setUseLegacyCanvasViewport(bool enabled);
    /// 获取当前是否启用旧版 CanvasViewport2D 回退路径
    /// @return true 表示当前使用旧版视口
    bool useLegacyCanvasViewport() const;

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
    /// @param firstLine 主选中线
    /// @param secondLine 次选中线
    void configureWorkbenchPanels(PropertiesPanelWidget* properties,
        const std::shared_ptr<LineEntity2D>& firstLine,
        const std::shared_ptr<LineEntity2D>& secondLine) const;
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
    /// @param firstLine 主选中线
    /// @param secondLine 次选中线
    void configureInitialWorkbenchState(PropertiesPanelWidget* properties,
        const std::shared_ptr<LineEntity2D>& firstLine,
        const std::shared_ptr<LineEntity2D>& secondLine) const;
    /// 配置旧版 CanvasViewport2D 的运行状态
    /// @param viewport 旧版 2D 视口
    /// @param properties 属性面板
    void configureLegacyViewport(CanvasViewport2D* viewport, PropertiesPanelWidget* properties);

private:
    /// 是否使用旧版 CanvasViewport2D 作为临时回退
    bool m_useLegacyCanvasViewport{ false };
    /// ViewWidget 适配器，让 OperationBus 能在旧系统中工作
    std::unique_ptr<class ViewWidgetAdapter> m_viewWidgetAdapter;
    /// 2D 实体文档
    std::shared_ptr<EntityDocument2D> m_document;
};

// ============================================================ 
/**
 * @class Workbench3D
 * @brief 3D 工作台实现
 *
 * 提供 3D 场景浏览功能，包括轨道旋转、缩放、节点选择等操作。
 */
class Workbench3D final : public UiWorkbench
{
public:
    QString id() const override;
    QString displayName() const override;
    bool initialize(const UiServices& services) override;
    void attachToWindow(WorkbenchWindow& window) override;
    void activate() override;
    void deactivate() override;
    void shutdown() override;

private:
    /// 组装 3D 工作台 UI
    void build3DWorkbenchUi(WorkbenchWindow& window);
    /// 创建 3D 场景树与属性面板
    void build3DScenePanels(WorkbenchWindow& window, PropertiesPanelWidget*& properties, SceneTreeDockWidget*& sceneDock, QString& rootNodeId);
    /// 创建 3D 视口
    QWidget* build3DViewport(WorkbenchWindow& window, PropertiesPanelWidget* properties, SceneTreeDockWidget* sceneDock);
    /// 创建 3D 工具栏
    void build3DToolBars(WorkbenchWindow& window);
    /// 初始化 3D 初始状态
    void init3DInitialState(const SceneDocument3D& scene, const QString& rootNodeId);
    /// 场景树选择回调（提取自 build3DScenePanels 以减少闭包复杂度）
    void onSceneTreeSelection(const QString& nodeId, SceneTreeDockWidget* sceneDock,
                              PropertiesPanelWidget* properties, WorkbenchWindow& window);

private:
    /// 3D 场景文档
    std::shared_ptr<SceneDocument3D> m_scene;
    /// 默认相机控制器
    DefaultCameraController3D m_camera;
};

// ============================================================ 
using Workbench2DMain = Workbench2D;

