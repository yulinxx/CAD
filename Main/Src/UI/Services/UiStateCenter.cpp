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
    state.currentWorkbenchId = m_workbenchId;
    state.currentThemeId = m_themeId;
    state.currentViewMode = m_viewMode;

    state.currentLayerId = m_layerId;
    state.layerVisible = m_layerVisible;
    state.layerLocked = m_layerLocked;
    state.currentDocumentId = m_documentId;
    state.currentCommandId = m_commandId;

    state.currentCommandPhase = m_commandPhase;
    state.currentCommandOwner = m_commandOwner;
    state.currentCommandType = m_commandType;
    state.interactionKind = m_interactionKind;
    state.interactionPointerX = m_interactionPointerX;
    state.interactionPointerY = m_interactionPointerY;
    state.interactionKey = m_interactionKey;

    state.currentSelectionText = m_selectionText;
    state.currentSelectionSource = m_selectionSource;
    state.currentSelectionType = m_selectionType;

    state.busy = m_busy;
    state.dirty = m_dirty;
    state.commandFailed = m_commandFailed;
    state.failedCommandId = m_failedCommandId;
    state.failureMessage = m_failureMessage;
    state.progress = m_progress;
    state.statusMessage = m_statusMessage;
    state.statusPrompt = m_statusPrompt;
    state.taskPhase = m_taskPhase;
    state.errorCode = m_errorCode;
    state.metadata = m_metadata;
    state.refreshState = m_refreshState;
    state.activeToolId = m_activeToolId;
    state.inputFocusWidget = m_inputFocusWidget;

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

QString UiStateCenter::interactionKind() const
{
    return m_interactionKind;
}

int UiStateCenter::interactionPointerX() const
{
    return m_interactionPointerX;
}

int UiStateCenter::interactionPointerY() const
{
    return m_interactionPointerY;
}

int UiStateCenter::interactionKey() const
{
    return m_interactionKey;
}

bool UiStateCenter::busy() const
{
    return m_busy;
}

bool UiStateCenter::dirty() const
{
    return m_dirty;
}

int UiStateCenter::progress() const
{
    return m_progress;
}

QString UiStateCenter::statusMessage() const
{
    return m_statusMessage;
}

QString UiStateCenter::statusPrompt() const
{
    return m_statusPrompt;
}

QString UiStateCenter::taskPhase() const
{
    return m_taskPhase;
}

int UiStateCenter::errorCode() const
{
    return m_errorCode;
}

QVariantMap UiStateCenter::metadata() const
{
    return m_metadata;
}

QString UiStateCenter::refreshState() const
{
    return m_refreshState;
}

QString UiStateCenter::activeToolId() const
{
    return m_activeToolId;
}

QString UiStateCenter::inputFocusWidget() const
{
    return m_inputFocusWidget;
}

void UiStateCenter::setRefreshState(const QString& state)
{
    if (m_refreshState == state)
    {
        return;
    }
    m_refreshState = state;
    emit refreshStateChanged(state);
    emit stateChanged();
}

void UiStateCenter::setInteractionState(const QString& kind, int pointerX, int pointerY, int key)
{
    if (m_interactionKind == kind && m_interactionPointerX == pointerX && m_interactionPointerY == pointerY &&
        m_interactionKey == key)
    {
        return;
    }

    m_interactionKind = kind;
    m_interactionPointerX = pointerX;
    m_interactionPointerY = pointerY;
    m_interactionKey = key;

    emit interactionStateChanged(kind, pointerX, pointerY, key);
    emit stateChanged();
}

void UiStateCenter::clearInteractionState()
{
    setInteractionState(QString(), -1, -1, -1);
}

void UiStateCenter::setStatusPrompt(const QString& prompt)
{
    if (m_statusPrompt == prompt)
    {
        return;
    }

    m_statusPrompt = prompt;
    m_metadata.insert(QStringLiteral("statusPrompt"), prompt);
    emit statusPromptChanged(prompt);
    emit metadataChanged();
    emit stateChanged();
}

// 状态设置方法（带变更检测和信号发射）

void UiStateCenter::setCurrentWorkbenchId(const QString& id)
{
    if (m_workbenchId == id)
    {
        return;
    }

    m_workbenchId = id;
    emit currentWorkbenchChanged(id);
    emit stateChanged();
}

