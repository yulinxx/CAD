/**
 * @file SceneTreeBuilder3D.cpp
 * @brief 3D 场景树构建器实现
 *
 * 从 3D 场景管理器构建场景树模型。
 */
#include "SceneTreeBuilder3D.h"

#include "Engine3D/SceneManager3D.h"
#include "Engine3D/SyEntity/SyMeshEntity.h"

#include <QObject>
#include <unordered_set>
#include <vector>

namespace
{
    /// 收集所有网格图元（forEachEntity 回调）
    void collectMesh(Eg::SyMeshEntity* mesh, void* ctx)
    {
        auto* out = static_cast<std::vector<const Eg::SyMeshEntity*>*>(ctx);
        if (mesh)
        {
            out->push_back(mesh);
        }
    }

    /// 选中 ID 收集为整数集合：build 主循环用 mesh->id 直查，
    /// 避免每个图元都 QString::number + QSet<QString>::contains 的双重字符串分配。
    bool collectSelectedId(Eg::EntityId id, void* ctx)
    {
        static_cast<std::unordered_set<uint64_t>*>(ctx)->insert(id);
        return true;
    }
}  // namespace

SceneTreeNode3D SceneTreeBuilder3D::buildMeshNode(const Eg::SyMeshEntity* mesh, bool selected)
{
    SceneTreeNode3D node;
    if (!mesh)
    {
        return node;
    }

    node.id = QString::number(mesh->id);
    node.typeName = QObject::tr("Mesh");
    const char* rawName = mesh->name();
    node.displayName = (rawName && *rawName) ? QString::fromUtf8(rawName) : node.typeName;
    node.selected = selected;
    node.visible = mesh->visible();
    node.locked = mesh->locked();
    node.info = QObject::tr("%1 tris").arg(static_cast<int>(mesh->triangleCount()));
    return node;
}

SceneTreeModel3D SceneTreeBuilder3D::build(Eg::SceneManager3D* scene)
{
    SceneTreeModel3D model;
    if (!scene)
    {
        return model;
    }

    // 选中态走整数集合一次收集（与 selectedIds() 语义一致，但主循环零字符串查找）
    std::unordered_set<uint64_t> selectedSet;
    selectedSet.reserve(scene->getSelectedEntityCount());
    scene->forEachSelectedEntityId(&collectSelectedId, &selectedSet);

    std::vector<const Eg::SyMeshEntity*> meshes;
    meshes.reserve(scene->getEntityCount());
    scene->forEachEntity(&collectMesh, &meshes);

    model.nodes.reserve(static_cast<int>(meshes.size()));
    for (const Eg::SyMeshEntity* mesh : meshes)
    {
        if (!mesh)
        {
            continue;
        }

        const bool isSelected = selectedSet.count(mesh->id) > 0;
        SceneTreeNode3D node = buildMeshNode(mesh, isSelected);
        if (node.selected)
        {
            ++model.selectedCount;
        }

        model.nodes.append(node);
        ++model.totalCount;
    }

    return model;
}

QSet<QString> SceneTreeBuilder3D::selectedIds(Eg::SceneManager3D* scene)
{
    QSet<QString> ids;
    if (!scene)
    {
        return ids;
    }

    std::vector<const Eg::SyMeshEntity*> meshes;
    scene->forEachSelectedMesh(&collectMesh, &meshes);
    for (const Eg::SyMeshEntity* mesh : meshes)
    {
        if (mesh)
        {
            ids.insert(QString::number(mesh->id));
        }
    }
    return ids;
}