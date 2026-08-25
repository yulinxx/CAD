/**
 * @file ProcessingChainTests.cpp
 * @brief 加工链路测试：MotionPlanCompiler + ProcessingJobService。
 *
 * 这条链路的每一环都会直接作用在真机上（出光、走位），因此测试的重点
 * 不是「happy path 能跑通」，而是**所有失败路径都必须显式失败**：
 *   - 工艺参数非法 → 拒绝，不钳到边界后照常加工；
 *   - 没有可加工内容 → 拒绝，不产出空计划（否则「点了开始什么都不发生」）；
 *   - 勾了「只加工选中」但没选东西 → 拒绝，绝不退化成加工全图（会烧废整块料）；
 *   - 安全门没过 → 拒绝开工与拒绝恢复。
 *
 * 时间推进全部手动（DeviceHost::tick + ProcessingJobService::pollProgress），
 * 不依赖事件循环也不 sleep —— 加工进度的断言必须是确定性的。
 */

#include <gtest/gtest.h>

#include <memory>
#include <vector>

#include <QString>


#include "Hardware/DeviceHost.h"
#include "Hardware/MachineProfile.h"
#include "Hardware/ProcessingJobService.h"
#include "Hardware/ToolpathJobSpec.h"

#include "Engine2D/Core/SceneManager.h"
#include "Engine2D/Interaction/LayerManager.h"
#include "Engine2D/SyEntity/SyCircle.h"
#include "Engine2D/SyEntity/SyLine.h"


#ifdef ENABLE_HARDWARE
#include "Hardware/Device/MotionPlan.h"
#include "Hardware/MotionPlanCompiler.h"
#endif

namespace
{
    // ==================== 纯数据结构：ToolpathJobSpec ====================
    // 这几条不依赖硬件模块，关掉 BUILD_HARDWARE 也要跑，
    // 因为参数校验规则是「上层界面与编译器共用」的契约。

    TEST(LayerProcessParamsTest, DefaultsAreValid)
    {
        EXPECT_TRUE(LayerProcessParams{}.valid());
    }

    TEST(LayerProcessParamsTest, PowerOutOfRangeIsInvalid)
    {
        LayerProcessParams p;
        p.powerPercent = 120.0;
        EXPECT_FALSE(p.valid());

        p.powerPercent = -1.0;
        EXPECT_FALSE(p.valid());
    }

    /// 速度为 0 不是「慢慢走」，而是「原地不动持续出光」—— 直接烧穿。
    TEST(LayerProcessParamsTest, NonPositiveSpeedIsInvalid)
    {
        LayerProcessParams p;
        p.speedMmPerSec = 0.0;
        EXPECT_FALSE(p.valid());
    }

    TEST(LayerProcessParamsTest, PassCountBelowOneIsInvalid)
    {
        LayerProcessParams p;
        p.passes = 0;
        EXPECT_FALSE(p.valid());
    }

    TEST(ToolpathJobSpecTest, DefaultsAreConservative)
    {
        const ToolpathJobSpec spec;
        // 默认必须是「加工全图、跳过隐藏与锁定」
        EXPECT_FALSE(spec.selectionOnly);
        EXPECT_TRUE(spec.skipHiddenLayers);
        EXPECT_TRUE(spec.skipLockedLayers);
        // 每遍收尾补关光，默认必须开
        EXPECT_TRUE(spec.emitTrailingLaserOff);
        EXPECT_GT(spec.chordToleranceMm, 0.0);
    }

#ifdef ENABLE_HARDWARE

    // ==================== 测试辅助 ====================

    /**
     * @brief 往场景里加一条线段，返回**场景内那一份**图元。
     *
     * 必须回读：`SceneManager::addEntity(const SyEntity*)` 不接管所有权，它 clone 一份，
     * 而 `SyEntity` 的拷贝构造刻意重新分配 id（SyEntity.h）。
     * 因此「构造时记下的 id」和「传进去的指针」在 add 之后都指不到场景里的那个对象 ——
     * 拿它们去做图层绑定或选集匹配会全部落空，且不报任何错。
     */
    Eg::SyEntity* addLine(Eg::SceneManager& scene, double x1, double y1, double x2, double y2)
    {
        Eg::SyLine line;
        line.setPointVector({ Ut::Vec2d(x1, y1), Ut::Vec2d(x2, y2) });
        scene.addEntity(&line);

        const std::vector<Eg::SyEntity*> all = scene.getAllEntities();
        return all.empty() ? nullptr : all.back();
    }

    /// 往场景里加一个圆，返回场景内那一份图元。理由同 addLine。
    Eg::SyEntity* addCircle(Eg::SceneManager& scene, double cx, double cy, double r)
    {
        Eg::SyCircle circle;
        circle.basePoint = Ut::Vec2d(cx, cy);
        circle.dRadius = r;
        circle.bClosed = true;
        scene.addEntity(&circle);

        const std::vector<Eg::SyEntity*> all = scene.getAllEntities();
        return all.empty() ? nullptr : all.back();
    }


    /// 统计计划里某个 opcode 出现的次数。
    size_t countOp(const Hw::MotionPlanView& view, Hw::MotionOp op)
    {
        size_t n = 0;
        for (size_t i = 0; i < view.commandCount; ++i)
        {
            if (view.commands[i].op == op)
            {
                ++n;
            }
        }
        return n;
    }

