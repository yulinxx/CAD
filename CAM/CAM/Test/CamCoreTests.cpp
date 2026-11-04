#define _USE_MATH_DEFINES

/**
 * @file CamCoreTests.cpp
 * @brief CAM 模块单元测试（M0）。
 *
 * 测试目标：
 *   1. 公开结构体默认构造 + C++ 内部 ABI 自检
 *   2. 直线/圆弧段编译产出非空
 *   3. 长度与空移统计正确
 *   4. 取消提前退出（不产出半成品）
 *   5. 进度回调有推进
 *   6. 输入校验边界（空作业、非法参数、弦高容差）
 *   7. failOnUnmachinable 行为
 *   8. 包围盒
 */

#include "CAM/CamCore.h"

#include <gtest/gtest.h>

using namespace Cam;
using Ut::Vec2d;

// ============================================================
//  辅助：构造一个包含 100mm 正方形轮廓的 ToolpathJobInput
// ============================================================

static ToolpathJobInput makeSquareJob(double sizeMm)
{
    ToolpathJobInput input;
    std::strncpy(input.jobId, "test_square", sizeof(input.jobId) - 1);
    input.jobId[sizeof(input.jobId) - 1] = '\0';
    input.selectionOnly = false;
    input.emitArcs = true;
    input.skipHiddenLayers = false;
    input.skipLockedLayers = false;
    input.failOnUnmachinable = true;

    ToolpathJobInput::LayerBucket layer;
    layer.layerId = 1;
    layer.visible = true;
    layer.locked = false;
    layer.outputEnabled = true;
    layer.params.powerPercent = 50.0;
    layer.params.speedMmPerSec = 200.0;
    layer.params.rapidSpeedMmPerSec = 300.0;
    layer.params.passes = 1;
    layer.params.chordToleranceMm = 0.01;
    layer.params.valid();

    EntitySnapshot entity;
    entity.kind = EntitySnapshot::GeometryKind::Contour;
    entity.entityId = 101;
    entity.layerId = 1;

    // 正方形：从 (0,0) → (100,0) → (100,100) → (0,100) → (0,0)（闭合）
    Contour square;
    square.closed = true;
    square.hole = false;
    square.sourceEntityId = 101;
    square.sourceLayerId = 1;

    auto addLine = [&](const Vec2d& start, const Vec2d& end) {
        PathSegment seg;
        seg.type = PathSegment::Type::Line;
        seg.ptStart = start;
        seg.ptEnd = end;
        seg.closed = false;
        square.segments.push_back(seg);
    };

    addLine({0.0, 0.0}, {sizeMm, 0.0});
    addLine({sizeMm, 0.0}, {sizeMm, sizeMm});
    addLine({sizeMm, sizeMm}, {0.0, sizeMm});
    addLine({0.0, sizeMm}, {0.0, 0.0});

    entity.contours.push_back(std::move(square));
    layer.entities.push_back(std::move(entity));
    input.layers.push_back(std::move(layer));

    return input;
}

// ============================================================
//  公开结构体默认构造：容器为空、数值字段有合法初值
// ============================================================

TEST(CamCoreTests, PublicStructs_DefaultConstructToEmpty)
{
    PathSegment seg;
    EXPECT_EQ(seg.type, PathSegment::Type::Line);
    EXPECT_TRUE(seg.counterClockwise);
    EXPECT_FALSE(seg.closed);

    Contour contour;
    EXPECT_TRUE(contour.segments.empty());
    EXPECT_FALSE(contour.closed);
    EXPECT_FALSE(contour.hole);

    EntitySnapshot entity;
    EXPECT_EQ(entity.kind, EntitySnapshot::GeometryKind::Contour);
    EXPECT_TRUE(entity.contours.empty());
    EXPECT_TRUE(entity.visible);
    EXPECT_FALSE(entity.locked);
    EXPECT_TRUE(entity.unmachinableReason.empty());

    ProcessParams params;
    EXPECT_TRUE(params.valid());

    ToolpathJobInput::LayerBucket layer;
    EXPECT_TRUE(layer.entities.empty());
    EXPECT_TRUE(layer.params.valid());

    ToolpathJobInput job;
    EXPECT_TRUE(job.layers.empty());

    CamPlan plan;
    EXPECT_TRUE(plan.layers.empty());
}

