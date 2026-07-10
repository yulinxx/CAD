#include "UiEntities.h"

#include <algorithm>
#include <random>
#include <sstream>
#include <iomanip>

// ============================================================================
// SceneNode
// ============================================================================

SceneNode::SceneNode(std::string id, std::string name)
    : m_id(std::move(id))
    , m_name(std::move(name))
{
}

void SceneNode::addChild(const std::shared_ptr<SceneNode>& child)
{
    if (child) m_children.push_back(child);
}

std::vector<std::shared_ptr<SceneNode>> SceneNode::children() const { return m_children; }

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
    ids.push_back(m_id);
    for (const auto& child : m_children)
    {
        if (!child)
            continue;
        const auto childIds = child->pathIdsRecursive();
        for (const auto& id : childIds)
            ids.push_back(id);
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
    for (auto& item : m_items)
    {
        if (!item)
            continue;
        item->setSelected(false);
        item->setHighlighted(false);
    }
    m_items.clear();
}

void SelectionSet::add(const std::shared_ptr<SceneNode>& node)
{
    if (!node || contains(node->id()))
        return;
    m_items.push_back(node);
    node->setSelected(true);
    node->setHighlighted(true);
}

void SelectionSet::remove(const std::string& nodeId)
{
    auto it = std::remove_if(m_items.begin(), m_items.end(),
        [&](const std::shared_ptr<SceneNode>& item) {
            if (!item || item->id() != nodeId)
                return false;
            item->setSelected(false);
            item->setHighlighted(false);
            return true;
        });
    m_items.erase(it, m_items.end());
}

bool SelectionSet::contains(const std::string& nodeId) const
{
    return std::any_of(m_items.begin(), m_items.end(),
        [&](const std::shared_ptr<SceneNode>& item) {
            return item && item->id() == nodeId;
        });
}

std::vector<std::shared_ptr<SceneNode>> SelectionSet::items() const { return m_items; }
bool SelectionSet::empty() const { return m_items.empty(); }

// ============================================================================
// SceneDocument3D
// ============================================================================

static std::string generateUuid()
{
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 15);

    std::stringstream ss;
    ss << std::hex << std::uppercase;
    for (int i = 0; i < 32; ++i)
    {
        if (i == 8 || i == 12 || i == 16 || i == 20)
            ss << '-';
        ss << dis(gen);
    }
    return ss.str();
}

std::shared_ptr<SceneNode> SceneDocument3D::createNode(const std::string& name)
{
    auto node = std::make_shared<SceneNode>(generateUuid(), name);
    m_roots.push_back(node);
    return node;
}

std::shared_ptr<SceneNode> SceneDocument3D::nodeById(const std::string& id) const
{
    for (const auto& node : m_roots)
    {
        if (node && node->id() == id)
            return node;
    }
    return {};
}

void SceneDocument3D::removeNode(const std::string& id)
{
    m_selection.remove(id);
    auto it = std::remove_if(m_roots.begin(), m_roots.end(),
        [&](const std::shared_ptr<SceneNode>& node) {
            return node && node->id() == id;
        });
    m_roots.erase(it, m_roots.end());
}

void SceneDocument3D::removeNode(const std::shared_ptr<SceneNode>& node)
{
    if (node)
        removeNode(node->id());
}

std::vector<std::shared_ptr<SceneNode>> SceneDocument3D::entities() const
{
    std::vector<std::shared_ptr<SceneNode>> items;
    items.reserve(m_roots.size());
    for (const auto& e : m_roots) items.push_back(e);
    return items;
}

std::vector<std::shared_ptr<SceneNode>> SceneDocument3D::rootNodes() const { return m_roots; }
SelectionSet& SceneDocument3D::selection() { return m_selection; }
const SelectionSet& SceneDocument3D::selection() const { return m_selection; }

// ============================================================================
// DefaultCameraController3D
// ============================================================================

void DefaultCameraController3D::orbit(double deltaYaw, double deltaPitch)
{
    m_yaw += deltaYaw;
    m_pitch = std::clamp(m_pitch + deltaPitch, -89.0, 89.0);
}

void DefaultCameraController3D::zoom(double delta)
{
    m_distance = std::clamp(m_distance + delta, 2.0, 500.0);
}

void DefaultCameraController3D::pan(const RenderPointF& delta)
{
    m_panOffset.x += delta.x;
    m_panOffset.y += delta.y;
}

void DefaultCameraController3D::reset()
{
    m_yaw = 0.0;
    m_pitch = 15.0;
    m_distance = 10.0;
    m_panOffset = RenderPointF();
}

double DefaultCameraController3D::yaw() const { return m_yaw; }
double DefaultCameraController3D::pitch() const { return m_pitch; }
double DefaultCameraController3D::distance() const { return m_distance; }
