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

void SceneNode::setSelected(bool /*selected*/)
{
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
    m_scene->selectEntity(m_scene->findMeshById(node->engineEntityId()));
}

void SelectionSet::remove(const std::string& nodeId)
{
    if (!m_scene)
        return;
    auto eid = Eg::parseEntityId(nodeId);
    if (!eid)
        return;
    std::vector<Eg::EntityId> selIds;
    m_scene->forEachSelectedEntityId([](Eg::EntityId id, void* ctx) -> bool {
        auto* vec = static_cast<std::vector<Eg::EntityId>*>(ctx);
        vec->push_back(id);
        return true;
        }, &selIds);
    m_scene->clearSelection();
    for (auto id : selIds)
    {
        if (id != *eid)
            if (auto* m = m_scene->findMeshById(id))
                m_scene->selectEntity(m);
    }
}

bool SelectionSet::contains(const std::string& entityId) const
{
    if (!m_scene)
        return false;
    auto eid = Eg::parseEntityId(entityId);
    if (!eid)
        return false;
    struct FindCtx
    {
        bool found; Eg::EntityId target;
    };
    FindCtx ctx{ false, *eid };
    m_scene->forEachSelectedEntityId([](Eg::EntityId id, void* rawCtx) {
        auto* c = static_cast<FindCtx*>(rawCtx);
        if (id == c->target)
        {
            c->found = true;
            return false; // 停止遍历
        }
        return true;
        }, &ctx);
    return ctx.found;
}

std::vector<std::shared_ptr<SceneNode>> SelectionSet::items() const
{
    std::vector<std::shared_ptr<SceneNode>> result;
    if (!m_scene)
        return result;
    // ABI 收口：通过回调收集选中图元（替代 getSelectedMeshes() 返回值）
    m_scene->forEachSelectedMesh([](Eg::SyMeshEntity* mesh, void* ctx) {
        auto* vec = static_cast<std::vector<std::shared_ptr<SceneNode>>*>(ctx);
        if (!mesh)
            return;
        auto node = std::make_shared<SceneNode>(mesh->getId(), std::string(mesh->name()));
        vec->push_back(node);
        }, &result);
    return result;
}

bool SelectionSet::empty() const
{
    return m_scene ? !m_scene->hasSelection() : true;
}

// ============================================================================
// SceneDocument3DAdapter
// ============================================================================

static std::string engineIdStr(Eg::EntityId id)
{
    return std::to_string(id);
}

void SceneDocument3DAdapter::setEngineScene(std::shared_ptr<Eg::SceneManager3D> scene)
{
    m_engineScene = std::move(scene);
    m_selection.setScene(m_engineScene.get());
    m_uiRoot = std::make_shared<SceneNode>(static_cast<Eg::EntityId>(-1), "Root");
}

std::shared_ptr<SceneNode> SceneDocument3DAdapter::createNode(const std::string& name)
{
    if (!m_engineScene)
    {
        setEngineScene(std::make_shared<Eg::SceneManager3D>());
    }

    auto mesh = std::make_unique<Eg::SyMeshEntity>(name);
    Eg::EntityId meshId = mesh->id;
    m_engineScene->addEntity(mesh.release());

    auto node = std::make_shared<SceneNode>(meshId, name);
    if (m_uiRoot)
        m_uiRoot->addChild(node);
    return node;
}

std::shared_ptr<SceneNode> SceneDocument3DAdapter::nodeById(const std::string& id) const
{
    if (!m_uiRoot)
        return {};
    return m_uiRoot->childByIdRecursive(id);
}

void SceneDocument3DAdapter::removeNode(const std::string& id)
{
    if (!m_engineScene)
        return;
    auto eid = Eg::parseEntityId(id);
    if (!eid)
        return;
    if (auto* mesh = m_engineScene->findMeshById(*eid))
    {
        // removeEntity 转移所有权，销毁后移除 UI 节点
        delete m_engineScene->removeEntity(mesh);
    }
}

void SceneDocument3DAdapter::removeNode(const std::shared_ptr<SceneNode>& node)
{
    if (node)
        removeNode(node->id());
}

std::vector<std::shared_ptr<SceneNode>> SceneDocument3DAdapter::entities() const
{
    if (!m_uiRoot)
        return {};
    // m_uiRoot 的子节点即为根层节点，与旧 m_roots 语义一致
    return m_uiRoot->children();
}

std::vector<std::shared_ptr<SceneNode>> SceneDocument3DAdapter::rootNodes() const
{
    if (m_uiRoot)
        return m_uiRoot->children();
    return {};
}

SelectionSet& SceneDocument3DAdapter::selection()
{
    return m_selection;
}

const SelectionSet& SceneDocument3DAdapter::selection() const
{
    return m_selection;
}

// ---- SceneDocumentBase 接口 ----

void SceneDocument3DAdapter::forEachEntityId(void(*visitor)(const char*, void*), void* ctx) const
{
    if (!m_engineScene)
        return;

    // 将两个回调参数打包，避免捕获 lambda 无法转为函数指针
    struct VisitorCtx
    {
        void(*visitor)(const char*, void*);
        void* ctx;
    };
    VisitorCtx vctx{ visitor, ctx };

    m_engineScene->forEachEntityId(
        [](Eg::EntityId eid, void* userData) -> bool {
            auto* vc = static_cast<VisitorCtx*>(userData);
            std::string idStr = engineIdStr(eid);
            vc->visitor(idStr.c_str(), vc->ctx);
            return true;
        },
        &vctx);
}

void SceneDocument3DAdapter::removeEntity(const char* id)
{
    removeNode(std::string(id));
}

void SceneDocument3DAdapter::clear()
{
    if (m_engineScene)
        m_engineScene->clearScene();
    m_uiRoot.reset();
    m_selection.setScene(nullptr);
}