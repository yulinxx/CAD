#include "DefaultSceneCompiler.h"

#include <chrono>
#include <string>
#include <functional>

#include "Log/SyLogger.h"

// ============================================================================
// 通用编译模板（2D/3D 共享）
// ============================================================================

namespace
{
    using TraverseFunc = std::function<std::vector<RenderBatch>(const RenderContext&)>;

    RenderFrame compileGeneric(TraverseFunc traverse, const RenderContext& context,
                               const std::string& sceneType, CompilationStrategy& strategy,
                               BatchManager& batchManager, void* lastDocumentPtr, void* currentDocumentPtr)
    {
        if (lastDocumentPtr != currentDocumentPtr)
        {
            SY_DEBUG("[SceneCompiler] compile document changed, invalidating cache");
            strategy.setCacheValid(false);
            strategy.markAllDirty();
            lastDocumentPtr = currentDocumentPtr;
        }

        if (strategy.cacheValid() && !strategy.forceFullCompile() && !context.isDirty)
        {
            if (!strategy.hasDirtyEntities())
            {
                SY_DEBUG("[SceneCompiler] compile sceneType=%s cached, no dirty entities", sceneType.c_str());
                RenderFrame result = batchManager.cachedFrame();
                result.frameId = context.frameId;
                result.timestamp = std::chrono::steady_clock::now();
                result.statistics.frameTimeMs = 0.0f;
                result.statistics.compileTimeMs = 0.0f;
                result.description = "Compiled " + sceneType + " scene (cached, " +
                                    std::to_string(result.batchCount()) + " batches)";
                return result;
            }

            SY_DEBUG("[SceneCompiler] compile sceneType=%s incremental compilation started", sceneType.c_str());
            const auto t0 = std::chrono::steady_clock::now();
            const auto compileStart = std::chrono::steady_clock::now();

            std::vector<RenderBatch> newBatches = traverse(context);
            RenderFrame result = batchManager.mergeIncremental(
                batchManager.cachedFrame(), newBatches, strategy.dirtyEntityIds());
            strategy.clearDirty();

            const auto compileEnd = std::chrono::steady_clock::now();
            const float compileTime = std::chrono::duration<float, std::milli>(compileEnd - compileStart).count();

            result.frameId = context.frameId;
            result.timestamp = t0;
            result.valid = true;
            result.description = "Compiled " + sceneType + " scene (incremental, " +
                                std::to_string(result.batchCount()) + " batches)";

            batchManager.fillStatistics(result, context, t0, compileStart, compileEnd);
            batchManager.cacheFrame(result);
            
            SY_INFO("[SceneCompiler] compile sceneType=%s incremental completed batches=%d time=%.2fms",
                sceneType.c_str(), result.batchCount(), compileTime);
            return result;
        }

        SY_DEBUG("[SceneCompiler] compile sceneType=%s full compilation started", sceneType.c_str());
        const auto t0 = std::chrono::steady_clock::now();

        RenderFrame frame;
        frame.frameId = context.frameId;
        frame.timestamp = t0;

        std::vector<RenderBatch> batches = traverse(context);

        const auto compileStart = std::chrono::steady_clock::now();
        frame.batches = batches;
        frame.valid = true;
        const auto compileEnd = std::chrono::steady_clock::now();
        const float compileTime = std::chrono::duration<float, std::milli>(compileEnd - compileStart).count();

        frame.description = "Compiled " + sceneType + " scene (full) on " + context.backendName;

        batchManager.fillStatistics(frame, context, t0, compileStart, compileEnd);
        batchManager.cacheFrame(frame);
        strategy.setCacheValid(true);
        strategy.setForceFullCompile(false);

        SY_INFO("[SceneCompiler] compile sceneType=%s full completed entities=%d batches=%d time=%.2fms",
            sceneType.c_str(), static_cast<int>(batches.size()), frame.batchCount(), compileTime);
        return frame;
    }

