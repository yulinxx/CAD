/**
 * @file PinnedMarkerGeometry.h
 * @brief 屏幕定尺寸方块标记的顶点生成：纯几何，不碰渲染会话
 *
 * ## 为什么单独成一个头
 *
 * 选择缩放手柄与点标记（旋转手柄 / 编辑器控制点 / 绘制控制点）都是
 * 「锚点在世界、大小恒为 N 像素」的装饰，走 `RenderSpace::WorldPinned` +
 * `VertexFormat::P3O2C4`（世界锚点 + 像素偏移 + RGBA），换算在
 * `world_pinned_p3o2c4.vert` 里完成（见 `Docs/03-渲染主链/新渲染架构.md` §15）。
 *
 * 这段几何原先内联在 `OverlaySceneBuilder.cpp` 的匿名命名空间里，和瞬态环分配
 * （`rxSessionAllocTransient`）耦在一起，导致两件事：
 *
 * 1. **不可测**。`DrawCommand` 只暴露 buffer/offset/count，顶点字节不可回读，
 *    「尺寸被夹到下限」「像素偏移生效」这类性质无法从公开接口观察；而且
 *    `build()` 必须在 BeginFrame/EndFrame 之间跑，需要一个真实渲染会话。
 * 2. **易在合并中丢失**。定义与调用点相隔两百行，曾出现过定义被合并丢掉、
 *    调用留在原处的情况（C2065/C3861）。
 *
 * 拆出来之后：本头只依赖 RenderTypes + ScreenConstantMetrics，两者都是纯头，
 * 测试目标无需链接 Renderx；`OverlaySceneBuilder.cpp` 只负责把产出的顶点
 * 交给瞬态环。顶点结构与 P3O2C4 的 stride 断言仍留在 `.cpp`（那里才认识
 * `rxVertexStride`），保证布局一旦漂移立刻编译失败。
 *
 * 偏移量单位一律是**物理像素**（与 uViewport = backingSize 同一坐标系）。
 *
 * 本头与 `ScreenConstantMetrics.h` 一并从 UI2D 下沉到 RenderBridge：
 * 覆盖层构建器要在这里落地，而 UI2D 依赖 RenderBridge，留在 UI2D 就成环。
 * 命名空间保持 `Render::`，调用点无需改动。
 */
#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

#include "RenderBridge/ScreenConstantMetrics.h"

#include "Render/RenderTypes.h"

namespace Render
{
    /**
     * @brief P3O2C4 顶点：世界锚点 (x,y,z) + 像素偏移 (ox,oy) + RGBA
     *
     * 字段顺序即顶点缓冲的字节布局，不要重排；与 DLL 的 stride 断言在
     * `OverlaySceneBuilder.cpp` 里。
     */
    struct PinnedVertex
    {
        float x, y, z, ox, oy, r, g, b, a;
    };

    inline PinnedVertex pinnedVertex(const Vec2f& anchor, float ox, float oy, const Color& c)
    {
        return { anchor.x(), anchor.y(), 0.0f, ox, oy, c.r(), c.g(), c.b(), c.a() };
    }

    /// 一个标记的顶点数：填充三角形 6 + 边框 LineList 8
    constexpr size_t kPinnedMarkerFillVertexCount = 6;
    constexpr size_t kPinnedMarkerBorderVertexCount = 8;

