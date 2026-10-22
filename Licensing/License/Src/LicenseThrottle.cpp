#include "LicenseThrottle.h"

#include "Log/SyLogger.h"

#include <openssl/evp.h>
#include <openssl/hmac.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <vector>

namespace
{
    // 固定盐值与机器码拼接后作为状态文件的 HMAC 密钥
    const char kHmacSalt[] = "sanyi-license-throttle-v1";
    constexpr char kStateFileName[] = "license.throttle";

    // 全局节流参数：默认值即生产策略，测试钩子可临时覆盖
    LicenseThrottle::Config g_throttleConfig;

    int64_t WallNowUnix()
    {
        return static_cast<int64_t>(std::time(nullptr));
    }

    std::string EncodeBase64(const std::string& raw)
    {
        if (raw.empty())
        {
            return {};
        }

        std::vector<unsigned char> out(((raw.size() + 2) / 3) * 4 + 4);
        const int written = EVP_EncodeBlock(
            out.data(), reinterpret_cast<const unsigned char*>(raw.data()), static_cast<int>(raw.size()));
        if (written <= 0)
        {
            return {};
        }
        return std::string(reinterpret_cast<const char*>(out.data()), static_cast<size_t>(written));
    }

    std::string DecodeBase64(const std::string& b64)
    {
        if (b64.empty())
        {
            return {};
        }

        std::vector<unsigned char> out((b64.size() / 4 + 1) * 3 + 4);
        const int written = EVP_DecodeBlock(
            out.data(), reinterpret_cast<const unsigned char*>(b64.data()), static_cast<int>(b64.size()));
        if (written <= 0)
        {
            return {};
        }

        size_t padding = 0;
        for (auto it = b64.rbegin(); it != b64.rend() && *it == '='; ++it)
        {
            ++padding;
        }

        const size_t outLen = static_cast<size_t>(written) - std::min(padding, static_cast<size_t>(written));
        out.resize(outLen);
        return std::string(reinterpret_cast<const char*>(out.data()), outLen);
    }

    std::string ComputeHmac(const std::string& key, const std::string& data)
    {
        unsigned char digest[EVP_MAX_MD_SIZE] = {};
        unsigned int digestLen = 0;
        if (!HMAC(EVP_sha256(),
                key.data(),
                static_cast<int>(key.size()),
                reinterpret_cast<const unsigned char*>(data.data()),
                data.size(),
                digest,
                &digestLen))
        {
            return {};
        }
        return std::string(reinterpret_cast<const char*>(digest), digestLen);
    }

    std::string TrimWhitespace(const std::string& text)
    {
        size_t begin = 0;
        size_t end = text.size();
        while (begin < end && std::isspace(static_cast<unsigned char>(text[begin])))
        {
            ++begin;
        }
        while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1])))
        {
            --end;
        }
        return text.substr(begin, end - begin);
    }

    // 将秒数格式化为可读时长（小时/分钟/秒）
    std::string FormatDuration(int64_t seconds)
    {
        char buf[64] = {};
        if (seconds >= 3600)
        {
            const int64_t h = seconds / 3600;
            std::snprintf(buf, sizeof(buf), "%lld hour%s", static_cast<long long>(h), h == 1 ? "" : "s");
        }
        else if (seconds >= 60)
        {
            const int64_t m = seconds / 60;
            std::snprintf(buf, sizeof(buf), "%lld minute%s", static_cast<long long>(m), m == 1 ? "" : "s");
        }
        else
        {
            std::snprintf(buf, sizeof(buf), "%lld second%s", static_cast<long long>(seconds), seconds == 1 ? "" : "s");
        }
        return buf;
    }
}  // namespace

LicenseThrottle::LicenseThrottle(std::string configDir, std::string machineCode)
    : m_configDir(std::move(configDir))
    , m_machineCode(std::move(machineCode))
{
}

LicenseThrottle::Verdict LicenseThrottle::Acquire(int64_t* outRetryAfterSec) const
{
    if (outRetryAfterSec)
    {
        *outRetryAfterSec = 0;
    }

    const Config cfg = GetConfig();
    State s;
    Load(s);

    const int64_t now = WallNowUnix();

    // 硬锁定优先：锁定期内一律拒绝
    if (s.lockUntilUnix > now)
    {
        if (outRetryAfterSec)
        {
            *outRetryAfterSec = s.lockUntilUnix - now;
        }
        return Verdict::HardLocked;
    }

    // 退避期：失败次数超过免费额度后，每次尝试前需等待递增的延迟
    if (s.failures >= cfg.freeAttempts && s.failures < cfg.hardLockFailures)
    {
        const int64_t delay = ComputeBackoffDelay(s.failures - cfg.freeAttempts, cfg);
        int64_t elapsed = now - s.lastAttemptUnix;
        if (elapsed < 0)
        {
            elapsed = 0;  // 时钟回拨防御
        }
        if (elapsed < delay)
        {
            if (outRetryAfterSec)
            {
                *outRetryAfterSec = delay - elapsed;
            }
            return Verdict::RateLimited;
        }
    }

    return Verdict::Allow;
}

