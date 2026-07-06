#pragma once

#include "RenderCoreApi.h"

#include <QString>
#include <QVector>
#include <QPointF>
#include <QRectF>
#include <cstdint>
#include <chrono>

/**
 * @file RenderTypes.h
 * @brief 统一渲染抽象层的共享数据类型
 *
 * 这些类型贯穿整个渲染管线：从场景编译到后端渲染再到帧输出。
 * 所有渲染后端（OpenGL / Vulkan / Metal / 软件）都使用同一套数据结构。
 */

// ============================================================================
// 渲染模式
// ============================================================================

/// 渲染模式枚举
enum class RenderMode : uint8_t
{
    Wireframe,   ///< 线框模式
    Shaded,      ///< 着色模式
    Solid,       ///< 实体模式
    XRay,        ///< X 光透视模式
};

/// 渲染模式名称转换
inline QString renderModeName(RenderMode mode)
{
    switch (mode)
    {
    case RenderMode::Wireframe: return QStringLiteral("Wireframe");
    case RenderMode::Shaded:    return QStringLiteral("Shaded");
    case RenderMode::Solid:     return QStringLiteral("Solid");
    case RenderMode::XRay:      return QStringLiteral("XRay");
    default:                    return QStringLiteral("Unknown");
    }
}

// ============================================================================
// 渲染图元类型
// ============================================================================

/// 图元类型
enum class PrimitiveType : uint8_t
{
    Points        = 0,
    Lines         = 1,
    LineStrip     = 2,
    LineLoop      = 3,
    Triangles     = 4,
    TriangleStrip = 5,
};

/// 顶点数据
struct RenderVertex
{
    float x{ 0.0f };
    float y{ 0.0f };
    float z{ 0.0f };
    float r{ 0.0f };
    float g{ 0.0f };
    float b{ 0.0f };
    float a{ 1.0f };
};

/// 渲染批次
struct RenderBatch
{
    /// 图元类型
    PrimitiveType primitiveType{ PrimitiveType::Lines };
    /// 顶点数据
    QVector<RenderVertex> vertices;
    /// 线宽
    float lineWidth{ 1.0f };
    /// 点大小
    float pointSize{ 1.0f };
    /// 所属实体 ID（用于选择高亮）
    QString entityId;
    /// 是否选中
    bool selected{ false };
    /// 包围盒（用于视锥裁剪）
    QRectF boundingBox;

    /// 顶点数量
    int vertexCount() const { return vertices.size(); }
    /// 是否为空批次
    bool empty() const { return vertices.isEmpty(); }
};

// ============================================================================
// 渲染统计
// ============================================================================

/// 帧渲染统计
struct RenderStatistics
{
    /// 帧号
    uint64_t frameId{ 0 };
    /// 帧时间戳
    std::chrono::steady_clock::time_point timestamp;

    /// 绘制调用次数
    int drawCallCount{ 0 };
    /// 顶点总数
    int totalVertexCount{ 0 };
    /// 实体总数
    int entityCount{ 0 };
    /// 批次数
    int batchCount{ 0 };
    /// 被裁剪的批次数
    int culledBatchCount{ 0 };

    /// 帧渲染耗时（毫秒）
    float frameTimeMs{ 0.0f };
    /// 场景编译耗时（毫秒）
    float compileTimeMs{ 0.0f };

    /// GPU 内存使用量（字节）
    size_t gpuMemoryBytes{ 0 };

    /// 生成描述字符串
    QString description() const
    {
        return QStringLiteral("Frame %1 | %2 draws | %3 verts | %4 ents | %5 ms")
            .arg(frameId)
            .arg(drawCallCount)
            .arg(totalVertexCount)
            .arg(entityCount)
            .arg(frameTimeMs, 0, 'f', 2);
    }
};

// ============================================================================
// 覆盖层信息
// ============================================================================

/// 覆盖层文本项
struct OverlayTextItem
{
    QString text;
    QPointF position;
    float r{ 1.0f };
    float g{ 1.0f };
    float b{ 1.0f };
    float a{ 1.0f };
};

/// 覆盖层信息
struct RenderOverlay
{
    /// 文本覆盖层
    QVector<OverlayTextItem> texts;
    /// 视图中心十字线是否可见
    bool crosshairVisible{ false };
    /// 网格是否可见
    bool gridVisible{ false };
    /// 状态栏文本
    QString statusText;
    /// 坐标显示文本
    QString coordinateText;
};

// ============================================================================
// 脏区域
// ============================================================================

/// 脏区域标记
struct DirtyRegion
{
    QRectF rect;
    uint64_t frameId{ 0 };
};

/// 脏区域类型
enum class DirtyRegionType : uint8_t
{
    None       = 0,
    Transform  = 1 << 0,   ///< 变换变化
    Geometry   = 1 << 1,   ///< 几何变化
    Selection  = 1 << 2,   ///< 选择变化
    View       = 1 << 3,   ///< 视图变化
    All        = Transform | Geometry | Selection | View,
};

inline DirtyRegionType operator|(DirtyRegionType a, DirtyRegionType b)
{
    return static_cast<DirtyRegionType>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}

inline bool hasDirtyFlag(DirtyRegionType flags, DirtyRegionType flag)
{
    return (static_cast<uint8_t>(flags) & static_cast<uint8_t>(flag)) != 0;
}

// ============================================================================
// 后端能力
// ============================================================================

/// 后端能力标志
enum class BackendCapability : uint32_t
{
    None                = 0,
    HardwareAccelerated = 1 << 0,   ///< 硬件加速
    MultiViewport       = 1 << 1,   ///< 多视口
    InstancedRendering  = 1 << 2,   ///< 实例化渲染
    ComputeShader       = 1 << 3,   ///< 计算着色器
    RayTracing          = 1 << 4,   ///< 光线追踪
    AntiAliasing        = 1 << 5,   ///< 抗锯齿
    HighDPI             = 1 << 6,   ///< 高 DPI
    OffscreenRendering  = 1 << 7,   ///< 离屏渲染
};

inline BackendCapability operator|(BackendCapability a, BackendCapability b)
{
    return static_cast<BackendCapability>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

inline bool hasCapability(BackendCapability caps, BackendCapability cap)
{
    return (static_cast<uint32_t>(caps) & static_cast<uint32_t>(cap)) != 0;
}