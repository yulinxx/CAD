#include "Assembly/Commands/CommandActions.h"

#include <QAction>
#include <QActionGroup>
#include <QIcon>
#include <QKeySequence>
#include <QWidget>

#include <utility>

#include "Assembly/Host/ThemedIcon.h"
#include "UiShell/Assembly/ShellServices.h"

namespace AA
{
    CommandActions::CommandActions(const ShellServices* services, Trigger trigger)
        : m_shellServices(services)
        , m_trigger(std::move(trigger))
    {
    }

    CommandActions::~CommandActions()
    {
        for (const QMetaObject::Connection& connection : m_connections)
            QObject::disconnect(connection);
    }

    void CommandActions::attach(QWidget* owner)
    {
        for (const QMetaObject::Connection& connection : m_connections)
            QObject::disconnect(connection);
        m_connections.clear();

        m_owner = owner;
        m_actions.clear();
        m_groups.clear();
    }

    const CommandView* CommandActions::viewOf(const QString& commandId) const
    {
        const ICommandCatalog* catalog = m_shellServices ? m_shellServices->cmdCatalog : nullptr;
        return catalog ? catalog->find(commandId) : nullptr;
    }

    QString CommandActions::translate(const QString& key) const
    {
        if (key.isEmpty())
            return QString();
        const ITextProvider* text = m_shellServices ? m_shellServices->text : nullptr;
        return text ? text->text(key) : key;
    }

    QAction* CommandActions::action(const QString& commandId)
    {
        if (commandId.isEmpty() || !m_owner)
            return nullptr;

        const QPointer<QAction> existing = m_actions.value(commandId);
        if (existing)
            return existing.data();

        // 有命令目录时只认目录里的命令，与菜单规划的过滤规则一致；没有目录时（纯 UI 场景）照常创建
        const CommandView* view = viewOf(commandId);
        const bool hasCatalog = m_shellServices && m_shellServices->cmdCatalog;
        if (hasCatalog && !view)
            return nullptr;

        auto* created = new QAction(m_owner);
        created->setObjectName(QStringLiteral("msui.action.") + commandId);
        // 标记“来自 profile 命令目录”：MenuController 只给内置语言/皮肤动作做直连，
        // 避免同一动作既走命令分发又被内置绑定处理两次（语言切换会执行两遍）。
        created->setProperty("msui.profileCommand", true);

        if (view)
        {
            if (!view->strIconKey.isEmpty())
                created->setIcon(AA::themedIcon(view->strIconKey));
            if (!view->strShortcut.isEmpty())
                created->setShortcut(QKeySequence(view->strShortcut, QKeySequence::PortableText));
            created->setCheckable(view->bCheckable);
        }
        if (created->isCheckable())
            created->setChecked(m_checked.value(commandId, false));

        // 勾选态先记下再执行：命令执行期间 shell 可能重建（模态对话框里功能开关变化），之后不再访问动作。
        m_connections.append(QObject::connect(created, &QAction::triggered, created, [this, commandId](bool checked) {
            if (const QAction* source = m_actions.value(commandId).data(); source && source->isCheckable())
                m_checked.insert(commandId, checked);
            if (m_trigger)
                m_trigger(commandId);
            }));

        // 加进 shell 的动作列表：快捷键在整个窗口生效，不依赖它是否出现在某个菜单或工具栏里。
        m_owner->addAction(created);
        m_actions.insert(commandId, created);

        applyText(created, commandId);
        applyState(created, commandId);
        return created;
    }

    void CommandActions::addToGroup(QAction* action, const QString& groupKey)
    {
        if (!action || groupKey.isEmpty() || !m_owner)
            return;

        QPointer<QActionGroup> group = m_groups.value(groupKey);
        if (!group)
        {
            group = new QActionGroup(m_owner);
            group->setExclusive(true);
            m_groups.insert(groupKey, group);
        }
        action->setCheckable(true);
        group->addAction(action);
    }

    void CommandActions::refresh()
    {
        for (auto it = m_actions.constBegin(); it != m_actions.constEnd(); ++it)
        {
            if (QAction* action = it.value().data())
            {
                applyText(action, it.key());
                applyState(action, it.key());
                // 图标按当前调色板着色：换肤后要重新着色，否则深色主题仍留浅色图标。
                applyIcon(action, it.key());
            }
        }
    }

    void CommandActions::applyIcon(QAction* action, const QString& commandId) const
    {
        const CommandView* view = viewOf(commandId);
        if (!view || view->strIconKey.isEmpty())
            return;

        action->setIcon(AA::themedIcon(view->strIconKey));
    }

    void CommandActions::applyText(QAction* action, const QString& commandId) const
    {
        const CommandView* view = viewOf(commandId);

        // 标题：目录里的 strTitleKey 按当前语言翻译；没有目录或没有 titleKey 时显示命令 id。
        QString title = view ? translate(view->strTitleKey) : QString();
        if (title.isEmpty())
            title = commandId;
        action->setText(title);

        QString toolTip = view ? translate(view->strToolTipKey) : QString();
        if (toolTip.isEmpty())
            toolTip = title;
        if (!action->shortcut().isEmpty())
            toolTip += QStringLiteral(" (%1)").arg(action->shortcut().toString(QKeySequence::NativeText));
        action->setToolTip(toolTip);
    }

    void CommandActions::applyState(QAction* action, const QString& commandId)
    {
        // 功能开关：命令需要的开关全部打开才可用。
        bool featuresOk = true;
        if (const CommandView* view = viewOf(commandId); view && !view->listRequiredFeatures.isEmpty())
        {
            const FeatureFlagSet flags = (m_shellServices && m_shellServices->featureFlags)
                ? m_shellServices->featureFlags->snapshot()
                : FeatureFlagSet();
            for (const QString& feature : view->listRequiredFeatures)
            {
                if (!flags.has(feature))
                {
                    featuresOk = false;
                    break;
                }
            }
        }

        // 业务状态：提供者只改自己关心的字段。
        CommandStatus status;
        status.bEnabled = true;
        status.bChecked = action->isChecked();
        if (m_shellServices && m_shellServices->cmdState)
            m_shellServices->cmdState->queryState(commandId, status);

        action->setEnabled(featuresOk && status.bEnabled);
        action->setVisible(status.bVisible);
        if (action->isCheckable())
        {
            action->setChecked(status.bChecked);
            m_checked.insert(commandId, status.bChecked);
        }
    }
}  // namespace AA