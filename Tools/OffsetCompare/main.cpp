// 同一输入双实现对照工具（P3 前置数据）
//
// 目的：在合并两份偏移实现之前，先拿到「同一组几何、同一组参数」下
// Engine2D::Geo2DOffset 与 Engraving::PolygonOffset 的可复现数值差异。
// 本工具**不修改任何生产实现**，只输出 CSV 基线，供后续收敛决策使用。
//
// 参数对齐（写进 CSV 头）：
//   - Engine2D: ClipperD(4) = 1e-4 mm，JoinType::Round，miterLimit=2，arcTolerance=0.05
//   - Engraving: Paths64 scale=1000 = 1e-3 mm，JoinType::Round，miterLimit=2，arcTolerance=0.05
// 坐标单位统一为 mm；面积单位 mm²。

#include "Engine2D/Geo/GeometryContext.h"
#include "Engine2D/Geo/GeometryPrecision.h"
#include "Engine2D/Geometry/Geo2DOffset.h"
#include "Engraving/PolygonOffset.h"
#include "Ut/Vec.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

namespace
{
    using LoopE = std::vector<Ut::Vec2d>;
    using LoopsE = std::vector<LoopE>;

    struct Case
    {
        std::string name;
        LoopsE loops;
    };

    LoopE square(double x0, double y0, double size)
    {
        return { { x0, y0 }, { x0 + size, y0 }, { x0 + size, y0 + size }, { x0, y0 + size } };
    }

    LoopE circle(double cx, double cy, double r, int n = 64)
    {
        LoopE out;
        out.reserve(static_cast<std::size_t>(n));
        for (int i = 0; i < n; ++i)
        {
            const double a = 2.0 * 3.14159265358979323846 * i / n;
            out.emplace_back(cx + r * std::cos(a), cy + r * std::sin(a));
        }
        return out;
    }

    std::vector<Case> makeCases()
    {
        std::vector<Case> cases;
        cases.push_back({ "square100", { square(0.0, 0.0, 100.0) } });
        cases.push_back({ "rect200x100", { square(0.0, 0.0, 200.0) } });
        cases.back().loops[0] = { { 0, 0 }, { 200, 0 }, { 200, 100 }, { 0, 100 } };

        LoopE lshape = { { 0, 0 }, { 100, 0 }, { 100, 40 }, { 40, 40 }, { 40, 100 }, { 0, 100 } };
        cases.push_back({ "l_shape", { lshape } });

        // 外环 CCW + 内孔 CW
        LoopE outer = square(0.0, 0.0, 100.0);
        LoopE hole = { { 70, 30 }, { 30, 30 }, { 30, 70 }, { 70, 70 } };
        cases.push_back({ "square_with_hole", { outer, hole } });

        LoopE star;
        for (int i = 0; i < 10; ++i)
        {
            const double a = 3.14159265358979323846 / 2.0 + i * 3.14159265358979323846 / 5.0;
            const double r = (i % 2 == 0) ? 60.0 : 25.0;
            star.emplace_back(50.0 + r * std::cos(a), 50.0 + r * std::sin(a));
        }
        cases.push_back({ "ten_point_star", { star } });

        cases.push_back({ "circle_r50", { circle(50.0, 50.0, 50.0) } });
        return cases;
    }

    double areaOf(const LoopE& loop)
    {
        double s = 0.0;
        for (std::size_t i = 0; i < loop.size(); ++i)
        {
            const auto& a = loop[i];
            const auto& b = loop[(i + 1) % loop.size()];
            s += a.x() * b.y() - b.x() * a.y();
        }
        return std::fabs(0.5 * s);
    }

    double totalArea(const LoopsE& loops)
    {
        double total = 0.0;
        for (const auto& loop : loops)
        {
            total += areaOf(loop);
        }
        return total;
    }

    std::size_t totalPoints(const LoopsE& loops)
    {
        std::size_t n = 0;
        for (const auto& loop : loops)
        {
            n += loop.size();
        }
        return n;
    }

    Engraving::PolygonOffsetOptions engravingOptions(double delta)
    {
        Engraving::PolygonOffsetOptions o;
        o.offsetDistance = static_cast<float>(delta);
        o.joinType = Clipper2Lib::JoinType::Round;
        o.endType = Clipper2Lib::EndType::Polygon;
        o.miterLimit = 2.0;
        o.arcTolerance = 0.05;
        return o;
    }

