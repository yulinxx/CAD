#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

// 激活失败节流器：连续失败后指数退避，达到上限触发硬锁定。
// 状态持久化到 configDir 下的保护文件（机器码派生 HMAC 签名），
// 用于阻止脚本高频调用 License_Activate 盲试注册码。
// 注意：本地防线只能提高攻击成本，删除状态文件即可重置计数，
// 这是纯客户端校验的固有上限；彻底防刷需要在线校验兜底。
class LicenseThrottle
{
public:
    struct Config
    {
        int freeAttempts = 3;       // 前几次连续失败不施加退避惩罚
        int baseDelaySec = 30;      // 退避基础等待（秒），超出免费次数后逐次翻倍
        int maxDelaySec = 3600;     // 单次退避延迟上限（秒）
        int hardLockFailures = 10;  // 连续失败达到该次数触发硬锁定
        int hardLockSec = 86400;    // 硬锁定持续时长（秒，默认 24 小时）
    };

    enum class Verdict
    {
        Allow,       // 允许本次尝试
        RateLimited, // 退避等待期未满
        HardLocked   // 硬锁定期未满
    };

    LicenseThrottle(std::string configDir, std::string machineCode);

    // 判断本次激活尝试是否放行；outRetryAfterSec 返回建议等待秒数
    Verdict Acquire(int64_t* outRetryAfterSec) const;

    // 记录一次校验失败：累加计数、推进退避或触发硬锁定，并持久化
    void RecordFailure();

    // 激活成功后清除失败状态
    void RecordSuccess();

    // 构造面向用户的等待提示文本（英文，经 API 透传给 UI）
    static std::string BuildRetryMessage(Verdict verdict, int64_t retryAfterSec);

    // 全局参数：生产环境使用默认值，仅测试钩子会修改
    static void SetConfig(const Config& config);
    static Config GetConfig();

private:
    struct State
    {
        int64_t failures = 0;       // 连续失败次数
        int64_t lastAttemptUnix = 0; // 最近一次失败时间戳（秒）
        int64_t lockUntilUnix = 0;   // 硬锁定截止时间戳（秒）
    };

    std::filesystem::path StateFilePath() const;
    bool Load(State& out) const;
    bool Save(const State& state) const;
    static int64_t ComputeBackoffDelay(int64_t failuresBeyondFree, const Config& cfg);

    std::string m_configDir;
    std::string m_machineCode;
};
