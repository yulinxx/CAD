/**
 * @file UiStateCenter.cpp
 * @brief UI 状态中心实现
 *
 * 集中管理全局 UI 状态（主题、视图模式、工作台等）。
 */
#include "UiStateCenter.h"

/// @param parent 父对象
UiStateCenter::UiStateCenter(QObject* parent)
    : QObject(parent)
{
}

/// 获取状态快照，包含所有当前状态
UiStateSnapshot UiStateCenter::snapshot() const
{
    UiStateSnapshot state;
    state.currentWorkbenchId = m_workspace.workbenchId;
    state.currentThemeId = m_workspace.themeId;
    state.currentViewMode = m_workspace.viewMode;

    state.currentLayerId = m_view.layerId;
    state.currentDocumentId = m_workspace.documentId;
    state.currentCommandId = m_command.commandId;

    state.currentCommandPhase = m_command.commandPhase;
    state.currentCommandOwner = m_command.commandOwner;
    state.currentCommandType = m_command.commandType;

    state.currentSelectionText = m_selection.text;
    state.currentSelectionSource = m_selection.source;
    state.currentSelectionType = m_selection.type;

    state.busy = m_command.busyCount > 0;
    state.dirty = m_command.dirty;
    state.progress = m_command.progress;
    state.statusPrompt = m_command.statusPrompt;
    state.metadata = m_view.metadata;
    state.refreshState = m_view.refreshState;
    state.activeToolId = m_workspace.activeToolId;
    state.inputFocusWidget = m_workspace.inputFocusWidget;

    return state;
}

// 状态获取方法

QString UiStateCenter::currentWorkbenchId() const
{
    return m_workspace.workbenchId;
}

QString UiStateCenter::currentLayerId() const
{
    return m_view.layerId;
}

bool UiStateCenter::dirty() const
{
    return m_command.dirty;
}

int UiStateCenter::progress() const
{
    return m_command.progress;
}

QVariantMap UiStateCenter::metadata() const
{
    return m_view.metadata;
}

void UiStateCenter::setStatusPrompt(const QString& prompt)
{
    if (m_command.statusPrompt == prompt)
    {
        return;
    }

    m_command.statusPrompt = prompt;
    // 不再写 metadata["statusPrompt"]：消除双写 + 回读环
    // metadata 降级为纯扩展袋，核心字段仅由各自 setter 维护
    emit stateChanged();
}

// 状态设置方法（带变更检测和信号发射）

void UiStateCenter::setCurrentWorkbenchId(const QString& id)
{
    if (m_workspace.workbenchId == id)
    {
        return;
    }

    m_workspace.workbenchId = id;
    emit currentWorkbenchChanged(id);
    emit stateChanged();
}

void UiStateCenter::setCurrentThemeId(const QString& id)
{
    if (m_workspace.themeId == id)
    {
        return;
    }

    m_workspace.themeId = id;
    emit currentThemeChanged(id);
    emit stateChanged();
}

void UiStateCenter::setCurrentViewMode(const QString& mode)
{
    if (m_workspace.viewMode == mode)
    {
        return;
    }

    m_workspace.viewMode = mode;
    emit stateChanged();
}

void UiStateCenter::setCurrentLayerId(const QString& layerId)
{
    if (m_view.layerId == layerId)
    {
        return;
    }

    m_view.layerId = layerId;
    emit stateChanged();
}

void UiStateCenter::setCurrentDocumentId(const QString& documentId)
{
    if (m_workspace.documentId == documentId)
    {
        return;
    }

    m_workspace.documentId = documentId;
    emit stateChanged();
}

void UiStateCenter::setCurrentCommandId(const QString& commandId)
{
    if (m_command.commandId == commandId)
    {
        return;
    }

    m_command.commandId = commandId;
    emit stateChanged();
}

void UiStateCenter::setCurrentCommandPhase(const QString& phase)
{
    if (m_command.commandPhase == phase)
    {
        return;
    }

    m_command.commandPhase = phase;
    emit stateChanged();
}