    /// 找到第一条指定 opcode 的下标；找不到返回 SIZE_MAX。
    size_t indexOfOp(const Hw::MotionPlanView& view, Hw::MotionOp op)
    {
        for (size_t i = 0; i < view.commandCount; ++i)
        {
            if (view.commands[i].op == op)
            {
                return i;
            }
        }
        return static_cast<size_t>(-1);
    }

    // ==================== MotionPlanCompiler ====================

    /// 空场景必须失败并给出可展示原因，绝不能返回「成功但计划是空的」。
    TEST(MotionPlanCompilerTest, EmptySceneFailsWithDisplayableReason)
    {
        Eg::SceneManager scene;
        Hw::MotionPlanBuilder builder;

        const ToolpathCompileResult r =
            MotionPlanCompiler::compile(scene, nullptr, ToolpathJobSpec{}, builder);

        EXPECT_FALSE(r.ok);
        EXPECT_FALSE(r.error.isEmpty());
        EXPECT_EQ(0u, builder.commandCount());
    }

    /// 一条线段应产出完整的「图层头 + 遍 + 运动 + 收尾」结构。
    TEST(MotionPlanCompilerTest, SingleLineProducesFullLayerStructure)
    {
        Eg::SceneManager scene;
        addLine(scene, 0.0, 0.0, 10.0, 0.0);

        Hw::MotionPlanBuilder builder;
        const ToolpathCompileResult r =
            MotionPlanCompiler::compile(scene, nullptr, ToolpathJobSpec{}, builder);

        ASSERT_TRUE(r.ok) << r.error.toStdString();
        EXPECT_EQ(1, r.layerCount);
        EXPECT_EQ(1, r.entityCount);

        const Hw::MotionPlanView view = builder.view();
        EXPECT_EQ(1u, countOp(view, Hw::MotionOp::BeginLayer));
        EXPECT_EQ(1u, countOp(view, Hw::MotionOp::EndLayer));
        EXPECT_EQ(1u, countOp(view, Hw::MotionOp::BeginPass));
        EXPECT_EQ(1u, countOp(view, Hw::MotionOp::EndPass));
        EXPECT_EQ(1u, countOp(view, Hw::MotionOp::SetLaserParams));
        EXPECT_EQ(1u, countOp(view, Hw::MotionOp::SetFeedRate));
        EXPECT_EQ(1u, countOp(view, Hw::MotionOp::SetRapidRate));
        EXPECT_EQ(1u, countOp(view, Hw::MotionOp::RapidTo));
        EXPECT_EQ(1u, countOp(view, Hw::MotionOp::LineTo));
        EXPECT_EQ(1u, countOp(view, Hw::MotionOp::LaserOff));
    }

    /// 轮廓的第一次移动必须是不出光的空移。
    /// 若第一段就带出光，激光会从上一个轮廓终点一路烧到本轮廓起点。
    TEST(MotionPlanCompilerTest, FirstMoveIsRapidNotLit)
    {
        Eg::SceneManager scene;
        addLine(scene, 5.0, 5.0, 15.0, 5.0);

        Hw::MotionPlanBuilder builder;
        ASSERT_TRUE(MotionPlanCompiler::compile(scene, nullptr, ToolpathJobSpec{}, builder).ok);

        const Hw::MotionPlanView view = builder.view();
        const size_t rapid = indexOfOp(view, Hw::MotionOp::RapidTo);
        const size_t line = indexOfOp(view, Hw::MotionOp::LineTo);
        ASSERT_NE(static_cast<size_t>(-1), rapid);
        ASSERT_NE(static_cast<size_t>(-1), line);
        EXPECT_LT(rapid, line);
        EXPECT_FALSE(view.commands[rapid].laserOn);
        EXPECT_TRUE(view.commands[line].laserOn);
    }

    /// 图层头写下的工艺参数必须与作业规格逐字一致 —— 这几个数直接决定烧成什么样。
    TEST(MotionPlanCompilerTest, LaserParamsAndRatesMatchSpec)
    {
        Eg::SceneManager scene;
        addLine(scene, 0.0, 0.0, 1.0, 1.0);

        ToolpathJobSpec spec;
        spec.defaultParams.powerPercent = 63.5;
        spec.defaultParams.frequencyHz = 45000.0;
        spec.defaultParams.pulseWidthUs = 12.0;
        spec.defaultParams.waveformIndex = 3;
        spec.defaultParams.speedMmPerSec = 250.0;
        spec.rapidSpeedMmPerSec = 900.0;

        Hw::MotionPlanBuilder builder;
        ASSERT_TRUE(MotionPlanCompiler::compile(scene, nullptr, spec, builder).ok);

        const Hw::MotionPlanView view = builder.view();
        const size_t laserIdx = indexOfOp(view, Hw::MotionOp::SetLaserParams);
        ASSERT_NE(static_cast<size_t>(-1), laserIdx);
        EXPECT_DOUBLE_EQ(63.5, view.commands[laserIdx].laser.powerPercent);
        EXPECT_DOUBLE_EQ(45000.0, view.commands[laserIdx].laser.frequencyHz);
        EXPECT_DOUBLE_EQ(12.0, view.commands[laserIdx].laser.pulseWidthUs);
        EXPECT_EQ(3, view.commands[laserIdx].laser.waveformIndex);

        const size_t feedIdx = indexOfOp(view, Hw::MotionOp::SetFeedRate);
        const size_t rapidRateIdx = indexOfOp(view, Hw::MotionOp::SetRapidRate);
        ASSERT_NE(static_cast<size_t>(-1), feedIdx);
        ASSERT_NE(static_cast<size_t>(-1), rapidRateIdx);
        EXPECT_DOUBLE_EQ(250.0, view.commands[feedIdx].rate);
        EXPECT_DOUBLE_EQ(900.0, view.commands[rapidRateIdx].rate);
    }

