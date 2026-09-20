/**
 * @file EntityToVertices.h
 * @brief 引擎图元 → 渲染顶点转换工具
 *
 * 将 SyEntity 派生类转换为 VertexP3C3 数组，供 RenderWidget 增量渲染 API 使用。
 * 从 RenderViewport2D.cpp 中提取，独立为可复用的工具模块。
 *
 * 职责边界：
 *   - 仅负责几何→顶点离散化，不涉及渲染提交或 OpenGL 调用
 *   - 文本等复杂类型返回 false，由调用方走全量刷新路径
 *   - **无状态**：本模块不持有按图元 ID 的离散化缓存
 *
 * 几何变化检测由 RenderSceneBuilder 的段位账本负责（每段记顶点内容哈希，装配轮次里自比对）。
 */
#pragma once

// 大写 Render/：应用层词汇表（顶点布局 / PrimitiveType / tess 参数），
// 由 UI/Common/Include 提供，与渲染 DLL 的公共头无关。
#include "Render/RenderTypes.h"
#include <vector>

namespace Eg
{
    struct SyEntity;
}

/**
 * @brief 将单个引擎图元转换为 VertexP3C3 顶点数组
 *
 * 由 Engine 侧 emitEntityGeometry 完成图元分解，本地 sink 离散化为顶点。
 * 本函数不保留任何跨调用状态：几何没变时不做重复上传，是消费端
 * （`RenderSceneBuilder::upsertEntity` 的段位哈希）的判断，不是这一层的。
 *
 * @param entity       引擎图元指针（非空）
 * @param outVertices  输出顶点数组
 * @param outType      输出图元类型（LineStrip/LineLoop/PointList）
 * @param cameraCenter 相机中心（世界坐标），用于精度优化（nullptr 时退化为直接转换）
 * @param worldToScreenScale 世界单位 → 屏幕像素的比例（正交相机下即 zoom），
 *       曲线（圆/弧/椭圆）离散化按此自适应段数。默认 1.0 = 与 *Fixed 一致。
 * @param chordErrorPixels 曲线 LOD 的目标弦高误差（屏幕像素），取值越大段数越少。
 *       默认取 Render::tess::kLodChordErrorPixels；调用方传视口的当前设置值，
 *       保证增量路径与全量路径（RenderSceneBuilder）用同一份精度。
 * @return true 转换成功，false 表示该类型不支持增量路径（如文本）
 */
bool entityToVertices(
    const Eg::SyEntity* entity, std::vector<Render::VertexP3C3>& outVertices, Render::PrimitiveType& outType,
    const double* cameraCenter = nullptr, double worldToScreenScale = 1.0,
    double chordErrorPixels = Render::tess::kLodChordErrorPixels);
