#pragma once

#include <QString>
#include <functional>
#include <memory>

#include "UiServices.h"
#include "UiFrameworkServices.h"

class UiStateCenter;
class UiLayoutService;
class QMouseEvent;
class QKeyEvent;

enum class InteractionEventType
{
    MouseDown,
    MouseMove,
    MouseUp,
    KeyPress
};

struct InteractionEvent
{
    InteractionEventType type{ InteractionEventType::MouseMove };
    int x{ -1 };
    int y{ -1 };
    int key{ -1 };
};

using InteractionEventHandler = std::function<bool(const InteractionEvent&)>;

/**
 * @brief 交互式命令生命周期分发器
 *
 * 处理需要用户交互的命令（绘图、选择、编辑等）的鼠标转发和生命周期管理。
 * 视口层仅依赖此交互接口，实现与具体命令调度器的解耦。
 *
 * 统一生命周期：
 *   begin(commandId) → 标记命令开始并同步状态
 *   dispatchEvent(event) → 接收统一的鼠标/键盘交互事件
 *   submit() → 收尾并同步状态
 *   cancel() → 取消并回退状态
 */
class IInteractionDispatcher
{
public:
    virtual ~IInteractionDispatcher() = default;

    /// 开始命令（标记忙碌状态）
    virtual void begin(const QString& commandId) = 0;

    /// 提交命令（完成 + undo 压栈）
    virtual void submit() = 0;

    /// 取消命令（不进 undo 栈）
    virtual void cancel() = 0;

    /// 获取当前活动命令 ID
    virtual QString activeCommandId() const = 0;

    /// 判断是否有活动命令
    virtual bool hasActiveCommand() const = 0;

    /// 设置活动命令的事件消费回调
    virtual void setEventHandler(InteractionEventHandler handler) = 0;

    /// 接收统一交互事件；返回 true 表示事件已被消费
    virtual bool dispatchEvent(const InteractionEvent& event) = 0;

    /// 兼容旧调用方的鼠标按下入口
    bool forwardMouseDown(int x, int y)
    {
        return dispatchEvent({ InteractionEventType::MouseDown, x, y, -1 });
    }

    /// 兼容旧调用方的鼠标移动入口
    bool forwardMouseMove(int x, int y)
    {
        return dispatchEvent({ InteractionEventType::MouseMove, x, y, -1 });
    }

    /// 兼容旧调用方的鼠标释放入口
    bool forwardMouseUp(int x, int y)
    {
        return dispatchEvent({ InteractionEventType::MouseUp, x, y, -1 });
    }

    /// 兼容旧调用方的键盘入口
    bool forwardKeyPress(int key)
    {
        return dispatchEvent({ InteractionEventType::KeyPress, -1, -1, key });
    }

    /// 设置状态中心
    virtual void setStateCenter(UiStateCenter* stateCenter) = 0;

    /// 设置 UI 服务集合
    virtual void setUiServices(const UiServices& services) = 0;

    /// 设置布局服务
    virtual void setLayoutService(UiLayoutService* layoutService) = 0;

    /// 设置框架级服务桥接
    virtual void setFrameworkServices(const UiFrameworkServices& services) = 0;

    /// 设置工具切换回调
    virtual void setToolChangedCallback(std::function<void(const QString&)> callback) = 0;

    /// 更新命令类型
    virtual void setCommandType(const QString& commandType) = 0;
};

/**
 * @brief 默认交互式命令生命周期分发器实现
 */
class DefaultInteractionDispatcher final : public IInteractionDispatcher
{
public:
    DefaultInteractionDispatcher();
    ~DefaultInteractionDispatcher() override;

    void begin(const QString& commandId) override;
    void submit() override;
    void cancel() override;
    QString activeCommandId() const override;
    bool hasActiveCommand() const override;
    void setEventHandler(InteractionEventHandler handler) override;
    bool dispatchEvent(const InteractionEvent& event) override;
    void setStateCenter(UiStateCenter* stateCenter) override;
    void setUiServices(const UiServices& services) override;
    void setLayoutService(UiLayoutService* layoutService) override;
    void setFrameworkServices(const UiFrameworkServices& services) override;
    void setToolChangedCallback(std::function<void(const QString&)> callback) override;
    void setCommandType(const QString& commandType) override;

private:
    QString resolveCommandType(const QString& commandId) const;
    void syncCommandFinishState();

private:
    UiStateCenter* m_stateCenter{ nullptr };
    UiLayoutService* m_layoutService{ nullptr };
    UiServices m_uiServices;
    UiFrameworkServices m_frameworkServices;
    QString m_activeCommandId;
    QString m_commandType;
    InteractionEventHandler m_eventHandler;
    std::function<void(const QString&)> m_toolChangedCallback;
};
