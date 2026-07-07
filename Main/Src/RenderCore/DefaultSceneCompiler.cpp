#include "DefaultSceneCompiler.h"

#include <chrono>

// ============================================================================
// 编译入口（全量/增量自动路由）
// ============================================================================

RenderFrame DefaultSceneCompiler::compile(EntityDocument2D* document, const RenderContext& context)
{
    // 检测文档切换，自动失效缓存
    if (m_lastDocument2D != static_cast<void*>(document))
    {
        invalidateCache();
        m_lastDocument2D = static_cast<void*>(document);
    }
    return compileInternal(document, context);
}

RenderFrame DefaultSceneCompiler::compile(Eg::SceneManager* scene, const RenderContext& context)
{
    invalidateCache();
    return compileInternal(scene, context);
}

RenderFrame DefaultSceneCompiler::compile(SceneDocument3D* document, const RenderContext& context)
{
    // 检测文档切换，自动失效缓存
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

RenderFrame DefaultSceneCompiler::compileInternal(EntityDocument2D* document, const RenderContext& context)
{
    // 若能走增量路径
    if (m_strategy.cacheValid() && !m_strategy.forceFullCompile() && !context.isDirty)
    {
        if (!m_strategy.hasDirtyEntities())
        {
            // 缓存命中：零开销返回
            RenderFrame result = m_batchManager.cachedFrame();
            result.frameId = context.frameId;
            result.timestamp = std::chrono::steady_clock::now();
            result.statistics.frameTimeMs = 0.0f;
            result.statistics.compileTimeMs = 0.0f;
            result.description = QStringLiteral("Compiled 2D scene (cached, %1 batches)")
                .arg(result.batchCount());
            return result;
        }

        // 增量编译：仅重编译脏实体
        const auto t0 = std::chrono::steady_clock::now();
        const auto compileStart = std::chrono::steady_clock::now();

        QList<RenderBatch> newBatches = m_traverser.traverse2D(document, context);
        RenderFrame result = m_batchManager.mergeIncremental(
            m_batchManager.cachedFrame(), newBatches, m_strategy.dirtyEntityIds());
        m_strategy.clearDirty();

        const auto compileEnd = std::chrono::steady_clock::now();

        result.frameId = context.frameId;
        result.timestamp = t0;
        result.valid = true;
        result.description = QStringLiteral("Compiled 2D scene (incremental, %1 batches)")
            .arg(result.batchCount());

        m_batchManager.fillStatistics(result, context, t0, compileStart, compileEnd);
        m_batchManager.cacheFrame(result);
        return result;
    }

    // 全量编译
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
    frame.batches = m_traverser.traverse2D(document, context);
    const auto compileEnd = std::chrono::steady_clock::now();

    frame.valid = true;
    frame.description = QStringLiteral("Compiled 2D scene (full) on %1")
        .arg(context.backendName);

    m_batchManager.fillStatistics(frame, context, t0, compileStart, compileEnd);

    m_batchManager.cacheFrame(frame);
    m_strategy.setCacheValid(true);
    m_strategy.setForceFullCompile(false);

    return frame;
}

// ============================================================================
// 编译核心逻辑（2D - Eg::SceneManager 路径）：全量/增量自动路由
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
            result.description = QStringLiteral("Compiled 2D scene (cached, %1 batches)")
                .arg(result.batchCount());
            return result;
        }

        const auto t0 = std::chrono::steady_clock::now();
        const auto compileStart = std::chrono::steady_clock::now();

        QList<RenderBatch> newBatches = m_traverser.traverse2D(scene, context);
        RenderFrame result = m_batchManager.mergeIncremental(
            m_batchManager.cachedFrame(), newBatches, m_strategy.dirtyEntityIds());
        m_strategy.clearDirty();

        const auto compileEnd = std::chrono::steady_clock::now();

        result.frameId = context.frameId;
        result.timestamp = t0;
        result.valid = true;
        result.description = QStringLiteral("Compiled 2D scene (incremental, %1 batches)")
            .arg(result.batchCount());

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
        frame.description = QStringLiteral("No 2D scene");
        return frame;
    }

    const auto compileStart = std::chrono::steady_clock::now();
    frame.batches = m_traverser.traverse2D(scene, context);
    const auto compileEnd = std::chrono::steady_clock::now();

    frame.valid = true;
    frame.description = QStringLiteral("Compiled 2D scene (full) on %1")
        .arg(context.backendName);

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
    // 若能走增量路径
    if (m_strategy.cacheValid() && !m_strategy.forceFullCompile() && !context.isDirty)
    {
        if (!m_strategy.hasDirtyEntities())
        {
            // 缓存命中：零开销返回
            RenderFrame result = m_batchManager.cachedFrame();
            result.frameId = context.frameId;
            result.timestamp = std::chrono::steady_clock::now();
            result.statistics.frameTimeMs = 0.0f;
            result.statistics.compileTimeMs = 0.0f;
            result.description = QStringLiteral("Compiled 3D scene (cached, %1 batches)")
                .arg(result.batchCount());
            return result;
        }

        // 增量编译：仅重编译脏实体
        const auto t0 = std::chrono::steady_clock::now();
        const auto compileStart = std::chrono::steady_clock::now();

        QList<RenderBatch> newBatches = m_traverser.traverse3D(document, context);
        RenderFrame result = m_batchManager.mergeIncremental(
            m_batchManager.cachedFrame(), newBatches, m_strategy.dirtyEntityIds());
        m_strategy.clearDirty();

        const auto compileEnd = std::chrono::steady_clock::now();

        result.frameId = context.frameId;
        result.timestamp = t0;
        result.valid = true;
        result.description = QStringLiteral("Compiled 3D scene (incremental, %1 batches)")
            .arg(result.batchCount());

        m_batchManager.fillStatistics(result, context, t0, compileStart, compileEnd);
        m_batchManager.cacheFrame(result);
        return result;
    }

    // 全量编译
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
    frame.batches = m_traverser.traverse3D(document, context);
    const auto compileEnd = std::chrono::steady_clock::now();

    frame.valid = true;
    frame.description = QStringLiteral("Compiled 3D scene (full) on %1")
        .arg(context.backendName);

    m_batchManager.fillStatistics(frame, context, t0, compileStart, compileEnd);

    m_batchManager.cacheFrame(frame);
    m_strategy.setCacheValid(true);
    m_strategy.setForceFullCompile(false);

    return frame;
}

