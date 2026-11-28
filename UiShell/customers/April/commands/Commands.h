#pragma once

#include <QByteArray>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QVariantMap>

#include "UiShell/Contract/Command.h"   // RD::ICommandCatalog / ICommandInvoker / ICommandState

#include "OperationCommandSink.h"       // M6：不再依赖旧 ClientConfig/IUiCommandDispatcher

namespace Apr
{
    // M6：把现有 CAD 命令体系（OperationBus）与 UiShell 外壳命令
    // 适配到 RD::ICommandCatalog + RD::ICommandInvoker + RD::ICommandState。
    // 不再经过旧 WorkbenchWindow / UiWorkbench / IUiCommandDispatcher。
    class Commands : public RD::ICommandCatalog,
                     public RD::ICommandInvoker,
                     public RD::ICommandState,
                     public RD::ICommandProfileRegistrar
    {
    public:
        Commands() = default;

        // ---------------- RD::ICommandCatalog ----------------
        const RD::CommandView* find(const QString& commandId) const override;

        // ---------------- RD::ICommandInvoker ----------------
        void invoke(const QString& commandId) override;
        void invokeWithArgs(const QString& commandId, const QJsonObject& args) override;

        // ---------------- RD::ICommandState ----------------
        void queryState(const QString& commandId, RD::CommandStatus& status) const override;

        // ---------------- 装配 ----------------
        void addCommand(const RD::CommandView& command);              ///< 登记命令元数据
        int addCommandsFromProfile(const QByteArray& profileJson);    ///< 从客户 profile 的 menus 递归登记命令
        int registerFromProfile(const QByteArray& profileJson) override;  ///< RD::ICommandProfileRegistrar
        void setSink(ICommandSink* sink);        ///< 客户侧命令出口
        ICommandSink* sink() const;

        const QList<RD::CommandView>& commands() const;

    private:
        void collectCommands(const QJsonArray& nodes);               ///< 递归收集 type=command 的节点

        QList<RD::CommandView> m_commands;
        ICommandSink* m_sink = nullptr;
    };
}  // namespace Apr
