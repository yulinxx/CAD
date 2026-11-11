/**
 * @file RasterScanlineBridge.h
 * @brief 位图实体 → 扫描线段（毫米坐标）的唯一桥接点。
 *
 * 真机链路是 `MotionPlanCompiler → Hw::MotionPlan`，它不经过 CamCore / Cam::compile。
 * 本桥接层只负责三件事：取像素、按真实像素当量换算单位、调用 Raster 扫描线库；
 * 不参与判定"位图能否加工"，该结论由 `Eg::Cam::GeometryExtractor` 的 GeometryKind 统一给出。
 */

#ifndef RASTER_SCANLINE_BRIDGE_H
#define RASTER_SCANLINE_BRIDGE_H

#include "Engine2D/AprEntity/AprImage.h"

#include <cstdint>
#include <string>
#include <vector>

namespace CamBridge
{
    /** @brief 一条扫描段（世界坐标，毫米） */
    struct RasterRun
    {
        Ut::Vec2d start;
        Ut::Vec2d end;
        double powerPercent = 0.0;   ///< 0..100；laserOn=false 时为 0
        double speedMmPerSec = 0.0;
        int32_t pass = 0;
        int32_t line = 0;            ///< 扫描带序号；换带时必须空移，不能带光跨行
        int32_t dir = 1;             ///< 1 = 正向，-1 = 反向（双向扫描）
        bool laserOn = false;
        bool overscan = false;       ///< true = 图像边界外的过扫段（关光行走，但计入边界）
    };

    /** @brief 位图扫描参数（全部为物理单位，桥接层内部才换算成像素） */
    struct RasterScanlineRequest
    {
        double lineSpacingMm = 0.1;   ///< 行距，允许小数
        double overscanMm = 0.0;      ///< 单侧过扫长度
        double angleDeg = 0.0;        ///< 扫描角度
        bool bidirectional = true;    ///< 双向扫描
        int32_t passes = 1;           ///< 遍数（整图分遍由扫描线库保证）
        double powerPercent = 100.0;  ///< 最暗像素对应的功率百分比
        double speedMmPerSec = 100.0;
    };

    /**
     * @brief 把位图实体装配成毫米坐标的扫描段
     *
     * @param image     位图实体（像素 + 四个角点映射）
     * @param request   物理单位参数
     * @param out       输出扫描段，按整图分遍顺序排列
     * @param defect    失败原因（英文，可直接进日志）；成功时清空
     * @return 成功至少产出一条扫描段
     */
    bool buildRasterRuns(const Eg::AprImage& image,
                         const RasterScanlineRequest& request,
                         std::vector<RasterRun>& out,
                         std::string& defect);
}  // namespace CamBridge

#endif  // RASTER_SCANLINE_BRIDGE_H
