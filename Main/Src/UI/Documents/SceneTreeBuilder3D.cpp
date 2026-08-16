#include "SceneTreeBuilder3D.h"

#include "Engine3D/SceneManager3D.h"
#include "Engine3D/SyEntity/SyMeshEntity.h"

#include <QObject>
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

    // 3D 场景选择状态统一存放在 SceneManager3D 的选择列表中（而非实体标志位），
    // 这里一次性收集，供节点选中态与 selectedCount 复用，保证与 selectedIds() 一致。
    const QSet<QString> selected = selectedIds(scene);

    std::vector<const Eg::SyMeshEntity*> meshes;
    scene->forEachEntity(&collectMesh, &meshes);

    for (const Eg::SyMeshEntity* mesh : meshes)
    {
        if (!mesh)
        {
            continue;
        }

        const bool isSelected = selected.contains(QString::number(mesh->id));
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