// ============================================================
//  ABI 自检：版本号与关键结构 sizeof 必须与公开头一致
// ============================================================

TEST(CamCoreTests, AbiSelfCheck_MatchesPublicStructSizes)
{
    const CamAbiInfo info = camAbiInfo();
    EXPECT_EQ(info.version, CAM_ABI_VERSION);
    EXPECT_GT(info.version, 0);

    // 自检函数必须如实报告当前布局；新增/调整公开结构体后漏改这里即失败
    EXPECT_EQ(info.sizeofPathSegment, sizeof(PathSegment));
    EXPECT_EQ(info.sizeofContour, sizeof(Contour));
    EXPECT_EQ(info.sizeofEntitySnapshot, sizeof(EntitySnapshot));
    EXPECT_EQ(info.sizeofToolpathJobInput, sizeof(ToolpathJobInput));
    EXPECT_EQ(info.sizeofCamPlan, sizeof(CamPlan));
    EXPECT_EQ(info.sizeofCamResult, sizeof(CamResult));
}

// ============================================================
//  直线段编译：100mm 正方形
// ============================================================

TEST(CamCoreTests, CompileLineSquare_ProducesValidPlan)
{
    ToolpathJobInput input = makeSquareJob(100.0);
    CamPlan plan;
    CamResult result;

    CamResult actual = compile(input, plan, nullptr, &result);
    EXPECT_TRUE(result.ok);
    EXPECT_EQ(result.error, CamError::Ok);
    EXPECT_STREQ(result.errorText, camErrorName(CamError::Ok));
    EXPECT_EQ(result.layerCount, 1);
    EXPECT_EQ(result.contourCount, 1);
    EXPECT_EQ(result.entityCount, 1);

    // 轮廓段数：1 rapid（0,0→0,0 距离=0 不插）+ 4 line segments
    // 起点 currentPosition 初始 (0,0)，轮廓首点也是 (0,0)，距离为 0，不插 rapid
    ASSERT_EQ(plan.layers.size(), 1);
    const auto& firstLayer = plan.layers.front();
    ASSERT_EQ(firstLayer.contours.size(), 1);
    const auto& contour = firstLayer.contours.front();
    EXPECT_EQ(contour.segments.size(), 4u);  // 4 cutting segments, no rapid
    for (const auto& seg : contour.segments)
    {
        EXPECT_FALSE(seg.isRapid);
    }

    // 总 cutting length = 4 * 100mm = 400mm
    EXPECT_NEAR(plan.totalCuttingLengthMm, 400.0, 1e-3);
    EXPECT_NEAR(contour.cuttingLengthMm, 400.0, 1e-3);
    EXPECT_NEAR(contour.rapidLengthMm, 0.0, 1e-3);  // 起点和 currentPosition 重合

    // 包围盒
    EXPECT_NEAR(plan.boundsMinX, 0.0, 1e-6);
    EXPECT_NEAR(plan.boundsMinY, 0.0, 1e-6);
    EXPECT_NEAR(plan.boundsMaxX, 100.0, 1e-6);
    EXPECT_NEAR(plan.boundsMaxY, 100.0, 1e-6);
}

// ============================================================
//  圆弧段编译
// ============================================================

