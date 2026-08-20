#include "SceneTreeBuilder2D.h"

#include "Engine2D/Core/SceneManager.h"
#include "Engine2D/Core/GroupManager.h"
#include "Engine2D/Interaction/LayerManager.h"
#include "Engine2D/SyEntity/SyGroup.h"
#include "Engine/SyEntity/SyEntity.h"
#include "Engine/SyEntity/EType.h"

#include <QObject>

namespace
{
    void visitGroupMembers(const Eg::SyGroup* group, QSet<qint64>& out)
    {
        if (!group)
        {
            return;
        }
        for (const auto* sub : group->subGroups())
        {
            visitGroupMembers(sub, out);
        }
        for (const auto* e : group->entities())
        {
            if (e)
            {
                out.insert(static_cast<qint64>(e->id));
            }
        }
    }
}  // namespace

QString SceneTreeBuilder2D::typeName(Eg::EType eType)
{
    switch (eType)
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

SceneTreeTopology2D SceneTreeBuilder2D::buildTopology(Eg::SceneManager* scene)
{
    SceneTreeTopology2D topo;
    if (!scene)
    {
        return topo;
    }

    // 收集所有已入群组的图元 ID，避免在顶层重复列出
    QSet<qint64> groupedIds;
    for (auto* group : scene->groupManager().getTopLevelGroups())
    {
        visitGroupMembers(group, groupedIds);
    }

    topo.topLevel.reserve(
        static_cast<int>(groupedIds.size()) + static_cast<int>(scene->groupManager().getTopLevelGroups().size()));

    // 顶层群组
    for (auto* group : scene->groupManager().getTopLevelGroups())
    {
        if (group)
        {
            topo.topLevel.push_back({ static_cast<qint64>(group->id), true });
        }
    }

    // 未入群组的图元作为顶层行
    for (const auto& e : scene->getAllEntities())
    {
        if (!e || groupedIds.contains(static_cast<qint64>(e->id)))
        {
            continue;
        }
        topo.topLevel.push_back({ static_cast<qint64>(e->id), false });
    }

    return topo;
}

QVector<SceneTreeRow2D> SceneTreeBuilder2D::groupMembers(Eg::SceneManager* scene, qint64 groupId)
{
    QVector<SceneTreeRow2D> out;
    if (!scene)
    {
        return out;
    }

    auto* group = scene->groupManager().findGroup(static_cast<Eg::EntityId>(groupId));
    if (!group)
    {
        return out;
    }

    for (auto* sub : group->subGroups())
    {
        if (sub)
        {
            out.push_back({ static_cast<qint64>(sub->id), true });
        }
    }

    for (auto* e : group->entities())
    {
        if (e)
        {
            out.push_back({ static_cast<qint64>(e->id), false });
        }
    }
    return out;
}

SceneTreeRowMeta2D SceneTreeBuilder2D::rowMeta(Eg::SceneManager* scene, LayerManager* layers, const SceneTreeRow2D& row)
{
    SceneTreeRowMeta2D meta;
    if (!scene)
    {
        return meta;
    }

    if (row.isGroup)
    {
        meta.typeName = typeName(Eg::EType::GROUP);
        meta.displayName = meta.typeName;
        meta.visible = true;
        if (auto* group = scene->groupManager().findGroup(static_cast<Eg::EntityId>(row.id)))
        {
            if (group->name() && *group->name())
            {
                meta.displayName = QString::fromUtf8(group->name());
            }
        }
        return meta;
    }

    auto* entity = scene->findEntityById(static_cast<Eg::EntityId>(row.id));
    if (!entity)
    {
        return meta;
    }
    meta.typeName = typeName(entity->eType);
    meta.displayName = (entity->name() && *entity->name()) ? QString::fromUtf8(entity->name()) : meta.typeName;
    meta.visible = entity->visible();
    meta.locked = entity->locked();
    meta.selected = entity->selected();

    if (layers)
        meta.layerName = QString::fromStdString(layers->layerName(layers->getEntityLayer(entity)));

    return meta;
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
            ids.insert(QString::number(static_cast<qint64>(e->id)));
        }
    }
    return ids;
}