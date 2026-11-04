#ifndef CAM_TOOLPATH_JOB_INPUT_H
#define CAM_TOOLPATH_JOB_INPUT_H

/**
 * @file ToolpathJobInput.h
 * @brief CAM 层的唯一输入契约 —— 中性 C++ 值类型，自持有数据。
 *
 * 这是 Engine2D/UI/Hardware 类型与 CAM 内部类型之间的隔离墙：
 *   - 不 include Engine2D/Engine2D/Core/SceneManager.h
 *   - 不 include Engine2D/Interaction/LayerManager.h
 *   - 不 include SyEntity.h
 *   - 不 include Hardware/Device/*.h
 *   - 只包含 Ut/Vec.h 和标准库头
 *
 * 转换工作由 Main/Src/CAM/CamInputBridge（唯一桥接点）完成 ——
 * 那是整个仓库里唯一允许同时接触 Engine2D 类型和 CAM 类型的文件。
 *
 * ================ 单位与坐标系约定 ================
 *   位置 ：毫米（mm），XY 平面
 *   角度 ：度（degree），0~360
 *   速度 ：mm/s
 *   功率 ：百分比（0.0 ~ 100.0）
 *   时间 ：微秒（us）
 *   坐标系：用户空间，左下角原点，X 右 / Y 上
 *   机器坐标系转换：CAM 全程只处理用户坐标，不做任何变换；
 *                   "用户坐标 → 机器坐标"由 Main 侧发射器在 M2 一步完成
 *
 * ================ ABI 与演进约定 ================
 *   本文件属于 CAM 的 C++ 内部 ABI（见 CAMExport.h）：结构体含 STL 容器，
 *   使用方必须与 CAM 同工具链构建。
 *   本文件内不再保留 structSize 版本守卫字段 —— 对含 STL 容器的结构体，
 *   "赋 sizeof 再和自己比"没有任何校验价值。版本一致性通过
 *   CAM_ABI_VERSION 与 camAbiInfo() 启动自检保证。
 *   字段只能在尾部追加，不得调整已有字段顺序。
 */

#include "CAM/CAMExport.h"