TEST(CamCoreTests, CompileArc_ProducesArcSegment)
{
    ToolpathJobInput input;
    std::strncpy(input.jobId, "test_arc", sizeof(input.jobId) - 1);
    input.emitArcs = true;
    input.failOnUnmachinable = true;

    ToolpathJobInput::LayerBucket layer;
    layer.layerId = 2;
    layer.params.powerPercent = 50.0;
    layer.params.speedMmPerSec = 200.0;
    layer.params.valid();

    EntitySnapshot entity;
    entity.kind = EntitySnapshot::GeometryKind::Contour;
    entity.entityId = 201;
    entity.layerId = 2;

    Contour contour;
    contour.closed = false;
    contour.sourceEntityId = 201;

    // 半圆：圆心 (10,10)，半径 10，起点 (-0°, 逆时针扫过 180° 到 180°)
    PathSegment arc;
    arc.type = PathSegment::Type::Arc;
    arc.ptCenter = Vec2d(10.0, 10.0);
    arc.dRadius = 10.0;
    arc.ptStart = Vec2d(20.0, 10.0);   // 0°
    arc.ptEnd = Vec2d(0.0, 10.0);      // 180°
    arc.dStartAngle = 0.0;
    arc.dEndAngle = 180.0;
    arc.counterClockwise = true;        // 逆时针：0→180
    contour.segments.push_back(arc);

    entity.contours.push_back(std::move(contour));
    layer.entities.push_back(std::move(entity));
    input.layers.push_back(std::move(layer));

    CamPlan plan;
    CamResult result = compile(input, plan);
    EXPECT_TRUE(result.ok);
    ASSERT_EQ(plan.layers.size(), 1);
    const auto& c = plan.layers.front().contours.front();
    // 圆弧起点 (20,10) 和 currentPosition (0,0) 距离非零，CAM 自动插 Rapid 段
    ASSERT_EQ(c.segments.size(), 2u);
    EXPECT_TRUE(c.segments.front().isRapid);          // 第 1 段是 Rapid
    EXPECT_EQ(c.segments.back().type, PlanPathSegment::Type::Arc);  // 第 2 段是 Arc
    EXPECT_FALSE(c.segments.back().isRapid);

    // CAM 契约的角度单位是度：输入 0/180 度必须原样进入 Plan，不得被再次换算
    EXPECT_NEAR(c.segments.back().dStartAngle, 0.0, 1e-9);
    EXPECT_NEAR(c.segments.back().dEndAngle, 180.0, 1e-9);

    // 半圆长度 = pi * r = pi * 10 ≈ 31.4159
    const double expectedLen = M_PI * 10.0;
    EXPECT_NEAR(plan.totalCuttingLengthMm, expectedLen, 1e-3);
}

// ============================================================
//  两条轮廓：验证排序 + 空移段
// ============================================================

TEST(CamCoreTests, MultipleContours_InsertRapidBetween)
{
    ToolpathJobInput input;
    input.failOnUnmachinable = true;

    ToolpathJobInput::LayerBucket layer;
    layer.layerId = 3;
    layer.params.powerPercent = 50.0;
    layer.params.speedMmPerSec = 200.0;
    layer.params.valid();

    // entity A: 正方形在 (50, 50)，entity B: 正方形在 (200, 200)
    auto addContourEntity = [&](uint64_t eid, const Vec2d& bottomLeft, double size) {
        EntitySnapshot e;
        e.kind = EntitySnapshot::GeometryKind::Contour;
        e.entityId = eid;
        e.layerId = 3;

        Contour c;
        c.sourceEntityId = eid;
        c.sourceLayerId = 3;

        const double x = bottomLeft.x();
        const double y = bottomLeft.y();
        PathSegment s1, s2, s3, s4;
        s1.type = PathSegment::Type::Line;
        s1.ptStart = Vec2d(x, y); s1.ptEnd = Vec2d(x + size, y);
        s2.type = PathSegment::Type::Line;
        s2.ptStart = Vec2d(x + size, y); s2.ptEnd = Vec2d(x + size, y + size);
        s3.type = PathSegment::Type::Line;
        s3.ptStart = Vec2d(x + size, y + size); s3.ptEnd = Vec2d(x, y + size);
        s4.type = PathSegment::Type::Line;
        s4.ptStart = Vec2d(x, y + size); s4.ptEnd = Vec2d(x, y);
        c.segments = {s1, s2, s3, s4};
        e.contours.push_back(c);
        layer.entities.push_back(e);
    };

    addContourEntity(1001, {50.0, 50.0}, 10.0);   // id 1001，起点 (50,50)
    addContourEntity(1002, {200.0, 200.0}, 10.0);  // id 1002，起点 (200,200)

    input.layers.push_back(layer);

    CamPlan plan;
    CamResult result = compile(input, plan);
    EXPECT_TRUE(result.ok);

    // M0 按 sourceEntityId 排序 → 1001 先，1002 后
    ASSERT_EQ(plan.layers.size(), 1);
    const auto& layerResult = plan.layers.front();
    ASSERT_EQ(layerResult.contours.size(), 2);

    const auto& first = layerResult.contours.front();   // entity 1001
    const auto& second = layerResult.contours.back();    // entity 1002

    // first: 起点 currentPosition=(0,0) → (50,50) 距离 ~70.7，插 rapid
    // rapid 段 ptEnd=(50,50) 应该是 cutting 的 ptStart
    ASSERT_GE(first.segments.size(), 5u);  // 1 rapid + 4 cutting
    EXPECT_TRUE(first.segments.front().isRapid);
    EXPECT_EQ(first.segments.front().ptEnd.x(), 50.0);
    EXPECT_EQ(first.segments.front().ptEnd.y(), 50.0);

    // second: 起点 currentPosition=(50,50) → (200,200) 距离 ~212，插 rapid
    ASSERT_GE(second.segments.size(), 5u);
    EXPECT_TRUE(second.segments.front().isRapid);
    EXPECT_EQ(second.segments.front().ptEnd.x(), 200.0);
    EXPECT_EQ(second.segments.front().ptEnd.y(), 200.0);

    // 总空移 = rapid1 + rapid2
    // rapid1: (0,0)→(50,50) = sqrt(50^2+50^2) = 70.71
    // rapid2: (50,50)→(200,200) = sqrt(150^2+150^2) = 212.13
    // 但 first 的 cutting 结束在 (50,50)，currentPosition 更新到那里，second 的 rapid 从 (50,50) 开始
    EXPECT_NEAR(first.rapidLengthMm, std::sqrt(50.0 * 50.0 * 2.0), 1e-3);
    EXPECT_NEAR(second.rapidLengthMm, std::sqrt(150.0 * 150.0 * 2.0), 1e-3);
    EXPECT_NEAR(plan.totalRapidLengthMm,
                std::sqrt(50.0 * 50.0 * 2.0) + std::sqrt(150.0 * 150.0 * 2.0),
                1e-3);
}

