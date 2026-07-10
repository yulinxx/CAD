#include "DefaultSceneCompiler.h"

#include <chrono>
#include <string>

// ============================================================================
// 编译入口（全量/增量自动路由）
// ============================================================================

RenderFrame DefaultSceneCompiler::compile(Eg::SceneManager* scene, const RenderContext& context)
{
    invalidateCache();
    return compileInternal(scene, context);
}

RenderFrame DefaultSceneCompiler::compile(SceneDocument3D* document, const RenderContext& context)
{
    if (m_lastDocument3D != static_cast<void*>(document))
    {
        invalidateCache();
        m_lastDocument3D = static_cast<void*>(document);
    }
    return compileInternal(document, context);
}

// ============================================================================
// 编译核心逻辑（2D）：全量/增量自动路由
// ============================================================================

RenderFrame DefaultSceneCompiler::compileInternal(Eg::SceneManager* scene, const RenderContext& context)
{
    if (m_strategy.cacheValid() && !m_strategy.forceFullCompile() && !context.isDirty)
    {
        if (!m_strategy.hasDirtyEntities())
        {
            RenderFrame result = m_batchManager.cachedFrame();
            result.frameId = context.frameId;
            result.timestamp = std::chrono::steady_clock::now();
            result.statistics.frameTimeMs = 0.0f;
            result.statistics.compileTimeMs = 0.0f;
            result.description = "Compiled 2D scene (cached, " + std::to_string(result.batchCount()) + " batches)";
            return result;
        }

        const auto t0 = std::chrono::steady_clock::now();
        const auto compileStart = std::chrono::steady_clock::now();

        std::vector<RenderBatch> newBatches = m_traverser.traverse2D(scene, context);
        RenderFrame result = m_batchManager.mergeIncremental(
            m_batchManager.cachedFrame(), newBatches, m_strategy.dirtyEntityIds());
        m_strategy.clearDirty();

        const auto compileEnd = std::chrono::steady_clock::now();

        result.frameId = context.frameId;
        result.timestamp = t0;
        result.valid = true;
        result.description = "Compiled 2D scene (incremental, " + std::to_string(result.batchCount()) + " batches)";

        m_batchManager.fillStatistics(result, context, t0, compileStart, compileEnd);
        m_batchManager.cacheFrame(result);
        return result;
    }

    const auto t0 = std::chrono::steady_clock::now();

    RenderFrame frame;
    frame.frameId = context.frameId;
    frame.timestamp = t0;

    if (!scene)
    {
        frame.valid = false;
        frame.description = "No 2D scene";
        return frame;
    }

    const auto compileStart = std::chrono::steady_clock::now();
    frame.batches = m_traverser.traverse2D(scene, context);
    const auto compileEnd = std::chrono::steady_clock::now();

    frame.valid = true;
    frame.description = "Compiled 2D scene (full) on " + context.backendName;

    m_batchManager.fillStatistics(frame, context, t0, compileStart, compileEnd);

    m_batchManager.cacheFrame(frame);
    m_strategy.setCacheValid(true);
    m_strategy.setForceFullCompile(false);

    return frame;
}

// ============================================================================
// 编译核心逻辑（3D）：全量/增量自动路由
// ============================================================================

RenderFrame DefaultSceneCompiler::compileInternal(SceneDocument3D* document, const RenderContext& context)
{
    if (m_strategy.cacheValid() && !m_strategy.forceFullCompile() && !context.isDirty)
    {
        if (!m_strategy.hasDirtyEntities())
        {
            RenderFrame result = m_batchManager.cachedFrame();
            result.frameId = context.frameId;
            result.timestamp = std::chrono::steady_clock::now();
            result.statistics.frameTimeMs = 0.0f;
            result.statistics.compileTimeMs = 0.0f;
            result.description = "Compiled 3D scene (cached, " + std::to_string(result.batchCount()) + " batches)";
            return result;
        }

        const auto t0 = std::chrono::steady_clock::now();
        const auto compileStart = std::chrono::steady_clock::now();

        std::vector<RenderBatch> newBatches = m_traverser.traverse3D(document, context);
        RenderFrame result = m_batchManager.mergeIncremental(
            m_batchManager.cachedFrame(), newBatches, m_strategy.dirtyEntityIds());
        m_strategy.clearDirty();

        const auto compileEnd = std::chrono::steady_clock::now();

        result.frameId = context.frameId;
        result.timestamp = t0;
        result.valid = true;
        result.description = "Compiled 3D scene (incremental, " + std::to_string(result.batchCount()) + " batches)";

        m_batchManager.fillStatistics(result, context, t0, compileStart, compileEnd);
        m_batchManager.cacheFrame(result);
        return result;
    }

    const auto t0 = std::chrono::steady_clock::now();

    RenderFrame frame;
    frame.frameId = context.frameId;
    frame.timestamp = t0;

    if (!document)
    {
        frame.valid = false;
        frame.description = "No 3D document";
        return frame;
    }

    const auto compileStart = std::chrono::steady_clock::now();
    frame.batches = m_traverser.traverse3D(document, context);
    const auto compileEnd = std::chrono::steady_clock::now();

    frame.valid = true;
    frame.description = "Compiled 3D scene (full) on " + context.backendName;

    m_batchManager.fillStatistics(frame, context, t0, compileStart, compileEnd);

    m_batchManager.cacheFrame(frame);
    m_strategy.setCacheValid(true);
    m_strategy.setForceFullCompile(false);

    return frame;
}

