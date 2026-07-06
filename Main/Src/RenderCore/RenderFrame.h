#pragma once

#include <QImage>
#include <QString>
#include <QVector>
#include <cstdint>
#include <chrono>

#include "RenderCoreApi.h"
#include "RenderTypes.h"

/**
 * @file RenderFrame.h
 * @brief 渲染帧结果
 *
 * 封装一帧渲染的完整输出：
 * - 渲染批次列表（用于后续渲染）
 * - 颜色缓冲（软件后端/调试快照）
 * - 统计信息
 * - 覆盖层数据
 *
 * 不同后端渲染完成后，统一通过此结构返回结果。
 * 上层（Viewport / ShellHost）通过此结构获取渲染反馈。
 */
struct RENDER_CORE_API RenderFrame
{
    /// 帧号
    uint64_t frameId{ 0 };

    /// 帧时间戳
    std::chrono::steady_clock::time_point timestamp;

    /// 渲染批次列表（场景编译后的输出）
    QVector<RenderBatch> batches;

    /// 渲染完成后的颜色缓冲（软件后端直接绘制，GPU 后端作为调试快照）
    QImage colorBuffer;

    /// 帧描述信息
    QString description;

    /// 渲染统计
    RenderStatistics statistics;

    /// 覆盖层信息
    RenderOverlay overlay;

    /// 是否有效
    bool valid{ false };

    // ============ 便捷方法 ============

    /// 批次总数
    int batchCount() const { return batches.size(); }

    /// 顶点总数
    int totalVertexCount() const
    {
        int count = 0;
        for (const auto& batch : batches)
            count += batch.vertexCount();
        return count;
    }

    /// 实体总数（去重）
    int entityCount() const
    {
        QStringList ids;
        for (const auto& batch : batches)
        {
            if (!batch.entityId.isEmpty() && !ids.contains(batch.entityId))
                ids.append(batch.entityId);
        }
        return ids.size();
    }

    /// 生成完整描述
    QString fullDescription() const
    {
        return QStringLiteral("[Frame %1] %2 | %3 batches | %4 verts | %5 ents | %6")
            .arg(frameId)
            .arg(description)
            .arg(batchCount())
            .arg(totalVertexCount())
            .arg(entityCount())
            .arg(valid ? QStringLiteral("valid") : QStringLiteral("INVALID"));
    }
};