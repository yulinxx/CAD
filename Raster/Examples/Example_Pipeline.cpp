/**
 * @file Example_Pipeline.cpp
 * @brief 管线用法：把一套雕刻前处理固化成可复用的工艺流程。
 *
 * 为什么要用管线而不是一串函数调用：
 *   同一套前处理要同时跑在调试机、生产机和客户自研软件上，
 *   管线是那份**唯一**的定义——可存、可传、可复现。
 */

#include <Raster/RasterCpp.h>

#include <cstdio>
#include <string>

int main(int argc, char** argv)
{
    const std::string input = (argc > 1) ? argv[1] : "input.png";
    const std::string output = (argc > 2) ? argv[2] : "output_pipeline.png";

    try {
        raster::Pipeline pipeline = raster::Pipeline::create();

        // 平场：扣除相机照明不均（暗场下的固定照度梯度）。
        RasterOpParams flat{};
        flat.structSize = sizeof(RasterOpParams);
        flat.opId = RASTER_OP_FLAT_FIELD;
        flat.i0 = 31;  // 背景估计半径
        pipeline.add(RASTER_OP_FLAT_FIELD, flat);

        // 灰度
        RasterOpParams gray{};
        gray.structSize = sizeof(RasterOpParams);
        gray.opId = RASTER_OP_TO_GRAY;
        pipeline.add(RASTER_OP_TO_GRAY, gray);

        // 去噪
        RasterOpParams denoise{};
        denoise.structSize = sizeof(RasterOpParams);
        denoise.opId = RASTER_OP_DENOISE;
        denoise.i0 = RASTER_DENOISE_FAST_NLMEANS;
        denoise.f0 = 10.0;
        pipeline.add(RASTER_OP_DENOISE, denoise);

        // 二值化
        RasterOpParams threshold{};
        threshold.structSize = sizeof(RasterOpParams);
        threshold.opId = RASTER_OP_THRESHOLD;
        threshold.i0 = RASTER_THRESH_OTSU;
        threshold.f0 = 0.0;
        threshold.f1 = 255.0;
        pipeline.add(RASTER_OP_THRESHOLD, threshold);

        // 去孤点：把噪点从工艺图里抹掉。
        RasterOpParams despeckle{};
        despeckle.structSize = sizeof(RasterOpParams);
        despeckle.opId = RASTER_OP_DESPECKLE;
        despeckle.i0 = 0;
        despeckle.i1 = 4;
        pipeline.add(RASTER_OP_DESPECKLE, despeckle);

        std::printf("pipeline has %d operators\n", pipeline.count());

        raster::Image source = raster::Image::load(input);
        raster::Image result = pipeline.execute(source);
        result.save(output);

        const RasterStats stats = raster::stats(result);
        std::printf("ink ratio = %.4f -> %s\n", stats.inkRatio, output.c_str());
    } catch (const std::exception& ex) {
        std::fprintf(stderr, "error: %s\n", ex.what());
        return 1;
    }
    return 0;
}
