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
        m_uiState.stateCenter->setCurrentDocumentId(snapshot.currentDocumentId);
        // 工具/输入状态恢复（仅写入状态中心，子类 activate() 负责应用到视口）
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
