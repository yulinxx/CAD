/**
 * @file Example_LaserCut.cpp
 * @brief 激光切割路径提取：二值化 → 轮廓 → 割缝补偿 → 走刀顺序 → 导出路径点。
 *
 * 这是本模块最贴近生产的流程：算出来的轮廓点可以直接喂给刀路模块。
 */

#include <Raster/RasterCpp.h>

#include <cstdio>
#include <utility>
#include <string>
#include <vector>

int main(int argc, char** argv)
{
    const std::string input = (argc > 1) ? argv[1] : "input.png";
    const double kerfWidth = (argc > 2) ? std::atof(argv[2]) : 0.15;  // 割缝宽度，单位：像素

    try {
        raster::Image source = raster::Image::load(input);
        raster::Image gray = source.toGray();
        raster::Image binary = gray.threshold(RASTER_THRESH_OTSU, 0.0, 255.0);

        // 1) 提取全部轮廓（外轮廓 + 孔洞）。
        raster::ContourSet contours =
            raster::ContourSet::trace(binary, RASTER_TRACE_ALL, 10.0, 0.5);

        std::printf("traced %d contours\n", contours.count());

        // 2) 割缝补偿：激光会把一条 kerf 宽的材料烧掉，
        //    不补偿的话成品尺寸会小约 kerf/2。
        raster::ContourSet compensated =
            contours.kerfCompensate(kerfWidth, RASTER_KERF_OUTSIDE);

        // 3) 走刀顺序：先切内孔、再切外框，避免切完外框零件先掉下去。
        raster::ContourSet ordered = compensated.ordered(RASTER_ORDER_INNER_FIRST);

        for (int32_t i = 0; i < ordered.count(); ++i) {
            const RasterContourInfo info = ordered.info(i);
            const std::vector<double> points = ordered.points(i);
            std::printf("  #%d %s area=%.1f perimeter=%.1f points=%d\n", i,
                        info.isHole ? "hole  " : "outer ", info.area, info.perimeter,
                        static_cast<int>(points.size() / 2));
        }

        // 4) 导出轮廓图，供工艺人员核对路径。
        // Image 是只移动的 RAII 类型：要"拿走"一个句柄必须显式 move。
        raster::Image preview = std::move(binary);
        preview.save("cut_contours.png");
    } catch (const std::exception& ex) {
        std::fprintf(stderr, "error: %s\n", ex.what());
        return 1;
    }
    return 0;
}
