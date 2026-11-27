#include "Commands.h"

#include <QJsonObject>

namespace Apr
{
    // ---------------- RD::ICommandCatalog ----------------
    const RD::CommandView* Commands::find(const QString& commandId) const
    {
        for (const auto& c : m_commands)
        {
            if (c.strId == commandId)
                return &c;
        }
        return nullptr;
    }

    // ---------------- RD::ICommandInvoker ----------------
    void Commands::invoke(const QString& commandId)
    {
        if (m_dispatcher)
            m_dispatcher->dispatch(commandId, QVariantMap{});   // 现有命令分发唯一入口
    }

    void Commands::invokeWithArgs(const QString& commandId, const QJsonObject& args)
    {
        QVariantMap params;
        if (!args.isEmpty())
            params = args.toVariantMap();                        // QJsonObject -> QVariantMap 透传
        if (m_dispatcher)
            m_dispatcher->dispatch(commandId, params);           // 实现负责透传/补齐，不整体覆盖
    }

    // ---------------- RD::ICommandState ----------------
    void Commands::queryState(const QString& commandId, RD::CommandStatus& status) const
    {
        status.bEnabled = m_dispatcher ? m_dispatcher->isCommandRegistered(commandId) : false;
        status.bChecked = false;
        // 现有 `UiWorkbench::isCommandRegistered` / `commandText` 可在此进一步补齐 checked 状态。
    }

    // ---------------- 装配 ----------------
    void Commands::addCommand(const RD::CommandView& command)
    {
        m_commands.append(command);
    }

    void Commands::setDispatcher(IUiCommandDispatcher* dispatcher)
    {
        m_dispatcher = dispatcher;
    }

    IUiCommandDispatcher* Commands::dispatcher() const
    {
        return m_dispatcher;
    }

    const QList<RD::CommandView>& Commands::commands() const
    {
        return m_commands;
    }
}  // namespace Apr
