#include "UiSelectionTools.h"

#include <memory>

#include "UiEntities.h"
#include "UiGeometryAlgorithms.h"
#include "UiStateCenter.h"

namespace UiSelectionTools
{
    namespace
    {
        std::shared_ptr<UiEntity> cloneAndTranslate(const std::shared_ptr<UiEntity>& entity, const QPointF& delta)
        {
            if (!entity)
                return {};

            if (auto line = std::dynamic_pointer_cast<LineEntity2D>(entity))
                return std::make_shared<LineEntity2D>(line->id(), line->start() + delta, line->end() + delta);
            if (auto circle = std::dynamic_pointer_cast<CircleEntity2D>(entity))
                return std::make_shared<CircleEntity2D>(circle->id(), circle->center() + delta, circle->radius());
            if (auto polyline = std::dynamic_pointer_cast<PolylineEntity2D>(entity))
            {
                auto pts = polyline->points();
                for (auto& pt : pts)
                    pt += delta;
                return std::make_shared<PolylineEntity2D>(polyline->id(), pts);
            }
            return {};
        }
    }

    void trimSelectedByPoint(EntityDocument2D* document, const QPointF& point, UiStateCenter* stateCenter)
    {
        if (!document)
            return;
        for (const auto& line : document->lines())
        {
            if (!line)
                continue;
            if (line->distanceToPoint(point) > 8.0)
                continue;
            const QPointF hit = UiGeometryAlgorithms::projectPointToLine(point, line->start(), line->end());
            if (line->distanceToStart(hit) < line->distanceToEnd(hit))
                line->setStart(hit);
            else
                line->setEnd(hit);
            break;
        }
        if (stateCenter)
            stateCenter->setDirty(true);
    }

    void extendSelectedByPoint(EntityDocument2D* document, const QPointF& point, UiStateCenter* stateCenter)
    {
        trimSelectedByPoint(document, point, stateCenter);
    }

    void applySelectionTransform(EntityDocument2D* document, const QPointF& anchor, const QPointF& target, bool transformCopy, const QString& mode, UiStateCenter* stateCenter, const QString& toolName)
    {
        Q_UNUSED(mode);
        Q_UNUSED(toolName);
        if (!document)
            return;

        const QPointF delta = target - anchor;
        for (const auto& entity : document->selection().items())
        {
            if (!entity)
                continue;
            if (auto line = std::dynamic_pointer_cast<LineEntity2D>(entity))
            {
                line->setStart(line->start() + delta);
                line->setEnd(line->end() + delta);
            }
            else if (auto circle = std::dynamic_pointer_cast<CircleEntity2D>(entity))
            {
                circle->setCenter(circle->center() + delta);
            }
            else if (auto polyline = std::dynamic_pointer_cast<PolylineEntity2D>(entity))
            {
                polyline->translate(delta);
            }
        }

        if (transformCopy)
        {
            std::vector<std::shared_ptr<UiEntity>> copies;
            for (const auto& entity : document->selection().items())
            {
                if (!entity)
                    continue;
                if (auto line = std::dynamic_pointer_cast<LineEntity2D>(entity))
                    copies.push_back(document->createLine(line->start() + delta, line->end() + delta));
                else if (auto circle = std::dynamic_pointer_cast<CircleEntity2D>(entity))
                    copies.push_back(document->createCircle(circle->center() + delta, circle->radius()));
                else if (auto polyline = std::dynamic_pointer_cast<PolylineEntity2D>(entity))
                {
                    auto pts = polyline->points();
                    for (auto& pt : pts)
                        pt += delta;
                    copies.push_back(document->createPolyline(pts));
                }
            }
            for (const auto& copy : copies)
                document->selection().add(copy);
        }

        if (stateCenter)
            stateCenter->setDirty(true);
    }
}
