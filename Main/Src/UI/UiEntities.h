#pragma once

#include "../RenderCore/RenderTypes.h"

#include <memory>
#include <string>
#include <vector>

// ============================================================
/**
 * @brief 3D 场景树节点
 *
 * 构成 SceneDocument3D 的层次结构，支持父子节点和递归查询。
 */
class SceneNode
{
public:
    SceneNode(std::string id, std::string name);

    std::string id() const { return m_id; }
    std::string name() const { return m_name; }
    std::string typeName() const { return "SceneNode"; }

    bool selected() const { return m_selected; }
    void setSelected(bool selected) { m_selected = selected; }
    bool highlighted() const { return m_highlighted; }
    void setHighlighted(bool highlighted) { m_highlighted = highlighted; }

    void addChild(const std::shared_ptr<SceneNode>& child);
    std::vector<std::shared_ptr<SceneNode>> children() const;
    std::shared_ptr<SceneNode> childByIdRecursive(const std::string& id) const;
    std::vector<std::string> pathIdsRecursive() const;
    std::vector<std::string> pathNamesRecursive() const;

private:
    std::string m_id;
    std::string m_name;
    std::vector<std::shared_ptr<SceneNode>> m_children;
    bool m_selected{ false };
    bool m_highlighted{ false };
};

// ============================================================
/**
 * @brief 选择集容器（3D 场景树专用）
 *
 * 管理 SceneDocument3D 中 SceneNode 的选择状态。
 * 不再服务于已移除的 EntityDocument2D。
 */
class SelectionSet
{
public:
    void clear();
    void add(const std::shared_ptr<SceneNode>& node);
    void remove(const std::string& nodeId);
    bool contains(const std::string& nodeId) const;
    std::vector<std::shared_ptr<SceneNode>> items() const;
    bool empty() const;

private:
    std::vector<std::shared_ptr<SceneNode>> m_items;
};

// ============================================================
/**
 * @brief 3D 场景文档
 *
 * 管理 3D 场景的层次结构（SceneNode 树）和选择状态。
 * 2D 场景请使用 Eg::SceneManager (Engine/2D/Core/SceneManager.h)
 */
class SceneDocument3D
{
public:
    std::shared_ptr<SceneNode> createNode(const std::string& name);
    std::shared_ptr<SceneNode> nodeById(const std::string& id) const;
    void removeNode(const std::string& id);
    void removeNode(const std::shared_ptr<SceneNode>& node);
    std::vector<std::shared_ptr<SceneNode>> entities() const;
    std::vector<std::shared_ptr<SceneNode>> rootNodes() const;
    SelectionSet& selection();
    const SelectionSet& selection() const;

private:
    std::vector<std::shared_ptr<SceneNode>> m_roots;
    SelectionSet m_selection;
};

// ============================================================
class CameraController3D
{
public:
    virtual ~CameraController3D() = default;
    virtual void orbit(double deltaYaw, double deltaPitch) = 0;
    virtual void zoom(double delta) = 0;
    virtual void pan(const RenderPointF& delta) = 0;
    virtual void reset() = 0;
};

// ============================================================
class DefaultCameraController3D final : public CameraController3D
{
public:
    void orbit(double deltaYaw, double deltaPitch) override;
    void zoom(double delta) override;
    void pan(const RenderPointF& delta) override;
    void reset() override;
    double yaw() const;
    double pitch() const;
    double distance() const;

private:
    double m_yaw{ 0.0 };
    double m_pitch{ 15.0 };
    double m_distance{ 10.0 };
    RenderPointF m_panOffset;
};
