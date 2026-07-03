/**
 * @file Main/Src/UI/UiWorkbench.h
 */
#pragma once

#include <QString>

#include <memory>

#include "UiEntities.h"
#include "UiServices.h"

class WorkbenchWindow;

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

private:
    /// UI 服务引用
    const UiServices* m_services{ nullptr };
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
