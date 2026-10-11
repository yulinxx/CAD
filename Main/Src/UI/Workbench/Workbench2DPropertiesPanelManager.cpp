#include "Workbench2DPropertiesPanelManager.h"

#include "WorkbenchWindow.h"
#include "UiPropertiesPanel.h"
#include "WorkbenchTiming.h"
#include "UI2D/Operation/CommandActionHub.h"
#include "UI2D/Service/EntityPropertyEditSession2D.h"
#include "Engine2D/Edit/SceneEditService.h"
#include "Engine2D/Core/SceneManager.h"
#include "Engine/SyEntity/SyEntity.h"
#include "Log/SyLogger.h"

Workbench2DPropertiesPanelManager::Workbench2DPropertiesPanelManager() = default;

Workbench2DPropertiesPanelManager::~Workbench2DPropertiesPanelManager() = default;

void Workbench2DPropertiesPanelManager::setup(
    WorkbenchWindow* window, SceneEditService* editService, CommandActionHub* hub)
{
    m_window = window;
    m_editService = editService;
    m_hub = hub;
}

void Workbench2DPropertiesPanelManager::scheduleRefresh()
{
    if (!m_refreshTimer)
    {
        m_refreshTimer = new QTimer(this);
        m_refreshTimer->setSingleShot(true);
        m_refreshTimer->setInterval(WorkbenchTiming::kPropertiesDebounceMs);
        QObject::connect(m_refreshTimer, &QTimer::timeout, this, [this]() {
            refresh();
        });
    }
    if (m_refreshTimer->isActive())
    {
        return;
    }
    m_refreshTimer->start();
}

void Workbench2DPropertiesPanelManager::refresh()
{
    if (!m_window)
    {
        return;
    }
    auto* props = m_window->propertiesDock();
    if (!props)
    {
        return;
    }

    std::vector<Eg::EntityId> entityIds;
    if (m_editService)
    {
        if (auto* scene = m_editService->sceneManager())
        {
            for (Eg::SyEntity* e : scene->getSelectedEntities())
            {
                if (e)
                {
                    entityIds.push_back(e->id);
                }
            }
        }
    }

    auto session = std::make_shared<EntityPropertyEditSession2D>(m_editService, std::move(entityIds));

    props->setEditTarget(session);
    props->setPropertyModel(session->buildModel());

    if (m_hub)
    {
        props->setLockState(m_hub->currentSnapshot().anyLocked());
    }
}

void Workbench2DPropertiesPanelManager::shutdown()
{
    if (m_refreshTimer)
    {
        m_refreshTimer->stop();
        m_refreshTimer->deleteLater();
        m_refreshTimer = nullptr;
    }
}
