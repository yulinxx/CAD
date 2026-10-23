/**
 * @file RasterApiTests.cpp
 * @brief C ABI 契约测试：版本、状态码、错误信息、参数校验、ABI 兼容性。
 */

#include "RasterTestSupport.h"

#include <gtest/gtest.h>

using namespace raster_test;

namespace
{
    TEST(RasterApi, VersionIsSelfConsistent)
    {
        const uint32_t version = Raster_GetVersion();
        EXPECT_EQ(version, RASTER_VERSION);
        EXPECT_EQ(Raster_GetVersion() & 0xFFFF0000u, static_cast<uint32_t>(RASTER_VERSION_MAJOR) << 16);
        EXPECT_EQ(Raster_GetAbiVersion(), static_cast<uint32_t>(RASTER_ABI_VERSION));
    }

    TEST(RasterApi, VersionStringIsStable)
    {
        const char* first = Raster_GetVersionString();
        ASSERT_NE(first, nullptr);
        EXPECT_NE(first[0], '\0');
        EXPECT_STREQ(first, Raster_GetVersionString());
    }

    TEST(RasterApi, VersionStringMatchesTheHeaderMacros)
    {
        // 版本串与数字宏必须同源：宏在公开头里，消费方也会读它。
        EXPECT_STREQ(Raster_GetVersionString(), RASTER_VERSION_STRING);
    }

    TEST(RasterApi, StatusTextNeverReturnsNull)
    {
        EXPECT_STREQ(Raster_StatusText(RASTER_OK), "OK");
        EXPECT_NE(Raster_StatusText(-99999), nullptr);
        EXPECT_STRNE(Raster_StatusText(-99999), "");
    }

    TEST(RasterApi, OpenCvVersionIsReported)
    {
        EXPECT_GT(Raster_GetOpenCvVersion(), 0u);
    }

    TEST(RasterApi, LastErrorMessageReportsFailure)
    {
        RasterImage image = nullptr;
        EXPECT_EQ(Raster_CreateImage(0, 10, RASTER_FORMAT_GRAY8, &image), RASTER_E_INVALID_ARG);

        char buffer[256] = {0};
        const int written = Raster_GetLastErrorMessage(buffer, sizeof(buffer));
        EXPECT_GT(written, 0);
        EXPECT_NE(std::string(buffer).find("dimensions"), std::string::npos);
    }

    TEST(RasterApi, LastErrorMessageRejectsTinyBuffer)
    {
        RasterImage image = nullptr;
        EXPECT_EQ(Raster_CreateImage(-1, -1, RASTER_FORMAT_GRAY8, &image), RASTER_E_INVALID_ARG);

        char tiny[4] = {0};
        EXPECT_LT(Raster_GetLastErrorMessage(tiny, sizeof(tiny)), 0);

        char none[4] = {0};
        EXPECT_EQ(Raster_GetLastErrorMessage(none, 0), -1);
        EXPECT_EQ(Raster_GetLastErrorMessage(nullptr, 16), -1);
    }

    TEST(RasterApi, NullArgumentsAreRejected)
    {
        RasterImage image = nullptr;
        EXPECT_EQ(Raster_CreateImage(10, 10, RASTER_FORMAT_GRAY8, nullptr), RASTER_E_INVALID_ARG);
        EXPECT_EQ(Raster_CloneImage(nullptr, &image), RASTER_E_INVALID_ARG);
        EXPECT_EQ(Raster_ToGray(nullptr, &image), RASTER_E_INVALID_ARG);
        EXPECT_EQ(Raster_Denoise(nullptr, RASTER_DENOISE_MEDIAN, 3.0, &image), RASTER_E_INVALID_ARG);
    }

    TEST(RasterApi, StructSizeMismatchIsRejected)
    {
        // 模拟"调用方用的是旧版头文件"：structSize 不认识时必须拒绝，
        // 而不是按新布局去读调用方的栈内存。
        RasterImage image = nullptr;
        ASSERT_EQ(Raster_CreateImage(8, 8, RASTER_FORMAT_GRAY8, &image), RASTER_OK);
        ImageGuard guard(image);

        RasterImageInfo info{};
        info.structSize = static_cast<int32_t>(sizeof(RasterImageInfo) + 16);
        EXPECT_EQ(Raster_GetImageInfo(image, &info), RASTER_E_STRUCT_SIZE);

        RasterStats stats{};
        stats.structSize = 4;
        EXPECT_EQ(Raster_ComputeStats(image, &stats), RASTER_E_STRUCT_SIZE);
    }

    TEST(RasterApi, DestroyingNullHandlesIsSafe)
    {
        Raster_DestroyImage(nullptr);
        Raster_DestroyContourSet(nullptr);
        Raster_DestroyDefectSet(nullptr);
        Raster_DestroyPipeline(nullptr);
    }

    TEST(RasterApi, UnknownPixelFormatIsRejected)
    {
        RasterImage image = nullptr;
        EXPECT_EQ(Raster_CreateImage(8, 8, 99, &image), RASTER_E_INVALID_ARG);
        EXPECT_EQ(image, nullptr);
    }
}  // namespace
