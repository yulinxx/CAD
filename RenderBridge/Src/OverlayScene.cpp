/**
 * @file OverlayScene.cpp
 * @brief OverlayScene 实现（设计说明见头文件）
 *
 * 几何生成部分移植自 UI2D 的 `OverlaySceneBuilder.cpp`：顶点格式与空间、
 * 瞬态环分配、sortKey 层级、定尺寸标记与捕捉形状的生成规则都原样保留，
 * 改动只在「谁持有状态」—— 原先是一个扁平的 `OverlayState`，
 * 现在是各自独立的层。
 */
#include "RenderBridge/OverlayScene.h"

#include "RenderBridge/PinnedMarkerGeometry.h"
#include "RenderBridge/ScreenConstantMetrics.h"

#include "Log/SyLogger.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>

#ifndef M_PI
    #define M_PI 3.14159265358979323846
#endif

namespace RT = Render::RT;

namespace
{
    /// 覆盖层固定层级：在图元（layer=100）之上，且按半透明混合
    constexpr uint8_t kOverlayLayer = 200;
    constexpr uint8_t kOverlayTransparent = 1;

    /// P3C4 顶点（位置 + RGBA），用于 World 空间的普通覆盖层几何
    struct OVertex
    {
        float x, y, z, r, g, b, a;
    };
    static_assert(sizeof(OVertex) == RT::rxVertexStride(RT::VertexFormat::P3C4),
        "OVertex must match the DLL's P3C4 stride");

    inline OVertex vtx(float x, float y, float z, const Render::Color& c)
    {
        return { x, y, z, c.r(), c.g(), c.b(), c.a() };
    }

    /// P3O2C4 顶点（世界锚点 + 像素偏移 + RGBA）定义在 PinnedMarkerGeometry.h，
    /// 与 DLL stride 的一致性在这里断言 —— 那个纯几何头不认识 rxVertexStride。
    using PVertex = Render::PinnedVertex;
    static_assert(sizeof(PVertex) == RT::rxVertexStride(RT::VertexFormat::P3O2C4),
        "PVertex must match the DLL's P3O2C4 stride");

    /**
     * @brief 把一批顶点写进瞬态环并追加一条 DrawCommand
     *
     * 两个 emit 变体只在顶点格式与渲染空间上不同，其余（瞬态环分配、sortKey、
     * 字节偏移语义）完全一致，故用同一个模板生成，避免两份互相漂移的实现。
     */
    template <typename V>
    void emitAs(RT::SessionHandle session, std::vector<RT::DrawCommand>& out, const std::vector<V>& verts,
        RT::PrimitiveTopology topo, RT::VertexFormat format, RT::RenderSpace space, uint16_t& seq)
    {
        if (verts.empty())
            return;
        const uint64_t bytes = static_cast<uint64_t>(verts.size()) * sizeof(V);
        RT::TransientAlloc a{};
        RT::rxSessionAllocTransient(session, bytes, &a);
        // 环容量不足时 DLL 返回无效句柄；丢弃本批而不是画出错误几何。
        if (!RT::rxValid(a.buffer) || !a.cpuPtr)
            return;
        std::memcpy(a.cpuPtr, verts.data(), static_cast<size_t>(bytes));

        RT::DrawCommand c{};
        c.vertexBuffer = a.buffer;
        // vertexOffset 与 TransientAlloc::offset 同为**字节**偏移，原样填入，不除 stride。
        c.vertexOffset = a.offset;
        c.vertexCount = static_cast<uint32_t>(verts.size());
        c.instanceCount = 1;
        c.topology = topo;
        c.space = space;
        c.vertexFormat = format;
        c.indexType = RT::IndexType::None;
        // pipelineIndex 留 0：Runtime 按 (格式, 空间, 拓扑) 解析出的默认管线
        // 已开启 SrcAlpha/OneMinusSrcAlpha 混合，半透明覆盖层无需额外指定。
        c.sortKey = RT::rxMakeSortKey(kOverlayLayer, kOverlayTransparent, 0, seq++);
        out.push_back(c);
    }

    void emitWorld(RT::SessionHandle session, std::vector<RT::DrawCommand>& out,
        const std::vector<OVertex>& verts, RT::PrimitiveTopology topo, uint16_t& seq)
    {
        emitAs(session, out, verts, topo, RT::VertexFormat::P3C4, RT::RenderSpace::World, seq);
    }

