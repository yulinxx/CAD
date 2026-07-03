/**
 * @file Main/Src/UI/UiCommandDispatcher.h
 */
#pragma once

#include <QString>

class QAction;
class UiStateCenter;
class UiLayoutService;

/**
 * @file UiCommandDispatcher.h
 * @brief 命令分发器接口定义
 *
 * 定义了 UI 命令分发器接口，负责管理命令的执行生命周期。
 */

 /**
  * @class UiCommandDispatcher
  * @brief 命令分发器抽象接口
  *
  * 定义命令执行的标准流程：begin -> execute -> submit/cancel。
  * 支持将 QAction 绑定到命令 ID，实现 UI 动作与命令的解耦。
  */
class UiCommandDispatcher
{
public:
    virtual ~UiCommandDispatcher() = default;

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

    /// 获取当前活动命令 ID
    /// @return 当前命令标识符
    virtual QString activeCommandId() const = 0;
};

/**
 * @class DefaultUiCommandDispatcher
 * @brief 默认命令分发器实现
 *
 * 实现命令分发逻辑，管理命令生命周期并同步状态到状态中心。
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
    QString activeCommandId() const override;

private:
    /// 更新命令阶段状态
    /// @param phase 当前阶段（begin/submit/cancel/idle）
    void updatePhase(const QString& phase);

private:
    /// UI 状态中心
    UiStateCenter* m_stateCenter{ nullptr };
    /// 布局服务
    UiLayoutService* m_layoutService{ nullptr };
    /// 当前活动命令 ID
    QString m_activeCommandId;
};
