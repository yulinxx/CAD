/**
 * @file UiCommandDispatcher.cpp
 * @brief 命令分发器实现
 */

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
        { QStringLiteral("commandPhase"), QStringLiteral("begin") },
        { QStringLiteral("commandType"), commandId.startsWith(QStringLiteral("3d.")) ? QStringLiteral("3D") : QStringLiteral("2D") }
        });
}

void DefaultUiCommandDispatcher::execute(const QString& commandId)
{
    qDebug() << "Execute command:" << commandId;
    begin(commandId);

    if (m_layoutService)
        Q_UNUSED(m_layoutService);
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
            { QStringLiteral("commandPhase"), QStringLiteral("idle") },
            { QStringLiteral("commandType"), m_activeCommandId.startsWith(QStringLiteral("3d.")) ? QStringLiteral("3D") : QStringLiteral("2D") }
            });
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
            { QStringLiteral("commandPhase"), QStringLiteral("idle") },
            { QStringLiteral("commandType"), m_activeCommandId.startsWith(QStringLiteral("3d.")) ? QStringLiteral("3D") : QStringLiteral("2D") }
            });
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

QString DefaultUiCommandDispatcher::activeCommandId() const
{
    return m_activeCommandId;
}