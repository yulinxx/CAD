#include "TextReconciler.h"

#include "RenderWidget.h"
#include "Engine2D/Core/SceneManager.h"
#include "Engine/SyEntity/SyEntity.h"
#include "Engine/Layer/SyLayer.h"
#include "Engine2D/SyEntity/SyText.h"
#include "Engine/SyEntity/EType.h"

TextReconciler::TextReconciler(RenderWidget* widget)
    : m_renderWidget(widget)
{
}

void TextReconciler::reconcile(Eg::SceneManager* sm,
    bool fullReconcile,
    const std::unordered_set<uint64_t>& selectedIds,
    bool hideSelected,
    const std::unordered_set<Eg::EntityId>& dirtyIds)
{
    if (!m_renderWidget || !sm)
    {
        return;
    }

    if (fullReconcile)
    {
        // 全量重建：重置本地账本与渲染侧账本后整体重传
        m_renderWidget->clearWorldTexts();
        m_worldTextIds.clear();
    }

    // 期望集合：场景中所有可见 SyText（可见 = 图元可见 && 图层可见）
    std::unordered_set<uint64_t> desired;
    // 使用类型索引避免全场景扫描
    auto textEntities = sm->getEntitiesByType(Eg::EType::TEXT);
    for (auto* e : textEntities)
    {
        if (!e || !e->visible())
        {
            continue;
        }
        if (e->layer() && !e->layer()->isVisible())
        {
            continue;
        }
        auto uid = static_cast<uint64_t>(e->id);
        // 如果启用了"选中时隐藏原图"且实体被选中，跳过
        if (hideSelected && selectedIds.count(uid))
        {
            continue;
        }
        desired.insert(uid);
    }

    // 移除：本地账本中存在但场景已不期望（删除 / 隐藏 / 图层隐藏）
    for (auto it = m_worldTextIds.begin(); it != m_worldTextIds.end();)
    {
        if (!desired.count(*it))
        {
            m_renderWidget->removeWorldText(*it);
            it = m_worldTextIds.erase(it);
        }
        else
        {
            ++it;
        }
    }

    // 新增/更新：期望但尚未登记 → 登记；已登记但本轮 dirty（内容/位置/字高变化）→ 重登记
    for (auto id : desired)
    {
        const bool isNew = m_worldTextIds.count(id) == 0;
        const bool isDirty = dirtyIds.count(static_cast<Eg::EntityId>(id)) > 0;
        if (!isNew && !isDirty)
        {
            continue;
        }

        auto* e = sm->findEntityById(static_cast<Eg::EntityId>(id));
        if (!e)
        {
            continue;
        }

        // 空串或非正字高画不出任何东西，不纳入账本（移除残留）
        const auto* text = static_cast<const Eg::SyText*>(e);
        if (text->text()[0] == '\0' || text->dHeight <= 0.0)
        {
            m_renderWidget->removeWorldText(id);
            m_worldTextIds.erase(id);
            continue;
        }

        m_renderWidget->setWorldText(id, text);
        m_worldTextIds.insert(id);
    }
}