/**
 * @file CamBridgeTests.cpp
 * @brief CAM 桥接层回归测试（仅在 BUILD_CAM=ON 时编入 MainTests）。
 *
 * 覆盖两条曾经会直接导致「默认配置报 InvalidParams」的契约：
 *   1. Engine 的 rapidSpeed=0（0=工程默认）必须被桥接层归一化为具体数值；
 *   2. 图元级 visible/locked 与图层工艺 enabled 必须透传给 CAM 并参与过滤。
 */

#include <gtest/gtest.h>

#include "CAM/CamCore.h"
#include "CAM/ToolpathJobInput.h"

#include "CAM/CamInputBridge.h"
#include "Engine2D/Core/SceneManager.h"
#include "Engine2D/Core/SceneWriteGateway.h"
#include "Engine2D/SyEntity/SyArc.h"
#include "Engine2D/SyEntity/SyImage.h"
#include "Engine2D/SyEntity/SyLine.h"
#include "Hardware/ToolpathJobSpec.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace
{
    /// 往场景里加一条折线，返回场景内的那一份图元
    Eg::SyEntity* addPolyline(Eg::SceneManager& scene)
    {
        Eg::SyLine line;
        line.setPointVector({ Ut::Vec2d(0.0, 0.0), Ut::Vec2d(10.0, 0.0), Ut::Vec2d(10.0, 10.0) });
        Eg::SceneWriteGateway::addEntity(scene, &line);

        const std::vector<Eg::SyEntity*> all = scene.getAllEntities();
        return all.empty() ? nullptr : all.back();
    }

    /// 加一张横向灰度阶梯图：物理尺寸固定 25.4mm × 25.4mm，像素数由 DPI 决定
    Eg::SyEntity* addGradientImage(Eg::SceneManager& scene, int pixels)
    {
        Eg::SyImage image;
        image.nWidth = pixels;
        image.nHeight = pixels;
        image.ePixelFormat = Eg::SyPixelFormat::Gray8;
        image.topLeft = Ut::Vec2d(0.0, 0.0);
        image.topRight = Ut::Vec2d(25.4, 0.0);
        image.bottomLeft = Ut::Vec2d(0.0, -25.4);
        image.bottomRight = Ut::Vec2d(25.4, -25.4);

        std::vector<unsigned char> data(static_cast<size_t>(pixels) * static_cast<size_t>(pixels), 0);
        for (int y = 0; y < pixels; ++y) {
            for (int x = 0; x < pixels; ++x) {
                data[static_cast<size_t>(y) * static_cast<size_t>(pixels) + static_cast<size_t>(x)] =
                    static_cast<unsigned char>(pixels > 1 ? (x * 255) / (pixels - 1) : 0);
            }
        }
        image.setPixelDataVector(data);
        Eg::SceneWriteGateway::addEntity(scene, &image);

        const std::vector<Eg::SyEntity*> all = scene.getAllEntities();
        return all.empty() ? nullptr : all.back();
    }

    /// 扫描线段在用户坐标下的包围盒
    void scanlineBounds(const std::vector<Cam::ScanlineSegment>& lines, double& widthMm,
                        double& heightMm)
    {
        double minX = 1e9;
        double maxX = -1e9;
        double minY = 1e9;
        double maxY = -1e9;
        for (const auto& line : lines) {
            minX = std::min(minX, std::min(line.ptStart.x(), line.ptEnd.x()));
            maxX = std::max(maxX, std::max(line.ptStart.x(), line.ptEnd.x()));
            minY = std::min(minY, std::min(line.ptStart.y(), line.ptEnd.y()));
            maxY = std::max(maxY, std::max(line.ptStart.y(), line.ptEnd.y()));
        }
        widthMm = maxX - minX;
        heightMm = maxY - minY;
    }
}

TEST(CamBridgeTests, DefaultProcessParams_NormalizedAndCompilable)
{
    Eg::SceneManager scene;
    ASSERT_NE(addPolyline(scene), nullptr);

    // ToolpathJobSpec 默认 params.rapidSpeedMmPerSec = 0（Engine 约定 = 工程默认）
    ToolpathJobSpec spec;
    ASSERT_EQ(spec.defaultParams.rapidSpeedMmPerSec, 0.0);

    Cam::ToolpathJobInput input = CamBridge::buildJobInput(scene, nullptr, spec);
    ASSERT_FALSE(input.layers.empty());

    const Cam::ProcessParams& params = input.layers.front().params;
    EXPECT_GT(params.rapidSpeedMmPerSec, 0.0);
    EXPECT_NEAR(params.rapidSpeedMmPerSec, spec.rapidSpeedMmPerSec, 1e-9);
    EXPECT_TRUE(params.valid());

    Cam::CamPlan plan;
    Cam::CamResult result;
    Cam::CamResult actual = Cam::compile(input, plan, nullptr, &result);
    EXPECT_TRUE(actual.ok);
    EXPECT_EQ(actual.error, Cam::CamError::Ok);
    EXPECT_FALSE(plan.layers.empty());
}

