/**
 * @file RasterImageTests.cpp
 * @brief 图像句柄测试：创建、拷贝、视图、像素搬运、格式转换。
 */

#include "RasterTestSupport.h"

#include <gtest/gtest.h>

using namespace raster_test;

namespace
{
    TEST(RasterImage, CreateInitialisesZeroedPixels)
    {
        RasterImage image = nullptr;
        ASSERT_EQ(Raster_CreateImage(16, 12, RASTER_FORMAT_GRAY8, &image), RASTER_OK);
        ImageGuard guard(image);

        RasterImageInfo info{};
        info.structSize = sizeof(RasterImageInfo);
        ASSERT_EQ(Raster_GetImageInfo(image, &info), RASTER_OK);
        EXPECT_EQ(info.width, 16);
        EXPECT_EQ(info.height, 12);
        EXPECT_EQ(info.channels, 1);
        EXPECT_EQ(info.format, static_cast<int32_t>(RASTER_FORMAT_GRAY8));
        EXPECT_EQ(info.dataSize, static_cast<size_t>(16 * 12));

        for (int y = 0; y < 12; ++y) {
            for (int x = 0; x < 16; ++x) {
                EXPECT_EQ(pixelAt(image, x, y), 0);
            }
        }
    }

    TEST(RasterImage, CloneProducesIndependentCopy)
    {
        auto source = makeGradient(8, 8);
        RasterImage copy = nullptr;
        ASSERT_EQ(Raster_CloneImage(source.get(), &copy), RASTER_OK);
        ImageGuard guard(copy);

        // 改副本不应影响原件。
        RasterImageView view{};
        view.structSize = sizeof(RasterImageView);
        ASSERT_EQ(Raster_GetImageView(copy, &view), RASTER_OK);
        auto* pixels = static_cast<uint8_t*>(const_cast<void*>(view.data));
        pixels[0] = 200;

        EXPECT_NE(pixelAt(copy, 0, 0), pixelAt(source.get(), 0, 0));
    }

    TEST(RasterImage, CreateFromViewCopiesCallerMemory)
    {
        std::vector<uint8_t> source(4 * 4, 77);
        RasterImageView view{};
        view.structSize = sizeof(RasterImageView);
        view.width = 4;
        view.height = 4;
        view.channels = 1;
        view.format = static_cast<int32_t>(RASTER_FORMAT_GRAY8);
        view.data = source.data();
        view.stride = 0;

        RasterImage image = nullptr;
        ASSERT_EQ(Raster_CreateImageFromView(&view, &image), RASTER_OK);
        ImageGuard guard(image);

        EXPECT_EQ(pixelAt(image, 3, 3), 77);

        // 改动调用方内存不应影响库内副本。
        source[0] = 0;
        EXPECT_EQ(pixelAt(image, 0, 0), 77);
    }

    TEST(RasterImage, CreateFromViewRejectsBadArguments)
    {
        RasterImage image = nullptr;
        RasterImageView view{};
        view.structSize = sizeof(RasterImageView);
        view.width = 0;
        view.height = 4;
        view.channels = 1;
        view.format = static_cast<int32_t>(RASTER_FORMAT_GRAY8);
        view.data = nullptr;
        EXPECT_EQ(Raster_CreateImageFromView(&view, &image), RASTER_E_INVALID_ARG);

        view.width = 4;
        view.height = 4;
        view.channels = 1;
        view.format = static_cast<int32_t>(RASTER_FORMAT_GRAY8);
        view.data = nullptr;
        EXPECT_EQ(Raster_CreateImageFromView(&view, &image), RASTER_E_INVALID_ARG);
    }

    TEST(RasterImage, CopyImageToHonoursStride)
    {
        // 每行取值不同（行 y 全为 (y+1)*10），才能验证"第 y 行落在 y*stride 处"。
        RasterImage handle = nullptr;
        ASSERT_EQ(Raster_CreateImage(5, 3, RASTER_FORMAT_GRAY8, &handle), RASTER_OK);
        ImageGuard guard(handle);
        RasterImageView view{};
        view.structSize = sizeof(RasterImageView);
        ASSERT_EQ(Raster_GetImageView(handle, &view), RASTER_OK);
        auto* pixels = static_cast<uint8_t*>(const_cast<void*>(view.data));
        for (int y = 0; y < 3; ++y) {
            for (int x = 0; x < 5; ++x) {
                pixels[y * view.stride + x] = static_cast<uint8_t>((y + 1) * 10);
            }
        }

        std::vector<uint8_t> destination(5 * 10 + 16, 0);
        ASSERT_EQ(Raster_CopyImageTo(handle, destination.data(), destination.size(), 10), RASTER_OK);
        // 第 1 行起点应是第 10 个字节处，而不是第 5 个字节处。
        EXPECT_EQ(destination[0], 10u);
        EXPECT_EQ(destination[5], 0u);
        EXPECT_EQ(destination[10], 20u);
        EXPECT_EQ(destination[20], 30u);
    }

    TEST(RasterImage, CopyImageToRejectsSmallBuffer)
    {
        auto source = makeGradient(8, 8);
        std::vector<uint8_t> destination(8, 0);
        EXPECT_EQ(Raster_CopyImageTo(source.get(), destination.data(), destination.size(), 0),
                  RASTER_E_BUFFER_TOO_SMALL);
    }

    TEST(RasterImage, ToGrayFromColour)
    {
        RasterImage colour = nullptr;
        ASSERT_EQ(Raster_CreateImage(4, 4, RASTER_FORMAT_BGR8, &colour), RASTER_OK);
        ImageGuard colourGuard(colour);

        RasterImage gray = nullptr;
        ASSERT_EQ(Raster_ToGray(colour, &gray), RASTER_OK);
        ImageGuard grayGuard(gray);

        RasterImageInfo info{};
        info.structSize = sizeof(RasterImageInfo);
        ASSERT_EQ(Raster_GetImageInfo(gray, &info), RASTER_OK);
        EXPECT_EQ(info.channels, 1);
        EXPECT_EQ(info.format, static_cast<int32_t>(RASTER_FORMAT_GRAY8));
    }

    TEST(RasterImage, Gray16IsSupported)
    {
        auto gray = makeGradient(8, 8);
        RasterImage height = nullptr;
        ASSERT_EQ(Raster_HeightMapFromGray(gray.get(), 0.0, 1.0, &height), RASTER_OK);
        ImageGuard guard(height);

        RasterImageInfo info{};
        info.structSize = sizeof(RasterImageInfo);
        ASSERT_EQ(Raster_GetImageInfo(height, &info), RASTER_OK);
        EXPECT_EQ(info.channels, 1);
        EXPECT_EQ(info.format, static_cast<int32_t>(RASTER_FORMAT_GRAY16));
        EXPECT_EQ(info.dataSize, static_cast<size_t>(8 * 8 * 2));
    }

    TEST(RasterImage, BorrowedViewPointsInsideLibrary)
    {
        auto source = makeGradient(4, 4);
        RasterImageView view{};
        view.structSize = sizeof(RasterImageView);
        ASSERT_EQ(Raster_GetImageView(source.get(), &view), RASTER_OK);
        EXPECT_NE(view.data, nullptr);
        EXPECT_EQ(view.width, 4);
        EXPECT_EQ(view.height, 4);
        EXPECT_GT(view.stride, 0);
    }
}  // namespace
