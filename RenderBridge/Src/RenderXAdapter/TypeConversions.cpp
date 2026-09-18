/**
 * @file TypeConversions.cpp
 * @brief RenderAbstraction ↔ RenderX 类型转换实现（声明见 TypeConversions.h）
 *
 * 所有 Render::RT 的身影都被关在本文件与适配器实现里：适配器的其余部分
 * 只面对 RenderAbstraction 的类型，UI 层则完全看不到两者。
 *
 * 转换一律逐字段写，不做 memcpy 直传 —— RenderAbstraction 是独立的抽象
 * 布局（比 RenderX 更紧凑，见 IRenderTypes.h 的 static_assert），直传会错位。
 */
#include "TypeConversions.h"

#include <cstring>

namespace RenderBridge {
namespace RenderXConversion {

using namespace RenderAbstraction;

// ==================== RenderAbstraction → RenderX ====================

Render::RT::VertexFormat toRenderXFormat(VertexFormat fmt) {
    switch (fmt) {
    case VertexFormat::PositionColor:          return Render::RT::VertexFormat::P3C3;
    case VertexFormat::PositionColorAlpha:     return Render::RT::VertexFormat::P3C4;
    case VertexFormat::PositionNormal:         return Render::RT::VertexFormat::P3N3;
    case VertexFormat::PositionUVColor:        return Render::RT::VertexFormat::P2T2C4;
    case VertexFormat::WorldPosUVColor:        return Render::RT::VertexFormat::P3T2C4;
    // 定尺寸标记专用：世界锚点 + 像素偏移，配 RenderSpace::WorldPinned
    case VertexFormat::WorldAnchorOffsetColor: return Render::RT::VertexFormat::P3O2C4;
    // 世界空间贴图与 WorldPosUVColor 是同一份布局（P3T2C4），只是语义不同
    case VertexFormat::WorldPosTexColor:       return Render::RT::VertexFormat::P3T2C4;
    }
    return Render::RT::VertexFormat::P3C3;
}

Render::RT::RenderSpace toRenderXSpace(RenderSpace space) {
    // RenderX 只有 World / Screen；WorldPinned 由顶点格式 P3O2C4 表达
    return space == RenderSpace::Screen
        ? Render::RT::RenderSpace::Screen : Render::RT::RenderSpace::World;
}

Render::RT::PrimitiveTopology toRenderXPrimitive(PrimitiveType type) {
    switch (type) {
    case PrimitiveType::Points:    return Render::RT::PrimitiveTopology::Points;
    case PrimitiveType::Lines:     return Render::RT::PrimitiveTopology::Lines;
    case PrimitiveType::Triangles: return Render::RT::PrimitiveTopology::Triangles;
    // RenderX 的公共拓扑枚举里没有 Strip/Loop。这两个值属于「调用方应先自行
    // 展开成 Lines/Triangles」的输入（UI 层画折线时就是这么做的），这里只做
    // 同类退化 —— 旧实现让它们静默落到兜底的 Triangles，连类别都错了。
    case PrimitiveType::LineStrip:     return Render::RT::PrimitiveTopology::Lines;
    case PrimitiveType::LineLoop:      return Render::RT::PrimitiveTopology::Lines;
    case PrimitiveType::TriangleStrip: return Render::RT::PrimitiveTopology::Triangles;
    }
    return Render::RT::PrimitiveTopology::Triangles;
}

Render::RT::BlendFactor toRenderXBlend(BlendFactor bf) {
    switch (bf) {
    case BlendFactor::Zero:             return Render::RT::BlendFactor::Zero;
    case BlendFactor::One:              return Render::RT::BlendFactor::One;
    case BlendFactor::SrcAlpha:         return Render::RT::BlendFactor::SrcAlpha;
    case BlendFactor::OneMinusSrcAlpha: return Render::RT::BlendFactor::OneMinusSrcAlpha;
    }
    return Render::RT::BlendFactor::SrcAlpha;
}

Render::RT::DepthFunc toRenderXDepthFunc(DepthFunc df) {
    switch (df) {
    case DepthFunc::Always:    return Render::RT::DepthFunc::Always;
    case DepthFunc::Less:      return Render::RT::DepthFunc::Less;
    case DepthFunc::LessEqual: return Render::RT::DepthFunc::LessEqual;
    case DepthFunc::Greater:   return Render::RT::DepthFunc::Greater;
    }
    return Render::RT::DepthFunc::LessEqual;
}

Render::RT::FillMode toRenderXFillMode(FillMode fm) {
    return fm == FillMode::Wireframe
        ? Render::RT::FillMode::Wireframe : Render::RT::FillMode::Solid;
}

Render::RT::DefaultPipeline toRenderXDefaultPipeline(VertexFormat fmt, RenderSpace space, PrimitiveType topo) {
    // 与 RenderX 内部按 (格式, 空间, 拓扑) 解析默认管线的规则保持一致。
    // 调用方只在「没有显式指定管线」时才该走这里；显式指定的场景（如字形
    // 与位图同格式同拓扑但管线不同）必须直接填 pipelineIndex。
    const Render::RT::VertexFormat rf = toRenderXFormat(fmt);
    const Render::RT::RenderSpace rs = toRenderXSpace(space);
    const Render::RT::PrimitiveTopology rt = toRenderXPrimitive(topo);

    if (rf == Render::RT::VertexFormat::P3N3
        && rs == Render::RT::RenderSpace::World
        && rt == Render::RT::PrimitiveTopology::Triangles) {
        return Render::RT::DefaultPipeline::Mesh3D;
    }
    if (rf == Render::RT::VertexFormat::P3C4 && rt == Render::RT::PrimitiveTopology::Triangles) {
        return rs == Render::RT::RenderSpace::Screen
            ? Render::RT::DefaultPipeline::ScreenTextured
            : Render::RT::DefaultPipeline::WorldTextured;
    }
    return Render::RT::DefaultPipeline::WorldTri;
}

Render::RT::DrawCommand toRenderXDrawCommand(const DrawInstruction& cmd) {
    Render::RT::DrawCommand out{};
    // 句柄：本层的包装结构 → RenderX 的 enum class
    out.vertexBuffer = static_cast<Render::RT::BufferHandle>(cmd.vertexBuffer.value);
    // 本层不带索引缓冲（索引化绘制尚未进入抽象层）
    out.indexBuffer = Render::RT::BufferHandle::Invalid;
    out.texture = static_cast<Render::RT::TextureHandle>(cmd.texture.value);
    out.sortKey = cmd.sortKey;
    out.userData = cmd.userData;
    out.vertexOffset = cmd.vertexOffset;
    out.vertexCount = cmd.vertexCount;
    out.indexOffset = cmd.indexOffset;
    out.indexCount = cmd.indexCount;
    out.instanceCount = cmd.instanceCount;
    out.firstInstance = 0;
    out.topology = toRenderXPrimitive(cmd.topology);
    out.space = toRenderXSpace(cmd.space);
    out.vertexFormat = toRenderXFormat(cmd.format);
    out.indexType = Render::RT::IndexType::None;
    // 本层用 uint32 承载管线/材质编号以留扩展余地；实际值来自
    // rxPipelineCreate / rxMaterialAdd 的 uint16 索引，不会溢出
    out.pipelineIndex = static_cast<uint16_t>(cmd.pipelineIndex);
    out.materialIndex = static_cast<uint16_t>(cmd.materialIndex);
    out.lineWidth = cmd.lineWidth;
    out.pointSize = cmd.pointSize;
    return out;
}

Render::RT::Lighting3DDesc toRenderXLighting(const LightingDesc& lighting) {
    Render::RT::Lighting3DDesc out{};
    out.ambientColor[0] = lighting.ambientColor.r;
    out.ambientColor[1] = lighting.ambientColor.g;
    out.ambientColor[2] = lighting.ambientColor.b;
    out.ambientIntensity = lighting.ambientIntensity;
    out.ambientEnabled = lighting.ambientEnabled ? 1 : 0;
    out.doubleSided = lighting.doubleSided ? 1 : 0;
    out.specularEnabled = lighting.specularEnabled ? 1 : 0;
    out.specularIntensity = lighting.specularIntensity;

    // 三盏灯的填法一致，逐字段搬；enabled 由 intensity 推导（本层没有该开关）
    const auto fillLight = [](Render::RT::DirectionalLight3D& dst, const DirectionalLight& src) {
        dst.enabled = src.intensity > 0.0f ? 1 : 0;
        dst.direction[0] = src.direction.x;
        dst.direction[1] = src.direction.y;
        dst.direction[2] = src.direction.z;
        dst.color[0] = src.color.r;
        dst.color[1] = src.color.g;
        dst.color[2] = src.color.b;
        dst.intensity = src.intensity;
    };
    fillLight(out.key, lighting.key);
    fillLight(out.fill, lighting.fill);
    fillLight(out.rim, lighting.rim);

    out.viewPos[0] = lighting.viewPos.x;
    out.viewPos[1] = lighting.viewPos.y;
    out.viewPos[2] = lighting.viewPos.z;
    out.minBrightness = lighting.minBrightness;
    out.exposure = lighting.exposure;
    out.shininess = lighting.shininess;
    return out;
}

void toRenderXViewMatrix(const Matrix4x4& view, float out[16]) {
    // 两侧都是 16 个连续的 float（列主序），直接搬
    std::memcpy(out, view.m, 16 * sizeof(float));
}

// ==================== RenderX → RenderAbstraction ====================

VertexFormat fromRenderXFormat(Render::RT::VertexFormat fmt) {
    switch (fmt) {
    case Render::RT::VertexFormat::P3C3:   return VertexFormat::PositionColor;
    case Render::RT::VertexFormat::P3C4:   return VertexFormat::PositionColorAlpha;
    case Render::RT::VertexFormat::P3N3:   return VertexFormat::PositionNormal;
    case Render::RT::VertexFormat::P2T2C4: return VertexFormat::PositionUVColor;
    case Render::RT::VertexFormat::P3O2C4: return VertexFormat::WorldAnchorOffsetColor;
    case Render::RT::VertexFormat::P3T2C4: return VertexFormat::WorldPosUVColor;
    }
    return VertexFormat::PositionColor;
}

RenderSpace fromRenderXSpace(Render::RT::RenderSpace space) {
    return space == Render::RT::RenderSpace::Screen ? RenderSpace::Screen : RenderSpace::World;
}

PrimitiveType fromRenderXPrimitive(Render::RT::PrimitiveTopology topo) {
    switch (topo) {
    case Render::RT::PrimitiveTopology::Points:    return PrimitiveType::Points;
    case Render::RT::PrimitiveTopology::Lines:     return PrimitiveType::Lines;
    case Render::RT::PrimitiveTopology::LineStrip: return PrimitiveType::Lines;
    case Render::RT::PrimitiveTopology::LineLoop: return PrimitiveType::Lines;
    case Render::RT::PrimitiveTopology::Triangles: return PrimitiveType::Triangles;
    case Render::RT::PrimitiveTopology::TriangleStrip: return PrimitiveType::Triangles;
    }
    return PrimitiveType::Triangles;
}

FrameStatistics fromRenderXStats(const Render::RT::FrameStats& stats) {
    // 只搬本层声明过的字段：其余（管线切换数、瞬态环占用等）留在后端侧，
    // 抽象层目前不对 UI 暴露。
    FrameStatistics out{};
    out.drawCallCount = stats.drawCallCount;
    out.triangleCount = stats.triangleCount;
    out.lineCount = stats.lineCount;
    out.pointCount = stats.pointCount;
    out.gpuMemoryBytes = stats.gpuMemoryBytes;
    return out;
}

} // namespace RenderXConversion
} // namespace RenderBridge