TEST(CamBridgeTests, ArcAngles_ConvertedFromRadiansToDegrees)
{
    const double kPi = 3.14159265358979323846;

    Eg::SceneManager scene;
    Eg::SyArc arc;
    arc.basePoint = Ut::Vec2d(0.0, 0.0);
    arc.dRadius = 10.0;
    // 几何层角度单位是弧度：四分之一圆弧
    arc.dStartAngle = 0.0;
    arc.dEndAngle = kPi * 0.5;
    Eg::SceneWriteGateway::addEntity(scene, &arc);

    ToolpathJobSpec spec;
    Cam::ToolpathJobInput input = CamBridge::buildJobInput(scene, nullptr, spec);
    ASSERT_FALSE(input.layers.empty());
    ASSERT_FALSE(input.layers.front().entities.empty());

    const Cam::EntitySnapshot& snapshot = input.layers.front().entities.front();
    ASSERT_EQ(snapshot.kind, Cam::EntitySnapshot::GeometryKind::Contour);
    ASSERT_EQ(snapshot.contours.size(), 1u);
    ASSERT_FALSE(snapshot.contours.front().segments.empty());

    // 桥接层是唯一换算点：进入 CAM 时角度必须变成度
    const Cam::PathSegment& segment = snapshot.contours.front().segments.back();
    ASSERT_EQ(segment.type, Cam::PathSegment::Type::Arc);
    EXPECT_NEAR(segment.dStartAngle, 0.0, 1e-9);
    EXPECT_NEAR(segment.dEndAngle, 90.0, 1e-9);

    Cam::CamPlan plan;
    Cam::CamResult result;
    Cam::CamResult actual = Cam::compile(input, plan, nullptr, &result);
    ASSERT_TRUE(actual.ok);
    ASSERT_FALSE(plan.layers.empty());
    // 四分之一圆弧长 = r * π/2
    EXPECT_NEAR(plan.totalCuttingLengthMm, 10.0 * kPi * 0.5, 1e-6);
}

TEST(CamBridgeTests, EntityVisibleAndLocked_ArePassedThrough)
{
    Eg::SceneManager scene;
    Eg::SyEntity* entity = addPolyline(scene);
    ASSERT_NE(entity, nullptr);

    entity->setVisible(false);
    entity->setLocked(true);

    ToolpathJobSpec spec;
    ASSERT_TRUE(spec.skipHiddenLayers);
    ASSERT_TRUE(spec.skipLockedLayers);

    Cam::ToolpathJobInput input = CamBridge::buildJobInput(scene, nullptr, spec);
    ASSERT_FALSE(input.layers.empty());
    ASSERT_FALSE(input.layers.front().entities.empty());

    const Cam::EntitySnapshot& snapshot = input.layers.front().entities.front();
    EXPECT_FALSE(snapshot.visible);
    EXPECT_TRUE(snapshot.locked);

    // 两项跳过开关都生效后，作业内没有可加工内容
    Cam::CamPlan plan;
    Cam::CamResult result;
    Cam::CamResult actual = Cam::compile(input, plan, nullptr, &result);
    EXPECT_FALSE(actual.ok);
    EXPECT_EQ(actual.error, Cam::CamError::EmptyJob);
}

