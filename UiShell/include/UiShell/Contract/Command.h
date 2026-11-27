#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

namespace RD
{
    // 命令视图
    struct CommandView
    {
        QString strId;
        QString strTitleKey;        // 标题
        QString strIconKey;         // 图标 key
        QString strToolTipKey;      // 提示文字 key
        QString strShortcut;        // 快捷键
        bool bCheckable = false;    // 开关类命令
        QStringList listRequiredFeatures;  // 所需功能开关
    };

    // 命令运行期状态，由 shell 查询。
    struct CommandStatus
    {
        bool bEnabled = true;   // 与功能开关叠加
        bool bChecked = false;  // 仅 bCheckable 命令有效
    };

    class ICommandState
    {
    public:
        virtual ~ICommandState() = default;

    public:
        virtual void queryState(const QString& commandId, CommandStatus& status) const = 0;
    };

    class ICommandCatalog
    {
    public:
        virtual ~ICommandCatalog() = default;

    public:
        virtual const CommandView* find(const QString& commandId) const = 0;
    };

    // 可选：客户命令目录可从实际加载的 profile 补充登记命令。
    // 站点用 --profile 换成外置 ui.json 时，core 在规划前调用它，
    // 避免新菜单里的命令被 AssemblyPlanner 当作“未知命令”剔除。
    // core 只依赖本接口，不认识任何客户类型；不实现即保持原有目录。
    class ICommandProfileRegistrar
    {
    public:
        virtual ~ICommandProfileRegistrar() = default;

        // 返回本次新增的命令数；重复登记必须幂等。
        virtual int registerFromProfile(const QByteArray& profileJson) = 0;
    };

    class ICommandInvoker
    {
    public:
        virtual ~ICommandInvoker() = default;

        virtual void invoke(const QString& commandId) = 0;

        virtual void invokeWithArgs(const QString& commandId, const QJsonObject& args)
        {
            Q_UNUSED(args);
            invoke(commandId);
        }
    };
}  // namespace RD
