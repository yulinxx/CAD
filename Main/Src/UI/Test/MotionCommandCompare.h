#pragma once

/**
 * @file MotionCommandCompare.h
 * @brief MotionPlan IR 指令的逐字段比较（测试专用共享实现）。
 *
 * `MotionPlanKerfLeadTests` 与 `MotionPlanProcessMigrationTests` 都需要断言
 * 「两段编译结果完全相同」。这类断言只能有一份实现：两份各自维护的比较函数
 * 迟早会漏掉某个字段，而漏掉的字段恰恰就是回归没被发现的字段。
 *
 * 取**全集比较**：`MotionCommand` 的全部字段都参与比较，包括注释文本 `text`。
 * 理由：IR 会被导出成加工文件离线回放，任何字段不同都是行为不同；
 * 不存在「这个字段不影响刀路所以可以不比」的字段。
 *
 * 不用 memcmp：MotionCommand 是 POD，结构体填充字节（padding）的内容
 * 未定义，memcmp 会把等值的两条指令判成不同。
 */

#include <cstring>

#include "Hardware/Device/MotionPlan.h"

namespace MotionTest
{
    /// 逐字段比较两条 IR 指令；字段集合 = MotionCommand 全集。
    inline bool sameCommand(const Hw::MotionCommand& a, const Hw::MotionCommand& b)
    {
        if (a.op != b.op || a.laserOn != b.laserOn ||
            a.counterClockwise != b.counterClockwise ||
            a.durationUs != b.durationUs || a.intArg != b.intArg ||
            a.channel != b.channel || a.level != b.level)
        {
            return false;
        }
        if (a.target.count != b.target.count ||
            a.rate != b.rate || a.centerX != b.centerX || a.centerY != b.centerY)
        {
            return false;
        }
        for (int32_t i = 0; i < a.target.count; ++i)
        {
            if (a.target.v[i] != b.target.v[i])
            {
                return false;
            }
        }
        if (std::strncmp(a.text, b.text, sizeof(a.text)) != 0)
        {
            return false;
        }
        return a.laser.powerPercent == b.laser.powerPercent &&
               a.laser.frequencyHz == b.laser.frequencyHz &&
               a.laser.pulseWidthUs == b.laser.pulseWidthUs &&
               a.laser.waveformIndex == b.laser.waveformIndex;
    }
}  // namespace MotionTest
