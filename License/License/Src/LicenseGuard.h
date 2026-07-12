#pragma once

#include <cstdint>
#include <atomic>

// ---------------------------------------------------------------
// 散射状态校验器（替代单 bool 校验）
//
// 解决的问题：
//   bool isValid 会被逆向工程师直接内存 patch 绕过。
//
// 方案：
//   将验证状态分散到多个相互关联的 int32_t 变量中，
//   不同调用者使用不同的 Flavor 检查不同的变量组合，
//   patch 全部变量且保持数学关系的难度远高于 patch 单个 bool。
// ---------------------------------------------------------------

class LicenseGuard
{
public:
    // 不同调用场景使用不同 Flavor，导致检查路径不同
    enum Flavor : uint32_t
    {
        Flavor_Startup  = 0x1A3C,
        Flavor_Save     = 0x2B4D,
        Flavor_Export   = 0x3C5E,
        Flavor_Render   = 0x4D6F,
        Flavor_Generic  = 0x5E7A,
    };

    // ---------- 生命周期 ----------

    // 标记为"已授权"（激活成功或启动校验通过后调用）
    static void MarkValid();

    // 标记为"未授权"（清除许可时调用）
    static void MarkInvalid();

    // 刷新：清空状态，下次 Check() 会强制从 LicenseManager 重新获取
    static void Refresh();

    // ---------- 校验 ----------

    // 核心校验函数。不同 Flavor 使用不同变量组合进行检查。
    static bool Check(Flavor flavor);

    // 快速检查是否标记为有效（不持久验证，仅查内存状态）
    static bool IsQuickValid();

private:
    // 散射状态 —— 多个相互关联的变量
    struct GuardState
    {
        // 主状态组 A
        std::atomic<uint32_t> tokenA_hi{ 0 };
        std::atomic<uint32_t> tokenA_lo{ 0 };
        // 主状态组 B
        std::atomic<uint32_t> tokenB_hi{ 0 };
        std::atomic<uint32_t> tokenB_lo{ 0 };
        // 交叉校验和
        std::atomic<uint32_t> crossSum{ 0 };
        // 有效期标记时间戳（防止重放）
        std::atomic<uint32_t> timestamp{ 0 };
    };

    static GuardState s_state;

    // 内部常量
    static constexpr uint32_t kMagicBase    = 0x3E1F5A8C;
    static constexpr uint32_t kMagicScatter = 0x7C3D9B1E;

    // ---------- 辅助函数 ----------

    static uint32_t Scramble(uint32_t val, uint32_t seed);
    static uint32_t ComputeCrossSum();
    static bool     VerifyGroupA();
    static bool     VerifyGroupB();
    static bool     VerifyCrossSum();
    static bool     VerifyTimestamp();
};
