#include "DocumentExportAdapter.h"

#include "Engine2D/Core/SceneManager.h"
#include "Log/SyLogger.h"

DocumentExportAdapter::DocumentExportAdapter(Eg::SceneManager* sceneManager)
    : m_sceneManager(sceneManager)
{
}

Fio::VecSyEntityPtr DocumentExportAdapter::collect2D()
{
    Fio::VecSyEntityPtr entities;
    if (!m_sceneManager)
    {
        return entities;
    }

    auto allEntities = m_sceneManager->getAllEntities();
    entities.reserve(allEntities.size());
    for (auto* e : allEntities)
    {
        // ABI: clone 在 Engine2D 分配，/MD 共享堆下跨 DLL delete 安全
        entities.push_back(std::unique_ptr<Eg::SyEntity>(e->clone()));
    }

    SY_DEBUGF("[DocumentExportAdapter] Collected %d entities from 2D scene", (int)entities.size());
    return entities;
}

Fio::VecSyEntityPtr DocumentExportAdapter::collect3D()
{
    // 3D 场景收集，后续由 3D 场景管理器接管
    return collect2D();
}

Fio::VecSyEntityPtr DocumentExportAdapter::collectSelected()
{
    Fio::VecSyEntityPtr entities;
    if (!m_sceneManager)
    {
        return entities;
    }

    auto selectedEntities = m_sceneManager->getSelectedEntities();
    entities.reserve(selectedEntities.size());
    for (auto* e : selectedEntities)
    {
        // ABI: clone 在 Engine2D 分配，/MD 共享堆下跨 DLL delete 安全
        entities.push_back(std::unique_ptr<Eg::SyEntity>(e->clone()));
    }

    SY_DEBUGF("[DocumentExportAdapter] Collected %d selected entities", (int)entities.size());
    return entities;
}