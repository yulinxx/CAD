#pragma once

#include "SceneCompiler.h"

#include "SceneTraverser.h"
#include "CompilationStrategy.h"
#include "BatchManager.h"

#include <memory>

/**
 * @file DefaultSceneCompiler.h
 * @brief 默认场景编译器实现
 *
 * 采用三层架构：
 * - SceneTraverser：场景遍历，生成渲染批次
 * - CompilationStrategy：编译策略决策（增量/全量）
 * - BatchManager：批次缓存、合并、分组、裁剪
 *
 * DefaultSceneCompiler 作为编排层，协调三层完成编译流程。
 */
class DefaultSceneCompiler final : public SceneCompiler
{
public:
    // ============ 全量编译 ============

    RenderFrame compile(EntityDocument2D* document, const RenderContext& context) override;
    RenderFrame compile(Eg::SceneManager* scene, const RenderContext& context) override;
    RenderFrame compile(SceneDocument3D* document, const RenderContext& context) override;

    // ============ 增量编译 ============

    RenderFrame compileIncremental(EntityDocument2D* document,
                                   const RenderContext& context,
                                   const RenderFrame& previousFrame) override;
    RenderFrame compileIncremental(Eg::SceneManager* scene,
                                   const RenderContext& context,
                                   const RenderFrame& previousFrame) override;
    RenderFrame compileIncremental(SceneDocument3D* document,
                                   const RenderContext& context,
                                   const RenderFrame& previousFrame) override;

    // ============ 缓存控制 ============

    void invalidateCache() override;
    bool hasCachedFrame() const override;
    uint64_t cachedFrameId() const override;

    // ============ 脏实体追踪 ============

    void markEntityDirty(const QString& entityId) override;
    void markAllDirty() override;

    // ============ 批次查询 ============

    QVector<int> groupBatchesByPrimitiveType(const RenderFrame& frame) const override;
    RenderFrame cullBatches(const RenderFrame& frame, const QRectF& viewportRect) const override;

private:
    /// 编译核心逻辑（全量/增量自动路由）
    RenderFrame compileInternal(EntityDocument2D* document, const RenderContext& context);
    RenderFrame compileInternal(Eg::SceneManager* scene, const RenderContext& context);
    RenderFrame compileInternal(SceneDocument3D* document, const RenderContext& context);

    SceneTraverser m_traverser;
    CompilationStrategy m_strategy;
    BatchManager m_batchManager;

    /// 上次编译的文档指针（用于检测场景切换，自动失效缓存）
    void* m_lastDocument2D{ nullptr };
    void* m_lastDocument3D{ nullptr };
};