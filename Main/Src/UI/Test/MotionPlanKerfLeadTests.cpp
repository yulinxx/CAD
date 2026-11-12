/**
 * @file MotionPlanKerfLeadTests.cpp
 * @brief 生产链割缝补偿 / 引入线接线测试（走 MotionPlanCompiler 真实入口）。
 *
 * 重点：
 *   - 参数关闭时输出必须与接线前完全一致（逐条指令比对）；
 *   - 参数开启时补偿与引入必须真实出现在 MotionPlan 里；
 *   - 孔/外轮廓的补偿方向相反；
 *   - 引入落点必须离开成品边缘。
 *
 * 全部无 GUI、无设备，确定性地检查 IR 指令。
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstring>
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

    size_t countLitLine(const Hw::MotionPlanView& view)
    {
        size_t n = 0;
        for (size_t i = 0; i < view.commandCount; ++i)
        {
            if (view.commands[i].op == Hw::MotionOp::LineTo && view.commands[i].laserOn)
            {
                ++n;
            }
        }
        return n;
    }

    struct Bounds
    {
        double minX = 1e18;
        double minY = 1e18;
        double maxX = -1e18;
        double maxY = -1e18;
        bool valid = false;
    };

    /// 出光直线段的终点包围盒
    Bounds litLineBounds(const Hw::MotionPlanView& view)
    {
        Bounds b;
        for (size_t i = 0; i < view.commandCount; ++i)
        {
            const Hw::MotionCommand& c = view.commands[i];
            if (c.op != Hw::MotionOp::LineTo || !c.laserOn || c.target.count < 2)
            {
                continue;
            }
            b.minX = std::min(b.minX, c.target.v[0]);
            b.minY = std::min(b.minY, c.target.v[1]);
            b.maxX = std::max(b.maxX, c.target.v[0]);
            b.maxY = std::max(b.maxY, c.target.v[1]);
            b.valid = true;
        }
        return b;
    }

    /// 只统计落在指定窗口内的出光点，用于把孔从外轮廓里分离出来
    Bounds litBoundsInWindow(const Hw::MotionPlanView& view,
                             double x0, double y0, double x1, double y1)
    {
        Bounds b;
        for (size_t i = 0; i < view.commandCount; ++i)
        {
            const Hw::MotionCommand& c = view.commands[i];
            if (c.op != Hw::MotionOp::LineTo || !c.laserOn || c.target.count < 2)
            {
                continue;
            }
            const double x = c.target.v[0];
            const double y = c.target.v[1];
            if (x < x0 || x > x1 || y < y0 || y > y1)
            {
                continue;
            }
            b.minX = std::min(b.minX, x);
            b.minY = std::min(b.minY, y);
            b.maxX = std::max(b.maxX, x);
            b.maxY = std::max(b.maxY, y);
            b.valid = true;
        }
        return b;
    }
}  // namespace

// ============================================================
//  默认值 = 现有行为
// ============================================================

TEST(MotionPlanKerfLeadTest, KerfOff_IsByteIdenticalToBaseline)
{
    Eg::SceneManager sceneA;
    addPolygon(sceneA, rectPoints(0.0, 0.0, 100.0, 80.0));

    Eg::SceneManager sceneB;
    addPolygon(sceneB, rectPoints(0.0, 0.0, 100.0, 80.0));

    Hw::MotionPlanBuilder baseline;
    ToolpathJobSpec specA;
    ASSERT_TRUE(compileRect(sceneA, specA, baseline).ok);

    Hw::MotionPlanBuilder explicitOff;
    ToolpathJobSpec specB;
    specB.defaultParams.kerfMm = 0.0;
    specB.defaultParams.leadInEnabled = false;
    specB.defaultParams.leadInLengthMm = 0.0;
    ASSERT_TRUE(compileRect(sceneB, specB, explicitOff).ok);

    const Hw::MotionPlanView a = baseline.view();
    const Hw::MotionPlanView b = explicitOff.view();
    ASSERT_EQ(a.commandCount, b.commandCount);
    for (size_t i = 0; i < a.commandCount; ++i)
    {
        EXPECT_TRUE(MotionTest::sameCommand(a.commands[i], b.commands[i])) << "command #" << i;
    }
}

TEST(MotionPlanKerfLeadTest, LeadInOff_IsByteIdenticalToBaseline)
{
    Eg::SceneManager sceneA;
    addPolygon(sceneA, rectPoints(0.0, 0.0, 100.0, 80.0));
    Eg::SceneManager sceneB;
    addPolygon(sceneB, rectPoints(0.0, 0.0, 100.0, 80.0));

    Hw::MotionPlanBuilder baseline;
    ASSERT_TRUE(compileRect(sceneA, ToolpathJobSpec{}, baseline).ok);

    Hw::MotionPlanBuilder explicitOff;
    ToolpathJobSpec spec;
    spec.defaultParams.leadInEnabled = false;
    spec.defaultParams.leadInLengthMm = 0.0;
    ASSERT_TRUE(compileRect(sceneB, spec, explicitOff).ok);

    const Hw::MotionPlanView a = baseline.view();
    const Hw::MotionPlanView b = explicitOff.view();
    ASSERT_EQ(a.commandCount, b.commandCount);
    for (size_t i = 0; i < a.commandCount; ++i)
    {
        EXPECT_TRUE(MotionTest::sameCommand(a.commands[i], b.commands[i])) << "command #" << i;
    }
}

// ============================================================
//  割缝补偿
// ============================================================

TEST(MotionPlanKerfLeadTest, PositiveKerf_ExpandsOuterContour)
{
    Eg::SceneManager scene;
    addPolygon(scene, rectPoints(0.0, 0.0, 100.0, 80.0));

    ToolpathJobSpec spec;
    spec.defaultParams.kerfMm = 0.5;

    Hw::MotionPlanBuilder builder;
    const ToolpathCompileResult r = compileRect(scene, spec, builder);
    ASSERT_TRUE(r.ok) << r.error.toStdString();
    EXPECT_EQ(1, r.kerfCompensatedContourCount);
    EXPECT_EQ(0, r.kerfDroppedContourCount);

    const Bounds b = litLineBounds(builder.view());
    ASSERT_TRUE(b.valid);
    EXPECT_NEAR(-0.5, b.minX, 1e-4);
    EXPECT_NEAR(-0.5, b.minY, 1e-4);
    EXPECT_NEAR(100.5, b.maxX, 1e-4);
    EXPECT_NEAR(80.5, b.maxY, 1e-4);
}

TEST(MotionPlanKerfLeadTest, NegativeKerf_ShrinksOuterContour)
{
    Eg::SceneManager scene;
    addPolygon(scene, rectPoints(0.0, 0.0, 100.0, 80.0));

    ToolpathJobSpec spec;
    spec.defaultParams.kerfMm = -0.5;  // 负号 = 内补偿

    Hw::MotionPlanBuilder builder;
    ASSERT_TRUE(compileRect(scene, spec, builder).ok);

    const Bounds b = litLineBounds(builder.view());
    ASSERT_TRUE(b.valid);
    EXPECT_NEAR(0.5, b.minX, 1e-4);
    EXPECT_NEAR(0.5, b.minY, 1e-4);
    EXPECT_NEAR(99.5, b.maxX, 1e-4);
    EXPECT_NEAR(79.5, b.maxY, 1e-4);
}

TEST(MotionPlanKerfLeadTest, HoleIsCompensatedInOppositeDirection)
{
    Eg::SceneManager scene;
    addPolygon(scene, rectPoints(0.0, 0.0, 100.0, 80.0));
    // 内孔顺时针（与外轮廓相反的绕向），包含关系由 fillHoleFlags 解析
    addPolygon(scene, { Ut::Vec2d(30.0, 30.0), Ut::Vec2d(30.0, 50.0),
                        Ut::Vec2d(70.0, 50.0), Ut::Vec2d(70.0, 30.0) });

    ToolpathJobSpec spec;
    spec.defaultParams.kerfMm = 0.5;

    Hw::MotionPlanBuilder builder;
    const ToolpathCompileResult r = compileRect(scene, spec, builder);
    ASSERT_TRUE(r.ok) << r.error.toStdString();
    EXPECT_GE(r.kerfCompensatedContourCount, 2);

    // 孔在 (30,30)-(70,50)；正割缝对外环外扩、对孔内缩，孔变为 (30.5,30.5)-(69.5,49.5)
    const Bounds hole = litBoundsInWindow(builder.view(), 25.0, 25.0, 75.0, 55.0);
    ASSERT_TRUE(hole.valid);
    EXPECT_NEAR(30.5, hole.minX, 1e-4);
    EXPECT_NEAR(30.5, hole.minY, 1e-4);
    EXPECT_NEAR(69.5, hole.maxX, 1e-4);
    EXPECT_NEAR(49.5, hole.maxY, 1e-4);
}

// ============================================================
//  引入线
// ============================================================

TEST(MotionPlanKerfLeadTest, TangentLeadIn_AddsLitApproachOutsidePart)
{
    Eg::SceneManager scene;
    addPolygon(scene, rectPoints(0.0, 0.0, 100.0, 80.0));

    ToolpathJobSpec spec;
    spec.defaultParams.leadInEnabled = true;
    spec.defaultParams.leadInLengthMm = 5.0;
    spec.defaultParams.leadInMode = 0;  // 沿切线反向

    Hw::MotionPlanBuilder builder;
    const ToolpathCompileResult r = compileRect(scene, spec, builder);
    ASSERT_TRUE(r.ok) << r.error.toStdString();
    EXPECT_EQ(1, r.leadInAppliedContourCount);
    EXPECT_EQ(0, r.leadInSkippedContourCount);

    const Hw::MotionPlanView view = builder.view();

    // 第一条 RapidTo 必须落在轮廓外 5mm 的引入起点
    size_t rapidIdx = static_cast<size_t>(-1);
    for (size_t i = 0; i < view.commandCount; ++i)
    {
        if (view.commands[i].op == Hw::MotionOp::RapidTo)
        {
            rapidIdx = i;
            break;
        }
    }
    ASSERT_NE(static_cast<size_t>(-1), rapidIdx);
    EXPECT_NEAR(-5.0, view.commands[rapidIdx].target.v[0], 1e-4);
    EXPECT_NEAR(0.0, view.commands[rapidIdx].target.v[1], 1e-4);

    // 紧随其后的出光直线段切回轮廓起点
    ASSERT_LT(rapidIdx + 1, view.commandCount);
    const Hw::MotionCommand& lead = view.commands[rapidIdx + 1];
    EXPECT_EQ(Hw::MotionOp::LineTo, lead.op);
    EXPECT_TRUE(lead.laserOn);
    EXPECT_NEAR(0.0, lead.target.v[0], 1e-4);
    EXPECT_NEAR(0.0, lead.target.v[1], 1e-4);

    // 4 条轮廓边 + 1 条引入线
    EXPECT_EQ(5u, countLitLine(view));
}

TEST(MotionPlanKerfLeadTest, LeadInWithInvalidLengthIsRejected)
{
    Eg::SceneManager scene;
    addPolygon(scene, rectPoints(0.0, 0.0, 10.0, 10.0));

    ToolpathJobSpec spec;
    spec.defaultParams.leadInEnabled = true;
    spec.defaultParams.leadInLengthMm = 0.0;

    Hw::MotionPlanBuilder builder;
    const ToolpathCompileResult r = compileRect(scene, spec, builder);
    EXPECT_FALSE(r.ok);
    EXPECT_FALSE(r.error.isEmpty());
}

// ============================================================
//  mode 语义锁定：2 号必须是同一条「直线」定义
// ============================================================

namespace
{
    /// 第一条 RapidTo 的落点 = 引入线起点
    Ut::Vec2d firstRapidTarget(const Hw::MotionPlanView& view)
    {
        for (size_t i = 0; i < view.commandCount; ++i)
        {
            if (view.commands[i].op == Hw::MotionOp::RapidTo)
            {
                return Ut::Vec2d(view.commands[i].target.v[0], view.commands[i].target.v[1]);
            }
        }
        return Ut::Vec2d(0.0, 0.0);
    }

    Ut::Vec2d compileLeadStart(int32_t mode, double angleDeg)
    {
        Eg::SceneManager scene;
        addPolygon(scene, rectPoints(0.0, 0.0, 100.0, 80.0));

        ToolpathJobSpec spec;
        spec.defaultParams.leadInEnabled = true;
        spec.defaultParams.leadInLengthMm = 5.0;
        spec.defaultParams.leadInMode = mode;
        spec.defaultParams.leadInAngleDeg = angleDeg;

        Hw::MotionPlanBuilder builder;
        EXPECT_TRUE(compileRect(scene, spec, builder).ok);
        return firstRapidTarget(builder.view());
    }
}  // namespace

TEST(MotionPlanKerfLeadTest, NormalLeadIn_LeavesAlongScrapSideNormal)
{
    const Ut::Vec2d start = compileLeadStart(1, 0.0);
    EXPECT_NEAR(0.0, start.x(), 1e-4);
    EXPECT_NEAR(-5.0, start.y(), 1e-4);
}

TEST(MotionPlanKerfLeadTest, StraightLeadIn_DegeneratesToTangentAtZeroAndNormalAtNinety)
{
    // 2 号是「沿 leadInAngleDeg 的直线」这一条定义：
    // 角度 0 必须与切线引入完全重合，90 必须与法向引入完全重合。
    const Ut::Vec2d zero = compileLeadStart(2, 0.0);
    EXPECT_NEAR(-5.0, zero.x(), 1e-4);
    EXPECT_NEAR(0.0, zero.y(), 1e-4);

    const Ut::Vec2d ninety = compileLeadStart(2, 90.0);
    EXPECT_NEAR(0.0, ninety.x(), 1e-4);
    EXPECT_NEAR(-5.0, ninety.y(), 1e-4);
}

TEST(MotionPlanKerfLeadTest, StraightLeadIn_FortyFiveDegreesBisectsTangentAndNormal)
{
    const Ut::Vec2d start = compileLeadStart(2, 45.0);
    const double expected = 5.0 / std::sqrt(2.0);
    EXPECT_NEAR(-expected, start.x(), 1e-4);
    EXPECT_NEAR(-expected, start.y(), 1e-4);
}

TEST(MotionPlanKerfLeadTest, LegacyModeThreeStaysTangent)
{
    // 旧档案里的 leadInMode=3 在生产链一直是切线；映射成 Arc 会改变刀路。
    const Ut::Vec2d three = compileLeadStart(3, 0.0);
    const Ut::Vec2d tangent = compileLeadStart(0, 0.0);
    EXPECT_NEAR(tangent.x(), three.x(), 1e-9);
    EXPECT_NEAR(tangent.y(), three.y(), 1e-9);
}

#endif  // ENABLE_HARDWARE
