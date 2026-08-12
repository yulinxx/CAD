#include "UiInteractionDispatcher.h"

#include "UiStateCenter.h"
#include "UiServices.h"
#include "UiFrameworkServices.h"
#include "Log/SyLogger.h"

namespace
{
    QString statusPromptForCommand(const QString& commandId)
    {
        if (commandId.startsWith(QStringLiteral("draw.line")))
            return QStringLiteral("Draw line - click to start");
        if (commandId.startsWith(QStringLiteral("draw.circle")))
            return QStringLiteral("Draw circle - click center, drag radius");
        if (commandId.startsWith(QStringLiteral("draw.arc")))
            return QStringLiteral("Draw arc - click start, center, end");
        if (commandId.startsWith(QStringLiteral("draw.rect")))
            return QStringLiteral("Draw rectangle - click corner, drag to opposite");
        if (commandId.startsWith(QStringLiteral("draw.polyline")))
            return QStringLiteral("Draw polyline - click points, right-click to finish");
        if (commandId.startsWith(QStringLiteral("draw.spline")))
            return QStringLiteral("Draw spline - click control points");
        if (commandId.startsWith(QStringLiteral("draw.ellipse")))
            return QStringLiteral("Draw ellipse - click center, drag axes");
        if (commandId.startsWith(QStringLiteral("edit.move")))
            return QStringLiteral("Move - select entities, drag to destination");
        if (commandId.startsWith(QStringLiteral("edit.copy")))
            return QStringLiteral("Copy - select entities, specify destination");
        if (commandId.startsWith(QStringLiteral("edit.rotate")))
            return QStringLiteral("Rotate - select entities, specify angle");
        if (commandId.startsWith(QStringLiteral("edit.scale")))
            return QStringLiteral("Scale - select entities, specify factor");
        if (commandId.startsWith(QStringLiteral("edit.delete")))
            return QStringLiteral("Delete - select entities to remove");
        if (commandId.startsWith(QStringLiteral("edit.fillet")))
            return QStringLiteral("Fillet - select two lines or arcs");
        if (commandId.startsWith(QStringLiteral("edit.chamfer")))
            return QStringLiteral("Chamfer - select two lines");
        if (commandId.startsWith(QStringLiteral("edit.trim")))
            return QStringLiteral("Trim - select cutting edge, then entities to trim");
        if (commandId.startsWith(QStringLiteral("edit.extend")))
            return QStringLiteral("Extend - select boundary, then entities to extend");
        if (commandId.startsWith(QStringLiteral("edit.offset")))
            return QStringLiteral("Offset - select entity, specify distance");
        if (commandId.startsWith(QStringLiteral("edit.mirror")))
            return QStringLiteral("Mirror - select entities, specify mirror line");
        if (commandId.startsWith(QStringLiteral("edit.array")))
            return QStringLiteral("Array - select entities, specify pattern");
        if (commandId.startsWith(QStringLiteral("edit.stretch")))
            return QStringLiteral("Stretch - select entities with crossing window");
        if (commandId.startsWith(QStringLiteral("select")))
            return QStringLiteral("Select - click or drag to select entities");
        if (commandId.startsWith(QStringLiteral("view.pan")))
            return QStringLiteral("Pan - drag to navigate");
        if (commandId.startsWith(QStringLiteral("view.zoom")))
            return QStringLiteral("Zoom - scroll or drag to zoom");

        return QStringLiteral("Ready");
    }
}

DefaultInteractionDispatcher::DefaultInteractionDispatcher() = default;
DefaultInteractionDispatcher::~DefaultInteractionDispatcher() = default;

void DefaultInteractionDispatcher::syncCommandFinishState()
{
    if (!m_stateCenter)
        return;

    m_stateCenter->setBusy(false);
    m_stateCenter->setCurrentCommandPhase(QStringLiteral("idle"));
    m_stateCenter->setCurrentCommandId(QString());
    m_stateCenter->setCurrentCommandType(QString());
    m_stateCenter->setStatusPrompt(QStringLiteral("Ready"));
    m_stateCenter->clearInteractionState();

    QVariantMap meta = m_stateCenter->metadata();
    meta.remove(QStringLiteral("commandId"));
    meta.remove(QStringLiteral("commandType"));
    meta.insert(QStringLiteral("statusPrompt"), QStringLiteral("Ready"));
    m_stateCenter->setMetadata(meta);

    m_activeCommandId.clear();
    m_commandType.clear();
}

