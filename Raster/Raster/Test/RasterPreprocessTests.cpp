/**
 * @file RasterPreprocessTests.cpp
 * @brief 通用预处理算子的行为测试。
 */

#include "RasterTestSupport.h"

#include <gtest/gtest.h>

using namespace raster_test;

namespace
{
    TEST(RasterPreprocess, InvertFlipsEveryPixel)
    {
        auto source = makeGradient(16, 16);
        RasterImage inverted = nullptr;
        ASSERT_EQ(Raster_Invert(source.get(), &inverted), RASTER_OK);
        ImageGuard guard(inverted);

        for (int y = 0; y < 16; ++y) {
            for (int x = 0; x < 16; ++x) {
                EXPECT_EQ(pixelAt(inverted, x, y), 255 - pixelAt(source.get(), x, y));
            }
        }
    }

    TEST(RasterPreprocess, BrightnessContrastIsApplied)
    {
        auto source = makeGradient(16, 16);
        RasterImage adjusted = nullptr;
        ASSERT_EQ(Raster_AdjustBrightnessContrast(source.get(), 1.0, 10.0, &adjusted), RASTER_OK);
        ImageGuard guard(adjusted);

        for (int y = 0; y < 16; ++y) {
            for (int x = 0; x < 16; ++x) {
                const int expected = std::min(255, pixelAt(source.get(), x, y) + 10);
                EXPECT_EQ(pixelAt(adjusted, x, y), expected);
            }
        }
    }

    TEST(RasterPreprocess, ThresholdOtsuProducesBinaryImage)
    {
        auto source = makeGradient(32, 32);
        RasterImage binary = nullptr;
        ASSERT_EQ(Raster_Threshold(source.get(), RASTER_THRESH_OTSU, 0.0, 255.0, &binary), RASTER_OK);
        ImageGuard guard(binary);

        for (int y = 0; y < 32; ++y) {
            for (int x = 0; x < 32; ++x) {
                const int value = pixelAt(binary, x, y);
                EXPECT_TRUE(value == 0 || value == 255);
            }
        }
    }

    TEST(RasterPreprocess, ThresholdAdaptiveAcceptsBlockSizeInThresholdSlot)
    {
        auto source = makeGradient(32, 32);
        RasterImage binary = nullptr;
        // ADAPTIVE：thresh 复用为 blockSize，maxValue 复用为 C。
        ASSERT_EQ(Raster_Threshold(source.get(), RASTER_THRESH_ADAPTIVE, 15.0, 5.0, &binary), RASTER_OK);
        ImageGuard guard(binary);
        EXPECT_NE(binary, nullptr);
    }

    TEST(RasterPreprocess, ResizeChangesDimensions)
    {
        auto source = makeGradient(40, 20);
        RasterImage resized = nullptr;
        ASSERT_EQ(Raster_Resize(source.get(), 0.5, 0.5, RASTER_INTERP_AREA, &resized), RASTER_OK);
        ImageGuard guard(resized);

        RasterImageInfo info{};
        info.structSize = sizeof(RasterImageInfo);
        ASSERT_EQ(Raster_GetImageInfo(resized, &info), RASTER_OK);
        EXPECT_EQ(info.width, 20);
        EXPECT_EQ(info.height, 10);
    }

    TEST(RasterPreprocess, ResizeRejectsZeroScale)
    {
        auto source = makeGradient(8, 8);
        RasterImage resized = nullptr;
        EXPECT_EQ(Raster_Resize(source.get(), 0.0, 0.0, RASTER_INTERP_LINEAR, &resized),
                  RASTER_E_INVALID_ARG);
    }