void UiStateCenter::setCurrentCommandOwner(const QString& owner)
{
    if (m_command.commandOwner == owner)
    {
        return;
    }

    m_command.commandOwner = owner;
    // 不再镜像写 metadata["commandOwner"]：核心字段仅由本 setter 维护
    emit stateChanged();
}

void UiStateCenter::setCurrentCommandType(const QString& type)
{
    if (m_command.commandType == type)
    {
        return;
    }
    m_command.commandType = type;
    // 不再镜像写 metadata["commandType"]：核心字段仅由本 setter 维护
    emit stateChanged();
}

void UiStateCenter::setCurrentSelectionText(const QString& text)
{
    if (m_selection.text == text)
    {
        return;
    }
    m_selection.text = text;
    // 不再发 currentSelectionTextChanged（死信号，无消费者）
    emit stateChanged();
}

void UiStateCenter::setSelectionContext(const QString& source, const QString& text)
{
    if (m_selection.text == text && m_selection.source == source)
    {
        return;
    }

    m_selection.text = text;
    m_selection.source = source;
    m_selection.type = source.contains(QStringLiteral("3D")) ? QStringLiteral("3D") : QStringLiteral("2D");

    // 不再镜像写 metadata 中的 selection* 三键：核心字段仅由本 setter 维护
    // 不再发 currentSelectionTextChanged（死信号，无消费者）
    emit stateChanged();
}

void UiStateCenter::pushBusy()
{
    if (m_command.busyCount == 0)
    {
        m_command.busyCount = 1;
        emit busyChanged(true);
        emit stateChanged();
    }
    else
    {
        ++m_command.busyCount;
    }
}

void UiStateCenter::popBusy()
{
    if (m_command.busyCount <= 0)
    {
        SY_WARN("[UiStateCenter] popBusy without matching pushBusy, ignored");
        return;
    }
    --m_command.busyCount;
    if (m_command.busyCount == 0)
    {
        emit busyChanged(false);
        emit stateChanged();
    }
}

void UiStateCenter::setDirty(bool dirty)
{
    if (m_command.dirty == dirty)
    {
        return;
    }

    m_command.dirty = dirty;
    emit dirtyChanged(dirty);
    emit stateChanged();
}

void UiStateCenter::updateMetadata(const QString& key, const QVariant& value)
{
    if (m_view.metadata.value(key) == value)
    {
        return;
    }
    m_view.metadata[key] = value;
    // metadata 是纯扩展袋：不再回读同步核心键（statusPrompt/selection*/command*），
    // 核心字段仅由各自 setter 维护

    emit metadataChanged();
    emit stateChanged();
}

void UiStateCenter::updateMetadata(const QVariantMap& metadata)
{
    bool changed = false;
    for (auto it = metadata.constBegin(); it != metadata.constEnd(); ++it)
    {
        const QString& key = it.key();
        const QVariant& value = it.value();
        if (m_view.metadata.value(key) != value)
        {
            m_view.metadata[key] = value;
            changed = true;
            // 纯扩展袋语义：不回读同步核心键
        }
    }
    if (changed)
    {
        emit metadataChanged();
        emit stateChanged();
    }
}

void UiStateCenter::setMetadata(const QVariantMap& metadata)
{
    m_view.metadata = metadata;
    // metadata 是纯扩展袋：不再回读同步核心键（selection*/command*/statusPrompt）
    // 核心字段仅由各自 setter 维护

    emit metadataChanged();
    emit stateChanged();
}


void UiStateCenter::setActiveToolId(const QString& toolId)
{
    if (m_workspace.activeToolId == toolId)
    {
        return;
    }

    m_workspace.activeToolId = toolId;
    emit stateChanged();
}

void UiStateCenter::setInputFocusWidget(const QString& widgetName)
{
    if (m_workspace.inputFocusWidget == widgetName)
    {
        return;
    }

    m_workspace.inputFocusWidget = widgetName;
    emit stateChanged();
}