    TEST(MotionPlanCompilerTest, InvalidDefaultParamsAreRejected)
    {
        Eg::SceneManager scene;
        addLine(scene, 0.0, 0.0, 1.0, 0.0);

        ToolpathJobSpec spec;
        spec.defaultParams.powerPercent = 150.0;

        Hw::MotionPlanBuilder builder;
        const ToolpathCompileResult r = MotionPlanCompiler::compile(scene, nullptr, spec, builder);
        EXPECT_FALSE(r.ok);
        EXPECT_FALSE(r.error.isEmpty());
    }

    /// 单个图层的参数写错也要拦住，不能只校验默认值。
    TEST(MotionPlanCompilerTest, InvalidPerLayerParamsAreRejected)
    {
        Eg::SceneManager scene;
        addLine(scene, 0.0, 0.0, 1.0, 0.0);

        ToolpathJobSpec spec;
        LayerProcessParams bad;
        bad.speedMmPerSec = -5.0;
        spec.layerParams.insert(0, bad);

        Hw::MotionPlanBuilder builder;
        const ToolpathCompileResult r = MotionPlanCompiler::compile(scene, nullptr, spec, builder);
        EXPECT_FALSE(r.ok);
        EXPECT_TRUE(r.error.contains(QStringLiteral("图层")));
    }

    TEST(MotionPlanCompilerTest, NonPositiveChordToleranceIsRejected)
    {
        Eg::SceneManager scene;
        addCircle(scene, 0.0, 0.0, 5.0);

        ToolpathJobSpec spec;
        spec.chordToleranceMm = 0.0;

        Hw::MotionPlanBuilder builder;
        EXPECT_FALSE(MotionPlanCompiler::compile(scene, nullptr, spec, builder).ok);
    }

    /// 勾了「只加工选中」却没选任何图元：必须失败，
    /// 绝不能退化成加工全图 —— 那会直接烧废整块料。
    TEST(MotionPlanCompilerTest, SelectionOnlyWithEmptySelectionNeverFallsBackToWholeScene)
    {
        Eg::SceneManager scene;
        addLine(scene, 0.0, 0.0, 10.0, 0.0);
        addLine(scene, 0.0, 5.0, 10.0, 5.0);

        ToolpathJobSpec spec;
        spec.selectionOnly = true;

        Hw::MotionPlanBuilder builder;
        const ToolpathCompileResult r = MotionPlanCompiler::compile(scene, nullptr, spec, builder);
        EXPECT_FALSE(r.ok);
        EXPECT_EQ(0u, builder.commandCount());
    }

    TEST(MotionPlanCompilerTest, SelectionOnlyCompilesOnlySelectedEntities)
    {
        Eg::SceneManager scene;
        Eg::SyEntity* keep = addLine(scene, 0.0, 0.0, 10.0, 0.0);
        ASSERT_NE(nullptr, keep);
        addLine(scene, 0.0, 5.0, 10.0, 5.0);
        addLine(scene, 0.0, 9.0, 10.0, 9.0);

        ToolpathJobSpec spec;
        spec.selectionOnly = true;
        spec.selectedEntityIds.append(static_cast<int64_t>(keep->id));

        Hw::MotionPlanBuilder builder;
        const ToolpathCompileResult r = MotionPlanCompiler::compile(scene, nullptr, spec, builder);

        ASSERT_TRUE(r.ok) << r.error.toStdString();
        EXPECT_EQ(1, r.entityCount);
        EXPECT_EQ(1u, countOp(builder.view(), Hw::MotionOp::LineTo));
    }


    /// 多遍加工：每遍一个 Pass 块，且 header 要如实记录总遍数。
    TEST(MotionPlanCompilerTest, MultiplePassesEmitOneBlockEachAndRecordCount)
    {
        Eg::SceneManager scene;
        addLine(scene, 0.0, 0.0, 10.0, 0.0);

        ToolpathJobSpec spec;
        spec.defaultParams.passes = 3;

        Hw::MotionPlanBuilder builder;
        const ToolpathCompileResult r = MotionPlanCompiler::compile(scene, nullptr, spec, builder);

        ASSERT_TRUE(r.ok) << r.error.toStdString();
        const Hw::MotionPlanView view = builder.view();
        EXPECT_EQ(3u, countOp(view, Hw::MotionOp::BeginPass));
        EXPECT_EQ(3u, countOp(view, Hw::MotionOp::EndPass));
        EXPECT_EQ(3u, countOp(view, Hw::MotionOp::LineTo));
        EXPECT_EQ(3, view.header.passCount);
        // 图元数按「几个图元」算，不能被遍数乘大
        EXPECT_EQ(1, r.entityCount);
    }

