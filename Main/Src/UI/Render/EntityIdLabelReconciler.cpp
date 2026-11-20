#include "EntityIdLabelReconciler.h"

#include "RenderWidget.h"
#include "Engine2D/Core/SceneManager.h"
#include "Engine/AprEntity/AprEntity.h"
#include "Engine/Layer/AprLayer.h"

#include <string>
#include <utility>

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
            m_renderWidget->clearEntityIdLabels();
        }
        return;
    }

    bool changed = false;

    if (fullReconcile)
    {
        // 图层显隐等未必产生图元 dirty/deleted 记录的变化，必须以场景为真源全量对账。
        std::unordered_map<Eg::EntityId, Render::EntityIdLabelItem> rebuilt;
        sm->forEachEntity([&](Eg::AprEntity* e) {
            if (!e || e->id <= 0)
            {
                return;
            }
            Render::EntityIdLabelItem item;
            if (buildLabel(sm, e->id, item))
            {
                rebuilt.emplace(e->id, std::move(item));
            }
        });
        changed = rebuilt.size() != m_labels.size();
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
    }
    else
    {
        for (Eg::EntityId id : deletedIds)
        {
            changed = m_labels.erase(id) != 0 || changed;
        }

        // 增量路径只查本轮脏 ID，不遍历全场景。不可见图元和无效包围盒在此处移除。
        for (Eg::EntityId id : dirtyIds)
        {
            Render::EntityIdLabelItem item;
            if (buildLabel(sm, id, item))
            {
                const auto old = m_labels.find(id);
                const bool differs = old == m_labels.end() || old->second.anchorWorld.x() != item.anchorWorld.x() ||
                    old->second.anchorWorld.y() != item.anchorWorld.y() || old->second.text != item.text;
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
    }

    if (changed)
    {
        pushToWidget();
    }
}

bool EntityIdLabelReconciler::buildLabel(Eg::SceneManager* sm,
    Eg::EntityId id,
    Render::EntityIdLabelItem& outItem) const
{
    Eg::AprEntity* e = sm->findEntityById(id);
    if (!e || !e->visible() || e->id <= 0)
    {
        return false;
    }
    if (e->layer() && !e->layer()->isVisible())
    {
        return false;
    }

    const Ut::BBox2d bbox = e->getBbox();
    if (!bbox.isValid())
    {
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
