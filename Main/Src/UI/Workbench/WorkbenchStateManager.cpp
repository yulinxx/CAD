#include "WorkbenchStateManager.h"
#include "WorkbenchMenuManager.h"
#include "WorkbenchLayoutManager.h"
#include "WorkbenchWindow.h"
#include "UiWorkbench.h"
#include "UiStateCenter.h"
#include "UiThemeService.h"
#include "UiPropertiesPanel.h"
#include "Services/UiServices.h"
#include "Services/UiFrameworkServices.h"
#include "Log/SyLogger.h"
#include "UI/StatusBarBase.h"
#include "VersionInfo.h"

#include <QFileInfo>
#include <QLabel>
#include <QMainWindow>
#include <QObject>
#include <QRegularExpression>

WorkbenchStateManager::WorkbenchStateManager(WorkbenchWindow *parent, WorkbenchMenuManager *menuManager,
                                             WorkbenchLayoutManager *layoutManager)
    : m_parent(parent), m_menuManager(menuManager), m_layoutManager(layoutManager)
{
}

WorkbenchStateManager::~WorkbenchStateManager() = default;

// ==================== 服务注入 ====================

void WorkbenchStateManager::setUiStateCenter(UiStateCenter *stateCenter)
{
    // 状态中心入口只负责替换源头引用，不在这里做额外状态编排
    unbindStateSignals();
    m_stateCenter = stateCenter;
    m_uiServices.stateCenter = stateCenter;
    bindStateSignals();
}

void WorkbenchStateManager::setThemeService(UiThemeService *themeService)
{
    // 主题服务入口只替换引用，不在这里主动触发主题加载或界面刷新
    m_themeService = themeService;
    m_uiServices.themeService = themeService;
}

void WorkbenchStateManager::setFrameworkServices(const UiFrameworkServices &services)
{
    // 框架级能力统一从这里注入，后续错误、权限、性能都必须走同一条框架路径
    m_frameworkServices = services;
}

void WorkbenchStateManager::setActiveStatusBar(StatusBarBase *statusBarWidget)
{
    m_activeStatusBar = statusBarWidget;
}

void WorkbenchStateManager::configureServices(const UiServices &services)
{
    unbindStateSignals();
    m_uiServices = services;
    m_stateCenter = services.stateCenter;
    m_themeService = services.themeService;

    if (m_menuManager)
    {
        m_menuManager->setOperationBus(services.operationBus);
        m_menuManager->setStateCenter(services.stateCenter);
        m_menuManager->setThemeService(services.themeService);
        m_menuManager->setUiServices(&m_uiServices);
        m_menuManager->rebuildAllMenus();
    }

    bindStateSignals();
}

// ==================== 状态同步 ====================

void WorkbenchStateManager::bindStateSignals()
{
    // 只绑定状态中心信号，不在这里做任何状态初始化或业务编排
    if (!m_stateCenter)
        return;

    // 信号连接只用于刷新入口，不在这里插入额外的状态派生逻辑
    // 使用 m_parent 作为连接上下文，解绑时可精确断开本管理器挂到状态中心上的连接，
    // 不会误伤其他组件（如 UiWorkbench）对状态中心的监听
    QObject::connect(m_stateCenter, &UiStateCenter::stateChanged, m_parent, [this]() { refreshFromState(); });
    QObject::connect(m_stateCenter, &UiStateCenter::busyChanged, m_parent, [this](bool) { refreshFromState(); });
    QObject::connect(m_stateCenter, &UiStateCenter::dirtyChanged, m_parent, [this](bool) { refreshFromState(); });
}

void WorkbenchStateManager::unbindStateSignals()
{
    // 解绑只做信号断开，避免把清理逻辑混进来
    // 只断开接收者为窗口(m_parent)的连接，不全局断开发送者，避免影响其他组件监听
    if (!m_stateCenter)
        return;

    QObject::disconnect(m_stateCenter, nullptr, m_parent, nullptr);
}

void WorkbenchStateManager::syncWindowStateFromStateCenter()
{
    if (!m_stateCenter)
        return;

    const auto state = m_stateCenter->snapshot();
    // 以状态中心为准同步窗口层状态，避免窗口本地状态与全局状态漂移
    m_windowState.workbenchId = state.currentWorkbenchId;
    m_windowState.themeId = state.currentThemeId;
    m_windowState.busy = state.busy;
}

void WorkbenchStateManager::syncWorkbenchSelectionFromStateCenter()
{
    if (!m_stateCenter)
        return;

    const auto state = m_stateCenter->snapshot();
    // 选择上下文单独同步，避免刷新状态栏时把选择语义和窗口语义混在一起
    m_windowState.selectionText = state.currentSelectionText;
    m_windowState.selectionSource = state.currentSelectionSource;
    m_windowState.selectionType = state.currentSelectionType;
}

// ==================== UI 刷新 ====================

