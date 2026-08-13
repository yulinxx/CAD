#pragma once

#include <memory>
#include <string>
#include <vector>

#include "UI/SceneDocumentBase.h"

#include "Engine3D/SceneManager3D.h"

/**
 * @brief 3D 场景树节点（纯 UI 显示层）
 *
 * 不存储图元数据，所有属性（id/name/selected）委托给引擎图元查询。
 * 仅维护：
 * - m_engineEntityId：关联的引擎图元 ID
 * - m_children：UI 树层次结构（父子关系）
 * - m_highlighted：UI 高亮状态
 */
class SceneNode
{
public:
    /// 构造：必须关联引擎图元
    explicit SceneNode(Eg::EntityId entityId, std::string name);

    std::string id() const
    {
        return std::to_string(m_engineEntityId);
    }

    std::string name() const
    {
        return m_name;
    }

    std::string typeName() const
    {
        return "SceneNode";
    }

    bool selected() const;
    void setSelected(bool selected);

    bool highlighted() const
    {
        return m_highlighted;
    }

    void setHighlighted(bool highlighted)
    {
        m_highlighted = highlighted;
    }

    void addChild(const std::shared_ptr<SceneNode>& child);
    std::vector<std::shared_ptr<SceneNode>> children() const;
    std::shared_ptr<SceneNode> childByIdRecursive(const std::string& id) const;
    std::vector<std::string> pathIdsRecursive() const;
    std::vector<std::string> pathNamesRecursive() const;

    Eg::EntityId engineEntityId() const
    {
        return m_engineEntityId;
    }

    void setEngineEntityId(Eg::EntityId id)
    {
        m_engineEntityId = id;
    }

private:
    Eg::EntityId m_engineEntityId;
    std::string m_name;
    std::vector<std::shared_ptr<SceneNode>> m_children;
    bool m_highlighted{ false };
};

/**
 * @brief 选择集容器（基于引擎场景）
 *
 * 将选中图元 ID 委托给 SceneManager3D 管理。
 * SceneNode 的 selected() 直接读取引擎图元状态。
 */
class SelectionSet
{
public:
    void clear();
    void add(const std::shared_ptr<SceneNode>& node);
    void remove(const std::string& nodeId);
    bool contains(const std::string& entityId) const;
    std::vector<std::shared_ptr<SceneNode>> items() const;
    bool empty() const;

private:
    Eg::SceneManager3D* m_scene{ nullptr };
    friend class SceneDocument3DAdapter;

    void setScene(Eg::SceneManager3D* scene)
    {
        m_scene = scene;
    }
};

/**
 * @brief 3D 场景文档（引擎场景适配层）
 *
 * 数据全部由 Eg::SceneManager3D 管理，SceneNode 仅用于 UI 树层次显示。
 * - m_uiRoot：UI 树根节点，维护父子层次
 * - m_engineScene：引擎场景，维护图元数据
 */
class SceneDocument3DAdapter : public UI::SceneDocumentBase
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

    // ---- 引擎场景管理 ----

    std::shared_ptr<Eg::SceneManager3D> engineScene() const
    {
        return m_engineScene;
    }

    void setEngineScene(std::shared_ptr<Eg::SceneManager3D> scene);

    // ---- SceneDocumentBase 接口 ----

    void forEachEntityId(void (*visitor)(const char*, void*), void* ctx) const override;
    void removeEntity(const char* id) override;
    void clear() override;

private:
    mutable SelectionSet m_selection;
    std::shared_ptr<Eg::SceneManager3D> m_engineScene;
    std::shared_ptr<SceneNode> m_uiRoot;
};
