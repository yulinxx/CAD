/**
 * @file RasterContourTests.cpp
 * @brief 轮廓提取、割缝补偿、走刀顺序测试。
 */

#include "RasterTestSupport.h"

#include <gtest/gtest.h>

using namespace raster_test;

namespace
{
    TEST(RasterContour, TraceFindsOuterContourAndHole)
    {
        auto source = makeFrameWithHole(80, 80, 12, 5);
        RasterImage binary = nullptr;
        ASSERT_EQ(Raster_Threshold(source.get(), RASTER_THRESH_OTSU, 0.0, 255.0, &binary), RASTER_OK);
        ImageGuard binaryGuard(binary);

        RasterContourSet set = nullptr;
        ASSERT_EQ(Raster_TraceContours(binary, RASTER_TRACE_ALL, 10.0, 0.0, &set), RASTER_OK);
        ContourGuard guard(set);

        int32_t count = 0;
        ASSERT_EQ(Raster_ContourSetCount(set, &count), RASTER_OK);
        // 一个外框 + 一个圆孔。
        EXPECT_EQ(count, 2);

        int32_t holes = 0;
        for (int32_t i = 0; i < count; ++i) {
            RasterContourInfo info{};
            info.structSize = sizeof(RasterContourInfo);
            ASSERT_EQ(Raster_ContourSetGetInfo(set, i, &info), RASTER_OK);
            EXPECT_GT(info.area, 0.0);
            EXPECT_GT(info.perimeter, 0.0);
            if (info.isHole != 0) {
                ++holes;
            }
        }
        EXPECT_EQ(holes, 1);
    }

    TEST(RasterContour, TraceExternalOnlyReturnsOuterContours)
    {
        auto source = makeFrameWithHole(80, 80, 12, 5);
        RasterContourSet set = nullptr;
        ASSERT_EQ(Raster_TraceContours(source.get(), RASTER_TRACE_EXTERNAL, 10.0, 0.0, &set), RASTER_OK);
        ContourGuard guard(set);

        int32_t count = 0;
        ASSERT_EQ(Raster_ContourSetCount(set, &count), RASTER_OK);
        EXPECT_EQ(count, 1);
    }

    TEST(RasterContour, TraceWithApproximationReducesPointCount)
    {
        auto source = makeFrameWithHole(80, 80, 12, 5);
        RasterContourSet exact = nullptr;
        RasterContourSet simplified = nullptr;
        ASSERT_EQ(Raster_TraceContours(source.get(), RASTER_TRACE_ALL, 10.0, 0.0, &exact), RASTER_OK);
        ASSERT_EQ(Raster_TraceContours(source.get(), RASTER_TRACE_ALL, 10.0, 2.0, &simplified), RASTER_OK);
        ContourGuard exactGuard(exact);
        ContourGuard simplifiedGuard(simplified);

        // 比较总点数：矩形边在 CHAIN_APPROX_SIMPLE 下只有 4 个点，
        // 单看一条轮廓看不出逼近效果，圆孔那条才有大量点。
        int32_t count = 0;
        int32_t simplifiedCount = 0;
        ASSERT_EQ(Raster_ContourSetCount(exact, &count), RASTER_OK);
        ASSERT_EQ(Raster_ContourSetCount(simplified, &simplifiedCount), RASTER_OK);
        ASSERT_EQ(count, simplifiedCount);

        int32_t totalRaw = 0;
        int32_t totalReduced = 0;
        for (int32_t i = 0; i < count; ++i) {
            RasterContour raw{};
            raw.structSize = sizeof(RasterContour);
            RasterContour reduced{};
            reduced.structSize = sizeof(RasterContour);
            ASSERT_EQ(Raster_ContourSetGet(exact, i, &raw), RASTER_OK);
            ASSERT_EQ(Raster_ContourSetGet(simplified, i, &reduced), RASTER_OK);
            EXPECT_LE(reduced.pointCount, raw.pointCount);
            totalRaw += raw.pointCount;
            totalReduced += reduced.pointCount;
        }
        EXPECT_LT(totalReduced, totalRaw);
    }