TEST(CamBridgeTests, RasterEntity_AssembledToScanlines)
{
#ifndef BUILD_RASTER
    // 位图 → 扫描线的装配在 CamInputBridge 里按 BUILD_RASTER 条件编译。
    // Raster 子模块未初始化时该装配不参与构建，图元只会落到哨兵尺寸，
    // 这里必须显式跳过，而不是伪装成一次「装配失败」。
    GTEST_SKIP() << "requires BUILD_RASTER=ON";
#endif
    Eg::SceneManager scene;
    ASSERT_NE(addGradientImage(scene, 96), nullptr);

    ToolpathJobSpec spec;
    spec.defaultParams.fillEnabled = true;
    spec.defaultParams.fillIntervalMm = 1.0;
    spec.defaultParams.fillMode = Eg::FillMode::Raster;
    spec.defaultParams.powerPercent = 80.0;
    spec.defaultParams.speedMmPerSec = 100.0;
    spec.defaultParams.passes = 1;

    Cam::ToolpathJobInput input = CamBridge::buildJobInput(scene, nullptr, spec);
    ASSERT_FALSE(input.layers.empty());
    ASSERT_FALSE(input.layers.front().entities.empty());

    const Cam::EntitySnapshot& snapshot = input.layers.front().entities.front();
    ASSERT_EQ(snapshot.kind, Cam::EntitySnapshot::GeometryKind::Raster);
    ASSERT_FALSE(snapshot.scanlines.empty());

    // 至少要有出光段和停光段，且坐标已经离开像素域
    int onCount = 0;
    int offCount = 0;
    for (const auto& line : snapshot.scanlines) {
        if (line.laserOn) {
            ++onCount;
        } else {
            ++offCount;
        }
    }
    EXPECT_GT(onCount, 0);
    EXPECT_GT(offCount, 0);

    // CAM 编译后每一条出光扫描线对应一段 cutting，停光线对应 rapid；
    // 允许首段前出现一条把刀头送到图像起点的定位空移
    Cam::CamPlan plan;
    Cam::CamResult result;
    Cam::CamResult actual = Cam::compile(input, plan, nullptr, &result);
    ASSERT_TRUE(actual.ok);
    ASSERT_FALSE(plan.layers.empty());
    ASSERT_FALSE(plan.layers.front().contours.empty());

    int cutCount = 0;
    for (const auto& seg : plan.layers.front().contours.front().segments) {
        if (!seg.isRapid) {
            ++cutCount;
        }
    }
    const int litCount = static_cast<int>(std::count_if(
        snapshot.scanlines.begin(), snapshot.scanlines.end(),
        [](const Cam::ScanlineSegment& line) { return line.laserOn; }));
    EXPECT_EQ(cutCount, litCount);
}

TEST(CamBridgeTests, RasterEntity_SamePhysicalSizeAt96And300Dpi)
{
#ifndef BUILD_RASTER
    GTEST_SKIP() << "requires BUILD_RASTER=ON";
#endif
    auto buildForDpi = [](int pixels, Cam::ToolpathJobInput& input) {
        Eg::SceneManager scene;
        if (addGradientImage(scene, pixels) == nullptr) {
            return false;
        }
        ToolpathJobSpec spec;
        spec.defaultParams.fillEnabled = true;
        spec.defaultParams.fillIntervalMm = 1.0;
        spec.defaultParams.fillMode = Eg::FillMode::Raster;
        spec.defaultParams.powerPercent = 80.0;
        spec.defaultParams.speedMmPerSec = 100.0;
        spec.defaultParams.passes = 1;
        input = CamBridge::buildJobInput(scene, nullptr, spec);
        return !input.layers.empty() && !input.layers.front().entities.empty();
    };

    Cam::ToolpathJobInput at96;
    Cam::ToolpathJobInput at300;
    ASSERT_TRUE(buildForDpi(96, at96));
    ASSERT_TRUE(buildForDpi(300, at300));

    double width96 = 0.0;
    double height96 = 0.0;
    double width300 = 0.0;
    double height300 = 0.0;
    scanlineBounds(at96.layers.front().entities.front().scanlines, width96, height96);
    scanlineBounds(at300.layers.front().entities.front().scanlines, width300, height300);

    // 同一张图物理宽高都是 25.4mm；像素当量不同不应改变毫米尺寸
    EXPECT_NEAR(width96, 25.4, 1e-3);
    EXPECT_NEAR(width300, 25.4, 1e-3);
    EXPECT_NEAR(height96, height300, 0.5);
    EXPECT_GT(height96, 24.0);
    EXPECT_GT(height300, 24.0);
}

TEST(CamBridgeTests, DisabledLayerProcess_SkipsWholeLayer)
{
    Eg::SceneManager scene;
    ASSERT_NE(addPolyline(scene), nullptr);

    ToolpathJobSpec spec;
    const int layerId = 0;

    Eg::ProcessParams disabled;
    disabled.enabled = false;
    spec.layerParams.insert(layerId, disabled);

    Cam::ToolpathJobInput input = CamBridge::buildJobInput(scene, nullptr, spec);
    ASSERT_FALSE(input.layers.empty());
    EXPECT_FALSE(input.layers.front().params.enabled);

    Cam::CamPlan plan;
    Cam::CamResult result;
    Cam::CamResult actual = Cam::compile(input, plan, nullptr, &result);
    EXPECT_FALSE(actual.ok);
    EXPECT_EQ(actual.error, Cam::CamError::EmptyJob);
}
