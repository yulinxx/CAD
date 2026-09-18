#pragma once
/**
 * @file TypeConversions.h
 * @brief RenderAbstraction ↔ RenderX 类型转换
 *
 * 所有转换在此集中声明，确保UI层完全不需要知道 Render::RT 的存在。
 *
 * 本头位于 Src/ 下，属于 RenderBridge 的**私有实现头**，UI 层不应包含它；
 * 正因如此它可以直接引 renderx.h，不需要像公共 Include/ 那样做类型隔离。
 *
 * 命名空间用 RenderXConversion 而非 RenderXAdapter：后者在 RenderBridge
 * 作用域里已经是工厂类 RenderXAdapter 的名字，类名与命名空间名在同一作用域
 * 不能共存。
 *
 * 全部实现都在 TypeConversions.cpp，本头只放声明。
 */

#include "RenderAbstraction/IRenderTypes.h"

// 函数签名里出现 Render::RT 的枚举与结构体，需要它们的完整定义
#include "render/renderx.h"

namespace RenderBridge {
namespace RenderXConversion {

// RenderAbstraction → RenderX

Render::RT::VertexFormat toRenderXFormat(RenderAbstraction::VertexFormat fmt);
Render::RT::RenderSpace toRenderXSpace(RenderAbstraction::RenderSpace space);
Render::RT::PrimitiveTopology toRenderXPrimitive(RenderAbstraction::PrimitiveType type);
Render::RT::BlendFactor toRenderXBlend(RenderAbstraction::BlendFactor bf);
Render::RT::DepthFunc toRenderXDepthFunc(RenderAbstraction::DepthFunc df);
Render::RT::FillMode toRenderXFillMode(RenderAbstraction::FillMode fm);

// 具名管线 → RenderX 的内建管线枚举
Render::RT::DefaultPipeline toRenderXDefaultPipeline(RenderAbstraction::PipelineKind kind);

// PipelineDesc → PipelineDesc
Render::RT::PipelineDesc toRenderXPipelineDesc(const RenderAbstraction::PipelineDesc& desc);

// DrawInstruction → DrawCommand (用于提交)
Render::RT::DrawCommand toRenderXDrawCommand(const RenderAbstraction::DrawInstruction& cmd);

// LightingDesc → Lighting3DDesc
Render::RT::Lighting3DDesc toRenderXLighting(const RenderAbstraction::LightingDesc& lighting);

// FontDesc → FontDesc（字段一一对应，两侧同名不同类型）
Render::RT::FontDesc toRenderXFontDesc(const RenderAbstraction::FontDesc& desc);

// MaterialDesc → MaterialDesc（同上）
Render::RT::MaterialDesc toRenderXMaterial(const RenderAbstraction::MaterialDesc& desc);

// CameraDesc → viewMatrix (float[16])
void toRenderXViewMatrix(const RenderAbstraction::Matrix4x4& view, float out[16]);

// DrawListDesc → DrawListDesc（抽象层的可见范围类型不落进 RenderX，由适配器自己记）
Render::RT::DrawListDesc toRenderXDrawListDesc(const RenderAbstraction::DrawListDesc& desc);

// GeometryStoreDesc → GeometryStoreDesc
Render::RT::GeometryStoreDesc toRenderXGeometryStoreDesc(const RenderAbstraction::GeometryStoreDesc& desc);

// RenderX 的 GeometryBlock → 抽象层 POD
RenderAbstraction::GeometryBlock fromRenderXGeometryBlock(const Render::RT::GeometryBlock& block);

// RxResult → 分配结果（区分「本仓已满」与「真失败」）
RenderAbstraction::GeometryAllocResult fromRenderXGeometryAlloc(Render::RT::RxResult result);

// GeometryStoreStats → GeometryStoreStats（字段一一对应）
RenderAbstraction::GeometryStoreStats fromRenderXGeometryStats(const Render::RT::GeometryStoreStats& stats);

// 条目包围盒 → 2D 世界矩形 (minX, minY, maxX, maxY)，只取 x/y
void toRenderXViewBounds(const RenderAbstraction::Aabb3& box, float out[4]);

// 条目包围盒 → 世界空间 3D AABB
Render::RT::RxAabb3 toRenderXAabb3(const RenderAbstraction::Aabb3& box);

// 六平面视锥（两侧都是 planes[6][4]）
Render::RT::RxFrustum toRenderXFrustum(const RenderAbstraction::Frustum& frustum);

// RenderX → RenderAbstraction
RenderAbstraction::VertexFormat fromRenderXFormat(Render::RT::VertexFormat fmt);
RenderAbstraction::RenderSpace fromRenderXSpace(Render::RT::RenderSpace space);
RenderAbstraction::PrimitiveType fromRenderXPrimitive(Render::RT::PrimitiveTopology topo);
RenderAbstraction::FrameStatistics fromRenderXStats(const Render::RT::FrameStats& stats);
RenderAbstraction::FontMetrics fromRenderXFontMetrics(const Render::RT::FontMetrics& metrics);
RenderAbstraction::GlyphInfo fromRenderXGlyphInfo(const Render::RT::GlyphInfo& glyph);

} // namespace RenderXConversion
} // namespace RenderBridge
