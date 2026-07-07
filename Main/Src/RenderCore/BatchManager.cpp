#include "BatchManager.h"

#include <QHash>
#include <chrono>

// ============================================================================
// 缓存管理
// ============================================================================

void BatchManager::cacheFrame(const RenderFrame& frame)
{
    m_cachedFrame = frame;
    m_hasCachedFrame = true;
}

const RenderFrame& BatchManager::cachedFrame() const
{
    return m_cachedFrame;
}

bool BatchManager::hasCachedFrame() const
{
    return m_hasCachedFrame;
}

void BatchManager::invalidateCache()
{
    m_hasCachedFrame = false;
    m_cachedFrame = RenderFrame{};
}

// ============================================================================
// 增量批次合并
// ============================================================================

RenderFrame BatchManager::mergeIncremental(const RenderFrame& cachedFrame,
                                           const QList<RenderBatch>& newBatches,
                                           const QSet<QString>& dirtyEntityIds)
{
    RenderFrame result = cachedFrame;

    QList<RenderBatch> mergedBatches;
    for (const auto& batch : cachedFrame.batches)
    {
        if (!dirtyEntityIds.contains(batch.entityId))
            mergedBatches.append(batch);
    }

    QList<RenderBatch> dirtyOnlyBatches;
    for (const auto& batch : newBatches)
    {
        if (dirtyEntityIds.contains(batch.entityId))
            dirtyOnlyBatches.append(batch);
    }

    mergedBatches.append(dirtyOnlyBatches);
    result.batches = mergedBatches;

    return result;
}

// ============================================================================
// 批次分组
// ============================================================================

QVector<int> BatchManager::groupByPrimitiveType(const RenderFrame& frame) const
{
    QVector<int> groupIndices;
    if (frame.batches.isEmpty())
        return groupIndices;

    QHash<int, int> firstIndex;
    for (int i = 0; i < frame.batches.size(); ++i)
    {
        const int typeKey = static_cast<int>(frame.batches[i].primitiveType);
        if (!firstIndex.contains(typeKey))
        {
            firstIndex[typeKey] = i;
            groupIndices.append(i);
        }
    }

    return groupIndices;
}

// ============================================================================
// 视口裁剪
// ============================================================================

RenderFrame BatchManager::cullByViewport(const RenderFrame& frame, const QRectF& viewportRect) const
{
    if (viewportRect.isNull() || !viewportRect.isValid())
        return frame;

    RenderFrame result = frame;
    QList<RenderBatch> visible;
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

// ============================================================================
// 统计填充
// ============================================================================

void BatchManager::fillStatistics(RenderFrame& frame, const RenderContext& context,
                                  const std::chrono::steady_clock::time_point& t0,
                                  const std::chrono::steady_clock::time_point& compileStart,
                                  const std::chrono::steady_clock::time_point& compileEnd)
{
    auto& stats = frame.statistics;
    stats.frameId = context.frameId;
    stats.timestamp = t0;
    stats.batchCount = frame.batchCount();
    stats.totalVertexCount = frame.totalVertexCount();
    stats.entityCount = frame.entityCount();
    stats.compileTimeMs = std::chrono::duration<float, std::milli>(compileEnd - compileStart).count();
    stats.frameTimeMs = std::chrono::duration<float, std::milli>(compileEnd - t0).count();
}