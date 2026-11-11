/**
 * @file MotionPlanCompiler.cpp
 * @brief 运动规划编译器实现
 *
 * 将 2D 文档的几何编译为运动轨迹。
 *
 * 几何来源唯一：`Eg::Cam::GeometryExtractor`（Engine2D/Cam/）。
 * 坐标换算唯一：编译器发射指令时的 `Eg::MachineFrame`。
 */
#include "MotionPlanCompiler.h"

#ifdef ENABLE_HARDWARE

    #include <algorithm>
    #include <cmath>
    #include <unordered_map>
    #include <unordered_set>
    #include <vector>

    #include <QCoreApplication>

    #include "Engine/AprEntity/AprEntity.h"
    #include "Engine2D/Cam/ContourContainment.h"
    #include "Engine2D/Cam/GeometryExtractor.h"
    #include "Engine2D/Cam/KerfCompensator.h"
    #include "Engine2D/Cam/ToolpathGeometry.h"
    #include "Engine2D/Edit/LayerSnapshot.h"
    #include "Engine2D/Geo/GeometryContext.h"

    #include "Log/AprLogger.h"

namespace
{
    /// 弦高容差上界（mm）。见 compile() 里拒绝逻辑处的说明 ——
    /// 这是防「单位填错」（mm 当 m 填）的合理性上界，不是实现陷阱的绕行。
    constexpr double kMaxChordToleranceMm = 1.0;

    /// 无法加工图元的清单最多展示多少条（界面可读性；总数仍然准确）
    constexpr int kMaxUnmachinableDetails = 8;

    /// 一个图层的编译任务：图层 ID + 该图层的图元 + 工艺参数。
    struct LayerBucket
    {
        int layerId = 0;
        LayerProcessParams params;
        std::vector<const Eg::AprEntity*> entities;
    };

    /// 把作业规格里的容差装进 GeometryContext。
    Eg::GeometryContext makeContext(const ToolpathJobSpec& spec)
    {
        Eg::GeometryContext ctx = Eg::GeometryContext::defaultContext();
        ctx.dDiscretize = spec.chordToleranceMm;
        return ctx;
    }

    /// 取某图层的工艺参数：显式配置优先，否则用作业默认值。
    LayerProcessParams paramsForLayer(const ToolpathJobSpec& spec, int layerId)
    {
        const auto it = spec.layerParams.constFind(layerId);
        return (it != spec.layerParams.constEnd()) ? it.value() : spec.defaultParams;
    }

    /**
     * @brief 坐标换算器：用户空间 → 机器空间。
     *
     * 换算只在这里发生一次（编译器发射指令的那一刻），
     * 适配器与作业服务都不换算，避免两套链路各自换算导致同一份文档
     * 走不同路径烧到不同位置。
     *
     * 镜像会反转圆弧的绕向：X 镜像把 CCW 变成 CW。
     * 只改端点不改绕向，圆弧会走反边（90° 小弧变 270° 大弧）。
     */
    class FrameTransform
    {
    public:
        explicit FrameTransform(const Eg::MachineFrame& frame)
            : m_frame(frame)
        {
        }

        Ut::Vec2d point(const Ut::Vec2d& user) const
        {
            return m_frame.toMachine(user);
        }

        /// 圆弧的绕向：镜像时反转
        bool arcDirection(bool counterClockwise) const
        {
            if (m_frame.isReflected())
            {
                return !counterClockwise;
            }
            return counterClockwise;
        }

    private:
        const Eg::MachineFrame& m_frame;
    };

    // ============================================================
    //  割缝补偿 / 引入线（M1）
    // ============================================================

