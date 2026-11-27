#pragma once

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QVariantMap>

#include "UiShell/Contract/Command.h"   // RD::ICommandCatalog / ICommandInvoker / ICommandState

// ---- AprilUi（Main/Src/UI，静态库 AprilUi，M1）----
#include "UI/ClientConfig/IUiCommandDispatcher.h"   // IUiCommandDispatcher（UiWorkbench 实现）

namespace Apr
{
    // M4：把现有 CAD 命令体系（IUiCommandDispatcher / OperationBus / UiWorkbench）
    // 适配到 UiShell 的 RD::ICommandCatalog + RD::ICommandInvoker + RD::ICommandState。
    //
    // 不重写命令系统；复用 AprilUi 的 `UiWorkbench::dispatch` / `isCommandRegistered`。
    // customers/April 持有本对象，经 CustomerModule 提供给 UiShell，并可注入到 AprilWorkbench。
    class Commands : public RD::ICommandCatalog,
                     public RD::ICommandInvoker,
                     public RD::ICommandState
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
        void setDispatcher(IUiCommandDispatcher* dispatcher);        ///< 现有工作台/命令分发器
        IUiCommandDispatcher* dispatcher() const;

        const QList<RD::CommandView>& commands() const;

    private:
        QList<RD::CommandView> m_commands;
        IUiCommandDispatcher* m_dispatcher = nullptr;
    };
}  // namespace Apr
