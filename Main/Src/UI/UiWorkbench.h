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

/**
 * @file UiWorkbench.h
 * @brief 工作台接口定义
 *
 * 定义了 UI 工作台接口及其实现类，包括 2D 和 3D 工作台。
 */

 /**
  * @class UiWorkbench
  * @brief 工作台抽象接口
  *
  * 定义工作台的生命周期管理：初始化、附加到窗口、激活、停用、关闭。
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
    virtual void activate() = 0;

    /// 停用工作台
    virtual void deactivate() = 0;

    /// 关闭工作台
    virtual void shutdown() = 0;
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
    /// UI 服务引用
    const UiServices* m_services{ nullptr };
    /// 是否使用旧版 CanvasViewport2D 作为临时回退
    bool m_useLegacyCanvasViewport{ false };
    /// 2D 实体文档
    std::shared_ptr<EntityDocument2D> m_document;
};

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
    /// UI 服务引用
    const UiServices* m_services{ nullptr };
    /// 3D 场景文档
    std::shared_ptr<SceneDocument3D> m_scene;
    /// 默认相机控制器
    DefaultCameraController3D m_camera;
};

using Workbench2DMain = Workbench2D;