    /// 轮廓折线化：闭合判定与引入落点检查用。圆弧按弦高容差离散。
    std::vector<Ut::Vec2d> contourPolygon(const Eg::Cam::Contour& contour, double chordToleranceMm)
    {
        std::vector<Ut::Vec2d> pts;
        if (contour.segments.empty())
        {
            return pts;
        }
        pts.push_back(contour.segments.front().ptStart);
        for (const Eg::Cam::PathSegment& seg : contour.segments)
        {
            if (seg.type == Eg::Cam::PathSegment::Type::Line)
            {
                pts.push_back(seg.ptEnd);
            }
            else
            {
                const std::vector<Ut::Vec2d> arcPts =
                    Eg::Cam::flattenArcSegment(seg, chordToleranceMm);
                if (arcPts.size() > 1)
                {
                    pts.insert(pts.end(), arcPts.begin() + 1, arcPts.end());
                }
            }
        }
        // 闭合轮廓首尾重合：去掉末尾重复点，避免折线退化
        if (pts.size() > 1)
        {
            const double dx = pts.front().x() - pts.back().x();
            const double dy = pts.front().y() - pts.back().y();
            if (std::hypot(dx, dy) < 1e-7)
            {
                pts.pop_back();
            }
        }
        return pts;
    }

    double polygonSignedArea(const std::vector<Ut::Vec2d>& pts)
    {
        double area = 0.0;
        const size_t n = pts.size();
        for (size_t i = 0; i < n; ++i)
        {
            const Ut::Vec2d& p = pts[i];
            const Ut::Vec2d& q = pts[(i + 1) % n];
            area += p.x() * q.y() - q.x() * p.y();
        }
        return area * 0.5;
    }

    /// 点到线段的距离
    double distanceToSegment(const Ut::Vec2d& p, const Ut::Vec2d& a, const Ut::Vec2d& b)
    {
        const double dx = b.x() - a.x();
        const double dy = b.y() - a.y();
        const double len2 = dx * dx + dy * dy;
        if (len2 <= 1e-18)
        {
            return std::hypot(p.x() - a.x(), p.y() - a.y());
        }
        double t = ((p.x() - a.x()) * dx + (p.y() - a.y()) * dy) / len2;
        t = std::max(0.0, std::min(1.0, t));
        return std::hypot(p.x() - (a.x() + dx * t), p.y() - (a.y() + dy * t));
    }

    /// 点是否落在多边形内部（射线法；边界返回 true）
    bool pointInPolygon(const Ut::Vec2d& p, const std::vector<Ut::Vec2d>& pts)
    {
        bool inside = false;
        const size_t n = pts.size();
        for (size_t i = 0, j = n - 1; i < n; j = i++)
        {
            const Ut::Vec2d& a = pts[i];
            const Ut::Vec2d& b = pts[j];
            if (((a.y() > p.y()) != (b.y() > p.y())) &&
                (p.x() < (b.x() - a.x()) * (p.y() - a.y()) / (b.y() - a.y()) + a.x()))
            {
                inside = !inside;
            }
        }
        return inside;
    }

    /// 引入落点是否安全落在废料侧（严格：边界视为不安全）
    bool isScrapSide(const Ut::Vec2d& p, const std::vector<Ut::Vec2d>& polygon, bool hole)
    {
        const size_t n = polygon.size();
        for (size_t i = 0; i < n; ++i)
        {
            if (distanceToSegment(p, polygon[i], polygon[(i + 1) % n]) <= 1e-6)
            {
                return false;
            }
        }
        const bool inside = pointInPolygon(p, polygon);
        // 外轮廓的废料在多边形外；孔的废料是多边形内部（孔洞空腔）
        return hole ? inside : !inside;
    }

