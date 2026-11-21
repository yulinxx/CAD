/**
 * @file RasterMotionPlanTests.cpp
 * @brief 位图 → MotionPlan 端到端测试（真机链路：MotionPlanCompiler → Hw::MotionPlan）
 *
 * 覆盖：
 *   - 位图图元经 MotionPlanCompiler 的 Raster 分支产出真实扫描段（不再标记 unsupported）；
 *   - 行距按请求的毫米值与真实像素当量换算（300 DPI，非整数像素行距）；
 *   - 极性「暗 = 加工」：最暗像素对应最大功率，最亮（全透明合成白底）不出光；
 *   - 多遍为整图分遍（先全部第 0 遍，再全部第 1 遍），不是逐行两遍；
 *   - 过扫长度可检查，且刀路信封不超出「图像 ∪ 过扫」；
 *   - 不可加工的位图必须给出可见错误，不许静默产出空刀路。
 */
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "Hardware/MotionPlanCompiler.h"
#include "Hardware/ToolpathJobSpec.h"
#include "Hardware/Device/MotionPlan.h"

#include "Engine2D/Core/SceneManager.h"
#include "Engine2D/Core/SceneWriteGateway.h"
#include "Engine2D/AprEntity/AprImage.h"

#ifdef ENABLE_HARDWARE

namespace
{
    /// 300 DPI 的像素当量（mm/px）。刻意取非整数关系，才能真正验证 mm→px 用的是真实 DPI
    constexpr double kPitchMm = 25.4 / 300.0;

    /// 扫描带在像素坐标下的中心 y（与 Raster 库一致：y_i = (i + 0.5) * spacingPx）
    struct LitSegment
    {
        size_t commandIndex = 0;   ///< 指令序号，用于判断遍次先后
        int32_t pass = -1;
        double x0 = 0.0;           ///< 起点 x（上一条运动指令的终点）
        double y0 = 0.0;
        double x1 = 0.0;           ///< 终点 x
        double y1 = 0.0;
        double powerPercent = 0.0; ///< 该段生效的功率
    };

    /// 一条关光行走（空白区穿越或过扫）
    struct OffTravel
    {
        size_t commandIndex = 0;
        double x0 = 0.0, y0 = 0.0;
        double x1 = 0.0, y1 = 0.0;
    };

    /// 把 MotionPlan 扫一遍，抽出出光段、关光行走、以及出现过的功率值
    struct PlanScan
    {
        std::vector<LitSegment> lit;
        std::vector<OffTravel> off;
        std::vector<double> laserParamPowers;
        double totalLitLengthMm = 0.0;
        int32_t passCount = 0;
    };

    PlanScan scanPlan(const Hw::MotionPlanView& view)
    {
        PlanScan scan;
        double px = 0.0;
        double py = 0.0;
        int32_t pass = -1;
        double power = 0.0;

        for (size_t i = 0; i < view.commandCount; ++i)
        {
            const Hw::MotionCommand& cmd = view.commands[i];
            const double tx = cmd.target.v[0];
            const double ty = cmd.target.v[1];
            switch (cmd.op)
            {
            case Hw::MotionOp::BeginPass:
                pass = cmd.intArg;
                ++scan.passCount;
                break;
            case Hw::MotionOp::SetLaserParams:
                power = cmd.laser.powerPercent;
                scan.laserParamPowers.push_back(power);
                break;
            case Hw::MotionOp::RapidTo:
                px = tx;
                py = ty;
                break;
            case Hw::MotionOp::LineTo:
                if (cmd.laserOn)
                {
                    LitSegment seg;
                    seg.commandIndex = i;
                    seg.pass = pass;
                    seg.x0 = px;
                    seg.y0 = py;
                    seg.x1 = tx;
                    seg.y1 = ty;
                    seg.powerPercent = power;
                    // 扫描段恒为水平，长度取 x 方向差
                    scan.totalLitLengthMm += std::fabs(tx - px);
                    scan.lit.push_back(seg);
                }
                else
                {
                    OffTravel off;
                    off.commandIndex = i;
                    off.x0 = px;
                    off.y0 = py;
                    off.x1 = tx;
                    off.y1 = ty;
                    scan.off.push_back(off);
                }
                px = tx;
                py = ty;
                break;
            default:
                break;
            }
        }
        return scan;
    }

