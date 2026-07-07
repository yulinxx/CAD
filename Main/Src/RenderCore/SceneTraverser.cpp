#include "SceneTraverser.h"

#include "../UI/UiEntities.h"
#include "Engine2D/Core/SceneManager.h"
#include "Engine2D/SyEntity/SyLine.h"
#include "Engine2D/SyEntity/SyCircle.h"
#include "Engine2D/SyEntity/SyArc.h"
#include "Engine2D/SyEntity/SyPolygon.h"

#include <QtMath>

// ============================================================================
// 辅助函数（批次生成工具）
// ============================================================================

namespace
{
    RenderVertex makeVertex(float x, float y, float z, bool selected)
    {
        RenderVertex v;
        v.x = x;
        v.y = y;
        v.z = z;
        if (selected)
        {
            v.r = 0.2f;
            v.g = 0.8f;
            v.b = 1.0f;
        }
        else
        {
            v.r = 1.0f;
            v.g = 1.0f;
            v.b = 1.0f;
        }
        v.a = 1.0f;
        return v;
    }

    RenderBatch makeLineBatch(const QString& entityId, bool selected,
                               const QVector<RenderVertex>& verts)
    {
        RenderBatch batch;
        batch.entityId = entityId;
        batch.selected = selected;
        batch.primitiveType = PrimitiveType::Lines;
        batch.vertices = verts;
        batch.lineWidth = 1.0f;

        if (!verts.isEmpty())
        {
            float minX = verts[0].x, minY = verts[0].y;
            float maxX = verts[0].x, maxY = verts[0].y;
            for (const auto& v : verts)
            {
                minX = qMin(minX, v.x); minY = qMin(minY, v.y);
                maxX = qMax(maxX, v.x); maxY = qMax(maxY, v.y);
            }
            batch.boundingBox = QRectF(QPointF(minX, minY), QPointF(maxX, maxY));
        }

        return batch;
    }

    RenderBatch compileLine(const LineEntity2D& line, bool selected)
    {
        const auto& start = line.start();
        const auto& end = line.end();
        return makeLineBatch(line.id(), selected, {
            makeVertex(start.x(), start.y(), 0.0f, selected),
            makeVertex(end.x(), end.y(), 0.0f, selected)
        });
    }

    RenderBatch compilePolyline(const PolylineEntity2D& polyline, bool selected)
    {
        const auto& points = polyline.points();
        QVector<RenderVertex> verts;
        verts.reserve(points.size());

        for (const auto& p : points)
            verts.append(makeVertex(p.x(), p.y(), 0.0f, selected));

        RenderBatch batch = makeLineBatch(polyline.id(), selected, verts);
        batch.primitiveType = PrimitiveType::LineStrip;
        return batch;
    }

    QVector<RenderVertex> tessellateCircle(float cx, float cy, float radius, int segments = 32)
    {
        QVector<RenderVertex> verts;
        verts.reserve(segments * 2);

        const float step = 360.0f / segments;
        for (int i = 0; i < segments; ++i)
        {
            const float a1 = qDegreesToRadians(i * step);
            const float a2 = qDegreesToRadians((i + 1) * step);

            verts.append(makeVertex(cx + radius * qCos(a1), cy + radius * qSin(a1), 0.0f, false));
            verts.append(makeVertex(cx + radius * qCos(a2), cy + radius * qSin(a2), 0.0f, false));
        }

        return verts;
    }

    QVector<RenderVertex> tessellateArc(float cx, float cy, float radius,
                                       float startAngleDeg, float spanDeg, int segments = 32)
    {
        QVector<RenderVertex> verts;
        const float absSpan = qAbs(spanDeg);
        const int actualSegments = qMax(1, qRound(segments * absSpan / 360.0f));
        verts.reserve(actualSegments * 2);

        const float step = spanDeg / actualSegments;
        for (int i = 0; i < actualSegments; ++i)
        {
            const float a1 = qDegreesToRadians(startAngleDeg + i * step);
            const float a2 = qDegreesToRadians(startAngleDeg + (i + 1) * step);

            verts.append(makeVertex(cx + radius * qCos(a1), cy + radius * qSin(a1), 0.0f, false));
            verts.append(makeVertex(cx + radius * qCos(a2), cy + radius * qSin(a2), 0.0f, false));
        }

        return verts;
    }
}

// ============================================================================
// 2D 场景遍历
// ============================================================================

QList<RenderBatch> SceneTraverser::traverse2D(EntityDocument2D* document, const RenderContext& context)
{
    Q_UNUSED(context);
    QList<RenderBatch> batches;

    if (!document)
        return batches;

    const auto& selection = document->selection();
    const auto selectedItems = selection.items();
    QSet<QString> selectedIdSet;
    for (const auto& item : selectedItems)
    {
        if (item)
            selectedIdSet.insert(item->id());
    }

    for (const auto& line : document->lines())
    {
        if (!line)
            continue;
        bool sel = selectedIdSet.contains(line->id());
        batches.append(compileLine(*line, sel));
    }

    for (const auto& polyline : document->polylines())
    {
        if (!polyline || polyline->points().isEmpty())
            continue;
        bool sel = selectedIdSet.contains(polyline->id());
        batches.append(compilePolyline(*polyline, sel));
    }

    for (const auto& circle : document->circles())
    {
        if (!circle)
            continue;
        bool sel = selectedIdSet.contains(circle->id());
        auto verts = tessellateCircle(circle->center().x(), circle->center().y(), circle->radius());
        if (sel)
        {
            for (auto& v : verts)
            {
                v.r = 0.2f; v.g = 0.8f; v.b = 1.0f;
            }
        }
        batches.append(makeLineBatch(circle->id(), sel, verts));
    }

    for (const auto& arc : document->arcs())
    {
        if (!arc)
            continue;
        bool sel = selectedIdSet.contains(arc->id());
        auto verts = tessellateArc(arc->center().x(), arc->center().y(),
                                    arc->radius(), arc->startAngleDeg(), arc->spanDeg());
        if (sel)
        {
            for (auto& v : verts)
            {
                v.r = 0.2f; v.g = 0.8f; v.b = 1.0f;
            }
        }
        batches.append(makeLineBatch(arc->id(), sel, verts));
    }

    return batches;
}