    void emitPinned(RT::SessionHandle session, std::vector<RT::DrawCommand>& out,
        const std::vector<PVertex>& verts, RT::PrimitiveTopology topo, uint16_t& seq)
    {
        emitAs(session, out, verts, topo, RT::VertexFormat::P3O2C4, RT::RenderSpace::WorldPinned, seq);
    }

    void rectLineList(std::vector<OVertex>& v, const Render::BBox2d& b, const Render::Color& c)
    {
        const float minX = static_cast<float>(b.minPt.x());
        const float minY = static_cast<float>(b.minPt.y());
        const float maxX = static_cast<float>(b.maxPt.x());
        const float maxY = static_cast<float>(b.maxPt.y());
        v.push_back(vtx(minX, minY, 0, c));
        v.push_back(vtx(maxX, minY, 0, c));
        v.push_back(vtx(maxX, minY, 0, c));
        v.push_back(vtx(maxX, maxY, 0, c));
        v.push_back(vtx(maxX, maxY, 0, c));
        v.push_back(vtx(minX, maxY, 0, c));
        v.push_back(vtx(minX, maxY, 0, c));
        v.push_back(vtx(minX, minY, 0, c));
    }

    void filledRect(std::vector<OVertex>& v, const Render::BBox2d& b, const Render::Color& c)
    {
        const float minX = static_cast<float>(b.minPt.x());
        const float minY = static_cast<float>(b.minPt.y());
        const float maxX = static_cast<float>(b.maxPt.x());
        const float maxY = static_cast<float>(b.maxPt.y());
        v.push_back(vtx(minX, minY, 0, c));
        v.push_back(vtx(maxX, minY, 0, c));
        v.push_back(vtx(maxX, maxY, 0, c));
        v.push_back(vtx(minX, minY, 0, c));
        v.push_back(vtx(maxX, maxY, 0, c));
        v.push_back(vtx(minX, maxY, 0, c));
    }

    void filledQuad(std::vector<OVertex>& v, const Render::Vec2f q[4], const Render::Color& c)
    {
        v.push_back(vtx(q[0].x(), q[0].y(), 0, c));
        v.push_back(vtx(q[1].x(), q[1].y(), 0, c));
        v.push_back(vtx(q[2].x(), q[2].y(), 0, c));
        v.push_back(vtx(q[0].x(), q[0].y(), 0, c));
        v.push_back(vtx(q[2].x(), q[2].y(), 0, c));
        v.push_back(vtx(q[3].x(), q[3].y(), 0, c));
    }

    /// 把一组定尺寸标记合成两笔（全部填充一笔、全部边框一笔）。
    /// 顶点生成本身是纯几何，在 PinnedMarkerGeometry.h 里，可单测。
    void emitPinnedMarkerGroup(RT::SessionHandle session, std::vector<RT::DrawCommand>& out,
        const RenderBridge::OverlayMarkerGroup& group, uint16_t& seq)
    {
        if (group.empty())
            return;
        Render::PinnedMarkerGroup g{};
        g.anchors = &group.anchors;
        g.offsetsPx = group.offsetsPx.empty() ? nullptr : &group.offsetsPx;
        g.sizePx = group.sizePx;
        g.fill = group.fill;
        g.border = group.border;
        g.perMarkerFills = group.perMarkerFills.empty() ? nullptr : &group.perMarkerFills;
        g.perMarkerBorders = group.perMarkerBorders.empty() ? nullptr : &group.perMarkerBorders;

        std::vector<PVertex> fillV, borderV;
        Render::buildPinnedMarkers(&g, 1, fillV, borderV);
        emitPinned(session, out, fillV, RT::PrimitiveTopology::Triangles, seq);
        emitPinned(session, out, borderV, RT::PrimitiveTopology::Lines, seq);
    }