    /// 图层被禁用（enabled=false）后没有任何内容可加工，必须失败而不是产出空计划。
    TEST(MotionPlanCompilerTest, DisabledLayerLeavesNothingAndFails)
    {
        Eg::SceneManager scene;
        addLine(scene, 0.0, 0.0, 10.0, 0.0);

        ToolpathJobSpec spec;
        LayerProcessParams off;
        off.enabled = false;
        spec.layerParams.insert(0, off);

        Hw::MotionPlanBuilder builder;
        const ToolpathCompileResult r = MotionPlanCompiler::compile(scene, nullptr, spec, builder);

        EXPECT_FALSE(r.ok);
        EXPECT_EQ(0u, builder.commandCount());
        EXPECT_EQ(1, r.skippedEntityCount);
    }

    /// 关掉 emitArcs 后圆必须被离散成折线，一条 ArcTo 都不许剩。
    TEST(MotionPlanCompilerTest, ArcsAreDiscretizedWhenArcOutputDisabled)
    {
        Eg::SceneManager scene;
        addCircle(scene, 0.0, 0.0, 10.0);

        ToolpathJobSpec spec;
        spec.emitArcs = false;

        Hw::MotionPlanBuilder builder;
        ASSERT_TRUE(MotionPlanCompiler::compile(scene, nullptr, spec, builder).ok);

        const Hw::MotionPlanView view = builder.view();
        EXPECT_EQ(0u, countOp(view, Hw::MotionOp::ArcTo));
        EXPECT_GT(countOp(view, Hw::MotionOp::LineTo), 8u);
    }

    /// 圆的离散**不受弦高容差影响** —— 这是 Engine2D 当前的实现事实，不是期望行为。
    ///
    /// BezierAlgorithms::discretizeEntity 对 CIRCLE/ARC/ELLIPSE 走 default 分支，
    /// 那里把 param（GeometryContext::dDiscretize）当成采样点个数，且 <2 时回落到 32。
    /// 所以 0.5 与 0.005 两个容差都得到同样的 32 点 / 31 段。
    /// 这条断言是**留痕的守卫**：哪天 Engine2D 补上了真正的弦高细分，它会失败，
    /// 那时应当把它改回「收紧容差 → 段数变多」，并同步删掉 ToolpathJobSpec.h 上的限制说明。
    TEST(MotionPlanCompilerTest, CircleFlatteningIgnoresToleranceToday)
    {
        Eg::SceneManager scene;
        addCircle(scene, 0.0, 0.0, 20.0);

        ToolpathJobSpec coarse;
        coarse.emitArcs = false;
        coarse.chordToleranceMm = 0.5;

        ToolpathJobSpec fine;
        fine.emitArcs = false;
        fine.chordToleranceMm = 0.005;

        Hw::MotionPlanBuilder coarseBuilder;
        Hw::MotionPlanBuilder fineBuilder;
        ASSERT_TRUE(MotionPlanCompiler::compile(scene, nullptr, coarse, coarseBuilder).ok);
        ASSERT_TRUE(MotionPlanCompiler::compile(scene, nullptr, fine, fineBuilder).ok);

        EXPECT_EQ(countOp(coarseBuilder.view(), Hw::MotionOp::LineTo),
            countOp(fineBuilder.view(), Hw::MotionOp::LineTo));
    }

    /// 过大的容差必须在入口被拒。
    ///
    /// 因为它会被 Engine2D 当成采样点个数：5.0 → 5 个点，一个圆变成五边形，
    /// 而编译照样返回成功。这种计划一旦送去出光就是废料，所以宁可拒绝执行。
    TEST(MotionPlanCompilerTest, AbsurdlyLargeToleranceIsRejected)
    {
        Eg::SceneManager scene;
        addCircle(scene, 0.0, 0.0, 20.0);

        ToolpathJobSpec spec;
        spec.chordToleranceMm = 5.0;

        Hw::MotionPlanBuilder builder;
        const ToolpathCompileResult result = MotionPlanCompiler::compile(scene, nullptr, spec, builder);
        EXPECT_FALSE(result.ok);
        EXPECT_FALSE(result.error.isEmpty());
    }


    TEST(MotionPlanCompilerTest, TrailingLaserOffCanBeDisabled)
    {
        Eg::SceneManager scene;
        addLine(scene, 0.0, 0.0, 10.0, 0.0);

        ToolpathJobSpec spec;
        spec.emitTrailingLaserOff = false;

        Hw::MotionPlanBuilder builder;
        ASSERT_TRUE(MotionPlanCompiler::compile(scene, nullptr, spec, builder).ok);
        EXPECT_EQ(0u, countOp(builder.view(), Hw::MotionOp::LaserOff));
    }

    /// 包围盒必须覆盖几何：设备靠它做下发前的软限位/幅面预检。
    TEST(MotionPlanCompilerTest, BoundsCoverGeometry)
    {
        Eg::SceneManager scene;
        addLine(scene, -3.0, -4.0, 12.0, 7.0);

        Hw::MotionPlanBuilder builder;
        const ToolpathCompileResult r =
            MotionPlanCompiler::compile(scene, nullptr, ToolpathJobSpec{}, builder);

        ASSERT_TRUE(r.ok) << r.error.toStdString();
        EXPECT_LE(r.boundsMinX, -3.0);
        EXPECT_LE(r.boundsMinY, -4.0);
        EXPECT_GE(r.boundsMaxX, 12.0);
        EXPECT_GE(r.boundsMaxY, 7.0);
    }

