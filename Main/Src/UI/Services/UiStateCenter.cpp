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
    state.layerVisible = m_view.layerVisible;
    state.layerLocked = m_view.layerLocked;
    state.currentDocumentId = m_workspace.documentId;
    state.currentCommandId = m_command.commandId;

    state.currentCommandPhase = m_command.commandPhase;
    state.currentCommandOwner = m_command.commandOwner;
    state.currentCommandType = m_command.commandType;
    state.interactionKind = m_view.interactionKind;
    state.interactionPointerX = m_view.interactionPointerX;
    state.interactionPointerY = m_view.interactionPointerY;
    state.interactionKey = m_view.interactionKey;

    state.currentSelectionText = m_selection.text;
    state.currentSelectionSource = m_selection.source;
    state.currentSelectionType = m_selection.type;

    state.busy = m_command.busyCount > 0;
    state.dirty = m_command.dirty;
    state.commandFailed = m_command.failed;
    state.failedCommandId = m_command.failedCommandId;
    state.failureMessage = m_command.failureMessage;
    state.progress = m_command.progress;
    state.statusMessage = m_command.statusMessage;
    state.statusPrompt = m_command.statusPrompt;
    state.taskPhase = m_command.taskPhase;
    state.errorCode = m_command.errorCode;
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

QString UiStateCenter::currentThemeId() const
{
    return m_workspace.themeId;
}

QString UiStateCenter::currentViewMode() const
{
    return m_workspace.viewMode;
}

QString UiStateCenter::currentLayerId() const
{
    return m_view.layerId;
}

QString UiStateCenter::currentDocumentId() const
{
    return m_workspace.documentId;
}

QString UiStateCenter::currentCommandId() const
{
    return m_command.commandId;
}

QString UiStateCenter::currentCommandPhase() const
{
    return m_command.commandPhase;
}

QString UiStateCenter::currentSelectionText() const
{
    return m_selection.text;
}

QString UiStateCenter::currentSelectionSource() const
{
    return m_selection.source;
}

QString UiStateCenter::currentSelectionType() const
{
    return m_selection.type;
}

QString UiStateCenter::currentCommandOwner() const
{
    return m_command.commandOwner;
}

QString UiStateCenter::currentCommandType() const
{
    return m_command.commandType;
}

QString UiStateCenter::interactionKind() const
{
    return m_view.interactionKind;
}

int UiStateCenter::interactionPointerX() const
{
    return m_view.interactionPointerX;
}

int UiStateCenter::interactionPointerY() const
{
    return m_view.interactionPointerY;
}

int UiStateCenter::interactionKey() const
{
    return m_view.interactionKey;
}

bool UiStateCenter::busy() const
{
    return m_command.busyCount > 0;
}

bool UiStateCenter::dirty() const
{
    return m_command.dirty;
}

int UiStateCenter::progress() const
{
    return m_command.progress;
}

QString UiStateCenter::statusMessage() const
{
    return m_command.statusMessage;
}

QString UiStateCenter::statusPrompt() const
{
    return m_command.statusPrompt;
}

QString UiStateCenter::taskPhase() const
{
    return m_command.taskPhase;
}

int UiStateCenter::errorCode() const
{
    return m_command.errorCode;
}

QVariantMap UiStateCenter::metadata() const
{
    return m_view.metadata;
}

QString UiStateCenter::refreshState() const
{
    return m_view.refreshState;
}

QString UiStateCenter::activeToolId() const
{
    return m_workspace.activeToolId;
}

QString UiStateCenter::inputFocusWidget() const
{
    return m_workspace.inputFocusWidget;
}

void UiStateCenter::setRefreshState(const QString& state)
{
    if (m_view.refreshState == state)
    {
        return;
    }
    m_view.refreshState = state;
    emit refreshStateChanged(state);
    emit stateChanged();
}

void UiStateCenter::setInteractionState(const QString& kind, int pointerX, int pointerY, int key)
{
    if (m_view.interactionKind == kind && m_view.interactionPointerX == pointerX && m_view.interactionPointerY == pointerY &&
        m_view.interactionKey == key)
    {
        return;
    }

    m_view.interactionKind = kind;
    m_view.interactionPointerX = pointerX;
    m_view.interactionPointerY = pointerY;
    m_view.interactionKey = key;

    emit interactionStateChanged(kind, pointerX, pointerY, key);
    emit stateChanged();
}

void UiStateCenter::clearInteractionState()
{
    setInteractionState(QString(), -1, -1, -1);
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
    emit currentViewModeChanged(mode);
    emit stateChanged();
}

void UiStateCenter::setCurrentLayerId(const QString& layerId)
{
    if (m_view.layerId == layerId)
    {
        return;
    }

    m_view.layerId = layerId;
    emit currentLayerChanged(layerId);
    emit stateChanged();
}

void UiStateCenter::setLayerVisibilityState(bool visible)
{
    if (m_view.layerVisible == visible)
    {
        return;
    }
    m_view.layerVisible = visible;
    emit layerVisibilityChanged(visible);
    emit stateChanged();
}

bool UiStateCenter::layerVisibilityState() const
{
    return m_view.layerVisible;
}

void UiStateCenter::setLayerLockState(bool locked)
{
    if (m_view.layerLocked == locked)
    {
        return;
    }
    m_view.layerLocked = locked;
    emit layerLockChanged(locked);
    emit stateChanged();
}

