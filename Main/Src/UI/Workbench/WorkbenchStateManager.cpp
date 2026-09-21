/**
 * @file WorkbenchStateManager.cpp
 * @brief 工作台状态管理器实现
 *
 * 管理工作台状态（窗口位置、面板可见性等）。
 */
#include "WorkbenchStateManager.h"
#include "WorkbenchMenuManager.h"
#include "WorkbenchLayoutManager.h"
#include "WorkbenchWindow.h"
#include "UiWorkbench.h"
#include "UiStateCenter.h"
#include "UiPropertiesPanel.h"
#include "Services/UiFrameworkServices.h"
#include "Log/SyLogger.h"
#include "UI/StatusBarBase.h"
#include "VersionInfo.h"

#include <QFileInfo>
#include <QLabel>
#include <QMainWindow>
#include <QObject>
#include <QRegularExpression>

WorkbenchStateManager::WorkbenchStateManager(
    WorkbenchWindow* parent, WorkbenchMenuManager* menuManager, WorkbenchLayoutManager* layoutManager)
    : m_parent(parent)
    , m_menuManager(menuManager)
    , m_layoutManager(layoutManager)
{
    // 单次定时器：同一事件循环内的多次信号只触发一次刷新
    m_refreshCoalescer.setSingleShot(true);
    m_refreshCoalescer.setInterval(0);
    QObject::connect(&m_refreshCoalescer, &QTimer::timeout, &m_refreshCoalescer, [this]() {
        doRefreshFromState();
    });
}

WorkbenchStateManager::~WorkbenchStateManager() = default;

// ==================== 服务注入 ====================

void WorkbenchStateManager::setUiStateCenter(UiStateCenter* stateCenter)
{
    // 状态中心入口只负责替换源头引用，不在这里做额外状态编排
    unbindStateSignals();
    m_stateCenter = stateCenter;
    bindStateSignals();
    SY_DEBUGF("[WorkbenchStateManager] State center set: %p", static_cast<void*>(stateCenter));
}

void WorkbenchStateManager::setFrameworkServices(const UiFrameworkServices& services)
{
    // 框架级能力统一从这里注入，后续错误、权限、性能都必须走同一条框架路径
    m_frameworkServices = services;
}

void WorkbenchStateManager::setActiveStatusBar(StatusBarBase* statusBarWidget)
{
    m_activeStatusBar = statusBarWidget;
}

// ==================== 状态同步 ====================

void WorkbenchStateManager::bindStateSignals()
{
    // 只绑定状态中心信号，不在这里做任何状态初始化或业务编排
    if (!m_stateCenter)
    {
        SY_DEBUG("[WorkbenchStateManager] bindStateSignals: stateCenter is null, skip binding");
        return;
    }

    SY_DEBUG("[WorkbenchStateManager] Binding state center signals");
    // 信号连接只用于刷新入口，不在这里插入额外的状态派生逻辑
    // 使用 m_parent 作为连接上下文，解绑时可精确断开本管理器挂到状态中心上的连接，
    // 不会误伤其他组件（如 UiWorkbench）对状态中心的监听
    QObject::connect(m_stateCenter, &UiStateCenter::stateChanged, m_parent, [this]() {
        m_refreshCoalescer.start();
    });
    QObject::connect(m_stateCenter, &UiStateCenter::busyChanged, m_parent, [this](bool) {
        m_refreshCoalescer.start();
    });
    QObject::connect(m_stateCenter, &UiStateCenter::dirtyChanged, m_parent, [this](bool) {
        m_refreshCoalescer.start();
    });
}

void WorkbenchStateManager::unbindStateSignals()
{
    // 解绑只做信号断开，避免把清理逻辑混进来
    // 只断开接收者为窗口(m_parent)的连接，不全局断开发送者，避免影响其他组件监听
    if (!m_stateCenter)
    {
        return;
    }

    SY_DEBUG("[WorkbenchStateManager] Unbinding state center signals");
    QObject::disconnect(m_stateCenter, nullptr, m_parent, nullptr);
}

void WorkbenchStateManager::syncWindowStateFromStateCenter(const UiStateSnapshot& state)
{
    m_windowState.currentWorkbenchId = state.currentWorkbenchId;
    m_windowState.currentThemeId = state.currentThemeId;
    m_windowState.busy = state.busy;
}

void WorkbenchStateManager::syncWindowStateFromStateCenter()
{
    if (!m_stateCenter)
    {
        return;
    }
    syncWindowStateFromStateCenter(m_stateCenter->snapshot());
}