// ============================================================
//  输入校验：空作业
// ============================================================

TEST(CamCoreTests, EmptyJob_ReturnsEmptyJob)
{
    ToolpathJobInput input;
    input.failOnUnmachinable = true;

    ToolpathJobInput::LayerBucket layer;
    layer.layerId = 1;
    layer.visible = true;
    layer.outputEnabled = true;
    input.layers.push_back(layer);  // layer 存在但无 entities

    CamPlan plan;
    CamResult result = compile(input, plan);
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.error, CamError::EmptyJob);
}

// ============================================================
//  输入校验：非法 ProcessParams
// ============================================================

TEST(CamCoreTests, InvalidParams_ReturnsInvalid)
{
    ToolpathJobInput input = makeSquareJob(100.0);
    // 把速度设成 0
    input.layers.front().params.speedMmPerSec = 0.0;
    EXPECT_FALSE(input.layers.front().params.valid());

    CamPlan plan;
    CamResult result = compile(input, plan);
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.error, CamError::InvalidParams);
}

// ============================================================
//  输入校验：弦高容差过大（防单位填错）
// ============================================================

TEST(CamCoreTests, ChordToleranceTooLarge_ReturnsError)
{
    ToolpathJobInput input = makeSquareJob(100.0);
    input.layers.front().params.chordToleranceMm = 10.0;  // 远大于 1.0mm
    // valid() 里要求 chordToleranceMm <= 1.0，所以直接 InvalidParams
    EXPECT_FALSE(input.layers.front().params.valid());

    CamPlan plan;
    CamResult result = compile(input, plan);
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.error, CamError::InvalidParams);
}

// ============================================================
//  取消：progress.cancelled = 1
// ============================================================

TEST(CamCoreTests, Cancelled_ReturnsCancelledAndNoPartialPlan)
{
    ToolpathJobInput input = makeSquareJob(100.0);
    CamPlan plan;

    Progress progress;
    progress.cancelled = 1;  // 一开始就置 1

    CamResult result = compile(input, plan, &progress);
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.error, CamError::Cancelled);
    // plan 必须空 —— 不允许产出半成品
    EXPECT_TRUE(plan.layers.empty());
}

// ============================================================
//  进度回调：fraction 从 0 到 1
// ============================================================