    /**
     * @brief 计算引入线起点（用户空间）。
     *
     * 约束：落点必须在废料侧，且不能落在成品边缘上。
     * 做法：先按配置方向取候选点，失败再取关于切线镜像的方向兜底。
     * 开口轮廓没有内外侧语义，一律跳过（由调用方计数）。
     */
    bool makeLeadInPoint(const Eg::Cam::Contour& contour,
        const LayerProcessParams& params,
        double chordToleranceMm,
        Ut::Vec2d& outStart)
    {
        if (!params.leadInEnabled || !(params.leadInLengthMm > 0.0) ||
            !contour.closed || contour.segments.empty())
        {
            return false;
        }

        const Eg::Cam::PathSegment& first = contour.segments.front();
        double tx = 0.0;
        double ty = 0.0;
        if (first.type == Eg::Cam::PathSegment::Type::Line)
        {
            tx = first.ptEnd.x() - first.ptStart.x();
            ty = first.ptEnd.y() - first.ptStart.y();
        }
        else
        {
            const double rx = first.ptStart.x() - first.ptCenter.x();
            const double ry = first.ptStart.y() - first.ptCenter.y();
            tx = first.counterClockwise ? -ry : ry;
            ty = first.counterClockwise ? rx : -rx;
        }
        const double tlen = std::hypot(tx, ty);
        if (tlen <= 1e-12)
        {
            return false;
        }
        tx /= tlen;
        ty /= tlen;

        const std::vector<Ut::Vec2d> polygon = contourPolygon(contour, chordToleranceMm);
        if (polygon.size() < 3)
        {
            return false;
        }
        const double area = polygonSignedArea(polygon);
        if (std::fabs(area) < 1e-9)
        {
            return false;
        }

        // 废料侧：外轮廓取多边形外侧；孔取多边形内侧。绕向由面积符号决定。
        const bool scrapIsLeft = contour.hole ? (area > 0.0) : (area < 0.0);
        const double sign = scrapIsLeft ? 1.0 : -1.0;
        const double nx = -ty * sign;  // 切线左法向 × 方向符号
        const double ny = tx * sign;

        double dx = -tx;
        double dy = -ty;
        if (params.leadInMode == 1)
        {
            dx = nx;
            dy = ny;
        }
        else if (params.leadInMode == 2)
        {
            const double rad = params.leadInAngleDeg * (3.14159265358979323846 / 180.0);
            dx = -tx * std::cos(rad) + nx * std::sin(rad);
            dy = -ty * std::cos(rad) + ny * std::sin(rad);
        }
        const double dlen = std::hypot(dx, dy);
        if (dlen <= 1e-12)
        {
            return false;
        }
        dx /= dlen;
        dy /= dlen;

        const Ut::Vec2d anchor = first.ptStart;
        for (int attempt = 0; attempt < 2; ++attempt)
        {
            const double s = (attempt == 0) ? 1.0 : -1.0;
            const Ut::Vec2d candidate(anchor.x() + dx * params.leadInLengthMm * s,
                anchor.y() + dy * params.leadInLengthMm * s);
            if (isScrapSide(candidate, polygon, contour.hole))
            {
                outStart = candidate;
                return true;
            }
        }
        return false;
    }

    /// 一个图层的割缝预处理结果：按图元 ID 回填补偿后的轮廓。
    struct LayerKerfResult
    {
        bool applied = false;
        std::unordered_map<uint64_t, std::vector<Eg::Cam::Contour>> byEntity;
    };

    /**
     * @brief 每图层执行一次割缝补偿。
     *
     * 必须先做整层包含关系解析（fillHoleFlags），否则无法区分外环与孔，
     * 补偿方向会整体反过来。参数来自 LayerProcessParams，默认 0 时直接跳过，
     * 保证关闭状态下刀路逐字节不变。
     */
    LayerKerfResult prepareKerf(const LayerBucket& bucket,
        const Eg::Cam::ExtractOptions& options,
        double chordToleranceMm,
        ToolpathCompileResult& result)
    {
        LayerKerfResult prep;
        const double halfWidth = std::fabs(bucket.params.kerfMm);
        if (!(halfWidth > 0.0))
        {
            return prep;
        }
        prep.applied = true;

        std::vector<Eg::Cam::Contour> allContours;
        std::vector<uint64_t> contourEntityIds;
        for (const Eg::AprEntity* entity : bucket.entities)
        {
            if (!entity)
            {
                continue;
            }
            const Eg::Cam::ExtractResult extracted =
                Eg::Cam::GeometryExtractor::extract(entity, options);
            if (extracted.kind != Eg::Cam::GeometryKind::Contour)
            {
                continue;
            }
            contourEntityIds.push_back(static_cast<uint64_t>(entity->id));
            for (const Eg::Cam::Contour& contour : extracted.contours)
            {
                allContours.push_back(contour);
            }
        }
        if (allContours.empty())
        {
            return prep;
        }

        Eg::Cam::ContourContainment::fillHoleFlags(allContours);

        Eg::Cam::KerfOptions kerfOptions;
        kerfOptions.kerfWidth = halfWidth * 2.0;
        kerfOptions.side = bucket.params.kerfMm >= 0.0 ? Eg::Cam::KerfSide::Outside
                                                       : Eg::Cam::KerfSide::Inside;
        kerfOptions.arcTolerance = std::max(1e-3, std::min(0.05, chordToleranceMm));

        const Eg::Cam::KerfResult kerfResult =
            Eg::Cam::KerfCompensator::apply(allContours, kerfOptions);

        for (const Eg::Cam::Contour& contour : kerfResult.contours)
        {
            prep.byEntity[contour.sourceEntityId].push_back(contour);
        }
        // 补偿后整条轮廓消失的图元也要有条目，发射阶段据此跳过
        for (uint64_t id : contourEntityIds)
        {
            prep.byEntity.emplace(id, std::vector<Eg::Cam::Contour>{});
        }

        result.kerfCompensatedContourCount += static_cast<int32_t>(kerfResult.contours.size());
        result.kerfDroppedContourCount += kerfResult.droppedLoopCount;
        result.kerfSplitContourCount += kerfResult.splitLoopCount;
        return prep;
    }