    /// planId 要带上作业标识：现场对着日志追「是哪一次加工出的问题」全靠它。
    TEST(MotionPlanCompilerTest, PlanIdCarriesJobId)
    {
        Eg::SceneManager scene;
        addLine(scene, 0.0, 0.0, 1.0, 0.0);

        ToolpathJobSpec spec;
        spec.jobId = QStringLiteral("job-42");

        Hw::MotionPlanBuilder builder;
        ASSERT_TRUE(MotionPlanCompiler::compile(scene, nullptr, spec, builder).ok);
        EXPECT_STREQ("job-42", builder.view().header.planId);
    }

    /// 隐藏图层默认跳过；关掉 skipHiddenLayers 后同一份场景应当能编译出来。
    TEST(MotionPlanCompilerTest, HiddenLayerIsSkippedByDefault)
    {
        Eg::SceneManager scene;
        LayerManager layers(&scene);

        const int layerId = layers.createLayer("Cut");
        Eg::SyEntity* line = addLine(scene, 0.0, 0.0, 10.0, 0.0);
        ASSERT_NE(nullptr, line);
        ASSERT_TRUE(layers.assignEntityToLayer(line, layerId));
        ASSERT_TRUE(layers.setLayerVisible(layerId, false));

        Hw::MotionPlanBuilder builder;
        const ToolpathCompileResult hidden =
            MotionPlanCompiler::compile(scene, &layers, ToolpathJobSpec{}, builder);
        EXPECT_FALSE(hidden.ok);

        ToolpathJobSpec ignoreVisibility;
        ignoreVisibility.skipHiddenLayers = false;
        Hw::MotionPlanBuilder builder2;
        const ToolpathCompileResult shown =
            MotionPlanCompiler::compile(scene, &layers, ignoreVisibility, builder2);
        EXPECT_TRUE(shown.ok) << shown.error.toStdString();
    }

    /// 锁定图层默认跳过：锁定的含义是「不许改也不许加工」。
    TEST(MotionPlanCompilerTest, LockedLayerIsSkippedByDefault)
    {
        Eg::SceneManager scene;
        LayerManager layers(&scene);

        const int layerId = layers.createLayer("Locked");
        Eg::SyEntity* line = addLine(scene, 0.0, 0.0, 10.0, 0.0);
        ASSERT_NE(nullptr, line);
        ASSERT_TRUE(layers.assignEntityToLayer(line, layerId));
        ASSERT_TRUE(layers.setLayerLocked(layerId, true));

        Hw::MotionPlanBuilder builder;
        EXPECT_FALSE(MotionPlanCompiler::compile(scene, &layers, ToolpathJobSpec{}, builder).ok);
    }

    /// 多图层各自带独立参数：BeginLayer 的图层号与 SetLaserParams 的功率必须一一对应。
    TEST(MotionPlanCompilerTest, EachLayerCarriesItsOwnParams)
    {
        Eg::SceneManager scene;
        LayerManager layers(&scene);

        const int cut = layers.createLayer("Cut");
        const int engrave = layers.createLayer("Engrave");

        Eg::SyEntity* a = addLine(scene, 0.0, 0.0, 10.0, 0.0);
        ASSERT_NE(nullptr, a);
        ASSERT_TRUE(layers.assignEntityToLayer(a, cut));

        Eg::SyEntity* b = addLine(scene, 0.0, 5.0, 10.0, 5.0);
        ASSERT_NE(nullptr, b);
        ASSERT_TRUE(layers.assignEntityToLayer(b, engrave));

        ToolpathJobSpec spec;
        LayerProcessParams cutParams;
        cutParams.powerPercent = 90.0;
        LayerProcessParams engraveParams;
        engraveParams.powerPercent = 20.0;
        spec.layerParams.insert(cut, cutParams);
        spec.layerParams.insert(engrave, engraveParams);

        Hw::MotionPlanBuilder builder;
        const ToolpathCompileResult r = MotionPlanCompiler::compile(scene, &layers, spec, builder);

        ASSERT_TRUE(r.ok) << r.error.toStdString();
        EXPECT_EQ(2, r.layerCount);
        EXPECT_EQ(2, r.entityCount);

        // 逐条扫描：记录每个 BeginLayer 之后紧跟的功率
        const Hw::MotionPlanView view = builder.view();
        int currentLayer = -1;
        int checked = 0;
        for (size_t i = 0; i < view.commandCount; ++i)
        {
            if (view.commands[i].op == Hw::MotionOp::BeginLayer)
            {
                currentLayer = view.commands[i].intArg;
            }
            else if (view.commands[i].op == Hw::MotionOp::SetLaserParams)
            {
                const double expected = (currentLayer == cut) ? 90.0 : 20.0;
                EXPECT_DOUBLE_EQ(expected, view.commands[i].laser.powerPercent)
                    << "layer=" << currentLayer;
                ++checked;
            }
        }
        EXPECT_EQ(2, checked);
    }


    // ==================== ProcessingJobService ====================

    /**
     * @brief 跑在模拟设备上的加工作业测试。
     *
     * 时间全部手动推进：tick(20) 一拍等价于生产环境默认的 tick 周期。
     *
     * 信号用普通 connect 记录而不是 QSignalSpy：MainTests 没有链接 Qt Test 模块，
     * 而这里只需要「发了几次、带了什么值」，自己记一份反而更直观。
     */
    class ProcessingJobServiceTest : public ::testing::Test
    {
    protected:
        /// 收集作业服务发出的信号。
        struct SignalLog
        {
            int startedCount = 0;
            QString startedJobId;
            int startedCommandCount = 0;

