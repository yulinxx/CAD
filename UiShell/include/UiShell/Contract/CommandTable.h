#pragma once

#include <QByteArray>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QString>
#include <QStringList>

#include <functional>
#include <utility>

#include "UiShell/Contract/Command.h"

namespace RD
{
    // 命令表
    class CommandTable : public ICommandCatalog,
                         public ICommandInvoker,
                         public ICommandState,
                         public ICommandProfileRegistrar
    {
    public:
        using Handler = std::function<void(const QJsonObject& args)>;
        using Predicate = std::function<bool()>;

        CommandTable() = default;
        CommandTable(const CommandTable&) = delete;
        CommandTable& operator=(const CommandTable&) = delete;

    public:
        // 登记命令
        CommandTable& add(const QString& id, Handler handler = {})
        {
            Entry entry;
            entry.view.strId = id;
            entry.view.strTitleKey = QStringLiteral("command.") + id;
            entry.handler = std::move(handler);

            const auto it = m_index.constFind(id);
            if (it != m_index.constEnd())
            {
                m_last = it.value();
                m_entries[m_last] = entry;
            }
            else
            {
                m_last = m_entries.size();
                m_index.insert(id, m_last);
                m_entries.append(entry);
            }
            return *this;
        }

        CommandTable& title(const QString& titleKey)
        {
            if (Entry* entry = last())
                entry->view.strTitleKey = titleKey;
            return *this;
        }

        CommandTable& icon(const QString& iconKey)
        {
            if (Entry* entry = last())
                entry->view.strIconKey = iconKey;
            return *this;
        }

        CommandTable& toolTip(const QString& toolTipKey)
        {
            if (Entry* entry = last())
                entry->view.strToolTipKey = toolTipKey;
            return *this;
        }

        CommandTable& shortcut(const QString& keys)
        {
            if (Entry* entry = last())
                entry->view.strShortcut = keys;
            return *this;
        }

        CommandTable& needsFeatures(const QStringList& features)
        {
            if (Entry* entry = last())
                entry->view.listRequiredFeatures = features;
            return *this;
        }

        // shell 每次执行命令后重新查询，状态变化时由面板调用 IShellHost::refreshCommands()。
        CommandTable& enabledWhen(Predicate enabled)
        {
            if (Entry* entry = last())
                entry->enabled = std::move(enabled);
            return *this;
        }

        // 标记为 checkable，勾选值取自 checked。
        CommandTable& checkedWhen(Predicate checked)
        {
            if (Entry* entry = last())
            {
                entry->view.bCheckable = true;
                entry->checked = std::move(checked);
            }
            return *this;
        }

        // ICommandCatalog
        const CommandView* find(const QString& commandId) const override
        {
            const Entry* entry = lookup(commandId);
            return entry ? &entry->view : nullptr;
        }

        // 调用执行
        void invoke(const QString& commandId) override
        {
            invokeWithArgs(commandId, QJsonObject());
        }

        void invokeWithArgs(const QString& commandId, const QJsonObject& args) override
        {
            const Entry* entry = lookup(commandId);
            if (entry && entry->handler)
                entry->handler(args);
        }

        // 查询状态
        void queryState(const QString& commandId, CommandStatus& status) const override
        {
            const Entry* entry = lookup(commandId);
            if (!entry)
                return;

            if (entry->enabled)
                status.bEnabled = entry->enabled();

            if (entry->checked)
                status.bChecked = entry->checked();
        }

        // ICommandProfileRegistrar：从实际加载的 profile 补充登记菜单命令。
        // 站点外置 ui.json 引用了客户代码未预登记的命令时，避免被装配器过滤。
        int registerFromProfile(const QByteArray& profileJson) override
        {
            const QJsonDocument doc = QJsonDocument::fromJson(profileJson);
            if (!doc.isObject())
                return 0;
            const QJsonValue menus = doc.object().value(QStringLiteral("menus"));
            if (!menus.isArray())
                return 0;

            const int before = m_entries.size();
            registerProfileNodes(menus.toArray());
            return m_entries.size() - before;
        }

    private:
        void registerProfileNodes(const QJsonArray& nodes)
        {
            for (const QJsonValue& value : nodes)
            {
                if (!value.isObject())
                    continue;
                const QJsonObject obj = value.toObject();

                const QString commandId = obj.value(QStringLiteral("commandId")).toString();
                if (obj.value(QStringLiteral("type")).toString() == QStringLiteral("command")
                    && !commandId.isEmpty() && !lookup(commandId))
                {
                    add(commandId);
                    const QString titleKey = obj.value(QStringLiteral("titleKey")).toString();
                    const QString iconKey = obj.value(QStringLiteral("iconKey")).toString();
                    if (!titleKey.isEmpty())
                        title(titleKey);
                    if (!iconKey.isEmpty())
                        icon(iconKey);
                }

                const QJsonValue children = obj.value(QStringLiteral("children"));
                if (children.isArray())
                    registerProfileNodes(children.toArray());
            }
        }

        struct Entry
        {
            CommandView view;
            Handler handler;
            Predicate enabled;
            Predicate checked;
        };

        Entry* last()
        {
            return (m_last >= 0 && m_last < m_entries.size()) ? &m_entries[m_last] : nullptr;
        }

        const Entry* lookup(const QString& commandId) const
        {
            const auto it = m_index.constFind(commandId);
            return it == m_index.constEnd() ? nullptr : &m_entries.at(it.value());
        }

    private:
        QList<Entry> m_entries;
        QHash<QString, int> m_index;
        int m_last = -1;
    };
}  // namespace RD