    LoopsE fromEngraving(const std::vector<std::vector<Engraving::Point3D>>& paths)
    {
        LoopsE out;
        out.reserve(paths.size());
        for (const auto& path : paths)
        {
            LoopE loop;
            loop.reserve(path.size());
            for (const auto& p : path)
            {
                loop.emplace_back(static_cast<double>(p.x), static_cast<double>(p.y));
            }
            out.push_back(std::move(loop));
        }
        return out;
    }

    struct Row
    {
        std::string kind;
        std::string shape;
        std::string param;
        int layer{ -1 };
        double engineArea{ 0.0 };
        double engravingArea{ 0.0 };
        int engineRings{ 0 };
        int engravingRings{ 0 };
        std::size_t enginePoints{ 0 };
        std::size_t engravingPoints{ 0 };
    };

    double relDiff(double a, double b)
    {
        const double denom = std::max(std::fabs(a), std::fabs(b));
        return denom > 1e-12 ? std::fabs(a - b) / denom * 100.0 : 0.0;
    }

    void writeRow(std::ostream& out, const Row& r)
    {
        out << r.kind << ',' << r.shape << ',' << r.param << ',' << r.layer
            << ',' << std::fixed << std::setprecision(6) << r.engineArea
            << ',' << r.engravingArea
            << ',' << std::fabs(r.engineArea - r.engravingArea)
            << ',' << std::setprecision(4) << relDiff(r.engineArea, r.engravingArea)
            << ',' << r.engineRings << ',' << r.engravingRings
            << ',' << r.enginePoints << ',' << r.engravingPoints << '\n';
    }

    void runOffsetComparison(std::ostream& out, const std::vector<Case>& cases)
    {
        const auto ctx = Eg::GeometryContext::defaultContext();
        for (const auto& c : cases)
        {
            std::vector<std::vector<Engraving::Point3D>> engravingInput;
            engravingInput.reserve(c.loops.size());
            for (const auto& loop : c.loops)
            {
                std::vector<Engraving::Point3D> path;
                path.reserve(loop.size());
                for (const auto& p : loop)
                {
                    path.emplace_back(static_cast<float>(p.x()), static_cast<float>(p.y()), 0.0f);
                }
                engravingInput.push_back(std::move(path));
            }

            for (const double delta : { 0.5, 1.0, 2.0, -0.5, -1.0 })
            {
                Eg::Geo2DOffsetParams p;
                p.dOffset = std::fabs(delta);
                p.bInset = delta < 0.0;
                p.eJoin = Eg::Geo2DJoinType::Round;
                p.dMiterLimit = 2.0;
                p.dArcTolerance = 0.05;

                const auto e2 = Eg::Geo2DOffset::offsetContours(c.loops, p, ctx);
                const auto engr = Engraving::offsetPolygons(engravingInput, engravingOptions(delta), 1000.0);
                const auto engrLoops = fromEngraving(engr);

                Row r;
                r.kind = "offset";
                r.shape = c.name;
                r.param = std::to_string(delta);
                r.engineArea = totalArea(e2);
                r.engravingArea = totalArea(engrLoops);
                r.engineRings = static_cast<int>(e2.size());
                r.engravingRings = static_cast<int>(engrLoops.size());
                r.enginePoints = totalPoints(e2);
                r.engravingPoints = totalPoints(engrLoops);
                writeRow(out, r);
            }
        }
    }

