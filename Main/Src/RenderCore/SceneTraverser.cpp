#include "SceneTraverser.h"

#include "../UI/UiEntities.h"
#include "Engine2D/Core/SceneManager.h"
#include "Engine2D/SyEntity/SyLine.h"
#include "Engine2D/SyEntity/SyCircle.h"
#include "Engine2D/SyEntity/SyArc.h"
#include "Engine2D/SyEntity/SyPolygon.h"

#include <cmath>
#include <set>

#include "Log/SyLogger.h"

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

    RenderBatch makeLineBatch(const std::string& entityId, bool selected,
                               const std::vector<RenderVertex>& verts)
    {
        RenderBatch batch;
        batch.entityId = entityId;
        batch.selected = selected;
        batch.primitiveType = PrimitiveType::Lines;
        batch.vertices = verts;
        batch.lineWidth = 1.0f;

        if (!verts.empty())
        {
            float minX = verts[0].x, minY = verts[0].y;
            float maxX = verts[0].x, maxY = verts[0].y;
            for (const auto& v : verts)
            {
                minX = std::min(minX, v.x); minY = std::min(minY, v.y);
                maxX = std::max(maxX, v.x); maxY = std::max(maxY, v.y);
            }
            batch.boundingBox = RenderRectF{ minX, minY, maxX - minX, maxY - minY };
        }

        return batch;
    }

    std::vector<RenderVertex> tessellateCircle(float cx, float cy, float radius, int segments = 32)
    {
        std::vector<RenderVertex> verts;
        verts.reserve(segments * 2);

        const float step = 360.0f / segments;
        for (int i = 0; i < segments; ++i)
        {
            const float a1 = i * step * static_cast<float>(M_PI) / 180.0f;
            const float a2 = (i + 1) * step * static_cast<float>(M_PI) / 180.0f;

            verts.push_back(makeVertex(cx + radius * std::cos(a1), cy + radius * std::sin(a1), 0.0f, false));
            verts.push_back(makeVertex(cx + radius * std::cos(a2), cy + radius * std::sin(a2), 0.0f, false));
        }

        return verts;
    }

    std::vector<RenderVertex> tessellateArc(float cx, float cy, float radius,
                                           float startAngleDeg, float spanDeg, int segments = 32)
    {
        std::vector<RenderVertex> verts;
        const float absSpan = std::abs(spanDeg);
        const int actualSegments = std::max(1, static_cast<int>(std::round(segments * absSpan / 360.0f)));
        verts.reserve(actualSegments * 2);

        const float step = spanDeg / actualSegments;
        for (int i = 0; i < actualSegments; ++i)
        {
            const float a1 = (startAngleDeg + i * step) * static_cast<float>(M_PI) / 180.0f;
            const float a2 = (startAngleDeg + (i + 1) * step) * static_cast<float>(M_PI) / 180.0f;

            verts.push_back(makeVertex(cx + radius * std::cos(a1), cy + radius * std::sin(a1), 0.0f, false));
            verts.push_back(makeVertex(cx + radius * std::cos(a2), cy + radius * std::sin(a2), 0.0f, false));
        }

        return verts;
    }
}

// ============================================================================
// 2D 场景遍历
// ============================================================================

std::vector<RenderBatch> SceneTraverser::traverse2D(Eg::SceneManager* scene, const RenderContext& context)
{
    (void)context;
    std::vector<RenderBatch> batches;

    if (!scene)
    {
        SY_WARN("[SceneTraverser] traverse2D called with null scene");
        return batches;
    }

    SY_DEBUG("[SceneTraverser] traverse2D started");

    auto selectedEntities = scene->getSelectedEntities();
    std::set<std::string> selectedIds;
    for (auto* e : selectedEntities)
    {
        if (e)
            selectedIds.insert(std::to_string(e->id));
    }

    auto allEntities = scene->getAllEntities();
    SY_DEBUG("[SceneTraverser] traverse2D processing %d entities, %d selected",
        static_cast<int>(allEntities.size()), static_cast<int>(selectedIds.size()));

    for (const auto* entity : allEntities)
    {
        if (!entity)
            continue;

        const std::string idStr = std::to_string(entity->id);
        const bool sel = selectedIds.find(idStr) != selectedIds.end();

        switch (entity->eType)
        {
        case Eg::EType::LINE:
        {
            const auto* line = static_cast<const Eg::SyLine*>(entity);
            if (line->vPoints.size() >= 2)
            {
                const auto& p0 = line->vPoints[0];
                const auto& p1 = line->vPoints[1];
                batches.push_back(makeLineBatch(idStr, sel, {
                    makeVertex(p0.x(), p0.y(), 0.0f, sel),
                    makeVertex(p1.x(), p1.y(), 0.0f, sel)
                }));
            }
            break;
        }
        case Eg::EType::POLYGON:
        {
            const auto* poly = static_cast<const Eg::SyPolygon*>(entity);
            std::vector<RenderVertex> verts;
            verts.reserve(poly->vVertices.size());
            for (const auto& pt : poly->vVertices)
                verts.push_back(makeVertex(pt.x(), pt.y(), 0.0f, sel));
            if (!verts.empty())
            {
                RenderBatch batch = makeLineBatch(idStr, sel, verts);
                batch.primitiveType = PrimitiveType::LineStrip;
                batches.push_back(batch);
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
            batches.push_back(makeLineBatch(idStr, sel, verts));
            break;
        }
        case Eg::EType::ARC:
        {
            const auto* arc = static_cast<const Eg::SyArc*>(entity);
            double spanDeg = (arc->dEndAngle - arc->dStartAngle) * 180.0 / M_PI;
            double startDeg = arc->dStartAngle * 180.0 / M_PI;
            auto verts = tessellateArc(
                arc->basePoint.x(), arc->basePoint.y(),
                arc->dRadius, static_cast<float>(startDeg), static_cast<float>(spanDeg));
            if (sel)
            {
                for (auto& v : verts)
                { v.r = 0.2f; v.g = 0.8f; v.b = 1.0f; }
            }
            batches.push_back(makeLineBatch(idStr, sel, verts));
            break;
        }
        default:
            break;
        }
    }

    SY_INFO("[SceneTraverser] traverse2D completed batches=%d", static_cast<int>(batches.size()));
    return batches;
}

// ============================================================================
// 3D 场景遍历
// ============================================================================

std::vector<RenderBatch> SceneTraverser::traverse3D(SceneDocument3D* document, const RenderContext& context)
{
    (void)context;
    std::vector<RenderBatch> batches;

    if (!document)
    {
        SY_WARN("[SceneTraverser] traverse3D called with null document");
        return batches;
    }

    SY_DEBUG("[SceneTraverser] traverse3D started");

    const auto& selection = document->selection();
    const auto selectedItems = selection.items();
    std::set<std::string> selectedIdSet;
    for (const auto& item : selectedItems)
    {
        if (item)
            selectedIdSet.insert(item->id());
    }

    SY_DEBUG("[SceneTraverser] traverse3D processing %d nodes, %d selected",
        static_cast<int>(document->rootNodes().size()), static_cast<int>(selectedIdSet.size()));

    for (const auto& node : document->rootNodes())
    {
        if (!node)
            continue;

        std::string nodeId = node->id();
        bool sel = selectedIdSet.find(nodeId) != selectedIdSet.end();

        RenderBatch batch;
        batch.entityId = nodeId;
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
        batch.boundingBox = RenderRectF{ -s, -s, s * 2, s * 2 };
        batches.push_back(batch);
    }

    SY_INFO("[SceneTraverser] traverse3D completed batches=%d", static_cast<int>(batches.size()));
    return batches;
}