    /// 造一张轴对齐的位图实体（左上角在原点，向右向下展开）
    Eg::AprImage makeImage(int32_t width,
                          int32_t height,
                          Eg::AprPixelFormat format,
                          const std::vector<uint8_t>& pixels)
    {
        Eg::AprImage image;
        image.nWidth = width;
        image.nHeight = height;
        image.ePixelFormat = format;
        image.topLeft = Ut::Vec2d(0.0, 0.0);
        image.topRight = Ut::Vec2d(static_cast<double>(width) * kPitchMm, 0.0);
        image.bottomLeft = Ut::Vec2d(0.0, static_cast<double>(height) * kPitchMm);
        image.bottomRight = Ut::Vec2d(static_cast<double>(width) * kPitchMm,
                                      static_cast<double>(height) * kPitchMm);
        image.setPixelData(pixels.data(), pixels.size());
        return image;
    }

    /// 把位图加进场景并返回**场景内那一份**（addEntity 会 clone 并重新分配 id）
    Eg::AprImage* addImage(Eg::SceneManager& scene, const Eg::AprImage& image)
    {
        Eg::SceneWriteGateway::addEntity(scene, &image);
        const std::vector<Eg::AprEntity*> all = scene.getAllEntities();
        return all.empty() ? nullptr : static_cast<Eg::AprImage*>(all.back());
    }

    /// 竖向四段灰度图：黑 / 128 / 200 / 白，用来同时验证功率梯度与"白不出光"
    std::vector<uint8_t> makeGrayBands(int32_t width, int32_t height)
    {
        std::vector<uint8_t> pixels(static_cast<size_t>(width) * static_cast<size_t>(height), 255);
        for (int32_t y = 0; y < height; ++y)
        {
            for (int32_t x = 0; x < width; ++x)
            {
                uint8_t value = 255;
                if (x < width / 4) value = 0;
                else if (x < width / 2) value = 128;
                else if (x < (width * 3) / 4) value = 200;
                pixels[static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)] = value;
            }
        }
        return pixels;
    }

    /// 全黑 RGBA 图，右半部分 alpha = 0（透明）
    std::vector<uint8_t> makeHalfTransparentRgba(int32_t width, int32_t height)
    {
        std::vector<uint8_t> pixels(static_cast<size_t>(width) * static_cast<size_t>(height) * 4, 0);
        for (int32_t y = 0; y < height; ++y)
        {
            for (int32_t x = 0; x < width; ++x)
            {
                const size_t base = (static_cast<size_t>(y) * static_cast<size_t>(width)
                                     + static_cast<size_t>(x)) * 4;
                const bool transparent = x >= width / 2;
                pixels[base + 0] = 0;
                pixels[base + 1] = 0;
                pixels[base + 2] = 0;
                pixels[base + 3] = transparent ? 0 : 255;
            }
        }
        return pixels;
    }

    /// 造一张轴对齐、像素纵横比可不同的位图实体（基线测量用）
    Eg::AprImage makeAnisotropicImage(int32_t width,
                                     int32_t height,
                                     double pitchX,
                                     double pitchY,
                                     const std::vector<uint8_t>& pixels)
    {
        Eg::AprImage image;
        image.nWidth = width;
        image.nHeight = height;
        image.ePixelFormat = Eg::AprPixelFormat::Gray8;
        image.topLeft = Ut::Vec2d(0.0, 0.0);
        image.topRight = Ut::Vec2d(static_cast<double>(width) * pitchX, 0.0);
        image.bottomLeft = Ut::Vec2d(0.0, static_cast<double>(height) * pitchY);
        image.bottomRight = Ut::Vec2d(static_cast<double>(width) * pitchX,
                                      static_cast<double>(height) * pitchY);
        image.setPixelData(pixels.data(), pixels.size());
        return image;
    }

    /// 从出光段里按 y 去重，得到扫描带中心序列（升序）
    std::vector<double> uniqueScanYs(const PlanScan& scan)
    {
        std::vector<double> ys;
        for (const LitSegment& seg : scan.lit)
        {
            if (ys.empty() || std::fabs(seg.y1 - ys.back()) > 1e-9)
            {
                ys.push_back(seg.y1);
            }
        }
        return ys;
    }

    /// 出光段是否完全落在 [xMin, xMax] 内
    bool litWithin(const PlanScan& scan, double xMin, double xMax, double tol = 1e-9)
    {
        for (const LitSegment& seg : scan.lit)
        {
            if (std::min(seg.x0, seg.x1) < xMin - tol || std::max(seg.x0, seg.x1) > xMax + tol)
            {
                return false;
            }
        }
        return true;
    }
}  // namespace

