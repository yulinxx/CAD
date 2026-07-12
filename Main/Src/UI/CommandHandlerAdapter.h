#pragma once

#include <QObject>
#include <QString>
#include <memory>

#include "UI2D/Operation/IOperation.h"
#include "UI2D/Operation/OperationBus.h"
#include "UI/UiCommandHandler.h"
#include "UiServices.h"

class ICommandHandler;

/**
 * @class CommandHandlerAdapter
 * @brief 将旧式 ICommandHandler 包装为 IOperation，使其可通过 OperationBus 调用
 *
 * 迁移适配器模式：在不修改旧命令实现的前提下，将其注册到 OperationBus 上。
 * 旧命令仍通过 UiCommandDispatcher 保持向后兼容，新代码统一走 OperationBus。
 *
 * 使用方式：
 *   auto adapter = std::make_unique<CommandHandlerAdapter>(handler, services);
 *   operationBus->registerOperation(std::move(adapter));
 */
class CommandHandlerAdapter : public IOperation
{
    Q_OBJECT

public:
    CommandHandlerAdapter(ICommandHandler* handler, const UiServices& services);
    ~CommandHandlerAdapter() override = default;

    OperationId id() const override;
    bool canExecute(const OperationContext& ctx) const override;
    OperationResult execute(OperationContext& ctx, const OperationRequest& req) override;
    bool isUndoable() const override;
    bool mutatesScene() const override;

    /// 获取被包装的旧命令处理器
    ICommandHandler* wrappedHandler() const { return m_handler; }

    /// 将 handler 的 commandId() 映射为 OperationId（外部注册时使用）
    static OperationId mapCommandId(const QString& cmd);

    ICommandHandler* m_handler;
    UiServices m_services;
};