// ============================================================================
// 增量编译（2D）
// ============================================================================

RenderFrame DefaultSceneCompiler::compileIncremental(Eg::SceneManager* scene,
                                                       const RenderContext& context,
                                                       const RenderFrame& previousFrame)
{
    const auto t0 = std::chrono::steady_clock::now();

    if (!m_strategy.cacheValid() || m_strategy.forceFullCompile())
        return compile(scene, context);

    if (context.isDirty)
        return compile(scene, context);

    if (!m_strategy.hasDirtyEntities())
    {
        RenderFrame result = m_batchManager.cachedFrame();
        result.frameId = context.frameId;
        result.timestamp = t0;
        result.statistics.frameTimeMs = 0.0f;
        result.statistics.compileTimeMs = 0.0f;
        result.description = "Incremental 2D (cached, " + std::to_string(result.batchCount()) + " batches)";
        return result;
    }

    const auto compileStart = std::chrono::steady_clock::now();

    std::vector<RenderBatch> newBatches = m_traverser.traverse2D(scene, context);
    RenderFrame result = m_batchManager.mergeIncremental(
        m_batchManager.cachedFrame(), newBatches, m_strategy.dirtyEntityIds());
    m_strategy.clearDirty();

    const auto compileEnd = std::chrono::steady_clock::now();

    result.frameId = context.frameId;
    result.timestamp = t0;
    result.valid = true;
    result.description = "Incremental 2D (partial, " + std::to_string(result.batchCount()) + " batches)";

    m_batchManager.fillStatistics(result, context, t0, compileStart, compileEnd);

    m_batchManager.cacheFrame(result);

    return result;
}

// ============================================================================
// 增量编译（3D）
// ============================================================================

RenderFrame DefaultSceneCompiler::compileIncremental(SceneDocument3D* document,
                                                      const RenderContext& context,
                                                      const RenderFrame& previousFrame)
{
    const auto t0 = std::chrono::steady_clock::now();

    if (!m_strategy.cacheValid() || m_strategy.forceFullCompile())
        return compile(document, context);

    if (context.isDirty)
        return compile(document, context);

    if (!m_strategy.hasDirtyEntities())
    {
        RenderFrame result = m_batchManager.cachedFrame();
        result.frameId = context.frameId;
        result.timestamp = t0;
        result.statistics.frameTimeMs = 0.0f;
        result.statistics.compileTimeMs = 0.0f;
        result.description = "Incremental 3D (cached, " + std::to_string(result.batchCount()) + " batches)";
        return result;
    }

    const auto compileStart = std::chrono::steady_clock::now();

    std::vector<RenderBatch> newBatches = m_traverser.traverse3D(document, context);
    RenderFrame result = m_batchManager.mergeIncremental(
        m_batchManager.cachedFrame(), newBatches, m_strategy.dirtyEntityIds());
    m_strategy.clearDirty();

    const auto compileEnd = std::chrono::steady_clock::now();

    result.frameId = context.frameId;
    result.timestamp = t0;
    result.valid = true;
    result.description = "Incremental 3D (partial, " + std::to_string(result.batchCount()) + " batches)";

    m_batchManager.fillStatistics(result, context, t0, compileStart, compileEnd);

    m_batchManager.cacheFrame(result);

    return result;
}

// ============================================================================
// 缓存控制
// ============================================================================

void DefaultSceneCompiler::invalidateCache()
{
    m_batchManager.invalidateCache();
    m_strategy.setCacheValid(false);
    m_strategy.setForceFullCompile(true);
}

bool DefaultSceneCompiler::hasCachedFrame() const
{
    return m_batchManager.hasCachedFrame();
}

uint64_t DefaultSceneCompiler::cachedFrameId() const
{
    return m_batchManager.hasCachedFrame() ? m_batchManager.cachedFrame().frameId : 0;
}

// ============================================================================
// 脏实体追踪
// ============================================================================

void DefaultSceneCompiler::markEntityDirty(const std::string& entityId)
{
    m_strategy.markEntityDirty(entityId);
}

void DefaultSceneCompiler::markAllDirty()
{
    m_strategy.markAllDirty();
}

// ============================================================================
// 批次查询（委托 BatchManager）
// ============================================================================

std::vector<int> DefaultSceneCompiler::groupBatchesByPrimitiveType(const RenderFrame& frame) const
{
    return m_batchManager.groupByPrimitiveType(frame);
}

RenderFrame DefaultSceneCompiler::cullBatches(const RenderFrame& frame, const RenderRectF& viewportRect) const
{
    return m_batchManager.cullByViewport(frame, viewportRect);
}
