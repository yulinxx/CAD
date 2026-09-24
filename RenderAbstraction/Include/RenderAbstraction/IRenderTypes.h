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
/// 保留式绘制列表句柄：由后端持有场景状态，只在图元变化时 upsert 槽位
struct DrawListHandle { uint64_t value = 0; };
struct BufferHandle { uint64_t value = 0; };
struct TextureHandle { uint64_t value = 0; };
/// 常驻几何仓句柄
struct GeometryStoreHandle { uint64_t value = 0; };

inline bool isValid(DeviceHandle h) { return h.value != 0; }
inline bool isValid(DrawListHandle h) { return h.value != 0; }
inline bool isValid(BufferHandle h) { return h.value != 0; }
inline bool isValid(TextureHandle h) { return h.value != 0; }
inline bool isValid(GeometryStoreHandle h) { return h.value != 0; }

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

/**
 * @brief 顶点格式的字节步长
 *
 * 宿主侧各有自己的顶点结构（UICommon 的 VertexP3C3、覆盖层的 OVertex 等），
 * 它们必须与后端按同一格式解读的步长一致——不一致的表现是几何整体错位，
 * 很难从画面反推。这个函数就是那条契约的单一来源：宿主用自己的结构体尺寸
 * 与它做 static_assert，不必为了一个常量去引后端头文件。
 */
inline constexpr uint32_t vertexStride(VertexFormat fmt) {
    switch (fmt) {
    case VertexFormat::PositionColor:          return 24;  // float3 + float3
    case VertexFormat::PositionColorAlpha:     return 28;  // float3 + float4
    case VertexFormat::PositionNormal:         return 24;  // float3 + float3
    case VertexFormat::PositionUVColor:        return 32;  // float2 + float2 + float4
    case VertexFormat::WorldPosUVColor:        return 36;  // float3 + float2 + float4
    case VertexFormat::WorldAnchorOffsetColor: return 36;  // float3 + float2 + float4
    case VertexFormat::WorldPosTexColor:       return 36;  // float3 + float2 + float4
    }
    return 0;
}

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

/**
 * @brief 具名内建管线
 *
 * 后端按「顶点格式 + 渲染空间 + 拓扑」能自动解析的只是其中一部分；剩下那些
 * **同格式同空间同拓扑、差别只在片元**的管线（字形 vs 贴图、网格 vs 线框、
 * 高亮 vs gizmo）无法由三元组区分，必须由调用方显式点名。
 *
 * 这里只列业务真正要区分的那几条，不照搬后端的内建管线全集：适配层用 switch
 * 映射，未列出的组合由后端的默认解析规则兜底。
 */
enum class PipelineKind : uint8_t {
    /// 世界空间通用三角（P3C3/P3C4，无贴图无光照）
    WorldTri,
    /// 世界空间贴图：采样 RGBA 位图（P3T2C4）
    WorldTextured,
    /// 屏幕空间贴图：顶点当像素坐标（P2T2C4）
    ScreenTextured,
    /// 屏幕空间字形：图集是 R8 覆盖率，alpha 取 .r、rgb 取顶点色
    ScreenGlyph,
    /// 世界空间字形：图集是 R8 距离场，靠 fwidth 得到缩放无关的抗锯齿
    WorldGlyphSdf,
    /// 3D 网格：P3N3 + 内建光照，颜色与高光来自 MaterialDesc
    Mesh3D,
    /// 同 Mesh3D，填充模式为线框
    Mesh3DWire,
    /// 3D 选中高亮：三角面按线框填充，测深不写深
    Highlight3D,
    /// 3D 变换手柄：实心填充 + 深度偏移，压住模型表面不 z-fighting
    Gizmo3D
};

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

// ==================== 统一可见范围 ====================
//
// 2D 视口是「相机正交俯视」的世界矩形，判据只用到条目的 x/y；
// 3D 视口在斜视角下必须用六平面视锥配完整 3D 包围盒，否则会误裁可见图元。
// 两者是**不同的剔除判据**，但同属「本帧可见范围」这一个概念，因此在抽象层
// 合成一个带类型标签的结构，由适配层翻译成后端各自的提交接口。

/// 可见范围类型，同时决定条目包围盒的解读方式
enum class ViewVolumeType : uint8_t {
    /// 世界空间矩形 (minX, minY, maxX, maxY)，只用包围盒的 x/y
    Rect2D    = 0,
    /// 六平面视锥，使用完整 3D 包围盒
    Frustum3D = 1
};

