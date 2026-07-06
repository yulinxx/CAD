#pragma once

#include "SceneCompiler.h"

/**
 * @file DefaultSceneCompiler.h
 * @brief 默认场景编译器实现
 *
 * 提供最小可运行的场景编译骨架：
 * - 遍历所有实体生成顶点数据
 * - 按图元类型分组生成批次
 * - 支持增量更新和缓存
 * - 支持视口裁剪
 *
 * 后续可替换为更高效的实现（如 GPU 驱动编译、多线程编译）。
 */
class DefaultSceneCompiler final : public SceneCompiler
{
public:
    // ============ 全量编译 ============

    RenderFrame compile(EntityDocument2D* document, const RenderContext& context) override;
    RenderFrame compile(SceneDocument3D* document, const RenderContext& context) override;

    // ============ 增量编译 ============

    RenderFrame compileIncremental(EntityDocument2D* document,
                                   const RenderContext& context,
                                   const RenderFrame& previousFrame) override;
    RenderFrame compileIncremental(SceneDocument3D* document,
                                   const RenderContext& context,
                                   const RenderFrame& previousFrame) override;

    // ============ 缓存控制 ============

    void invalidateCache() override;
    bool hasCachedFrame() const override;
    uint64_t cachedFrameId() const override;

    // ============ 批次查询 ============

    QVector<int> groupBatchesByPrimitiveType(const RenderFrame& frame) const override;
    RenderFrame cullBatches(const RenderFrame& frame, const QRectF& viewportRect) const override;

private:
    /// 编译 2D 实体为批次列表
    QVector<RenderBatch> compileEntities2D(EntityDocument2D* document, const RenderContext& context);

    /// 编译 3D 实体为批次列表
    QVector<RenderBatch> compileEntities3D(SceneDocument3D* document, const RenderContext& context);

    /// 将圆近似为线段顶点
    static QVector<RenderVertex> tessellateCircle(float cx, float cy, float radius,
                                                   int segments = 64);

    /// 将圆弧近似为线段顶点
    static QVector<RenderVertex> tessellateArc(float cx, float cy, float radius,
                                                float startAngleDeg, float spanDeg,
                                                int segments = 64);

    /// 缓存帧
    RenderFrame m_cachedFrame;
    bool m_hasCachedFrame{ false };
};