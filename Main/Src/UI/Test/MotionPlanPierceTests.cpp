/**
 * @file MotionPlanPierceTests.cpp
 * @brief 生产链穿孔接线测试（走 MotionPlanCompiler 真实入口）。
 *
 * 重点：
 *   - 穿孔关闭（默认）时指令流必须与接线前逐字段一致，且不出现任何 Dwell；
 *   - 穿孔开启时：穿孔点落在切入起点、延时次数与时长正确、统计字段正确；
 *   - pierceOffsetMm 让穿孔点离开成品边缘（无引线且闭合轮廓）；
 *   - 预穿孔不产生任何额外指令。
 *
 * 全部无 GUI、无设备，确定性地检查 IR 指令。
 */

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <vector>

#include "Hardware/ToolpathJobSpec.h"

#include "Engine2D/Core/SceneManager.h"
#include "Engine2D/Core/SceneWriteGateway.h"
#include "Engine2D/AprEntity/AprPolygon.h"

#ifdef ENABLE_HARDWARE
    #include "Hardware/Device/MotionPlan.h"
    #include "Hardware/MotionPlanCompiler.h"
    #include "MotionCommandCompare.h"
    #include "MotionPlanTestScene.h"

namespace
{
    using MotionTest::addPolygon;
    using MotionTest::compileRect;
    using MotionTest::rectPoints;

    size_t countDwell(const Hw::MotionPlanView& view)
    {
        size_t n = 0;
        for (size_t i = 0; i < view.commandCount; ++i)
        {
            if (view.commands[i].op == Hw::MotionOp::Dwell)
            {
                ++n;
            }
        }
        return n;
    }

    /// 第一条空移指令的下标；没有则返回 -1
    long firstRapidIndex(const Hw::MotionPlanView& view)
    {
        for (size_t i = 0; i < view.commandCount; ++i)
        {
            if (view.commands[i].op == Hw::MotionOp::RapidTo)
            {
                return static_cast<long>(i);
            }
        }
        return -1;
    }

    /// 穿孔关闭/预穿孔：完整指令流与基线逐字段相等，且没有任何 Dwell
    void expectSameStreamAsBaseline(const ToolpathJobSpec& spec)
    {
        Eg::SceneManager sceneA;
        addPolygon(sceneA, rectPoints(0.0, 0.0, 100.0, 80.0));
        Eg::SceneManager sceneB;
        addPolygon(sceneB, rectPoints(0.0, 0.0, 100.0, 80.0));

        Hw::MotionPlanBuilder baseline;
        const ToolpathCompileResult baseResult =
            compileRect(sceneA, ToolpathJobSpec{}, baseline);
        ASSERT_TRUE(baseResult.ok) << baseResult.error.toStdString();
        ASSERT_EQ(0u, countDwell(baseline.view()));

        Hw::MotionPlanBuilder current;
        const ToolpathCompileResult result = compileRect(sceneB, spec, current);
        ASSERT_TRUE(result.ok) << result.error.toStdString();
        EXPECT_EQ(0, result.pierceAppliedContourCount);
        EXPECT_EQ(0, result.pierceDwellSegmentCount);
        EXPECT_EQ(0u, countDwell(current.view()));

        const Hw::MotionPlanView a = baseline.view();
        const Hw::MotionPlanView b = current.view();
        ASSERT_EQ(a.commandCount, b.commandCount);
        for (size_t i = 0; i < a.commandCount; ++i)
        {
            EXPECT_TRUE(MotionTest::sameCommand(a.commands[i], b.commands[i])) << "command #" << i;
        }
    }
}  // namespace

// ============================================================
//  关闭 = 现有行为
// ============================================================

TEST(MotionPlanPierceTest, Disabled_IsByteIdenticalToBaseline)
{
    // 即便填了非零延时/次数，只要 enabled=false 就不允许产生任何指令
    ToolpathJobSpec spec;
    spec.defaultPierce.enabled = false;
    spec.defaultPierce.pierceDelayUs = 800;
    spec.defaultPierce.pierceCount = 4;
    spec.defaultPierce.dwellUs = 300;
    spec.defaultPierce.pierceOffsetMm = 2.0;
    expectSameStreamAsBaseline(spec);
}

TEST(MotionPlanPierceTest, PrePierced_IsByteIdenticalToBaseline)
{
    ToolpathJobSpec spec;
    spec.defaultPierce.enabled = true;
    spec.defaultPierce.prePierced = true;
    spec.defaultPierce.pierceDelayUs = 800;
    spec.defaultPierce.pierceCount = 4;
    spec.defaultPierce.pierceOffsetMm = 2.0;
    expectSameStreamAsBaseline(spec);
}

