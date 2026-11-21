#include "EntityIdLabelReconciler.h"

#include "RenderWidget.h"
#include "Engine2D/Core/SceneManager.h"
#include "Engine/AprEntity/AprEntity.h"
#include "Engine/Layer/AprLayer.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

namespace
{
    // 屏幕像素边距不随相机缩放，避免文字贴在线条上或进入填充区域。
    constexpr float kEntityIdLabelMarginPx = 4.0f;
}

EntityIdLabelReconciler::EntityIdLabelReconciler(RenderWidget* widget)
    : m_renderWidget(widget)
{
}

void EntityIdLabelReconciler::reconcile(Eg::SceneManager* sm,
    bool fullReconcile,
    const std::unordered_set<Eg::EntityId>& dirtyIds,
    const std::unordered_set<Eg::EntityId>& deletedIds)
{
    if (!m_renderWidget || !sm)
    {
        return;
    }

    // 开关关闭：清空账本并通知渲染侧，无需扫描场景。
    // 这样取消勾选后立即生效，不必等下一次场景变更触发刷新。
    if (!m_renderWidget->showEntityIdLabels())
    {
        if (!m_labels.empty())
        {
            m_labels.clear();
            m_stats = {};
            m_renderWidget->clearEntityIdLabels();
        }
        return;
    }

    if (fullReconcile)
    {
        if (rebuildAll(sm))
        {
            pushToWidget();
        }
        return;
    }

    bool changed = false;
    for (Eg::EntityId id : deletedIds)
    {
        changed = m_labels.erase(id) != 0 || changed;
    }

    // 增量路径只查本轮脏 ID，不遍历全场景。不可见、出视口和无效包围盒的图元在此处移除。
    for (Eg::EntityId id : dirtyIds)
    {
        Render::EntityIdLabelItem item;
        if (buildLabel(sm, id, item))
        {
            const auto old = m_labels.find(id);
            const bool differs = old == m_labels.end() || old->second.anchorWorld.x() != item.anchorWorld.x() ||
                old->second.anchorWorld.y() != item.anchorWorld.y() || old->second.text != item.text ||
                old->second.offsetPx.x() != item.offsetPx.x() || old->second.offsetPx.y() != item.offsetPx.y();
            if (differs)
            {
                m_labels[id] = std::move(item);
                changed = true;
            }
        }
        else
        {
            changed = m_labels.erase(id) != 0 || changed;
        }
    }

    // 增量新增可能使预算超限：按与全量一致的稳定规则裁掉 ID 最大的标签，
    // 正常拖动已有标签不会触发重选（数量不变时不会进入这里）。
    const size_t maxLabels = effectiveMaxLabels();
    if (maxLabels != 0 && m_labels.size() > maxLabels)
    {
        std::vector<Eg::EntityId> ids;
        ids.reserve(m_labels.size());
        for (const auto& entry : m_labels)
        {
            ids.push_back(entry.first);
        }
        std::sort(ids.begin(), ids.end());
        while (ids.size() > maxLabels)
        {
            m_labels.erase(ids.back());
            ids.pop_back();
            changed = true;
        }
    }

    m_stats.shown = m_labels.size();
    if (changed)
    {
        pushToWidget();
    }
}

