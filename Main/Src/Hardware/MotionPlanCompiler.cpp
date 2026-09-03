#include "MotionPlanCompiler.h"

#ifdef ENABLE_HARDWARE

#include <algorithm>
#include <cmath>
#include <unordered_set>
#include <vector>

#include "Engine/SyEntity/SyEntity.h"
#include "Engine2D/Edit/LayerSnapshot.h"
#include "Engine2D/Geo/GeometryContext.h"
#include "Engine2D/Geometry/Geo2DPath.h"
#include "Engine2D/Geometry/Geo2DTypes.h"

#include "Log/SyLogger.h"

namespace
{
    /// 弦高容差上界（mm）。见 compile() 里拒绝逻辑处的说明 ——
    /// 这是为绕开 Engine2D 把容差当采样数用的实现陷阱而设的硬上界，不是精度偏好。
    constexpr double kMaxChordToleranceMm = 1.0;

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
     * @brief 把一条折线编译进计划。
     * @return 是否产出了至少一段有效运动
     *
     * 首点用空移（laser off）过去，其余点才出光。这一点不能省：
     * 若第一段就带出光，激光会从上一个轮廓的终点一路烧到本轮廓起点。
     */
    bool emitPolyline(const std::vector<Ut::Vec2d>& pts, Hw::MotionPlanBuilder& out)
    {
        if (pts.size() < 2)
        {
            return false;
        }
        out.emitRapidTo(pts.front().x(), pts.front().y());
        for (size_t i = 1; i < pts.size(); ++i)
        {
            out.emitLineTo(pts[i].x(), pts[i].y(), true);
        }
        return true;
    }