    /**
      * @brief 把一条带累积弧长的轮廓折线离散成「流水虚线」线段，**追加**到 outSegments
     *
     * 单路径版 + 追加语义是刻意的：调用方要把整个选中集的虚线合成**一条**
     * DrawCommand。此前是「每条路径调一次多路径版 tessellate + 一次 emit」，
     * 选中一万个图元就是每帧一万次瞬态环分配与一万条 DrawCommand。
     *
     * 复杂度 O(V + D)（V=折线顶点数，D=虚线段数）。此前 O(V × D)：每个 dash 都
     * 全量扫一遍 vs 找区间内顶点，还在 lambda 里现 new 一个 pts 向量。一条被细分到
     * 两千顶点、跑几百个 dash 的轮廓，单帧单条就是百万次迭代 —— 这是「选中几千个
     * 图元后整个软件卡死」的主因。这里改成沿弧长单调推进的游标：dash 起点只会
     * 向前走，游标绝不回退。
     */
    void appendSelectionDash(const Render::SelectionOutlinePath& path, float dashLength, float gapLength,
        float offset, std::vector<Render::Vec2f>& outSegments)
    {
        constexpr size_t kMaxSegmentsPerPath = 16384;

        if (dashLength <= 1e-6f)
            return;
        const float period = dashLength + (gapLength > 0.0f ? gapLength : 0.0f);
        if (period <= 1e-6f)
            return;

        const auto& vs = path.vertices;
        if (vs.size() < 2)
            return;
        const float total = vs.back().arcLength;
        if (total <= 1e-6f)
            return;

        float phase = std::fmod(offset, period);
        if (phase < 0.0f)
            phase += period;

        const size_t lastSeg = vs.size() - 2;  // 折线段 i 的两端是 vs[i]、vs[i+1]

        // 把游标推进到「包含弧长 s 的那一段」。只向前走，故整条路径累计 O(V)。
        auto advance = [&](size_t i, float s) -> size_t {
            while (i < lastSeg && vs[i + 1].arcLength <= s)
            {
                ++i;
            }
            return i;
        };
        auto posAt = [&](size_t i, float s) -> Render::Vec2f {
            const float a0 = vs[i].arcLength;
            const float span = vs[i + 1].arcLength - a0;
            const float t = span > 1e-6f ? std::min(std::max((s - a0) / span, 0.0f), 1.0f) : 0.0f;
            return Render::Vec2f(vs[i].position.x() + (vs[i + 1].position.x() - vs[i].position.x()) * t,
                vs[i].position.y() + (vs[i + 1].position.y() - vs[i].position.y()) * t);
        };

        const size_t startSeg = outSegments.size();
        size_t cursor = 0;
        float s = phase;
        bool drawing = true;
        while (s < total)
        {
            const float e = std::min(s + (drawing ? dashLength : gapLength), total);
            if (drawing)
            {
                if (outSegments.size() - startSeg >= kMaxSegmentsPerPath)
                {
                    break;
                }

                cursor = advance(cursor, s);
                Render::Vec2f prev = posAt(cursor, s);

                // 夹在 (s, e) 之间的原始顶点必须原样穿进去，否则虚线会把折线拐角抹平
                size_t k = cursor + 1;
                while (k < vs.size() && vs[k].arcLength < e)
                {
                    if (vs[k].arcLength > s && outSegments.size() - startSeg < kMaxSegmentsPerPath)
                    {
                        outSegments.push_back(prev);
                        outSegments.push_back(vs[k].position);
                        prev = vs[k].position;
                    }
                    ++k;
                }

                const size_t endSeg = k > 0 ? std::min(k - 1, lastSeg) : 0;
                if (outSegments.size() - startSeg < kMaxSegmentsPerPath)
                {
                    outSegments.push_back(prev);
                    outSegments.push_back(posAt(endSeg, e));
                }
                cursor = endSeg;
            }
            drawing = !drawing;
            s = e;
        }
    }

    // ==================== 捕捉标记形状 ====================
    //
    // 形状与尺寸都以 kSnapIndicatorRadiusPx 为半径基准按固定系数缩放，
    // 每个捕捉类型对应唯一形状；颜色由调用方决定。全部走 WorldPinned，
    // 偏移量单位是物理像素（乘过 DPR 后传入）。

    void pinnedMarkerPoint(std::vector<PVertex>& v, const Render::Vec2f& p,
        float radiusPx, const Render::Color& c)
    {
        if (!std::isfinite(p.x()) || !std::isfinite(p.y()))
            return;
        constexpr int kSegments = 8;
        for (int i = 0; i < kSegments; ++i)
        {
            const float a0 = 2.0f * 3.14159265f * i / kSegments;
            const float a1 = 2.0f * 3.14159265f * (i + 1) / kSegments;
            v.push_back(Render::pinnedVertex(p, radiusPx * std::cos(a0), radiusPx * std::sin(a0), c));
            v.push_back(Render::pinnedVertex(p, radiusPx * std::cos(a1), radiusPx * std::sin(a1), c));
        }
    }