void WorkbenchStateManager::refreshStatusText()
{
    // 这里只刷新全局状态展示，不在此处拼接工作台业务流程
    if (m_stateCenter)
    {
        syncWindowStateFromStateCenter();
        const auto state = m_stateCenter->snapshot();
        const auto &panel = m_layoutManager->panelState();

        if (panel.workbenchLabel)
        {
            // 从状态提示（三级回退）读取统一状态提示
            QString statusPrompt = state.statusPrompt;
            if (statusPrompt.isEmpty())
                statusPrompt = state.metadata.value(QStringLiteral("statusPrompt")).toString();
            if (statusPrompt.isEmpty())
                statusPrompt = m_parent->tr("Ready");

            // 提取文件名用于状态栏展示，完整路径放在 tooltip 中
            QString docDisplay = state.currentDocumentId;
            QString docTooltip;
            if (!docDisplay.isEmpty() && docDisplay != QStringLiteral("none"))
            {
                QFileInfo fi(docDisplay);
                docTooltip = docDisplay;
                docDisplay = fi.fileName();
            }

            panel.workbenchLabel->setText(
                m_parent->tr("WB:%1 | Doc:%2 | Cmd:%3(%4) | Layer:%5 | View:%6 | Dirty:%7 | %8")
                    .arg(state.currentWorkbenchId)
                    .arg(docDisplay)
                    .arg(state.currentCommandId)
                    .arg(state.currentCommandPhase)
                    .arg(state.currentLayerId)
                    .arg(state.currentViewMode)
                    .arg(state.dirty ? m_parent->tr("Y") : m_parent->tr("N"))
                    .arg(statusPrompt));
            panel.workbenchLabel->setToolTip(docTooltip);
        }

        if (panel.busyLabel)
            panel.busyLabel->setText(state.busy ? m_parent->tr("Busy") : m_parent->tr("Idle"));

        // 这里仍然只做展示，不把状态写回状态中心，避免循环同步
        m_layoutManager->updateBusyIndicator(state.busy);
        return;
    }

    const auto &panel = m_layoutManager->panelState();
    if (panel.workbenchLabel)
        panel.workbenchLabel->setText(m_parent->tr("Workbench: %1").arg(m_windowState.workbenchId));
    if (panel.busyLabel)
        panel.busyLabel->setText(m_windowState.busy ? m_parent->tr("Busy") : m_parent->tr("Idle"));
    m_layoutManager->updateBusyIndicator(m_windowState.busy);
}

void WorkbenchStateManager::refreshFromState()
{
    // 这里是框架层的总刷新入口，不把工作台实现逻辑写进来
    syncWindowStateFromStateCenter();
    syncWorkbenchSelectionFromStateCenter();
    // 刷新状态栏前先同步本地镜像，避免展示时读到半更新状态
    refreshStatusText();
    updateWindowTitle();

    // 统一更新状态栏消息和选择信息（通过 StatusBarBase 接口，不直接操作裸 QLabel）
    if (m_stateCenter && m_activeStatusBar)
    {
        const auto state = m_stateCenter->snapshot();
        QString prompt = state.statusPrompt;
        if (prompt.isEmpty())
            prompt = state.metadata.value(QStringLiteral("statusPrompt")).toString();
        if (prompt.isEmpty())
            prompt = m_parent->tr("Ready");
        m_activeStatusBar->setMessageText(prompt);
    }
    if (m_activeStatusBar && m_stateCenter)
    {
        const auto state = m_stateCenter->snapshot();
        const QString selectionText = state.currentSelectionText.trimmed();
        int selectedCount = 0;

        if (selectionText.isEmpty() || selectionText == QStringLiteral("none"))
        {
            selectedCount = 0;
        }
        else if (selectionText.contains(QStringLiteral("entities selected")))
        {
            QRegularExpression re(QStringLiteral("(\\d+)"));
            auto match = re.match(selectionText);
            if (match.hasMatch())
                selectedCount = match.captured(1).toInt();
        }
        else
        {
            selectedCount = 1;
        }

        m_activeStatusBar->setSelectionInfo(selectedCount, m_parent->tr("Selected: %1").arg(selectedCount));
    }

    const auto &panel = m_layoutManager->panelState();

    // 注意：posLabel/selLabel/msgLabel 已移除 —— 这些由 StatusBarBase 子类管理，
    // 状态栏消息与选择信息在上方通过 m_activeStatusBar 接口统一更新

    if (m_menuManager)
    {
        const auto state = m_stateCenter->snapshot();
        m_menuManager->refreshWorkbenchMenuChecks(state.currentWorkbenchId);
        m_menuManager->refreshThemeMenuChecks(state.currentThemeId);
        m_menuManager->refreshGridSnapMenuChecks();
    }
    // 属性面板使用状态中心快照作为输入，不在这里额外拼接窗口本地状态
    if (panel.propertiesDock && m_stateCenter)
    {
        const auto state = m_stateCenter->snapshot();
        // 属性面板同样以状态中心为准，避免单独维护一套展示状态
        QString statusPrompt = state.statusPrompt;
        if (statusPrompt.isEmpty())
            statusPrompt = state.metadata.value(QStringLiteral("statusPrompt")).toString();
        if (statusPrompt.isEmpty())
            statusPrompt = m_parent->tr("Ready");
        panel.propertiesDock->setStateText(
            m_parent->tr("WB=%1 | View=%2 | Cmd=%3(%4) | Dirty=%5 | Layer=%6 | Doc=%7 | Busy=%8 | %9")
                .arg(state.currentWorkbenchId)
                .arg(state.currentViewMode)
                .arg(state.currentCommandId)
                .arg(state.currentCommandPhase)
                .arg(state.dirty ? m_parent->tr("Y") : m_parent->tr("N"))
                .arg(state.currentLayerId)
                .arg(state.currentDocumentId)
                .arg(state.busy ? m_parent->tr("Y") : m_parent->tr("N"))
                .arg(statusPrompt));

        // 选择文本格式化委托给当前工作台（2D/3D 各自定义格式）
        QString selectionText;
        if (auto *wb = m_parent->currentWorkbench())
            selectionText = wb->formatSelectionText(state);
        else
            selectionText = m_parent->tr("Sel=%1 | SelSrc=%2 | CmdSrc=%3 | SelType=%4 | CmdType=%5")
                                .arg(state.currentSelectionText)
                                .arg(state.currentSelectionSource)
                                .arg(state.currentCommandOwner)
                                .arg(state.currentSelectionType)
                                .arg(state.currentCommandType);
        panel.propertiesDock->setSelectionText(selectionText);
    }
}