TEST(CamCoreTests, ProgressCallback_ReachesOne)
{
    ToolpathJobInput input = makeSquareJob(100.0);
    CamPlan plan;

    double lastFraction = 0.0;
    int callCount = 0;
    Progress progress;
    progress.onProgress = [](double f, const char*, void* userData) -> int32_t {
        auto* last = static_cast<double*>(userData);
        if (f > *last) *last = f;
        return 0;
    };
    progress.userData = &lastFraction;

    CamResult result = compile(input, plan, &progress);
    EXPECT_TRUE(result.ok);
    EXPECT_GE(lastFraction, 0.95);  // 最后阶段 fraction 到 1.0
}

// ============================================================
//  Unmachinable：Fillable + failOnUnmachinable = true
// ============================================================

TEST(CamCoreTests, UnmachinableFillable_WithFail_ReturnsError)
{
    ToolpathJobInput input = makeSquareJob(100.0);
    input.failOnUnmachinable = true;

    // 在已有 layer 里追加一个 Fillable entity
    EntitySnapshot fillableEntity;
    fillableEntity.kind = EntitySnapshot::GeometryKind::Fillable;
    fillableEntity.entityId = 102;
    fillableEntity.layerId = 1;
    input.layers.front().entities.push_back(fillableEntity);

    CamPlan plan;
    CamResult result = compile(input, plan);
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.error, CamError::UncontainedRaster);
    EXPECT_EQ(result.unmachinableEntityCount, 1);
}

// ============================================================
//  EntitySnapshot 跳过：visible = false
// ============================================================

TEST(CamCoreTests, HiddenLayer_Skipped)
{
    ToolpathJobInput input = makeSquareJob(100.0);
    input.layers.front().visible = false;
    input.skipHiddenLayers = true;

    CamPlan plan;
    CamResult result = compile(input, plan);
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.error, CamError::EmptyJob);
}

// ============================================================
//  passes 展开：每个遍次独立产出 PlanLayerResult
// ============================================================

TEST(CamCoreTests, Passes_AreExpandedIntoDistinctLayerResults)
{
    ToolpathJobInput input = makeSquareJob(100.0);
    input.layers.front().params.passes = 3;

    CamPlan plan;
    CamResult result = compile(input, plan);
    ASSERT_TRUE(result.ok);

    ASSERT_EQ(plan.layers.size(), 3u);
    for (size_t i = 0; i < plan.layers.size(); ++i)
    {
        EXPECT_EQ(plan.layers[i].passIndex, (int32_t)i);
        EXPECT_EQ(plan.layers[i].layerId, 1);
        ASSERT_EQ(plan.layers[i].contours.size(), 1u);
        EXPECT_EQ(plan.layers[i].contours.front().segments.size(), 4u);
    }

    // 图元只统计一次，轮廓按遍次累计，切长按遍次累计
    EXPECT_EQ(result.entityCount, 1);
    EXPECT_EQ(result.contourCount, 3);
    EXPECT_EQ(result.layerCount, 1);
    EXPECT_NEAR(plan.totalCuttingLengthMm, 1200.0, 1e-3);
}

// ============================================================
//  默认工艺参数（rapidSpeed 已由桥接层归一化为具体值）
// ============================================================

TEST(CamCoreTests, DefaultProcessParams_CompileSuccessfully)
{
    ToolpathJobInput input = makeSquareJob(100.0);
    input.layers.front().params = ProcessParams{};
    ASSERT_TRUE(input.layers.front().params.valid());

    CamPlan plan;
    CamResult result = compile(input, plan);
    EXPECT_TRUE(result.ok);
    EXPECT_EQ(result.error, CamError::Ok);
    ASSERT_EQ(plan.layers.size(), 1u);
    EXPECT_EQ(plan.layers.front().passIndex, 0);
}

// ============================================================
//  图层工艺停用：整层不产出刀路
// ============================================================

TEST(CamCoreTests, DisabledLayerParams_SkipsLayer)
{
    ToolpathJobInput input = makeSquareJob(100.0);
    input.layers.front().params.enabled = false;

    CamPlan plan;
    CamResult result = compile(input, plan);
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.error, CamError::EmptyJob);
}

// ============================================================
//  图元级隐藏 / 锁定：按 skipHidden/skipLocked 过滤
// ============================================================

TEST(CamCoreTests, HiddenEntity_SkippedWhenSkipHiddenEnabled)
{
    ToolpathJobInput input = makeSquareJob(100.0);
    input.layers.front().entities.front().visible = false;
    input.skipHiddenLayers = true;

    CamPlan plan;
    CamResult result = compile(input, plan);
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.error, CamError::EmptyJob);
}

