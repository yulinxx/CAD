#include "DocumentExportAdapter.h"

#include "Engine2D/Core/SceneManager.h"
#include "Engine3D/SceneManager3D.h"
#include "Engine3D/SyEntity/SyMeshEntity.h"
#include "Log/SyLogger.h"

DocumentExportAdapter::DocumentExportAdapter(Eg::SceneManager* sceneManager,
    Eg::SceneManager3D* sceneManager3D)
    : m_sceneManager(sceneManager)
    , m_sceneManager3D(sceneManager3D)
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
        // ABI: clone 在 Engine2D 分配，跨 DLL delete 安全（需保证所有模块使用相同堆）
        entities.push_back(std::unique_ptr<Eg::SyEntity>(e->clone()));
    }

    SY_DEBUGF("[DocumentExportAdapter] Collected %d entities from 2D scene", (int)entities.size());
    return entities;
}

Fio::VecSyEntityPtr DocumentExportAdapter::collect3D()
{
    Fio::VecSyEntityPtr entities;
    if (!m_sceneManager3D)
    {
        return entities;
    }

    struct CollectCtx
    {
        Fio::VecSyEntityPtr* entities;
        size_t count;
    };

    CollectCtx ctx = { &entities, 0 };

    m_sceneManager3D->forEachEntity(
        [](Eg::SyMeshEntity* entity, void* userData) {
            if (entity)
            {
                auto* ctx = static_cast<CollectCtx*>(userData);
                ctx->entities->push_back(std::unique_ptr<Eg::SyEntity>(entity->clone()));
                ++ctx->count;
            }
        },
        &ctx);

    SY_DEBUGF("[DocumentExportAdapter] Collected %d entities from 3D scene", (int)entities.size());
    return entities;
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
        // ABI: clone 在 Engine2D 分配，跨 DLL delete 安全（需保证所有模块使用相同堆）
        entities.push_back(std::unique_ptr<Eg::SyEntity>(e->clone()));
    }

    SY_DEBUGF("[DocumentExportAdapter] Collected %d selected entities", (int)entities.size());
    return entities;
}