            int progressCount = 0;
            double lastFraction = -1.0;

            int finishedCount = 0;
            bool finishedSuccess = false;
            QString finishedMessage;

            int stateChangedCount = 0;
            int lastState = -1;
        };

        void SetUp() override
        {
            m_host = std::make_unique<DeviceHost>();
            m_job = std::make_unique<ProcessingJobService>(m_host.get());

            QObject::connect(m_job.get(), &ProcessingJobService::jobStarted,
                [this](const QString& jobId, int commandCount) {
                    ++m_log.startedCount;
                    m_log.startedJobId = jobId;
                    m_log.startedCommandCount = commandCount;
                });
            QObject::connect(m_job.get(), &ProcessingJobService::jobProgress,
                [this](double fraction, int, int) {
                    ++m_log.progressCount;
                    m_log.lastFraction = fraction;
                });
            QObject::connect(m_job.get(), &ProcessingJobService::jobFinished,
                [this](bool success, const QString& message) {
                    ++m_log.finishedCount;
                    m_log.finishedSuccess = success;
                    m_log.finishedMessage = message;
                });
            QObject::connect(m_job.get(), &ProcessingJobService::jobStateChanged,
                [this](int state, const QString&) {
                    ++m_log.stateChangedCount;
                    m_log.lastState = state;
                });
        }

        void TearDown() override
        {
            m_job.reset();
            if (m_host)
            {
                m_host->stop();
            }
            m_host.reset();
        }


        /// 启动模拟机并把安全链路推到「允许开工」。
        ///
        /// 为什么要推**两拍**：兜底档案里安全门点位 debounceMs = 20，而
        /// IoPointMap::poll 的去抖是「电平稳定满 debounceMs 才认」——
        /// 第一拍（t=20）只是把变化登记为 pending，第二拍（t=40）才真正采信。
        /// 只推一拍的话，门会一直读作「未关」，裁决停在 Blocked，
        /// 后面所有加工用例都会卡在安全门上而不是它们各自要验的逻辑。
        void startSimulatedDevice()
        {
            QString error;
            ASSERT_TRUE(m_host->start(MachineProfileLoader::builtinSimulatedProfile(), error))
                << error.toStdString();
            m_host->tick(20);
            m_host->tick(20);
            ASSERT_TRUE(m_host->canStartProcessing()) << m_host->safetyViolations().join(';').toStdString();
        }

        /// 编译一份最小可执行计划到 m_builder。
        void buildSimplePlan()
        {
            addLine(m_scene, 0.0, 0.0, 10.0, 0.0);
            ToolpathJobSpec spec;
            spec.jobId = QStringLiteral("test-job");
            ASSERT_TRUE(MotionPlanCompiler::compile(m_scene, nullptr, spec, m_builder).ok);
        }

        /// 推进最多 maxTicks 拍，直到作业结束。
        void runUntilFinished(int maxTicks = 5000)
        {
            for (int i = 0; i < maxTicks; ++i)
            {
                if (!m_job->isRunning() && !m_job->isPaused())
                {
                    return;
                }
                m_host->tick(20);
                m_job->pollProgress();
            }
        }

        std::unique_ptr<DeviceHost> m_host;
        std::unique_ptr<ProcessingJobService> m_job;
        Eg::SceneManager m_scene;
        Hw::MotionPlanBuilder m_builder;
        SignalLog m_log;
    };


    /// 设备没起来时必须拒绝，并给出可展示原因。
    TEST_F(ProcessingJobServiceTest, RefusesWhenDeviceIsNotStarted)
    {
        buildSimplePlan();

        QString error;
        EXPECT_FALSE(m_job->startPlan(m_builder.view(), QStringLiteral("j"), error));
        EXPECT_FALSE(error.isEmpty());
        EXPECT_FALSE(m_job->isRunning());
    }

    /// 空计划必须被拒绝：设备收到空计划的表现是「动一下就结束」，最难排查。
    TEST_F(ProcessingJobServiceTest, RefusesEmptyPlan)
    {
        startSimulatedDevice();

        Hw::MotionPlanView empty;
        QString error;
        EXPECT_FALSE(m_job->startPlan(empty, QStringLiteral("j"), error));
        EXPECT_FALSE(error.isEmpty());
    }

    TEST_F(ProcessingJobServiceTest, StartsPlanAndReportsRunning)
    {
        startSimulatedDevice();
        buildSimplePlan();

        QString error;
        ASSERT_TRUE(m_job->startPlan(m_builder.view(), QStringLiteral("test-job"), error))
            << error.toStdString();

        EXPECT_TRUE(m_job->isRunning());
        EXPECT_FALSE(m_job->isPaused());
        EXPECT_EQ(QStringLiteral("test-job"), m_job->currentJobId());
        EXPECT_EQ(1, m_log.startedCount);
        EXPECT_EQ(QStringLiteral("test-job"), m_log.startedJobId);
        EXPECT_GT(m_log.startedCommandCount, 0);
    }


    /// 已有作业在跑时再次开工必须被拒绝，且不影响正在跑的那份。
    TEST_F(ProcessingJobServiceTest, SecondStartIsRefusedWhileRunning)
    {
        startSimulatedDevice();
        buildSimplePlan();

        QString error;
        ASSERT_TRUE(m_job->startPlan(m_builder.view(), QStringLiteral("first"), error));

        EXPECT_FALSE(m_job->startPlan(m_builder.view(), QStringLiteral("second"), error));
        EXPECT_FALSE(error.isEmpty());
        EXPECT_EQ(QStringLiteral("first"), m_job->currentJobId());
        EXPECT_TRUE(m_job->isRunning());
    }