/// 位图图元必须产出非零扫描段，且行距等于请求的毫米值（按真实 DPI 换算）
TEST(RasterMotionPlanTest, ProducesScanSegmentsWithRequestedLineSpacing)
{
    const int32_t width = 64;
    const int32_t height = 64;
    const double spacingMm = 0.25;

    Eg::SceneManager scene;
    ASSERT_NE(nullptr, addImage(scene, makeImage(width, height,
                                                  Eg::AprPixelFormat::Gray8,
                                                  makeGrayBands(width, height))));

    ToolpathJobSpec spec;
    spec.defaultParams.powerPercent = 60.0;
    spec.defaultParams.speedMmPerSec = 100.0;
    spec.defaultParams.fillIntervalMm = spacingMm;
    spec.defaultParams.passes = 1;

    Hw::MotionPlanBuilder builder;
    const ToolpathCompileResult result = MotionPlanCompiler::compile(scene, nullptr, spec, builder);
    ASSERT_TRUE(result.ok) << result.error.toStdString();
    ASSERT_EQ(1, result.layerCount);
    ASSERT_EQ(1, result.entityCount);

    const Hw::MotionPlanView view = builder.view();
    const PlanScan scan = scanPlan(view);

    // 1) 扫描段数量非零
    ASSERT_FALSE(scan.lit.empty());

    // 2) 行距：相邻扫描带中心的物理间距必须等于请求的毫米值
    //    （y 由 (i+0.5)*spacingPx 在像素空间生成，再乘真实像素当量）
    const std::vector<double> ys = uniqueScanYs(scan);
    ASSERT_GE(ys.size(), 2u);
    for (size_t i = 1; i < ys.size(); ++i)
    {
        EXPECT_NEAR(spacingMm, ys[i] - ys[i - 1], 1e-6) << "scan line " << i;
    }

    // 3) 覆盖高度：图像高 64px * 0.0846667 = 5.41867mm，按 0.25mm 应得 22 条扫描带
    const double heightMm = static_cast<double>(height) * kPitchMm;
    const size_t expectedLines = static_cast<size_t>(std::ceil(heightMm / spacingMm));
    EXPECT_EQ(expectedLines, ys.size());

    // 4) 极性：最暗（黑）像素段功率 = 请求的最大功率 60%
    bool foundDarkest = false;
    for (const LitSegment& seg : scan.lit)
    {
        if (std::min(seg.x0, seg.x1) >= 0.0 && std::max(seg.x0, seg.x1) <= 16.0 * kPitchMm + 1e-9)
        {
            EXPECT_NEAR(60.0, seg.powerPercent, 1e-6);
            foundDarkest = true;
        }
    }
    EXPECT_TRUE(foundDarkest) << "no lit segment found in the darkest band";

    // 5) 最亮（白）区不出光：每行出光长度只有 48px，而不是整行 64px
    const size_t rows = ys.size();
    EXPECT_NEAR(static_cast<double>(rows) * 48.0 * kPitchMm, scan.totalLitLengthMm, 1e-6);

    // 6) 中间灰度按「暗 = 加工」线性落到更低的功率（灰 128 → 约 29.8%，灰 200 → 约 12.9%）
    bool foundMid = false;
    bool foundLight = false;
    for (double power : scan.laserParamPowers)
    {
        if (std::fabs(power - 29.8) < 0.5) foundMid = true;
        if (std::fabs(power - 12.9) < 0.5) foundLight = true;
    }
    EXPECT_TRUE(foundMid) << "gray 128 band did not produce its own power";
    EXPECT_TRUE(foundLight) << "gray 200 band did not produce its own power";
}

