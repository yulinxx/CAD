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
    #include <unordered_set>
    #include <vector>

    #include <QCoreApplication>

    #include "Engine/SyEntity/SyEntity.h"
    #include "Engine2D/Cam/GeometryExtractor.h"
    #include "Engine2D/Cam/ToolpathGeometry.h"
    #include "Engine2D/Edit/LayerSnapshot.h"
    #include "Engine2D/Geo/GeometryContext.h"

    #include "Log/SyLogger.h"

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
        std::vector<const Eg::SyEntity*> entities;
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
     * 换算**只在这里发生一次**（编译器发射指令的那一刻）。
     * 适配器与作业服务都不再换算。
     *
     * 之前的状态是「UI2D 的旧链路在服务层换算、新 HAL 链路完全不换算」——
     * 两套坐标语义并存，同一份文档走不同路径会烧到不同位置。
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
        out.emitRapidTo(start.x(), start.y());

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
    bool emitEntity(const Eg::SyEntity* entity,
        const Eg::Cam::ExtractOptions& options,
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
            if (emitContour(contour, preferArcs, chordToleranceMm, frame, out, contourDegraded))
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

        const std::vector<Eg::SyEntity*> all = scene.getAllEntities();
        for (const Eg::SyEntity* entity : all)
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
                        SY_WARNF("[MotionPlanCompiler] Layer %d missing from layerOrder, appended last", b.layerId);
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
            if (layers && spec.skipLockedLayers && !layers->isLayerLocked(bucket.layerId))
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

                for (const Eg::SyEntity* entity : bucket.entities)
                {
                    bool degraded = false;
                    if (emitEntity(entity,
                            extractOptions,
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

        SY_DEBUGF("[MotionPlanCompiler] '%s': %d layer(s), %d entity(ies), %lld command(s), "
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