    void emitSquareMarker(std::vector<PVertex>& v, const Render::Vec2f& p, const Render::Color& c, float scale)
    {
        const float s = Render::ScreenMetrics::kSnapIndicatorRadiusPx * 0.68f * scale;
        v.push_back(Render::pinnedVertex(p, -s, -s, c));
        v.push_back(Render::pinnedVertex(p, s, -s, c));
        v.push_back(Render::pinnedVertex(p, s, s, c));
        v.push_back(Render::pinnedVertex(p, -s, s, c));
        v.push_back(Render::pinnedVertex(p, -s, -s, c));
    }

    void emitDiamondMarker(std::vector<PVertex>& v, const Render::Vec2f& p, const Render::Color& c, float scale)
    {
        const float s = Render::ScreenMetrics::kSnapIndicatorRadiusPx * 0.78f * scale;
        v.push_back(Render::pinnedVertex(p, 0, -s, c));
        v.push_back(Render::pinnedVertex(p, s, 0, c));
        v.push_back(Render::pinnedVertex(p, 0, s, c));
        v.push_back(Render::pinnedVertex(p, -s, 0, c));
        v.push_back(Render::pinnedVertex(p, 0, -s, c));
    }

    void emitCrossMarker(std::vector<PVertex>& v, const Render::Vec2f& p, const Render::Color& c, float scale)
    {
        const float arm = Render::ScreenMetrics::kSnapIndicatorRadiusPx * 0.62f * scale;
        v.push_back(Render::pinnedVertex(p, -arm, 0, c));
        v.push_back(Render::pinnedVertex(p, arm, 0, c));
        v.push_back(Render::pinnedVertex(p, 0, -arm, c));
        v.push_back(Render::pinnedVertex(p, 0, arm, c));
    }

    void emitXMarker(std::vector<PVertex>& v, const Render::Vec2f& p, const Render::Color& c, float scale)
    {
        const float arm = Render::ScreenMetrics::kSnapIndicatorRadiusPx * 0.62f * scale;
        v.push_back(Render::pinnedVertex(p, -arm, -arm, c));
        v.push_back(Render::pinnedVertex(p, arm, arm, c));
        v.push_back(Render::pinnedVertex(p, arm, -arm, c));
        v.push_back(Render::pinnedVertex(p, -arm, arm, c));
    }

    void emitTriangleMarker(std::vector<PVertex>& v, const Render::Vec2f& p, const Render::Color& c, float scale)
    {
        const float r = Render::ScreenMetrics::kSnapIndicatorRadiusPx * 0.70f * scale;
        const float h = r * 1.15f;
        v.push_back(Render::pinnedVertex(p, 0, -h, c));
        v.push_back(Render::pinnedVertex(p, r, h * 0.5f, c));
        v.push_back(Render::pinnedVertex(p, -r, h * 0.5f, c));
        v.push_back(Render::pinnedVertex(p, 0, -h, c));
    }

    void emitArcMarker(std::vector<PVertex>& v, const Render::Vec2f& p, const Render::Color& c, float scale)
    {
        const float r = Render::ScreenMetrics::kSnapIndicatorRadiusPx * 0.66f * scale;
        constexpr int kSegments = 12;
        constexpr float kStartAngle = 0.0f;
        const float endAngle = static_cast<float>(M_PI) * 0.75f;
        for (int i = 0; i < kSegments; ++i)
        {
            const float a0 = kStartAngle + (endAngle - kStartAngle) * i / kSegments;
            const float a1 = kStartAngle + (endAngle - kStartAngle) * (i + 1) / kSegments;
            v.push_back(Render::pinnedVertex(p, r * std::cos(a0), r * std::sin(a0), c));
            v.push_back(Render::pinnedVertex(p, r * std::cos(a1), r * std::sin(a1), c));
        }
    }

    void emitRightAngleMarker(std::vector<PVertex>& v, const Render::Vec2f& p, const Render::Color& c, float scale)
    {
        const float s = Render::ScreenMetrics::kSnapIndicatorRadiusPx * 0.55f * scale;
        v.push_back(Render::pinnedVertex(p, -s, 0, c));
        v.push_back(Render::pinnedVertex(p, -s, s, c));
        v.push_back(Render::pinnedVertex(p, -s, s, c));
        v.push_back(Render::pinnedVertex(p, 0, s, c));
    }

