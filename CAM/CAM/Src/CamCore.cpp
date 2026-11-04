#define _USE_MATH_DEFINES

/**
 * @file CamCore.cpp
 * @brief CAM 编译主流程（M0 阶段）。
 *
 * 输入 ToolpathJobInput → 输出 CamPlan + CamResult。
 * M0 做：输入校验 → 分层处理 → 轮廓排序 → 空移段 → 长度统计 → 包围盒。
 * M0 不做：割缝补偿 / 引线 / 穿孔 / 功率曲线 / 拐角处理 / 共边切割 ——
 * 这些子模块在 Src/GeometryTransform / Process / Order 的目录占位，M1 落地。
 */

#include "CAM/CamCore.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <string>
#include <unordered_set>
#include <vector>

namespace Cam
{
    // ============================================================
    //  CamError → 文本
    // ============================================================

    const char* camErrorName(CamError error)
    {
        switch (error)
        {
        case CamError::Ok:                 return "OK";
        case CamError::InvalidParams:      return "Invalid process parameters";
        case CamError::EmptyJob:           return "No processable content in job";
        case CamError::ChordToleranceTooLarge: return "Chord tolerance exceeds maximum (1.0mm)";
        case CamError::SelectionEmpty:     return "Selection-only mode but no entities selected";
        case CamError::Cancelled:          return "Compilation cancelled by user";
        case CamError::UncontainedRaster:  return "Fillable/unsupported entities not supported yet";
        }
        return "Unknown error";
    }

    // ============================================================
    //  ABI 自检 —— 桥接层启动时打印版本号与关键结构 sizeof
    // ============================================================

    CamAbiInfo camAbiInfo()
    {
        CamAbiInfo info;
        info.version = CAM_ABI_VERSION;
        info.sizeofPathSegment = sizeof(PathSegment);
        info.sizeofContour = sizeof(Contour);
        info.sizeofEntitySnapshot = sizeof(EntitySnapshot);
        info.sizeofToolpathJobInput = sizeof(ToolpathJobInput);
        info.sizeofCamPlan = sizeof(CamPlan);
        info.sizeofCamResult = sizeof(CamResult);
        return info;
    }

    // ============================================================
    //  内部工具函数
    // ============================================================

    /// 两点距离（mm）
    static double pointDistance(const Ut::Vec2d& a, const Ut::Vec2d& b)
    {
        const double dx = a.x() - b.x();
        const double dy = a.y() - b.y();
        return std::sqrt(dx * dx + dy * dy);
    }

    /// 一段的长度（Line=距离；Arc=r*Δθ 弧度）
    static double segmentLength(const Cam::PathSegment& seg)
    {
        if (seg.type == Cam::PathSegment::Type::Line)
        {
            return pointDistance(seg.ptStart, seg.ptEnd);
        }
        // Arc: 圆心角（弧度）* 半径
        double delta = seg.dEndAngle - seg.dStartAngle;
        if (delta < 0.0)
        {
            delta += 360.0;
        }
        if (!seg.counterClockwise)
        {
            delta = 360.0 - delta;
        }
        return seg.dRadius * delta * (M_PI / 180.0);
    }

    /// 一段的长度（PlanPathSegment 版）
    static double planSegmentLength(const Cam::PlanPathSegment& seg)
    {
        if (seg.type == Cam::PlanPathSegment::Type::Line)
        {
            return pointDistance(seg.ptStart, seg.ptEnd);
        }
        double delta = seg.dEndAngle - seg.dStartAngle;
        if (delta < 0.0)
        {
            delta += 360.0;
        }
        if (!seg.counterClockwise)
        {
            delta = 360.0 - delta;
        }
        return seg.dRadius * delta * (M_PI / 180.0);
    }

    /// 从 ToolpathJobInput::PathSegment 复制一份到 PlanPathSegment
    static Cam::PlanPathSegment toPlanSegment(const Cam::PathSegment& src,
                                               bool isRapid)
    {
        Cam::PlanPathSegment out;
        out.type = Cam::PlanPathSegment::Type(src.type);
        out.ptStart = src.ptStart;
        out.ptEnd = src.ptEnd;
        out.ptCenter = src.ptCenter;
        out.dRadius = src.dRadius;
        out.dStartAngle = src.dStartAngle;
        out.dEndAngle = src.dEndAngle;
        out.counterClockwise = src.counterClockwise;
        out.closed = src.closed;
        out.isRapid = isRapid;
        return out;
    }