/// 透明像素合成白底后必须不出光（与矢量化前景一致）
TEST(RasterMotionPlanTest, TransparentPixelsDoNotBurn)
{
    const int32_t width = 64;
    const int32_t height = 64;
    const double spacingMm = 0.25;

    Eg::SceneManager scene;
    ASSERT_NE(nullptr, addImage(scene, makeImage(width, height,
                                                  Eg::AprPixelFormat::RGBA32,
                                                  makeHalfTransparentRgba(width, height))));

    ToolpathJobSpec spec;
    spec.defaultParams.powerPercent = 60.0;
    spec.defaultParams.fillIntervalMm = spacingMm;

    Hw::MotionPlanBuilder builder;
    const ToolpathCompileResult result = MotionPlanCompiler::compile(scene, nullptr, spec, builder);
    ASSERT_TRUE(result.ok) << result.error.toStdString();

    const PlanScan scan = scanPlan(builder.view());
    ASSERT_FALSE(scan.lit.empty());

    // 只有左半（不透明黑）出光，右半透明区不得有出光段
    EXPECT_TRUE(litWithin(scan, 0.0, 32.0 * kPitchMm));
    const size_t rows = uniqueScanYs(scan).size();
    EXPECT_NEAR(static_cast<double>(rows) * 32.0 * kPitchMm, scan.totalLitLengthMm, 1e-6);
}

/// passes=2 必须是整图分遍：先输出全部第 0 遍，再输出全部第 1 遍
TEST(RasterMotionPlanTest, PassesRunWholeImageNotPerRow)
{
    const int32_t width = 64;
    const int32_t height = 64;

    Eg::SceneManager scene;
    ASSERT_NE(nullptr, addImage(scene, makeImage(width, height,
                                                  Eg::AprPixelFormat::Gray8,
                                                  makeGrayBands(width, height))));

    ToolpathJobSpec spec;
    spec.defaultParams.powerPercent = 60.0;
    spec.defaultParams.fillIntervalMm = 0.25;
    spec.defaultParams.passes = 2;

    Hw::MotionPlanBuilder builder;
    const ToolpathCompileResult result = MotionPlanCompiler::compile(scene, nullptr, spec, builder);
    ASSERT_TRUE(result.ok) << result.error.toStdString();

    const PlanScan scan = scanPlan(builder.view());
    ASSERT_EQ(2, scan.passCount);

    // 遍次序列必须单调不减（0…0, 1…1），不能出现 0,1,0,1 的逐行交替
    std::vector<double> yByPass[2];
    for (const LitSegment& seg : scan.lit)
    {
        ASSERT_GE(seg.pass, 0);
        ASSERT_LE(seg.pass, 1);
        if (yByPass[seg.pass].empty()
            || std::fabs(seg.y1 - yByPass[seg.pass].back()) > 1e-9)
        {
            yByPass[seg.pass].push_back(seg.y1);
        }
    }
    int32_t lastPass = -1;
    for (const LitSegment& seg : scan.lit)
    {
        EXPECT_GE(seg.pass, lastPass) << "pass index went backwards at command " << seg.commandIndex;
        lastPass = seg.pass;
    }

    // 每一遍都是完整的整图：扫描带数量与 y 序列一致
    ASSERT_FALSE(yByPass[0].empty());
    ASSERT_FALSE(yByPass[1].empty());
    EXPECT_EQ(yByPass[0].size(), yByPass[1].size());
    for (size_t i = 0; i < std::min(yByPass[0].size(), yByPass[1].size()); ++i)
    {
        EXPECT_NEAR(yByPass[0][i], yByPass[1][i], 1e-9) << "pass 1 line " << i;
    }
}