void WorkbenchStateManager::syncWorkbenchSelectionFromStateCenter(const UiStateSnapshot& state)
{
    m_windowState.currentSelectionText = state.currentSelectionText;
    m_windowState.currentSelectionSource = state.currentSelectionSource;
    m_windowState.currentSelectionType = state.currentSelectionType;
}

void WorkbenchStateManager::syncWorkbenchSelectionFromStateCenter()
{
    if (!m_stateCenter)
    {
        return;
    }
    syncWorkbenchSelectionFromStateCenter(m_stateCenter->snapshot());
}

// ==================== UI 刷新 ====================

void WorkbenchStateManager::refreshStatusText(const UiStateSnapshot& state)
{
    m_layoutManager->updateBusyIndicator(state.busy);
}

void WorkbenchStateManager::refreshStatusText()
{
    if (!m_stateCenter)
    {
        return;
    }
    refreshStatusText(m_stateCenter->snapshot());
}

void WorkbenchStateManager::refreshFromState()
{
    doRefreshFromState();
}

void WorkbenchStateManager::doRefreshFromState()
{
    if (!m_stateCenter)
    {
        return;
    }

    SY_DEBUG("[WorkbenchStateManager] Refreshing from state center");

    // 取一次 snapshot，所有子方法复用，避免 3-4 次重复拷贝
    const auto state = m_stateCenter->snapshot();

    syncWindowStateFromStateCenter(state);
    syncWorkbenchSelectionFromStateCenter(state);
    refreshStatusText(state);
    updateWindowTitle(state);

    // 统一更新状态栏消息和选择信息（通过 StatusBarBase 接口，不直接操作裸 QLabel）
    if (m_activeStatusBar)
    {
        QString prompt = state.statusPrompt;
        if (prompt.isEmpty())
        {
            prompt = state.metadata.value(QStringLiteral("statusPrompt")).toString();
        }
        if (prompt.isEmpty())
        {
            prompt = m_parent->tr("Ready");
        }
        m_activeStatusBar->setMessageText(prompt);
    }

    const auto& panel = m_layoutManager->panelState();

    // 注意：posLabel/selLabel/msgLabel 已移除 —— 这些由 StatusBarBase 子类管理，
    // 状态栏消息与选择信息在上方通过 m_activeStatusBar 接口统一更新

    // 菜单勾选态不在这里推：配置驱动菜单已连到状态中心的 stateChanged / metadataChanged，
    // 由 WorkbenchMenuManager::refreshConfiguredMenuState 单点同步。
}

void WorkbenchStateManager::updateWindowTitle(const UiStateSnapshot& state)
{
    QString docFile;
    QString docId = state.currentDocumentId;
    if (!docId.isEmpty() && docId != QStringLiteral("none"))
    {
        QFileInfo fi(docId);
        docFile = fi.fileName();
    }

    QString title;
    if (docFile.isEmpty())
    {
        title =
            QStringLiteral("%1 - %2 - %3")
                .arg(QString::fromStdString(MainApp::appName()), state.currentWorkbenchId, state.currentViewMode);
    }
    else
    {
        title = QStringLiteral("%1 - %2 [%3 - %4]")
                    .arg(docFile,
                        QString::fromStdString(MainApp::appName()),
                        state.currentWorkbenchId,
                        state.currentViewMode);
    }

    if (state.dirty)
    {
        title.prepend(QStringLiteral("* "));
    }
    m_parent->setWindowTitle(title);
}

void WorkbenchStateManager::updateWindowTitle()
{
    if (!m_stateCenter)
    {
        return;
    }
    updateWindowTitle(m_stateCenter->snapshot());
}

// ==================== 工作台切换状态收尾 ====================

void WorkbenchStateManager::resetCommandStateToIdle()
{
    if (!m_stateCenter)
    {
        return;
    }

    // 统一把命令状态清回 idle，避免切换和收尾流程各自写一套
    m_stateCenter->setCurrentCommandId(QStringLiteral("idle"));
    m_stateCenter->setCurrentCommandPhase(QStringLiteral("idle"));
    m_stateCenter->setCurrentCommandOwner(QStringLiteral("none"));
    m_stateCenter->setCurrentCommandType(QStringLiteral("none"));
}

void WorkbenchStateManager::resetWorkbenchLocalMirror()
{
    // 本地镜像只做清空，不向状态中心写额外语义
    m_windowState.busy = false;
    m_windowState.currentWorkbenchId = QStringLiteral("default");
    m_windowState.currentThemeId = QStringLiteral("system");
    m_windowState.currentSelectionText.clear();
    m_windowState.currentSelectionSource.clear();
    m_windowState.currentSelectionType.clear();
    m_windowState.currentCommandId.clear();
    m_windowState.currentCommandPhase.clear();
}