/// 统一可见范围。按 type 取用对应成员，另一成员被忽略
struct ViewVolume {
    ViewVolumeType type = ViewVolumeType::Rect2D;
    /// type == Rect2D 时有效：世界空间 (minX, minY, maxX, maxY)
    float rect[4] = {};
    /// type == Frustum3D 时有效
    Frustum frustum;
};

static_assert(sizeof(ViewVolume) == 116, "ViewVolume ABI size changed");

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

// ==================== 瞬态提交 ====================
//
// 顶点每帧都变的数据走瞬态缓冲：beginFrame 之后分配一段本帧可写内存，直接写顶点，
// 再用 DrawPacket 批量提交。缓冲在 endFrame 后失效，跨帧不可复用。

/// 瞬态缓冲分配结果。cpuPtr 仅在 beginFrame / endFrame 之间有效。
struct TransientAlloc {
    /// 分配到的缓冲，原样填 DrawInstruction::vertexBuffer
    BufferHandle buffer;
    /// 本帧可直接写入的映射指针
    uint8_t* cpuPtr = nullptr;
    /// 缓冲内字节偏移，原样填 DrawInstruction::vertexOffset
    uint32_t offset = 0;
    uint32_t sizeBytes = 0;
};

/// 一次批量提交：若干指令 + 本批共用的视图矩阵与视口
struct DrawPacket {
    const DrawInstruction* commands = nullptr;
    uint32_t commandCount = 0;
    /// 是否启用渲染侧的包围盒 / 视锥剔除
    bool enableCulling = false;
    /// 是否覆盖 Session 当前的视图矩阵；false 时 viewMatrix 被忽略
    bool hasViewMatrix = false;
    /// 列主序 4x4
    float viewMatrix[16] = {};
    /// x, y, width, height（像素）。全零表示整个表面
    float viewport[4] = {};
};

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

// ==================== 字体 ====================

/// 字体句柄
struct FontHandle { uint64_t value = 0; };

inline bool isValid(FontHandle h) { return h.value != 0; }

/// 字体创建描述
struct FontDesc {
    /// TTF/OTF 字节。渲染侧会**内部拷贝**一份，调用方可以立即释放；
    /// 同一 data 指针 + size 的多次 createFont 会共享同一份内部拷贝（见 rxFont）。
    const void* data = nullptr;
    uint64_t dataBytes = 0;
    /// 光栅化像素高度。SDF 模式下这只是距离场的采样精度，不是显示字号
    float pixelHeight = 0.0f;
    /// 图集尺寸，0 表示用默认值
    uint32_t atlasWidth = 0;
    uint32_t atlasHeight = 0;
    /// > 0 表示生成有符号距离场：可任意缩放，一个 FontHandle 覆盖所有显示尺寸
    uint32_t sdfPadding = 0;
};

/// 字体级度量（像素，已按 FontDesc::pixelHeight 缩放）
struct FontMetrics {
    /// 基线以上高度（正值）
    float ascent = 0.0f;
    /// 基线以下深度（**负值**）
    float descent = 0.0f;
    /// 行间额外间隙。行高 = ascent - descent + lineGap
    float lineGap = 0.0f;
    /// 回显创建时的 pixelHeight，便于调用方按需缩放
    float pixelHeight = 0.0f;
};

/// 单个字形在图集中的位置与排版度量。
/// 坐标以**基线上的笔位置**为原点，x 向右、y 向下为正（屏幕空间约定）
struct GlyphInfo {
    /// 图集 UV，已归一化到 [0,1]
    float u0 = 0.0f;
    float v0 = 0.0f;
    float u1 = 0.0f;
    float v1 = 0.0f;
    /// 相对笔位置的像素偏移（bearingY 通常为负：字形在基线之上）
    float bearingX = 0.0f;
    float bearingY = 0.0f;
    /// 字形位图像素尺寸。空白字符（空格）为 0，此时不必产出四边形
    float width = 0.0f;
    float height = 0.0f;
    /// 水平步进（像素）
    float advance = 0.0f;
};

// ==================== 材质 ====================