    /**
     * @brief 把一条折线编译进计划。
     * @return 是否产出了至少一段有效运动
     *
     * 首点用空移（laser off）过去，其余点才出光。这一点不能省：
     * 若第一段就带出光，激光会从上一个轮廓的终点一路烧到本轮廓起点。
     */
    bool emitPolyline(const std::vector<Ut::Vec2d>& pts, const FrameTransform& frame, Hw::MotionPlanBuilder& out)
    {
        if (pts.size() < 2)
        {
            return false;
        }
        const Ut::Vec2d start = frame.point(pts.front());
        out.emitRapidTo(start.x(), start.y());
        for (size_t i = 1; i < pts.size(); ++i)
        {
            const Ut::Vec2d p = frame.point(pts[i]);
            out.emitLineTo(p.x(), p.y(), true);
        }
        return true;
    }

    /**
     * @brief 把一个加工轮廓编译进计划。
     *
     * 轮廓的段只有 Line 与 Arc 两种（Cam 层的统一约定，见 ToolpathTypes.h）。
     *
     * @param preferArcs true = 圆弧直接编译成 ArcTo；false = 按弦高容差离散成直线
     * @param degraded    出参：该轮廓因弧被离散而整体降级时置 true
     *
     * 首点必须空移过去：若第一段就带出光，激光会从上一个轮廓的终点一路烧过来。
     */
    bool emitContour(const Eg::Cam::Contour& contour,
        bool preferArcs,
        double chordToleranceMm,
        const Ut::Vec2d* leadInStart,
        const FrameTransform& frame,
        Hw::MotionPlanBuilder& out,
        bool& degraded)
    {
        degraded = false;
        if (contour.segments.empty())
        {
            return false;
        }

        // 轮廓起点：第一个段的 ptStart
        const Ut::Vec2d start = frame.point(contour.segments.front().ptStart);
        if (leadInStart)
        {
            // 空移到引入起点，再沿引入线出光切到轮廓起点
            const Ut::Vec2d lead = frame.point(*leadInStart);
            out.emitRapidTo(lead.x(), lead.y());
            out.emitLineTo(start.x(), start.y(), true);
        }
        else
        {
            out.emitRapidTo(start.x(), start.y());
        }

        for (const Eg::Cam::PathSegment& seg : contour.segments)
        {
            if (seg.type == Eg::Cam::PathSegment::Type::Line)
            {
                const Ut::Vec2d end = frame.point(seg.ptEnd);
                out.emitLineTo(end.x(), end.y(), true);
                continue;
            }

            // 圆弧
            if (preferArcs)
            {
                // counterClockwise 来自 Cam 层：它记录的是**用户画的那段弧**的行进方向，
                // 不是靠「dStart→dEnd 是 CCW」推断的。
                // 恒传 true 会让用户逆时针画的弧走反边（90° 小弧变 270° 大弧），
                // 那是烧废一整块料的量级。
                const Ut::Vec2d end = frame.point(seg.ptEnd);
                const Ut::Vec2d center = frame.point(seg.ptCenter);
                out.emitArcTo(end.x(),
                    end.y(),
                    center.x(),
                    center.y(),
                    frame.arcDirection(seg.counterClockwise),
                    true);
                continue;
            }

            // 弧 → 折线（按弦高容差）
            degraded = true;
            const std::vector<Ut::Vec2d> pts = Eg::Cam::flattenArcSegment(seg, chordToleranceMm);
            for (size_t i = 1; i < pts.size(); ++i)
            {
                const Ut::Vec2d p = frame.point(pts[i]);
                out.emitLineTo(p.x(), p.y(), true);
            }
        }
        return true;
    }

