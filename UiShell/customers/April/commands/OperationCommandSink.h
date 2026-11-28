#pragma once

#include <QJsonObject>
#include <QString>
#include <QVariantMap>

#include <functional>

class OperationBus;

namespace Apr
{
    // 轻量接口：Commands 只依赖它，独立测试无需链接 OperationBus/UiShell 实现。
    class ICommandSink
    {
    public:
        virtual ~ICommandSink() = default;
        virtual void dispatch(const QString& commandId, const QVariantMap& params) = 0;
        virtual bool isRegistered(const QString& commandId) const = 0;
    };

    // M6：替代旧 IUiCommandDispatcher 的客户侧命令出口。
    // 2D 命令直接进 OperationBus（与工具栏/右键/快捷键同一条路径）；
    // 外壳级 theme/language/help 命令交回 UiShell 运行时，不再经过 WorkbenchWindow。
    class OperationCommandSink final : public ICommandSink
    {
    public:
        using WorkbenchSwitch = std::function<void(const QString&)>;

        void setBus(OperationBus* bus) { m_bus = bus; }
        void setWorkbenchSwitch(WorkbenchSwitch fn) { m_workbenchSwitch = std::move(fn); }

        void dispatch(const QString& commandId, const QVariantMap& params = {}) override;
        bool isRegistered(const QString& commandId) const override;

    private:
        OperationBus* m_bus = nullptr;
        WorkbenchSwitch m_workbenchSwitch;
    };
}  // namespace Apr
