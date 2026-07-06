#pragma once

#include <QString>
#include <memory>
#include <map>
#include <functional>

#include "UiFrameworkServices.h"
#include "UiCommandHandler.h"
#include "UiServices.h"

class QAction;
class UiStateCenter;
class UiLayoutService;

/**
 * @file UiCommandDispatcher.h
 * @brief 命令分发器接口定义
 *
 * 定义了 UI 命令分发器接口，负责管理命令的执行生命周期。
 * 支持命令处理器注册、交互式命令、撤销/重做等功能。
 *
 * 统一命令生命周期协议（P0-1）：
 *   execute(commandId)
 *     → handlerFor(commandId)          // 按 ID 查找 handler，不依赖 currentHandler
 *     → handler->reset()               // 重置到 Idle 状态
 *     → begin(commandId)               // 同步状态中心，标记 busy
 *     → handler->activate(services)    // 激活命令和工具
 *     → [事件循环]                      // 交互式命令等待用户输入，非交互式直接提交
 *     → isComplete() → true            // 自动检测完成条件
 *     → submit()                       // commit + undo 压栈 + 清理
 *   或
 *     → cancel()                       // 取消 + 清理（不进栈）
 *
 * 接口契约（P0-10）：
 *   execute()     → 创建/准备命令上下文，不直接做最终提交，不负责UI细节
 *   handlerFor()  → 负责把输入路由到正确handler，不做业务判断
 *   begin()       → 初始化命令状态，同步状态中心，标记busy
 *   submit()      → 做最终校验和落库，只在成功时允许commit，统一处理undo压栈
 *   cancel()      → 取消命令，不进栈，清理状态中心
 *   forward*()    → 转发事件到当前handler，事件后自动检测isComplete并提交
 */

/**
 * @class UiCommandDispatcher
 * @brief 命令分发器抽象接口
 *
 * 定义命令执行的标准流程：begin -> execute -> submit/cancel。
 * 支持将 QAction 绑定到命令 ID，实现 UI 动作与命令的解耦。
 * 命令执行过程中会自动同步状态到状态中心，确保命令状态与 UI 状态一致。
 * 
 * 支持命令处理器模式：每个命令 ID 可以注册一个 ICommandHandler，
 * 交互式命令在激活后进入等待状态，接收用户输入后提交或取消。
 */
class UiCommandDispatcher
{
public:
    virtual ~UiCommandDispatcher() = default;

public:
    /// 将 QAction 绑定到命令 ID
    /// @param action 动作对象
    /// @param commandId 命令标识符
    virtual void bindAction(QAction* action, const QString& commandId) = 0;

    /// 执行命令
    /// @param commandId 命令标识符
    virtual void execute(const QString& commandId) = 0;

    /// 提交命令（完成）
    virtual void submit() = 0;

    /// 取消命令
    virtual void cancel() = 0;

    /// 开始命令（标记忙碌状态）
    /// @param commandId 命令标识符
    virtual void begin(const QString& commandId) = 0;

    /// 设置状态中心
    /// @param stateCenter UI 状态中心
    virtual void setStateCenter(UiStateCenter* stateCenter) = 0;

    /// 设置布局服务
    /// @param layoutService 布局服务
    virtual void setLayoutService(UiLayoutService* layoutService) = 0;

    /// 设置框架级服务桥接
    /// @param services 框架级服务集合
    virtual void setFrameworkServices(const UiFrameworkServices& services) = 0;

    /// 获取当前活动命令 ID
    /// @return 当前命令标识符
    virtual QString activeCommandId() const = 0;

    /// 更新命令类型
    /// @param commandType 命令类型（如 "2d", "3d", "view" 等）
    virtual void setCommandType(const QString& commandType) = 0;

    /// 注册命令处理器
    /// @param handler 命令处理器
    virtual void registerHandler(ICommandHandler* handler) = 0;

    /// 撤销操作
    /// @return 是否成功撤销
    virtual bool undo() = 0;

    /// 重做操作
    /// @return 是否成功重做
    virtual bool redo() = 0;

    /// 设置撤销栈
    /// @param undoStack 撤销栈
    virtual void setUndoStack(IUndoStack* undoStack) = 0;

    /// 设置 UI 服务集合
    /// @param services UI 服务集合
    virtual void setUiServices(const UiServices& services) = 0;