    /**
     * @brief 把一个图元编译进计划。
     * @param degraded 出参：该图元因段类型不支持而被整体离散时置 true
     */
    bool emitEntity(const Eg::SyEntity* entity,
                    const Eg::GeometryContext& ctx,
                    bool preferArcs,
                    Hw::MotionPlanBuilder& out,
                    bool& degraded)
    {
        degraded = false;
        if (!entity)
        {
            return false;
        }

        if (!preferArcs)
        {
            return emitPolyline(Eg::Geo2DPath::toPolyline(entity, ctx), out);
        }

        const std::vector<Eg::PathSegment> segments = Eg::Geo2DPath::decompose(entity, ctx);
        if (segments.empty())
        {
            // 分解不出任何段：文字（未转轮廓）、图片、点等没有可加工路径的图元走这里
            return false;
        }

        // 先检查是否存在无法原样表达的段（椭圆、以及没带近似点的其他段）。
        // 一旦存在就整体走折线：把「一半圆弧、一半折线」混在一个轮廓里，
        // 接缝处的容差差异会在工件上留下可见台阶。
        bool needsFallback = false;
        for (const Eg::PathSegment& seg : segments)
        {
            const bool expressible =
                seg.eType == Eg::EPathSegmentType::Line
                || seg.eType == Eg::EPathSegmentType::Arc
                || (seg.eType == Eg::EPathSegmentType::PolylineApprox && seg.vApproxPoints.size() >= 2);
            if (!expressible)
            {
                needsFallback = true;
                break;
            }
        }
        if (needsFallback)
        {
            degraded = true;
            return emitPolyline(Eg::Geo2DPath::toPolyline(entity, ctx), out);
        }

        out.emitRapidTo(segments.front().ptStart.x(), segments.front().ptStart.y());
        for (const Eg::PathSegment& seg : segments)
        {
            switch (seg.eType)
            {
            case Eg::EPathSegmentType::Line:
                out.emitLineTo(seg.ptEnd.x(), seg.ptEnd.y(), true);
                break;

            case Eg::EPathSegmentType::Arc:
                // Geo2DPath 约定分解出的圆弧一律为 CCW 扫掠（见 Geo2DTypes.h 的注释），
                // 因此这里恒传 true；若哪天该约定变了，工件上的表现是圆弧走反向长边
                out.emitArcTo(seg.ptEnd.x(), seg.ptEnd.y(),
                              seg.ptCenter.x(), seg.ptCenter.y(),
                              true, true);
                break;

            case Eg::EPathSegmentType::PolylineApprox:
                for (size_t i = 1; i < seg.vApproxPoints.size(); ++i)
                {
                    out.emitLineTo(seg.vApproxPoints[i].x(), seg.vApproxPoints[i].y(), true);
                }
                break;

            default:
                // 上面的 needsFallback 已经拦掉了，走到这里说明段类型枚举扩展了而这里没跟上
                SY_ERRORF("[MotionPlanCompiler] Unhandled path segment type %d on entity %lld",
                    static_cast<int>(seg.eType), static_cast<long long>(entity->id));
                return false;
            }
        }
        return true;
    }
}

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
            result.error = QStringLiteral("默认工艺参数非法（功率 %1%%、速度 %2mm/s、遍数 %3）")
                               .arg(spec.defaultParams.powerPercent)
                               .arg(spec.defaultParams.speedMmPerSec)
                               .arg(spec.defaultParams.passes);
            return result;
        }
        for (auto it = spec.layerParams.constBegin(); it != spec.layerParams.constEnd(); ++it)
        {
            if (!it.value().valid())
            {
                result.error = QStringLiteral("图层 %1 的工艺参数非法").arg(it.key());
                return result;
            }
        }
        if (spec.chordToleranceMm <= 0.0)
        {
            result.error = QStringLiteral("弦高容差必须为正数（当前 %1）").arg(spec.chordToleranceMm);
            return result;
        }
        // 上界不是「精度够用就行」的经验值，而是躲开 Engine2D 的一个实现陷阱：
        // BezierAlgorithms::discretizeEntity 对 圆 / 圆弧 / 椭圆 走的是 default 分支，
        // 那里把 param（即这里的 dDiscretize）当成**采样点个数**用，且 <2 时回落到 32。
        // 于是 [0,2) 的任何容差都得到 32 点，而 5.0 会静默变成 5 点 —— 一个圆被切成五边形，
        // 却照样返回成功。宁可在入口拒绝，也不能把这种结果送去出光。
        if (spec.chordToleranceMm > kMaxChordToleranceMm)
        {
            result.error = QStringLiteral("弦高容差 %1mm 过大（上限 %2mm）")
                               .arg(spec.chordToleranceMm)
                               .arg(kMaxChordToleranceMm);
            return result;
        }
        if (spec.selectionOnly && spec.selectedEntityIds.isEmpty())
        {
            // 「只加工选中」但没选任何东西：绝不能退化成加工全图
            result.error = QStringLiteral("已勾选「只加工选中图元」，但当前没有选中任何图元");
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
                        SY_WARNF("[MotionPlanCompiler] Layer %d missing from layerOrder, appended last",
                            b.layerId);
                        ordered.push_back(std::move(b));
                    }
                }
                buckets = std::move(ordered);
            }
        }

        // ---- 逐图层编译 ----
        const Eg::GeometryContext ctx = makeContext(spec);
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
            if (layers && spec.skipLockedLayers && layers->isLayerLocked(bucket.layerId))
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

            maxPasses = std::max(maxPasses, bucket.params.passes);

            int32_t emittedInLayer = 0;
            for (int32_t pass = 0; pass < bucket.params.passes; ++pass)
            {
                out.emitBeginPass(pass);
                for (const Eg::SyEntity* entity : bucket.entities)
                {
                    bool degraded = false;
                    if (emitEntity(entity, ctx, spec.emitArcs, out, degraded))
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

        if (out.commandCount() == 0 || result.layerCount == 0)
        {
            result.error = QStringLiteral(
                "没有可加工的图元（共 %1 个图元，其中 %2 个被隐藏/锁定/无路径而跳过）")
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
                 "bounds [%.3f,%.3f]-[%.3f,%.3f], skipped=%d degraded=%d",
            spec.jobId.toUtf8().constData(), result.layerCount, result.entityCount,
            static_cast<long long>(result.commandCount), result.boundsMinX, result.boundsMinY,
            result.boundsMaxX, result.boundsMaxY,
            result.skippedEntityCount, result.degradedEntityCount);
        return result;
    }
}

#endif  // ENABLE_HARDWARE