void DefaultInteractionDispatcher::begin(const QString& commandId)
{
    m_activeCommandId = commandId;
    m_commandType = resolveCommandType(commandId);

    if (m_stateCenter)
    {
        m_stateCenter->setCurrentCommandId(commandId);
        m_stateCenter->setCurrentCommandType(m_commandType);
        m_stateCenter->setStatusPrompt(statusPromptForCommand(commandId));
        m_stateCenter->clearInteractionState();
        m_stateCenter->setCurrentCommandPhase(QStringLiteral("active"));
        m_stateCenter->setBusy(true);
    }

    SY_INFOF("[InteractionDispatcher] Begin command: id=%s type=%s",
        commandId.toUtf8().constData(),
        m_commandType.toUtf8().constData());

    if (m_toolChangedCallback)
        m_toolChangedCallback(commandId);
}

void DefaultInteractionDispatcher::submit()
{
    if (!m_stateCenter)
    {
        SY_WARN("[InteractionDispatcher] submit without state center");
        return;
    }

    SY_INFOF("[InteractionDispatcher] Submit command: id=%s", m_activeCommandId.toUtf8().constData());
    syncCommandFinishState();
}

void DefaultInteractionDispatcher::cancel()
{
    if (!m_stateCenter)
    {
        SY_WARN("[InteractionDispatcher] cancel without state center");
        return;
    }

    SY_INFOF("[InteractionDispatcher] Cancel command: id=%s", m_activeCommandId.toUtf8().constData());
    syncCommandFinishState();
}

QString DefaultInteractionDispatcher::activeCommandId() const
{
    return m_activeCommandId;
}

bool DefaultInteractionDispatcher::hasActiveCommand() const
{
    return !m_activeCommandId.isEmpty();
}

void DefaultInteractionDispatcher::setEventHandler(InteractionEventHandler handler)
{
    m_eventHandler = std::move(handler);
}

bool DefaultInteractionDispatcher::dispatchEvent(const InteractionEvent& event)
{
    if (!m_stateCenter || !hasActiveCommand())
        return false;

    const bool consumed = m_eventHandler && m_eventHandler(event);

    QString kind;
    switch (event.type)
    {
        case InteractionEventType::MouseDown:
            kind = QStringLiteral("mouseDown");
            break;
        case InteractionEventType::MouseMove:
            kind = QStringLiteral("mouseMove");
            break;
        case InteractionEventType::MouseUp:
            kind = QStringLiteral("mouseUp");
            break;
        case InteractionEventType::KeyPress:
            kind = QStringLiteral("key");
            break;
    }

    m_stateCenter->setInteractionState(kind, event.x, event.y, event.key);
    SY_DEBUGF("[InteractionDispatcher] Event dispatched: kind=%s x=%d y=%d key=%d consumed=%d",
        kind.toUtf8().constData(), event.x, event.y, event.key, consumed ? 1 : 0);
    return consumed;
}

void DefaultInteractionDispatcher::setStateCenter(UiStateCenter* stateCenter)
{
    m_stateCenter = stateCenter;
}

void DefaultInteractionDispatcher::setUiServices(const UiServices& services)
{
    m_uiServices = services;
}

void DefaultInteractionDispatcher::setLayoutService(UiLayoutService* layoutService)
{
    m_layoutService = layoutService;
}

void DefaultInteractionDispatcher::setFrameworkServices(const UiFrameworkServices& services)
{
    m_frameworkServices = services;
}

void DefaultInteractionDispatcher::setToolChangedCallback(std::function<void(const QString&)> callback)
{
    m_toolChangedCallback = callback;
}

void DefaultInteractionDispatcher::setCommandType(const QString& commandType)
{
    m_commandType = commandType;
    if (m_stateCenter)
        m_stateCenter->setCurrentCommandType(commandType);
}

QString DefaultInteractionDispatcher::resolveCommandType(const QString& commandId) const
{
    if (commandId.startsWith(QStringLiteral("draw.")))
        return QStringLiteral("draw");
    if (commandId.startsWith(QStringLiteral("edit.")))
        return QStringLiteral("edit");
    if (commandId.startsWith(QStringLiteral("view.")))
        return QStringLiteral("view");
    if (commandId.startsWith(QStringLiteral("select")))
        return QStringLiteral("select");
    if (commandId.startsWith(QStringLiteral("file.")))
        return QStringLiteral("file");
    if (commandId.startsWith(QStringLiteral("help.")))
        return QStringLiteral("help");
    return QStringLiteral("other");
}