/// 过扫长度符合设定，且整个刀路信封不超出「图像 ∪ 过扫」
TEST(RasterMotionPlanTest, OverscanLengthMatchesSettingAndStaysInEnvelope)
{
    const int32_t width = 64;
    const int32_t height = 64;
    const double overscanMm = 0.5;
    // 过扫长度在库内按整像素取整：lround(0.5mm / 0.0846667mm) = 6px
    const int32_t overscanPx = 6;
    const double overscanMmActual = static_cast<double>(overscanPx) * kPitchMm;

    std::vector<uint8_t> pixels(static_cast<size_t>(width) * static_cast<size_t>(height), 0);

    Eg::SceneManager scene;
    ASSERT_NE(nullptr, addImage(scene, makeImage(width, height,
                                                  Eg::AprPixelFormat::Gray8, pixels)));

    ToolpathJobSpec spec;
    spec.defaultParams.powerPercent = 60.0;
    spec.defaultParams.fillIntervalMm = 0.25;
    // spec.rasterOverscanMm 已从 ToolpathJobSpec 移除

    Hw::MotionPlanBuilder builder;
    const ToolpathCompileResult result = MotionPlanCompiler::compile(scene, nullptr, spec, builder);
    ASSERT_TRUE(result.ok) << result.error.toStdString();

    const Hw::MotionPlanView view = builder.view();
    const PlanScan scan = scanPlan(view);
    ASSERT_FALSE(scan.lit.empty());

    const double widthMm = static_cast<double>(width) * kPitchMm;

    // 1) 出光段严格落在图像内
    EXPECT_TRUE(litWithin(scan, 0.0, widthMm));

    // 2) 过扫以关光行走出现在计划里，每行左右各一条，长度 = 取整后的过扫量
    const size_t rows = uniqueScanYs(scan).size();
    ASSERT_EQ(rows * 2, scan.off.size());
    for (const OffTravel& off : scan.off)
    {
        EXPECT_NEAR(overscanMmActual, std::fabs(off.x1 - off.x0), 1e-9);
        // 过扫段必须整段在图像左/右之外
        const bool leftSide = std::max(off.x0, off.x1) <= 1e-9;
        const bool rightSide = std::min(off.x0, off.x1) >= widthMm - 1e-9;
        EXPECT_TRUE(leftSide || rightSide) << "off travel inside the image at command " << off.commandIndex;
    }

    // 3) 刀路信封 = 图像 ∪ 过扫，不再向外扩张
    EXPECT_NEAR(-overscanMmActual, view.header.boundsMinX, 1e-9);
    EXPECT_NEAR(widthMm + overscanMmActual, view.header.boundsMaxX, 1e-9);
    EXPECT_LE(0.0, view.header.boundsMinY);
    EXPECT_LE(view.header.boundsMaxY, static_cast<double>(height) * kPitchMm);
}

/// 负向：像素缓冲缺失的位图必须显式失败，不许静默产出空刀路
TEST(RasterMotionPlanTest, RasterWithoutPixelBufferFailsLoudly)
{
    const int32_t width = 8;
    const int32_t height = 8;

    // 只给尺寸与角点，不给像素数据 —— 无法扫描
    Eg::AprImage broken;
    broken.nWidth = width;
    broken.nHeight = height;
    broken.ePixelFormat = Eg::AprPixelFormat::Gray8;
    broken.topLeft = Ut::Vec2d(0.0, 0.0);
    broken.topRight = Ut::Vec2d(static_cast<double>(width) * kPitchMm, 0.0);
    broken.bottomLeft = Ut::Vec2d(0.0, static_cast<double>(height) * kPitchMm);
    broken.bottomRight = Ut::Vec2d(static_cast<double>(width) * kPitchMm,
                                   static_cast<double>(height) * kPitchMm);

    Eg::SceneManager scene;
    Eg::SceneWriteGateway::addEntity(scene, &broken);

    ToolpathJobSpec spec;
    spec.defaultParams.powerPercent = 60.0;
    spec.defaultParams.fillIntervalMm = 0.25;

    Hw::MotionPlanBuilder builder;
    const ToolpathCompileResult result = MotionPlanCompiler::compile(scene, nullptr, spec, builder);

    EXPECT_FALSE(result.ok);
    EXPECT_FALSE(result.error.isEmpty());
    EXPECT_EQ(1, result.unmachinableEntityCount);
    ASSERT_FALSE(result.unmachinableDetails.isEmpty());
    EXPECT_FALSE(result.unmachinableDetails.first().isEmpty());
    // 失败路径不得留下半截计划（否则上层会以为有刀路可跑）
    EXPECT_EQ(0u, builder.commandCount());
}