void LicenseThrottle::RecordFailure()
{
    const Config cfg = GetConfig();
    State s;
    Load(s);

    const int64_t now = WallNowUnix();

    // 硬锁定已过期：新一轮锁定周期从零计数，保证每个锁定时长内最多尝试 hardLockFailures 次
    if (s.lockUntilUnix > 0 && now >= s.lockUntilUnix)
    {
        s = State{};
    }

    // 距上次失败超过硬锁定时长则计数衰减归零，容忍正常用户偶发输错
    if (s.lastAttemptUnix > 0 && now - s.lastAttemptUnix > cfg.hardLockSec)
    {
        s = State{};
    }

    ++s.failures;
    s.lastAttemptUnix = now;
    if (s.failures >= cfg.hardLockFailures)
    {
        s.lockUntilUnix = now + cfg.hardLockSec;
    }

    Save(s);
}

void LicenseThrottle::RecordSuccess()
{
    std::error_code ec;
    std::filesystem::remove(StateFilePath(), ec);
}

std::string LicenseThrottle::BuildRetryMessage(Verdict verdict, int64_t retryAfterSec)
{
    const std::string duration = FormatDuration(retryAfterSec > 0 ? retryAfterSec : 1);
    if (verdict == Verdict::HardLocked)
    {
        return "Activation is temporarily locked due to repeated failed attempts. Please try again in " + duration + ".";
    }
    return "Too many failed activation attempts. Please try again in " + duration + ".";
}

void LicenseThrottle::SetConfig(const Config& config)
{
    g_throttleConfig = config;
}

LicenseThrottle::Config LicenseThrottle::GetConfig()
{
    return g_throttleConfig;
}

std::filesystem::path LicenseThrottle::StateFilePath() const
{
    return std::filesystem::path(m_configDir) / kStateFileName;
}

bool LicenseThrottle::Load(State& out) const
{
    out = State{};

    std::ifstream file(StateFilePath());
    if (!file)
    {
        return false;
    }

    std::string content;
    std::string line;
    while (std::getline(file, line))
    {
        content += line;
    }
    content = TrimWhitespace(content);

    // 文件格式: Base64(failures|lastAttempt|lockUntil).Base64(HMAC-SHA256)
    const auto dotPos = content.find('.');
    if (dotPos == std::string::npos)
    {
        SY_WARN("[LicenseThrottle] throttle state file malformed, resetting counters");
        return false;
    }

    const std::string payload = DecodeBase64(content.substr(0, dotPos));
    const std::string storedMac = DecodeBase64(content.substr(dotPos + 1));
    const std::string expectedMac = ComputeHmac(m_machineCode + kHmacSalt, payload);

    if (payload.empty() || storedMac.empty() || storedMac != expectedMac)
    {
        SY_WARN("[LicenseThrottle] throttle state HMAC mismatch (tampered?), resetting counters");
        return false;
    }

    long long failures = 0;
    long long lastAttempt = 0;
    long long lockUntil = 0;
    const int matched = std::sscanf(payload.c_str(), "%lld|%lld|%lld", &failures, &lastAttempt, &lockUntil);
    if (matched != 3 || failures < 0)
    {
        SY_WARN("[LicenseThrottle] throttle state parse failed, resetting counters");
        out = State{};
        return false;
    }

    out.failures = failures;
    out.lastAttemptUnix = lastAttempt;
    out.lockUntilUnix = lockUntil;
    return true;
}

bool LicenseThrottle::Save(const State& state) const
{
    char payloadBuf[128] = {};
    std::snprintf(payloadBuf,
        sizeof(payloadBuf),
        "%lld|%lld|%lld",
        static_cast<long long>(state.failures),
        static_cast<long long>(state.lastAttemptUnix),
        static_cast<long long>(state.lockUntilUnix));
    const std::string payload(payloadBuf);

    const std::string mac = ComputeHmac(m_machineCode + kHmacSalt, payload);
    if (mac.empty())
    {
        return false;
    }

    const std::string encoded = EncodeBase64(payload) + "." + EncodeBase64(mac);

    std::error_code ec;
    const std::filesystem::path dir(m_configDir);
    if (!std::filesystem::exists(dir, ec))
    {
        std::filesystem::create_directories(dir, ec);
    }

    std::ofstream file(StateFilePath(), std::ios::out | std::ios::trunc);
    if (!file)
    {
        SY_WARN("[LicenseThrottle] failed to persist throttle state");
        return false;
    }
    file << encoded;
    return file.good();
}

int64_t LicenseThrottle::ComputeBackoffDelay(int64_t failuresBeyondFree, const Config& cfg)
{
    int64_t delay = cfg.baseDelaySec;
    for (int64_t i = 0; i < failuresBeyondFree && delay < cfg.maxDelaySec; ++i)
    {
        delay *= 2;
    }
    return std::min(delay, static_cast<int64_t>(cfg.maxDelaySec));
}