    /// 设置工具切换回调（用于同步工具栏状态）
    /// @param callback 回调函数，参数为当前活动命令 ID
    virtual void setToolChangedCallback(std::function<void(const QString&)> callback) = 0;

    /// 转发鼠标按下事件给当前命令处理器
    /// @param x 鼠标 X 坐标
    /// @param y 鼠标 Y 坐标
    /// @return 是否处理了事件
    virtual bool forwardMouseDown(int x, int y) = 0;

    /// 转发鼠标移动事件给当前命令处理器
    /// @param x 鼠标 X 坐标
    /// @param y 鼠标 Y 坐标
    /// @return 是否处理了事件
    virtual bool forwardMouseMove(int x, int y) = 0;

    /// 转发鼠标释放事件给当前命令处理器
    /// @param x 鼠标 X 坐标
    /// @param y 鼠标 Y 坐标
    /// @return 是否处理了事件
    virtual bool forwardMouseUp(int x, int y) = 0;

    /// 转发键盘按键事件给当前命令处理器
    /// @param key 按键代码
    /// @return 是否处理了事件
    virtual bool forwardKeyPress(int key) = 0;

    /// 判断是否有活动命令
    /// @return true 表示有活动命令
    virtual bool hasActiveCommand() const = 0;

    /// 获取当前活动的命令处理器
    /// @return 当前命令处理器，如果没有活动命令则返回 nullptr
    virtual ICommandHandler* currentHandler() const = 0;

    /// 根据命令 ID 获取命令处理器
    /// @param commandId 命令标识符
    /// @return 命令处理器，如果未注册则返回 nullptr
    virtual ICommandHandler* handlerFor(const QString& commandId) const = 0;
};

/**
 * @class DefaultUiCommandDispatcher
 * @brief 默认命令分发器实现
 *
 * 实现命令分发逻辑，管理命令生命周期并同步状态到状态中心。
 * 命令执行时自动更新状态中心的命令相关字段，确保 UI 状态与命令状态一致。
 * 
 * 支持命令处理器模式和撤销/重做功能。
 */
class DefaultUiCommandDispatcher final : public UiCommandDispatcher
{
public:
    void bindAction(QAction* action, const QString& commandId) override;
    void execute(const QString& commandId) override;
    void submit() override;
    void cancel() override;
    void begin(const QString& commandId) override;
    void setStateCenter(UiStateCenter* stateCenter) override;
    void setLayoutService(UiLayoutService* layoutService) override;
    void setFrameworkServices(const UiFrameworkServices& services) override;
    QString activeCommandId() const override;
    void setCommandType(const QString& commandType) override;
    void registerHandler(ICommandHandler* handler) override;
    bool undo() override;
    bool redo() override;
    void setUndoStack(IUndoStack* undoStack) override;
    void setUiServices(const UiServices& services) override;
    void setToolChangedCallback(std::function<void(const QString&)> callback) override;
    bool forwardMouseDown(int x, int y) override;
    bool forwardMouseMove(int x, int y) override;
    bool forwardMouseUp(int x, int y) override;
    bool forwardKeyPress(int key) override;
    bool hasActiveCommand() const override;
    ICommandHandler* currentHandler() const override;
    ICommandHandler* handlerFor(const QString& commandId) const override;

private:
    /// 更新命令阶段状态
    /// @param phase 当前阶段（begin/submit/cancel/idle）
    void updatePhase(const QString& phase);

    /// 从命令 ID 解析命令类型
    /// @param commandId 命令标识符
    /// @return 命令类型
    QString resolveCommandType(const QString& commandId) const;

private:
    /// UI 状态中心
    UiStateCenter* m_stateCenter{ nullptr };
    /// 布局服务
    UiLayoutService* m_layoutService{ nullptr };
    /// 框架级服务桥接
    UiFrameworkServices m_frameworkServices;
    /// UI 服务集合
    UiServices m_uiServices;
    /// 当前活动命令 ID
    QString m_activeCommandId;
    /// 当前命令类型
    QString m_commandType{ QStringLiteral("none") };
    /// 撤销栈
    IUndoStack* m_undoStack{ nullptr };
    /// 命令处理器注册表
    std::map<QString, ICommandHandler*> m_handlers;
    /// 工具切换回调
    std::function<void(const QString&)> m_toolChangedCallback;
};