/// 材质描述。2D 管线只消费 lineWidth / pointSize，3D 网格靠 color / ambient / specular
struct MaterialDesc {
    /// 线宽（像素）。各后端上限不同，请以设备能力为准
    float lineWidth = 0.0f;
    float pointSize = 0.0f;
    /// 漫反射色（RGBA）。2D 管线不消费此值——2D 的颜色在顶点里
    float color[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    uint32_t flags = 0;
    /// 环境反射色。仅 3D 网格管线消费
    float ambient[3] = { 0.0f, 0.0f, 0.0f };
    /// 镜面反射色。仅 3D 网格管线消费
    float specular[3] = { 0.0f, 0.0f, 0.0f };
    /// Phong 高光指数，越大高光越锐
    float shininess = 32.0f;
};

// ==================== 离屏渲染目标 ====================

/// 渲染目标绑定。颜色句柄无效时表示恢复默认交换链
struct RenderTargetBinding {
    TextureHandle color;
    /// 可选深度附件；无效句柄表示不使用深度
    TextureHandle depth;
    /// 0 表示采用颜色附件的尺寸
    uint32_t width = 0;
    uint32_t height = 0;
};

// ==================== 常驻几何仓 ====================
//
// 用途与「每帧重传顶点的瞬态环」相反：图元顶点常驻显存，编辑时只重写变化的
// 那一块。仓按块分配，块是**逐图元的小块**，因此第 3 个字段的 offset 不能当
// 身份用（扩容或整理后会变），释放与写入一律用 id。

struct GeometryStoreDesc {
    /// 初始容量（字节）。0 表示用后端默认值
    uint64_t initialBytes = 0;
    /// 单仓增长上限（字节）。0 表示不限（后端仍会钳到自己的硬顶）
    uint64_t maxBytes = 0;
    /// 分配粒度（字节）。0 表示用后端默认值（通常已按 vec4 对齐）
    uint32_t granularity = 0;
    /// 仓内放的是索引数据而非顶点数据（影响后端的对齐与用途推断）
    bool forIndices = false;
};

struct GeometryBlock {
    /// 块所在仓的缓冲句柄。仓扩容不会让它失效（后端原地改写句柄槽位），
    /// 因此可以长期持有，直接填 DrawInstruction::vertexBuffer
    BufferHandle buffer;
    /// 块标识。释放与写入都用它
    uint64_t id = 0;
    /// 块内字节偏移，原样填 DrawInstruction::vertexOffset
    uint32_t offset = 0;
    uint32_t sizeBytes = 0;
};

static_assert(sizeof(GeometryBlock) == 24, "GeometryBlock ABI size changed");

/// 分配结果。区别于单纯的成败：本仓已满时调用方应换一个仓，而不是放弃
enum class GeometryAllocResult : uint8_t {
    Ok,
    /// 本仓已到上限，换仓（或新开仓）后重试
    StoreFull,
    Failed
};

struct GeometryStoreStats {
    uint64_t capacityBytes = 0;
    /// 已分配给块的字节数（含粒度对齐产生的内部浪费）
    uint64_t usedBytes = 0;
    /// 空闲表中最大连续空洞，用于判断是否需要整理。
    /// 注意它**不是可加的**：多仓合并统计时取最大值，不能求和
    uint64_t largestFreeBytes = 0;
    uint32_t blockCount = 0;
    uint32_t freeRangeCount = 0;
    /// 本帧因写入而排队的脏字节数（合并后）
    uint64_t dirtyBytesThisFrame = 0;
    /// 累计扩容次数。频繁扩容说明初始容量给小了
    uint32_t growCount = 0;
    uint32_t _pad0 = 0;
};

static_assert(sizeof(GeometryStoreStats) == 48, "GeometryStoreStats ABI size changed");

// ==================== 保留式绘制列表 ====================

/**
 * @brief 绘制列表创建描述
 *
 * type 一旦确定不再改变：条目包围盒的语义与提交时的可见范围判据都由它决定，
 * 一个列表内的条目必须同属一套判据（2D 图元与 3D 图元各用各的列表）。
 */
struct DrawListDesc {
    /// 预留槽位数。槽号可跳号，列表按需增长，预留只是避免早期反复搬迁
    uint32_t initialCapacity = 4096;
    /// 是否允许相邻同伴合并成一次绘制。3D 网格必须关闭：相邻图元各有自己的
    /// 材质，合批会把材质抹成一个
    bool enableMerging = true;
    /// 是否启用基于条目包围盒的剔除
    bool enableCulling = true;
    /// 可见范围类型，决定条目包围盒与提交范围的解读方式
    ViewVolumeType type = ViewVolumeType::Rect2D;
};

// ==================== 着色器管线句柄 ====================
// pipelineIndex 用 uint32_t 而非 uint16_t 以支持更多管线

} // namespace RenderAbstraction