/// 像素缓冲长度与像素格式不匹配时必须显式失败。
/// 截断的 RGB24/RGBA32 缓冲若被当成整图，会越界读取并把半截数据雕到料上。
TEST(RasterMotionPlanTest, MismatchedPixelBufferFailsLoudly)
{
    const int32_t width = 8;
    const int32_t height = 8;

    struct Case
    {
        Eg::AprPixelFormat format;
        size_t bytesPerPixel;
        const char* description;
    };
    const Case cases[] = {
        { Eg::AprPixelFormat::Gray8, 2, "Gray8 buffer with 2 bytes/px" },
        { Eg::AprPixelFormat::RGB24, 2, "RGB24 buffer truncated to 2 bytes/px" },
        { Eg::AprPixelFormat::RGBA32, 3, "RGBA32 buffer truncated to 3 bytes/px" },
    };

    for (const Case& c : cases)
    {
        const size_t byteCount =
            static_cast<size_t>(width) * static_cast<size_t>(height) * c.bytesPerPixel;
        std::vector<uint8_t> pixels(byteCount, 0);
        Eg::AprImage broken = makeImage(width, height, c.format, pixels);

        Eg::SceneManager scene;
        Eg::SceneWriteGateway::addEntity(scene, &broken);

        ToolpathJobSpec spec;
        spec.defaultParams.powerPercent = 60.0;
        spec.defaultParams.fillIntervalMm = 0.25;

        Hw::MotionPlanBuilder builder;
        const ToolpathCompileResult result = MotionPlanCompiler::compile(scene, nullptr, spec, builder);

        EXPECT_FALSE(result.ok) << c.description;
        EXPECT_EQ(1, result.unmachinableEntityCount) << c.description;
        EXPECT_FALSE(result.unmachinableDetails.isEmpty()) << c.description;
        EXPECT_EQ(0u, builder.commandCount()) << c.description;
    }
}

