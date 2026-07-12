#include "CommandGeometry.h"

#include <cmath>

#include "Engine2D/SyEntity/SyLine.h"
#include "Engine2D/SyEntity/SyCircle.h"
#include "Engine2D/SyEntity/SyArc.h"
#include "Engine2D/SyEntity/SyPolygon.h"
#include "Engine2D/SyEntity/SyBezier2.h"
#include "Engine2D/SyEntity/SyBezier.h"
#include "Engine2D/SyEntity/SyNurbs.h"
#include "Engine2D/SyEntity/SySmartLine.h"

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

// 对单个 Ut::Vec2d 点应用镜像
static Ut::Vec2d mirrorVec2d(const Ut::Vec2d& pt, const Ut::Vec2d& aStart, const Ut::Vec2d& aEnd)
{
    const double dx = aEnd.x() - aStart.x();
    const double dy = aEnd.y() - aStart.y();
    const double len2 = dx * dx + dy * dy;
    if (len2 < 1e-8)
        return pt;
    const double t = ((pt.x() - aStart.x()) * dx + (pt.y() - aStart.y()) * dy) / len2;
    return Ut::Vec2d(
        2.0 * (aStart.x() + t * dx) - pt.x(),
        2.0 * (aStart.y() + t * dy) - pt.y()
    );
}

void applyMirrorToEntity(Eg::SyEntity* entity, const QPointF& axisStart, const QPointF& axisEnd)
{
    if (!entity)
        return;

    const Ut::Vec2d aStart(axisStart.x(), axisStart.y());
    const Ut::Vec2d aEnd(axisEnd.x(), axisEnd.y());

    entity->basePoint = mirrorVec2d(entity->basePoint, aStart, aEnd);

    switch (entity->eType)
    {
        case Eg::EType::LINE:
        case Eg::EType::SMARTLINE:
        {
            auto* line = static_cast<Eg::SyLine*>(entity);
            for (auto& pt : line->vPoints)
                pt = mirrorVec2d(pt, aStart, aEnd);
            if (!line->vPoints.empty())
                line->basePoint = line->vPoints[0];
            line->setModified();
            break;
        }
        case Eg::EType::POLYGON:
        {
            auto* polygon = static_cast<Eg::SyPolygon*>(entity);
            for (auto& v : polygon->vVertices)
                v = mirrorVec2d(v, aStart, aEnd);
            if (!polygon->vVertices.empty())
                polygon->basePoint = polygon->vVertices[0];
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
        case Eg::EType::BEZIER2:
        {
            auto* bezier2 = static_cast<Eg::SyBezier2*>(entity);
            bezier2->ptCtrl = mirrorVec2d(bezier2->ptCtrl, aStart, aEnd);
            bezier2->ptEnd = mirrorVec2d(bezier2->ptEnd, aStart, aEnd);
            bezier2->setModified();
            break;
        }
        case Eg::EType::BEZIER:
        {
            auto* bezier = static_cast<Eg::SyBezier*>(entity);
            bezier->ptCtrl0 = mirrorVec2d(bezier->ptCtrl0, aStart, aEnd);
            bezier->ptCtrl1 = mirrorVec2d(bezier->ptCtrl1, aStart, aEnd);
            bezier->ptEnd = mirrorVec2d(bezier->ptEnd, aStart, aEnd);
            bezier->setModified();
            break;
        }
        case Eg::EType::SPLINE:
        {
            auto* nurbs = static_cast<Eg::SyNurbs*>(entity);
            for (auto& cp : nurbs->vControlPoints)
                cp = mirrorVec2d(cp, aStart, aEnd);
            nurbs->setModified();
            break;
        }
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
        case Eg::EType::SMARTLINE:
        {
            auto* line = static_cast<Eg::SyLine*>(entity);
            for (auto& pt : line->vPoints)
                pt = toVec2d(rotatePoint(QPointF(pt.x(), pt.y()), center, cosAngle, sinAngle));
            if (!line->vPoints.empty())
                line->basePoint = line->vPoints.front();
            line->setModified();
            break;
        }
        case Eg::EType::POLYGON:
        {
            auto* polygon = static_cast<Eg::SyPolygon*>(entity);
            for (auto& v : polygon->vVertices)
                v = toVec2d(rotatePoint(QPointF(v.x(), v.y()), center, cosAngle, sinAngle));
            if (!polygon->vVertices.empty())
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
        case Eg::EType::BEZIER2:
        {
            auto* bezier2 = static_cast<Eg::SyBezier2*>(entity);
            bezier2->basePoint = toVec2d(rotatePoint(QPointF(bezier2->basePoint.x(), bezier2->basePoint.y()), center, cosAngle, sinAngle));
            bezier2->ptCtrl = toVec2d(rotatePoint(QPointF(bezier2->ptCtrl.x(), bezier2->ptCtrl.y()), center, cosAngle, sinAngle));
            bezier2->ptEnd = toVec2d(rotatePoint(QPointF(bezier2->ptEnd.x(), bezier2->ptEnd.y()), center, cosAngle, sinAngle));
            bezier2->setModified();
            break;
        }
        case Eg::EType::BEZIER:
        {
            auto* bezier = static_cast<Eg::SyBezier*>(entity);
            bezier->basePoint = toVec2d(rotatePoint(QPointF(bezier->basePoint.x(), bezier->basePoint.y()), center, cosAngle, sinAngle));
            bezier->ptCtrl0 = toVec2d(rotatePoint(QPointF(bezier->ptCtrl0.x(), bezier->ptCtrl0.y()), center, cosAngle, sinAngle));
            bezier->ptCtrl1 = toVec2d(rotatePoint(QPointF(bezier->ptCtrl1.x(), bezier->ptCtrl1.y()), center, cosAngle, sinAngle));
            bezier->ptEnd = toVec2d(rotatePoint(QPointF(bezier->ptEnd.x(), bezier->ptEnd.y()), center, cosAngle, sinAngle));
            bezier->setModified();
            break;
        }
        case Eg::EType::SPLINE:
        {
            auto* nurbs = static_cast<Eg::SyNurbs*>(entity);
            for (auto& cp : nurbs->vControlPoints)
                cp = toVec2d(rotatePoint(QPointF(cp.x(), cp.y()), center, cosAngle, sinAngle));
            nurbs->setModified();
            break;
        }
        default:
            break;
    }
}