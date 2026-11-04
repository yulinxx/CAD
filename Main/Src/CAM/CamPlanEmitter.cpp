/**
 * @file CamPlanEmitter.cpp
 * @brief Cam::CamPlan → Hw::MotionPlan 的发射实现。
 */

#include "CamPlanEmitter.h"

#include <algorithm>

namespace
{
    /// 段级有效参数。CamPlan 里的 -1 / <=0 只在这里被解释一次，
    /// 之后全流程都拿「已填实」的数值，避免继承规则散落在多个分支里。
    struct SegmentParams
    {
        double powerPercent = 0.0;
        double speedMmPerSec = 0.0;
        int32_t passIndex = 0;
    };

    /// 解析段级覆盖：功率/速度/遍次缺省时继承图层值
    SegmentParams resolveSegment(const Cam::PlanPathSegment& segment, const Cam::PlanLayerResult& layer)
    {
        SegmentParams params;
        params.powerPercent = segment.powerPercent >= 0.0 ? segment.powerPercent : layer.powerPercent;
        params.speedMmPerSec = segment.speedMmPerSec > 0.0 ? segment.speedMmPerSec : layer.speedMmPerSec;
        params.passIndex = segment.passIndex >= 0 ? segment.passIndex : layer.passIndex;
        return params;
    }

    /// 组装一组激光参数：频率/脉宽始终来自图层，只有功率可被段级覆盖
    Hw::LaserParams laserParamsFor(const Cam::PlanLayerResult& layer, double powerPercent)
    {
        Hw::LaserParams laser;
        laser.powerPercent = powerPercent;
        laser.frequencyHz = layer.frequencyHz;
        laser.pulseWidthUs = layer.pulseWidthUs;
        // 波形序号不在 CamPlan 的契约里，保持默认值 0
        return laser;
    }

    /// 图层里是否至少有一段几何
    bool layerHasSegments(const Cam::PlanLayerResult& layer)
    {
        for (const Cam::PlanContour& contour : layer.contours)
        {
            if (!contour.segments.empty())
            {
                return true;
            }
        }
        return false;
    }
}  // namespace

namespace CamPlanEmitter
{
    size_t emitCamPlan(const Cam::CamPlan& plan, const Eg::MachineFrame& frame, Hw::MotionPlanBuilder& out)
    {
        size_t emittedSegments = 0;
        int32_t maxPassIndex = 0;

        for (const Cam::PlanLayerResult& layer : plan.layers)
        {
            // 空图层不写图层头：设备收到「有图层标记但没有任何运动」的计划，
            // 表现是「点了开始加工，机器动一下就结束了」
            if (!layerHasSegments(layer))
            {
                continue;
            }

            out.emitBeginLayer(layer.layerId);
            out.emitSetLaserParams(laserParamsFor(layer, layer.powerPercent));
            out.emitSetFeedRate(layer.speedMmPerSec);
            out.emitSetRapidRate(layer.rapidSpeedMmPerSec);

            int32_t currentPass = layer.passIndex;
            double currentPower = layer.powerPercent;
            double currentFeed = layer.speedMmPerSec;
            maxPassIndex = std::max(maxPassIndex, currentPass);
            out.emitBeginPass(currentPass);

            for (const Cam::PlanContour& contour : layer.contours)
            {
                for (const Cam::PlanPathSegment& segment : contour.segments)
                {
                    const SegmentParams params = resolveSegment(segment, layer);

                    // 遍次切换必须成对收尾/开头，否则设备会把两遍当成一遍
                    if (params.passIndex != currentPass)
                    {
                        out.emitEndPass();
                        out.emitBeginPass(params.passIndex);
                        currentPass = params.passIndex;
                        maxPassIndex = std::max(maxPassIndex, currentPass);
                    }

                    if (segment.isRapid)
                    {
                        // 停光段只做空移：功率覆盖被忽略（CAM 对停光段会把功率填 0，
                        // 原样下发会变成「以 0% 功率切一段」的无意义参数），
                        // 空移速度由图层头的 SetRapidRate 统一决定。
                        const Ut::Vec2d target = frame.toMachine(segment.ptEnd);
                        out.emitRapidTo(target.x(), target.y());
                        ++emittedSegments;
                        continue;
                    }

                    if (params.powerPercent != currentPower)
                    {
                        out.emitSetLaserParams(laserParamsFor(layer, params.powerPercent));
                        currentPower = params.powerPercent;
                    }
                    if (params.speedMmPerSec != currentFeed)
                    {
                        out.emitSetFeedRate(params.speedMmPerSec);
                        currentFeed = params.speedMmPerSec;
                    }

                    const Ut::Vec2d end = frame.toMachine(segment.ptEnd);
                    if (segment.type == Cam::PlanPathSegment::Type::Arc)
                    {
                        const Ut::Vec2d center = frame.toMachine(segment.ptCenter);
                        // 镜像会反转绕向，只改端点不改绕向会让圆弧走反边
                        const bool counterClockwise =
                            frame.isReflected() ? !segment.counterClockwise : segment.counterClockwise;
                        out.emitArcTo(end.x(), end.y(), center.x(), center.y(), counterClockwise, true);
                    }
                    else
                    {
                        out.emitLineTo(end.x(), end.y(), true);
                    }
                    ++emittedSegments;
                }
            }

            // 每遍收尾补一条显式关光：指令跑完而光还亮着时没有运动，
            // 激光会持续烧一个点，属于事故
            out.emitLaserOff();
            out.emitEndPass();

            out.emitEndLayer();
        }

        if (emittedSegments > 0)
        {
            Hw::MotionPlanHeader header = out.header();
            header.axisCount = 2;
            header.passCount = std::max<int32_t>(1, maxPassIndex + 1);
            out.setHeader(header);
            // 包围盒按机器坐标重算：CamPlan 的包围盒是用户坐标，不能直接透传给软限位预检
            out.recomputeBounds();
        }

        return emittedSegments;
    }

}  // namespace CamPlanEmitter
