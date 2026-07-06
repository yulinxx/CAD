#include "DefaultSceneCompiler.h"

#include <QString>
#include <QSet>
#include <QtMath>
#include <chrono>

#include "../UI/UiEntities.h"

// ============================================================================
// 辅助函数
// ============================================================================

namespace
{
    /// 创建默认的选中颜色
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

    /// 从线段创建批次
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

    /// 编译单条线段
    RenderBatch compileLine(const LineEntity2D& line, bool selected)
    {
        const auto& start = line.start();
        const auto& end = line.end();
        return makeLineBatch(line.id(), selected, {
            makeVertex(start.x(), start.y(), 0.0f, selected),
            makeVertex(end.x(), end.y(), 0.0f, selected)
        });
    }

    /// 编译折线
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
}

// ============================================================================
// 圆/圆弧细分
// ============================================================================

QVector<RenderVertex> DefaultSceneCompiler::tessellateCircle(float cx, float cy, float radius, int segments)
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

QVector<RenderVertex> DefaultSceneCompiler::tessellateArc(float cx, float cy, float radius,
                                                           float startAngleDeg, float spanDeg, int segments)
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

// ============================================================================
// 2D 场景编译
// ============================================================================

QVector<RenderBatch> DefaultSceneCompiler::compileEntities2D(EntityDocument2D* document,
                                                              const RenderContext& context)
{
    Q_UNUSED(context);
    QVector<RenderBatch> batches;

    if (!document)
        return batches;

    const auto& selection = document->selection();
    const auto selectedIds = selection.items();
    QSet<QString> selectedIdSet;
    for (const auto& item : selectedIds)
    {
        if (item)
            selectedIdSet.insert(item->id());
    }

    // 线段
    for (const auto& line : document->lines())
    {
        if (!line)
            continue;
        bool sel = selectedIdSet.contains(line->id());
        batches.append(compileLine(*line, sel));
    }

    // 折线
    for (const auto& polyline : document->polylines())
    {
        if (!polyline || polyline->points().isEmpty())
            continue;
        bool sel = selectedIdSet.contains(polyline->id());
        batches.append(compilePolyline(*polyline, sel));
    }

    // 圆
    for (const auto& circle : document->circles())
    {
        if (!circle)
            continue;
        bool sel = selectedIdSet.contains(circle->id());
        auto verts = tessellateCircle(circle->center().x(), circle->center().y(), circle->radius());
        // 应用选择颜色
        if (sel)
        {
            for (auto& v : verts)
            {
                v.r = 0.2f; v.g = 0.8f; v.b = 1.0f;
            }
        }
        batches.append(makeLineBatch(circle->id(), sel, verts));
    }

    // 圆弧
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
// 3D 场景编译
// ============================================================================

QVector<RenderBatch> DefaultSceneCompiler::compileEntities3D(SceneDocument3D* document,
                                                              const RenderContext& context)
{
    Q_UNUSED(context);
    QVector<RenderBatch> batches;

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

    // 遍历根节点生成占位批次
    for (const auto& node : document->rootNodes())
    {
        if (!node)
            continue;

        bool sel = selectedIdSet.contains(node->id());

        // 每个节点生成一个简单的十字标记
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

// ============================================================================
// 全量编译
// ============================================================================

RenderFrame DefaultSceneCompiler::compile(EntityDocument2D* document, const RenderContext& context)
{
    const auto t0 = std::chrono::steady_clock::now();

    RenderFrame frame;
    frame.frameId = context.frameId;
    frame.timestamp = t0;

    if (!document)
    {
        frame.valid = false;
        frame.description = QStringLiteral("No 2D document");
        return frame;
    }

    const auto compileStart = std::chrono::steady_clock::now();
    frame.batches = compileEntities2D(document, context);
    const auto compileEnd = std::chrono::steady_clock::now();

    frame.valid = true;
    frame.description = QStringLiteral("Compiled 2D scene with %1 entities on %2")
        .arg(document->entities().size())
        .arg(context.backendName);

    // 填充统计
    auto& stats = frame.statistics;
    stats.frameId = context.frameId;
    stats.timestamp = t0;
    stats.batchCount = frame.batchCount();
    stats.totalVertexCount = frame.totalVertexCount();
    stats.entityCount = frame.entityCount();
    stats.compileTimeMs = std::chrono::duration<float, std::milli>(compileEnd - compileStart).count();
    stats.frameTimeMs = std::chrono::duration<float, std::milli>(compileEnd - t0).count();

    // 缓存帧
    m_cachedFrame = frame;
    m_hasCachedFrame = true;

    return frame;
}

RenderFrame DefaultSceneCompiler::compile(SceneDocument3D* document, const RenderContext& context)
{
    const auto t0 = std::chrono::steady_clock::now();

    RenderFrame frame;
    frame.frameId = context.frameId;
    frame.timestamp = t0;

    if (!document)
    {
        frame.valid = false;
        frame.description = QStringLiteral("No 3D document");
        return frame;
    }

    const auto compileStart = std::chrono::steady_clock::now();
    frame.batches = compileEntities3D(document, context);
    const auto compileEnd = std::chrono::steady_clock::now();

    frame.valid = true;
    frame.description = QStringLiteral("Compiled 3D scene with %1 entities on %2")
        .arg(document->entities().size())
        .arg(context.backendName);

    auto& stats = frame.statistics;
    stats.frameId = context.frameId;
    stats.timestamp = t0;
    stats.batchCount = frame.batchCount();
    stats.totalVertexCount = frame.totalVertexCount();
    stats.entityCount = frame.entityCount();
    stats.compileTimeMs = std::chrono::duration<float, std::milli>(compileEnd - compileStart).count();
    stats.frameTimeMs = std::chrono::duration<float, std::milli>(compileEnd - t0).count();

    m_cachedFrame = frame;
    m_hasCachedFrame = true;

    return frame;
}

// ============================================================================
// 增量编译
// ============================================================================

RenderFrame DefaultSceneCompiler::compileIncremental(EntityDocument2D* document,
                                                      const RenderContext& context,
                                                      const RenderFrame& previousFrame)
{
    // 如果没有脏标记或上下文未变化，直接返回缓存
    if (!context.isDirty && m_hasCachedFrame && m_cachedFrame.frameId == previousFrame.frameId)
    {
        RenderFrame result = previousFrame;
        result.statistics.frameTimeMs = 0.0f;
        return result;
    }

    // 否则回退到全量编译
    return compile(document, context);
}

RenderFrame DefaultSceneCompiler::compileIncremental(SceneDocument3D* document,
                                                      const RenderContext& context,
                                                      const RenderFrame& previousFrame)
{
    if (!context.isDirty && m_hasCachedFrame && m_cachedFrame.frameId == previousFrame.frameId)
    {
        RenderFrame result = previousFrame;
        result.statistics.frameTimeMs = 0.0f;
        return result;
    }

    return compile(document, context);
}

// ============================================================================
// 缓存控制
// ============================================================================

void DefaultSceneCompiler::invalidateCache()
{
    m_hasCachedFrame = false;
    m_cachedFrame = RenderFrame{};
}

bool DefaultSceneCompiler::hasCachedFrame() const
{
    return m_hasCachedFrame;
}

uint64_t DefaultSceneCompiler::cachedFrameId() const
{
    return m_hasCachedFrame ? m_cachedFrame.frameId : 0;
}

// ============================================================================
// 批次查询
// ============================================================================

QVector<int> DefaultSceneCompiler::groupBatchesByPrimitiveType(const RenderFrame& frame) const
{
    Q_UNUSED(frame);
    // 返回空的批次索引列表（当前按实体独立批次，不合并）
    return {};
}

RenderFrame DefaultSceneCompiler::cullBatches(const RenderFrame& frame, const QRectF& viewportRect) const
{
    if (viewportRect.isNull() || !viewportRect.isValid())
        return frame;

    RenderFrame result = frame;
    QVector<RenderBatch> visible;
    int culled = 0;

    for (const auto& batch : frame.batches)
    {
        if (batch.boundingBox.isValid() && !viewportRect.intersects(batch.boundingBox))
        {
            ++culled;
            continue;
        }
        visible.append(batch);
    }

    result.batches = visible;
    result.statistics.culledBatchCount = culled;
    result.statistics.batchCount = visible.size();

    return result;
}