    /// 完整跑完一份计划：进度必须推进到 1.0，并且以成功结束。
    TEST_F(ProcessingJobServiceTest, ProgressAdvancesAndJobCompletes)
    {
        startSimulatedDevice();
        buildSimplePlan();

        QString error;
        ASSERT_TRUE(m_job->startPlan(m_builder.view(), QStringLiteral("run"), error));

        runUntilFinished();

        EXPECT_FALSE(m_job->isRunning());
        EXPECT_FALSE(m_job->isPaused());
        EXPECT_EQ(1, m_log.finishedCount);
        EXPECT_TRUE(m_log.finishedSuccess);
        EXPECT_GT(m_log.progressCount, 0);
        EXPECT_DOUBLE_EQ(1.0, m_job->progressFraction());
    }


    /// 作业跑完之后必须能再开下一份 —— 状态要收干净。
    TEST_F(ProcessingJobServiceTest, CanStartAgainAfterCompletion)
    {
        startSimulatedDevice();
        buildSimplePlan();

        QString error;
        ASSERT_TRUE(m_job->startPlan(m_builder.view(), QStringLiteral("first"), error));
        runUntilFinished();
        ASSERT_FALSE(m_job->isRunning());

        EXPECT_TRUE(m_job->startPlan(m_builder.view(), QStringLiteral("second"), error))
            << error.toStdString();
        EXPECT_EQ(QStringLiteral("second"), m_job->currentJobId());
    }

    TEST_F(ProcessingJobServiceTest, PauseThenResume)
    {
        startSimulatedDevice();
        buildSimplePlan();

        QString error;
        ASSERT_TRUE(m_job->startPlan(m_builder.view(), QStringLiteral("pausable"), error));

        ASSERT_TRUE(m_job->pauseJob(error)) << error.toStdString();
        EXPECT_TRUE(m_job->isPaused());
        EXPECT_FALSE(m_job->isRunning());

        ASSERT_TRUE(m_job->resumeJob(error)) << error.toStdString();
        EXPECT_TRUE(m_job->isRunning());
        EXPECT_FALSE(m_job->isPaused());
    }

    /// 暂停时进度不许继续推进 —— 否则界面显示的进度是假的。
    TEST_F(ProcessingJobServiceTest, ProgressDoesNotAdvanceWhilePaused)
    {
        startSimulatedDevice();
        buildSimplePlan();

        QString error;
        ASSERT_TRUE(m_job->startPlan(m_builder.view(), QStringLiteral("frozen"), error));
        m_host->tick(20);
        m_job->pollProgress();
        ASSERT_TRUE(m_job->pauseJob(error)) << error.toStdString();

        const double frozen = m_job->progressFraction();
        for (int i = 0; i < 20; ++i)
        {
            m_host->tick(20);
            m_job->pollProgress();
        }
        EXPECT_DOUBLE_EQ(frozen, m_job->progressFraction());
        EXPECT_TRUE(m_job->isPaused());
    }

    TEST_F(ProcessingJobServiceTest, AbortFinishesUnsuccessfully)
    {
        startSimulatedDevice();
        buildSimplePlan();

        QString error;
        ASSERT_TRUE(m_job->startPlan(m_builder.view(), QStringLiteral("abortable"), error));
        ASSERT_TRUE(m_job->abortJob(error)) << error.toStdString();

        EXPECT_FALSE(m_job->isRunning());
        EXPECT_FALSE(m_job->isPaused());
        EXPECT_EQ(1, m_log.finishedCount);
        EXPECT_FALSE(m_log.finishedSuccess);
    }


    /// 没有作业时 pause/resume/abort 都必须明确失败，而不是静默成功。
    TEST_F(ProcessingJobServiceTest, ControlCommandsFailWithoutActiveJob)
    {
        startSimulatedDevice();

        QString error;
        EXPECT_FALSE(m_job->pauseJob(error));
        EXPECT_FALSE(error.isEmpty());

        error.clear();
        EXPECT_FALSE(m_job->resumeJob(error));
        EXPECT_FALSE(error.isEmpty());

        error.clear();
        EXPECT_FALSE(m_job->abortJob(error));
        EXPECT_FALSE(error.isEmpty());
    }

    /// 安全条件引用了不存在的点位（配置写错）→ 点位 invalid →
    /// violateWhenInvalid 默认 true → 禁止开工。这是 fail-safe 的核心断言。
    TEST_F(ProcessingJobServiceTest, RefusesWhenSafetyVerdictBlocks)
    {
        MachineProfile profile;
        profile.deviceId = QStringLiteral("simulated_composite");
        MachineSafetyConditionConfig cond;
        cond.pointName = QStringLiteral("safety.not_wired");
        cond.description = QStringLiteral("未接线的安全点位");
        cond.severity = QStringLiteral("blocked");
        cond.actions = QStringList{ QStringLiteral("block_start") };
        profile.safetyConditions.append(cond);

        QString error;
        ASSERT_TRUE(m_host->start(profile, error)) << error.toStdString();
        m_host->tick(20);
        ASSERT_FALSE(m_host->canStartProcessing());

        buildSimplePlan();
        EXPECT_FALSE(m_job->startPlan(m_builder.view(), QStringLiteral("blocked"), error));
        EXPECT_FALSE(error.isEmpty());
        EXPECT_FALSE(m_job->isRunning());
    }