    TEST(RasterPreprocess, CropClipsToImageBounds)
    {
        auto source = makeGradient(20, 20);
        RasterImage cropped = nullptr;
        ASSERT_EQ(Raster_Crop(source.get(), 5, 5, 100, 100, &cropped), RASTER_OK);
        ImageGuard guard(cropped);

        RasterImageInfo info{};
        info.structSize = sizeof(RasterImageInfo);
        ASSERT_EQ(Raster_GetImageInfo(cropped, &info), RASTER_OK);
        EXPECT_EQ(info.width, 15);
        EXPECT_EQ(info.height, 15);
    }

    TEST(RasterPreprocess, CropOutsideImageIsRejected)
    {
        auto source = makeGradient(20, 20);
        RasterImage cropped = nullptr;
        EXPECT_EQ(Raster_Crop(source.get(), 100, 100, 10, 10, &cropped), RASTER_E_INVALID_ARG);
    }

    TEST(RasterPreprocess, RotateKeepsCanvasSize)
    {
        auto source = makeGradient(30, 30);
        RasterImage rotated = nullptr;
        ASSERT_EQ(Raster_Rotate(source.get(), 90.0, RASTER_BORDER_CONSTANT, 0.0, &rotated), RASTER_OK);
        ImageGuard guard(rotated);

        RasterImageInfo info{};
        info.structSize = sizeof(RasterImageInfo);
        ASSERT_EQ(Raster_GetImageInfo(rotated, &info), RASTER_OK);
        EXPECT_EQ(info.width, 30);
        EXPECT_EQ(info.height, 30);
    }

    TEST(RasterPreprocess, FlipHorizontalMirrorsPixels)
    {
        auto source = makeGradient(8, 4);
        RasterImage flipped = nullptr;
        ASSERT_EQ(Raster_Flip(source.get(), RASTER_FLIP_HORIZONTAL, &flipped), RASTER_OK);
        ImageGuard guard(flipped);

        for (int y = 0; y < 4; ++y) {
            EXPECT_EQ(pixelAt(flipped, 0, y), pixelAt(source.get(), 7, y));
            EXPECT_EQ(pixelAt(flipped, 7, y), pixelAt(source.get(), 0, y));
        }
    }

    TEST(RasterPreprocess, FlipRejectsUnknownAxis)
    {
        auto source = makeGradient(8, 4);
        RasterImage flipped = nullptr;
        EXPECT_EQ(Raster_Flip(source.get(), 42, &flipped), RASTER_E_INVALID_ARG);
    }

    TEST(RasterPreprocess, MorphologyErodeShrinksBrightRegions)
    {
        auto source = makeCheckerboard(32, 32, 8);
        RasterImage eroded = nullptr;
        ASSERT_EQ(Raster_Morphology(source.get(), RASTER_MORPH_ERODE, RASTER_KERNEL_RECT, 5, &eroded),
                  RASTER_OK);
        ImageGuard guard(eroded);

        // 腐蚀只会减少白像素，不会增加。
        EXPECT_LE(countWhite(eroded), countWhite(source.get()));
    }

    TEST(RasterPreprocess, MorphologyRejectsBadKernelSize)
    {
        auto source = makeGradient(8, 8);
        RasterImage result = nullptr;
        EXPECT_EQ(Raster_Morphology(source.get(), RASTER_MORPH_OPEN, RASTER_KERNEL_RECT, 0, &result),
                  RASTER_E_INVALID_ARG);
    }

    TEST(RasterPreprocess, CannyFindsEdges)
    {
        auto source = makeFrameWithHole(64, 64, 10, 4);
        RasterImage edges = nullptr;
        ASSERT_EQ(Raster_Canny(source.get(), 50.0, 150.0, &edges), RASTER_OK);
        ImageGuard guard(edges);
        EXPECT_GT(countWhite(edges), 0);
    }

    TEST(RasterPreprocess, ClaheProducesSameSize)
    {
        auto source = makeGradient(32, 32);
        RasterImage enhanced = nullptr;
        ASSERT_EQ(Raster_Clahe(source.get(), 2.0, 8, &enhanced), RASTER_OK);
        ImageGuard guard(enhanced);

        RasterImageInfo info{};
        info.structSize = sizeof(RasterImageInfo);
        ASSERT_EQ(Raster_GetImageInfo(enhanced, &info), RASTER_OK);
        EXPECT_EQ(info.width, 32);
        EXPECT_EQ(info.height, 32);
    }