void WorkbenchStateManager::updateWindowTitle()
{
    if (m_stateCenter)
    {
        const auto state = m_stateCenter->snapshot();

        // 提取文档文件名用于窗口标题
        QString docFile;
        QString docId = state.currentDocumentId;
        if (!docId.isEmpty() && docId != QStringLiteral("none"))
        {
            QFileInfo fi(docId);
            docFile = fi.fileName();
        }

        QString title;
        if (docFile.isEmpty())
            title =
                QStringLiteral("%1 - %2 - %3")
                    .arg(QString::fromStdString(MainApp::appName()), state.currentWorkbenchId, state.currentViewMode);
        else
            title = QStringLiteral("%1 - %2 [%3 - %4]")
                        .arg(docFile, QString::fromStdString(MainApp::appName()), state.currentWorkbenchId,
                             state.currentViewMode);

        if (state.dirty)
            title.prepend(QStringLiteral("* "));
        m_parent->setWindowTitle(title);
        return;
    }

    m_parent->setWindowTitle(
        QStringLiteral("%1 - %2").arg(QString::fromStdString(MainApp::appName()), m_windowState.workbenchId));
}

// ==================== 工作台切换状态收尾 ====================

void WorkbenchStateManager::resetCommandStateToIdle()
{
    if (!m_stateCenter)
        return;

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
    m_windowState.workbenchId = QStringLiteral("default");
    m_windowState.themeId = QStringLiteral("system");
    m_windowState.selectionText.clear();
    m_windowState.selectionSource.clear();
    m_windowState.selectionType.clear();
}

void WorkbenchStateManager::clearSelectionState()
{
    if (!m_stateCenter)
        return;

    // 清空选择相关状态，避免工作台切换后沿用旧选择文本
    m_stateCenter->setCurrentSelectionText(QString());
    m_stateCenter->setSelectionContext(QStringLiteral("none"), QString());
    m_stateCenter->setMetadata({{QStringLiteral("selectionSource"), QStringLiteral("none")},
                                {QStringLiteral("selectionText"), QString()},
                                {QStringLiteral("selectionType"), QStringLiteral("none")}});
}

void WorkbenchStateManager::setWorkbenchSwitchContext(const QString &workbenchId, const QString &switchContextText)
{
    if (!m_stateCenter)
        return;

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

void WorkbenchStateManager::setWorkbenchTransitionState(const QString &phase, const QString &status)
{
    if (!m_stateCenter)
        return;

    // 工作台切换阶段只更新少量明确字段，避免把过渡态扩散到 metadata 的其他用途里
    m_stateCenter->setStatusPrompt(status);

    QVariantMap meta = m_stateCenter->metadata();
    meta.insert(QStringLiteral("workbenchTransitionPhase"), phase);
    meta.insert(QStringLiteral("workbenchTransitionStatus"), status);
    m_stateCenter->setMetadata(meta);
}

void WorkbenchStateManager::resetWorkbenchTransientState()
{
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
        m_stateCenter->setMetadata(meta);
        clearSelectionState();
        // 保留 dirty 标记：切换工作台不应清除"未保存"状态
    }
    // 本地镜像收尾单独处理，避免状态中心清理和窗口镜像清理混在一起
    resetWorkbenchLocalMirror();
}