#include "UiCommandDispatcher.h"

#include <QAction>
#include <QDebug>

#include "UiLayoutService.h"
#include "UiStateCenter.h"

void DefaultUiCommandDispatcher::bindAction(QAction* action, const QString& commandId)
{
    if (!action)
        return;

    QObject::connect(action, &QAction::triggered, action, [this, commandId]() {
        if (m_frameworkServices.canExecuteCommand && !m_frameworkServices.canExecuteCommand(commandId, QStringLiteral("UiCommandDispatcher::bindAction")))
        {
            if (m_frameworkServices.reportError)
                m_frameworkServices.reportError(QStringLiteral("command.denied"), QStringLiteral("Command denied: %1").arg(commandId), QStringLiteral("UiCommandDispatcher::bindAction"));
            return;
        }
        execute(commandId);
    });
}

void DefaultUiCommandDispatcher::updatePhase(const QString& phase)
{
    if (!m_stateCenter)
        return;

    m_stateCenter->setCurrentCommandPhase(phase);
    m_stateCenter->setCurrentCommandId(m_activeCommandId);
}

void DefaultUiCommandDispatcher::begin(const QString& commandId)
{
    m_activeCommandId = commandId;

    if (!m_stateCenter)
        return;

    m_stateCenter->setBusy(true);
    m_stateCenter->setCurrentCommandId(commandId);
    m_stateCenter->setCurrentCommandOwner(QStringLiteral("dispatcher"));
    m_stateCenter->setCurrentCommandPhase(QStringLiteral("begin"));
    m_stateCenter->setMetadata({
        { QStringLiteral("commandState"), QStringLiteral("begin") },
        { QStringLiteral("activeCommandId"), commandId },
        { QStringLiteral("commandOwner"), QStringLiteral("dispatcher") },
        { QStringLiteral("commandPhase"), QStringLiteral("begin") }
    });
}

void DefaultUiCommandDispatcher::execute(const QString& commandId)
{
    begin(commandId);

    if (!m_stateCenter && m_frameworkServices.reportError)
        m_frameworkServices.reportError(QStringLiteral("command.execute_no_state"), QStringLiteral("Command executed without state center: %1").arg(commandId), QStringLiteral("DefaultUiCommandDispatcher::execute"));

    submit();
}

void DefaultUiCommandDispatcher::submit()
{
    updatePhase(QStringLiteral("submit"));

    if (m_stateCenter)
    {
        m_stateCenter->setBusy(false);
        m_stateCenter->setCurrentCommandPhase(QStringLiteral("idle"));
        m_stateCenter->setCurrentCommandId(QStringLiteral("idle"));
        m_stateCenter->setCurrentCommandOwner(QStringLiteral("none"));
        m_stateCenter->setMetadata({
            { QStringLiteral("commandState"), QStringLiteral("submit") },
            { QStringLiteral("activeCommandId"), m_activeCommandId },
            { QStringLiteral("commandOwner"), QStringLiteral("none") },
            { QStringLiteral("commandPhase"), QStringLiteral("idle") }
        });
    }
    else if (m_frameworkServices.reportError)
    {
        m_frameworkServices.reportError(QStringLiteral("command.submit_no_state"), QStringLiteral("Command submit without state center: %1").arg(m_activeCommandId), QStringLiteral("DefaultUiCommandDispatcher::submit"));
    }

    m_activeCommandId.clear();
}

void DefaultUiCommandDispatcher::cancel()
{
    updatePhase(QStringLiteral("cancel"));

    if (m_stateCenter)
    {
        m_stateCenter->setBusy(false);
        m_stateCenter->setCurrentCommandPhase(QStringLiteral("idle"));
        m_stateCenter->setCurrentCommandId(QStringLiteral("idle"));
        m_stateCenter->setCurrentCommandOwner(QStringLiteral("none"));
        m_stateCenter->setMetadata({
            { QStringLiteral("commandState"), QStringLiteral("cancel") },
            { QStringLiteral("activeCommandId"), m_activeCommandId },
            { QStringLiteral("commandOwner"), QStringLiteral("none") },
            { QStringLiteral("commandPhase"), QStringLiteral("idle") }
        });
    }
    else if (m_frameworkServices.reportError)
    {
        m_frameworkServices.reportError(QStringLiteral("command.cancel_no_state"), QStringLiteral("Command cancel without state center: %1").arg(m_activeCommandId), QStringLiteral("DefaultUiCommandDispatcher::cancel"));
    }

    m_activeCommandId.clear();
}

void DefaultUiCommandDispatcher::setStateCenter(UiStateCenter* stateCenter)
{
    m_stateCenter = stateCenter;
}

void DefaultUiCommandDispatcher::setLayoutService(UiLayoutService* layoutService)
{
    m_layoutService = layoutService;
}

void DefaultUiCommandDispatcher::setFrameworkServices(const UiFrameworkServices& services)
{
    m_frameworkServices = services;
}

QString DefaultUiCommandDispatcher::activeCommandId() const
{
    return m_activeCommandId;
}