// ============================================================================
// 增量编译（2D - EntityDocument2D）
// ============================================================================

RenderFrame DefaultSceneCompiler::compileIncremental(EntityDocument2D* document,
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
        result.description = QStringLiteral("Incremental 2D (cached, %1 batches)")
            .arg(result.batchCount());
        return result;
    }

    const auto compileStart = std::chrono::steady_clock::now();

    QList<RenderBatch> newBatches = m_traverser.traverse2D(document, context);
    RenderFrame result = m_batchManager.mergeIncremental(
        m_batchManager.cachedFrame(), newBatches, m_strategy.dirtyEntityIds());
    m_strategy.clearDirty();

    const auto compileEnd = std::chrono::steady_clock::now();

    result.frameId = context.frameId;
    result.timestamp = t0;
    result.valid = true;
    result.description = QStringLiteral("Incremental 2D (partial, %1 batches)")
        .arg(result.batchCount());

    m_batchManager.fillStatistics(result, context, t0, compileStart, compileEnd);

    m_batchManager.cacheFrame(result);

    return result;
}

// ============================================================================
// 增量编译（2D - Eg::SceneManager）
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
        result.description = QStringLiteral("Incremental 2D (cached, %1 batches)")
            .arg(result.batchCount());
        return result;
    }

    const auto compileStart = std::chrono::steady_clock::now();

    QList<RenderBatch> newBatches = m_traverser.traverse2D(scene, context);
    RenderFrame result = m_batchManager.mergeIncremental(
        m_batchManager.cachedFrame(), newBatches, m_strategy.dirtyEntityIds());
    m_strategy.clearDirty();

    const auto compileEnd = std::chrono::steady_clock::now();

    result.frameId = context.frameId;
    result.timestamp = t0;
    result.valid = true;
    result.description = QStringLiteral("Incremental 2D (partial, %1 batches)")
        .arg(result.batchCount());

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
        result.description = QStringLiteral("Incremental 3D (cached, %1 batches)")
            .arg(result.batchCount());
        return result;
    }

    const auto compileStart = std::chrono::steady_clock::now();

    QList<RenderBatch> newBatches = m_traverser.traverse3D(document, context);
    RenderFrame result = m_batchManager.mergeIncremental(
        m_batchManager.cachedFrame(), newBatches, m_strategy.dirtyEntityIds());
    m_strategy.clearDirty();

    const auto compileEnd = std::chrono::steady_clock::now();

    result.frameId = context.frameId;
    result.timestamp = t0;
    result.valid = true;
    result.description = QStringLiteral("Incremental 3D (partial, %1 batches)")
        .arg(result.batchCount());

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

void DefaultSceneCompiler::markEntityDirty(const QString& entityId)
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

QVector<int> DefaultSceneCompiler::groupBatchesByPrimitiveType(const RenderFrame& frame) const
{
    return m_batchManager.groupByPrimitiveType(frame);
}

RenderFrame DefaultSceneCompiler::cullBatches(const RenderFrame& frame, const QRectF& viewportRect) const
{
    return m_batchManager.cullByViewport(frame, viewportRect);
}