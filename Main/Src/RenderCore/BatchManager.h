#pragma once

#include "RenderTypes.h"
#include "RenderFrame.h"
#include "RenderContext.h"

#include <vector>
#include <set>
#include <string>
#include <chrono>
#include <unordered_map>

/**
 * @file BatchManager.h
 * @brief 批次管理器
 *
 * 负责批次的缓存、合并、分组和裁剪。
 *
 * 职责边界：
 * - 缓存当前帧批次
 * - 合并增量更新的批次
 * - 按图元类型分组批次
 * - 视口裁剪
 * - 填充帧统计信息
 *
 * 不承担：
 * - 场景遍历（由 SceneTraverser 负责）
 * - 编译策略决策（由 CompilationStrategy 负责）
 * - 渲染实现（由 SoftwareRenderer/IRenderBackend 负责）
 */
class BatchManager
{
public:
    /// 缓存当前帧
    void cacheFrame(const RenderFrame& frame);

    /// 获取缓存帧
    const RenderFrame& cachedFrame() const;

    /// 检查是否有缓存帧
    bool hasCachedFrame() const;

    /// 使缓存失效
    void invalidateCache();

    /// 合并增量批次（移除脏实体批次，添加新批次）
    RenderFrame mergeIncremental(const RenderFrame& cachedFrame,
        const std::vector<RenderBatch>& newBatches,
        const std::set<std::string>& dirtyEntityIds);

    /// 按图元类型分组，返回起始索引列表
    std::vector<int> groupByPrimitiveType(const RenderFrame& frame) const;

    /// 视口裁剪
    RenderFrame cullByViewport(const RenderFrame& frame, const RenderRectF& viewportRect) const;

    /// 填充帧统计信息
    void fillStatistics(RenderFrame& frame, const RenderContext& context,
        const std::chrono::steady_clock::time_point& t0,
        const std::chrono::steady_clock::time_point& compileStart,
        const std::chrono::steady_clock::time_point& compileEnd);

private:
    RenderFrame m_cachedFrame;
    bool m_hasCachedFrame{ false };
};
