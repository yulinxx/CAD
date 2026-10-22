/**
 * @file RasterLaserTests.cpp
 * @brief 激光工艺算子测试：抖动、半调、去孤点、高度图、1 bit 打包。
 */

#include "RasterTestSupport.h"

#include <gtest/gtest.h>

using namespace raster_test;

namespace
{
    TEST(RasterLaser, DitherProducesBinaryImage)
    {
        auto source = makeGradient(32, 32);
        RasterImage dithered = nullptr;
        ASSERT_EQ(Raster_Dither(source.get(), RASTER_DITHER_FLOYD_STEINBERG, &dithered), RASTER_OK);
        ImageGuard guard(dithered);

        for (int y = 0; y < 32; ++y) {
            for (int x = 0; x < 32; ++x) {
                const int value = pixelAt(dithered, x, y);
                EXPECT_TRUE(value == 0 || value == 255);
            }
        }
    }

    TEST(RasterLaser, DitherMethodsProduceDifferentPatterns)
    {
        auto source = makeGradient(64, 64);
        RasterImage fs = nullptr;
        RasterImage atkinson = nullptr;
        RasterImage bayer = nullptr;
        ASSERT_EQ(Raster_Dither(source.get(), RASTER_DITHER_FLOYD_STEINBERG, &fs), RASTER_OK);
        ASSERT_EQ(Raster_Dither(source.get(), RASTER_DITHER_ATKINSON, &atkinson), RASTER_OK);
        ASSERT_EQ(Raster_Dither(source.get(), RASTER_DITHER_BAYER_8X8, &bayer), RASTER_OK);
        ImageGuard fsGuard(fs);
        ImageGuard atkinsonGuard(atkinson);
        ImageGuard bayerGuard(bayer);

        EXPECT_NE(countWhite(fs), countWhite(atkinson));
        EXPECT_NE(countWhite(fs), countWhite(bayer));
    }

    TEST(RasterLaser, DitherRejectsUnknownMethod)
    {
        auto source = makeGradient(16, 16);
        RasterImage dithered = nullptr;
        EXPECT_EQ(Raster_Dither(source.get(), 777, &dithered), RASTER_E_INVALID_ARG);
    }

    TEST(RasterLaser, HalftoneProducesBinaryImage)
    {
        auto source = makeGradient(48, 48);
        RasterImage screened = nullptr;
        ASSERT_EQ(Raster_Halftone(source.get(), 6, 45.0, RASTER_HALFTONE_DOT, &screened), RASTER_OK);
        ImageGuard guard(screened);

        for (int y = 0; y < 48; ++y) {
            for (int x = 0; x < 48; ++x) {
                const int value = pixelAt(screened, x, y);
                EXPECT_TRUE(value == 0 || value == 255);
            }
        }
    }

    TEST(RasterLaser, HalftoneRejectsTinyCell)
    {
        auto source = makeGradient(16, 16);
        RasterImage screened = nullptr;
        EXPECT_EQ(Raster_Halftone(source.get(), 1, 45.0, RASTER_HALFTONE_DOT, &screened),
                  RASTER_E_INVALID_ARG);
    }

    TEST(RasterLaser, DespeckleRemovesTinyIslands)
    {
        // 白底 + 一个 2x2 的小白点：面积小于阈值，应被抹掉。
        RasterImage handle = nullptr;
        ASSERT_EQ(Raster_CreateImage(32, 32, RASTER_FORMAT_GRAY8, &handle), RASTER_OK);
        ImageGuard guard(handle);
        RasterImageView view{};
        view.structSize = sizeof(RasterImageView);
        ASSERT_EQ(Raster_GetImageView(handle, &view), RASTER_OK);
        auto* pixels = static_cast<uint8_t*>(const_cast<void*>(view.data));
        std::memset(pixels, 0, static_cast<size_t>(view.stride) * 32);
        for (int y = 10; y < 12; ++y) {
            for (int x = 10; x < 12; ++x) {
                pixels[y * view.stride + x] = 255;
            }
        }

        RasterImage cleaned = nullptr;
        ASSERT_EQ(Raster_Despeckle(handle, 0, 16, &cleaned), RASTER_OK);
        ImageGuard cleanedGuard(cleaned);

        EXPECT_EQ(countWhite(cleaned), 0);
    }

    TEST(RasterLaser, DespeckleKeepsLargeRegions)
    {
        auto source = makeCheckerboard(32, 32, 16);
        RasterImage cleaned = nullptr;
        ASSERT_EQ(Raster_Despeckle(source.get(), 0, 16, &cleaned), RASTER_OK);
        ImageGuard guard(cleaned);
        EXPECT_GT(countWhite(cleaned), 0);
    }

