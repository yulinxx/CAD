#include "CommandSnapshots.h"

#include "SceneDocument2D.h"
#include "Engine2D/Core/SceneManager.h"
#include "Engine2D/SyEntity/SyLine.h"
#include "Engine2D/SyEntity/SyCircle.h"
#include "Engine2D/SyEntity/SyArc.h"
#include "Engine2D/SyEntity/SyPolygon.h"
#include "Mat/Mat.hpp"

namespace
{
    // 将QPointF转换为Ut::Vec2d
    inline Ut::Vec2d toVec2d(const QPointF& p)
    {
        return Ut::Vec2d(p.x(), p.y());
    }
} // namespace

// 从实体提取快照数据，用于撤销/重做操作时保存实体状态
EntitySnapshot takeSnapshot(const Eg::SyEntity* entity)
{
    EntitySnapshot snap;
    if (!entity)
        return snap;

    snap.id = QString::number(entity->id);
    snap.type = static_cast<int>(entity->eType);
    snap.basePoint = QPointF(entity->basePoint.x(), entity->basePoint.y());
    snap.closed = entity->bClosed;
    snap.ccw = entity->bCCW;

    switch (entity->eType)
    {
        case Eg::EType::LINE:
        {
            const auto* line = static_cast<const Eg::SyLine*>(entity);
            for (const auto& pt : line->vPoints)
                snap.points.append(QPointF(pt.x(), pt.y()));
            break;
        }
        case Eg::EType::CIRCLE:
        {
            const auto* circle = static_cast<const Eg::SyCircle*>(entity);
            snap.radius = circle->dRadius;
            break;
        }
        case Eg::EType::ARC:
        {
            const auto* arc = static_cast<const Eg::SyArc*>(entity);
            snap.radius = arc->dRadius;
            snap.startAngle = arc->dStartAngle;
            snap.endAngle = arc->dEndAngle;
            break;
        }
        case Eg::EType::POLYGON:
        {
            const auto* polygon = static_cast<const Eg::SyPolygon*>(entity);
            snap.radius = polygon->dCircumRadius;
            for (const auto& v : polygon->vVertices)
                snap.points.append(QPointF(v.x(), v.y()));
            break;
        }
        default:
            break;
    }
    return snap;
}

// 根据快照数据重建实体并插入到场景中（用于实体已被删除的撤销场景）
bool restoreFromSnapshot(SceneDocument2D* document, const EntitySnapshot& snap)
{
    if (!document || snap.id.isEmpty())
        return false;

    auto* sm = document->sceneManager();
    if (!sm)
        return false;

    std::unique_ptr<Eg::SyEntity> entity;
    const auto etype = static_cast<Eg::EType>(snap.type);

    switch (etype)
    {
        case Eg::EType::LINE:
        {
            auto line = std::make_unique<Eg::SyLine>();
            line->eType = Eg::EType::LINE;
            line->bClosed = snap.closed;
            line->bCCW = snap.ccw;
            line->vPoints.reserve(snap.points.size());
            for (const auto& pt : snap.points)
                line->vPoints.push_back(toVec2d(pt));
            if (!line->vPoints.empty())
                line->basePoint = line->vPoints[0];
            entity = std::move(line);
            break;
        }
        case Eg::EType::CIRCLE:
        {
            auto circle = std::make_unique<Eg::SyCircle>();
            circle->eType = Eg::EType::CIRCLE;
            circle->bClosed = true;
            circle->bCCW = snap.ccw;
            circle->basePoint = toVec2d(snap.basePoint);
            circle->dRadius = snap.radius;
            entity = std::move(circle);
            break;
        }
        case Eg::EType::ARC:
        {
            auto arc = std::make_unique<Eg::SyArc>();
            arc->eType = Eg::EType::ARC;
            arc->bCCW = snap.ccw;
            arc->basePoint = toVec2d(snap.basePoint);
            arc->dRadius = snap.radius;
            arc->dStartAngle = snap.startAngle;
            arc->dEndAngle = snap.endAngle;
            entity = std::move(arc);
            break;
        }
        case Eg::EType::POLYGON:
        {
            auto polygon = std::make_unique<Eg::SyPolygon>();
            polygon->eType = Eg::EType::POLYGON;
            polygon->bClosed = true;
            polygon->bCCW = snap.ccw;
            polygon->nSides = snap.points.size();
            polygon->dCircumRadius = snap.radius;
            polygon->vVertices.reserve(snap.points.size());
            for (const auto& pt : snap.points)
                polygon->vVertices.push_back(toVec2d(pt));
            if (!polygon->vVertices.empty())
                polygon->basePoint = polygon->vVertices[0];
            entity = std::move(polygon);
            break;
        }
        default:
            return false;
    }

    bool ok = false;
    const Eg::EntityId originalId = static_cast<Eg::EntityId>(snap.id.toULongLong(&ok));
    if (!ok)
        return false;
    entity->id = originalId;

    return sm->insertEntityPreserveId(std::move(entity));
}

// 根据快照数据恢复实体几何属性（用于实体存在但属性被修改的撤销场景）
void restoreEntityGeometryFromSnapshot(Eg::SyEntity* entity, const EntitySnapshot& snap)
{
    if (!entity)
        return;

    entity->basePoint = toVec2d(snap.basePoint);
    entity->bClosed = snap.closed;
    entity->bCCW = snap.ccw;

    switch (entity->eType)
    {
        case Eg::EType::LINE:
        {
            auto* line = static_cast<Eg::SyLine*>(entity);
            line->vPoints.clear();
            line->vPoints.reserve(snap.points.size());
            for (const auto& pt : snap.points)
                line->vPoints.push_back(toVec2d(pt));
            if (!line->vPoints.empty())
                line->basePoint = line->vPoints[0];
            line->setModified();
            break;
        }
        case Eg::EType::CIRCLE:
        {
            auto* circle = static_cast<Eg::SyCircle*>(entity);
            circle->dRadius = snap.radius;
            circle->setModified();
            break;
        }
        case Eg::EType::ARC:
        {
            auto* arc = static_cast<Eg::SyArc*>(entity);
            arc->dRadius = snap.radius;
            arc->dStartAngle = snap.startAngle;
            arc->dEndAngle = snap.endAngle;
            arc->setModified();
            break;
        }
        case Eg::EType::POLYGON:
        {
            auto* polygon = static_cast<Eg::SyPolygon*>(entity);
            polygon->vVertices.clear();
            polygon->vVertices.reserve(snap.points.size());
            for (const auto& pt : snap.points)
                polygon->vVertices.push_back(toVec2d(pt));
            polygon->nSides = snap.points.size();
            polygon->dCircumRadius = snap.radius;
            if (!polygon->vVertices.empty())
                polygon->basePoint = polygon->vVertices[0];
            polygon->setModified();
            break;
        }
        default:
            entity->setModified();
            break;
    }
}