    void emitDotMarker(std::vector<PVertex>& v, const Render::Vec2f& p, const Render::Color& c, float scale)
    {
        pinnedMarkerPoint(v, p, Render::ScreenMetrics::kSnapIndicatorRadiusPx * 0.22f * scale, c);
    }

    void emitStarMarker(std::vector<PVertex>& v, const Render::Vec2f& p, const Render::Color& c, float scale)
    {
        const float r = Render::ScreenMetrics::kSnapIndicatorRadiusPx * 0.72f * scale;
        constexpr int kPoints = 5;
        for (int i = 0; i < kPoints * 2; ++i)
        {
            const float angle = static_cast<float>(M_PI) * 2 * i / (kPoints * 2) - static_cast<float>(M_PI) / 2;
            const float rr = (i % 2 == 0) ? r : r * 0.4f;
            v.push_back(Render::pinnedVertex(p, rr * std::cos(angle), rr * std::sin(angle), c));
        }
    }

    void emitHexagonMarker(std::vector<PVertex>& v, const Render::Vec2f& p, const Render::Color& c, float scale)
    {
        const float r = Render::ScreenMetrics::kSnapIndicatorRadiusPx * 0.72f * scale;
        constexpr int kSegments = 6;
        for (int i = 0; i < kSegments; ++i)
        {
            const float a0 = 2.0f * 3.14159265f * i / kSegments - 3.14159265f * 0.5f;
            const float a1 = 2.0f * 3.14159265f * (i + 1) / kSegments - 3.14159265f * 0.5f;
            v.push_back(Render::pinnedVertex(p, r * std::cos(a0), r * std::sin(a0), c));
            v.push_back(Render::pinnedVertex(p, r * std::cos(a1), r * std::sin(a1), c));
        }
    }

    void emitHourglassMarker(std::vector<PVertex>& v, const Render::Vec2f& p, const Render::Color& c, float scale)
    {
        const float r = Render::ScreenMetrics::kSnapIndicatorRadiusPx * 0.72f * scale;
        const Render::Vec2f a(-r, -r), b(r, -r), cc(-r, r), d(r, r);
        v.push_back(Render::pinnedVertex(p, a.x(), a.y(), c));
        v.push_back(Render::pinnedVertex(p, b.x(), b.y(), c));
        v.push_back(Render::pinnedVertex(p, b.x(), b.y(), c));
        v.push_back(Render::pinnedVertex(p, cc.x(), cc.y(), c));
        v.push_back(Render::pinnedVertex(p, cc.x(), cc.y(), c));
        v.push_back(Render::pinnedVertex(p, d.x(), d.y(), c));
        v.push_back(Render::pinnedVertex(p, d.x(), d.y(), c));
        v.push_back(Render::pinnedVertex(p, a.x(), a.y(), c));
    }

    void emitSnapMarker(std::vector<PVertex>& v, const Render::Vec2f& p,
        RenderBridge::SnapMarkerShape shape, const Render::Color& color, float scale)
    {
        if (!std::isfinite(p.x()) || !std::isfinite(p.y()))
            return;
        switch (shape)
        {
        case RenderBridge::SnapMarkerShape::Square:     emitSquareMarker(v, p, color, scale); break;
        case RenderBridge::SnapMarkerShape::Diamond:    emitDiamondMarker(v, p, color, scale); break;
        case RenderBridge::SnapMarkerShape::Cross:      emitCrossMarker(v, p, color, scale); break;
        case RenderBridge::SnapMarkerShape::X:          emitXMarker(v, p, color, scale); break;
        case RenderBridge::SnapMarkerShape::Triangle:   emitTriangleMarker(v, p, color, scale); break;
        case RenderBridge::SnapMarkerShape::Arc:        emitArcMarker(v, p, color, scale); break;
        case RenderBridge::SnapMarkerShape::RightAngle: emitRightAngleMarker(v, p, color, scale); break;
        case RenderBridge::SnapMarkerShape::Dot:        emitDotMarker(v, p, color, scale); break;
        case RenderBridge::SnapMarkerShape::Star:       emitStarMarker(v, p, color, scale); break;
        case RenderBridge::SnapMarkerShape::Hexagon:    emitHexagonMarker(v, p, color, scale); break;
        case RenderBridge::SnapMarkerShape::Hourglass:  emitHourglassMarker(v, p, color, scale); break;
        case RenderBridge::SnapMarkerShape::Circle:
        default:
        {
            constexpr int kSeg = 20;
            const float kR = Render::ScreenMetrics::kSnapIndicatorRadiusPx * 0.66f * scale;
            for (int i = 0; i < kSeg; ++i)
            {
                const float a0 = 2.0f * 3.14159265f * i / kSeg;
                const float a1 = 2.0f * 3.14159265f * (i + 1) / kSeg;
                v.push_back(Render::pinnedVertex(p, kR * std::cos(a0), kR * std::sin(a0), color));
                v.push_back(Render::pinnedVertex(p, kR * std::cos(a1), kR * std::sin(a1), color));
            }
            break;
        }
        }
    }
}  // namespace