/// 各向异性图：行距必须用纵向像素当量，与请求的毫米值一致；
/// 各向同性图的输出必须与改动前逐项相同（无回归）。
TEST(RasterMotionPlanTest, LineSpacingUsesVerticalPixelPitch)
{
    const int32_t width = 128;
    const int32_t height = 64;
    const double spacingMm = 0.25;
    const double pitchX = kPitchMm;   // 横向：300 DPI
    const double pitchY = 0.2;        // 纵向：明显不同的像素尺寸（各向异性）

    const double widthMm = static_cast<double>(width) * pitchX;
    // 纵向 12.8mm / 0.25mm 应得 51 条扫描带（首条中心 0.125mm，末条 12.625mm）
    const size_t anisotropicLines = 51;

    struct Case
    {
        const char* label;
        double py;
        size_t expectedLines;
        double expectedTotalLitMm;
    };
    // 各向同性数值来自改动前的基线测量，用作回归对照
    const Case cases[] = {
        { "isotropic", pitchX, 22, 178.816 },
        { "anisotropic", pitchY, anisotropicLines, 414.528 },
    };

    for (const Case& c : cases)
    {
        const std::vector<uint8_t> pixels = makeGrayBands(width, height);

        Eg::SceneManager scene;
        ASSERT_NE(nullptr, addImage(scene,
            makeAnisotropicImage(width, height, pitchX, c.py, pixels)));

        ToolpathJobSpec spec;
        spec.defaultParams.powerPercent = 60.0;
        spec.defaultParams.fillIntervalMm = spacingMm;

        Hw::MotionPlanBuilder builder;
        const ToolpathCompileResult result = MotionPlanCompiler::compile(scene, nullptr, spec, builder);
        ASSERT_TRUE(result.ok) << c.label << ": " << result.error.toStdString();

        const Hw::MotionPlanView view = builder.view();
        const PlanScan scan = scanPlan(builder.view());
        ASSERT_FALSE(scan.lit.empty()) << c.label;

        // 1) 实际行距等于请求的毫米值：两个方向都用各自的真实当量
        const std::vector<double> ys = uniqueScanYs(scan);
        ASSERT_GE(ys.size(), 2u) << c.label;
        for (size_t i = 1; i < ys.size(); ++i)
        {
            EXPECT_NEAR(spacingMm, ys[i] - ys[i - 1], 1e-6)
                << c.label << " scan line " << i;
        }

        // 2) 扫描带数量与覆盖率：整高都要被覆盖，不能因当量取错而稀疏
        EXPECT_EQ(c.expectedLines, ys.size()) << c.label;
        EXPECT_NEAR(static_cast<double>(c.expectedLines) * spacingMm - spacingMm,
                    ys.back() - ys.front(), 1e-6) << c.label;

        // 3) 总出光长度：每行只有 3/4 宽度出光，行间距按实际值累加
        EXPECT_NEAR(c.expectedTotalLitMm, scan.totalLitLengthMm, 1e-6) << c.label;

        // 4) 几何仍落在图像内
        EXPECT_TRUE(litWithin(scan, 0.0, widthMm)) << c.label;
        EXPECT_LE(view.header.boundsMaxY, static_cast<double>(height) * c.py + 1e-9) << c.label;
        EXPECT_GE(view.header.boundsMinY, 0.0) << c.label;

        // 5) 极性不受影响：最暗仍为请求功率，最亮不出光
        EXPECT_NEAR(60.0, *std::max_element(scan.laserParamPowers.begin(),
                                            scan.laserParamPowers.end()), 1e-6) << c.label;
    }
}

/// 各向异性时过扫仍按横向当量换算：过扫沿行方向，不受纵向当量影响。
TEST(RasterMotionPlanTest, OverscanUsesHorizontalPixelPitch)
{
    const int32_t width = 128;
    const int32_t height = 64;
    const double pitchX = kPitchMm;
    const double pitchY = 0.2;
    const double overscanMm = 0.5;
    // 横向当量下 lround(0.5 / 0.0846667) = 6px
    const double overscanMmActual = 6.0 * pitchX;

    std::vector<uint8_t> pixels(static_cast<size_t>(width) * static_cast<size_t>(height), 0);

    Eg::SceneManager scene;
    ASSERT_NE(nullptr, addImage(scene,
        makeAnisotropicImage(width, height, pitchX, pitchY, pixels)));

    ToolpathJobSpec spec;
    spec.defaultParams.powerPercent = 60.0;
    spec.defaultParams.fillIntervalMm = 0.25;
    // spec.rasterOverscanMm 已从 ToolpathJobSpec 移除

    Hw::MotionPlanBuilder builder;
    const ToolpathCompileResult result = MotionPlanCompiler::compile(scene, nullptr, spec, builder);
    ASSERT_TRUE(result.ok) << result.error.toStdString();

    const Hw::MotionPlanView view = builder.view();
    const PlanScan scan = scanPlan(view);
    const size_t rows = uniqueScanYs(scan).size();
    ASSERT_EQ(rows * 2, scan.off.size());
    for (const OffTravel& off : scan.off)
    {
        EXPECT_NEAR(overscanMmActual, std::fabs(off.x1 - off.x0), 1e-6);
    }
    EXPECT_NEAR(-overscanMmActual, view.header.boundsMinX, 1e-6);
}

#endif  // ENABLE_HARDWARE