    /// 构造一条 rapid 段（空移）
    static Cam::PlanPathSegment makeRapidSegment(const Ut::Vec2d& from, const Ut::Vec2d& to)
    {
        Cam::PlanPathSegment seg;
        seg.type = Cam::PlanPathSegment::Type::Line;
        seg.ptStart = from;
        seg.ptEnd = to;
        seg.isRapid = true;
        return seg;
    }

    /// 更新包围盒
    static void updateBounds(double& minX, double& minY,
                             double& maxX, double& maxY,
                             const Ut::Vec2d& pt)
    {
        if (pt.x() < minX) minX = pt.x();
        if (pt.y() < minY) minY = pt.y();
        if (pt.x() > maxX) maxX = pt.x();
        if (pt.y() > maxY) maxY = pt.y();
    }

    // ============================================================
    //  过滤规则 —— 校验与主流程共用同一份实现
    // ============================================================

    /// 不可试图元清单里展示的类别名（英文，进日志/诊断清单）
    static const char* entityKindName(EntitySnapshot::GeometryKind kind)
    {
        switch (kind)
        {
        case EntitySnapshot::GeometryKind::Contour: return "Contour";
        case EntitySnapshot::GeometryKind::Point:   return "Point";
        case EntitySnapshot::GeometryKind::Fillable: return "Fillable";
        case EntitySnapshot::GeometryKind::Raster:  return "Raster";
        case EntitySnapshot::GeometryKind::Unsupported: return "Unsupported";
        }
        return "Unsupported";
    }

    /// 图层是否被排除：工艺停用 / 不输出 / 隐藏 / 锁定（后两者受全局开关约束）
    static bool isLayerExcluded(const ToolpathJobInput& input,
                                const ToolpathJobInput::LayerBucket& layer)
    {
        if (!layer.params.enabled)
        {
            return true;
        }
        if (!layer.outputEnabled)
        {
            return true;
        }
        if (input.skipHiddenLayers && !layer.visible)
        {
            return true;
        }
        if (input.skipLockedLayers && layer.locked)
        {
            return true;
        }
        return false;
    }

    /// 图元是否被排除：图元级隐藏 / 锁定（受全局开关约束）
    static bool isEntityExcluded(const ToolpathJobInput& input,
                                 const EntitySnapshot& entity)
    {
        return (input.skipHiddenLayers && !entity.visible)
            || (input.skipLockedLayers && entity.locked);
    }

    /// 判断 progress 已取消（轮询点）
    static bool isCancelled(const Cam::Progress* progress)
    {
        return progress && progress->cancelled != 0;
    }

    /// 进度推进（带回调）
    static void reportProgress(Cam::Progress* progress, double fraction, const char* stage)
    {
        if (!progress)
        {
            return;
        }
        // fraction 不能倒退；若当前比传入大，保持大的值
        if (fraction > progress->fraction)
        {
            progress->fraction = fraction;
        }
        progress->stage = stage;
        if (progress->onProgress)
        {
            progress->onProgress(progress->fraction, stage, progress->userData);
        }
    }

    /// 把错误码 + 可读文本写进 CamResult
    static CamResult makeResult(CamError error, const char* errorText)
    {
        CamResult r;
        r.ok = (error == CamError::Ok);
        r.error = error;
        const char* text = errorText ? errorText : camErrorName(error);
        std::strncpy(r.errorText, text, sizeof(r.errorText) - 1);
        r.errorText[sizeof(r.errorText) - 1] = '\0';
        return r;
    }

    // ============================================================
    //  输入校验
    // ============================================================

    static CamResult validateInput(const ToolpathJobInput& input)
    {
        bool hasAnyEntity = false;

        // 只校验真正会参与加工的图层：被停用/隐藏/锁定/不输出的图层不产出刀路
        for (const auto& layer : input.layers)
        {
            if (isLayerExcluded(input, layer))
            {
                continue;
            }
            if (!layer.params.valid())
            {
                return makeResult(CamError::InvalidParams,
                                  "Invalid process params for layer");
            }

            // 弦高容差上界（防 m 当 mm 填）
            const double tol = layer.params.chordToleranceMm;
            if (tol <= 0.0)
            {
                return makeResult(CamError::ChordToleranceTooLarge,
                                  "Chord tolerance must be positive");
            }
            if (tol > 1.0)
            {
                return makeResult(CamError::ChordToleranceTooLarge,
                                  "Chord tolerance exceeds 1.0mm maximum");
            }

            for (const auto& entity : layer.entities)
            {
                if (!isEntityExcluded(input, entity))
                {
                    hasAnyEntity = true;
                    break;
                }
            }
        }

        // 空作业：所有图层/图元都被过滤后没有任何可加工内容
        if (!hasAnyEntity)
        {
            return makeResult(CamError::EmptyJob,
                              "No visible/output-enabled entities");
        }

        return makeResult(CamError::Ok, nullptr);
    }