bool UiStateCenter::layerLockState() const
{
    return m_view.layerLocked;
}

void UiStateCenter::setCurrentDocumentId(const QString& documentId)
{
    if (m_workspace.documentId == documentId)
    {
        return;
    }

    m_workspace.documentId = documentId;
    emit currentDocumentChanged(documentId);
    emit stateChanged();
}

void UiStateCenter::setCurrentCommandId(const QString& commandId)
{
    if (m_command.commandId == commandId)
    {
        return;
    }

    m_command.commandId = commandId;
    emit currentCommandChanged(commandId);
    emit stateChanged();
}

void UiStateCenter::setCurrentCommandPhase(const QString& phase)
{
    if (m_command.commandPhase == phase)
    {
        return;
    }

    m_command.commandPhase = phase;
    emit currentCommandPhaseChanged(phase);
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

void UiStateCenter::setBusy(bool busy)
{
    if (busy)
    {
        pushBusy();
    }
    else
    {
        popBusy();
    }
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

/// 统一设置命令失败状态，触发 commandFailed 信号
/// @param commandId 失败的命令 ID
/// @param message 失败原因描述
void UiStateCenter::setCommandFailed(const QString& commandId, const QString& message)
{
    m_command.failed = true;
    m_command.failedCommandId = commandId;
    m_command.failureMessage = message;

    // 不再镜像写 metadata：失败态唯一真相是上方三个成员 + commandFailed 信号

    emit commandFailed(commandId, message);
    emit stateChanged();
}

/// 清除命令失败状态
void UiStateCenter::clearCommandFailed()
{
    if (!m_command.failed)
    {
        return;
    }

    m_command.failed = false;
    m_command.failedCommandId.clear();
    m_command.failureMessage.clear();

    // 不再镜像写 metadata：与 setCommandFailed 对称移除

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

// DEPRECATED: 以下 API 无生产调用者，仅为测试保留（如需恢复请取消注释并补全实现）
// /// 统一设置任务进度和消息
// /// @param progress 进度值 (0-100)，-1 表示清除进度
// /// @param message 状态消息
// void UiStateCenter::setProgress(int progress, const QString& message)
// {
//     if (m_command.progress == progress && m_command.statusMessage == message)
//     {
//         return;
//     }
//
//     m_command.progress = progress;
//     m_command.statusMessage = message;
//
//     // 同步写入元数据，方便状态栏等展示层读取
//     m_view.metadata.insert(QStringLiteral("progress"), progress);
//     m_view.metadata.insert(QStringLiteral("statusMessage"), message);
//
//     emit progressChanged(progress, message);
//     emit stateChanged();
// }
//
// /// 设置任务阶段和消息
// /// @param phase 阶段标识
// /// @param message 阶段描述
// void UiStateCenter::setTaskPhase(const QString& phase, const QString& message)
// {
//     if (m_command.taskPhase == phase && m_command.statusMessage == message)
//     {
//         return;
//     }
//
//     m_command.taskPhase = phase;
//     m_command.statusMessage = message;
//
//     m_view.metadata.insert(QStringLiteral("taskPhase"), phase);
//     m_view.metadata.insert(QStringLiteral("statusMessage"), message);
//
//     emit taskPhaseChanged(phase, message);
//     emit stateChanged();
// }
//
// /// 统一设置错误状态
// /// @param code 错误码
// /// @param message 错误描述
// void UiStateCenter::setError(int code, const QString& message)
// {
//     m_command.errorCode = code;
//     m_command.statusMessage = message;
//
//     m_view.metadata.insert(QStringLiteral("errorCode"), code);
//     m_view.metadata.insert(QStringLiteral("errorMessage"), message);
//
//     emit errorOccurred(code, message);
//     emit stateChanged();
// }
//
// /// 清除错误状态
// void UiStateCenter::clearError()
// {
//     if (m_command.errorCode == 0 && m_command.statusMessage.isEmpty())
//     {
//         return;
//     }
//
//     m_command.errorCode = 0;
//     m_command.statusMessage.clear();
//
//     m_view.metadata.insert(QStringLiteral("errorCode"), 0);
//     m_view.metadata.insert(QStringLiteral("errorMessage"), QString());
//
//     emit stateChanged();
// }
//
// /// 清除任务进度和阶段（任务完成时调用）
// void UiStateCenter::clearTask()
// {
//     if (m_command.progress == -1 && m_command.taskPhase.isEmpty() && m_command.statusMessage.isEmpty())
//     {
//         return;
//     }
//
//     m_command.progress = -1;
//     m_command.taskPhase.clear();
//     m_command.statusMessage.clear();
//
//     m_view.metadata.insert(QStringLiteral("progress"), -1);
//     m_view.metadata.insert(QStringLiteral("taskPhase"), QString());
//     m_view.metadata.insert(QStringLiteral("statusMessage"), QString());
//
//     emit stateChanged();
// }

void UiStateCenter::setActiveToolId(const QString& toolId)
{
    if (m_workspace.activeToolId == toolId)
    {
        return;
    }

    m_workspace.activeToolId = toolId;
    emit activeToolChanged(toolId);
    emit stateChanged();
}

void UiStateCenter::setInputFocusWidget(const QString& widgetName)
{
    if (m_workspace.inputFocusWidget == widgetName)
    {
        return;
    }

    m_workspace.inputFocusWidget = widgetName;
    emit inputFocusWidgetChanged(widgetName);
    emit stateChanged();
}