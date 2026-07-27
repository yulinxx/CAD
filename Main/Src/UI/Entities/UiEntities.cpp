#include "UiEntities.h"

#include "Engine/EntityIdUtils.h"
#include <algorithm>

// ============================================================================
// SceneNode
// ============================================================================

SceneNode::SceneNode(Eg::EntityId entityId, std::string name)
    : m_engineEntityId(entityId)
    , m_name(std::move(name))
{
}

bool SceneNode::selected() const
{
    // 由 SelectionSet::items() 统一包装查询，此处返回 false
    return false;
}

void SceneNode::setSelected(bool selected)
{
    (void)selected;
    // 选中状态由引擎场景管理
}

void SceneNode::addChild(const std::shared_ptr<SceneNode>& child)
{
    if (child) m_children.push_back(child);
}

std::vector<std::shared_ptr<SceneNode>> SceneNode::children() const
{
    return m_children;
}

std::shared_ptr<SceneNode> SceneNode::childByIdRecursive(const std::string& id) const
{
    for (const auto& child : m_children)
    {
        if (!child)
            continue;
        if (child->id() == id)
            return child;
        auto nested = child->childByIdRecursive(id);
        if (nested)
            return nested;
    }
    return {};
}

std::vector<std::string> SceneNode::pathIdsRecursive() const
{
    std::vector<std::string> ids;
    ids.push_back(id());
    for (const auto& child : m_children)
    {
        if (!child)
            continue;
        const auto childIds = child->pathIdsRecursive();
        for (const auto& cid : childIds)
            ids.push_back(cid);
    }
    return ids;
}

std::vector<std::string> SceneNode::pathNamesRecursive() const
{
    std::vector<std::string> names;
    names.push_back(m_name);
    for (const auto& child : m_children)
    {
        if (!child)
            continue;
        const auto childNames = child->pathNamesRecursive();
        for (const auto& name : childNames)
            names.push_back(name);
    }
    return names;
}

// ============================================================================
// SelectionSet
// ============================================================================

void SelectionSet::clear()
{
    if (m_scene)
        m_scene->clearSelection();
}

void SelectionSet::add(const std::shared_ptr<SceneNode>& node)
{
    if (!node || !m_scene)
        return;
    m_scene->selectMesh(m_scene->findMeshById(node->engineEntityId()));
}

void SelectionSet::remove(const std::string& nodeId)
{
    if (!m_scene)
        return;
    auto eid = Eg::parseEntityId(nodeId);
    if (!eid)
        return;
    auto selIds = m_scene->selectedEntityIds();
    m_scene->clearSelection();
    for (auto id : selIds)
    {
        if (id != *eid)
            if (auto* m = m_scene->findMeshById(id))
                m_scene->selectMesh(m);
    }
}

bool SelectionSet::contains(const std::string& entityId) const
{
    if (!m_scene)
        return false;
    auto eid = Eg::parseEntityId(entityId);
    if (!eid)
        return false;
    const auto selIds = m_scene->selectedEntityIds();
    return std::find(selIds.begin(), selIds.end(), *eid) != selIds.end();
}

std::vector<std::shared_ptr<SceneNode>> SelectionSet::items() const
{
    std::vector<std::shared_ptr<SceneNode>> result;
    if (!m_scene)
        return result;
    for (auto* mesh : m_scene->getSelectedMeshes())
    {
        auto node = std::make_shared<SceneNode>(mesh->getId(), mesh->strName);
        result.push_back(node);
    }
    return result;
}

bool SelectionSet::empty() const
{
    return m_scene ? !m_scene->hasSelection() : true;
}

// ============================================================================
// SceneDocument3D
// ============================================================================

static std::string engineIdStr(Eg::EntityId id)
{
    return std::to_string(id);
}

void SceneDocument3D::setEngineScene(std::shared_ptr<Eg::SceneManager3D> scene)
{
    m_engineScene = std::move(scene);
    m_selection.setScene(m_engineScene.get());
    m_uiRoot = std::make_shared<SceneNode>(static_cast<Eg::EntityId>(-1), "Root");
}

std::shared_ptr<SceneNode> SceneDocument3D::createNode(const std::string& name)
{
    if (!m_engineScene)
    {
        setEngineScene(std::make_shared<Eg::SceneManager3D>());
    }

    auto mesh = std::make_unique<Eg::SyMeshEntity>(name);
    Eg::EntityId meshId = mesh->id;
    m_engineScene->addEntity(std::move(mesh));

    auto node = std::make_shared<SceneNode>(meshId, name);
    if (m_uiRoot)
        m_uiRoot->addChild(node);
    return node;
}

std::shared_ptr<SceneNode> SceneDocument3D::nodeById(const std::string& id) const
{
    if (!m_uiRoot)
        return {};
    return m_uiRoot->childByIdRecursive(id);
}

void SceneDocument3D::removeNode(const std::string& id)
{
    if (!m_engineScene)
        return;
    auto eid = Eg::parseEntityId(id);
    if (!eid)
        return;
    if (auto* mesh = m_engineScene->findMeshById(*eid))
        m_engineScene->removeEntity(mesh);
}

void SceneDocument3D::removeNode(const std::shared_ptr<SceneNode>& node)
{
    if (node)
        removeNode(node->id());
}

std::vector<std::shared_ptr<SceneNode>> SceneDocument3D::entities() const
{
    if (!m_uiRoot)
        return {};
    // m_uiRoot 的子节点即为根层节点，与旧 m_roots 语义一致
    return m_uiRoot->children();
}

std::vector<std::shared_ptr<SceneNode>> SceneDocument3D::rootNodes() const
{
    if (m_uiRoot)
        return m_uiRoot->children();
    return {};
}

SelectionSet& SceneDocument3D::selection()
{
    return m_selection;
}

const SelectionSet& SceneDocument3D::selection() const
{
    return m_selection;
}

// ---- SceneDocumentBase 接口 ----

std::vector<std::string> SceneDocument3D::allEntityIds() const
{
    std::vector<std::string> ids;
    if (!m_engineScene)
        return ids;
    for (auto eid : m_engineScene->getAllEntityIds())
        ids.push_back(engineIdStr(eid));
    return ids;
}

void SceneDocument3D::removeEntity(const std::string& id)
{
    removeNode(id);
}

void SceneDocument3D::clear()
{
    if (m_engineScene)
        m_engineScene->clearScene();
    m_uiRoot.reset();
    m_selection.setScene(nullptr);
}