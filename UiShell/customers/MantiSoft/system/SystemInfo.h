#pragma once

#include <QString>

namespace MS
{
    // 硬件与系统信息：关于框「硬件信息」区块的数据来源。
    // 客户层自有实现，不依赖框架层；跨平台（Windows / macOS / Linux），
    // 不使用 Win32 API，避免与框架的平台层产生耦合。
    struct SystemInfo
    {
        QString osVersion;  // 操作系统版本，如 "Windows 11 Pro 23H2 (22631)" / "macOS 15.1 (24B83)"
        QString ramInfo;   // 物理内存，如 "16.0GB"
        QString cpuInfo;   // CPU 型号
        QString gpuInfo;   // 显卡型号

        // 采集全部信息。个别项采集失败时对应字段为空字符串，由调用方决定显示方式。
        static SystemInfo collect();
    };
}  // namespace MS
