/**
 * @file CamPlanEmitterTests.cpp
 * @brief CamPlan → MotionPlan 发射器的纯单测（不依赖整机与设备）。
 *
 * 只验证「CamPlan 字段 → 指令序列」这一层：坐标用恒等机架，
 * 这样断言的是发射规则本身，不会被镜像/平移的数值淹没。
 */

#include <gtest/gtest.h>

#include "CAM/CamPlan.h"
#include "CAM/CamPlanEmitter.h"
#include "Engine2D/Machine/MachineFrame.h"
#include "Hardware/Device/MotionPlan.h"

#include <cstddef>
#include <vector>

namespace
{
    /// 构造一条直线段；覆盖字段保持 -1 / <=0 的继承默认值
    Cam::PlanPathSegment camPlanLine(double x0, double y0, double x1, double y1, bool rapid)
    {
        Cam::PlanPathSegment segment;
        segment.type = Cam::PlanPathSegment::Type::Line;
        segment.ptStart = Ut::Vec2d(x0, y0);
        segment.ptEnd = Ut::Vec2d(x1, y1);
        segment.isRapid = rapid;
        return segment;
    }

    /// 构造一个只含一条轮廓的图层
    Cam::PlanLayerResult camPlanLayer(int32_t layerId,
        double powerPercent,
        double speedMmPerSec,
        int32_t passIndex,
        const std::vector<Cam::PlanPathSegment>& segments)
    {
        Cam::PlanLayerResult layer;
        layer.layerId = layerId;
        layer.powerPercent = powerPercent;
        layer.frequencyHz = 20000.0;
        layer.pulseWidthUs = 10.0;
        layer.speedMmPerSec = speedMmPerSec;
        layer.rapidSpeedMmPerSec = 300.0;
        layer.passIndex = passIndex;

        Cam::PlanContour contour;
        contour.segments = segments;
        layer.contours.push_back(contour);
        return layer;
    }

    /// 统计某个 opcode 出现的次数
    size_t camPlanCountOp(const Hw::MotionPlanView& view, Hw::MotionOp op)
    {
        size_t count = 0;
        for (size_t i = 0; i < view.commandCount; ++i)
        {
            if (view.commands[i].op == op)
            {
                ++count;
            }
        }
        return count;
    }

    /// 从 from 起找到第一个指定 opcode，找不到返回 view.commandCount
    size_t camPlanFindOp(const Hw::MotionPlanView& view, Hw::MotionOp op, size_t from)
    {
        for (size_t i = from; i < view.commandCount; ++i)
        {
            if (view.commands[i].op == op)
            {
                return i;
            }
        }
        return view.commandCount;
    }
}  // namespace

TEST(CamPlanEmitterTest, LayerHeader_EmitsBeginLayerThenLaserThenFeed)
{
    Cam::CamPlan plan;
    plan.layers.push_back(camPlanLayer(7, 40.0, 100.0, 0, { camPlanLine(0.0, 0.0, 10.0, 0.0, false) }));

    Hw::MotionPlanBuilder out;
    EXPECT_EQ(CamPlanEmitter::emitCamPlan(plan, Eg::MachineFrame(), out), 1u);

    const Hw::MotionPlanView view = out.view();
    ASSERT_GE(view.commandCount, 8u);

    // 层头顺序：BeginLayer → SetLaserParams → SetFeedRate → SetRapidRate → BeginPass
    EXPECT_EQ(view.commands[0].op, Hw::MotionOp::BeginLayer);
    EXPECT_EQ(view.commands[0].intArg, 7);
    EXPECT_EQ(view.commands[1].op, Hw::MotionOp::SetLaserParams);
    EXPECT_NEAR(view.commands[1].laser.powerPercent, 40.0, 1e-9);
    EXPECT_NEAR(view.commands[1].laser.frequencyHz, 20000.0, 1e-9);
    EXPECT_NEAR(view.commands[1].laser.pulseWidthUs, 10.0, 1e-9);
    EXPECT_EQ(view.commands[2].op, Hw::MotionOp::SetFeedRate);
    EXPECT_NEAR(view.commands[2].rate, 100.0, 1e-9);
    EXPECT_EQ(view.commands[3].op, Hw::MotionOp::SetRapidRate);
    EXPECT_EQ(view.commands[4].op, Hw::MotionOp::BeginPass);
    EXPECT_EQ(view.commands[4].intArg, 0);

    // 图层只有一个出光段：一次 LineTo，最后 LaserOff → EndPass → EndLayer
    EXPECT_EQ(camPlanCountOp(view, Hw::MotionOp::LineTo), 1u);
    EXPECT_EQ(view.commands[view.commandCount - 3].op, Hw::MotionOp::LaserOff);
    EXPECT_EQ(view.commands[view.commandCount - 2].op, Hw::MotionOp::EndPass);
    EXPECT_EQ(view.commands[view.commandCount - 1].op, Hw::MotionOp::EndLayer);

    ASSERT_GT(view.commandCount, 5u);
    EXPECT_EQ(view.commands[5].op, Hw::MotionOp::LineTo);
    EXPECT_TRUE(view.commands[5].laserOn);
    EXPECT_NEAR(view.commands[5].target.v[0], 10.0, 1e-9);
    EXPECT_NEAR(view.commands[5].target.v[1], 0.0, 1e-9);
}

