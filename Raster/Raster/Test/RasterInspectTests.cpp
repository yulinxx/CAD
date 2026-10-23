/**
 * @file RasterInspectTests.cpp
 * @brief 统计与质检测试：灰度统计、直方图、缺陷检测、两图比对。
 */

#include "RasterTestSupport.h"

#include <gtest/gtest.h>

using namespace raster_test;

namespace
{
    TEST(RasterInspect, StatsDescribeTheImage)
    {
        auto source = makeGradient(32, 32);
        RasterStats stats{};
        stats.structSize = sizeof(RasterStats);
        ASSERT_EQ(Raster_ComputeStats(source.get(), &stats), RASTER_OK);

        EXPECT_EQ(stats.width, 32);
        EXPECT_EQ(stats.height, 32);
        EXPECT_NEAR(stats.min, 0.0, 1.0);
        EXPECT_NEAR(stats.max, 255.0, 1.0);
        EXPECT_NEAR(stats.mean, 127.5, 8.0);
        EXPECT_GT(stats.stdDev, 0.0);
        EXPECT_GE(stats.inkRatio, 0.0);
        EXPECT_LE(stats.inkRatio, 1.0);
    }

    TEST(RasterInspect, InkRatioReflectsDarkCoverage)
    {
        RasterImage handle = nullptr;
        ASSERT_EQ(Raster_CreateImage(10, 10, RASTER_FORMAT_GRAY8, &handle), RASTER_OK);
        ImageGuard guard(handle);
        RasterImageView view{};
        view.structSize = sizeof(RasterImageView);
        ASSERT_EQ(Raster_GetImageView(handle, &view), RASTER_OK);
        auto* pixels = static_cast<uint8_t*>(const_cast<void*>(view.data));
        std::memset(pixels, 0, static_cast<size_t>(view.stride) * 10);
        for (int y = 0; y < 5; ++y) {
            for (int x = 0; x < 10; ++x) {
                pixels[y * view.stride + x] = 255;
            }
        }

        RasterStats stats{};
        stats.structSize = sizeof(RasterStats);
        ASSERT_EQ(Raster_ComputeStats(handle, &stats), RASTER_OK);
        EXPECT_NEAR(stats.inkRatio, 0.5, 0.01);
    }

    TEST(RasterInspect, HistogramCountsAllPixels)
    {
        auto source = makeGradient(16, 16);
        std::vector<uint32_t> bins(256, 0);
        RasterHistogram histogram{};
        histogram.structSize = sizeof(RasterHistogram);
        histogram.binCount = static_cast<int32_t>(bins.size());
        histogram.bins = bins.data();

        ASSERT_EQ(Raster_Histogram(source.get(), &histogram), RASTER_OK);
        EXPECT_EQ(histogram.binCount, 256);

        uint64_t total = 0;
        for (uint32_t value : bins) {
            total += value;
        }
        EXPECT_EQ(total, static_cast<uint64_t>(16 * 16));
    }

    TEST(RasterInspect, HistogramRejectsMissingBuffer)
    {
        auto source = makeGradient(8, 8);
        RasterHistogram histogram{};
        histogram.structSize = sizeof(RasterHistogram);
        histogram.binCount = 0;
        histogram.bins = nullptr;
        EXPECT_EQ(Raster_Histogram(source.get(), &histogram), RASTER_E_INVALID_ARG);
    }

