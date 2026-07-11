#include "CommandGeometry.h"

#include <cmath>

#include "Engine2D/SyEntity/SyLine.h"
#include "Engine2D/SyEntity/SyCircle.h"
#include "Engine2D/SyEntity/SyArc.h"
#include "Engine2D/SyEntity/SyPolygon.h"

inline Ut::Vec2d toVec2d(const QPointF& p)
{
    return Ut::Vec2d(p.x(), p.y());
}

QPointF rotatePoint(const QPointF& point, const QPointF& center, double cosAngle, double sinAngle)
{
    const double dx = point.x() - center.x();
    const double dy = point.y() - center.y();
    return QPointF(
        center.x() + dx * cosAngle - dy * sinAngle,
        center.y() + dx * sinAngle + dy * cosAngle
    );
}

QPointF mirrorPoint(const QPointF& pt, const QPointF& axisStart, const QPointF& axisEnd)
{
    const double dx = axisEnd.x() - axisStart.x();
    const double dy = axisEnd.y() - axisStart.y();
    const double len2 = dx * dx + dy * dy;

    if (len2 < 1e-8)
        return pt;

    const double t = ((pt.x() - axisStart.x()) * dx + (pt.y() - axisStart.y()) * dy) / len2;
    const double projX = axisStart.x() + t * dx;
    const double projY = axisStart.y() + t * dy;

    return QPointF(2.0 * projX - pt.x(), 2.0 * projY - pt.y());
}

void applyMirrorToEntity(Eg::SyEntity* entity, const QPointF& axisStart, const QPointF& axisEnd)
{
    if (!entity)
        return;

    entity->basePoint = toVec2d(mirrorPoint(QPointF(entity->basePoint.x(), entity->basePoint.y()), axisStart, axisEnd));

    switch (entity->eType)
    {
    case Eg::EType::LINE:
    {
        auto* line = static_cast<Eg::SyLine*>(entity);
        for (auto& pt : line->vPoints)
            pt = toVec2d(mirrorPoint(QPointF(pt.x(), pt.y()), axisStart, axisEnd));
        line->setModified();
        break;
    }
    case Eg::EType::POLYGON:
    {
        auto* polygon = static_cast<Eg::SyPolygon*>(entity);
        for (auto& v : polygon->vVertices)
            v = toVec2d(mirrorPoint(QPointF(v.x(), v.y()), axisStart, axisEnd));
        polygon->setModified();
        break;
    }
    case Eg::EType::ARC:
    {
        auto* arc = static_cast<Eg::SyArc*>(entity);
        const double angle = atan2(axisEnd.y() - axisStart.y(), axisEnd.x() - axisStart.x());
        const double mirrorAngle = 2.0 * angle;
        arc->dStartAngle = mirrorAngle - arc->dStartAngle;
        arc->dEndAngle = mirrorAngle - arc->dEndAngle;
        std::swap(arc->dStartAngle, arc->dEndAngle);
        arc->setModified();
        break;
    }
    case Eg::EType::CIRCLE:
        entity->setModified();
        break;
    default:
        break;
    }
}

void applyRotationToEntity(Eg::SyEntity* entity, const QPointF& center, double angleDelta)
{
    if (!entity)
        return;

    const double cosAngle = cos(angleDelta);
    const double sinAngle = sin(angleDelta);

    switch (entity->eType)
    {
    case Eg::EType::LINE:
    {
        auto* line = static_cast<Eg::SyLine*>(entity);
        for (auto& pt : line->vPoints)
            pt = toVec2d(rotatePoint(QPointF(pt.x(), pt.y()), center, cosAngle, sinAngle));
        line->basePoint = line->vPoints.front();
        line->setModified();
        break;
    }
    case Eg::EType::POLYGON:
    {
        auto* polygon = static_cast<Eg::SyPolygon*>(entity);
        for (auto& v : polygon->vVertices)
            v = toVec2d(rotatePoint(QPointF(v.x(), v.y()), center, cosAngle, sinAngle));
        polygon->basePoint = polygon->vVertices.front();
        polygon->setModified();
        break;
    }
    case Eg::EType::ARC:
    {
        auto* arc = static_cast<Eg::SyArc*>(entity);
        arc->basePoint = toVec2d(rotatePoint(QPointF(arc->basePoint.x(), arc->basePoint.y()), center, cosAngle, sinAngle));
        arc->dStartAngle += angleDelta;
        arc->dEndAngle += angleDelta;
        arc->setModified();
        break;
    }
    case Eg::EType::CIRCLE:
    {
        auto* circle = static_cast<Eg::SyCircle*>(entity);
        circle->basePoint = toVec2d(rotatePoint(QPointF(circle->basePoint.x(), circle->basePoint.y()), center, cosAngle, sinAngle));
        circle->setModified();
        break;
    }
    default:
        break;
    }
}