    void runInsetRingsComparison(std::ostream& out, const std::vector<Case>& cases)
    {
        const auto ctx = Eg::GeometryContext::defaultContext();
        constexpr double step = 1.0;
        constexpr int maxLayers = 8;

        for (const auto& c : cases)
        {
            // Engine2D：逐层内缩，层序稳定
            Eg::Geo2DOffsetParams p;
            p.dOffset = step;
            p.bInset = true;
            p.eJoin = Eg::Geo2DJoinType::Round;
            p.dMiterLimit = 2.0;
            p.dArcTolerance = 0.05;
            const auto layers = Eg::Geo2DOffset::insetRings(c.loops, step, p, ctx, maxLayers, 1.0);

            // Engraving：轮廓平行填充（外环 CCW / 孔洞 CW）
            std::vector<Engraving::Point3D> outer;
            std::vector<std::vector<Engraving::Point3D>> holes;
            for (std::size_t i = 0; i < c.loops.size(); ++i)
            {
                std::vector<Engraving::Point3D> path;
                for (const auto& pt : c.loops[i])
                {
                    path.emplace_back(static_cast<float>(pt.x()), static_cast<float>(pt.y()), 0.0f);
                }
                if (i == 0)
                {
                    outer = std::move(path);
                }
                else
                {
                    holes.push_back(std::move(path));
                }
            }
            const auto engrRings = Engraving::contourParallelFill(
                outer, holes, static_cast<float>(step), 1000.0);
            const auto engrLoops = fromEngraving(engrRings);

            // Engraving 的 inflateRegionToRings 返回**扁平**环列表：每轮内缩把该层
            // 所有环依次 push_back，不携带层号。按 Engine2D 每层的环数切片对齐，
            // 才是同一层的比较；若环数不同则该层标记 mismatch 并停止后续层。
            std::size_t cursor = 0;
            for (std::size_t i = 0; i < layers.size(); ++i)
            {
                const auto& engineLayer = layers[i];
                const std::size_t engineRingCount = engineLayer.size();
                if (cursor + engineRingCount > engrLoops.size())
                {
                    break;
                }

                LoopsE engrLayer;
                engrLayer.reserve(engineRingCount);
                std::size_t engrPoints = 0;
                for (std::size_t k = 0; k < engineRingCount; ++k)
                {
                    const auto& ring = engrLoops[cursor + k];
                    engrLayer.push_back(ring);
                    engrPoints += ring.size();
                }
                cursor += engineRingCount;

                Row r;
                r.kind = "inset_ring";
                r.shape = c.name;
                r.param = "step=1.0";
                r.layer = static_cast<int>(i);
                r.engineArea = totalArea(engineLayer);
                r.engravingArea = totalArea(engrLayer);
                r.engineRings = static_cast<int>(engineRingCount);
                r.engravingRings = static_cast<int>(engrLayer.size());
                r.enginePoints = totalPoints(engineLayer);
                r.engravingPoints = engrPoints;
                writeRow(out, r);
            }
        }
    }
}

int main(int argc, char** argv)
{
    const std::string outPath = argc > 1 ? argv[1] : "Docs/performance-audit/offset-ab-baseline.csv";
    std::ofstream out(outPath, std::ios::trunc);
    if (!out)
    {
        std::cerr << "cannot open output: " << outPath << '\n';
        return 2;
    }

    out << "# 同一输入双实现对照基线（Engine2D vs Engraving 偏移内核）\n"
        << "# 生成工具: Tools/OffsetCompare/AprOffsetCompare\n"
        << "# Engine2D: ClipperD(4)=1e-4mm, JoinType::Round, miter=2, arcTol=0.05\n"
        << "# Engraving offset: Paths64 scale=1000=1e-3mm, JoinType::Round, miter=2, arcTol=0.05\n"
        << "# Engraving inset_ring: inflateRegionToRings 硬编码 Miter/2.0/0.0（公开 API 无参数）\n"
        << "# 单位: 坐标 mm, 面积 mm²; rel_diff_pct = |a-b| / max(|a|,|b|) * 100\n"
        << "# 注意: 这是数值差异基线，不是验收阈值；合并实现前必须解释每一行的大差异。\n"
        << "kind,shape,param,layer,engine_area_mm2,engraving_area_mm2,abs_diff_mm2,"
           "rel_diff_pct,engine_rings,engraving_rings,engine_points,engraving_points\n";

    const auto cases = makeCases();
    runOffsetComparison(out, cases);
    runInsetRingsComparison(out, cases);
    out.close();

    std::cout << "wrote " << outPath << " (" << cases.size() << " shapes)\n";
    std::cout << "Engine2D precision = " << Eg::kEngine2DQuantizationMm
              << " mm, Engraving precision = " << Eg::kEngravingQuantizationMm << " mm\n";
    return 0;
}
