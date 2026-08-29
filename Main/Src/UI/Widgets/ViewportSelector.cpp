#include "ViewportSelector.h"

#include "ISelectionService.h"

#include "Engine2D/Core/SceneManager.h"
#include "Engine/SyEntity/SyEntity.h"
#include "Engine/EntityIdUtils.h"

ViewportSelector::ViewportSelector(Eg::SceneManager* sceneManager, ISelectionService* selectionService)
    : m_sceneManager(sceneManager)
    , m_selectionService(selectionService)
{
}

std::optional<Ut::BBox2d> ViewportSelector::selectionBBox() const
{
    if (!m_selectionService || !m_sceneManager)
    {
        return std::nullopt;
    }

    // 通过 ID 遍历选中项，再用 SceneManager 查询实体指针合并 BBox
    // 这样 ISelectionService 保持纯 ID 接口，不泄漏 SyEntity*
    struct BBoxContext
    {
        Ut::BBox2d combined;
        bool hasEntity = false;
        Eg::SceneManager* sceneManager = nullptr;
    } ctx;

    ctx.sceneManager = m_sceneManager;

    m_selectionService->visitSelectedIds(
        [](const char* id, void* context) {
            if (!id)
            {
                return;
            }
            auto* bc = static_cast<BBoxContext*>(context);
            // ID 字符串 -> EntityId -> SyEntity*
            auto eid = Eg::parseEntityId(std::string(id));
            if (!eid)
            {
                return;
            }
            Eg::SyEntity* entity = bc->sceneManager->findEntityById(*eid);
            if (!entity)
            {
                return;
            }
            Ut::BBox2d bbox = entity->getBbox();
            if (bbox.isValid())
            {
                bc->combined.expand(bbox);
                bc->hasEntity = true;
            }
        },
        &ctx);

    if (!ctx.hasEntity || !ctx.combined.isValid())
    {
        return std::nullopt;
    }

    return ctx.combined;
}