namespace RenderBridge
{
    // ==================== 逐层设置 ====================

    void OverlayScene::setSelectionBox(const Render::BBox2d& box, const Render::Color& border)
    {
        // bbox 退化（无选中）是「清掉选择框」的意思，不需要额外的 has 标志
        m_selectionBox.valid = box.isValid();
        if (m_selectionBox.valid)
        {
            m_selectionBox.box = box;
            m_selectionBox.border = border;
        }
    }

    void OverlayScene::setSelectionHighlight(const std::vector<Render::Vec2f>& quadCorners,
        const Render::Color& fill, const Render::Color& border)
    {
        if (quadCorners.empty())
        {
            m_highlight.quadCorners.clear();
            return;
        }
        if (quadCorners.size() % 4 != 0)
        {
            // 每块四边形 4 个顶点，长度不是 4 的倍数说明调用方拼错了，
            // 直接拒绝而不是画出一半的几何
            SY_ERRORF("[OverlayScene] setSelectionHighlight: quadCorners size %zu is not a multiple of 4",
                quadCorners.size());
            return;
        }
        m_highlight.quadCorners = quadCorners;
        m_highlight.fill = fill;
        m_highlight.border = border;
    }

    void OverlayScene::setSelectionRect(const Render::BBox2d& rect, const Render::Color& fill,
        const Render::Color& border)
    {
        m_selectionRect.valid = rect.isValid();
        if (m_selectionRect.valid)
        {
            m_selectionRect.rect = rect;
            m_selectionRect.fill = fill;
            m_selectionRect.border = border;
        }
    }

    void OverlayScene::setSelectionOutlines(std::vector<Render::SelectionOutlinePath> paths)
    {
        m_outlines.paths = std::move(paths);
    }

    void OverlayScene::setSelectionDashStyle(const Render::SelectionDashStyle& style)
    {
        // 样式是设置项不是这一帧的内容，因此 clearLayer 不会清掉它
        m_outlines.dash = style;
    }

    void OverlayScene::setSelectionHandles(OverlayMarkerGroup group)
    {
        m_selectionHandles.group = std::move(group);
    }

    void OverlayScene::setPointMarkers(OverlayMarkerGroup group)
    {
        m_pointMarkers.group = std::move(group);
    }

    void OverlayScene::setSnapIndicator(const Render::Vec2f& worldPos, bool visible,
        SnapMarkerShape shape, const Render::Color& color)
    {
        m_snap.visible = visible;
        m_snap.worldPos = worldPos;
        m_snap.shape = shape;
        m_snap.color = color;
    }

    void OverlayScene::setToolPreview(const std::vector<Render::Vec2f>& points)
    {
        m_toolPreview.points = points;
    }

    void OverlayScene::setToolPreviewColor(const Render::Color& color)
    {
        m_toolPreview.color = color;
    }

    void OverlayScene::setControlLines(const std::vector<Render::Vec2f>& points)
    {
        m_controlLines.points = points;
    }

    void OverlayScene::setControlLinesColor(const Render::Color& color)
    {
        m_controlLines.color = color;
    }

    // ==================== 清除 ====================

    void OverlayScene::clearLayer(OverlayLayerId id)
    {
        switch (id)
        {
        case OverlayLayerId::SelectionBox:
            m_selectionBox.valid = false;
            break;
        case OverlayLayerId::SelectionHighlight:
            m_highlight.quadCorners.clear();
            break;
        case OverlayLayerId::SelectionRect:
            m_selectionRect.valid = false;
            break;
        case OverlayLayerId::SelectionOutlines:
            // 只清几何：虚线样式是设置项，清了会让下一次设置轮廓丢失样式
            m_outlines.paths.clear();
            break;
        case OverlayLayerId::SelectionHandles:
            m_selectionHandles.group.anchors.clear();
            break;
        case OverlayLayerId::PointMarkers:
            m_pointMarkers.group.anchors.clear();
            break;
        case OverlayLayerId::SnapIndicator:
            m_snap.visible = false;
            break;
        case OverlayLayerId::ToolPreview:
            m_toolPreview.points.clear();
            break;
        case OverlayLayerId::ControlLines:
            m_controlLines.points.clear();
            break;
        case OverlayLayerId::Count:
            break;
        }
    }

