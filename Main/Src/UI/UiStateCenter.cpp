#include "UiStateCenter.h"


/// @param parent 父对象
UiStateCenter::UiStateCenter(QObject* parent) : QObject(parent)
{
}

/// 获取状态快照，包含所有当前状态
UiStateSnapshot UiStateCenter::snapshot() const
{
    UiStateSnapshot state;
    state.currentWorkbenchId = m_workbenchId;
    state.currentThemeId = m_themeId;
    state.currentViewMode = m_viewMode;

    state.currentLayerId = m_layerId;
    state.currentDocumentId = m_documentId;
    state.currentCommandId = m_commandId;

    state.currentCommandPhase = m_commandPhase;
    state.currentCommandOwner = m_commandOwner;
    state.currentCommandType = m_commandType;

    state.currentSelectionText = m_selectionText;
    state.currentSelectionSource = m_selectionSource;
    state.currentSelectionType = m_selectionType;

    state.busy = m_busy;
    state.dirty = m_dirty;
    state.metadata = m_metadata;

    return state;
}

// 状态获取方法

QString UiStateCenter::currentWorkbenchId() const
{
    return m_workbenchId;
}
QString UiStateCenter::currentThemeId() const
{
    return m_themeId;
}
QString UiStateCenter::currentViewMode() const
{
    return m_viewMode;
}
QString UiStateCenter::currentLayerId() const
{
    return m_layerId;
}
QString UiStateCenter::currentDocumentId() const
{
    return m_documentId;
}
QString UiStateCenter::currentCommandId() const
{
    return m_commandId;
}
QString UiStateCenter::currentCommandPhase() const
{
    return m_commandPhase;
}
QString UiStateCenter::currentSelectionText() const
{
    return m_selectionText;
}
QString UiStateCenter::currentSelectionSource() const
{
    return m_selectionSource;
}
QString UiStateCenter::currentSelectionType() const
{
    return m_selectionType;
}
QString UiStateCenter::currentCommandOwner() const
{
    return m_commandOwner;
}
QString UiStateCenter::currentCommandType() const
{
    return m_commandType;
}

bool UiStateCenter::busy() const
{
    return m_busy;
}

bool UiStateCenter::dirty() const
{
    return m_dirty;
}

QVariantMap UiStateCenter::metadata() const
{
    return m_metadata;
}

// 状态设置方法（带变更检测和信号发射）

void UiStateCenter::setCurrentWorkbenchId(const QString& id)
{
    if (m_workbenchId == id)
        return;

    m_workbenchId = id;
    emit currentWorkbenchChanged(id);
    emit stateChanged();
}

void UiStateCenter::setCurrentThemeId(const QString& id)
{
    if (m_themeId == id)
        return;

    m_themeId = id;
    emit currentThemeChanged(id);
    emit stateChanged();
}

void UiStateCenter::setCurrentViewMode(const QString& mode)
{
    if (m_viewMode == mode)
        return;

    m_viewMode = mode;
    emit currentViewModeChanged(mode);
    emit stateChanged();
}

void UiStateCenter::setCurrentLayerId(const QString& layerId)
{
    if (m_layerId == layerId)
        return;

    m_layerId = layerId;
    emit currentLayerChanged(layerId);
    emit stateChanged();
}

void UiStateCenter::setCurrentDocumentId(const QString& documentId)
{
    if (m_documentId == documentId)
        return;

    m_documentId = documentId;
    emit currentDocumentChanged(documentId);
    emit stateChanged();
}

void UiStateCenter::setCurrentCommandId(const QString& commandId)
{
    if (m_commandId == commandId)
        return;

    m_commandId = commandId;
    emit currentCommandChanged(commandId);
    emit stateChanged();
}

void UiStateCenter::setCurrentCommandPhase(const QString& phase)
{
    if (m_commandPhase == phase)
        return;

    m_commandPhase = phase;
    emit currentCommandPhaseChanged(phase);
    emit stateChanged();
}

void UiStateCenter::setCurrentCommandOwner(const QString& owner)
{
    if (m_commandOwner == owner)
        return;

    m_commandOwner = owner;
    m_metadata.insert(QStringLiteral("commandOwner"), owner);
    emit metadataChanged();
    emit stateChanged();
}

void UiStateCenter::setCurrentCommandType(const QString& type)
{
    if (m_commandType == type)
        return;
    m_commandType = type;
    m_metadata.insert(QStringLiteral("commandType"), type);
    emit metadataChanged();
    emit stateChanged();
}

void UiStateCenter::setCurrentSelectionText(const QString& text)
{
    if (m_selectionText == text)
        return;
    m_selectionText = text;
    emit currentSelectionTextChanged(text);
    emit stateChanged();
}

void UiStateCenter::setSelectionContext(const QString& source, const QString& text)
{
    if (m_selectionText == text && m_selectionSource == source)
        return;

    m_selectionText = text;
    m_selectionSource = source;
    m_selectionType = source.contains(QStringLiteral("3D")) ? QStringLiteral("3D") : QStringLiteral("2D");

    m_metadata.insert(QStringLiteral("selectionSource"), source);
    m_metadata.insert(QStringLiteral("selectionText"), text);
    m_metadata.insert(QStringLiteral("selectionType"), m_selectionType);

    emit currentSelectionTextChanged(text);
    emit metadataChanged();
    emit stateChanged();
}

void UiStateCenter::setBusy(bool busy)
{
    if (m_busy == busy)
        return;

    m_busy = busy;
    emit busyChanged(busy);
    emit stateChanged();
}

void UiStateCenter::setDirty(bool dirty)
{
    if (m_dirty == dirty)
        return;

    m_dirty = dirty;
    emit dirtyChanged(dirty);
    emit stateChanged();
}

void UiStateCenter::setMetadata(const QVariantMap& metadata)
{
    m_metadata = metadata;

    if (m_metadata.contains(QStringLiteral("selectionSource")))
        m_selectionSource = m_metadata.value(QStringLiteral("selectionSource")).toString();
    if (m_metadata.contains(QStringLiteral("selectionText")))
        m_selectionText = m_metadata.value(QStringLiteral("selectionText")).toString();
    if (m_metadata.contains(QStringLiteral("selectionType")))
        m_selectionType = m_metadata.value(QStringLiteral("selectionType")).toString();

    if (m_metadata.contains(QStringLiteral("commandOwner")))
        m_commandOwner = m_metadata.value(QStringLiteral("commandOwner")).toString();
    if (m_metadata.contains(QStringLiteral("commandType")))
        m_commandType = m_metadata.value(QStringLiteral("commandType")).toString();

    emit metadataChanged();
    emit stateChanged();
}