    TEST(RasterInspect, DefectDetectionFindsBrightSpot)
    {
        // 灰底上放一个明显的白点。
        RasterImage handle = nullptr;
        ASSERT_EQ(Raster_CreateImage(64, 64, RASTER_FORMAT_GRAY8, &handle), RASTER_OK);
        ImageGuard guard(handle);
        RasterImageView view{};
        view.structSize = sizeof(RasterImageView);
        ASSERT_EQ(Raster_GetImageView(handle, &view), RASTER_OK);
        auto* pixels = static_cast<uint8_t*>(const_cast<void*>(view.data));
        for (int y = 0; y < 64; ++y) {
            for (int x = 0; x < 64; ++x) {
                pixels[y * view.stride + x] = 100;
            }
        }
        // 亮点必须**小于**形态学核（3x3），否则开运算会把它整体保留下来，
        // top-hat 残差接近 0，检不出来。2x2 正好被 3x3 开运算吃掉。
        for (int y = 30; y < 32; ++y) {
            for (int x = 30; x < 32; ++x) {
                pixels[y * view.stride + x] = 255;
            }
        }

        RasterDefectSet defects = nullptr;
        ASSERT_EQ(Raster_DetectDefects(handle, 0.9, 4.0, &defects), RASTER_OK);
        DefectGuard defectGuard(defects);

        int32_t count = 0;
        ASSERT_EQ(Raster_DefectSetCount(defects, &count), RASTER_OK);
        ASSERT_GT(count, 0);

        RasterDefect first{};
        first.structSize = sizeof(RasterDefect);
        ASSERT_EQ(Raster_DefectSetGet(defects, 0, &first), RASTER_OK);
        EXPECT_GE(first.area, 4.0);
        EXPECT_GE(first.x, 28);
        EXPECT_LE(first.x + first.width, 36);
    }

    TEST(RasterInspect, DefectDetectionRejectsOutOfRangeSensitivity)
    {
        auto source = makeGradient(16, 16);
        RasterDefectSet defects = nullptr;
        EXPECT_EQ(Raster_DetectDefects(source.get(), 2.0, 1.0, &defects), RASTER_E_INVALID_ARG);
        EXPECT_EQ(Raster_DetectDefects(source.get(), -0.5, 1.0, &defects), RASTER_E_INVALID_ARG);
    }

    TEST(RasterInspect, DefectSetGetWithOutOfRangeIndexFails)
    {
        auto source = makeGradient(16, 16);
        RasterDefectSet defects = nullptr;
        ASSERT_EQ(Raster_DetectDefects(source.get(), 0.5, 1.0, &defects), RASTER_OK);
        DefectGuard guard(defects);

        RasterDefect defect{};
        defect.structSize = sizeof(RasterDefect);
        EXPECT_EQ(Raster_DefectSetGet(defects, 1000, &defect), RASTER_E_NOT_FOUND);
    }

    TEST(RasterInspect, CompareIdenticalImagesReportsFullSimilarity)
    {
        auto source = makeGradient(24, 24);
        RasterCompareResult result{};
        result.structSize = sizeof(RasterCompareResult);
        ASSERT_EQ(Raster_CompareImages(source.get(), source.get(), &result), RASTER_OK);

        EXPECT_EQ(result.differingPixels, 0);
        EXPECT_NEAR(result.meanAbsDiff, 0.0, 1e-9);
        EXPECT_NEAR(result.rmse, 0.0, 1e-9);
        EXPECT_NEAR(result.similarity, 1.0, 1e-6);
    }

    TEST(RasterInspect, CompareDifferentImagesReportsDifference)
    {
        auto left = makeGradient(24, 24);
        auto right = makeGradient(24, 24);
        RasterImage inverted = nullptr;
        ASSERT_EQ(Raster_Invert(right.get(), &inverted), RASTER_OK);
        ImageGuard invertedGuard(inverted);

        RasterCompareResult result{};
        result.structSize = sizeof(RasterCompareResult);
        ASSERT_EQ(Raster_CompareImages(left.get(), inverted, &result), RASTER_OK);
        EXPECT_GT(result.differingPixels, 0);
        EXPECT_GT(result.rmse, 0.0);
        EXPECT_LT(result.similarity, 1.0);
    }

    TEST(RasterInspect, CompareRejectsDifferentSizes)
    {
        auto left = makeGradient(16, 16);
        auto right = makeGradient(32, 32);
        RasterCompareResult result{};
        result.structSize = sizeof(RasterCompareResult);
        EXPECT_EQ(Raster_CompareImages(left.get(), right.get(), &result), RASTER_E_INVALID_ARG);
    }
}  // namespace
