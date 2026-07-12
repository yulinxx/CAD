#include "CommandHandlerAdapter.h"

#include "Log/SyLogger.h"

namespace
{
    /// 将 QString 命令 ID 映射为 OperationId（与 UiCommandDispatcher 中的映射保持一致）
    OperationId mapCommandIdToOperation(const QString& cmd)
    {
        static const struct
        {
            const char* key; OperationId id;
        } map[] = {
            { "2d.select",          OperationId::Tool_Select },
            { "2d.draw_line",       OperationId::Tool_Line },
            { "2d.draw_circle",     OperationId::Tool_Circle },
            { "2d.draw_polyline",   OperationId::Tool_Polyline },
            { "2d.draw_arc",        OperationId::Tool_Arc },
            { "2d.draw_polygon",    OperationId::Tool_Polygon },
            { "2d.draw_bezier2",    OperationId::Tool_Bezier2 },
            { "2d.draw_bezier",     OperationId::Tool_Bezier },
            { "2d.draw_nurbs",      OperationId::Tool_Nurbs },
            { "2d.draw_smartline",  OperationId::Tool_SmartLine },
            { "2d.move",            OperationId::Edit_Move },
            { "2d.rotate",          OperationId::Edit_Rotate },
            { "2d.copy",            OperationId::Edit_Copy },
            { "2d.mirror",          OperationId::Edit_Mirror },
            { "2d.delete",          OperationId::Edit_Delete },
            { "edit.undo",          OperationId::Edit_Undo },
            { "edit.redo",          OperationId::Edit_Redo },
            { "edit.delete",        OperationId::Edit_Delete },
            { "edit.select_all",    OperationId::Edit_SelectAll },
            { "view.zoom_fit",      OperationId::View_ZoomFit },
            { "view.zoom_in",       OperationId::View_ZoomIn },
            { "view.zoom_out",      OperationId::View_ZoomOut },
        };
        for (const auto& entry : map)
        {
            if (cmd == QLatin1String(entry.key))
                return entry.id;
        }
        return OperationId::None;
    }
} // anonymous namespace

CommandHandlerAdapter::CommandHandlerAdapter(ICommandHandler* handler, const UiServices& services)
    : m_handler(handler)
    , m_services(services)
{
}

OperationId CommandHandlerAdapter::id() const
{
    OperationId opId = mapCommandIdToOperation(m_handler->commandId());
    return opId != OperationId::None ? opId : OperationId::None;
}

bool CommandHandlerAdapter::canExecute(const OperationContext& ctx) const
{
    (void)ctx;
    // 旧命令的可用性由 handler 自身逻辑决定；仅在非空时允许执行
    return m_handler != nullptr;
}

OperationResult CommandHandlerAdapter::execute(OperationContext& ctx, const OperationRequest& req)
{
    (void)ctx;
    (void)req;
    OperationResult result;

    if (!m_handler)
    {
        result.message = QStringLiteral("Handler is null");
        SY_ERRORF("[CommandHandlerAdapter] execute failed: handler is null");
        return result;
    }

    SY_INFOF("[CommandHandlerAdapter] executing op=%s via handler=%s",
        Cmd::operationIdToString(id()),
        m_handler->commandId().toUtf8().constData());

    // 重置 handler 状态
    m_handler->reset();

    // 激活 handler
    if (!m_handler->activate(m_services))
    {
        result.message = QStringLiteral("Handler activate failed: %1")
            .arg(m_handler->commandId());
        SY_ERRORF("[CommandHandlerAdapter] activate failed: %s",
            m_handler->commandId().toUtf8().constData());
        return result;
    }

    // 交互式命令：通过旧分发器保持激活（等待后续事件）
    if (m_handler->isInteractive())
    {
        result.success = true;
        result.message = QStringLiteral("Interactive command activated via adapter: %1")
            .arg(m_handler->commandId());
        SY_INFOF("[CommandHandlerAdapter] interactive command activated: %s",
            m_handler->commandId().toUtf8().constData());
        return result;
    }

    // 非交互式命令：直接提交
    m_handler->commit();
    result.success = true;
    result.flags |= Cmd::OpFlagUndoable;

    if (m_handler->createUndoCommand())
    {
        result.flags |= Cmd::OpFlagUndoable;
    }

    SY_INFOF("[CommandHandlerAdapter] non-interactive command completed: %s",
        m_handler->commandId().toUtf8().constData());
    return result;
}

bool CommandHandlerAdapter::isUndoable() const
{
    return m_handler && m_handler->createUndoCommand() != nullptr;
}

bool CommandHandlerAdapter::mutatesScene() const
{
    // 所有旧命令都会修改场景（含选择命令）
    return true;
}

OperationId CommandHandlerAdapter::mapCommandId(const QString& cmd)
{
    return mapCommandIdToOperation(cmd);
}