    void OverlayScene::clear()
    {
        for (uint8_t i = 0; i < static_cast<uint8_t>(OverlayLayerId::Count); ++i)
        {
            clearLayer(static_cast<OverlayLayerId>(i));
        }
    }

    // ==================== 帧参数与查询 ====================

    void OverlayScene::setFrameParams(const OverlayFrameParams& params)
    {
        // 比例必须为正：0 或负数说明视口还没算出来，沿用上一帧比算出扭曲几何好
        if (params.pixelToWorld > 0.0f)
        {
            m_frameParams.pixelToWorld = params.pixelToWorld;
        }
        m_frameParams.dashOffsetPx = params.dashOffsetPx;
    }

    void OverlayScene::setDevicePixelRatio(float devicePixelRatio)
    {
        if (devicePixelRatio > 0.0f)
        {
            m_devicePixelRatio = devicePixelRatio;
        }
    }

    bool OverlayScene::hasAnimatedSelectionOutlines() const
    {
        const Render::SelectionDashStyle& ds = m_outlines.dash;
        return !m_outlines.paths.empty() && ds.enabled && ds.animated && ds.speedPxPerSec > 0.0f;
    }

    bool OverlayScene::hasLayer(OverlayLayerId id) const
    {
        switch (id)
        {
        case OverlayLayerId::SelectionBox:      return m_selectionBox.valid;
        case OverlayLayerId::SelectionHighlight: return !m_highlight.quadCorners.empty();
        case OverlayLayerId::SelectionRect:     return m_selectionRect.valid;
        case OverlayLayerId::SelectionOutlines: return !m_outlines.paths.empty();
        case OverlayLayerId::SelectionHandles:  return !m_selectionHandles.group.empty();
        case OverlayLayerId::PointMarkers:      return !m_pointMarkers.group.empty();
        case OverlayLayerId::SnapIndicator:     return m_snap.visible;
        case OverlayLayerId::ToolPreview:       return !m_toolPreview.points.empty();
        case OverlayLayerId::ControlLines:      return !m_controlLines.points.empty();
        case OverlayLayerId::Count:             break;
        }
        return false;
    }

    // ==================== 提交 ====================

