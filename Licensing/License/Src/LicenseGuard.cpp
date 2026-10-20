#include "LicenseGuard.h"

#include <ctime>

LicenseGuard::GuardState LicenseGuard::s_state;

// ---- 辅助 ----

uint32_t LicenseGuard::Scramble(uint32_t val, uint32_t seed)
{
    // 简单的可逆混淆：ROL + XOR
    uint32_t r = (val << 7) | (val >> 25);
    return (r ^ (seed + 0x9E3779B9));
}

uint32_t LicenseGuard::ComputeCrossSum()
{
    return s_state.tokenA_hi ^ s_state.tokenA_lo ^ s_state.tokenB_hi ^ s_state.tokenB_lo ^ kMagicScatter;
}

bool LicenseGuard::VerifyGroupA()
{
    // A 组：tokenA_lo 必须是 tokenA_hi 的混淆结果
    // 使得 patch 时必须同时改两个值
    return s_state.tokenA_lo == Scramble(s_state.tokenA_hi, kMagicBase);
}

bool LicenseGuard::VerifyGroupB()
{
    // B 组：tokenB_lo 必须是 tokenB_hi 的不同混淆
    return s_state.tokenB_lo == Scramble(s_state.tokenB_hi, kMagicBase + 0x1234);
}

bool LicenseGuard::VerifyCrossSum()
{
    return s_state.crossSum == ComputeCrossSum();
}

// bool LicenseGuard::VerifyTimestamp()
//{
//     // 获取当前系统 Unix 时间戳（自 1970-01-01 以来的秒数）
//     uint32_t now = static_cast<uint32_t>(time(nullptr));
//     uint32_t diff = now - s_state.timestamp;
//
//     // 允许 ±1 秒误差 + 前向 10 年（防止 NTP 同步）
//     return diff < 3650u * 86400u && s_state.timestamp <= now + 1;
// }

bool LicenseGuard::VerifyTimestamp()
{
    // 1. 使用 64 位，彻底规避 2038/2106 问题
    int64_t now = static_cast<int64_t>(time(nullptr));
    int64_t ts = static_cast<int64_t>(s_state.timestamp);

    // 2. 有符号差值，避免无符号下溢
    int64_t diff = now - ts;

    // 3. 容忍窗口（根据业务调整）
    constexpr int64_t FUTURE_TOLERANCE = 300;      // 允许未来 5 分钟（NTP/时区）
    constexpr int64_t PAST_MAX_AGE = 366 * 86400;  // 最多早 1 年（根据 license 有效期定）

    // 4. 时间戳不能来自未来（超过容忍窗口）
    if (diff < -FUTURE_TOLERANCE)
    {
        return false;
    }

    // 5. 时间戳不能太旧（防止 freeze 旧时间）
    if (diff > PAST_MAX_AGE)
    {
        return false;
    }

    return true;
}

// ---- 公有 API ----

void LicenseGuard::MarkValid()
{
    s_state.tokenA_hi = kMagicBase;
    s_state.tokenA_lo = Scramble(kMagicBase, kMagicBase);

    s_state.tokenB_hi = kMagicBase + 0x5678;
    s_state.tokenB_lo = Scramble(kMagicBase + 0x5678, kMagicBase + 0x1234);

    s_state.crossSum = ComputeCrossSum();
    s_state.timestamp = static_cast<uint32_t>(time(nullptr));
}

void LicenseGuard::MarkInvalid()
{
    s_state.tokenA_hi = 0;
    s_state.tokenA_lo = 0;
    s_state.tokenB_hi = 0;
    s_state.tokenB_lo = 0;
    s_state.crossSum = 0;
    s_state.timestamp = 0;
}

void LicenseGuard::Refresh()
{
    MarkInvalid();
}

bool LicenseGuard::Check(Flavor flavor)
{
    // 不同 Flavor 走不同检查路径，patch 难度大幅上升
    switch (flavor)
    {
    case Flavor_Startup:
        // 启动时全量校验
        if (!VerifyGroupA())
        {
            return false;
        }
        if (!VerifyGroupB())
        {
            return false;
        }
        if (!VerifyCrossSum())
        {
            return false;
        }
        if (!VerifyTimestamp())
        {
            return false;
        }
        return true;

    case Flavor_Save:
        // 保存功能：检查 A 组 + 交叉和
        if (!VerifyGroupA())
        {
            return false;
        }
        if (!VerifyCrossSum())
        {
            return false;
        }
        return true;

    case Flavor_Export:
        // 导出功能：检查 B 组 + 交叉和
        if (!VerifyGroupB())
        {
            return false;
        }
        if (!VerifyCrossSum())
        {
            return false;
        }
        return true;

    case Flavor_Render:
        // 渲染功能：检查 A 组 + B 组 + 时间戳
        if (!VerifyGroupA())
        {
            return false;
        }
        if (!VerifyGroupB())
        {
            return false;
        }
        if (!VerifyTimestamp())
        {
            return false;
        }
        return true;

    case Flavor_Generic:
    default:
        // 通用：至少检查 A 组和交叉和
        if (!VerifyGroupA())
        {
            return false;
        }
        if (!VerifyCrossSum())
        {
            return false;
        }
        return true;
    }
}

bool LicenseGuard::IsQuickValid()
{
    // 快速路径：只检查 A 组是否有值（不校验数学关系）
    // 用于 UI 显示，不用于安全判断
    return s_state.tokenA_hi == kMagicBase && s_state.tokenB_hi == kMagicBase + 0x5678;
}