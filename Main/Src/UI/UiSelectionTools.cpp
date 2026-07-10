#include "UiSelectionTools.h"

#include "SceneDocument2D.h"
#include "UiGeometryAlgorithms.h"
#include "UiStateCenter.h"

#include "Engine2D/Core/SceneManager.h"
#include "Engine2D/SyEntity/SyLine.h"
#include "Engine2D/SyEntity/SyCircle.h"
#include "Engine2D/SyEntity/SyArc.h"
#include "Mat/Mat.hpp"

#include <memory>

namespace UiSelectionTools
{
    void trimSelectedByPoint(SceneDocument2D* document, const QPointF& point, UiStateCenter* stateCenter)
    {
        if (!document)
            return;
        auto* sm = document->sceneManager();
        if (!sm) return;
        const Ut::Vec2d pt(point.x(), point.y());
        for (auto* entity : sm->getAllEntities())
        {
            if (entity->eType != Eg::EType::LINE)
                continue;
            auto* line = static_cast<Eg::SyLine*>(entity);
            if (line->vPoints.size() < 2)
                continue;
            const auto& a = line->vPoints[0];
            const auto& b = line->vPoints[1];
            const double segLen = (b - a).length();
            const double dline = (segLen < 1e-12)
                ? (pt - a).length()
                : std::abs((b.x() - a.x()) * (a.y() - pt.y()) - (a.x() - pt.x()) * (b.y() - a.y())) / segLen;
            if (dline > 8.0)
                continue;
            const double distA = (pt - a).length();
            const double distB = (pt - b).length();
            const QPointF hit = UiGeometryAlgorithms::projectPointToLine(
                point, QPointF(a.x(), a.y()), QPointF(b.x(), b.y()));
            if (distA < distB)
                line->vPoints[0] = Ut::Vec2d(hit.x(), hit.y());
            else
                line->vPoints[1] = Ut::Vec2d(hit.x(), hit.y());
            break;
        }
        if (stateCenter)
            stateCenter->setDirty(true);
    }

    void extendSelectedByPoint(SceneDocument2D* document, const QPointF& point, UiStateCenter* stateCenter)
    {
        trimSelectedByPoint(document, point, stateCenter);
    }

    void applySelectionTransform(SceneDocument2D* document, const QPointF& anchor, const QPointF& target, bool transformCopy, const QString& mode, UiStateCenter* stateCenter, const QString& toolName)
    {
        Q_UNUSED(mode);
        Q_UNUSED(toolName);
        if (!document)
            return;
        auto* sm = document->sceneManager();
        if (!sm) return;

        const QPointF delta = target - anchor;
        const auto transMat = Ut::Mat3d::translate(delta.x(), delta.y());

        for (auto* entity : sm->getSelectedEntities())
        {
            entity->transform(transMat);
        }

        if (transformCopy)
        {
            for (auto* entity : sm->getSelectedEntities())
            {
                auto copy = entity->clone();
                copy->transform(transMat);
                sm->addEntity(copy.release());
            }
        }

        if (stateCenter)
            stateCenter->setDirty(true);
    }
}
