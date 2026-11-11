/**
 * @file MotionPlanContourOrderTests.cpp
 * @brief 2D 轮廓加工顺序（先内后外）测试，走 MotionPlanCompiler 真实入口。
 *
 * 覆盖：
 *   - 默认值 = 文档顺序，输出与显式关闭逐条一致；
 *   - 开启“先内后外”后，孔先于外轮廓、孔内岛先于孔；
 *   - 同深度的不相交外轮廓保持文档顺序；
 *   - 非法策略值被编译入口拒绝，不静默退化成某一种顺序。
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <vector>

#include "Hardware/ToolpathJobSpec.h"

#include "Engine2D/Core/SceneManager.h"
#include "Engine2D/Core/SceneWriteGateway.h"
#include "Engine2D/AprEntity/AprPolygon.h"

#ifdef ENABLE_HARDWARE
    #include "Hardware/Device/MotionPlan.h"
    #include "Hardware/MotionPlanCompiler.h"

namespace
{
    Eg::AprEntity* orderTestAddRect(Eg::SceneManager& scene,
                                    double x0,
                                    double y0,
                                    double x1,
                                    double y1)
    {
        Eg::AprPolygon poly;
        poly.setVertices({ Ut::Vec2d(x0, y0), Ut::Vec2d(x1, y0),
            Ut::Vec2d(x1, y1), Ut::Vec2d(x0, y1) });
        poly.bClosed = true;
        Eg::SceneWriteGateway::addEntity(scene, &poly);

        const std::vector<Eg::AprEntity*> all = scene.getAllEntities();
        return all.empty() ? nullptr : all.back();
    }

    /// 逐字段比较两条 IR，避免依赖 POD 的 padding。
    bool orderTestSameCommand(const Hw::MotionCommand& a, const Hw::MotionCommand& b)
    {
        if (a.op != b.op || a.laserOn != b.laserOn ||
            a.counterClockwise != b.counterClockwise ||
            a.durationUs != b.durationUs || a.intArg != b.intArg ||
            a.channel != b.channel || a.level != b.level)
        {
            return false;
        }
        if (a.target.count != b.target.count ||
            a.rate != b.rate || a.centerX != b.centerX || a.centerY != b.centerY)
        {
            return false;
        }
        for (int32_t i = 0; i < a.target.count; ++i)
        {
            if (a.target.v[i] != b.target.v[i])
            {
                return false;
            }
        }
        return a.laser.powerPercent == b.laser.powerPercent &&
               a.laser.frequencyHz == b.laser.frequencyHz &&
               a.laser.pulseWidthUs == b.laser.pulseWidthUs &&
               a.laser.waveformIndex == b.laser.waveformIndex;
    }

    /// 第一条出光直线段的终点；没有则返回 false
    bool orderTestFirstLitPoint(const Hw::MotionPlanView& view, Ut::Vec2d& out)
    {
        for (size_t i = 0; i < view.commandCount; ++i)
        {
            const Hw::MotionCommand& c = view.commands[i];
            if (c.op == Hw::MotionOp::LineTo && c.laserOn && c.target.count >= 2)
            {
                out = Ut::Vec2d(c.target.v[0], c.target.v[1]);
                return true;
            }
        }
        return false;
    }

    bool orderTestPointInWindow(const Ut::Vec2d& p,
                                double x0,
                                double y0,
                                double x1,
                                double y1)
    {
        return p.x() >= x0 && p.x() <= x1 && p.y() >= y0 && p.y() <= y1;
    }
}  // namespace

// ============================================================
//  默认值 = 现有行为
// ============================================================

TEST(MotionPlanContourOrderTest, DefaultIsDocumentOrder_ByteIdentical)
{
    Eg::SceneManager sceneA;
    orderTestAddRect(sceneA, 0.0, 0.0, 100.0, 80.0);   // 外轮廓先入文档
    orderTestAddRect(sceneA, 20.0, 20.0, 40.0, 40.0);  // 孔后入文档

    Eg::SceneManager sceneB;
    orderTestAddRect(sceneB, 0.0, 0.0, 100.0, 80.0);
    orderTestAddRect(sceneB, 20.0, 20.0, 40.0, 40.0);

    Hw::MotionPlanBuilder baseline;
    const ToolpathCompileResult a =
        MotionPlanCompiler::compile(sceneA, nullptr, ToolpathJobSpec{}, baseline);
    ASSERT_TRUE(a.ok);

    ToolpathJobSpec explicitDocument;
    explicitDocument.defaultParams.contourOrdering = 0;
    Hw::MotionPlanBuilder documentOrder;
    const ToolpathCompileResult b =
        MotionPlanCompiler::compile(sceneB, nullptr, explicitDocument, documentOrder);
    ASSERT_TRUE(b.ok);

    const Hw::MotionPlanView va = baseline.view();
    const Hw::MotionPlanView vb = documentOrder.view();
    ASSERT_EQ(va.commandCount, vb.commandCount);
    for (size_t i = 0; i < va.commandCount; ++i)
    {
        EXPECT_TRUE(orderTestSameCommand(va.commands[i], vb.commands[i])) << "command #" << i;
    }
    EXPECT_EQ(a.contourOrderingAppliedLayerCount, 0);
    EXPECT_EQ(b.contourOrderingAppliedLayerCount, 0);
}

// ============================================================
//  先内后外
// ============================================================

TEST(MotionPlanContourOrderTest, InnerFirst_CutsHoleBeforeOuter)
{
    Eg::SceneManager scene;
    orderTestAddRect(scene, 0.0, 0.0, 100.0, 80.0);   // 外轮廓
    orderTestAddRect(scene, 20.0, 20.0, 40.0, 40.0);  // 孔

    ToolpathJobSpec spec;
    spec.defaultParams.contourOrdering = 1;

    Hw::MotionPlanBuilder builder;
    const ToolpathCompileResult r = MotionPlanCompiler::compile(scene, nullptr, spec, builder);
    ASSERT_TRUE(r.ok);
    EXPECT_EQ(r.contourOrderingAppliedLayerCount, 1);

    Ut::Vec2d firstLit;
    ASSERT_TRUE(orderTestFirstLitPoint(builder.view(), firstLit));
    // 首刀必须落在孔上（外轮廓已被排到最后）
    EXPECT_TRUE(orderTestPointInWindow(firstLit, 19.0, 19.0, 41.0, 41.0))
        << "first lit point (" << firstLit.x() << ", " << firstLit.y() << ")";
}

TEST(MotionPlanContourOrderTest, InnerFirst_ThreeLevels_IslandHoleOuter)
{
    Eg::SceneManager scene;
    orderTestAddRect(scene, 0.0, 0.0, 100.0, 100.0);    // 外轮廓（深度 0）
    orderTestAddRect(scene, 20.0, 20.0, 80.0, 80.0);    // 孔（深度 1）
    orderTestAddRect(scene, 40.0, 40.0, 60.0, 60.0);    // 孔内岛（深度 2）

    ToolpathJobSpec spec;
    spec.defaultParams.contourOrdering = 1;

    Hw::MotionPlanBuilder builder;
    ASSERT_TRUE(MotionPlanCompiler::compile(scene, nullptr, spec, builder).ok);

    // 首刀应是最深层的岛，而不是文档里最先出现的 100x100 外轮廓
    Ut::Vec2d firstLit;
    ASSERT_TRUE(orderTestFirstLitPoint(builder.view(), firstLit));
    EXPECT_TRUE(orderTestPointInWindow(firstLit, 39.0, 39.0, 61.0, 61.0))
        << "first lit point (" << firstLit.x() << ", " << firstLit.y() << ")";
}

TEST(MotionPlanContourOrderTest, InnerFirst_DisjointOutersKeepDocumentOrder)
{
    Eg::SceneManager scene;
    orderTestAddRect(scene, 0.0, 0.0, 40.0, 40.0);     // 第一件
    orderTestAddRect(scene, 60.0, 0.0, 100.0, 40.0);   // 第二件

    ToolpathJobSpec spec;
    spec.defaultParams.contourOrdering = 1;

    Hw::MotionPlanBuilder builder;
    ASSERT_TRUE(MotionPlanCompiler::compile(scene, nullptr, spec, builder).ok);

    // 同深度必须稳定：第二件不能因为排序被提到第一件前面
    Ut::Vec2d firstLit;
    ASSERT_TRUE(orderTestFirstLitPoint(builder.view(), firstLit));
    EXPECT_TRUE(orderTestPointInWindow(firstLit, -1.0, -1.0, 41.0, 41.0))
        << "first lit point (" << firstLit.x() << ", " << firstLit.y() << ")";
}

// ============================================================
//  非法策略值：拒绝编译，不静默降级
// ============================================================

TEST(MotionPlanContourOrderTest, InvalidStrategyIsRejected)
{
    Eg::SceneManager scene;
    orderTestAddRect(scene, 0.0, 0.0, 100.0, 80.0);

    ToolpathJobSpec spec;
    spec.defaultParams.contourOrdering = 2;

    Hw::MotionPlanBuilder builder;
    const ToolpathCompileResult r = MotionPlanCompiler::compile(scene, nullptr, spec, builder);
    EXPECT_FALSE(r.ok);
    EXPECT_NE(r.error.indexOf(QStringLiteral("contour ordering")), -1) << r.error.toStdString();
}

#endif  // ENABLE_HARDWARE
