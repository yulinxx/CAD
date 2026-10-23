/**
 * @file RasterPipelineTests.cpp
 * @brief 管线测试：算子串联、参数校验、失败传播。
 */

#include "RasterTestSupport.h"

#include <gtest/gtest.h>

using namespace raster_test;

namespace
{
    TEST(RasterPipeline, CreateAndDestroy)
    {
        RasterPipeline pipeline = nullptr;
        ASSERT_EQ(Raster_CreatePipeline(&pipeline), RASTER_OK);
        PipelineGuard guard(pipeline);

        int32_t count = -1;
        ASSERT_EQ(Raster_PipelineCount(pipeline, &count), RASTER_OK);
        EXPECT_EQ(count, 0);
    }

    TEST(RasterPipeline, AddOperatorsIncreasesCount)
    {
        RasterPipeline pipeline = nullptr;
        ASSERT_EQ(Raster_CreatePipeline(&pipeline), RASTER_OK);
        PipelineGuard guard(pipeline);

        RasterOpParams params = makeOp(RASTER_OP_TO_GRAY);
        ASSERT_EQ(Raster_PipelineAdd(pipeline, RASTER_OP_TO_GRAY, &params, sizeof(params)), RASTER_OK);

        int32_t count = 0;
        ASSERT_EQ(Raster_PipelineCount(pipeline, &count), RASTER_OK);
        EXPECT_EQ(count, 1);
    }

    TEST(RasterPipeline, ClearRemovesAllOperators)
    {
        RasterPipeline pipeline = nullptr;
        ASSERT_EQ(Raster_CreatePipeline(&pipeline), RASTER_OK);
        PipelineGuard guard(pipeline);

        RasterOpParams params = makeOp(RASTER_OP_INVERT);
        ASSERT_EQ(Raster_PipelineAdd(pipeline, RASTER_OP_INVERT, &params, sizeof(params)), RASTER_OK);
        ASSERT_EQ(Raster_PipelineClear(pipeline), RASTER_OK);

        int32_t count = -1;
        ASSERT_EQ(Raster_PipelineCount(pipeline, &count), RASTER_OK);
        EXPECT_EQ(count, 0);
    }

    TEST(RasterPipeline, AddRejectsUnknownOpAndBadSize)
    {
        RasterPipeline pipeline = nullptr;
        ASSERT_EQ(Raster_CreatePipeline(&pipeline), RASTER_OK);
        PipelineGuard guard(pipeline);

        RasterOpParams params = makeOp(RASTER_OP_TO_GRAY);
        EXPECT_EQ(Raster_PipelineAdd(pipeline, 9999, &params, sizeof(params)), RASTER_E_INVALID_ARG);
        EXPECT_EQ(Raster_PipelineAdd(pipeline, RASTER_OP_TO_GRAY, &params, 3), RASTER_E_STRUCT_SIZE);
    }

    TEST(RasterPipeline, ExecuteChainsOperators)
    {
        auto source = makeGradient(32, 32);
        RasterPipeline pipeline = nullptr;
        ASSERT_EQ(Raster_CreatePipeline(&pipeline), RASTER_OK);
        PipelineGuard guard(pipeline);

        RasterOpParams toGray = makeOp(RASTER_OP_TO_GRAY);
        ASSERT_EQ(Raster_PipelineAdd(pipeline, RASTER_OP_TO_GRAY, &toGray, sizeof(toGray)), RASTER_OK);

        RasterOpParams threshold = makeOp(RASTER_OP_THRESHOLD);
        threshold.i0 = RASTER_THRESH_OTSU;
        threshold.f0 = 0.0;
        threshold.f1 = 255.0;
        ASSERT_EQ(Raster_PipelineAdd(pipeline, RASTER_OP_THRESHOLD, &threshold, sizeof(threshold)),
                  RASTER_OK);

        RasterImage output = nullptr;
        ASSERT_EQ(Raster_PipelineExecute(pipeline, source.get(), &output), RASTER_OK);
        ImageGuard outputGuard(output);

        RasterImageInfo info{};
        info.structSize = sizeof(RasterImageInfo);
        ASSERT_EQ(Raster_GetImageInfo(output, &info), RASTER_OK);
        EXPECT_EQ(info.width, 32);
        EXPECT_EQ(info.height, 32);
        EXPECT_EQ(info.channels, 1);

        for (int y = 0; y < 32; ++y) {
            for (int x = 0; x < 32; ++x) {
                const int value = pixelAt(output, x, y);
                EXPECT_TRUE(value == 0 || value == 255);
            }
        }
    }

    TEST(RasterPipeline, ExecuteOnEmptyPipelineFails)
    {
        auto source = makeGradient(16, 16);
        RasterPipeline pipeline = nullptr;
        ASSERT_EQ(Raster_CreatePipeline(&pipeline), RASTER_OK);
        PipelineGuard guard(pipeline);

        RasterImage output = nullptr;
        EXPECT_EQ(Raster_PipelineExecute(pipeline, source.get(), &output), RASTER_E_EMPTY);
    }

    TEST(RasterPipeline, ExecuteReportsFailingOperator)
    {
        auto source = makeGradient(16, 16);
        RasterPipeline pipeline = nullptr;
        ASSERT_EQ(Raster_CreatePipeline(&pipeline), RASTER_OK);
        PipelineGuard guard(pipeline);

        // 裁剪区域完全在图外 → 算子失败，管线必须把错误如实上报。
        RasterOpParams crop = makeOp(RASTER_OP_CROP);
        crop.i0 = 1000;
        crop.i1 = 1000;
        crop.i2 = 10;
        crop.i3 = 10;
        ASSERT_EQ(Raster_PipelineAdd(pipeline, RASTER_OP_CROP, &crop, sizeof(crop)), RASTER_OK);

        RasterImage output = nullptr;
        EXPECT_EQ(Raster_PipelineExecute(pipeline, source.get(), &output), RASTER_E_INVALID_ARG);
        EXPECT_EQ(output, nullptr);
    }

    TEST(RasterPipeline, ErodeBorderRemovesEdgePixels)
    {
        RasterImage handle = nullptr;
        ASSERT_EQ(Raster_CreateImage(32, 32, RASTER_FORMAT_GRAY8, &handle), RASTER_OK);
        ImageGuard guard(handle);
        RasterImageView view{};
        view.structSize = sizeof(RasterImageView);
        ASSERT_EQ(Raster_GetImageView(handle, &view), RASTER_OK);
        auto* pixels = static_cast<uint8_t*>(const_cast<void*>(view.data));
        std::memset(pixels, 255, static_cast<size_t>(view.stride) * 32);

        RasterPipeline pipeline = nullptr;
        ASSERT_EQ(Raster_CreatePipeline(&pipeline), RASTER_OK);
        PipelineGuard pipelineGuard(pipeline);

        RasterOpParams erode = makeOp(RASTER_OP_ERODE_BORDER);
        erode.i0 = 4;
        ASSERT_EQ(Raster_PipelineAdd(pipeline, RASTER_OP_ERODE_BORDER, &erode, sizeof(erode)), RASTER_OK);

        RasterImage output = nullptr;
        ASSERT_EQ(Raster_PipelineExecute(pipeline, handle, &output), RASTER_OK);
        ImageGuard outputGuard(output);

        EXPECT_EQ(pixelAt(output, 0, 0), 0);
        EXPECT_EQ(pixelAt(output, 16, 16), 255);
    }
}  // namespace
