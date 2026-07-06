#pragma once

#include <QString>
#include <QSize>
#include <QVector>
#include <cstdint>

#include "RenderCoreApi.h"
#include "RenderTypes.h"

/**
 * @file RenderContext.h
 * @brief 统一渲染上下文
 *
 * 封装 UI 侧传递给渲染后端的通用上下文信息。
 * 避免后端直接依赖业务对象，所有渲染条件通过此结构传递。
 *
 * 使用方式：
 * - Viewport 在 resize / mode 切换时更新 RenderContext
 * - 通过 IRenderBackend::bindContext() 传递给后端
 * - 后端根据 context 变化决定是否需要重新编译场景
 */
struct RENDER_CORE_API RenderContext
{
    /// 后端类型名称
    QString backendName;

    /// 场景类型：2D 或 3D
    QString sceneType;

    /// 当前视口大小（像素）
    QSize viewportSize;

    /// 渲染模式
    RenderMode renderMode{ RenderMode::Wireframe };

    /// 是否启用轨道模式
    bool orbitMode{ true };

    /// 是否启用测量模式
    bool measureMode{ false };

    /// 帧号（每帧递增，用于脏检测和缓存失效）
    uint64_t frameId{ 0 };

    /// 上下文是否已变更（需要后端重新编译场景）
    bool isDirty{ true };

    /// 脏区域类型（用于增量更新）
    DirtyRegionType dirtyType{ DirtyRegionType::All };

    /// 脏区域列表（用于局部重绘）
    QVector<DirtyRegion> dirtyRegions;

    // ============ 便捷方法 ============

    /// 标记为脏，需要全量重绘
    void markDirty(DirtyRegionType type = DirtyRegionType::All)
    {
        isDirty = true;
        dirtyType = dirtyType | type;
    }

    /// 清除脏标记（渲染完成后调用）
    void clearDirty()
    {
        isDirty = false;
        dirtyType = DirtyRegionType::None;
        dirtyRegions.clear();
    }

    /// 增加到下一帧
    void advanceFrame()
    {
        ++frameId;
    }

    /// 是否为 2D 场景
    bool is2D() const
    {
        return sceneType == QStringLiteral("2D");
    }

    /// 是否为 3D 场景
    bool is3D() const
    {
        return sceneType == QStringLiteral("3D");
    }
};