    /**
     * @brief 把一个图元编译进计划。
     *
     * 几何来自 Eg::Cam::GeometryExtractor —— 加工链路的**唯一**几何入口。
     * 提取器对「这类图元暂时无法加工」会给出明确的 kind 与原因，
     * 这里把它转成编译结果里的诊断信息，而不是静默跳过。
     */
    bool emitEntity(const Eg::AprEntity* entity,
        const Eg::Cam::ExtractOptions& options,
        const std::vector<Eg::Cam::Contour>* preparedContours,
        const LayerProcessParams& params,
        bool countLeadStats,
        bool preferArcs,
        double chordToleranceMm,
        const FrameTransform& frame,
        Hw::MotionPlanBuilder& out,
        bool& degraded,
        ToolpathCompileResult& result)
    {
        degraded = false;
        if (!entity)
        {
            return false;
        }

        // 有割缝预处理结果时直接使用，不再重复提取（几何已变换完毕）
        if (preparedContours != nullptr)
        {
            bool anyEmitted = false;
            for (const Eg::Cam::Contour& contour : *preparedContours)
            {
                bool contourDegraded = false;
                Ut::Vec2d leadStart;
                const bool hasLead = makeLeadInPoint(contour, params, chordToleranceMm, leadStart);
                if (countLeadStats)
                {
                    if (hasLead)
                    {
                        ++result.leadInAppliedContourCount;
                    }
                    else if (params.leadInEnabled)
                    {
                        ++result.leadInSkippedContourCount;
                    }
                }
                if (emitContour(contour,
                        preferArcs,
                        chordToleranceMm,
                        hasLead ? &leadStart : nullptr,
                        frame,
                        out,
                        contourDegraded))
                {
                    anyEmitted = true;
                    if (contourDegraded)
                    {
                        degraded = true;
                    }
                }
            }
            return anyEmitted;
        }

        const Eg::Cam::ExtractResult extracted = Eg::Cam::GeometryExtractor::extract(entity, options);

        switch (extracted.kind)
        {
        case Eg::Cam::GeometryKind::Contour:
            break;

        case Eg::Cam::GeometryKind::Point:
            // 单点图元：一次「原地打点」。当前 IR 没有专门的 mark 指令，
            // 用零长度的出光移动表达；卡片若不接受零长度移动会忽略它。
            // 这一点在真机联调时需要复核（见 Docs/05-硬件与设备 的待办）。
        {
            const Ut::Vec2d p = frame.point(extracted.point);
            out.emitRapidTo(p.x(), p.y());
            out.emitLineTo(p.x(), p.y(), true);
            return true;
        }

        case Eg::Cam::GeometryKind::Container:
            // 群组本身没有几何，成员是场景里的独立图元，各自被提取。
            return false;

        case Eg::Cam::GeometryKind::Fillable:
        case Eg::Cam::GeometryKind::Raster:
        case Eg::Cam::GeometryKind::Unsupported:
        default:
        {
            ++result.unmachinableEntityCount;
            if (result.unmachinableDetails.size() < kMaxUnmachinableDetails)
            {
                result.unmachinableDetails.append(QStringLiteral("%1 x %2 (%3)")
                                                     .arg(1)
                                                     .arg(QString::fromUtf8(Eg::Cam::geometryKindName(extracted.kind)))
                                                     .arg(QString::fromUtf8(extracted.reason ? extracted.reason : "")));
            }
            return false;
        }
        }

        bool anyEmitted = false;
        for (const Eg::Cam::Contour& contour : extracted.contours)
        {
            bool contourDegraded = false;
            Ut::Vec2d leadStart;
            const bool hasLead = makeLeadInPoint(contour, params, chordToleranceMm, leadStart);
            if (countLeadStats)
            {
                if (hasLead)
                {
                    ++result.leadInAppliedContourCount;
                }
                else if (params.leadInEnabled)
                {
                    ++result.leadInSkippedContourCount;
                }
            }
            if (emitContour(contour,
                    preferArcs,
                    chordToleranceMm,
                    hasLead ? &leadStart : nullptr,
                    frame,
                    out,
                    contourDegraded))
            {
                anyEmitted = true;
                if (contourDegraded)
                {
                    degraded = true;
                }
            }
        }
        return anyEmitted;
    }
}  // namespace