    /**
     * @brief 追加一个屏幕定尺寸方块标记的顶点
     *
     * @param anchor    世界锚点；非有限值（NaN/Inf）直接跳过，不产出退化几何
     * @param offsetPx  锚点之上再叠加的像素偏移（如旋转手柄的「角点外侧 28px」）
     * @param sizePx    方块全宽（像素）；调用方负责夹下限，见 buildPinnedMarkers
     */
    inline void appendPinnedMarker(std::vector<PinnedVertex>& fillV,
        std::vector<PinnedVertex>& borderV, const Vec2f& anchor, const Vec2f& offsetPx, float sizePx,
        const Color& fill, const Color& border)
    {
        if (!std::isfinite(anchor.x()) || !std::isfinite(anchor.y()))
            return;
        const float half = sizePx * 0.5f;
        // 填充比边框内缩 1px，让描边不被填充盖掉；小尺寸时退化为按比例内缩，
        // 否则 sizePx 接近 2 时 half-1 会变成 0 甚至负数，填充直接消失。
        const float inner = std::max({ half - 1.0f, half * 0.72f, 2.0f });
        const float ox = offsetPx.x();
        const float oy = offsetPx.y();

        const float xi0 = ox - inner, xi1 = ox + inner, yi0 = oy - inner, yi1 = oy + inner;
        fillV.push_back(pinnedVertex(anchor, xi0, yi0, fill));
        fillV.push_back(pinnedVertex(anchor, xi1, yi0, fill));
        fillV.push_back(pinnedVertex(anchor, xi1, yi1, fill));
        fillV.push_back(pinnedVertex(anchor, xi0, yi0, fill));
        fillV.push_back(pinnedVertex(anchor, xi1, yi1, fill));
        fillV.push_back(pinnedVertex(anchor, xi0, yi1, fill));

        const float xb0 = ox - half, xb1 = ox + half, yb0 = oy - half, yb1 = oy + half;
        borderV.push_back(pinnedVertex(anchor, xb0, yb0, border));
        borderV.push_back(pinnedVertex(anchor, xb1, yb0, border));
        borderV.push_back(pinnedVertex(anchor, xb1, yb0, border));
        borderV.push_back(pinnedVertex(anchor, xb1, yb1, border));
        borderV.push_back(pinnedVertex(anchor, xb1, yb1, border));
        borderV.push_back(pinnedVertex(anchor, xb0, yb1, border));
        borderV.push_back(pinnedVertex(anchor, xb0, yb1, border));
        borderV.push_back(pinnedVertex(anchor, xb0, yb0, border));
    }

    /**
     * @brief 一组屏幕定尺寸方块标记的描述
     *
     * 选择手柄与点标记除了「读哪几个字段」之外逐行同构，用这个描述把两者归并成
     * 同一段消费代码。今后新增定尺寸装饰只需再加一条描述，不必再抄一遍
     * 「取 sizePx → 建 fillV/borderV → 循环 appendPinnedMarker → emit ×2」。
     */
    struct PinnedMarkerGroup
    {
        /// 世界锚点
        const std::vector<Vec2f>* anchors = nullptr;
        /// 锚点之上再叠加的像素偏移；为空或长度不足时按 (0,0) 处理
        const std::vector<Vec2f>* offsetsPx = nullptr;
        float sizePx = 0.0f;
        Color fill;
        Color border;
        /// 可选：每个标记单独的填充颜色数组。如果为空或长度不足则使用统一的 fill
        const std::vector<Color>* perMarkerFills = nullptr;
        /// 可选：每个标记单独的边框颜色数组。如果为空或长度不足则使用统一的 border
        const std::vector<Color>* perMarkerBorders = nullptr;
    };

    /**
     * @brief 把若干组定尺寸标记合成两批顶点（全部填充一批、全部边框一批）
     *
     * 合批而不是逐组各出两批：它们同管线同状态，唯一差别是顶点数据。
     * sizePx 在这里统一夹到 `ScreenMetrics::kMinMarkerSizePx`——业务侧字段未初始化
     * （0 或负数）时兜底，而不是画出零面积几何。
     */
    inline void buildPinnedMarkers(const PinnedMarkerGroup* groups, size_t groupCount,
        std::vector<PinnedVertex>& fillV, std::vector<PinnedVertex>& borderV)
    {
        for (size_t g = 0; g < groupCount; ++g)
        {
            const PinnedMarkerGroup& grp = groups[g];
            if (!grp.anchors || grp.anchors->empty())
                continue;
            const float sizePx = std::max(grp.sizePx, ScreenMetrics::kMinMarkerSizePx);
            for (size_t i = 0; i < grp.anchors->size(); ++i)
            {
                const Vec2f offsetPx = (grp.offsetsPx && i < grp.offsetsPx->size())
                    ? (*grp.offsetsPx)[i]
                    : Vec2f(0.0f, 0.0f);
                // 使用每标记颜色（如果提供），否则使用统一颜色
                const Color fill = (grp.perMarkerFills && i < grp.perMarkerFills->size())
                    ? (*grp.perMarkerFills)[i]
                    : grp.fill;
                const Color border = (grp.perMarkerBorders && i < grp.perMarkerBorders->size())
                    ? (*grp.perMarkerBorders)[i]
                    : grp.border;
                appendPinnedMarker(fillV, borderV, (*grp.anchors)[i], offsetPx, sizePx, fill,
                    border);
            }
        }
    }
}  // namespace Render
