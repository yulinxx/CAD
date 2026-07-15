#include "SelectionService.h"

#include "Engine2D/Core/SceneManager.h"
#include "Engine/EntityIdUtils.h"
#include "Ut/Vec.h"

#include <algorithm>

SelectionService::SelectionService(Eg::SceneManager* sceneManager)
    : m_sceneManager(sceneManager)
{
}

std::vector<std::string> SelectionService::selectedIds() const
{
    if (!m_sceneManager)
        return {};

    auto selected = m_sceneManager->getSelectedEntities();
    std::vector<std::string> ids;
    ids.reserve(selected.size());
    for (const auto& e : selected)
        ids.push_back(std::to_string(e->id));
    return ids;
}

bool SelectionService::isSelected(const std::string& id) const
{
    if (!m_sceneManager)
        return false;

    auto eid = Eg::parseEntityId(id);
    if (!eid)
        return false;

    // SceneManager 未提供直接查询接口，需遍历当前选中 ID 列表
    auto selectedIds = m_sceneManager->selectedEntityIds();
    for (const auto& sid : selectedIds)
        if (sid == *eid)
            return true;
    return false;
}

void SelectionService::select(const std::string& id)
{
    if (!m_sceneManager)
        return;

    auto eid = Eg::parseEntityId(id);
    if (!eid)
        return;

    auto* entity = m_sceneManager->findEntityById(*eid);
    if (entity)
        m_sceneManager->selectEntity(entity);
}

void SelectionService::selectMultiple(const std::vector<std::string>& ids)
{
    if (!m_sceneManager)
        return;

    m_sceneManager->clearSelection();
    for (const auto& id : ids)
    {
        auto eid = Eg::parseEntityId(id);
        if (!eid)
            continue;

        auto* entity = m_sceneManager->findEntityById(*eid);
        if (entity)
            m_sceneManager->selectEntity(entity);
    }
}

void SelectionService::deselect(const std::string& id)
{
    if (!m_sceneManager)
        return;

    auto eid = Eg::parseEntityId(id);
    if (!eid)
        return;

    m_sceneManager->deselectEntity(*eid);
}

void SelectionService::clear()
{
    if (m_sceneManager)
        m_sceneManager->clearSelection();
}

void SelectionService::toggle(const std::string& id)
{
    if (isSelected(id))
        deselect(id);
    else
        select(id);
}

std::vector<Eg::SyEntity*> SelectionService::selectedEntities() const
{
    if (!m_sceneManager)
        return {};
    return m_sceneManager->getSelectedEntities();
}

QVector<QString> SelectionService::selectedIdsQ() const
{
    auto ids = selectedIds();
    QVector<QString> result;
    result.reserve(static_cast<int>(ids.size()));
    for (const auto& id : ids)
        result.push_back(QString::fromStdString(id));
    return result;
}

void SelectionService::selectEntity(const QString& id)
{
    select(id.toStdString());
}

void SelectionService::setSelectedEntityId(const QString& id)
{
    if (!m_sceneManager)
        return;

    m_sceneManager->clearSelection();
    selectEntity(id);
}

void SelectionService::setSelectedEntityIds(const QVector<QString>& ids)
{
    if (!m_sceneManager)
        return;

    m_sceneManager->clearSelection();
    for (const QString& id : ids)
    {
        auto eid = Eg::parseEntityId(id.toStdString());
        if (!eid)
            continue;

        auto* entity = m_sceneManager->findEntityById(*eid);
        if (entity)
            m_sceneManager->selectEntity(entity);
    }
}

QString SelectionService::entityIdAt(const QPointF& point, double tolerance) const
{
    if (!m_sceneManager)
        return {};

    auto hits = m_sceneManager->queryByPoint(Ut::Vec2d(point.x(), point.y()), tolerance);
    if (hits.empty())
        return {};
    return QString::number(hits.front()->id);
}