TEST(CamCoreTests, HiddenEntity_KeptWhenSkipHiddenDisabled)
{
    ToolpathJobInput input = makeSquareJob(100.0);
    input.layers.front().entities.front().visible = false;
    input.skipHiddenLayers = false;

    CamPlan plan;
    CamResult result = compile(input, plan);
    EXPECT_TRUE(result.ok);
    ASSERT_EQ(plan.layers.size(), 1u);
}

TEST(CamCoreTests, LockedEntity_SkippedWhenSkipLockedEnabled)
{
    ToolpathJobInput input = makeSquareJob(100.0);
    input.layers.front().entities.front().locked = true;
    input.skipLockedLayers = true;

    CamPlan plan;
    CamResult result = compile(input, plan);
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.error, CamError::EmptyJob);
}

// ============================================================
//  不可加工原因透传：清单必须带出提取器给出的 reason
// ============================================================

TEST(CamCoreTests, UnmachinableReason_SurfacedInResultDetails)
{
    ToolpathJobInput input = makeSquareJob(100.0);
    input.failOnUnmachinable = false;

    EntitySnapshot raster;
    raster.kind = EntitySnapshot::GeometryKind::Raster;
    raster.entityId = 42;
    raster.layerId = 1;
    raster.unmachinableReason = "raster engraving engine not implemented yet";
    input.layers.front().entities.push_back(std::move(raster));

    CamPlan plan;
    CamResult result = compile(input, plan);
    EXPECT_TRUE(result.ok);
    EXPECT_EQ(result.unmachinableEntityCount, 1);
    ASSERT_EQ(result.unmachinableDetails.size(), 1u);
    EXPECT_NE(result.unmachinableDetails.front().find("raster engraving"),
              std::string::npos);
}

TEST(CamCoreTests, UnsupportedEntity_FailsWithReasonListed)
{
    ToolpathJobInput input = makeSquareJob(100.0);
    input.failOnUnmachinable = true;

    EntitySnapshot unsupported;
    unsupported.kind = EntitySnapshot::GeometryKind::Unsupported;
    unsupported.entityId = 43;
    unsupported.layerId = 1;
    unsupported.unmachinableReason = "text has no vector geometry";
    input.layers.front().entities.push_back(std::move(unsupported));

    CamPlan plan;
    CamResult result = compile(input, plan);
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.error, CamError::UncontainedRaster);
    EXPECT_EQ(result.unmachinableEntityCount, 1);
    ASSERT_EQ(result.unmachinableDetails.size(), 1u);
    EXPECT_NE(result.unmachinableDetails.front().find("text has no vector geometry"),
              std::string::npos);
}

// ============================================================
//  位图光栅策略：扫描线段 1:1 编译，停光段转 rapid
// ============================================================

TEST(CamCoreTests, CompileRaster_EmitsOneSegmentPerScanline)
{
    ToolpathJobInput input = makeSquareJob(100.0);
    input.layers.front().entities.clear();

    EntitySnapshot raster;
    raster.kind = EntitySnapshot::GeometryKind::Raster;
    raster.entityId = 301;
    raster.layerId = 1;

    // 5 段：亮(0..10) → 停光(10..20) → 亮(20..30) → 停光(30..40) → 亮(40..50)
    auto addLine = [&](double x0, double x1, double y, bool on, double power, int pass) {
        ScanlineSegment line;
        line.ptStart = Vec2d(x0, y);
        line.ptEnd = Vec2d(x1, y);
        line.laserOn = on;
        line.powerPercent = power;
        line.speedMmPerSec = on ? 120.0 : 0.0;
        line.passIndex = pass;
        raster.scanlines.push_back(line);
    };
    addLine(0.0, 10.0, 0.0, true, 60.0, 0);
    addLine(10.0, 20.0, 0.0, false, 0.0, 0);
    addLine(20.0, 30.0, 0.0, true, 40.0, 0);
    addLine(30.0, 40.0, 0.0, false, 0.0, 0);
    addLine(40.0, 50.0, 0.0, true, 20.0, 0);

    input.layers.front().entities.push_back(std::move(raster));

    CamPlan plan;
    CamResult result = compile(input, plan);
    EXPECT_TRUE(result.ok);
    EXPECT_EQ(result.entityCount, 1);
    EXPECT_EQ(result.contourCount, 1);

    ASSERT_EQ(plan.layers.size(), 1u);
    ASSERT_EQ(plan.layers.front().contours.size(), 1u);
    const PlanContour& contour = plan.layers.front().contours.front();

    // 起点就是第一段起点，不插额外快速定位段；段数与库返回一致
    EXPECT_EQ(contour.segments.size(), input.layers.front().entities.front().scanlines.size());
    EXPECT_GT(contour.segments.size(), 0u);

    int rapidCount = 0;
    int cutCount = 0;
    for (const auto& seg : contour.segments)
    {
        if (seg.isRapid)
        {
            ++rapidCount;
            EXPECT_DOUBLE_EQ(seg.powerPercent, 0.0);
        }
        else
        {
            ++cutCount;
            EXPECT_GT(seg.powerPercent, 0.0);
            EXPECT_GT(seg.speedMmPerSec, 0.0);
        }
    }
    EXPECT_EQ(cutCount, 3);
    EXPECT_EQ(rapidCount, 2);

    // 加工长度 = 3×10mm，空移长度 = 2×10mm
    EXPECT_NEAR(contour.cuttingLengthMm, 30.0, 1e-6);
    EXPECT_NEAR(contour.rapidLengthMm, 20.0, 1e-6);
}