TEST(CamPlanEmitterTest, RapidSegment_IgnoresPowerOverride)
{
    // 双向扫描的典型形态：出光 60% → 停光（CAM 把功率填成 0）→ 出光 60%
    Cam::PlanPathSegment cutting1 = camPlanLine(0.0, 0.0, 10.0, 0.0, false);
    cutting1.powerPercent = 60.0;

    Cam::PlanPathSegment rapid = camPlanLine(10.0, 0.0, 10.0, 1.0, true);
    rapid.powerPercent = 0.0;
    rapid.speedMmPerSec = 100.0;

    Cam::PlanPathSegment cutting2 = camPlanLine(10.0, 1.0, 0.0, 1.0, false);
    cutting2.powerPercent = 60.0;

    Cam::CamPlan plan;
    plan.layers.push_back(camPlanLayer(1, 50.0, 100.0, 0, { cutting1, rapid, cutting2 }));

    Hw::MotionPlanBuilder out;
    ASSERT_EQ(CamPlanEmitter::emitCamPlan(plan, Eg::MachineFrame(), out), 3u);

    const Hw::MotionPlanView view = out.view();

    // SetLaserParams 只应出现两次：图层头 50% + 首段覆盖 60%；
    // 停光段的 0% 覆盖必须被忽略，否则会多出一次 SetLaserParams(0)
    EXPECT_EQ(camPlanCountOp(view, Hw::MotionOp::SetLaserParams), 2u);

    const size_t rapidIndex = camPlanFindOp(view, Hw::MotionOp::RapidTo, 0);
    ASSERT_LT(rapidIndex, view.commandCount);
    EXPECT_NEAR(view.commands[rapidIndex].target.v[0], 10.0, 1e-9);
    EXPECT_NEAR(view.commands[rapidIndex].target.v[1], 1.0, 1e-9);

    // 停光段之后到下一段出光之前，不得插入激光/进给参数
    for (size_t i = rapidIndex + 1; i < view.commandCount; ++i)
    {
        if (view.commands[i].op == Hw::MotionOp::LineTo)
        {
            break;
        }
        EXPECT_NE(view.commands[i].op, Hw::MotionOp::SetLaserParams);
        EXPECT_NE(view.commands[i].op, Hw::MotionOp::SetFeedRate);
    }
}

TEST(CamPlanEmitterTest, PassIndexSwitch_EmitsPairedEndAndBegin)
{
    Cam::PlanPathSegment first = camPlanLine(0.0, 0.0, 10.0, 0.0, false);  // 继承图层 passIndex=0
    Cam::PlanPathSegment second = camPlanLine(10.0, 1.0, 0.0, 1.0, false);
    second.passIndex = 1;
    // 同一遍内连续多段不应重复切换（-1 继承的是图层值 0，不是上一段的 1）
    Cam::PlanPathSegment third = camPlanLine(0.0, 2.0, 10.0, 2.0, false);
    third.passIndex = 1;

    Cam::CamPlan plan;
    plan.layers.push_back(camPlanLayer(3, 50.0, 100.0, 0, { first, second, third }));

    Hw::MotionPlanBuilder out;
    ASSERT_EQ(CamPlanEmitter::emitCamPlan(plan, Eg::MachineFrame(), out), 3u);

    const Hw::MotionPlanView view = out.view();

    // 图层头一遍 + 段间切换一遍
    EXPECT_EQ(camPlanCountOp(view, Hw::MotionOp::BeginPass), 2u);
    EXPECT_EQ(camPlanCountOp(view, Hw::MotionOp::EndPass), 2u);

    const size_t endPass = camPlanFindOp(view, Hw::MotionOp::EndPass, 0);
    ASSERT_LT(endPass + 1, view.commandCount);
    EXPECT_EQ(view.commands[endPass + 1].op, Hw::MotionOp::BeginPass);
    EXPECT_EQ(view.commands[endPass + 1].intArg, 1);
}

TEST(CamPlanEmitterTest, InheritedOverrides_ResolveToLayerValues)
{
    // 段级覆盖全为默认（-1 / <=0）：有效值必须来自 PlanLayerResult
    Cam::CamPlan plan;
    plan.layers.push_back(camPlanLayer(5, 40.0, 100.0, 2, { camPlanLine(0.0, 0.0, 10.0, 0.0, false) }));

    Hw::MotionPlanBuilder out;
    ASSERT_EQ(CamPlanEmitter::emitCamPlan(plan, Eg::MachineFrame(), out), 1u);

    const Hw::MotionPlanView view = out.view();

    // 只应有一个遍块，且遍序号取图层值 2
    EXPECT_EQ(camPlanCountOp(view, Hw::MotionOp::BeginPass), 1u);
    EXPECT_EQ(camPlanCountOp(view, Hw::MotionOp::EndPass), 1u);
    const size_t beginPass = camPlanFindOp(view, Hw::MotionOp::BeginPass, 0);
    ASSERT_LT(beginPass, view.commandCount);
    EXPECT_EQ(view.commands[beginPass].intArg, 2);

    // 功率/进给只来自图层头，段级继承不会额外下发
    EXPECT_EQ(camPlanCountOp(view, Hw::MotionOp::SetLaserParams), 1u);
    EXPECT_EQ(camPlanCountOp(view, Hw::MotionOp::SetFeedRate), 1u);
    EXPECT_NEAR(view.commands[1].laser.powerPercent, 40.0, 1e-9);
    EXPECT_NEAR(view.commands[2].rate, 100.0, 1e-9);
}