    TEST(RasterContour, ContourCoordinatesMatchTheImage)
    {
        // 白色方块位于左上角 20x20 区域，轮廓包围盒应与之一致。
        RasterImage handle = nullptr;
        ASSERT_EQ(Raster_CreateImage(80, 80, RASTER_FORMAT_GRAY8, &handle), RASTER_OK);
        ImageGuard guard(handle);
        RasterImageView view{};
        view.structSize = sizeof(RasterImageView);
        ASSERT_EQ(Raster_GetImageView(handle, &view), RASTER_OK);
        auto* pixels = static_cast<uint8_t*>(const_cast<void*>(view.data));
        std::memset(pixels, 0, static_cast<size_t>(view.stride) * 80);
        for (int y = 5; y < 25; ++y) {
            for (int x = 10; x < 30; ++x) {
                pixels[y * view.stride + x] = 255;
            }
        }

        RasterContourSet set = nullptr;
        ASSERT_EQ(Raster_TraceContours(handle, RASTER_TRACE_ALL, 1.0, 0.0, &set), RASTER_OK);
        ContourGuard contourGuard(set);

        int32_t count = 0;
        ASSERT_EQ(Raster_ContourSetCount(set, &count), RASTER_OK);
        ASSERT_EQ(count, 1);

        RasterContourInfo info{};
        info.structSize = sizeof(RasterContourInfo);
        ASSERT_EQ(Raster_ContourSetGetInfo(set, 0, &info), RASTER_OK);
        EXPECT_NEAR(info.minX, 10.0, 2.0);
        EXPECT_NEAR(info.minY, 5.0, 2.0);
        EXPECT_NEAR(info.maxX, 30.0, 2.0);
        EXPECT_NEAR(info.maxY, 25.0, 2.0);
    }

    TEST(RasterContour, KerfCompensationGrowsTheContour)
    {
        auto source = makeFrameWithHole(80, 80, 12, 5);
        RasterContourSet original = nullptr;
        ASSERT_EQ(Raster_TraceContours(source.get(), RASTER_TRACE_ALL, 10.0, 0.0, &original), RASTER_OK);
        ContourGuard originalGuard(original);

        RasterContourSet compensated = nullptr;
        ASSERT_EQ(Raster_KerfCompensate(original, 6.0, RASTER_KERF_OUTSIDE, &compensated), RASTER_OK);
        ContourGuard compensatedGuard(compensated);

        // 补偿之后孔洞可能被外扩吞掉，轮廓条数会变，
        // 所以比较"最大轮廓的面积"而不是逐条按索引比。
        double beforeArea = 0.0;
        double afterArea = 0.0;
        int32_t beforeCount = 0;
        int32_t afterCount = 0;
        ASSERT_EQ(Raster_ContourSetCount(original, &beforeCount), RASTER_OK);
        ASSERT_EQ(Raster_ContourSetCount(compensated, &afterCount), RASTER_OK);
        for (int32_t i = 0; i < beforeCount; ++i) {
            RasterContourInfo info{};
            info.structSize = sizeof(RasterContourInfo);
            ASSERT_EQ(Raster_ContourSetGetInfo(original, i, &info), RASTER_OK);
            beforeArea = std::max(beforeArea, info.area);
        }
        for (int32_t i = 0; i < afterCount; ++i) {
            RasterContourInfo info{};
            info.structSize = sizeof(RasterContourInfo);
            ASSERT_EQ(Raster_ContourSetGetInfo(compensated, i, &info), RASTER_OK);
            afterArea = std::max(afterArea, info.area);
        }

        // 外扩之后轮廓面积应当变大。
        EXPECT_GT(afterArea, beforeArea);
    }

