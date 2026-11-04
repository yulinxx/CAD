/**
 * @file LaserRoute.cpp
 * @brief 激光参数路由实现
 */
#include "LaserRoute.h"

#ifdef ENABLE_HARDWARE

    #include <QStringList>

    #include "DeviceHost.h"
    #include "Log/SyLogger.h"

    #include "Hardware/Device/IGalvoCard.h"
    #include "Hardware/Device/ILaserSource.h"
    #include "Hardware/Device/IMotionCard.h"

namespace LaserRoute
{
    Route resolve(DeviceHost* host)
    {
        Route r;
        if (!host || !host->isRunning())
        {
            return r;
        }
        r.galvo = host->galvoCard();
        r.motion = host->motionCard();
        r.laser = host->routedLaserSource();
        r.laserOrigin = host->routedLaserOrigin();
        return r;
    }

    bool apply(const Route& route, const Hw::LaserParams& params, QString& errorOut)
    {
        if (!route.laser)
        {
            // 没有激光器面不等于失败：运动卡机型可能由 IO 控光，
            // 功率由激光器面板手动给定。这种情况必须留痕，不能静默。
            SY_WARNF("[LaserRoute] No laser source on this machine (%s), laser params dropped",
                route.laserOrigin.toUtf8().constData());
            return true;
        }

        const Hw::HwResult r = route.laser->setParams(params);
        if (r.failed())
        {
            errorOut = QStringLiteral("SetLaserParams failed: [%1] %2")
                           .arg(QString::fromUtf8(Hw::hwErrorName(r.error)))
                           .arg(QString::fromUtf8(r.message));
            SY_ERRORF("[LaserRoute] %s", errorOut.toUtf8().constData());
            return false;
        }

        SY_INFOF("[LaserRoute] Laser params applied (%s): power=%.2f%% freq=%.1fHz pulse=%.1fus",
            route.laserOrigin.toUtf8().constData(), params.powerPercent, params.frequencyHz, params.pulseWidthUs);
        return true;
    }

    bool applyFromPlan(const Route& route, const Hw::MotionPlanView& view, ApplyReport& reportOut, QString& errorOut)
    {
        reportOut = ApplyReport{};

        // 先把所有参数组的下标收集出来：只有首段能在开工前落实，
        // 其余的存在与否决定是否要给操作员一条明确提示。
        size_t firstIndex = static_cast<size_t>(-1);
        for (size_t i = 0; i < view.commandCount; ++i)
        {
            if (view.commands[i].op == Hw::MotionOp::SetLaserParams)
            {
                if (firstIndex == static_cast<size_t>(-1))
                {
                    firstIndex = i;
                }
                ++reportOut.paramCount;
            }
        }

        if (firstIndex == static_cast<size_t>(-1))
        {
            return true;
        }

        const Hw::LaserParams& first = view.commands[firstIndex].laser;
        if (!apply(route, first, errorOut))
        {
            return false;
        }

        if (!route.laser)
        {
            // 没有激光器面的机型（由 IO 控光或面板手动给定功率）：
            // 参数无处落盘，必须显式告知操作员，但不得因此拒绝开工
            reportOut.unappliedCount = reportOut.paramCount;
            reportOut.warning = QStringLiteral(
                "Machine provides no laser source; %1 laser parameter group(s) are not applied by the host")
                .arg(reportOut.paramCount);
            SY_WARNF("[LaserRoute] %s", reportOut.warning.toUtf8().constData());
            return true;
        }
        reportOut.appliedToLaser = true;

        if (reportOut.paramCount > 1)
        {
            if (route.galvo)
            {
                // 振镜卡的指令流里带着参数变更，适配器自己会在执行到该段时生效
                reportOut.unappliedCount = 0;
            }
            else
            {
                // 运动卡的计划是整段预下发的，宿主无法在运行中插入一次激光器写入
                reportOut.unappliedCount = reportOut.paramCount - 1;
                reportOut.warning =
                    QStringLiteral("Plan contains %1 laser parameter groups; the motion card buffers the whole "
                                   "stream, so only the first group is applied to the laser source")
                        .arg(reportOut.paramCount);
                SY_WARNF("[LaserRoute] %s", reportOut.warning.toUtf8().constData());
            }
        }
        return true;
    }
}

#endif  // ENABLE_HARDWARE