    void OverlayScene::submit(RT::SessionHandle session, std::vector<RT::DrawCommand>& out) const
    {
        if (!RT::rxValid(session))
        {
            return;
        }
        // 序号沿枚举顺序递增：提交顺序即叠放顺序，不再依赖调用顺序
        uint16_t seq = 0;

        // 选中集包围盒：只描边不填充。填充会盖住被选中的图元本身
        if (m_selectionBox.valid)
        {
            std::vector<OVertex> v;
            rectLineList(v, m_selectionBox.box, m_selectionBox.border);
            emitWorld(session, out, v, RT::PrimitiveTopology::Lines, seq);
        }

        // 文字选区高亮：N 块四边形，填充与边框各一批。
        // 逐块 emit 会让跨行选区产生 2×行数 条命令，而格式/空间/拓扑完全一致
        if (m_highlight.quadCorners.size() >= 4)
        {
            const size_t quadCount = m_highlight.quadCorners.size() / 4;
            if (m_highlight.fill.a() > 1e-4f)
            {
                std::vector<OVertex> v;
                v.reserve(quadCount * 6);
                for (size_t q = 0; q < quadCount; ++q)
                {
                    filledQuad(v, &m_highlight.quadCorners[q * 4], m_highlight.fill);
                }
                emitWorld(session, out, v, RT::PrimitiveTopology::Triangles, seq);
            }
            if (m_highlight.border.a() > 1e-4f)
            {
                std::vector<OVertex> v;
                v.reserve(quadCount * 8);
                for (size_t q = 0; q < quadCount; ++q)
                {
                    const Render::Vec2f* c = &m_highlight.quadCorners[q * 4];
                    for (int i = 0; i < 4; ++i)
                    {
                        const Render::Vec2f& a = c[i];
                        const Render::Vec2f& b = c[(i + 1) % 4];
                        v.push_back(vtx(a.x(), a.y(), 0, m_highlight.border));
                        v.push_back(vtx(b.x(), b.y(), 0, m_highlight.border));
                    }
                }
                emitWorld(session, out, v, RT::PrimitiveTopology::Lines, seq);
            }
        }

        // 框选预览矩形
        if (m_selectionRect.valid)
        {
            if (m_selectionRect.fill.a() > 1e-4f)
            {
                std::vector<OVertex> v;
                filledRect(v, m_selectionRect.rect, m_selectionRect.fill);
                emitWorld(session, out, v, RT::PrimitiveTopology::Triangles, seq);
            }
            std::vector<OVertex> v;
            rectLineList(v, m_selectionRect.rect, m_selectionRect.border);
            emitWorld(session, out, v, RT::PrimitiveTopology::Lines, seq);
        }

        // 流水虚线轮廓：整个选中集合成**一条**命令（颜色是逐顶点的，路径间不必分批）
        if (!m_outlines.paths.empty())
        {
            const Render::SelectionDashStyle& ds = m_outlines.dash;
            const float p2w = m_frameParams.pixelToWorld > 0.0f ? m_frameParams.pixelToWorld : 1.0f;
            // 样式是像素基准，顶点是世界坐标，因此三个长度都要乘 pixelToWorld。
            // 少了这一步：虚线节距随缩放变化，且世界尺度小于 dashLengthPx 的图元
            // 整条轮廓短于一个 dash，退化成一整条实线。
            const float dash = ds.enabled ? ds.dashLengthPx * p2w : 1e9f;  // 禁用虚线 → 实线
            const float gap = ds.enabled ? ds.gapLengthPx * p2w : 0.0f;
            const float offset = m_frameParams.dashOffsetPx * p2w;

            // segs 复用同一块缓冲（clear 保留容量），避免每条路径一次堆分配
            std::vector<OVertex> v;
            std::vector<Render::Vec2f> segs;
            for (const auto& path : m_outlines.paths)
            {
                segs.clear();
                appendSelectionDash(path, dash, gap, offset, segs);
                if (segs.empty())
                    continue;
                v.reserve(v.size() + segs.size());
                for (const auto& s : segs)
                    v.push_back(vtx(s.x(), s.y(), 0, path.color));
            }
            emitWorld(session, out, v, RT::PrimitiveTopology::Lines, seq);
        }

        // 屏幕定尺寸方块标记：手柄与点标记各一层，各出填充 + 边框两笔
        emitPinnedMarkerGroup(session, out, m_selectionHandles.group, seq);
        emitPinnedMarkerGroup(session, out, m_pointMarkers.group, seq);

        // 捕捉指示器：只画彩色形状。捕捉命中时鼠标指针会被隐藏（见视图层），
        // 标记无需再加圆盘底。尺寸以逻辑像素声明，乘 DPR 换算成物理像素
        if (m_snap.visible)
        {
            std::vector<PVertex> v;
            const float dpr = m_devicePixelRatio > 0.0f ? m_devicePixelRatio : 1.0f;
            emitSnapMarker(v, m_snap.worldPos, m_snap.shape, m_snap.color, dpr);
            emitPinned(session, out, v, RT::PrimitiveTopology::Lines, seq);
        }

        // 预览折线：LineStrip 语义展开为相邻点对，以便与其它线合并成一批
        if (m_toolPreview.points.size() >= 2)
        {
            std::vector<OVertex> v;
            for (size_t i = 0; i + 1 < m_toolPreview.points.size(); ++i)
            {
                v.push_back(vtx(m_toolPreview.points[i].x(), m_toolPreview.points[i].y(), 0, m_toolPreview.color));
                v.push_back(vtx(m_toolPreview.points[i + 1].x(), m_toolPreview.points[i + 1].y(), 0, m_toolPreview.color));
            }
            emitWorld(session, out, v, RT::PrimitiveTopology::Lines, seq);
        }

        // 辅助线：调用方已按 LineList 语义成对排好
        if (!m_controlLines.points.empty())
        {
            std::vector<OVertex> v;
            v.reserve(m_controlLines.points.size());
            for (const auto& p : m_controlLines.points)
            {
                v.push_back(vtx(p.x(), p.y(), 0, m_controlLines.color));
            }
            emitWorld(session, out, v, RT::PrimitiveTopology::Lines, seq);
        }
    }
}  // namespace RenderBridge
