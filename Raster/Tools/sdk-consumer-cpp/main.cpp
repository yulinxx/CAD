/*
 * Raster SDK smoke test: C++ consumer through the installed CMake package.
 *
 * Guards what the manual include/lib wiring cannot: RasterConfig.cmake,
 * RasterTargets-<config>.cmake, the INSTALL_INTERFACE include directory and the
 * Debug/Release import library selection done by CMake.
 *
 * It also exercises the C++ facade (RasterCpp.h) -- which is header-only and
 * must never contribute an exported symbol to the DLL.
 *
 * Prints "consumer-cpp ok ..." on success and returns 0.
 */

#include <Raster/RasterCpp.h>

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

int main()
{
    if (raster::version() != RASTER_VERSION) {
        std::printf("version mismatch\n");
        return 1;
    }
    if (raster::abiVersion() != RASTER_ABI_VERSION) {
        std::printf("abi mismatch\n");
        return 2;
    }

    try {
        // 1) 造一张图：灰底上一个白方块。
        raster::Image source = raster::Image::create(128, 96, RASTER_FORMAT_GRAY8);
        std::vector<uint8_t> pixels(128 * 96, 20);
        for (int y = 30; y < 66; ++y) {
            for (int x = 40; x < 88; ++x) {
                pixels[static_cast<size_t>(y) * 128 + static_cast<size_t>(x)] = 230;
            }
        }
        raster::Image square = raster::Image::fromBytes(128, 96, RASTER_FORMAT_GRAY8,
                                                        pixels.data(), pixels.size());

        // 2) 二值化 + 轮廓提取：这是最贴近生产的路径。
        raster::Image binary = square.toGray().threshold(RASTER_THRESH_OTSU, 0.0, 255.0);
        raster::ContourSet contours =
            raster::ContourSet::trace(binary, RASTER_TRACE_ALL, 4.0, 0.5);
        if (contours.count() != 1) {
            std::printf("expected 1 contour, got %d\n", contours.count());
            return 3;
        }

        const RasterContourInfo info = contours.info(0);
        if (info.pointCount < 3) {
            std::printf("contour has too few points: %d\n", (int)info.pointCount);
            return 4;
        }

        // 3) 割缝补偿：外扩后面积必须变大。
        // ContourSet 是只移动的 RAII 类型，临时结果必须直接 move，
        // 不能写成 `const ContourSet x = ...`（拷贝构造已被删除）。
        raster::ContourSet compensated =
            contours.kerfCompensate(4.0, RASTER_KERF_OUTSIDE);
        if (compensated.count() < 1) {
            std::printf("kerf compensation produced no contour\n");
            return 5;
        }
        double beforeArea = 0.0;
        double afterArea = 0.0;
        for (int32_t i = 0; i < contours.count(); ++i) {
            beforeArea = std::max(beforeArea, contours.info(i).area);
        }
        for (int32_t i = 0; i < compensated.count(); ++i) {
            afterArea = std::max(afterArea, compensated.info(i).area);
        }
        if (afterArea <= beforeArea) {
            std::printf("kerf compensation did not grow the contour: %f -> %f\n", beforeArea,
                        afterArea);
            return 6;
        }

        // 4) 管线：工艺流程必须能复现。
        raster::Pipeline pipeline = raster::Pipeline::create();
        RasterOpParams toGray = raster::makeOpParams(RASTER_OP_TO_GRAY);
        pipeline.add(RASTER_OP_TO_GRAY, toGray);
        RasterOpParams threshold = raster::makeOpParams(RASTER_OP_THRESHOLD);
        threshold.i0 = RASTER_THRESH_OTSU;
        threshold.f0 = 0.0;
        threshold.f1 = 255.0;
        pipeline.add(RASTER_OP_THRESHOLD, threshold);
        raster::Image pipelined = pipeline.execute(square);
        if (pipelined.width() != 128 || pipelined.height() != 96) {
            std::printf("pipeline produced a wrong size\n");
            return 7;
        }

        const RasterStats stats = raster::stats(binary);
        std::printf("consumer-cpp ok: version=%s contours=%d area=%.1f->%.1f ink=%.3f\n",
                    raster::versionString().c_str(), (int)contours.count(), beforeArea, afterArea,
                    stats.inkRatio);
    } catch (const std::exception& ex) {
        std::printf("exception: %s\n", ex.what());
        return 8;
    }
    return 0;
}