void UiStateCenter::setCurrentThemeId(const QString& id)
{
    if (m_themeId == id)
    {
        return;
    }

    m_themeId = id;
    emit currentThemeChanged(id);
    emit stateChanged();
}

void UiStateCenter::setCurrentViewMode(const QString& mode)
{
    if (m_viewMode == mode)
    {
        return;
    }

    m_viewMode = mode;
    emit currentViewModeChanged(mode);
    emit stateChanged();
}

void UiStateCenter::setCurrentLayerId(const QString& layerId)
{
    if (m_layerId == layerId)
    {
        return;
    }

    m_layerId = layerId;
    emit currentLayerChanged(layerId);
    emit stateChanged();
}

void UiStateCenter::setLayerVisibilityState(bool visible)
{
    if (m_layerVisible == visible)
    {
        return;
    }
    m_layerVisible = visible;
    emit layerVisibilityChanged(visible);
    emit stateChanged();
}

bool UiStateCenter::layerVisibilityState() const
{
    return m_layerVisible;
}

void UiStateCenter::setLayerLockState(bool locked)
{
    if (m_layerLocked == locked)
    {
        return;
    }
    m_layerLocked = locked;
    emit layerLockChanged(locked);
    emit stateChanged();
}

bool UiStateCenter::layerLockState() const
{
    return m_layerLocked;
}

void UiStateCenter::setCurrentDocumentId(const QString& documentId)
{
    if (m_documentId == documentId)
    {
        return;
    }

    m_documentId = documentId;
    emit currentDocumentChanged(documentId);
    emit stateChanged();
}

void UiStateCenter::setCurrentCommandId(const QString& commandId)
{
    if (m_commandId == commandId)
    {
        return;
    }

    m_commandId = commandId;
    emit currentCommandChanged(commandId);
    emit stateChanged();
}

void UiStateCenter::setCurrentCommandPhase(const QString& phase)
{
    if (m_commandPhase == phase)
    {
        return;
    }

    m_commandPhase = phase;
    emit currentCommandPhaseChanged(phase);
    emit stateChanged();
}

void UiStateCenter::setCurrentCommandOwner(const QString& owner)
{
    if (m_commandOwner == owner)
    {
        return;
    }

    m_commandOwner = owner;
    m_metadata.insert(QStringLiteral("commandOwner"), owner);
    emit metadataChanged();
    emit stateChanged();
}

void UiStateCenter::setCurrentCommandType(const QString& type)
{
    if (m_commandType == type)
    {
        return;
    }
    m_commandType = type;
    m_metadata.insert(QStringLiteral("commandType"), type);
    emit metadataChanged();
    emit stateChanged();
}

void UiStateCenter::setCurrentSelectionText(const QString& text)
{
    if (m_selectionText == text)
    {
        return;
    }
    m_selectionText = text;
    emit currentSelectionTextChanged(text);
    emit stateChanged();
}

void UiStateCenter::setSelectionContext(const QString& source, const QString& text)
{
    if (m_selectionText == text && m_selectionSource == source)
    {
        return;
    }

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
    {
        return;
    }

    m_busy = busy;
    emit busyChanged(busy);
    emit stateChanged();
}

void UiStateCenter::setDirty(bool dirty)
{
    if (m_dirty == dirty)
    {
        return;
    }

    m_dirty = dirty;
    emit dirtyChanged(dirty);
    emit stateChanged();
}

/// 统一设置命令失败状态，触发 commandFailed 信号
/// @param commandId 失败的命令 ID
/// @param message 失败原因描述
void UiStateCenter::setCommandFailed(const QString& commandId, const QString& message)
{
    m_commandFailed = true;
    m_failedCommandId = commandId;
    m_failureMessage = message;

    // 同时写入元数据，方便展示层读取
    m_metadata.insert(QStringLiteral("commandFailed"), true);
    m_metadata.insert(QStringLiteral("failedCommandId"), commandId);
    m_metadata.insert(QStringLiteral("failureMessage"), message);

    emit commandFailed(commandId, message);
    emit stateChanged();
}

/// 清除命令失败状态
void UiStateCenter::clearCommandFailed()
{
    if (!m_commandFailed)
    {
        return;
    }

    m_commandFailed = false;
    m_failedCommandId.clear();
    m_failureMessage.clear();

    m_metadata.insert(QStringLiteral("commandFailed"), false);
    m_metadata.insert(QStringLiteral("failedCommandId"), QString());
    m_metadata.insert(QStringLiteral("failureMessage"), QString());

    emit stateChanged();
}