    RenderFrame compileIncrementalGeneric(TraverseFunc traverse, const RenderContext& context,
                                          const std::string& sceneType, CompilationStrategy& strategy,
                                          BatchManager& batchManager)
    {
        SY_DEBUG("[SceneCompiler] compileIncremental sceneType=%s", sceneType.c_str());
        const auto t0 = std::chrono::steady_clock::now();

        if (!strategy.cacheValid() || strategy.forceFullCompile())
        {
            SY_DEBUG("[SceneCompiler] compileIncremental sceneType=%s cache invalid, falling back to full compile", sceneType.c_str());
            return compileGeneric(traverse, context, sceneType, strategy, batchManager, nullptr, nullptr);
        }

        if (context.isDirty)
        {
            SY_DEBUG("[SceneCompiler] compileIncremental sceneType=%s context dirty, falling back to full compile", sceneType.c_str());
            return compileGeneric(traverse, context, sceneType, strategy, batchManager, nullptr, nullptr);
        }

        if (!strategy.hasDirtyEntities())
        {
            SY_DEBUG("[SceneCompiler] compileIncremental sceneType=%s cached, no dirty entities", sceneType.c_str());
            RenderFrame result = batchManager.cachedFrame();
            result.frameId = context.frameId;
            result.timestamp = t0;
            result.statistics.frameTimeMs = 0.0f;
            result.statistics.compileTimeMs = 0.0f;
            result.description = "Incremental " + sceneType + " (cached, " +
                                std::to_string(result.batchCount()) + " batches)";
            return result;
        }

        SY_DEBUG("[SceneCompiler] compileIncremental sceneType=%s partial compilation started", sceneType.c_str());
        const auto compileStart = std::chrono::steady_clock::now();

        std::vector<RenderBatch> newBatches = traverse(context);
        RenderFrame result = batchManager.mergeIncremental(
            batchManager.cachedFrame(), newBatches, strategy.dirtyEntityIds());
        strategy.clearDirty();

        const auto compileEnd = std::chrono::steady_clock::now();
        const float compileTime = std::chrono::duration<float, std::milli>(compileEnd - compileStart).count();

        result.frameId = context.frameId;
        result.timestamp = t0;
        result.valid = true;
        result.description = "Incremental " + sceneType + " (partial, " +
                            std::to_string(result.batchCount()) + " batches)";

        batchManager.fillStatistics(result, context, t0, compileStart, compileEnd);
        batchManager.cacheFrame(result);

        SY_INFO("[SceneCompiler] compileIncremental sceneType=%s partial completed batches=%d time=%.2fms",
            sceneType.c_str(), result.batchCount(), compileTime);
        return result;
    }
}

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
// 编译核心逻辑（2D）
// ============================================================================

RenderFrame DefaultSceneCompiler::compileInternal(Eg::SceneManager* scene, const RenderContext& context)
{
    auto traverse = [this, scene](const RenderContext& ctx) {
        return m_traverser.traverse2D(scene, ctx);
    };
    return compileGeneric(traverse, context, "2D", m_strategy, m_batchManager, nullptr, nullptr);
}

// ============================================================================
// 编译核心逻辑（3D）
// ============================================================================

RenderFrame DefaultSceneCompiler::compileInternal(SceneDocument3D* document, const RenderContext& context)
{
    auto traverse = [this, document](const RenderContext& ctx) {
        return m_traverser.traverse3D(document, ctx);
    };
    return compileGeneric(traverse, context, "3D", m_strategy, m_batchManager, m_lastDocument3D, document);
}

// ============================================================================
// 增量编译（2D）
// ============================================================================

RenderFrame DefaultSceneCompiler::compileIncremental(Eg::SceneManager* scene,
                                                       const RenderContext& context,
                                                       const RenderFrame& previousFrame)
{
    (void)previousFrame;
    auto traverse = [this, scene](const RenderContext& ctx) {
        return m_traverser.traverse2D(scene, ctx);
    };
    return compileIncrementalGeneric(traverse, context, "2D", m_strategy, m_batchManager);
}

// ============================================================================
// 增量编译（3D）
// ============================================================================

RenderFrame DefaultSceneCompiler::compileIncremental(SceneDocument3D* document,
                                                      const RenderContext& context,
                                                      const RenderFrame& previousFrame)
{
    (void)previousFrame;
    auto traverse = [this, document](const RenderContext& ctx) {
        return m_traverser.traverse3D(document, ctx);
    };
    return compileIncrementalGeneric(traverse, context, "3D", m_strategy, m_batchManager);
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