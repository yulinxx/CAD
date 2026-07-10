#include "BatchManager.h"

#include <unordered_map>
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
                                           const std::vector<RenderBatch>& newBatches,
                                           const std::set<std::string>& dirtyEntityIds)
{
    RenderFrame result = cachedFrame;

    std::vector<RenderBatch> mergedBatches;
    for (const auto& batch : cachedFrame.batches)
    {
        if (dirtyEntityIds.find(batch.entityId) == dirtyEntityIds.end())
            mergedBatches.push_back(batch);
    }

    std::vector<RenderBatch> dirtyOnlyBatches;
    for (const auto& batch : newBatches)
    {
        if (dirtyEntityIds.find(batch.entityId) != dirtyEntityIds.end())
            dirtyOnlyBatches.push_back(batch);
    }

    mergedBatches.insert(mergedBatches.end(), dirtyOnlyBatches.begin(), dirtyOnlyBatches.end());
    result.batches = mergedBatches;

    return result;
}

// ============================================================================
// 批次分组
// ============================================================================

std::vector<int> BatchManager::groupByPrimitiveType(const RenderFrame& frame) const
{
    std::vector<int> groupIndices;
    if (frame.batches.empty())
        return groupIndices;

    std::unordered_map<int, int> firstIndex;
    for (int i = 0; i < static_cast<int>(frame.batches.size()); ++i)
    {
        const int typeKey = static_cast<int>(frame.batches[i].primitiveType);
        if (firstIndex.find(typeKey) == firstIndex.end())
        {
            firstIndex[typeKey] = i;
            groupIndices.push_back(i);
        }
    }

    return groupIndices;
}

// ============================================================================
// 视口裁剪
// ============================================================================

RenderFrame BatchManager::cullByViewport(const RenderFrame& frame, const RenderRectF& viewportRect) const
{
    if (viewportRect.isNull() || !viewportRect.isValid())
        return frame;

    RenderFrame result = frame;
    std::vector<RenderBatch> visible;
    int culled = 0;

    for (const auto& batch : frame.batches)
    {
        if (batch.boundingBox.isValid() && !viewportRect.intersects(batch.boundingBox))
        {
            ++culled;
            continue;
        }
        visible.push_back(batch);
    }

    result.batches = visible;
    result.statistics.culledBatchCount = culled;
    result.statistics.batchCount = static_cast<int>(visible.size());

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
