#include "SceneEditServiceAdapter.h"
#include "SceneDocument2D.h"

#include "Engine2D/Core/SceneManager.h"

SceneEditServiceAdapter::SceneEditServiceAdapter(SceneDocument2D* document, QObject* parent)
    : QObject(parent)
    , m_document(document)
{
}

std::vector<Eg::EntityId> SceneEditServiceAdapter::getSelectedEntityIds() const
{
    std::vector<Eg::EntityId> result;
    if (!m_document)
        return result;
    auto* sm = m_document->sceneManager();
    if (!sm) return result;

    for (auto* entity : sm->getSelectedEntities())
        result.push_back(entity->id);
    return result;
}

std::vector<Eg::EntityId> SceneEditServiceAdapter::getAllEntityIds() const
{
    std::vector<Eg::EntityId> result;
    if (!m_document)
        return result;
    auto* sm = m_document->sceneManager();
    if (!sm) return result;

    for (auto* entity : sm->getAllEntities())
        result.push_back(entity->id);
    return result;
}

void SceneEditServiceAdapter::notifySceneChanged()
{
    // 发送场景变更信号，触发视口刷新和状态同步
    emit sceneChanged();
}