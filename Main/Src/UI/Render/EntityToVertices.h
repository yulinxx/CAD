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
 *   - **无状态**：本模块不再持有按图元 ID 的离散化缓存
 *
 * ## 为什么不再有离散化缓存
 *
 * 这里曾有一份进程级 `static` 的曲线离散化缓存（键为图元 ID，配一把
 * `shared_mutex`），理由是「图元只因颜色变化而变脏时跳过重新离散化」。
 * 它有两个结构性问题：
 *
 * 1. **失效靠外部驱动**。条目不会自行过期，删除图元要调
 *    `eraseEntityVertexCache()`、全量刷新要调 `clearEntityVertexCache()`，
 *    三个调用点全在 `SceneRefreshCoordinator` 里。任何新增的删除/重建路径
 *    漏调一次，缓存里就留下陈旧顶点，而且不报错。
 * 2. **收益与风险不成比例**。全量刷新前本来就先 `clear` 一次，那条路上它
 *    等于没有；增量路径上真正命中它的只有「曲线几何没变、颜色变了」这一种
 *    情形（拖动/缩放的几何在变，哈希本来就不匹配）。而它换来的是无界增长
 *    （文档自己记过「新建—删除」循环会把表撑大）与一个跨线程可变的全局态。
 *
 * 现在「这段几何跟上一轮比有没有变」由唯一一处回答：`RenderSceneBuilder`
 * 的段位账本（每段记顶点内容哈希，装配轮次里自比对）。它属于持有几何块的
 * 那一层，不需要任何外部失效调用，因此这里退回成纯函数。
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
 * @return true 转换成功，false 表示该类型不支持增量路径（如文本）
 */
bool entityToVertices(
    const Eg::SyEntity* entity, std::vector<Render::VertexP3C3>& outVertices, Render::PrimitiveType& outType,
    const double* cameraCenter = nullptr);
