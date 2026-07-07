#pragma once

#include <QString>
#include <QSize>
#include <cstdint>

#include "RenderCoreApi.h"
#include "RenderTypes.h"

/**
 * @file RenderContext.h
 * @brief 统一渲染上下文（最小字段集）
 *
 * 封装 UI 侧传递给渲染后端的通用上下文。
 * 只保留渲染所需的最小字段，不承载 UI 业务语义。
 *
 * 字段约束：
 * - 只放渲染管线真正需要的字段
 * - 不塞 UI 业务对象（如 Document、Selection）
 * - 不塞状态中心引用
 * - 不塞脏区细节（由 SceneCompiler 内部管理）
 *
 * 使用方式：
 * - Viewport 在 resize / mode 切换时更新 RenderContext
 * - 通过 IRenderBackend::bindContext() 传递给后端
 * - 后端根据 isDirty 决定是否需要重新编译场景
 */
struct RENDER_CORE_API RenderContext
{
    /// 后端类型名称（如 "OpenGL", "Vulkan", "Software"）
    QString backendName;

    /// 场景类型："2D" 或 "3D"
    QString sceneType;

    /// 当前视口大小（像素）
    QSize viewportSize;

    /// 渲染模式
    RenderMode renderMode{ RenderMode::Wireframe };

    /// 是否启用轨道模式（3D 专用）
    bool orbitMode{ true };

    /// 是否启用测量模式
    bool measureMode{ false };

    /// 帧号（每帧递增，用于脏检测和缓存失效）
    uint64_t frameId{ 0 };

    /// 上下文是否已变更（需要后端重新编译场景）
    bool isDirty{ true };

    // ============ 便捷方法 ============

    /// 标记为脏，需要重编译
    void markDirty()
    {
        isDirty = true;
    }

    /// 清除脏标记（渲染完成后调用）
    void clearDirty()
    {
        isDirty = false;
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