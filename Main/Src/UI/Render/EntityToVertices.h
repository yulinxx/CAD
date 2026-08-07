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
 */
#pragma once

#include <render/render_types.h>
#include <vector>

namespace Eg
{
    struct SyEntity;
}

/**
 * @brief 将单个引擎图元转换为 VertexP3C3 顶点数组
 *
 * 曲线类图元（Bezier/Bezier2/Nurbs/Circle/Arc/Ellipse）支持离散化缓存：
 * 当实体仅因颜色/选择/图层变更而标记为脏时，直接复用缓存的顶点数据。
 *
 * @param entity      引擎图元指针（非空）
 * @param outVertices 输出顶点数组
 * @param outType     输出图元类型（LineStrip/LineLoop/PointList）
 * @return true 转换成功，false 表示该类型不支持增量路径（如文本）
 */
bool entityToVertices(const Eg::SyEntity* entity,
    std::vector<render::VertexP3C3>& outVertices,
    render::PrimitiveType& outType);

/**
 * @brief 清空曲线离散化缓存
 *
 * 全量刷新或场景重建时调用，确保缓存不会持有已删除实体的旧数据。
 */
void clearEntityVertexCache();