void WorkbenchStateManager::clearSelectionState()
{
    if (!m_stateCenter)
    {
        return;
    }

    // 清空选择相关状态，避免工作台切换后沿用旧选择文本
    m_stateCenter->setCurrentSelectionText(QString());
    m_stateCenter->setSelectionContext(QStringLiteral("none"), QString());
    m_stateCenter->setMetadata({ { QStringLiteral("selectionSource"), QStringLiteral("none") },
        { QStringLiteral("selectionText"), QString() },
        { QStringLiteral("selectionType"), QStringLiteral("none") } });
}

void WorkbenchStateManager::setWorkbenchSwitchContext(const QString& workbenchId, const QString& switchContextText)
{
    if (!m_stateCenter)
    {
        SY_DEBUGF("[WorkbenchStateManager] setWorkbenchSwitchContext: stateCenter is null, workbenchId=%s",
            qPrintable(workbenchId));
        return;
    }

    SY_DEBUGF("[WorkbenchStateManager] Setting workbench switch context: %s, text=%s", qPrintable(workbenchId),
        qPrintable(switchContextText));
    // 工作台切换上下文统一在这里写入，避免 triggerWorkbench 里散落重复设置
    // 这里只写切换语义，不混入命令态和主题态
    m_stateCenter->setCurrentWorkbenchId(workbenchId);
    m_stateCenter->setSelectionContext(QStringLiteral("Workbench-Switch"), switchContextText);
    m_stateCenter->setStatusPrompt(switchContextText);

    // metadata 采用读-改-写，避免整体替换丢掉 statusPrompt/workbenchId/switchContext 等既有键
    QVariantMap meta = m_stateCenter->metadata();
    meta.insert(QStringLiteral("workbenchId"), workbenchId);
    meta.insert(QStringLiteral("switchContext"), switchContextText);
    meta.insert(QStringLiteral("selectionSource"), QStringLiteral("Workbench-Switch"));
    meta.insert(QStringLiteral("selectionText"), switchContextText);
    meta.insert(QStringLiteral("selectionType"), QStringLiteral("none"));
    m_stateCenter->setMetadata(meta);
}

void WorkbenchStateManager::setWorkbenchTransitionState(const QString& phase, const QString& status)
{
    if (!m_stateCenter)
    {
        return;
    }

    // 工作台切换阶段只更新少量明确字段，避免把过渡态扩散到 metadata 的其他用途里
    m_stateCenter->setStatusPrompt(status);

    QVariantMap meta = m_stateCenter->metadata();
    meta.insert(QStringLiteral("workbenchTransitionPhase"), phase);
    meta.insert(QStringLiteral("workbenchTransitionStatus"), status);
    m_stateCenter->setMetadata(meta);
}

void WorkbenchStateManager::resetWorkbenchTransientState()
{
    SY_DEBUG("[WorkbenchStateManager] Resetting transient workbench state");
    m_windowState.busy = false;

    if (m_stateCenter)
    {
        // 工作台切换收尾只做"清空/归零"，不在这里引入新的状态来源
        m_stateCenter->setBusy(false);
        resetCommandStateToIdle();
        setWorkbenchTransitionState(QStringLiteral("reset"), QStringLiteral("Idle"));
        // metadata 采用读-改-写，保留 statusPrompt 等既有键
        QVariantMap meta = m_stateCenter->metadata();
        meta.insert(QStringLiteral("workbenchId"), QStringLiteral("none"));
        meta.insert(QStringLiteral("commandType"), QStringLiteral("none"));
        meta.insert(QStringLiteral("commandState"), QStringLiteral("idle"));
        meta.insert(QStringLiteral("selectionSource"), QStringLiteral("none"));
        meta.insert(QStringLiteral("selectionText"), QString());
        meta.insert(QStringLiteral("selectionType"), QStringLiteral("none"));
        meta.insert(QStringLiteral("viewportStatus"), QStringLiteral("Idle"));
        meta.insert(QStringLiteral("rightPanelSource"), QStringLiteral("none"));
        meta.insert(QStringLiteral("drawToolSource"), QStringLiteral("none"));
        meta.insert(QStringLiteral("activeToolId"), QString());
        m_stateCenter->setMetadata(meta);
        clearSelectionState();
        // 保留 dirty 标记：切换工作台不应清除"未保存"状态
    }
    // 本地镜像收尾单独处理，避免状态中心清理和窗口镜像清理混在一起
    resetWorkbenchLocalMirror();
}