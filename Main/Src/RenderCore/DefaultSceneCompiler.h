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
    RenderFrame compile(Eg::SceneManager* scene, const RenderContext& context) override;
    RenderFrame compile(SceneDocument3D* document, const RenderContext& context) override;

    RenderFrame compileIncremental(Eg::SceneManager* scene,
        const RenderContext& context,
        const RenderFrame& previousFrame) override;
    RenderFrame compileIncremental(SceneDocument3D* document,
        const RenderContext& context,
        const RenderFrame& previousFrame) override;

    void invalidateCache() override;
    bool hasCachedFrame() const override;
    uint64_t cachedFrameId() const override;

    void markEntityDirty(const std::string& entityId) override;
    void markAllDirty() override;

    std::vector<int> groupBatchesByPrimitiveType(const RenderFrame& frame) const override;
    RenderFrame cullBatches(const RenderFrame& frame, const RenderRectF& viewportRect) const override;

private:
    RenderFrame compileInternal(Eg::SceneManager* scene, const RenderContext& context);
    RenderFrame compileInternal(SceneDocument3D* document, const RenderContext& context);

    SceneTraverser m_traverser;
    CompilationStrategy m_strategy;
    BatchManager m_batchManager;

    void* m_lastDocument3D{ nullptr };
};