// ============================================================================
// 2D 场景遍历（新版 Eg::SceneManager 路径）
// ============================================================================

QList<RenderBatch> SceneTraverser::traverse2D(Eg::SceneManager* scene, const RenderContext& context)
{
    Q_UNUSED(context);
    QList<RenderBatch> batches;

    if (!scene)
        return batches;

    // 选中 ID 集合
    auto selectedEntities = scene->getSelectedEntities();
    QSet<QString> selectedIds;
    for (auto* e : selectedEntities)
    {
        if (e)
            selectedIds.insert(QString::number(e->id));
    }

    auto allEntities = scene->getAllEntities();
    for (const auto* entity : allEntities)
    {
        if (!entity)
            continue;

        const QString idStr = QString::number(entity->id);
        const bool sel = selectedIds.contains(idStr);

        switch (entity->eType)
        {
        case Eg::EType::LINE:
        {
            const auto* line = static_cast<const Eg::SyLine*>(entity);
            if (line->vPoints.size() >= 2)
            {
                const auto& p0 = line->vPoints[0];
                const auto& p1 = line->vPoints[1];
                batches.append(makeLineBatch(idStr, sel, {
                    makeVertex(p0.x(), p0.y(), 0.0f, sel),
                    makeVertex(p1.x(), p1.y(), 0.0f, sel)
                }));
            }
            break;
        }
        case Eg::EType::POLYGON:
        {
            const auto* poly = static_cast<const Eg::SyPolygon*>(entity);
            QVector<RenderVertex> verts;
            verts.reserve(poly->vVertices.size());
            for (const auto& pt : poly->vVertices)
                verts.append(makeVertex(pt.x(), pt.y(), 0.0f, sel));
            if (!verts.isEmpty())
            {
                RenderBatch batch = makeLineBatch(idStr, sel, verts);
                batch.primitiveType = PrimitiveType::LineStrip;
                batches.append(batch);
            }
            break;
        }
        case Eg::EType::CIRCLE:
        {
            const auto* circle = static_cast<const Eg::SyCircle*>(entity);
            auto verts = tessellateCircle(
                circle->basePoint.x(), circle->basePoint.y(), circle->dRadius);
            if (sel)
            {
                for (auto& v : verts)
                { v.r = 0.2f; v.g = 0.8f; v.b = 1.0f; }
            }
            batches.append(makeLineBatch(idStr, sel, verts));
            break;
        }
        case Eg::EType::ARC:
        {
            const auto* arc = static_cast<const Eg::SyArc*>(entity);
            double spanDeg = qRadiansToDegrees(arc->dEndAngle - arc->dStartAngle);
            double startDeg = qRadiansToDegrees(arc->dStartAngle);
            auto verts = tessellateArc(
                arc->basePoint.x(), arc->basePoint.y(),
                arc->dRadius, startDeg, spanDeg);
            if (sel)
            {
                for (auto& v : verts)
                { v.r = 0.2f; v.g = 0.8f; v.b = 1.0f; }
            }
            batches.append(makeLineBatch(idStr, sel, verts));
            break;
        }
        default:
            break;
        }
    }

    return batches;
}

// ============================================================================
// 3D 场景遍历
// ============================================================================

QList<RenderBatch> SceneTraverser::traverse3D(SceneDocument3D* document, const RenderContext& context)
{
    Q_UNUSED(context);
    QList<RenderBatch> batches;

    if (!document)
        return batches;

    const auto& selection = document->selection();
    const auto selectedItems = selection.items();
    QSet<QString> selectedIdSet;
    for (const auto& item : selectedItems)
    {
        if (item)
            selectedIdSet.insert(item->id());
    }

    for (const auto& node : document->rootNodes())
    {
        if (!node)
            continue;

        bool sel = selectedIdSet.contains(node->id());

        RenderBatch batch;
        batch.entityId = node->id();
        batch.selected = sel;
        batch.primitiveType = PrimitiveType::Lines;

        const float s = 0.5f;
        batch.vertices = {
            makeVertex(-s,  0,  0, sel),
            makeVertex( s,  0,  0, sel),
            makeVertex( 0, -s,  0, sel),
            makeVertex( 0,  s,  0, sel),
            makeVertex( 0,  0, -s, sel),
            makeVertex( 0,  0,  s, sel),
        };
        batch.boundingBox = QRectF(-s, -s, s * 2, s * 2);
        batches.append(batch);
    }

    return batches;
}