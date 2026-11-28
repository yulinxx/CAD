#include "Commands.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

namespace Apr
{
    // ---------------- AA::ICommandCatalog ----------------
    const AA::CommandView* Commands::find(const QString& commandId) const
    {
        for (const auto& c : m_commands)
        {
            if (c.strId == commandId)
                return &c;
        }
        return nullptr;
    }

    // ---------------- AA::ICommandInvoker ----------------
    void Commands::invoke(const QString& commandId)
    {
        if (m_sink)
            m_sink->dispatch(commandId, QVariantMap{});   // 现有命令分发唯一入口
    }

    void Commands::invokeWithArgs(const QString& commandId, const QJsonObject& args)
    {
        QVariantMap params;
        if (!args.isEmpty())
            params = args.toVariantMap();                        // QJsonObject -> QVariantMap 透传
        if (m_sink)
            m_sink->dispatch(commandId, params);                // 实现负责透传/补齐，不整体覆盖
    }

    // ---------------- AA::ICommandState ----------------
    void Commands::queryState(const QString& commandId, AA::CommandStatus& status) const
    {
        status.bEnabled = m_sink ? m_sink->isRegistered(commandId) : false;
        status.bChecked = false;
    }

    // ---------------- 装配 ----------------
    void Commands::addCommand(const AA::CommandView& command)
    {
        m_commands.append(command);
    }

    // M4：ui.json 的 menus 是菜单/命令的唯一事实源。把里面的 command 节点登记进目录，
    // AssemblyPlanner 才不会把它们当“未知命令”过滤掉。菜单标题/图标也取自 profile，
    // 不在这里重复维护一份 176 项的命令表。
    int Commands::addCommandsFromProfile(const QByteArray& profileJson)
    {
        const QJsonDocument doc = QJsonDocument::fromJson(profileJson);
        if (!doc.isObject())
            return 0;
        const QJsonValue menus = doc.object().value(QStringLiteral("menus"));
        if (!menus.isArray())
            return 0;

        const int before = m_commands.size();
        collectCommands(menus.toArray());
        return m_commands.size() - before;
    }

    int Commands::registerFromProfile(const QByteArray& profileJson)
    {
        return addCommandsFromProfile(profileJson);
    }

    void Commands::collectCommands(const QJsonArray& nodes)
    {
        for (const QJsonValue& value : nodes)
        {
            if (!value.isObject())
                continue;
            const QJsonObject obj = value.toObject();

            const QString id = obj.value(QStringLiteral("commandId")).toString();
            if (obj.value(QStringLiteral("type")).toString() == QStringLiteral("command")
                && !id.isEmpty() && !find(id))
            {
                AA::CommandView view;
                view.strId = id;
                view.strTitleKey = obj.value(QStringLiteral("titleKey")).toString();
                view.strIconKey = obj.value(QStringLiteral("iconKey")).toString();
                view.bCheckable = obj.value(QStringLiteral("checkable")).toBool(false);
                m_commands.append(view);
            }

            const QJsonValue children = obj.value(QStringLiteral("children"));
            if (children.isArray())
                collectCommands(children.toArray());
        }
    }

    void Commands::setSink(ICommandSink* sink)
    {
        m_sink = sink;
    }

    ICommandSink* Commands::sink() const
    {
        return m_sink;
    }

    const QList<AA::CommandView>& Commands::commands() const
    {
        return m_commands;
    }
}  // namespace Apr
