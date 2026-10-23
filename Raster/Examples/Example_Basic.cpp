/**
 * @file Example_Basic.cpp
 * @brief 基本用法：载入 → 灰度 → 校正 → 二值化 → 保存。
 *
 * 本示例演示 C++ 门面（头文件内联，无 STL 跨 DLL 边界）。
 */

#include <Raster/RasterCpp.h>

#include <cstdio>
#include <string>

int main(int argc, char** argv)
{
    const std::string input = (argc > 1) ? argv[1] : "input.png";
    const std::string output = (argc > 2) ? argv[2] : "output_binary.png";

    try {
        raster::Image source = raster::Image::load(input);
        std::printf("loaded %s : %dx%d ch=%d\n", input.c_str(), source.width(), source.height(),
                    source.channels());

        // 1) 统一到灰度：激光工艺只关心亮度，不关心颜色。
        raster::Image gray = source.toGray();

        // 2) 去噪：扫描件上的噪点会在雕刻时变成多余的烧穿点。
        RasterImage denoised = nullptr;
        raster::check(Raster_Denoise(gray.get(), RASTER_DENOISE_FAST_NLMEANS, 10.0, &denoised),
                      "Raster_Denoise");
        raster::Image clean(denoised);

        // 3) Otsu 自动阈值：工件与背景对比度不固定时比固定阈值稳。
        raster::Image binary = clean.threshold(RASTER_THRESH_OTSU, 0.0, 255.0);

        // 4) 统计墨量：判断是不是一张空料 / 曝光过度的图。
        const RasterStats stats = raster::stats(binary);
        std::printf("ink ratio = %.4f, mean = %.2f\n", stats.inkRatio, stats.mean);

        binary.save(output);
        std::printf("saved %s\n", output.c_str());
    } catch (const std::exception& ex) {
        std::fprintf(stderr, "error: %s\n", ex.what());
        return 1;
    }
    return 0;
}
