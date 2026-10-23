/**
 * @file benchmark.cpp
 * @brief Raster 算子吞吐基准。
 *
 * 目的不是跑分，而是回答三个具体的工程问题：
 *   1. 一个典型工件（4000x3000 灰度）在各算子上的耗时量级；
 *   2. 哪些算子值得做 SIMD / 多线程优化（耗时占比高的那几个）；
 *   3. 一次完整工艺流程（平场 -> 二值化 -> 去孤点 -> 轮廓 -> 割缝补偿）
 *      能不能落在"工人能接受"的等待时间内。
 *
 * 用法：
 *   raster_benchmark [width height] [iterations]
 *
 * 输出是纯文本，便于 CI 抓取与对比历史。
 */

#include <Raster/RasterApi.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace
{
    using Clock = std::chrono::steady_clock;

    double msSince(Clock::time_point start)
    {
        const auto end = Clock::now();
        return std::chrono::duration<double, std::milli>(end - start).count();
    }

    /** @brief 造一张有结构的灰度图：渐变 + 若干方块 + 噪声。 */
    std::vector<uint8_t> makeWorkpiece(int width, int height)
    {
        std::vector<uint8_t> pixels(static_cast<std::size_t>(width) * height);
        unsigned int seed = 12345u;
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                // 确定性伪随机（不用 rand()，保证跨平台结果可比）。
                seed = seed * 1103515245u + 12345u;
                const int noise = static_cast<int>((seed >> 16) & 0x1F) - 16;
                int value = 60 + (x * 120) / (width > 1 ? width - 1 : 1);
                if (((x / 97) + (y / 89)) % 2 == 0) {
                    value = 200;
                }
                value += noise;
                if (value < 0) {
                    value = 0;
                } else if (value > 255) {
                    value = 255;
                }
                pixels[static_cast<std::size_t>(y) * width + x] = static_cast<uint8_t>(value);
            }
        }
        return pixels;
    }

    struct Row
    {
        const char* name;
        double ms;
    };

    void report(const char* title, const std::vector<Row>& rows, double megapixels)
    {
        std::printf("\n== %s ==\n", title);
        std::printf("%-34s %12s %14s\n", "operation", "ms", "MPix/s");
        std::printf("%-34s %12s %14s\n", "----------------------------------", "------------",
                    "--------------");
        for (const Row& row : rows) {
            const double throughput = (row.ms > 0.0) ? (megapixels / (row.ms / 1000.0)) : 0.0;
            std::printf("%-34s %12.2f %14.1f\n", row.name, row.ms, throughput);
        }
    }
}  // namespace