    TEST(RasterLaser, DespeckleHandlesManySmallComponents)
    {
        // 回归守卫：早先的实现是"每个连通域做一次全图比较 + 掩码填充"，
        // 复杂度 O(连通域数 × 像素数)。在 512x512 上撒 2000 个小点时，
        // 旧实现要几十秒，新实现是毫秒级 —— 这个用例就是那个 bug 的守门人。
        RasterImage handle = nullptr;
        ASSERT_EQ(Raster_CreateImage(512, 512, RASTER_FORMAT_GRAY8, &handle), RASTER_OK);
        ImageGuard guard(handle);
        RasterImageView view{};
        view.structSize = sizeof(RasterImageView);
        ASSERT_EQ(Raster_GetImageView(handle, &view), RASTER_OK);
        auto* pixels = static_cast<uint8_t*>(const_cast<void*>(view.data));
        std::memset(pixels, 0, static_cast<size_t>(view.stride) * 512);

        // 规则网格上的孤立白点：每个点都是独立连通域。
        int dots = 0;
        for (int y = 2; y < 510; y += 16) {
            for (int x = 2; x < 510; x += 16) {
                pixels[y * view.stride + x] = 255;
                ++dots;
            }
        }
        EXPECT_GT(dots, 900);

        RasterImage cleaned = nullptr;
        ASSERT_EQ(Raster_Despeckle(handle, 0, 4, &cleaned), RASTER_OK);
        ImageGuard cleanedGuard(cleaned);
        EXPECT_EQ(countWhite(cleaned), 0);
    }

    TEST(RasterLaser, DespeckleRejectsNegativeArguments)
    {
        auto source = makeCheckerboard(16, 16, 8);
        RasterImage cleaned = nullptr;
        EXPECT_EQ(Raster_Despeckle(source.get(), 0, -1, &cleaned), RASTER_E_INVALID_ARG);
        EXPECT_EQ(Raster_Despeckle(source.get(), -1, 1, &cleaned), RASTER_E_INVALID_ARG);
    }

    TEST(RasterLaser, HeightMapScalesIntoRequestedRange)
    {
        auto source = makeGradient(16, 16);
        RasterImage height = nullptr;
        ASSERT_EQ(Raster_HeightMapFromGray(source.get(), 0.25, 1.0, &height), RASTER_OK);
        ImageGuard guard(height);

        RasterImageView view{};
        view.structSize = sizeof(RasterImageView);
        ASSERT_EQ(Raster_GetImageView(height, &view), RASTER_OK);
        EXPECT_EQ(view.channels, 1);
        const auto* pixels = static_cast<const uint16_t*>(view.data);

        // 最暗的像素应落在下界附近，最亮的落在上界附近。
        const uint16_t minValue = pixels[0];
        const uint16_t maxValue = pixels[view.stride / sizeof(uint16_t) - 1];
        EXPECT_GE(minValue, static_cast<uint16_t>(0.25 * 65535.0 - 2.0));
        EXPECT_LE(maxValue, static_cast<uint16_t>(65535.0));
        EXPECT_GT(maxValue, minValue);
    }

    TEST(RasterLaser, HeightMapRejectsInvalidRange)
    {
        auto source = makeGradient(8, 8);
        RasterImage height = nullptr;
        EXPECT_EQ(Raster_HeightMapFromGray(source.get(), 0.8, 0.2, &height), RASTER_E_INVALID_ARG);
        EXPECT_EQ(Raster_HeightMapFromGray(source.get(), -0.1, 1.0, &height), RASTER_E_INVALID_ARG);
    }

    TEST(RasterLaser, PackToOneBitMatchesExpectedLayout)
    {
        // 8x2 的图：第一行 1010_1010，第二行全 0。
        RasterImage handle = nullptr;
        ASSERT_EQ(Raster_CreateImage(8, 2, RASTER_FORMAT_GRAY8, &handle), RASTER_OK);
        ImageGuard guard(handle);
        RasterImageView view{};
        view.structSize = sizeof(RasterImageView);
        ASSERT_EQ(Raster_GetImageView(handle, &view), RASTER_OK);
        auto* pixels = static_cast<uint8_t*>(const_cast<void*>(view.data));
        std::memset(pixels, 0, static_cast<size_t>(view.stride) * 2);
        for (int x = 0; x < 8; x += 2) {
            pixels[x] = 255;
        }

        std::vector<uint8_t> buffer(16, 0xAB);
        size_t written = 0;
        int32_t strideBytes = 0;
        ASSERT_EQ(Raster_PackToOneBit(handle, buffer.data(), buffer.size(), &written, &strideBytes),
                  RASTER_OK);

        EXPECT_EQ(strideBytes, 1);
        EXPECT_EQ(written, static_cast<size_t>(2));
        EXPECT_EQ(buffer[0], 0xAA);
        EXPECT_EQ(buffer[1], 0x00);
    }

    TEST(RasterLaser, PackToOneBitRejectsSmallBuffer)
    {
        auto source = makeGradient(16, 16);
        std::vector<uint8_t> buffer(4, 0);
        EXPECT_EQ(Raster_PackToOneBit(source.get(), buffer.data(), buffer.size(), nullptr, nullptr),
                  RASTER_E_BUFFER_TOO_SMALL);
    }
}  // namespace