// ============================================================
//  开启 = 真实穿孔
// ============================================================

TEST(MotionPlanPierceTest, Enabled_InsertsDwellAtCutStartAndRestoresLayerParams)
{
    Eg::SceneManager scene;
    addPolygon(scene, rectPoints(0.0, 0.0, 100.0, 80.0));

    ToolpathJobSpec spec;
    spec.defaultPierce.enabled = true;
    spec.defaultPierce.pierceDelayUs = 500;
    spec.defaultPierce.pierceCount = 2;
    spec.defaultPierce.dwellUs = 200;
    spec.defaultPierce.piercePowerPercent = 30.0;
    spec.defaultPierce.pierceSpeedMmPerSec = 50.0;

    Hw::MotionPlanBuilder builder;
    const ToolpathCompileResult r = compileRect(scene, spec, builder);
    ASSERT_TRUE(r.ok) << r.error.toStdString();

    // 1 条闭合矩形轮廓；穿孔延时 = 2 × pierceDelay + 1 × dwell
    EXPECT_EQ(1, r.pierceAppliedContourCount);
    EXPECT_EQ(3, r.pierceDwellSegmentCount);

    const Hw::MotionPlanView view = builder.view();
    ASSERT_EQ(3u, countDwell(view));

    const long rapid = firstRapidIndex(view);
    ASSERT_GE(rapid, 0);
    // 穿孔点 = 切入起点（无引线时即轮廓起点）
    EXPECT_NEAR(0.0, view.commands[rapid].target.v[0], 1e-4);
    EXPECT_NEAR(0.0, view.commands[rapid].target.v[1], 1e-4);

    // 穿孔功率/速度先于延时下发
    ASSERT_LT(static_cast<size_t>(rapid) + 2, view.commandCount);
    EXPECT_EQ(Hw::MotionOp::SetLaserParams, view.commands[rapid + 1].op);
    EXPECT_NEAR(30.0, view.commands[rapid + 1].laser.powerPercent, 1e-9);
    EXPECT_EQ(Hw::MotionOp::SetFeedRate, view.commands[rapid + 2].op);
    EXPECT_NEAR(50.0, view.commands[rapid + 2].rate, 1e-9);

    // 2 次打孔延时 + 1 次稳定延时
    EXPECT_EQ(Hw::MotionOp::Dwell, view.commands[rapid + 3].op);
    EXPECT_EQ(500, view.commands[rapid + 3].durationUs);
    EXPECT_EQ(Hw::MotionOp::Dwell, view.commands[rapid + 4].op);
    EXPECT_EQ(500, view.commands[rapid + 4].durationUs);
    EXPECT_EQ(Hw::MotionOp::Dwell, view.commands[rapid + 5].op);
    EXPECT_EQ(200, view.commands[rapid + 5].durationUs);

    // 首条切削段：从轮廓起点出光到 (100, 0)
    ASSERT_LT(static_cast<size_t>(rapid) + 6, view.commandCount);
    EXPECT_EQ(Hw::MotionOp::LineTo, view.commands[rapid + 6].op);
    EXPECT_TRUE(view.commands[rapid + 6].laserOn);
    EXPECT_NEAR(100.0, view.commands[rapid + 6].target.v[0], 1e-4);
    EXPECT_NEAR(0.0, view.commands[rapid + 6].target.v[1], 1e-4);

    // 首条切削段之后恢复层参数（50% / 100mm/s）
    ASSERT_LT(static_cast<size_t>(rapid) + 8, view.commandCount);
    EXPECT_EQ(Hw::MotionOp::SetLaserParams, view.commands[rapid + 7].op);
    EXPECT_NEAR(50.0, view.commands[rapid + 7].laser.powerPercent, 1e-9);
    EXPECT_EQ(Hw::MotionOp::SetFeedRate, view.commands[rapid + 8].op);
    EXPECT_NEAR(100.0, view.commands[rapid + 8].rate, 1e-9);
}