    /// 加工中安全裁决转为「禁止」时，作业必须被自动暂停 ——
    /// 不依赖安全条件是否配了 pause 动作。
    TEST_F(ProcessingJobServiceTest, SafetyViolationDuringJobPausesIt)
    {
        startSimulatedDevice();
        buildSimplePlan();

        QString error;
        ASSERT_TRUE(m_job->startPlan(m_builder.view(), QStringLiteral("guarded"), error));
        ASSERT_TRUE(m_job->isRunning());

        // 直接发出裁决变化信号：这里验证的是「宿主 → 作业服务」这条接线，
        // 而不是 SafetyMonitor 自身的判定逻辑（后者由 HardwareTests 覆盖）
        emit m_host->safetyVerdictChanged(false, QStringLiteral("安全门被打开"));

        EXPECT_TRUE(m_job->isPaused());
        EXPECT_FALSE(m_job->isRunning());
    }

    /// 暂停期间安全门被破坏后，恢复必须被拒绝：暂停时操作员很可能开门取件了。
    TEST_F(ProcessingJobServiceTest, ResumeIsRefusedWhileSafetyBlocks)
    {
        startSimulatedDevice();
        buildSimplePlan();

        QString error;
        ASSERT_TRUE(m_job->startPlan(m_builder.view(), QStringLiteral("guarded"), error));
        ASSERT_TRUE(m_job->pauseJob(error)) << error.toStdString();

        // 让宿主进入「禁止开工」状态：换一台带未接线安全条件的机器
        m_host->stop();
        MachineProfile blocked;
        blocked.deviceId = QStringLiteral("simulated_composite");
        MachineSafetyConditionConfig cond;
        cond.pointName = QStringLiteral("safety.not_wired");
        cond.severity = QStringLiteral("blocked");
        cond.actions = QStringList{ QStringLiteral("block_start") };
        blocked.safetyConditions.append(cond);
        ASSERT_TRUE(m_host->start(blocked, error)) << error.toStdString();
        m_host->tick(20);
        ASSERT_FALSE(m_host->canStartProcessing());

        error.clear();
        EXPECT_FALSE(m_job->resumeJob(error));
        EXPECT_FALSE(error.isEmpty());
    }

    /// 加工途中设备被 stop（急停后重连、档案重载）：作业必须以失败收尾，
    /// 而不是永远停在「加工中」。
    TEST_F(ProcessingJobServiceTest, DeviceStoppedMidJobFinishesWithFailure)
    {
        startSimulatedDevice();
        buildSimplePlan();

        QString error;
        ASSERT_TRUE(m_job->startPlan(m_builder.view(), QStringLiteral("interrupted"), error));

        m_host->stop();
        m_job->pollProgress();

        EXPECT_FALSE(m_job->isRunning());
        EXPECT_EQ(1, m_log.finishedCount);
        EXPECT_FALSE(m_log.finishedSuccess);
    }


    /// startJob（走几何编译）与 startPlan 走同一条门控与执行路径。
    TEST_F(ProcessingJobServiceTest, StartJobCompilesSceneAndRuns)
    {
        startSimulatedDevice();
        addLine(m_scene, 0.0, 0.0, 20.0, 0.0);
        addCircle(m_scene, 30.0, 0.0, 5.0);

        ToolpathJobSpec spec;
        spec.jobId = QStringLiteral("scene-job");

        QString error;
        ASSERT_TRUE(m_job->startJob(m_scene, nullptr, spec, error)) << error.toStdString();

        EXPECT_TRUE(m_job->isRunning());
        EXPECT_EQ(QStringLiteral("scene-job"), m_job->currentJobId());

        const ToolpathCompileResult compiled = m_job->lastCompileResult();
        EXPECT_TRUE(compiled.ok);
        EXPECT_EQ(2, compiled.entityCount);
        EXPECT_GT(compiled.commandCount, 0u);
    }

    /// 编译失败时不得进入「加工中」，且失败原因要来自编译器。
    TEST_F(ProcessingJobServiceTest, StartJobWithEmptySceneDoesNotEnterRunning)
    {
        startSimulatedDevice();

        QString error;
        EXPECT_FALSE(m_job->startJob(m_scene, nullptr, ToolpathJobSpec{}, error));
        EXPECT_FALSE(error.isEmpty());
        EXPECT_FALSE(m_job->isRunning());
        EXPECT_FALSE(m_job->lastCompileResult().ok);
    }

    /// 门控在编译之前：设备没起来时不应该白白编译一遍十万条指令。
    TEST_F(ProcessingJobServiceTest, StartJobChecksGateBeforeCompiling)
    {
        addLine(m_scene, 0.0, 0.0, 10.0, 0.0);

        QString error;
        EXPECT_FALSE(m_job->startJob(m_scene, nullptr, ToolpathJobSpec{}, error));
        // 没有编译发生，所以上一次编译结果仍是初始的「未成功」
        EXPECT_FALSE(m_job->lastCompileResult().ok);
        EXPECT_TRUE(m_job->lastCompileResult().error.isEmpty());
    }

#endif  // ENABLE_HARDWARE
}