#include "Ut/Vec.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace Cam
{
    // ============================================================
    //  基础几何值类型
    // ============================================================

    /// 一条段 —— Line 或 Arc，中性 C++ 值类型
    struct CAM_API PathSegment
    {
        enum class Type : int32_t
        {
            Line = 0,
            Arc = 1,
        };

        Type type = Type::Line;

        Ut::Vec2d ptStart;  ///< 行进起点（世界坐标，mm）
        Ut::Vec2d ptEnd;    ///< 行进终点（世界坐标，mm）

        // ---- 仅 Arc ----
        Ut::Vec2d ptCenter;         ///< 圆心
        double dRadius = 0.0;       ///< 半径（mm）
        // 角度单位：度（degree），与本文件顶部的单位约定一致。
        // Engine2D 几何层使用弧度，由桥接层在唯一类型转换点统一换算，CAM 内部不再转换。
        double dStartAngle = 0.0;   ///< 起点极角（几何量，与行进方向无关，度）
        double dEndAngle = 0.0;     ///< 终点极角（几何量，度）

        /// 行进方向：true=逆时针（角度递增），false=顺时针（角度递减）。
        /// 对 Line 段无意义。CAM 内部处理圆弧时以此为准，
        /// 不再靠几何约定推断——走错方向会烧掉整块料。
        bool counterClockwise = true;

        /// 是否闭合（首尾重合）
        bool closed = false;
    };

    /// 一条轮廓 —— 若干段首尾相接组成的有向运动
    struct CAM_API Contour
    {
        std::vector<PathSegment> segments;

        /// 是否闭合（首点 == 末点）。由来源图元决定，CAM 不得擅自改
        bool closed = false;

        /// 是否为孔洞（绕向与所属外环相反）。由桥接层在构造时填写；
        /// CAM 内部包含关系解析阶段也可能再次修正
        bool hole = false;

        /// 来源图元 ID —— 追溯用：预览高亮、报错定位
        uint64_t sourceEntityId = 0;

        /// 来源图层 ID（0 = 未归层）
        int32_t sourceLayerId = 0;

        /// 全部段的折线长度（mm）。CAM 填充阶段汇总
        double length = 0.0;
    };

    /// 一条填充区域（边界环 + 扫描参数）。M0 不实现填充，
    /// 但骨架先留，避免 M1 再改契约
    struct CAM_API FillRegion
    {
        std::vector<Contour> boundaries;  ///< 外环（含孔洞子环）
        double scanAngleDeg = 0.0;         ///< 扫描方向（度）
        double lineSpacing = 0.1;          ///< 扫描线间距（mm）
    };

    /// 一条扫描线加工段（位图光栅策略的输入单元，坐标已由桥接层换算为 mm）
    struct CAM_API ScanlineSegment
    {
        Ut::Vec2d ptStart;  ///< 行进起点（mm）
        Ut::Vec2d ptEnd;    ///< 行进终点（mm）

        /// 该段功率百分比 0..100；laserOn=false 时为 0
        double powerPercent = 0.0;

        /// 该段速度（mm/s）；<=0 表示继承图层参数
        double speedMmPerSec = 0.0;

        /// 遍次序号（从 0 开始）；-1 表示由图层 passes 展开
        int32_t passIndex = 0;

        /// 走刀方向：+1 = ptStart→ptEnd，-1 = 反向
        int32_t dir = 1;

        /// true = 出光加工，false = 停光空移
        bool laserOn = true;
    };

    /// 一个图元快照 —— 桥接层从 SyEntity 转换而来
    struct CAM_API EntitySnapshot
    {
        enum class GeometryKind : int32_t
        {
            Contour = 0,   ///< 沿轮廓加工（线、圆弧、样条…）
            Point = 1,     ///< 单点（原地打点）
            Fillable = 2,  ///< 区域，需要填充/扫描阶段
            Raster = 3,    ///< 位图，桥接层已算好扫描线（scanlines）
            Unsupported = 4,  ///< 提取器明确判定不可加工（原因见 unmachinableReason）
        };

        GeometryKind kind = GeometryKind::Contour;

        uint64_t entityId = 0;
        int32_t layerId = 0;

        // kind == Contour
        std::vector<Contour> contours;

        // kind == Point
        Ut::Vec2d point;

        // kind == Fillable
        FillRegion fillRegion;

        // kind == Raster：桥接层调用 Raster 库得到的扫描线刀路
        std::vector<ScanlineSegment> scanlines;

        // ---- 图元级跳过标记（由桥接层从 SyEntity 填写）----
        // 与图层级 visible/locked 并列：单个被隐藏/锁定的图元也必须能跳过
        bool visible = true;
        bool locked = false;

        /// 无法加工的原因（由桥接层透传 GeometryExtractor::ExtractResult::reason）。
        /// 静态字符串内容，转存为 std::string 以自持有；仅 Unsupported/Raster/Fillable 有意义
        std::string unmachinableReason;
    };

    // ============================================================
    //  工艺参数值类型 —— 完全中性，与 Engine/Process/ProcessParams.h 无关
    // ============================================================

    /// 基础加工工艺参数（每层默认 + 可覆盖）
    struct CAM_API ProcessParams
    {
        // -- 激光 --
        double powerPercent = 50.0;      ///< 0..100
        double frequencyHz = 20000.0;
        double pulseWidthUs = 10.0;
        int32_t waveformIndex = 0;

        // -- 运动 --
        double speedMmPerSec = 200.0;
        /// 空移速度。CAM 契约要求具体数值（> 0）；
        /// Engine 侧 0 = 使用工程默认值的约定由桥接层归一化后再写入
        double rapidSpeedMmPerSec = 300.0;

        // -- 遍次 --
        int32_t passes = 1;

        // -- 曲线离散 --
        double chordToleranceMm = 0.01;

        // ---- 开关 ----
        /// false = 工艺停用（保留参数，不产出刀路），与 Eg::ProcessParams 语义一致
        bool enabled = true;

        /// 参数合法性校验 —— 编译前调用，非法返回 false
        bool valid() const
        {
            return powerPercent >= 0.0 && powerPercent <= 100.0
                   && speedMmPerSec > 0.0
                   && rapidSpeedMmPerSec > 0.0
                   && passes >= 1
                   && chordToleranceMm > 0.0 && chordToleranceMm <= 1.0;
        }
    };

    // ---- M0 阶段以下参数结构体暂时不参与编译流程，先预留契约 ----

    struct CAM_API KerfParams
    {
        double widthMm = 0.0;
        bool enabled = false;
        int32_t side = 0;  ///< 0=Outside, 1=Inside, 2=Center
    };

    struct CAM_API LeadInOutParams
    {
        bool enabled = false;
        double leadInLengthMm = 0.0;
        double leadOutLengthMm = 0.0;
        double rampAngleDeg = 0.0;
        int32_t leadInMode = 0;  ///< 0=tangent, 1=normal, 2=straight
    };

    struct CAM_API PierceParams
    {
        bool enabled = false;
        uint32_t pierceDelayUs = 500;
        double piercePowerPercent = 30.0;
        double pierceSpeedMmPerSec = 50.0;
        int32_t pierceCount = 1;
        uint32_t dwellUs = 200;
    };

    struct CAM_API CornerParams
    {
        int32_t strategy = 0;            ///< 0=sharp, 1=round, 2=slowdown
        double radiusMm = 0.0;           ///< strategy=1
        double overcutMm = 0.0;          ///< strategy=2
        double slowdownFactor = 0.5;     ///< strategy=2 multiplier
    };

    struct CAM_API PowerCurvePoint
    {
        double speedMmPerSec = 0.0;
        double powerPercent = 50.0;
    };

    struct CAM_API PowerCurve
    {
        bool enabled = false;
        std::vector<PowerCurvePoint> points;
    };

    struct CAM_API PassStrategy
    {
        bool adaptiveThickness = false;
        int32_t totalDepthLayers = 1;
        double totalDepthMm = 1.0;
    };

    // ============================================================
    //  作业输入总结构
    // ============================================================

    struct CAM_API ToolpathJobInput
    {
        char jobId[128] = {};

        /// 一个图层的完整加工快照
        struct CAM_API LayerBucket
        {
            int32_t layerId = 0;
            bool visible = true;
            bool locked = false;
            bool outputEnabled = true;

            ProcessParams params;

            // ---- M1 才启用，M0 只透传不使用 ----
            KerfParams kerf;
            LeadInOutParams leadInOut;
            PierceParams pierce;
            CornerParams corner;
            PowerCurve powerCurve;
            PassStrategy passStrategy;

            std::vector<EntitySnapshot> entities;
        };

        std::vector<LayerBucket> layers;

        // ---- 全局设置 ----
        bool selectionOnly = false;
        int32_t selectionCount = 0;
        bool emitArcs = true;
        bool skipHiddenLayers = true;
        bool skipLockedLayers = true;
        bool emitTrailingLaserOff = true;
        bool failOnUnmachinable = true;
    };

}  // namespace Cam

#endif  // CAM_TOOLPATH_JOB_INPUT_H