TEST(MotionPlanPierceTest, Offset_MovesPiercePointOffPartEdge)
{
    Eg::SceneManager scene;
    addPolygon(scene, rectPoints(0.0, 0.0, 100.0, 80.0));

    ToolpathJobSpec spec;
    spec.defaultPierce.enabled = true;
    spec.defaultPierce.pierceDelayUs = 300;
    spec.defaultPierce.pierceCount = 1;
    spec.defaultPierce.dwellUs = 0;
    spec.defaultPierce.pierceOffsetMm = 2.0;

    Hw::MotionPlanBuilder builder;
    const ToolpathCompileResult r = compileRect(scene, spec, builder);
    ASSERT_TRUE(r.ok) << r.error.toStdString();
    EXPECT_EQ(1, r.pierceAppliedContourCount);
    EXPECT_EQ(1, r.pierceDwellSegmentCount);

    const Hw::MotionPlanView view = builder.view();
    const long rapid = firstRapidIndex(view);
    ASSERT_GE(rapid, 0);
    // 沿废料侧法向外移 2mm：矩形起点的外侧是 -Y
    EXPECT_NEAR(0.0, view.commands[rapid].target.v[0], 1e-4);
    EXPECT_NEAR(-2.0, view.commands[rapid].target.v[1], 1e-4);

    // 穿孔延时后就近切入轮廓起点（出光直线）
    const size_t dwellIdx = static_cast<size_t>(rapid) + 3;  // Rapid, SetLaser, SetFeed, Dwell
    ASSERT_LT(dwellIdx + 1, view.commandCount);
    EXPECT_EQ(Hw::MotionOp::Dwell, view.commands[dwellIdx].op);
    const Hw::MotionCommand& join = view.commands[dwellIdx + 1];
    EXPECT_EQ(Hw::MotionOp::LineTo, join.op);
    EXPECT_TRUE(join.laserOn);
    EXPECT_NEAR(0.0, join.target.v[0], 1e-4);
    EXPECT_NEAR(0.0, join.target.v[1], 1e-4);
}

TEST(MotionPlanPierceTest, LeadIn_PiercesAtLeadInStart)
{
    Eg::SceneManager scene;
    addPolygon(scene, rectPoints(0.0, 0.0, 100.0, 80.0));

    ToolpathJobSpec spec;
    spec.defaultParams.leadInEnabled = true;
    spec.defaultParams.leadInLengthMm = 5.0;
    spec.defaultParams.leadInMode = 0;  // 切线反向
    spec.defaultPierce.enabled = true;
    spec.defaultPierce.pierceDelayUs = 500;
    spec.defaultPierce.pierceCount = 1;
    spec.defaultPierce.dwellUs = 0;

    Hw::MotionPlanBuilder builder;
    const ToolpathCompileResult r = compileRect(scene, spec, builder);
    ASSERT_TRUE(r.ok) << r.error.toStdString();
    EXPECT_EQ(1, r.pierceAppliedContourCount);
    EXPECT_EQ(1, r.pierceDwellSegmentCount);

    const Hw::MotionPlanView view = builder.view();
    const long rapid = firstRapidIndex(view);
    ASSERT_GE(rapid, 0);
    // 穿孔点 = 引线起点，而不是轮廓起点
    EXPECT_NEAR(-5.0, view.commands[rapid].target.v[0], 1e-4);
    EXPECT_NEAR(0.0, view.commands[rapid].target.v[1], 1e-4);

    // 延时之后是引线出光段，切回轮廓起点
    const size_t dwellIdx = static_cast<size_t>(rapid) + 3;
    ASSERT_LT(dwellIdx + 1, view.commandCount);
    EXPECT_EQ(Hw::MotionOp::Dwell, view.commands[dwellIdx].op);
    const Hw::MotionCommand& lead = view.commands[dwellIdx + 1];
    EXPECT_EQ(Hw::MotionOp::LineTo, lead.op);
    EXPECT_TRUE(lead.laserOn);
    EXPECT_NEAR(0.0, lead.target.v[0], 1e-4);
    EXPECT_NEAR(0.0, lead.target.v[1], 1e-4);
}

TEST(MotionPlanPierceTest, LayerOverride_TakesPrecedenceOverDefault)
{
    Eg::SceneManager scene;
    addPolygon(scene, rectPoints(0.0, 0.0, 100.0, 80.0));

    ToolpathJobSpec spec;
    spec.defaultPierce.enabled = false;

    Eg::PierceSpec layerPierce;
    layerPierce.enabled = true;
    layerPierce.pierceDelayUs = 400;
    layerPierce.pierceCount = 3;
    layerPierce.dwellUs = 0;
    // 未归层图元落在 layerId=0
    spec.layerPierce.insert(0, layerPierce);

    Hw::MotionPlanBuilder builder;
    const ToolpathCompileResult r = compileRect(scene, spec, builder);
    ASSERT_TRUE(r.ok) << r.error.toStdString();
    EXPECT_EQ(1, r.pierceAppliedContourCount);
    EXPECT_EQ(3, r.pierceDwellSegmentCount);
}

#endif  // ENABLE_HARDWARE
