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
 * 实现现状：转换逻辑目前内联在 RenderXDeviceAdapter.cpp 的适配器类内
 * （作为私有静态成员函数）。接线到 IRenderFactory / IRenderDevice 路径时，
 * 应把实现整体搬到 TypeConversions.cpp，只留这些声明。
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
Render::RT::DefaultPipeline toRenderXDefaultPipeline(RenderAbstraction::VertexFormat fmt,
                                                          RenderAbstraction::RenderSpace space,
                                                          RenderAbstraction::PrimitiveType topo);

// DrawInstruction → DrawCommand (用于提交)
Render::RT::DrawCommand toRenderXDrawCommand(const RenderAbstraction::DrawInstruction& cmd);

// LightingDesc → Lighting3DDesc
Render::RT::Lighting3DDesc toRenderXLighting(const RenderAbstraction::LightingDesc& lighting);

// CameraDesc → viewMatrix (float[16])
void toRenderXViewMatrix(const RenderAbstraction::Matrix4x4& view, float out[16]);

// RenderX → RenderAbstraction
RenderAbstraction::VertexFormat fromRenderXFormat(Render::RT::VertexFormat fmt);
RenderAbstraction::RenderSpace fromRenderXSpace(Render::RT::RenderSpace space);
RenderAbstraction::PrimitiveType fromRenderXPrimitive(Render::RT::PrimitiveTopology topo);
RenderAbstraction::FrameStatistics fromRenderXStats(const Render::RT::FrameStats& stats);

} // namespace RenderXConversion
} // namespace RenderBridge
