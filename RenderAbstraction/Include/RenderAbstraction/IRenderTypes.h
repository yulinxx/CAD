#pragma once
/**
 * @file IRenderTypes.h
 * @brief 业务级渲染类型定义（零RenderX依赖）
 *
 * 这些类型是UI层与渲染层之间的稳定契约。
 * 任何渲染后端（RenderX/Vulkan/Metal/Software）都必须且只能通过这些类型通信。
 *
 * ABI安全规则：
 * - 所有类型均为POD，static_assert锁定大小
 * - 不使用STL容器
 * - 句柄用uint64_t包装，不用enum class（避免跨DLL enum layout问题）
 * - 字符串用const char*
 */

#include <cstdint>
#include <cstddef>

namespace RenderAbstraction {

// ==================== 句柄 ====================
// 用 uint64_t 值包装，与 RenderX 的 enum class : uint64_t 等大
// 但类型安全，防止误传

struct DeviceHandle { uint64_t value = 0; };
struct SurfaceHandle { uint64_t value = 0; };
struct CommandListHandle { uint64_t value = 0; };
struct BufferHandle { uint64_t value = 0; };
struct TextureHandle { uint64_t value = 0; };

inline bool isValid(DeviceHandle h) { return h.value != 0; }
inline bool isValid(SurfaceHandle h) { return h.value != 0; }
inline bool isValid(CommandListHandle h) { return h.value != 0; }
inline bool isValid(BufferHandle h) { return h.value != 0; }
inline bool isValid(TextureHandle h) { return h.value != 0; }

// ==================== 顶点格式 ====================

enum class VertexFormat : uint8_t {
    PositionColor,      // 位置float3 + 颜色float3 = 24字节
    PositionColorAlpha, // 位置float3 + 颜色float4 = 28字节
    PositionNormal,     // 位置float3 + 法线float3 = 24字节
    PositionUVColor,    // 位置float2 + UVfloat2 + 颜色float4 = 32字节
    WorldPosUVColor,    // 位置float3 + UVfloat2 + 颜色float4 = 36字节
    // 世界锚点float3 + 像素偏移float2 + 颜色float4 = 36字节，用于 WorldPinned
    WorldAnchorOffsetColor,
    // 位置float3 + UVfloat2 + 颜色float4 = 36字节，世界空间贴图
    WorldPosTexColor
};

enum class PrimitiveType : uint8_t {
    Points        = 0,
    Lines         = 1,
    LineStrip     = 2,
    LineLoop      = 3,
    Triangles     = 4,
    TriangleStrip = 5
};

enum class RenderSpace : uint8_t {
    World       = 0,
    Screen      = 1,
    // 跟随平移、不跟随缩放，用于世界锚点+像素偏移的定尺寸标记
    WorldPinned = 2
};

// ==================== 管线 ====================

enum class BlendFactor : uint8_t {
    Zero             = 0,
    One              = 1,
    SrcAlpha         = 2,
    OneMinusSrcAlpha = 3
};

enum class DepthFunc : uint8_t {
    Always   = 0,
    Less     = 1,
    LessEqual= 2,
    Greater  = 3
};

enum class FillMode : uint8_t {
    Solid    = 0,
    Wireframe= 1
};

struct PipelineDesc {
    PrimitiveType topology = PrimitiveType::Triangles;
    VertexFormat format = VertexFormat::PositionColor;
    uint8_t depthTest = 1;
    uint8_t depthWrite = 1;
    uint8_t blendEnable = 0;
    BlendFactor srcBlend = BlendFactor::SrcAlpha;
    BlendFactor dstBlend = BlendFactor::OneMinusSrcAlpha;
    DepthFunc depthFunc = DepthFunc::LessEqual;
    FillMode fillMode = FillMode::Solid;
    float depthBiasConstant = 0.0f;
    float depthBiasSlope = 0.0f;
};

// 本层是独立抽象类型，不是 RenderX 的二进制镜像：Adapter 逐字段转换
// （见 RenderBridge/Src/RenderXAdapter/RenderXDeviceAdapter.cpp）。
// 断言的作用是锁定抽象层自身的 POD 布局，防止字段增删无意改变尺寸。
static_assert(sizeof(PipelineDesc) == 20, "PipelineDesc ABI size changed");

// ==================== 数学类型 ====================

struct ColorF {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float a = 1.0f;
};

static_assert(sizeof(ColorF) == 16, "ColorF ABI size changed");

struct Vec2F {
    float x = 0.0f;
    float y = 0.0f;
};

struct Vec3F {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct Matrix4x4 {
    float m[4][4];
};

struct RectF {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
};

/// 3D 世界空间轴对齐包围盒
struct Aabb3 {
    float minX = 0, minY = 0, minZ = 0;
    float maxX = 0, maxY = 0, maxZ = 0;
};

/// 六平面视锥，plane[i] = (a,b,c,d)，a*x+b*y+c*z+d >= 0 在视锥内
struct Frustum {
    float planes[6][4];
};

// ==================== 绘制指令 ====================
//
// 这是UI层构造、渲染层消费的核心数据结构。
// 它不要求与 RenderX 的 DrawCommand 布局一致：Adapter 逐字段转换
// （见 toRenderXDrawCommand），UI 层因此完全不感知 Render::RT。

struct DrawInstruction {
    BufferHandle vertexBuffer;
    uint32_t vertexOffset = 0;
    uint32_t vertexCount = 0;
    uint32_t indexOffset = 0;
    uint32_t indexCount = 0;
    uint32_t instanceCount = 1;
    PrimitiveType topology = PrimitiveType::Triangles;
    VertexFormat format = VertexFormat::PositionColor;
    RenderSpace space = RenderSpace::World;
    uint32_t pipelineIndex = 0;
    uint32_t materialIndex = 0;
    float lineWidth = 0.0f;
    float pointSize = 0.0f;
    TextureHandle texture;
    uint64_t sortKey = 0;
    uint64_t userData = 0;
};

// 本层比 RenderX 的 DrawCommand（80 字节）更紧凑：不带 indexBuffer / firstInstance
// / indexType —— 这些由 Adapter 在转换时补默认值。因此**不能** memcpy 直传。
static_assert(sizeof(DrawInstruction) == 72, "DrawInstruction ABI size changed");

// ==================== 相机 ====================

struct CameraDesc {
    Matrix4x4 viewMatrix;
    Matrix4x4 projectionMatrix;
    RectF viewport; // x,y,width,height（像素）
};

// ==================== 光照 ====================

struct DirectionalLight {
    Vec3F direction;   // 指向光源方向（不需要预归一化）
    float pad0;
    ColorF color;
    float intensity = 1.0f;
};

static_assert(sizeof(DirectionalLight) == 36, "DirectionalLight ABI size changed");

struct LightingDesc {
    ColorF ambientColor;
    float ambientIntensity = 1.0f;
    bool ambientEnabled = true;
    bool doubleSided = false;
    bool specularEnabled = true;
    float specularIntensity = 1.0f;
    DirectionalLight key;
    DirectionalLight fill;
    DirectionalLight rim;
    Vec3F viewPos;     // 相机世界坐标
    float minBrightness = 0.1f;
    float exposure = 1.0f;
    float shininess = 32.0f;
};

static_assert(sizeof(LightingDesc) == 160, "LightingDesc ABI size changed");

// ==================== 帧统计 ====================

struct FrameStatistics {
    uint32_t drawCallCount = 0;
    uint32_t triangleCount = 0;
    uint32_t lineCount = 0;
    uint32_t pointCount = 0;
    uint32_t pipelineSwitches = 0;
    uint32_t culledCommandCount = 0;
    uint32_t mergedDrawCount = 0;
    uint32_t _pad0 = 0;
    uint64_t transientBytesUsed = 0;
    uint64_t geometryUploadBytes = 0;
    uint64_t gpuMemoryBytes = 0;
};

static_assert(sizeof(FrameStatistics) == 56, "FrameStatistics ABI size changed");

// ==================== 日志回调 ====================

enum class LogLevel : int32_t {
    Debug = 0,
    Info  = 1,
    Warn  = 2,
    Error = 3
};

using LogCallback = void (*)(LogLevel level, const char* message, void* userData);

// ==================== 后端枚举 ====================

enum class RenderBackend : int32_t {
    Null    = 0,
    OpenGL  = 1,
    Metal   = 2,
    Vulkan  = 3,
    Auto    = 4
};

// ==================== 原生窗口句柄 ====================
// 平台相关的窗口句柄包装

struct NativeWindowHandle {
    void* handleA = nullptr;
    void* handleB = nullptr;
};

// ==================== 排序键 ====================

inline uint64_t makeSortKey(uint8_t layer, uint8_t transparent, uint8_t depth, uint16_t seq) {
    return (static_cast<uint64_t>(layer) << 56)
         | (static_cast<uint64_t>(transparent) << 48)
         | (static_cast<uint64_t>(depth) << 32)
         | (static_cast<uint64_t>(seq));
}

// ==================== 纹理创建描述 ====================

struct TextureDesc {
    uint32_t width = 0;
    uint32_t height = 0;
    const uint8_t* rgba = nullptr;
    uint64_t rgbaBytes = 0;
};

static_assert(sizeof(TextureDesc) == 24, "TextureDesc ABI size changed");

// ==================== 缓冲创建描述 ====================

struct BufferDesc {
    uint64_t sizeBytes = 0;
    bool cpuWritable = false;
};

// ==================== 着色器管线句柄 ====================
// pipelineIndex 用 uint32_t 而非 uint16_t 以支持更多管线

} // namespace RenderAbstraction