namespace MotionPlanCompiler
{
    ToolpathCompileResult compile(const Eg::SceneManager& scene,
        const LayerManager* layers,
        const ToolpathJobSpec& spec,
        Hw::MotionPlanBuilder& out)
    {
        ToolpathCompileResult result;
        out.clear();

        if (!spec.defaultParams.valid())
        {
            result.error = QStringLiteral("Invalid default process params (power %1%%, speed %2mm/s, passes %3)")
                               .arg(spec.defaultParams.powerPercent)
                               .arg(spec.defaultParams.speedMmPerSec)
                               .arg(spec.defaultParams.passes);  // 默认工艺参数非法
            return result;
        }
        for (auto it = spec.layerParams.constBegin(); it != spec.layerParams.constEnd(); ++it)
        {
            if (!it.value().valid())
            {
                result.error = QStringLiteral("Invalid process params for layer %1").arg(it.key());  // 图层工艺参数非法
                return result;
            }
        }
        if (spec.chordToleranceMm <= 0.0)
        {
            result.error = QStringLiteral("Chord tolerance must be positive (current %1)")
                               .arg(spec.chordToleranceMm);  // 弦高容差必须为正数
            return result;
        }
        // 合理性上界：弦高容差超过 1mm 对激光加工没有合理场景，几乎必然是
        // 单位填错（把 m 当 mm 填，或把米制容差 0.01 直接当毫米用）。
        // 注意：Engine2D 侧已实现真正的弦高细分
        // （BezierAlgorithms::discretizeEntity 的 CIRCLE/ARC/ELLIPSE 分支按
        // CircleAlgorithms::sampleCountForTolerance 取段数），大容差现在会诚实地
        // 产出粗折线而不是报错 —— 所以这里拦的是「数量级错误」，不是精度偏好。
        if (spec.chordToleranceMm > kMaxChordToleranceMm)
        {
            result.error = QStringLiteral("Chord tolerance %1mm exceeds maximum %2mm")
                               .arg(spec.chordToleranceMm)
                               .arg(kMaxChordToleranceMm);  // 弦高容差过大
            return result;
        }
        if (spec.selectionOnly && spec.selectedEntityIds.isEmpty())
        {
            // 「只加工选中」但没选任何东西：绝不能退化成加工全图
            result.error = QStringLiteral(
                "Selection-only mode enabled but no entities selected");  // 已勾选只加工选中图元但未选中任何图元
            return result;
        }

        std::unordered_set<int64_t> selection;
        if (spec.selectionOnly)
        {
            selection.reserve(static_cast<size_t>(spec.selectedEntityIds.size()));
            for (int64_t id : spec.selectedEntityIds)
            {
                selection.insert(id);
            }
        }

        // ---- 分桶：图元按图层归类 ----
        std::vector<LayerBucket> buckets;
        auto bucketFor = [&buckets, &spec](int layerId) -> LayerBucket& {
            for (LayerBucket& b : buckets)
            {
                if (b.layerId == layerId)
                {
                    return b;
                }
            }
            LayerBucket b;
            b.layerId = layerId;
            b.params = paramsForLayer(spec, layerId);
            buckets.push_back(b);
            return buckets.back();
        };

        const std::vector<Eg::AprEntity*> all = scene.getAllEntities();
        for (const Eg::AprEntity* entity : all)
        {
            if (!entity || !entity->isValid())
            {
                ++result.skippedEntityCount;
                continue;
            }
            if (spec.selectionOnly && selection.find(entity->id) == selection.end())
            {
                continue;  // 未选中的不计入「跳过」统计，那不是异常
            }
            if (spec.skipHiddenLayers && !entity->visible())
            {
                ++result.skippedEntityCount;
                continue;
            }
            if (spec.skipLockedLayers && entity->locked())
            {
                ++result.skippedEntityCount;
                continue;
            }

            const int layerId = layers ? layers->getEntityLayer(entity) : 0;
            bucketFor(layerId).entities.push_back(entity);
        }

        // ---- 图层顺序：以文档的绘制顺序为准 ----
        if (layers)
        {
            const LayerDocumentSnapshot snapshot = layers->captureDocument();
            if (!snapshot.layerOrder.empty())
            {
                std::vector<LayerBucket> ordered;
                ordered.reserve(buckets.size());
                for (int layerId : snapshot.layerOrder)
                {
                    for (LayerBucket& b : buckets)
                    {
                        if (b.layerId == layerId && !b.entities.empty())
                        {
                            ordered.push_back(std::move(b));
                            b.entities.clear();
                        }
                    }
                }
                // 快照里没列到的图层（理论上不该有）补在末尾，绝不能悄悄丢掉
                for (LayerBucket& b : buckets)
                {
                    if (!b.entities.empty())
                    {
                        APR_WARNF("[MotionPlanCompiler] Layer %d missing from layerOrder, appended last", b.layerId);
                        ordered.push_back(std::move(b));
                    }
                }
                buckets = std::move(ordered);
            }
        }

        // ---- 逐图层编译 ----
        const Eg::GeometryContext ctx = makeContext(spec);
        Eg::Cam::ExtractOptions extractOptions;
        extractOptions.geometry = ctx;

        Eg::MachineFrame frame(spec.machineFrame);
        const FrameTransform transform(frame);

        int32_t maxPasses = 1;

        for (const LayerBucket& bucket : buckets)
        {
            if (bucket.entities.empty())
            {
                continue;
            }
            if (!bucket.params.enabled)
            {
                result.skippedEntityCount += static_cast<int32_t>(bucket.entities.size());
                continue;
            }
            if (layers && spec.skipHiddenLayers && !layers->isLayerVisible(bucket.layerId))
            {
                result.skippedEntityCount += static_cast<int32_t>(bucket.entities.size());
                continue;
            }
            // isLayerLocked 返回 true 表示图层已锁定；开启跳过时要跳过的正是这些图层
            if (layers && spec.skipLockedLayers && layers->isLayerLocked(bucket.layerId))
            {
                result.skippedEntityCount += static_cast<int32_t>(bucket.entities.size());
                continue;
            }
            // 图层「不参与加工输出」：与「不可见」「锁定」并列的第三种跳过理由。
            // 三者的区别：不可见 = 只是不显示；不输出 = 不送机器（尺寸标注、参考底图）。
            if (layers && !layers->isLayerOutput(bucket.layerId))
            {
                result.skippedEntityCount += static_cast<int32_t>(bucket.entities.size());
                continue;
            }

            // 割缝补偿：每层做一次（先解析整层包含关系，再统一偏置）
            const LayerKerfResult kerfPrep =
                prepareKerf(bucket, extractOptions, spec.chordToleranceMm, result);

            const size_t commandsBeforeLayer = out.commandCount();

            out.emitBeginLayer(bucket.layerId);

            Hw::LaserParams laser;
            laser.powerPercent = bucket.params.powerPercent;
            laser.frequencyHz = bucket.params.frequencyHz;
            laser.pulseWidthUs = bucket.params.pulseWidthUs;
            laser.waveformIndex = bucket.params.waveformIndex;
            out.emitSetLaserParams(laser);
            out.emitSetFeedRate(bucket.params.speedMmPerSec);
            out.emitSetRapidRate(spec.rapidSpeedMmPerSec);

            int32_t emittedInLayer = 0;
            for (int32_t pass = 0; pass < bucket.params.passes; ++pass)
            {
                out.emitBeginPass(pass);

                for (const Eg::AprEntity* entity : bucket.entities)
                {
                    const std::vector<Eg::Cam::Contour>* preparedContours = nullptr;
                    if (kerfPrep.applied && entity)
                    {
                        const auto it = kerfPrep.byEntity.find(
                            static_cast<uint64_t>(entity->id));
                        if (it != kerfPrep.byEntity.end())
                        {
                            preparedContours = &it->second;
                        }
                    }

                    bool degraded = false;
                    if (emitEntity(entity,
                            extractOptions,
                            preparedContours,
                            bucket.params,
                            pass == 0,
                            spec.emitArcs,
                            spec.chordToleranceMm,
                            transform,
                            out,
                            degraded,
                            result))
                    {
                        ++emittedInLayer;
                        if (degraded)
                        {
                            ++result.degradedEntityCount;
                        }
                    }
                    else if (pass == 0)
                    {
                        // 只在第一遍统计，否则多遍加工会把同一个图元重复计数
                        ++result.skippedEntityCount;
                    }
                }
                if (spec.emitTrailingLaserOff)
                {
                    // 每遍收尾补一条显式关光：真机上「list 跑完但光还亮着」是事故，
                    // 而每条移动指令自带的 laserOn 标志并不保证最后一条把光关掉
                    out.emitLaserOff();
                }
                out.emitEndPass();
            }

            out.emitEndLayer();

            if (emittedInLayer == 0)
            {
                // 整个图层什么都没编译出来：回退掉刚写的图层头，
                // 免得设备收到一段「有图层标记但没有运动」的空计划
                out.truncateTo(commandsBeforeLayer);
                continue;
            }

            // header 的总遍数是计划级元信息：各图层遍数可以不同，这里取最大值
            // （即单件最多会被重复加工的遍数），不能按块累加，否则会把它读成「串行做了几遍」
            maxPasses = std::max(maxPasses, std::max<int32_t>(1, bucket.params.passes));

            ++result.layerCount;
            result.entityCount += emittedInLayer / std::max<int32_t>(1, bucket.params.passes);
        }

        // ---- 无法加工的图元：按策略决定是整份作业失败还是跳过 ----
        if (result.unmachinableEntityCount > 0 && spec.failOnUnmachinableEntity)
        {
            result.error = QStringLiteral("%1 entity/entities cannot be machined yet: %2")
                               .arg(result.unmachinableEntityCount)
                               .arg(result.unmachinableDetails.join(QStringLiteral("; ")));
            out.clear();
            return result;
        }

        if (out.commandCount() == 0 || result.layerCount == 0)
        {
            result.error = QCoreApplication::translate(
                "MotionPlanCompiler", "No processable entities (total %1, %2 skipped as hidden/locked/no path)")
                               .arg(all.size())
                               .arg(result.skippedEntityCount);
            out.clear();
            return result;
        }

        Hw::MotionPlanHeader header = out.header();
        Hw::hwCopyText(header.planId, Hw::kIdLen, spec.jobId.toUtf8().constData());
        header.axisCount = 2;
        header.passCount = maxPasses;
        out.setHeader(header);
        out.recomputeBounds();

        const Hw::MotionPlanHeader& finalHeader = out.header();
        result.ok = true;
        result.commandCount = out.commandCount();
        result.boundsMinX = finalHeader.boundsMinX;
        result.boundsMinY = finalHeader.boundsMinY;
        result.boundsMaxX = finalHeader.boundsMaxX;
        result.boundsMaxY = finalHeader.boundsMaxY;

        APR_DEBUGF("[MotionPlanCompiler] '%s': %d layer(s), %d entity(ies), %lld command(s), "
                  "bounds [%.3f, %.3f]-[%.3f, %.3f], skipped=%d degraded=%d unmachinable=%d",
            spec.jobId.toUtf8().constData(),
            result.layerCount,
            result.entityCount,
            static_cast<long long>(result.commandCount),
            result.boundsMinX,
            result.boundsMinY,
            result.boundsMaxX,
            result.boundsMaxY,
            result.skippedEntityCount,
            result.degradedEntityCount,
            result.unmachinableEntityCount);
        return result;
    }
}

#endif  // ENABLE_HARDWARE
