#pragma once

#include <QString>
#include <functional>

class ICommandHandler;
class UiStateCenter;
class UiLayoutService;
struct UiServices;
struct UiFrameworkServices;

/**
 * @brief 交互式命令生命周期分发器
 *
 * 处理需要用户交互的命令（绘图、选择、编辑等）的鼠标转发和生命周期管理。
 * 从 UiCommandDispatcher 拆分而来，使视口层仅依赖交互接口而非完整调度器。
 *
 * 统一生命周期：
 *   begin(commandId) → handler->activate(services)
 *   forwardMouseDown/Move/Up/KeyPress → handler->onMouse*()
 *   submit() → handler->commit() + undo push
 *   cancel() → handler->cancel() + cleanup
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

    /// 获取当前活动的命令处理器
    virtual ICommandHandler* currentHandler() const = 0;

    /// 根据命令 ID 获取命令处理器
    virtual ICommandHandler* handlerFor(const QString& commandId) const = 0;

    /// 转发鼠标按下事件
    virtual bool forwardMouseDown(int x, int y) = 0;

    /// 转发鼠标移动事件
    virtual bool forwardMouseMove(int x, int y) = 0;

    /// 转发鼠标释放事件
    virtual bool forwardMouseUp(int x, int y) = 0;

    /// 转发键盘按键事件
    virtual bool forwardKeyPress(int key) = 0;

    /// 注册命令处理器
    virtual void registerHandler(ICommandHandler* handler) = 0;

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