int main(int argc, char** argv)
{
    // 默认 2000x1500（3 MPix）：够看出算子之间的量级差，又不至于让人等。
    // 注意 NLM 去噪是数量级最慢的算子（12 MPix 要几十秒），
    // 想看真实工件的量级就把尺寸调大、迭代数调成 1。
    int width = 2000;
    int height = 1500;
    int iterations = 1;
    if (argc > 2) {
        width = std::atoi(argv[1]);
        height = std::atoi(argv[2]);
    }
    if (argc > 3) {
        iterations = std::atoi(argv[3]);
    }
    if (width <= 0 || height <= 0 || iterations <= 0) {
        std::printf("usage: %s [width height] [iterations]\n", argv[0]);
        return 1;
    }

    const double megapixels = (static_cast<double>(width) * height) / 1.0e6;

    std::printf("Raster benchmark: %dx%d (%0.1f MPix), %d iteration(s)\n", width, height,
                megapixels, iterations);
    std::printf("library: %s (ABI %u, OpenCV %u)\n", Raster_GetVersionString(),
                Raster_GetAbiVersion(), Raster_GetOpenCvVersion());

    // --- 准备输入 ---
    RasterImage source = nullptr;
    if (Raster_CreateImage(width, height, RASTER_FORMAT_GRAY8, &source) != RASTER_OK) {
        std::printf("failed to create the source image\n");
        return 2;
    }
    {
        RasterImageView view{};
        view.structSize = sizeof(RasterImageView);
        Raster_GetImageView(source, &view);
        const std::vector<uint8_t> pixels = makeWorkpiece(width, height);
        std::memcpy(const_cast<void*>(view.data), pixels.data(), pixels.size());
    }

    std::vector<Row> rows;
    RasterImage gray = nullptr;
    RasterImage binary = nullptr;

    // --- 单算子 ---
    {
        auto t0 = Clock::now();
        for (int i = 0; i < iterations; ++i) {
            RasterImage out = nullptr;
            Raster_ToGray(source, &out);
            Raster_DestroyImage(out);
        }
        rows.push_back({"ToGray", msSince(t0) / iterations});
    }
    {
        Raster_ToGray(source, &gray);
        auto t0 = Clock::now();
        for (int i = 0; i < iterations; ++i) {
            RasterImage out = nullptr;
            Raster_Threshold(gray, RASTER_THRESH_OTSU, 0.0, 255.0, &out);
            Raster_DestroyImage(out);
        }
        rows.push_back({"Threshold (Otsu)", msSince(t0) / iterations});
    }
    {
        Raster_Threshold(gray, RASTER_THRESH_OTSU, 0.0, 255.0, &binary);
        auto t0 = Clock::now();
        for (int i = 0; i < iterations; ++i) {
            RasterImage out = nullptr;
            Raster_Denoise(gray, RASTER_DENOISE_FAST_NLMEANS, 10.0, &out);
            Raster_DestroyImage(out);
        }
        rows.push_back({"Denoise (NLM)", msSince(t0) / iterations});
    }
    {
        auto t0 = Clock::now();
        for (int i = 0; i < iterations; ++i) {
            RasterImage out = nullptr;
            Raster_FlatFieldCorrect(gray, 31, &out);
            Raster_DestroyImage(out);
        }
        rows.push_back({"FlatFieldCorrect", msSince(t0) / iterations});
    }
    {
        auto t0 = Clock::now();
        for (int i = 0; i < iterations; ++i) {
            RasterImage out = nullptr;
            Raster_Dither(gray, RASTER_DITHER_FLOYD_STEINBERG, &out);
            Raster_DestroyImage(out);
        }
        rows.push_back({"Dither (Floyd-Steinberg)", msSince(t0) / iterations});
    }
    {
        auto t0 = Clock::now();
        for (int i = 0; i < iterations; ++i) {
            RasterImage out = nullptr;
            Raster_Halftone(gray, 6, 45.0, RASTER_HALFTONE_DOT, &out);
            Raster_DestroyImage(out);
        }
        rows.push_back({"Halftone (dot screen)", msSince(t0) / iterations});
    }
    {
        auto t0 = Clock::now();
        for (int i = 0; i < iterations; ++i) {
            RasterImage out = nullptr;
            Raster_Despeckle(binary, 0, 4, &out);
            Raster_DestroyImage(out);
        }
        rows.push_back({"Despeckle", msSince(t0) / iterations});
    }
    {
        auto t0 = Clock::now();
        for (int i = 0; i < iterations; ++i) {
            RasterImage out = nullptr;
            Raster_Canny(gray, 50.0, 150.0, &out);
            Raster_DestroyImage(out);
        }
        rows.push_back({"Canny", msSince(t0) / iterations});
    }
    {
        auto t0 = Clock::now();
        for (int i = 0; i < iterations; ++i) {
            RasterImage out = nullptr;
            Raster_HeightMapFromGray(gray, 0.0, 1.0, &out);
            Raster_DestroyImage(out);
        }
        rows.push_back({"HeightMapFromGray", msSince(t0) / iterations});
    }
    {
        auto t0 = Clock::now();
        for (int i = 0; i < iterations; ++i) {
            RasterImage out = nullptr;
            Raster_Resize(gray, 0.5, 0.5, RASTER_INTERP_AREA, &out);
            Raster_DestroyImage(out);
        }
        rows.push_back({"Resize 50% (area)", msSince(t0) / iterations});
    }
    {
        auto t0 = Clock::now();
        for (int i = 0; i < iterations; ++i) {
            RasterContourSet set = nullptr;
            Raster_TraceContours(binary, RASTER_TRACE_ALL, 10.0, 0.5, &set);
            Raster_DestroyContourSet(set);
        }
        rows.push_back({"TraceContours", msSince(t0) / iterations});
    }
    {
        RasterContourSet set = nullptr;
        Raster_TraceContours(binary, RASTER_TRACE_ALL, 10.0, 0.5, &set);
        auto t0 = Clock::now();
        for (int i = 0; i < iterations; ++i) {
            RasterContourSet out = nullptr;
            Raster_KerfCompensate(set, 0.15, RASTER_KERF_OUTSIDE, &out);
            Raster_DestroyContourSet(out);
        }
        rows.push_back({"KerfCompensate", msSince(t0) / iterations});
        Raster_DestroyContourSet(set);
    }

    report("single operators", rows, megapixels);

    // --- 完整工艺流程 ---
    {
        RasterPipeline pipeline = nullptr;
        Raster_CreatePipeline(&pipeline);

        RasterOpParams flat{};
        flat.structSize = sizeof(RasterOpParams);
        flat.opId = RASTER_OP_FLAT_FIELD;
        flat.i0 = 31;
        Raster_PipelineAdd(pipeline, RASTER_OP_FLAT_FIELD, &flat, sizeof(flat));

        RasterOpParams toGray{};
        toGray.structSize = sizeof(RasterOpParams);
        toGray.opId = RASTER_OP_TO_GRAY;
        Raster_PipelineAdd(pipeline, RASTER_OP_TO_GRAY, &toGray, sizeof(toGray));

        RasterOpParams threshold{};
        threshold.structSize = sizeof(RasterOpParams);
        threshold.opId = RASTER_OP_THRESHOLD;
        threshold.i0 = RASTER_THRESH_OTSU;
        threshold.f1 = 255.0;
        Raster_PipelineAdd(pipeline, RASTER_OP_THRESHOLD, &threshold, sizeof(threshold));

        RasterOpParams despeckle{};
        despeckle.structSize = sizeof(RasterOpParams);
        despeckle.opId = RASTER_OP_DESPECKLE;
        despeckle.i1 = 4;
        Raster_PipelineAdd(pipeline, RASTER_OP_DESPECKLE, &despeckle, sizeof(despeckle));

        auto t0 = Clock::now();
        for (int i = 0; i < iterations; ++i) {
            RasterImage out = nullptr;
            Raster_PipelineExecute(pipeline, source, &out);
            Raster_DestroyImage(out);
        }
        const double pipelineMs = msSince(t0) / iterations;
        Raster_DestroyPipeline(pipeline);

        std::printf("\n== full process pipeline ==\n");
        std::printf("flat-field -> gray -> Otsu -> despeckle : %8.2f ms  (%0.1f MPix/s)\n",
                    pipelineMs, megapixels / (pipelineMs / 1000.0));
    }

    Raster_DestroyImage(binary);
    Raster_DestroyImage(gray);
    Raster_DestroyImage(source);
    return 0;
}
