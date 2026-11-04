#pragma once

/**
 * @file LaserRoute.h
 * @brief 激光参数路由：把计划里的功率/频率/脉宽投递到真正能生效的一方。
 *
 * ==================== 它解决什么问题 ====================
 *
 * MotionPlan 里的 SetLaserParams 只会随 loadPlan 交给「执行计划的那块卡」：
 *   - 振镜卡（Bjjcz / Scanlab）自己带功率 DA，能直接生效；
 *   - 运动卡（Leadshine / Zmotion / Aerotech）只把指令记进缓冲区，
 *     完全不碰激光器 —— 于是激光器收到的功率永远是默认值。
 *
 * 运动卡机型的激光器是独立的 IDevice，必须由宿主主动投递。
 * 本文件就是那条投递路径：按「设备组」解析出运动卡/振镜卡/激光器三个目标，
 * 再把参数同时给到能生效的一方。
 *
 * ==================== 为什么不能每条指令都投递 ====================
 *
 * 运动卡的计划是整段预下发的，中途的 SetLaserParams 已经进入卡的缓冲区，
 * 宿主无法在运行中插入一次串口写。因此只能应用首段参数，
 * 其余参数组必须**明确告知操作员未生效**，而不是假装成功。
 */

#ifdef ENABLE_HARDWARE

#include <QString>

#include "Hardware/Device/MotionPlan.h"

class DeviceHost;

namespace Hw
{
    class IGalvoCard;
    class ILaserSource;
    class IMotionCard;
}

namespace LaserRoute
{
    /// 一台机器的设备组：执行方 + 激光器。任一方都可以为空。
    struct Route
    {
        Hw::IMotionCard* motion = nullptr;
        Hw::IGalvoCard* galvo = nullptr;
        Hw::ILaserSource* laser = nullptr;

        /// 激光器的来源描述（"device" / "attached" / "none"），仅用于日志
        QString laserOrigin;

        bool canExecute() const { return motion != nullptr || galvo != nullptr; }
    };

    /// 从宿主解析设备组。每次调用都重新解析，不缓存指针。
    Route resolve(DeviceHost* host);

    /// 一次投递的结果。
    struct ApplyReport
    {
        bool appliedToLaser = false;  ///< 是否真的调用了 ILaserSource::setParams
        size_t paramCount = 0;        ///< 计划中的参数组数量
        size_t unappliedCount = 0;    ///< 运行中无法生效的参数组数量
        QString warning;              ///< 需要展示给操作员的说明，空串表示无需提示
    };

    /**
     * @brief 把计划里的激光参数投递到激光器。
     * @param errorOut 投递失败的原因（激光器拒绝参数时）
     * @return 失败表示参数被设备拒绝，作业不得继续 ——
     *         带着错误功率开光比不开工危险得多，不做「忽略并继续」。
     */
    bool applyFromPlan(const Route& route, const Hw::MotionPlanView& view, ApplyReport& reportOut, QString& errorOut);

    /// 投递单组参数（首段参数、运行期手动改功率都走这里）。
    bool apply(const Route& route, const Hw::LaserParams& params, QString& errorOut);
}

#endif  // ENABLE_HARDWARE
