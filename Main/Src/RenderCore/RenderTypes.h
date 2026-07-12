#pragma once

#include "RenderCoreApi.h"

#include <cstdint>
#include <chrono>
#include <string>
#include <vector>

struct RenderPointF
{
    double x{ 0.0 };
    double y{ 0.0 };
};

struct RenderRectF
{
    double x{ 0.0 };
    double y{ 0.0 };
    double width{ 0.0 };
    double height{ 0.0 };

    bool isValid() const
    {
        return width > 0 && height > 0;
    }

    bool isNull() const
    {
        return width == 0 && height == 0;
    }

    bool intersects(const RenderRectF& other) const
    {
        if (!isValid() || !other.isValid())
            return false;
        return (x < other.x + other.width) &&
            (x + width > other.x) &&
            (y < other.y + other.height) &&
            (y + height > other.y);
    }
};

struct ImageBuffer
{
    int width{ 0 };
    int height{ 0 };
    int channels{ 4 };
    std::vector<uint8_t> data;
};

struct Size2D
{
    int width{ 0 };
    int height{ 0 };

    bool isValid() const
    {
        return width > 0 && height > 0;
    }
};

enum class RenderMode : uint8_t
{
    Wireframe,
    Shaded,
    Solid,
    XRay,
};

inline const char* renderModeName(RenderMode mode)
{
    switch (mode)
    {
        case RenderMode::Wireframe: return "Wireframe";
        case RenderMode::Shaded:    return "Shaded";
        case RenderMode::Solid:     return "Solid";
        case RenderMode::XRay:      return "XRay";
        default:                    return "Unknown";
    }
}

enum class PrimitiveType : uint8_t
{
    Points = 0,
    Lines = 1,
    LineStrip = 2,
    LineLoop = 3,
    Triangles = 4,
    TriangleStrip = 5,
};

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

struct RenderBatch
{
    PrimitiveType primitiveType{ PrimitiveType::Lines };
    std::vector<RenderVertex> vertices;
    float lineWidth{ 1.0f };
    float pointSize{ 1.0f };
    std::string entityId;
    bool selected{ false };
    RenderRectF boundingBox;

    int vertexCount() const
    {
        return static_cast<int>(vertices.size());
    }
    bool empty() const
    {
        return vertices.empty();
    }
};

struct RenderStatistics
{
    uint64_t frameId{ 0 };
    std::chrono::steady_clock::time_point timestamp;

    int drawCallCount{ 0 };
    int totalVertexCount{ 0 };
    int entityCount{ 0 };
    int batchCount{ 0 };
    int culledBatchCount{ 0 };

    float frameTimeMs{ 0.0f };
    float compileTimeMs{ 0.0f };

    size_t gpuMemoryBytes{ 0 };

    std::string description() const
    {
        return "Frame " + std::to_string(frameId)
            + " | " + std::to_string(drawCallCount) + " draws"
            + " | " + std::to_string(totalVertexCount) + " verts"
            + " | " + std::to_string(entityCount) + " ents"
            + " | " + std::to_string(frameTimeMs) + " ms";
    }
};

struct OverlayTextItem
{
    std::string text;
    RenderPointF position;
    float r{ 1.0f };
    float g{ 1.0f };
    float b{ 1.0f };
    float a{ 1.0f };
};

struct RenderOverlay
{
    std::vector<OverlayTextItem> texts;
    bool crosshairVisible{ false };
    bool gridVisible{ false };
    std::string statusText;
    std::string coordinateText;
};

struct DirtyRegion
{
    RenderRectF rect;
    uint64_t frameId{ 0 };
};

enum class DirtyRegionType : uint8_t
{
    None = 0,
    Transform = 1 << 0,
    Geometry = 1 << 1,
    Selection = 1 << 2,
    View = 1 << 3,
    All = Transform | Geometry | Selection | View,
};

inline DirtyRegionType operator|(DirtyRegionType a, DirtyRegionType b)
{
    return static_cast<DirtyRegionType>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}

inline bool hasDirtyFlag(DirtyRegionType flags, DirtyRegionType flag)
{
    return (static_cast<uint8_t>(flags) & static_cast<uint8_t>(flag)) != 0;
}

enum class BackendCapability : uint32_t
{
    None = 0,
    HardwareAccelerated = 1 << 0,
    MultiViewport = 1 << 1,
    InstancedRendering = 1 << 2,
    ComputeShader = 1 << 3,
    RayTracing = 1 << 4,
    AntiAliasing = 1 << 5,
    HighDPI = 1 << 6,
    OffscreenRendering = 1 << 7,
};

inline BackendCapability operator|(BackendCapability a, BackendCapability b)
{
    return static_cast<BackendCapability>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

inline bool hasCapability(BackendCapability caps, BackendCapability cap)
{
    return (static_cast<uint32_t>(caps) & static_cast<uint32_t>(cap)) != 0;
}
