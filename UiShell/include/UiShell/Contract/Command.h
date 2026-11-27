#pragma once

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
