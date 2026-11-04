#pragma once

/**
 * @file CamPlanEmitter.h
 * @brief Cam::CamPlan → Hw::MotionPlan 的唯一发射器。
 *
 * 职责边界：
 *   - CAM 只产出用户坐标的工艺几何（CamPlan.h），不认识任何设备；
 *   - Hardware 只认设备指令（Hw::MotionPlan）；
 *   - 本文件是二者之间唯一的翻译点，也是「用户坐标 → 机器坐标」的唯一变换点
 *     （CamInputBridge.cpp 的约定：桥接层不换算，换算在发射器一步完成）。
 *
 * 除 CAM 的 CamPlan 与 Hardware 的 MotionPlanBuilder 外，只引入 Eg::MachineFrame：
 * 镜像/平移在仓库里已有唯一实现（MachineFrame.h 的 toMachine / isReflected），
 * 这里复用它，不另造第二套坐标变换。
 *
 * 仅在 BUILD_CAM 且 BUILD_HARDWARE 时编入。
 */

#include "CAM/CamPlan.h"
#include "Engine2D/Machine/MachineFrame.h"
#include "Hardware/Device/MotionPlan.h"

#include <cstddef>

namespace CamPlanEmitter
{
    /**
     * @brief 把一份 CamPlan 翻译成设备指令，追加到 out 末尾。
     *
     * 每个图层的指令骨架：
     *   BeginLayer → SetLaserParams → SetFeedRate → SetRapidRate
     *   → BeginPass → 各轮廓的段 → LaserOff → EndPass → EndLayer
     *
     * 段级覆盖规则：
     *   - passIndex >= 0 且与当前遍不同：成对 EndPass / BeginPass；
     *   - 出光段 powerPercent >= 0 时覆盖功率，speedMmPerSec > 0 时覆盖进给，
     *     与当前值相同时不重复下发；
     *   - 空移段（isRapid）忽略功率/速度覆盖 —— 停光段的功率没有意义，
     *     空移速度统一由图层头的 SetRapidRate 决定。
     *
     * @param plan   CAM 编译结果（用户坐标；空 plan 不做任何事）
     * @param frame  机器坐标帧（仅此处换算；反射时圆弧绕向反转）
     * @param out    目标构建器（本函数不清空，调用方负责生命周期）
     * @return 发出的运动段数（不含图层头、参数与遍标记）
     */
    size_t emitCamPlan(const Cam::CamPlan& plan,
        const Eg::MachineFrame& frame,
        Hw::MotionPlanBuilder& out);

}  // namespace CamPlanEmitter
