#include "ViewportSelector.h"

#include "ISelectionService.h"

#include "Engine/Scene/ISceneContext.h"
#include "Engine/SyEntity/SyEntity.h"
#include "Engine/EntityIdUtils.h"

ViewportSelector::ViewportSelector(Eg::ISceneContext* sceneContext, ISelectionService* selectionService)
    : m_sceneContext(sceneContext)
    , m_selectionService(selectionService)
{
}

std::optional<Ut::BBox2d> ViewportSelector::selectionBBox() const
{
    if (!m_selectionService || !m_sceneContext)
    {
        return std::nullopt;
    }

    // 通过 ID 遍历选中项，再用 SceneContext 查询图元指针合并 BBox
    // 这样 ISelectionService 保持纯 ID 接口，不泄漏 SyEntity*
    struct BBoxContext
    {
        Ut::BBox2d combined;
        bool hasEntity = false;
        Eg::ISceneContext* sceneContext = nullptr;
    } ctx;

    ctx.sceneContext = m_sceneContext;

    m_selectionService->visitSelectedIds(
        [](const char* id, void* context) {
            if (!id)
            {
                return;
            }
            auto* bc = static_cast<BBoxContext*>(context);
            // ID 字符串 -> EntityId -> IEntity*
            auto eid = Eg::parseEntityId(std::string(id));
            if (!eid)
            {
                return;
            }
            Eg::IEntity* entity = bc->sceneContext->findEntityById(*eid);
            // 2D 路径图元均为 SyEntity；hidden visibility 下不用 dynamic_cast
            auto* syEntity = static_cast<Eg::SyEntity*>(entity);
            if (!syEntity)
            {
                return;
            }
            Ut::BBox2d bbox = syEntity->getBbox();
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