    TEST(RasterPreprocess, DenoiseKeepsDimensions)
    {
        auto source = makeGradient(24, 24);
        RasterImage denoised = nullptr;
        ASSERT_EQ(Raster_Denoise(source.get(), RASTER_DENOISE_FAST_NLMEANS, 10.0, &denoised), RASTER_OK);
        ImageGuard guard(denoised);

        RasterImageInfo info{};
        info.structSize = sizeof(RasterImageInfo);
        ASSERT_EQ(Raster_GetImageInfo(denoised, &info), RASTER_OK);
        EXPECT_EQ(info.width, 24);
        EXPECT_EQ(info.height, 24);
    }

    TEST(RasterPreprocess, DenoiseRejectsUnknownMethod)
    {
        auto source = makeGradient(8, 8);
        RasterImage denoised = nullptr;
        EXPECT_EQ(Raster_Denoise(source.get(), 1234, 1.0, &denoised), RASTER_E_INVALID_ARG);
    }

    TEST(RasterPreprocess, FlatFieldRemovesIlluminationGradient)
    {
        // 造一张带明显照明梯度的图：左暗右亮。
        RasterImage handle = nullptr;
        ASSERT_EQ(Raster_CreateImage(64, 64, RASTER_FORMAT_GRAY8, &handle), RASTER_OK);
        ImageGuard guard(handle);
        RasterImageView view{};
        view.structSize = sizeof(RasterImageView);
        ASSERT_EQ(Raster_GetImageView(handle, &view), RASTER_OK);
        auto* pixels = static_cast<uint8_t*>(const_cast<void*>(view.data));
        for (int y = 0; y < 64; ++y) {
            for (int x = 0; x < 64; ++x) {
                pixels[y * view.stride + x] = static_cast<uint8_t>(60 + x * 2);
            }
        }

        RasterImage corrected = nullptr;
        ASSERT_EQ(Raster_FlatFieldCorrect(handle, 15, &corrected), RASTER_OK);
        ImageGuard correctedGuard(corrected);

        // 校正后左右两端的亮度差应显著小于原始图。
        const int rawDelta = pixelAt(handle, 63, 32) - pixelAt(handle, 0, 32);
        const int fixedDelta = pixelAt(corrected, 63, 32) - pixelAt(corrected, 0, 32);
        EXPECT_GT(rawDelta, 100);
        EXPECT_LT(std::abs(fixedDelta), std::abs(rawDelta));
    }

    TEST(RasterPreprocess, DeskewLeavesAxisAlignedImageUntouched)
    {
        auto source = makeFrameWithHole(64, 64, 8, 4);
        RasterImage deskewed = nullptr;
        ASSERT_EQ(Raster_Deskew(source.get(), &deskewed), RASTER_OK);
        ImageGuard guard(deskewed);

        RasterImageInfo info{};
        info.structSize = sizeof(RasterImageInfo);
        ASSERT_EQ(Raster_GetImageInfo(deskewed, &info), RASTER_OK);
        EXPECT_EQ(info.width, 64);
        EXPECT_EQ(info.height, 64);
    }

    TEST(RasterPreprocess, SharpenWithZeroAmountIsIdentity)
    {
        auto source = makeGradient(16, 16);
        RasterImage sharpened = nullptr;
        ASSERT_EQ(Raster_Sharpen(source.get(), 0.0, &sharpened), RASTER_OK);
        ImageGuard guard(sharpened);

        for (int y = 0; y < 16; ++y) {
            for (int x = 0; x < 16; ++x) {
                EXPECT_EQ(pixelAt(sharpened, x, y), pixelAt(source.get(), x, y));
            }
        }
    }
}  // namespace
