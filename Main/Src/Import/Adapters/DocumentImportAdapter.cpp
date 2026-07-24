#include "DocumentImportAdapter.h"

#include "Engine2D/Core/SceneManager.h"
#include "Engine2D/Edit/SceneEditService.h"
#include "Engine/SyEntity/SyEntity.h"
#include "Log/SyLogger.h"

DocumentImportAdapter::DocumentImportAdapter(
    Eg::SceneManager* sceneManager, SceneEditService* editService)
    : m_sceneManager(sceneManager)
    , m_editService(editService)
{
}

int DocumentImportAdapter::apply2D(Fio::VecSyEntityPtr& entities,
    bool preserveColors, bool preserveLayers)
{
    if (!m_sceneManager || entities.empty())
        return 0;

    int count = 0;
    for (auto& entity : entities)
    {
        if (!entity)
            continue;

        // 通过 SceneEditService 添加（支持 Undo），或直接添加
        if (m_editService)
        {
            Fio::VecSyEntityPtr single;
            single.push_back(std::move(entity));
            m_editService->addEntities(std::move(single), "Import");
        }
        else
        {
            m_sceneManager->addEntity(entity.release());
        }
        ++count;
    }

    SY_INFOF("[DocumentImportAdapter] Applied %d entities to 2D scene", count);
    return count;
}

int DocumentImportAdapter::apply3D(Fio::VecSyEntityPtr& entities)
{
    // 3D 场景适配暂用 2D 相同逻辑，后续由 3D 场景管理器接管
    return apply2D(entities, false, false);
}