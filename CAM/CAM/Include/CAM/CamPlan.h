#ifndef CAM_PLAN_H
#define CAM_PLAN_H

/**
 * @file CamPlan.h
 * @brief CAM 层工具级输出契约 —— 有序工序 + 路径 + 统计。
 *
 * 这是 ToolpathJobInput 经过 CAM 编译后的完整加工描述：
 *   - 按图层顺序分组（来自输入的 LayerBucket 顺序）
 *   - 每层内的轮廓已排序（M0 先做最基本的按 sourceEntityId 顺序，
 *     M1 引入最近邻 + 2-opt 空移优化）
 *   - 每条轮廓附带 pierce 点、长度统计
 *   - 空移长度 = 轮廓起点与上一条轮廓终点之间的距离（累积）
 *
 * M0 不做：割缝补偿、引线、穿孔、功率曲线、拐角处理、共边切割 ——
 * 这些变换在 M1 才会修改 segments。M0 的 segments 就是输入过来的原始段。
 *
 * ================ 坐标系约定 ================
 *   CamPlan 内所有坐标（段端点、圆心、穿孔点、包围盒）都是**用户坐标**
 *   （左下角原点，X 右 / Y 上，单位 mm）。
 *   CAM 全程只处理用户坐标，不做任何"用户坐标 → 机器坐标"的变换；
 *   该变换由 Main 侧发射器在 M2 一步完成（CamOp → Hw::MotionCommand）。
 *
 * ================ 与 MotionPlan IR 的关系 ================
 *   CamPlan ：工艺几何。与设备无关，可预览、可统计、可缓存。
 *   Hw::MotionPlan：设备指令。由 MotionPlanEmitter（M2 落地）翻译 CamPlan 得到。
 *   CamPlan 不 include Hardware/Device/MotionPlan.h。
 *
 * ================ ABI 约定 ================
 *   本文件属于 CAM 的 C++ 内部 ABI（见 CAMExport.h），结构体含 STL 容器，
 *   使用方必须与 CAM 同工具链构建；结构体不保留 structSize 版本守卫字段。
 */

#include "CAM/CAMExport.h"

#include "Ut/Vec.h"

#include <cstdint>
#include <vector>

namespace Cam
{
    // ---- CamPlan 专用的段类型 ----
    // 与 ToolpathJobInput::PathSegment 字段基本相同，但 M1/M2 会加更多字段
    // （比如 isRapid 标记、laserOn 标记、功率覆盖），所以分开定义

    struct CAM_API PlanPathSegment
    {
        enum class Type : int32_t
        {
            Line = 0,
            Arc = 1,
        };

        Type type = Type::Line;

        Ut::Vec2d ptStart;
        Ut::Vec2d ptEnd;
        Ut::Vec2d ptCenter;
        double dRadius = 0.0;
        /// 角度单位：度（degree），与 ToolpathJobInput 的契约一致
        double dStartAngle = 0.0;
        double dEndAngle = 0.0;
        bool counterClockwise = true;
        bool closed = false;

        /// true 表示这段是 rapid（空移，laser off）；false 表示 cutting（laser on）
        /// M0：所有 contours 的首段前会插入一条 Rapid 段（start → contour.start）
        bool isRapid = false;

        // ---- 逐段工艺覆盖（位图光栅策略使用）----
        /// 功率百分比 0..100；<0 表示继承图层参数
        double powerPercent = -1.0;
        /// 速度（mm/s）；<=0 表示继承图层参数
        double speedMmPerSec = -1.0;
        /// 遍次序号；-1 表示继承 PlanLayerResult::passIndex
        int32_t passIndex = -1;
    };

    /// CamPlan 里的轮廓 —— 已排序，可能已过工艺变换
    struct CAM_API PlanContour
    {
        std::vector<PlanPathSegment> segments;

        bool closed = false;
        bool hole = false;

        uint64_t sourceEntityId = 0;
        int32_t sourceLayerId = 0;

        /// 穿孔点（M0 为空，M1 PiercePlanner 填写）
        std::vector<Ut::Vec2d> piercePoints;

        /// 轮廓总长度（mm，含 rapid）
        double lengthMm = 0.0;

        /// 切削段累计长度（laser on，mm）
        double cuttingLengthMm = 0.0;

        /// 空移段累计长度（laser off，mm）
        double rapidLengthMm = 0.0;
    };

    /// 一个图层 × 一个遍次 的加工结果（M0 已按 passes 展开，passIndex 从 0 递增）
    struct CAM_API PlanLayerResult
    {
        int32_t layerId = 0;

        // ---- 副本，供下游 MotionPlanEmitter 用 ----
        double powerPercent = 50.0;
        double frequencyHz = 20000.0;
        double pulseWidthUs = 10.0;
        double speedMmPerSec = 200.0;
        double rapidSpeedMmPerSec = 300.0;
        int32_t passIndex = 0;

        std::vector<PlanContour> contours;

        double layerCuttingLengthMm = 0.0;
        double layerRapidLengthMm = 0.0;
    };

    /// CAM 编译完整结果 —— 工具级输出
    struct CAM_API CamPlan
    {
        std::vector<PlanLayerResult> layers;

        /// 预估耗时（秒），M0 留 0.0，M1 TimeEstimator 填写
        double estimatedSeconds = 0.0;

        /// 累计切削长度（mm）
        double totalCuttingLengthMm = 0.0;

        /// 累计空移长度（mm）
        double totalRapidLengthMm = 0.0;

        /// 所有轮廓的包围盒（用户空间坐标，mm）
        double boundsMinX = 0.0;
        double boundsMinY = 0.0;
        double boundsMaxX = 0.0;
        double boundsMaxY = 0.0;

        /// 清空所有内容
        void clear()
        {
            layers.clear();
            estimatedSeconds = 0.0;
            totalCuttingLengthMm = 0.0;
            totalRapidLengthMm = 0.0;
            boundsMinX = boundsMinY = boundsMaxX = boundsMaxY = 0.0;
        }
    };

}  // namespace Cam

#endif  // CAM_PLAN_H