    TEST(RasterContour, KerfCompensationCenterKeepsGeometry)
    {
        auto source = makeFrameWithHole(80, 80, 12, 5);
        RasterContourSet original = nullptr;
        ASSERT_EQ(Raster_TraceContours(source.get(), RASTER_TRACE_ALL, 10.0, 0.0, &original), RASTER_OK);
        ContourGuard originalGuard(original);

        RasterContourSet compensated = nullptr;
        ASSERT_EQ(Raster_KerfCompensate(original, 4.0, RASTER_KERF_CENTER, &compensated), RASTER_OK);
        ContourGuard compensatedGuard(compensated);

        RasterContourInfo before{};
        before.structSize = sizeof(RasterContourInfo);
        RasterContourInfo after{};
        after.structSize = sizeof(RasterContourInfo);
        ASSERT_EQ(Raster_ContourSetGetInfo(original, 0, &before), RASTER_OK);
        ASSERT_EQ(Raster_ContourSetGetInfo(compensated, 0, &after), RASTER_OK);
        EXPECT_NEAR(before.area, after.area, 1.0);
    }

    TEST(RasterContour, KerfCompensationRejectsBadArguments)
    {
        RasterContourSet set = nullptr;
        ASSERT_EQ(Raster_TraceContours(makeFrameWithHole(40, 40, 6, 3).get(), RASTER_TRACE_ALL, 1.0, 0.0,
                                        &set),
                  RASTER_OK);
        ContourGuard guard(set);

        RasterContourSet out = nullptr;
        EXPECT_EQ(Raster_KerfCompensate(set, -1.0, RASTER_KERF_OUTSIDE, &out), RASTER_E_INVALID_ARG);
        EXPECT_EQ(Raster_KerfCompensate(set, 1.0, 99, &out), RASTER_E_INVALID_ARG);
    }

    TEST(RasterContour, OrderInnerFirstPutsHolesFirst)
    {
        auto source = makeFrameWithHole(80, 80, 12, 5);
        RasterContourSet set = nullptr;
        ASSERT_EQ(Raster_TraceContours(source.get(), RASTER_TRACE_ALL, 10.0, 0.0, &set), RASTER_OK);
        ContourGuard guard(set);

        RasterContourSet ordered = nullptr;
        ASSERT_EQ(Raster_OrderContours(set, RASTER_ORDER_INNER_FIRST, &ordered), RASTER_OK);
        ContourGuard orderedGuard(ordered);

        RasterContour first{};
        first.structSize = sizeof(RasterContour);
        ASSERT_EQ(Raster_ContourSetGet(ordered, 0, &first), RASTER_OK);
        EXPECT_EQ(first.isHole, 1);
    }

    TEST(RasterContour, OrderByAreaDescending)
    {
        auto source = makeFrameWithHole(80, 80, 12, 5);
        RasterContourSet set = nullptr;
        ASSERT_EQ(Raster_TraceContours(source.get(), RASTER_TRACE_ALL, 10.0, 0.0, &set), RASTER_OK);
        ContourGuard guard(set);

        RasterContourSet ordered = nullptr;
        ASSERT_EQ(Raster_OrderContours(set, RASTER_ORDER_BY_AREA_DESC, &ordered), RASTER_OK);
        ContourGuard orderedGuard(ordered);

        RasterContourInfo first{};
        first.structSize = sizeof(RasterContourInfo);
        RasterContourInfo second{};
        second.structSize = sizeof(RasterContourInfo);
        ASSERT_EQ(Raster_ContourSetGetInfo(ordered, 0, &first), RASTER_OK);
        ASSERT_EQ(Raster_ContourSetGetInfo(ordered, 1, &second), RASTER_OK);
        EXPECT_GT(first.area, second.area);
    }

    TEST(RasterContour, GetWithOutOfRangeIndexFails)
    {
        auto source = makeFrameWithHole(40, 40, 6, 3);
        RasterContourSet set = nullptr;
        ASSERT_EQ(Raster_TraceContours(source.get(), RASTER_TRACE_ALL, 1.0, 0.0, &set), RASTER_OK);
        ContourGuard guard(set);

        RasterContour contour{};
        contour.structSize = sizeof(RasterContour);
        EXPECT_EQ(Raster_ContourSetGet(set, 999, &contour), RASTER_E_NOT_FOUND);
        EXPECT_EQ(Raster_ContourSetGet(set, -1, &contour), RASTER_E_NOT_FOUND);
    }
}  // namespace