void UiStateCenter::setMetadata(const QVariantMap& metadata)
{
    m_metadata = metadata;

    if (m_metadata.contains(QStringLiteral("selectionSource")))
    {
        m_selectionSource = m_metadata.value(QStringLiteral("selectionSource")).toString();
    }
    if (m_metadata.contains(QStringLiteral("selectionText")))
    {
        m_selectionText = m_metadata.value(QStringLiteral("selectionText")).toString();
    }
    if (m_metadata.contains(QStringLiteral("selectionType")))
    {
        m_selectionType = m_metadata.value(QStringLiteral("selectionType")).toString();
    }

    if (m_metadata.contains(QStringLiteral("commandOwner")))
    {
        m_commandOwner = m_metadata.value(QStringLiteral("commandOwner")).toString();
    }
    if (m_metadata.contains(QStringLiteral("commandType")))
    {
        m_commandType = m_metadata.value(QStringLiteral("commandType")).toString();
    }
    if (m_metadata.contains(QStringLiteral("statusPrompt")))
    {
        m_statusPrompt = m_metadata.value(QStringLiteral("statusPrompt")).toString();
    }

    emit metadataChanged();
    emit stateChanged();
}

/// 统一设置任务进度和消息
/// @param progress 进度值 (0-100)，-1 表示清除进度
/// @param message 状态消息
void UiStateCenter::setProgress(int progress, const QString& message)
{
    if (m_progress == progress && m_statusMessage == message)
    {
        return;
    }

    m_progress = progress;
    m_statusMessage = message;

    // 同步写入元数据，方便状态栏等展示层读取
    m_metadata.insert(QStringLiteral("progress"), progress);
    m_metadata.insert(QStringLiteral("statusMessage"), message);

    emit progressChanged(progress, message);
    emit stateChanged();
}

/// 设置任务阶段和消息
/// @param phase 阶段标识
/// @param message 阶段描述
void UiStateCenter::setTaskPhase(const QString& phase, const QString& message)
{
    if (m_taskPhase == phase && m_statusMessage == message)
    {
        return;
    }

    m_taskPhase = phase;
    m_statusMessage = message;

    m_metadata.insert(QStringLiteral("taskPhase"), phase);
    m_metadata.insert(QStringLiteral("statusMessage"), message);

    emit taskPhaseChanged(phase, message);
    emit stateChanged();
}

/// 统一设置错误状态
/// @param code 错误码
/// @param message 错误描述
void UiStateCenter::setError(int code, const QString& message)
{
    m_errorCode = code;
    m_statusMessage = message;

    m_metadata.insert(QStringLiteral("errorCode"), code);
    m_metadata.insert(QStringLiteral("errorMessage"), message);

    emit errorOccurred(code, message);
    emit stateChanged();
}

/// 清除错误状态
void UiStateCenter::clearError()
{
    if (m_errorCode == 0 && m_statusMessage.isEmpty())
    {
        return;
    }

    m_errorCode = 0;
    m_statusMessage.clear();

    m_metadata.insert(QStringLiteral("errorCode"), 0);
    m_metadata.insert(QStringLiteral("errorMessage"), QString());

    emit stateChanged();
}

/// 清除任务进度和阶段（任务完成时调用）
void UiStateCenter::clearTask()
{
    if (m_progress == -1 && m_taskPhase.isEmpty() && m_statusMessage.isEmpty())
    {
        return;
    }

    m_progress = -1;
    m_taskPhase.clear();
    m_statusMessage.clear();

    m_metadata.insert(QStringLiteral("progress"), -1);
    m_metadata.insert(QStringLiteral("taskPhase"), QString());
    m_metadata.insert(QStringLiteral("statusMessage"), QString());

    emit stateChanged();
}

void UiStateCenter::setActiveToolId(const QString& toolId)
{
    if (m_activeToolId == toolId)
    {
        return;
    }

    m_activeToolId = toolId;
    emit activeToolChanged(toolId);
    emit stateChanged();
}

void UiStateCenter::setInputFocusWidget(const QString& widgetName)
{
    if (m_inputFocusWidget == widgetName)
    {
        return;
    }

    m_inputFocusWidget = widgetName;
    emit inputFocusWidgetChanged(widgetName);
    emit stateChanged();
}