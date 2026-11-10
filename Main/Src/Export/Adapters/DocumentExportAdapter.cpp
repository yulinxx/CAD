#include "DocumentExportAdapter.h"

#include "Engine2D/Core/SceneManager.h"
#include "Engine3D/SceneManager3D.h"
#include "Engine3D/AprEntity/AprMeshEntity.h"
#include "Log/AprLogger.h"

DocumentExportAdapter::DocumentExportAdapter(Eg::SceneManager* sceneManager,
    Eg::SceneManager3D* sceneManager3D)
    : m_sceneManager(sceneManager)
    , m_sceneManager3D(sceneManager3D)
{
}

Fio::VecAprEntityPtr DocumentExportAdapter::collect2D()
{
    Fio::VecAprEntityPtr entities;
    if (!m_sceneManager)
    {
        return entities;
    }

    auto allEntities = m_sceneManager->getAllEntities();
    entities.reserve(allEntities.size());
    for (auto* e : allEntities)
    {
        // ABI: clone 在 Engine2D 分配，跨 DLL delete 安全（需保证所有模块使用相同堆）
        entities.push_back(std::unique_ptr<Eg::AprEntity>(e->clone()));
    }

    APR_DEBUGF("[DocumentExportAdapter] Collected %d entities from 2D scene", (int)entities.size());
    return entities;
}

Fio::VecAprEntityPtr DocumentExportAdapter::collect3D()
{
    Fio::VecAprEntityPtr entities;
    if (!m_sceneManager3D)
    {
        return entities;
    }

    struct CollectCtx
    {
        Fio::VecAprEntityPtr* entities;
        size_t count;
    };

    CollectCtx ctx = { &entities, 0 };

    m_sceneManager3D->forEachEntity(
        [](Eg::AprMeshEntity* entity, void* userData) {
            if (entity)
            {
                auto* ctx = static_cast<CollectCtx*>(userData);
                ctx->entities->push_back(std::unique_ptr<Eg::AprEntity>(entity->clone()));
                ++ctx->count;
            }
        },
        &ctx);

    APR_DEBUGF("[DocumentExportAdapter] Collected %d entities from 3D scene", (int)entities.size());
    return entities;
}

Fio::VecAprEntityPtr DocumentExportAdapter::collectSelected()
{
    Fio::VecAprEntityPtr entities;
    if (!m_sceneManager)
    {
        return entities;
    }

    auto selectedEntities = m_sceneManager->getSelectedEntities();
    entities.reserve(selectedEntities.size());
    for (auto* e : selectedEntities)
    {
        // ABI: clone 在 Engine2D 分配，跨 DLL delete 安全（需保证所有模块使用相同堆）
        entities.push_back(std::unique_ptr<Eg::AprEntity>(e->clone()));
    }

    APR_DEBUGF("[DocumentExportAdapter] Collected %d selected entities", (int)entities.size());
    return entities;
}