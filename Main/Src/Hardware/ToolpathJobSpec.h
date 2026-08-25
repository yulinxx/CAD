#pragma once

/**
 * @file ToolpathJobSpec.h
 * @brief 加工作业规格：把「要加工什么、用什么工艺参数」描述清楚。
 *
 * ==================== 为什么参数由调用方给，而不是编译器自己去查 ====================
 *
 * 仓库里目前有四套互不相通的工艺参数：
 *   - `HardwareProfile`（Engine2D，按图层绑定）—— 但 `HardwareProfileManager`
 *     全仓库没有任何实例化点，且 `hardwareProfileId` 不写入 .sy 文档；
 *   - `Eg::ProcessParams`（SQLite 预设，与图层/实体无绑定）；
 *   - `Eg::MaterialInfo` / `MaterialDatabase::MaterialInfo`（材料推荐值，两份）；
 *
 * 也就是说「按图层取工艺参数」这条链路当前是断的（详见
 * Docs/05-硬件与设备/硬件抽象层架构.md 的已知待办）。
 *
 * 因此本结构刻意把参数做成**显式入参**：编译器不去猜、不去查那张还没接通的表，
 * 由调用方（命令处理器）负责决定参数来源——今天可以是默认值 + 图层覆盖，
 * 将来 HardwareProfileManager 接通后换成从它读取，编译器一行都不用改。
 */

#include <QMap>
#include <QString>
#include <QVector>

#include <cstdint>

/**
 * @brief 一个图层（或作业默认）的工艺参数。
 */
struct LayerProcessParams
{
    double powerPercent = 50.0;      ///< 出光功率百分比 0–100
    double speedMmPerSec = 100.0;    ///< 加工速度
    double frequencyHz = 20000.0;    ///< 脉冲频率
    double pulseWidthUs = 0.0;       ///< 脉宽；CW 激光器填 0
    int32_t waveformIndex = 0;       ///< 波形序号（MOPA）
    int32_t passes = 1;              ///< 加工遍数
    bool enabled = true;             ///< false 表示该图层不参与本次加工

    bool valid() const
    {
        return powerPercent >= 0.0 && powerPercent <= 100.0
            && speedMmPerSec > 0.0
            && frequencyHz >= 0.0
            && pulseWidthUs >= 0.0
            && passes >= 1;
    }
};

/**
 * @brief 一次加工作业的完整规格。
 */
struct ToolpathJobSpec
{
    /// 作业标识，会写进 MotionPlan 的 planId，日志与进度上报都用它
    QString jobId = QStringLiteral("job");

    /// 未在 layerParams 中指定的图层使用的参数
    LayerProcessParams defaultParams;

    /// 图层 ID → 工艺参数
    QMap<int, LayerProcessParams> layerParams;

    /// 只加工选中图元。为空且本标志为 true 时视为「没有可加工内容」而报错，
    /// 不会静默退化成加工全图 —— 那会直接烧废整块料。
    bool selectionOnly = false;
    QVector<int64_t> selectedEntityIds;

    /// 空移速度
    double rapidSpeedMmPerSec = 300.0;

    /// 曲线离散的弦高容差（mm）。合法区间 (0, 1.0]，超界由编译器直接报错。
    ///
    /// 注意当前 Engine2D 的实际行为：圆 / 圆弧 / 椭圆的离散**不看这个值**，
    /// 一律按固定 32 个采样点走（BezierAlgorithms::discretizeEntity 的 default 分支
    /// 把该参数当成采样点个数，且 <2 时回落到 32）。只有 Bezier / Spline 真正按容差细分。
    /// 也就是说：调小它对圆弧没有任何效果 —— 想要更精细的圆弧，
    /// 要么让卡走 ArcTo（emitArcs = true），要么先把 Engine2D 的离散接口补上真正的弦高细分。
    double chordToleranceMm = 0.01;

    /// true 时圆弧直接编译成 ArcTo；false 时一律离散为直线。
    /// 不支持圆弧的卡由适配器自己降级，这里保留开关是为了排查
    /// 「圆弧走偏」类问题时能快速二分。
    bool emitArcs = true;

    /// 跳过隐藏 / 锁定的图层与图元
    bool skipHiddenLayers = true;
    bool skipLockedLayers = true;

    /// 每遍结束后是否补一条显式关光指令
    bool emitTrailingLaserOff = true;
};

/**
 * @brief 编译结果的统计与诊断信息。
 */
struct ToolpathCompileResult
{
    bool ok = false;
    QString error;                ///< ok=false 时的原因，可直接展示

    int32_t layerCount = 0;       ///< 实际参与加工的图层数
    int32_t entityCount = 0;      ///< 成功编译的图元数
    int32_t skippedEntityCount = 0;   ///< 因隐藏/锁定/无几何被跳过的图元数
    int32_t degradedEntityCount = 0;  ///< 因段类型不支持而整体离散的图元数
    size_t commandCount = 0;      ///< 生成的指令条数
    double boundsMinX = 0.0;
    double boundsMinY = 0.0;
    double boundsMaxX = 0.0;
    double boundsMaxY = 0.0;
};
