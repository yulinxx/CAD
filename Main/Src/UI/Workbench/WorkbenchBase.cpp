#include "WorkbenchBase.h"

#include "WorkbenchWindow.h"
#include "WorkbenchMenuManager.h"
#include "UiSceneTreePanel.h"
#include "UiPropertiesPanel.h"
#include "UI/StatusBarBase.h"
#include "ClientConfig/UiContextMenuService.h"
#include "ClientConfig/UiConfigurationManager.h"
#include "ClientConfig/UiClientConfigBase.h"
#include "Log/SyLogger.h"

#include <QMenu>
#include <QTimer>

WorkbenchBase::WorkbenchBase(QObject* parent)
    : UiWorkbench(parent)
{
}

WorkbenchBase::~WorkbenchBase() = default;

bool WorkbenchBase::requiresSkeletonDocks() const
{
    return true;
}

bool WorkbenchBase::managesOwnMenus() const
{
    return false;
}

ConnectionGuard& WorkbenchBase::connections()
{
    return m_connections;
}

void WorkbenchBase::mountStatusBarWidget(StatusBarBase* statusBar)
{
    if (!m_workbenchWindow || !statusBar)
    {
        return;
    }
    m_workbenchWindow->mountStatusBar(statusBar);
}

void WorkbenchBase::unmountStatusBarWidget()
{
    if (!m_workbenchWindow)
    {
        return;
    }
    m_workbenchWindow->unmountStatusBar();
}

QMenu* WorkbenchBase::buildContextMenu(const QString& contextMenuId, bool /*hasSelection*/)
{
    const UiConfigData* config = UiConfigurationManager::shared().configData();
    if (!UiContextMenuService::hasConfigFor(config, contextMenuId))
    {
        return nullptr;
    }

    auto* dispatcher = m_workbenchWindow ? m_workbenchWindow->menuManager()->commandDispatcher()
                                         : static_cast<IUiCommandDispatcher*>(this);
    return UiContextMenuService::instance().buildMenu(config, contextMenuId, dispatcher, nullptr);
}

void WorkbenchBase::schedulePropertiesPanelRefresh()
{
    if (!m_propertiesRefreshTimer)
    {
        m_propertiesRefreshTimer = new QTimer(this);
        m_propertiesRefreshTimer->setSingleShot(true);
        m_propertiesRefreshTimer->setInterval(m_propertiesRefreshIntervalMs);
        QObject::connect(m_propertiesRefreshTimer, &QTimer::timeout, this, [this]() {
            refreshPropertiesPanel();
        });
    }
    if (m_propertiesRefreshTimer->isActive())
    {
        return;
    }
    m_propertiesRefreshTimer->start();
}

void WorkbenchBase::cleanupPropertiesPanelRefreshTimer()
{
    if (m_propertiesRefreshTimer)
    {
        m_propertiesRefreshTimer->stop();
        m_propertiesRefreshTimer->deleteLater();
        m_propertiesRefreshTimer = nullptr;
    }
}

void WorkbenchBase::scheduleSceneTreeRefresh()
{
    if (!m_sceneTreeRefreshTimer)
    {
        m_sceneTreeRefreshTimer = new QTimer(this);
        m_sceneTreeRefreshTimer->setSingleShot(true);
        m_sceneTreeRefreshTimer->setInterval(m_sceneTreeRefreshIntervalMs);
        QObject::connect(m_sceneTreeRefreshTimer, &QTimer::timeout, this, [this]() {
            applySceneTreeIncremental();
        });
    }
    m_sceneTreeRefreshTimer->start();
}

void WorkbenchBase::cleanupSceneTreeRefreshTimer()
{
    if (m_sceneTreeRefreshTimer)
    {
        m_sceneTreeRefreshTimer->stop();
        m_sceneTreeRefreshTimer->deleteLater();
        m_sceneTreeRefreshTimer = nullptr;
    }
}

void WorkbenchBase::setSceneTreeRefreshInterval(int intervalMs)
{
    m_sceneTreeRefreshIntervalMs = intervalMs;
}

int WorkbenchBase::sceneTreeRefreshInterval() const
{
    return m_sceneTreeRefreshIntervalMs;
}

void WorkbenchBase::setPropertiesRefreshInterval(int intervalMs)
{
    m_propertiesRefreshIntervalMs = intervalMs;
}

int WorkbenchBase::propertiesRefreshInterval() const
{
    return m_propertiesRefreshIntervalMs;
}

void WorkbenchBase::resetSceneTreeState()
{
    m_lastSceneTreeEntityCount = 0;
    m_lastSceneTreeStructureRevision = 0;
    m_sceneTreeCursor = 0;
    m_sceneTreeIncrementalBusy = false;
}