bool EntityIdLabelReconciler::rebuildAll(Eg::SceneManager* sm)
{
    std::vector<Render::EntityIdLabelItem> candidates;
    size_t culledByViewport = 0;

    auto consider = [&](Eg::AprEntity* e) {
        if (!e || e->id <= 0)
        {
            return;
        }
        Render::EntityIdLabelItem item;
        bool viewportCulled = false;
        if (buildLabel(sm, e->id, item, &viewportCulled))
        {
            candidates.push_back(std::move(item));
        }
        else if (viewportCulled)
        {
            ++culledByViewport;
        }
    };

    // 视口盒有效时用空间索引做范围查询（O(log n + k)），避免大场景全量遍历；
    // 无效盒（首载/矩阵退化兜底）才退化为全场景遍历。
    if (m_visibleBounds.isValid())
    {
        const Eg::VecAprEntityPtr inBox = sm->queryByBox(m_visibleBounds, /*containedOnly=*/false);
        for (Eg::AprEntity* e : inBox)
        {
            consider(e);
        }
    }
    else
    {
        sm->forEachEntity(consider);
    }

    std::sort(candidates.begin(), candidates.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.entityId < rhs.entityId;
    });

    m_stats = {};
    m_stats.candidates = candidates.size();
    m_stats.culledByViewport = culledByViewport;

    const size_t maxLabels = effectiveMaxLabels();
    const size_t shownCount = maxLabels == 0 ? candidates.size() : std::min(candidates.size(), maxLabels);
    m_stats.shown = shownCount;
    m_stats.culledByBudget = candidates.size() - shownCount;

    std::unordered_map<Eg::EntityId, Render::EntityIdLabelItem> rebuilt;
    rebuilt.reserve(shownCount);

    // 保留已有标签优先（按 ID 升序截断），剩余名额再按 ID 升序补新标签。
    // 候选已按 ID 排序，因此选择结果稳定，不依赖 unordered_map 遍历顺序 ——
    // 平移时标签不会整片随机换人。
    if (maxLabels != 0 && !m_labels.empty())
    {
        for (const auto& candidate : candidates)
        {
            if (rebuilt.size() >= shownCount)
            {
                break;
            }
            if (m_labels.count(candidate.entityId) != 0)
            {
                rebuilt.emplace(candidate.entityId, candidate);
            }
        }
    }
    for (const auto& candidate : candidates)
    {
        if (rebuilt.size() >= shownCount)
        {
            break;
        }
        rebuilt.emplace(candidate.entityId, candidate);
    }

    bool changed = rebuilt.size() != m_labels.size();
    if (!changed)
    {
        for (const auto& entry : rebuilt)
        {
            const auto old = m_labels.find(entry.first);
            if (old == m_labels.end() || old->second.anchorWorld.x() != entry.second.anchorWorld.x() ||
                old->second.anchorWorld.y() != entry.second.anchorWorld.y() || old->second.text != entry.second.text)
            {
                changed = true;
                break;
            }
        }
    }
    m_labels = std::move(rebuilt);
    return changed;
}

size_t EntityIdLabelReconciler::effectiveMaxLabels() const
{
    return m_renderWidget != nullptr ? m_renderWidget->entityIdLabelMaxCount() : 0;
}

bool EntityIdLabelReconciler::buildLabel(Eg::SceneManager* sm,
    Eg::EntityId id,
    Render::EntityIdLabelItem& outItem,
    bool* outViewportCulled) const
{
    Eg::AprEntity* e = sm->findEntityById(id);
    if (!e || !e->visible() || e->id <= 0 || !e->isValid())
    {
        return false;
    }
    if (e->layer() == nullptr || !e->layer()->isVisible())
    {
        return false;
    }

    const Ut::BBox2d bbox = e->getBbox();
    if (!bbox.isValid())
    {
        return false;
    }
    // 视口裁剪：与几何层共用同一查询盒，出盒图元不生成标签（只做诊断区分）
    if (m_visibleBounds.isValid() && !bbox.intersects(m_visibleBounds))
    {
        if (outViewportCulled != nullptr)
        {
            *outViewportCulled = true;
        }
        return false;
    }

    outItem.entityId = id;
    outItem.text = std::to_string(id);
    // 锚点取包围盒右上角（世界坐标）；绘制侧以左上角对齐文字（hAlign=Left / vAlign=Top），
    // 再叠加固定屏幕像素偏移，文字会稳定地出现在图元右上方。
    outItem.anchorWorld = Ut::Vec2f(static_cast<float>(bbox.maxPt.x()), static_cast<float>(bbox.maxPt.y()));
    outItem.offsetPx = Ut::Vec2f(kEntityIdLabelMarginPx, -kEntityIdLabelMarginPx);
    return true;
}

void EntityIdLabelReconciler::reset()
{
    m_labels.clear();
    m_visibleBounds = Ut::BBox2d{};
    m_stats = {};
    if (m_renderWidget != nullptr)
    {
        m_renderWidget->clearEntityIdLabels();
    }
}

void EntityIdLabelReconciler::pushToWidget()
{
    Render::EntityIdLabelList list;
    list.reserve(m_labels.size());
    for (const auto& kv : m_labels)
    {
        list.push_back(kv.second);
    }
    // RenderWidget::setEntityIdLabels 内部触发重绘
    m_renderWidget->setEntityIdLabels(std::move(list));
}
