#include "SceneTreeBuilder2D.h"

#include "Engine2D/Core/SceneManager.h"
#include "Engine2D/Core/GroupManager.h"
#include "Engine2D/Interaction/LayerManager.h"
#include "Engine2D/SyEntity/SyGroup.h"
#include "Engine/SyEntity/SyEntity.h"
#include "Engine/SyEntity/EType.h"
#include "Engine/EntityIdUtils.h"

#include <QObject>
#include <functional>

namespace
{
    void collectGroupedEntityIds(const Eg::SyGroup* group, QSet<QString>& out)
    {
        if (!group)
        {
            return;
        }
        for (const auto* sub : group->subGroups())
        {
            collectGroupedEntityIds(sub, out);
        }
        for (const auto* e : group->entities())
        {
            if (e)
            {
                out.insert(QString::number(e->id));
            }
        }
    }
}  // namespace

QString SceneTreeBuilder2D::typeName(int eType)
{
    switch (static_cast<Eg::EType>(eType))
    {
    case Eg::EType::POINT:
        return QObject::tr("Point");
    case Eg::EType::LINE:
        return QObject::tr("Line");
    case Eg::EType::POLYGON:
        return QObject::tr("Polygon");
    case Eg::EType::ARC:
        return QObject::tr("Arc");
    case Eg::EType::CIRCLE:
        return QObject::tr("Circle");
    case Eg::EType::ELLIPSE:
        return QObject::tr("Ellipse");
    case Eg::EType::BEZIER2:
        return QObject::tr("Bezier2");
    case Eg::EType::BEZIER:
        return QObject::tr("Bezier");
    case Eg::EType::SPLINE:
        return QObject::tr("Spline");
    case Eg::EType::NURBS:
        return QObject::tr("NURBS");
    case Eg::EType::SMARTLINE:
        return QObject::tr("SmartLine");
    case Eg::EType::TEXT:
        return QObject::tr("Text");
    case Eg::EType::BAR_CODE:
        return QObject::tr("BarCode");
    case Eg::EType::QR_CODE:
        return QObject::tr("QRCode");
    case Eg::EType::IMAGE:
        return QObject::tr("Image");
    case Eg::EType::MESH:
        return QObject::tr("Mesh");
    case Eg::EType::GROUP:
        return QObject::tr("Group");
    case Eg::EType::UNKNOWN:
    default:
        return QObject::tr("Unknown");
    }
}

SceneTreeNode2D SceneTreeBuilder2D::buildEntityNode(Eg::SceneManager* scene, LayerManager* layers, const Eg::SyEntity* entity)
{
    SceneTreeNode2D node;
    if (!entity)
    {
        return node;
    }

    node.id = QString::number(entity->id);
    node.typeName = typeName(static_cast<int>(entity->eType));
    node.displayName = entity->name() && *entity->name() ? QString::fromUtf8(entity->name()) : node.typeName;
    node.selected = entity->selected();
    node.visible = entity->visible();
    node.locked = entity->locked();
    node.isGroup = false;

    if (layers && scene)
    {
        const int layerId = layers->getEntityLayer(entity);
        node.layerName = QString::fromStdString(layers->layerName(layerId));
    }
    Q_UNUSED(scene);
    return node;
}

SceneTreeNode2D SceneTreeBuilder2D::buildGroupNode(Eg::SceneManager* scene, LayerManager* layers, Eg::SyGroup* group)
{
    SceneTreeNode2D node;
    if (!group)
    {
        return node;
    }

    node.id = QString::number(group->id);
    node.typeName = QObject::tr("Group");
    const QString groupName = group->name() ? QString::fromUtf8(group->name()) : QString();
    node.displayName = groupName.isEmpty() ? node.typeName : groupName;
    node.isGroup = true;
    node.visible = true;

    bool anySelected = false;
    for (const auto* sub : group->subGroups())
    {
        SceneTreeNode2D child = buildGroupNode(scene, layers, const_cast<Eg::SyGroup*>(sub));
        anySelected = anySelected || child.selected;
        node.children.append(child);
    }
    for (const auto* e : group->entities())
    {
        SceneTreeNode2D child = buildEntityNode(scene, layers, e);
        anySelected = anySelected || child.selected;
        node.children.append(child);
    }
    node.selected = anySelected;
    return node;
}

SceneTreeModel2D SceneTreeBuilder2D::build(Eg::SceneManager* scene, LayerManager* layers)
{
    SceneTreeModel2D model;
    if (!scene)
    {
        return model;
    }

    // 收集所有已入群组的图元 ID，避免顶层重复列出
    QSet<QString> groupedIds;
    for (const auto* group : scene->groupManager().getTopLevelGroups())
    {
        collectGroupedEntityIds(group, groupedIds);
    }

    // 顶层群组节点
    for (auto* group : scene->groupManager().getTopLevelGroups())
    {
        SceneTreeNode2D node = buildGroupNode(scene, layers, group);
        if (node.selected)
        {
            ++model.selectedCount;
        }
        model.nodes.append(node);
    }

    // 未入群组的图元作为顶层节点
    for (const auto& e : scene->getAllEntities())
    {
        if (!e)
        {
            continue;
        }
        if (groupedIds.contains(QString::number(e->id)))
        {
            continue;
        }
        SceneTreeNode2D node = buildEntityNode(scene, layers, e);
        if (node.selected)
        {
            ++model.selectedCount;
        }
        model.nodes.append(node);
    }

    // 统计节点总数（含子节点）
    std::function<void(const SceneTreeNode2D&)> count = [&](const SceneTreeNode2D& n) {
        ++model.totalCount;
        for (const auto& c : n.children)
        {
            count(c);
        }
    };
    for (const auto& n : model.nodes)
    {
        count(n);
    }

    return model;
}

QSet<QString> SceneTreeBuilder2D::selectedIds(Eg::SceneManager* scene)
{
    QSet<QString> ids;
    if (!scene)
    {
        return ids;
    }
    for (const auto* e : scene->getSelectedEntities())
    {
        if (e)
        {
            ids.insert(QString::number(e->id));
        }
    }
    return ids;
}
