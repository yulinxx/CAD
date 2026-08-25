#pragma once

#include <QPointF>

// 注意大写 Render/：这是应用层词汇表（UI/Common/Include/Render/RenderTypes.h），
// 不是已删除的渲染 DLL 头。此前拼作 render/ 只在 macOS 大小写不敏感的
// 文件系统上侥幸通过，Linux 上直接找不到文件。
#include "Render/RenderTypes.h"

/// 2D 正交相机 — 封装缩放、平移、视图矩阵计算
///
/// P5 视口瘦身：将 zoomToFit/zoomToSelection/zoomIn/zoomOut 中的
/// 相机编排逻辑从 RenderViewport2D 下沉到此层，视口只负责获取视口尺寸和提交矩阵。
struct Camera2D
{
    float zoomX = 1.0f;
    float zoomY = 1.0f;
    QPointF panOffset;

    // 缩放范围：0.01 允许缩小到默认视图的 1/100（足够总览整个场景），
    // 500 允许放大到默认视图的 500 倍。超出此范围会退化渲染矩阵，易触发崩溃。
    static constexpr float MIN_ZOOM = 0.01f;
    static constexpr float MAX_ZOOM = 500.0f;

    // ---- 基础变换 ----

    void computeViewMatrix(float outMat[9], float vpW, float vpH) const;

    /// 直接返回 Render::Mat3f，供 RenderWidget::setViewMatrix 使用
    Render::Mat3f viewMatrix(float vpW, float vpH) const;

    QPointF screenToWorld(const QPoint& screenPos, float vpW, float vpH) const;

    void zoomIn(float factor, const QPointF& anchorWorld, float vpW, float vpH);
    void zoomOut(float factor, const QPointF& anchorWorld, float vpW, float vpH);

    void pan(float dx, float dy);
    void reset();

    // ---- 高级缩放（P5 从 RenderViewport2D 下沉） ----

    /// 缩放到指定边界框（场景全览或选中区域），自动计算缩放比和居中偏移
    void zoomToFit(float vpW, float vpH, float sceneW, float sceneH);

    /// 缩放到世界空间 BBox，内部完成 zoomToFit + 居中平移
    void zoomToBBox(float vpW, float vpH, float minX, float minY, float maxX, float maxY);

    /// 以视口中心为锚点缩放（zoomIn/zoomOut 的便捷封装）
    void zoomAtCenter(float factor, float vpW, float vpH);

    /// 设置可见范围：以 (centerX, centerY) 为中心，半宽半高为 halfW/halfH
    void setViewExtent(float vpW, float vpH, float centerX, float centerY, float halfW, float halfH);

    /// 重置到默认台面范围（1200x800，中心 600,400）
    void resetToDefault(float vpW, float vpH);
};