TEST(CamCoreTests, CompileRaster_DoesNotMultiplyLayerPasses)
{
    ToolpathJobInput input = makeSquareJob(100.0);
    input.layers.front().entities.clear();
    // 图层遍数由扫描线段的 pass 序号承担，CAM 不得再乘一遍
    input.layers.front().params.passes = 2;

    EntitySnapshot raster;
    raster.kind = EntitySnapshot::GeometryKind::Raster;
    raster.entityId = 302;
    raster.layerId = 1;

    for (int pass = 0; pass < 2; ++pass)
    {
        ScanlineSegment line;
        line.ptStart = Vec2d(0.0, 0.0);
        line.ptEnd = Vec2d(10.0, 0.0);
        line.laserOn = true;
        line.powerPercent = 50.0;
        line.passIndex = pass;
        raster.scanlines.push_back(line);
    }
    input.layers.front().entities.push_back(std::move(raster));

    CamPlan plan;
    CamResult result = compile(input, plan);
    EXPECT_TRUE(result.ok);
    ASSERT_EQ(plan.layers.size(), 1u);
    ASSERT_EQ(plan.layers.front().contours.size(), 1u);
    EXPECT_EQ(plan.layers.front().contours.front().segments.size(), 2u);
    EXPECT_EQ(plan.layers.front().contours.front().segments.front().passIndex, 0);
    EXPECT_EQ(plan.layers.front().contours.front().segments.back().passIndex, 1);
}

TEST(CamCoreTests, CompileRaster_EmptyScanlinesIsUnmachinable)
{
    // 保留一条可加工轮廓，避免整层没有可加工实体导致整体失败
    ToolpathJobInput input = makeSquareJob(100.0);
    input.failOnUnmachinable = false;

    EntitySnapshot raster;
    raster.kind = EntitySnapshot::GeometryKind::Raster;
    raster.entityId = 303;
    raster.layerId = 1;
    raster.unmachinableReason = "raster image has no pixel data";
    input.layers.front().entities.push_back(std::move(raster));

    CamPlan plan;
    CamResult result = compile(input, plan);
    EXPECT_TRUE(result.ok);
    EXPECT_EQ(result.unmachinableEntityCount, 1);
}

// ============================================================
//  camErrorName 覆盖所有枚举值
// ============================================================

TEST(CamCoreTests, AllErrorCodes_HaveNames)
{
    const std::initializer_list<CamError> allErrors = {
        CamError::Ok,
        CamError::InvalidParams,
        CamError::EmptyJob,
        CamError::ChordToleranceTooLarge,
        CamError::SelectionEmpty,
        CamError::Cancelled,
        CamError::UncontainedRaster,
    };
    for (auto err : allErrors)
    {
        const char* name = camErrorName(err);
        ASSERT_NE(name, nullptr);
        // Unknown error 也返回 "Unknown error"，不是 nullptr
    }
}
