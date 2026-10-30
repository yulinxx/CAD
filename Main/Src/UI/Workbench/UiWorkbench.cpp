#include "UiWorkbench.h"

#include <QWidget>
#include <QVariantMap>

#include "Services/UiStateCenter.h"
#include "Log/SyLogger.h"

UiStateSnapshot UiWorkbench::currentSnapshot() const
{
    UiStateSnapshot snapshot = m_savedState;
    if (m_uiState.stateCenter)
    {
        auto snap = m_uiState.stateCenter->snapshot();
        snapshot.currentViewMode = snap.currentViewMode;
        snapshot.currentLayerId = snap.currentLayerId;
        snapshot.currentDocumentId = snap.currentDocumentId;
        snapshot.currentSelectionSource = snap.currentSelectionSource;
        snapshot.currentSelectionText = snap.currentSelectionText;
        snapshot.currentSelectionType = snap.currentSelectionType;
        snapshot.dirty = snap.dirty;
        snapshot.activeDocDirty = snap.activeDocDirty;
        // 工具/输入状态（工作台切换时恢复）
        snapshot.activeToolId = snap.activeToolId;
        snapshot.inputFocusWidget = snap.inputFocusWidget;
    }
    return snapshot;
}

void UiWorkbench::restoreFromSnapshot(const UiStateSnapshot& snapshot)
{
    if (m_uiState.stateCenter)
    {
        m_uiState.stateCenter->setCurrentViewMode(snapshot.currentViewMode);
        m_uiState.stateCenter->setCurrentLayerId(snapshot.currentLayerId);
        // currentDocumentId 刻意不恢复：它是全局单值的「活动文档」，
        // 快照里存的只是上次停用那一刻的旧拷贝，并不代表本工作台的文档 ——
        // 恢复它会把另一工作台刚打开/另存的文件名从标题里覆盖掉
        //（如 2D 打开文件 → 自动切 3D → 标题被 3D 上次停用时的旧值覆盖）。
        // 活动文档只由各文档源实时写入：2D FileOperationRegistry、3D DocumentManager3D。
        // 与 FrameworkRegressionTest「活动文档在切换时保持不变」的约定一致。
        m_uiState.stateCenter->setActiveToolId(snapshot.activeToolId);
        m_uiState.stateCenter->setInputFocusWidget(snapshot.inputFocusWidget);
    }
}

// ==================== 框架层委托接口（默认实现） ====================

bool UiWorkbench::isCommandRegistered(const QString& /*commandId*/) const
{
    return false;
}

void UiWorkbench::dispatchCommand(const QString& /*commandId*/, const QVariantMap& /*params*/)
{
    SY_WARN("[UiWorkbench] Command dispatch requested without a workbench adapter");
}

Cmd::OperationResultBase UiWorkbench::dispatchCommandForResult(
    const QString& commandId, const QVariantMap& /*params*/)
{
    Cmd::OperationResultBase result;
    result.message = QStringLiteral("Command dispatch without a workbench adapter: %1").arg(commandId);
    SY_WARNF("[UiWorkbench] Command dispatch with result requested without an adapter: %s", qPrintable(commandId));
    return result;
}

QString UiWorkbench::commandText(const QString& /*commandId*/) const
{
    return QString();
}

void UiWorkbench::releaseCentralWidgetGLResources(QWidget* /*centralWidget*/) const
{
    // 默认不释放任何资源，子类按需重写
}

bool UiWorkbench::requiresSkeletonDocks() const
{
    return true;
}

bool UiWorkbench::managesOwnMenus() const
{
    return false;
}

bool UiWorkbench::showSettingsDialog(QWidget* /*parent*/)
{
    return false;
}