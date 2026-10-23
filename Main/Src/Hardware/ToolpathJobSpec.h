#pragma once

/**
 * @file ToolpathJobSpec.h
 * @brief 加工作业规格：把「要加工什么、用什么工艺参数」描述清楚。
 *
 * ==================== 为什么参数由调用方给，而不是编译器自己去查 ====================
 *
 * 仓库里此前有四套互不相通的工艺参数：
 *   - `HardwareProfile`（Engine2D，按图层绑定）—— 但 `HardwareProfileManager`
 *     全仓库没有任何实例化点，且 `hardwareProfileId` 不写入 .sy 文档；
 *   - `Eg::ProcessParams`（SQLite 预设，与图层/图元无绑定）；
 *   - `Eg::MaterialInfo` / `MaterialDatabase::MaterialInfo`（材料推荐值，两份）；
 *   - 本文件里的 `LayerProcessParams`（第四份）。
 *
 * v2 起统一为**一份**：`Eg::ProcessParams`（Engine/Common/Include/Engine/Process/）。
 * 它被三方同时看见：FileIO（写进 .sy）、Engine2D/Cam（生成刀路）、UI（工艺面板）。
 * 放在 Engine/Common 是因为 FileIO 只依赖 EngineCommon，Engine2D 也依赖 EngineCommon ——
 * 放这里三方都能用，且不产生任何新的模块依赖。
 *
 * 本文件里的 `LayerProcessParams` 保留为**类型别名**（不是第二份定义），
 * 让既有调用点（编译器、作业服务、测试）不必大改，语义却已经收成一份。
 *
 * ==================== 工艺参数的来源 ====================
 *
 * 接下来由调用方（命令处理器）负责决定参数来源 ——
 * 今天可以是默认值 + 图层覆盖，将来接通工艺库（ProcessLibrary）后从那里读，
 * 编译器一行都不用改。
 */

#include <QMap>
#include <QString>
#include <QStringList>
#include <QVector>

#include <cstdint>

#include "Engine/Process/ProcessParams.h"
#include "Engine2D/Machine/MachineFrame.h"

/**
 * @brief 一个图层（或作业默认）的工艺参数。
 *
 * **类型别名**，不是第二份定义 —— 参数只有一份，见文件头说明。
 */
using LayerProcessParams = Eg::ProcessParams;

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

    /// 曲线离散的弦高容差（mm）。合法区间 (0, 1.0]，超界由编译器直接报错
    /// （上界是防单位填错的合理性检查，见 MotionPlanCompiler::compile）。
    ///
    /// Engine2D 的实际行为：Bezier / Spline 按此容差自适应细分；圆 / 圆弧 / 椭圆按
    /// 弦高公式取段数（CircleAlgorithms::sampleCountForTolerance，sagitta = R(1-cos(Δ/2))），
    /// 调小它 → 段数变多、折线更贴合真实曲线。
    /// 想完全不离散圆弧，仍可用 emitArcs = true 让卡直接走 ArcTo。
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

    /// 遇到「这类图元暂时无法加工」时（位图光栅未实现、条码无模块几何等），
    /// 是整份作业失败（true，默认），还是跳过并在结果里报告（false）。
    ///
    /// 默认 true 是**故意的**：跳过意味着「用户以为烧了、其实没烧」——
    /// 在激光加工里这等于废件。宁可拒绝开工并说清楚是哪一类图元，
    /// 也不要默默少烧一块。
    ///
    /// 界面后续可以提供「忽略这些图元继续」的显式开关（那时才置 false），
    /// 前提是它把被跳过的清单完整展示给操作者。
    bool failOnUnmachinableEntity = true;

    /**
     * @brief 机器坐标系（用户空间 → 机器空间）
     *
     * 默认与用户空间一致（左下角原点、恒等变换），即零影响。
     *
     * 换算**只在这里发生一次**（编译器的最后一步），适配器与作业服务都不再换算。
     * 之前的新 HAL 链路根本没做换算，而旧的 UI2D 链路在服务层换算 ——
     * 两套坐标语义并存，是「同一份数据在不同路径下烧出不同位置」的根因。
     */
    Eg::MachineFrameConfig machineFrame;
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

    /// 无法加工的图元数量（位图 / 条码 / 3D 网格等）。
    /// failOnUnmachinableEntity = false 时，这些图元被跳过而其余照常加工。
    int32_t unmachinableEntityCount = 0;

    /// 无法加工图元的可读清单（最多若干条，用于界面展示）。
    /// 形如 "3 x IMAGE (raster engraving engine not implemented yet)"。
    QStringList unmachinableDetails;
};