    // ============================================================
    //  compile 主流程
    // ============================================================

    CamResult compile(const ToolpathJobInput& input,
                      CamPlan& plan,
                      Progress* progress,
                      CamResult* outResult)
    {
        plan.clear();

        // ---- 阶段 1：输入校验 ----
        reportProgress(progress, 0.0, "Validating input");
        if (isCancelled(progress))
        {
            CamResult r = makeResult(CamError::Cancelled, nullptr);
            if (outResult) *outResult = r;
            return r;
        }
        CamResult vr = validateInput(input);
        if (!vr.ok)
        {
            if (outResult) *outResult = vr;
            return vr;
        }

        // ---- 阶段 2：分层处理 ----
        // 轮廓的"当前位置"——下一条 rapid 的起点；从 (0,0) 开始
        Ut::Vec2d currentPosition(0.0, 0.0);
        // 包围盒累计
        double boundsMinX = std::numeric_limits<double>::max();
        double boundsMinY = std::numeric_limits<double>::max();
        double boundsMaxX = std::numeric_limits<double>::lowest();
        double boundsMaxY = std::numeric_limits<double>::lowest();
        int32_t totalContours = 0;
        int32_t machinedEntities = 0;
        int32_t processedLayers = 0;
        int32_t skippedEntities = 0;
        int32_t unmachinableEntities = 0;
        std::vector<std::string> unmachinableDetails;
        const size_t kMaxUnmachinableDetails = 32;

        const double stageStart = 0.1;
        const double stageEnd = 0.9;
        double fraction = stageStart;
        const auto layerCount = input.layers.size();

        for (size_t li = 0; li < layerCount; ++li)
        {
            const auto& layerBucket = input.layers[li];

            if (layerBucket.entities.empty()) continue;
            if (isLayerExcluded(input, layerBucket))
            {
                skippedEntities += (int32_t)layerBucket.entities.size();
                continue;
            }

            reportProgress(progress,
                stageStart + (stageEnd - stageStart) * (double)li / (double)layerCount,
                "Compiling layers");

            if (isCancelled(progress))
            {
                CamResult r = makeResult(CamError::Cancelled, nullptr);
                if (outResult) *outResult = r;
                return r;
            }

            // M0 排序策略：按 sourceEntityId 升序（骨架）
            // M1 引入最近邻 + 2-opt 空移优化（Tools/Order/）
            std::vector<const EntitySnapshot*> orderedEntities;
            orderedEntities.reserve(layerBucket.entities.size());
            for (const auto& e : layerBucket.entities)
            {
                orderedEntities.push_back(&e);
            }
            std::sort(orderedEntities.begin(), orderedEntities.end(),
                [](const EntitySnapshot* a, const EntitySnapshot* b) {
                    return a->entityId < b->entityId;
                });

            const int32_t passCount = std::max<int32_t>(1, layerBucket.params.passes);
            bool layerEmitted = false;

            // 按遍次展开：每个 pass 产出独立的 PlanLayerResult，passIndex 递增
            for (int32_t pass = 0; pass < passCount; ++pass)
            {
                PlanLayerResult layerResult;
                layerResult.layerId = layerBucket.layerId;
                // 工艺参数副本（下游 MotionPlanEmitter 消费）
                layerResult.powerPercent = layerBucket.params.powerPercent;
                layerResult.frequencyHz = layerBucket.params.frequencyHz;
                layerResult.pulseWidthUs = layerBucket.params.pulseWidthUs;
                layerResult.speedMmPerSec = layerBucket.params.speedMmPerSec;
                layerResult.rapidSpeedMmPerSec = layerBucket.params.rapidSpeedMmPerSec;
                layerResult.passIndex = pass;

                for (const auto* entity : orderedEntities)
                {
                    if (isEntityExcluded(input, *entity))
                    {
                        // 只在第一遍统计，避免多遍加工把同一个图元重复计数
                        if (pass == 0) ++skippedEntities;
                        continue;
                    }

                    // 位图的遍次已经由桥接层写入 scanline.passIndex，
                    // 图层 pass 循环不再重复展开，否则遍数会被乘算
                    if (entity->kind == EntitySnapshot::GeometryKind::Raster && pass > 0)
                    {
                        continue;
                    }

                    const size_t contoursBeforeEntity = layerResult.contours.size();

                    // ---- EntitySnapshot 分类处理 ----
                    if (entity->kind == EntitySnapshot::GeometryKind::Contour)
                    {
                        for (const auto& srcContour : entity->contours)
                        {
                            PlanContour planContour;
                            planContour.closed = srcContour.closed;
                            planContour.hole = srcContour.hole;
                            planContour.sourceEntityId = srcContour.sourceEntityId;
                            planContour.sourceLayerId = srcContour.sourceLayerId;

                            // 空移段：从 currentPosition → contour 首段起点
                            if (!srcContour.segments.empty())
                            {
                                const auto& firstSeg = srcContour.segments.front();
                                if (pointDistance(currentPosition, firstSeg.ptStart) > 1e-6)
                                {
                                    auto rapidSeg = makeRapidSegment(currentPosition, firstSeg.ptStart);
                                    planContour.segments.push_back(rapidSeg);
                                    const double rapidLen = planSegmentLength(rapidSeg);
                                    planContour.lengthMm += rapidLen;
                                    planContour.rapidLengthMm += rapidLen;
                                    currentPosition = firstSeg.ptStart;
                                }

                                // 加工段
                                for (const auto& srcSeg : srcContour.segments)
                                {
                                    auto cuttingSeg = toPlanSegment(srcSeg, /*isRapid=*/false);
                                    planContour.segments.push_back(cuttingSeg);

                                    const double len = segmentLength(srcSeg);
                                    planContour.lengthMm += len;
                                    planContour.cuttingLengthMm += len;
                                    currentPosition = srcSeg.ptEnd;

                                    updateBounds(boundsMinX, boundsMinY, boundsMaxX, boundsMaxY,
                                                 cuttingSeg.ptStart);
                                    updateBounds(boundsMinX, boundsMinY, boundsMaxX, boundsMaxY,
                                                 cuttingSeg.ptEnd);
                                    if (cuttingSeg.type == PlanPathSegment::Type::Arc)
                                    {
                                        updateBounds(boundsMinX, boundsMinY, boundsMaxX, boundsMaxY,
                                                     cuttingSeg.ptCenter);
                                    }
                                }
                            }

                            if (!planContour.segments.empty())
                            {
                                layerResult.contours.push_back(std::move(planContour));
                                ++totalContours;
                            }
                        }
                    }
                    else if (entity->kind == EntitySnapshot::GeometryKind::Point)
                    {
                        PlanContour planContour;
                        planContour.sourceEntityId = entity->entityId;
                        planContour.sourceLayerId = entity->layerId;

                        // Rapid 到目标点 → 原地 LineTo（长度 0）
                        if (pointDistance(currentPosition, entity->point) > 1e-6)
                        {
                            auto rapidSeg = makeRapidSegment(currentPosition, entity->point);
                            planContour.segments.push_back(rapidSeg);
                            planContour.rapidLengthMm += planSegmentLength(rapidSeg);
                        }
                        Cam::PlanPathSegment markSeg;
                        markSeg.type = Cam::PlanPathSegment::Type::Line;
                        markSeg.ptStart = entity->point;
                        markSeg.ptEnd = entity->point;
                        markSeg.isRapid = false;
                        planContour.segments.push_back(markSeg);
                        planContour.lengthMm += planContour.rapidLengthMm;

                        currentPosition = entity->point;
                        updateBounds(boundsMinX, boundsMinY, boundsMaxX, boundsMaxY, entity->point);

                        layerResult.contours.push_back(std::move(planContour));
                        ++totalContours;
                    }
                    else if (entity->kind == EntitySnapshot::GeometryKind::Raster &&
                             !entity->scanlines.empty())
                    {
                        // 位图光栅策略：桥接层已把像素换算成 mm，CAM 只负责
                        // 排序、空移、统计与逐段工艺覆盖，不重新读取位图
                        PlanContour planContour;
                        planContour.sourceEntityId = entity->entityId;
                        planContour.sourceLayerId = entity->layerId;
                        planContour.closed = false;

                        if (pointDistance(currentPosition, entity->scanlines.front().ptStart) > 1e-6)
                        {
                            auto rapidSeg =
                                makeRapidSegment(currentPosition, entity->scanlines.front().ptStart);
                            planContour.segments.push_back(rapidSeg);
                            const double rapidLen = planSegmentLength(rapidSeg);
                            planContour.lengthMm += rapidLen;
                            planContour.rapidLengthMm += rapidLen;
                            currentPosition = entity->scanlines.front().ptStart;
                        }

                        for (const auto& line : entity->scanlines)
                        {
                            PlanPathSegment planSeg;
                            planSeg.type = PlanPathSegment::Type::Line;
                            planSeg.ptStart = line.ptStart;
                            planSeg.ptEnd = line.ptEnd;
                            planSeg.isRapid = !line.laserOn;
                            planSeg.powerPercent = line.laserOn ? line.powerPercent : 0.0;
                            planSeg.speedMmPerSec =
                                line.speedMmPerSec > 0.0 ? line.speedMmPerSec : -1.0;
                            planSeg.passIndex = line.passIndex;

                            const double len = planSegmentLength(planSeg);
                            planContour.lengthMm += len;
                            if (planSeg.isRapid)
                            {
                                planContour.rapidLengthMm += len;
                            }
                            else
                            {
                                planContour.cuttingLengthMm += len;
                            }

                            updateBounds(boundsMinX, boundsMinY, boundsMaxX, boundsMaxY,
                                         planSeg.ptStart);
                            updateBounds(boundsMinX, boundsMinY, boundsMaxX, boundsMaxY,
                                         planSeg.ptEnd);

                            planContour.segments.push_back(planSeg);
                            currentPosition = line.ptEnd;
                        }

                        layerResult.contours.push_back(std::move(planContour));
                        ++totalContours;
                    }
                    else
                    {
                        // Fillable / 无扫描线的 Raster / Unsupported —— 暂不支持
                        if (pass == 0)
                        {
                            ++unmachinableEntities;
                            if (unmachinableDetails.size() < kMaxUnmachinableDetails)
                            {
                                std::string detail = std::string("1 x ") + entityKindName(entity->kind);
                                if (!entity->unmachinableReason.empty())
                                {
                                    detail += std::string(" (") + entity->unmachinableReason + ")";
                                }
                                unmachinableDetails.push_back(std::move(detail));
                            }
                        }
                        if (input.failOnUnmachinable)
                        {
                            plan.clear();
                            auto r = makeResult(CamError::UncontainedRaster,
                                                "Fillable/Raster entities not supported yet");
                            r.unmachinableEntityCount = unmachinableEntities;
                            r.unmachinableDetails = unmachinableDetails;
                            if (outResult) *outResult = r;
                            return r;
                        }
                    }

                    // 图元数按"实际产出刀路的图元"统计，多遍不重复计数
                    if (pass == 0 && layerResult.contours.size() > contoursBeforeEntity)
                    {
                        ++machinedEntities;
                    }
                }

                // ---- 统计该层该遍累计 ----
                for (const auto& c : layerResult.contours)
                {
                    layerResult.layerCuttingLengthMm += c.cuttingLengthMm;
                    layerResult.layerRapidLengthMm += c.rapidLengthMm;
                    plan.totalCuttingLengthMm += c.cuttingLengthMm;
                    plan.totalRapidLengthMm += c.rapidLengthMm;
                }

                if (!layerResult.contours.empty())
                {
                    plan.layers.push_back(std::move(layerResult));
                    layerEmitted = true;
                }
            }

            if (layerEmitted) ++processedLayers;
        }

        reportProgress(progress, 0.9, "Computing bounds and statistics");
        if (isCancelled(progress))
        {
            CamResult r = makeResult(CamError::Cancelled, nullptr);
            plan.clear();
            if (outResult) *outResult = r;
            return r;
        }

        // ---- 阶段 3：包围盒 + 最终校验 ----
        if (plan.layers.empty() || totalContours == 0)
        {
            CamResult r = makeResult(CamError::EmptyJob, "All entities skipped or not supported");
            r.skippedEntityCount = skippedEntities;
            r.unmachinableEntityCount = unmachinableEntities;
            if (outResult) *outResult = r;
            return r;
        }

        plan.boundsMinX = boundsMinX;
        plan.boundsMinY = boundsMinY;
        plan.boundsMaxX = boundsMaxX;
        plan.boundsMaxY = boundsMaxY;

        reportProgress(progress, 1.0, "Done");

        // ---- 组装最终 CamResult ----
        CamResult finalResult = makeResult(CamError::Ok, nullptr);
        finalResult.layerCount = processedLayers;
        finalResult.entityCount = machinedEntities;
        finalResult.contourCount = totalContours;
        finalResult.skippedEntityCount = skippedEntities;
        finalResult.unmachinableEntityCount = unmachinableEntities;
        finalResult.unmachinableDetails = unmachinableDetails;
        if (outResult)
        {
            *outResult = finalResult;
        }
        return finalResult;
    }

}  // namespace Cam
