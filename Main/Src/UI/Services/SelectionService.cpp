#include "SelectionService.h"

#include "Engine2D/Core/SceneManager.h"
#include "Log/SyLogger.h"
#include "Engine/EntityIdUtils.h"
#include "Ut/Vec.h"

#include <string>

SelectionService::SelectionService(Eg::SceneManager* sceneManager)
    : m_sceneManager(sceneManager)
{
    SY_DEBUG("[SelectionService] initialized");
}

// ==================== POD 安全接口实现 ====================

void SelectionService::visitSelectedIds(SelectedIdVisitor visitor, void* context) const
{
    if (!m_sceneManager || !visitor)
    {
        return;
    }

    auto selected = m_sceneManager->getSelectedEntities();
    for (const auto& e : selected)
    {
        // 将 EntityId 转为临时 std::string 再传 C string 给回调
        auto idStr = std::to_string(e->id);
        visitor(idStr.c_str(), context);
    }
}

bool SelectionService::isSelected(const char* id) const
{
    if (!m_sceneManager || !id)
    {
        return false;
    }

    auto eid = Eg::parseEntityId(std::string(id));
    if (!eid)
    {
        return false;
    }

    struct FindCtx
    {
        bool found = false;
        Eg::EntityId target;
    };

    FindCtx ctx{ false, *eid };
    m_sceneManager->forEachSelectedEntityId(
        [](Eg::EntityId sid, void* rawCtx) {
            auto* data = static_cast<FindCtx*>(rawCtx);
            if (sid == data->target)
            {
                data->found = true;
                return false;  // 停止遍历
            }
            return true;
        },
        &ctx);
    return ctx.found;
}

void SelectionService::select(const char* id)
{
    if (!m_sceneManager || !id)
    {
        return;
    }

    auto eid = Eg::parseEntityId(std::string(id));
    if (!eid)
    {
        return;
    }

    auto* entity = m_sceneManager->findEntityById(*eid);
    if (entity)
    {
        m_sceneManager->selectEntity(entity);
    }
}

void SelectionService::selectMultiple(const char* const* ids, size_t count)
{
    if (!m_sceneManager || !ids || count == 0)
    {
        return;
    }

    // 批量入口：收集实体后一次性批量选择（selectRange 整体替换选择集），
    // 而非循环调用 selectEntity（单选择替换语义，会导致只保留最后一个）。
    std::vector<Eg::IEntity*> entities;
    entities.reserve(count);
    for (size_t i = 0; i < count; ++i)
    {
        if (!ids[i])
        {
            continue;
        }

        auto eid = Eg::parseEntityId(std::string(ids[i]));
        if (!eid)
        {
            continue;
        }

        auto* entity = m_sceneManager->findEntityById(*eid);
        if (entity)
        {
            entities.push_back(entity);
        }
    }
    m_sceneManager->selectEntities(entities);
}

void SelectionService::deselect(const char* id)
{
    if (!m_sceneManager || !id)
    {
        return;
    }

    auto eid = Eg::parseEntityId(std::string(id));
    if (!eid)
    {
        return;
    }

    m_sceneManager->deselectEntity(*eid);
}

void SelectionService::clear()
{
    if (m_sceneManager)
    {
        m_sceneManager->clearSelection();
    }
}

void SelectionService::toggle(const char* id)
{
    if (isSelected(id))
    {
        deselect(id);
    }
    else
    {
        select(id);
    }
}

// ==================== Qt 便利方法 ====================

QVector<QString> SelectionService::selectedIdsQ() const
{
    QVector<QString> result;
    visitSelectedIds(
        [](const char* id, void* ctx) {
            auto* vec = static_cast<QVector<QString>*>(ctx);
            vec->push_back(QString::fromUtf8(id));
        },
        &result);
    return result;
}

void SelectionService::selectEntity(const QString& id)
{
    auto utf8 = id.toUtf8();
    select(utf8.constData());
}

void SelectionService::setSelectedEntityId(const QString& id)
{
    if (!m_sceneManager)
    {
        return;
    }

    m_sceneManager->clearSelection();
    selectEntity(id);
}

void SelectionService::setSelectedEntityIds(const QVector<QString>& ids)
{
    if (!m_sceneManager)
    {
        return;
    }
    std::vector<Eg::IEntity*> entities;
    entities.reserve(ids.size());
    for (const QString& id : ids)
    {
        auto utf8 = id.toUtf8();
        auto eid = Eg::parseEntityId(std::string(utf8.constData()));
        if (!eid)
        {
            continue;
        }

        auto* entity = m_sceneManager->findEntityById(*eid);
        if (entity)
        {
            entities.push_back(entity);
        }
    }
    m_sceneManager->selectEntities(entities);
}

QString SelectionService::entityIdAt(const QPointF& point, double tolerance) const
{
    if (!m_sceneManager)
    {
        return {};
    }

    auto hits = m_sceneManager->queryByPoint(Ut::Vec2d(point.x(), point.y()), tolerance);
    if (hits.empty())
    {
        return {};
    }
    return QString::number(hits.front()->id);
}