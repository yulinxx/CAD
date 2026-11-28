#pragma once

#include <QHash>
#include <QList>
#include <QMetaObject>
#include <QPointer>
#include <QString>

#include <functional>

class QAction;
class QActionGroup;
class QWidget;

namespace AA
{
    struct CommandView;
    struct ShellServices;

    // 命令动作池，菜单、profile 工具栏、面板按钮与快捷键复用
    class CommandActions
    {
    public:
        using Trigger = std::function<void(const QString& commandId)>;

        CommandActions(const ShellServices* services, Trigger trigger);
        ~CommandActions();

        CommandActions(const CommandActions&) = delete;
        CommandActions& operator=(const CommandActions&) = delete;

    public:
        void attach(QWidget* owner);

        // 获取命令
        QAction* action(const QString& commandId);

        // 菜单节点的 groupKey
        void addToGroup(QAction* action, const QString& groupKey);

        // 按当前语言刷新文字
        void refresh();

    private:
        const CommandView* viewOf(const QString& commandId) const;
        QString translate(const QString& key) const;
        void applyIcon(QAction* action, const QString& commandId) const;
        void applyText(QAction* action, const QString& commandId) const;
        void applyState(QAction* action, const QString& commandId);

    private:
        const ShellServices* m_shellServices = nullptr;
        Trigger m_trigger;

        QPointer<QWidget> m_owner;
        QHash<QString, QPointer<QAction>> m_actions;
        QHash<QString, QPointer<QActionGroup>> m_groups;
        QList<QMetaObject::Connection> m_connections;

        // 没有 ICommandState 提供勾选值时，由这里记住勾选态：shell 重建后新动作沿用。
        QHash<QString, bool> m_checked;
    };
}  // namespace AA
