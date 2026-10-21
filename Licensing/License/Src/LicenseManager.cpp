#include "LicenseManager.h"
#include "LicenseGuard.h"
#include "LicenseInternal.h"
#include "LicenseThrottle.h"
#include "MachineFingerprint.h"
#include "StrEncrypt.h"

#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/pem.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

namespace
{
    DEFINE_ENCRYPTED_STR(kPublicKey,
        0x67,
        0x4A,
        0x45,
        0x44,
        0x47,
        0x46,
        0x2E,
        0x28,
        0x29,
        0x26,
        0x3E,
        0x51,
        0x22,
        0x26,
        0x36,
        0x39,
        0x3F,
        0x34,
        0x58,
        0x32,
        0x3F,
        0x22,
        0x51,
        0x50,
        0x53,
        0x52,
        0xAD,
        0x8C,
        0x88,
        0xCE,
        0xCD,
        0xCC,
        0xC4,
        0xCE,
        0xE2,
        0xC8,
        0xC4,
        0xC9,
        0xEB,
        0xE6,
        0xFF,
        0xE7,
        0xFB,
        0xF8,
        0xD5,
        0xAA,
        0xE3,
        0xA5,
        0xD4,
        0xD6,
        0xC9,
        0xDC,
        0xDC,
        0xDA,
        0xDD,
        0xD2,
        0xDD,
        0xDE,
        0xF1,
        0x99,
        0xE3,
        0xEE,
        0xED,
        0xEC,
        0xE4,
        0xE4,
        0xCF,
        0xE2,
        0xE9,
        0xEA,
        0xFD,
        0xE8,
        0xEF,
        0xD9,
        0xD4,
        0x9A,
        0xFD,
        0xC2,
        0xD2,
        0xC4,
        0xE4,
        0xC5,
        0xDA,
        0xEA,
        0xF2,
        0xD8,
        0xD6,
        0xD8,
        0x8D,
        0xFE,
        0xAB,
        0xF1,
        0x80,
        0xCE,
        0xCE,
        0x87,
        0xB2,
        0xA4,
        0xFB,
        0x99,
        0xAF,
        0xA7,
        0xFA,
        0x9B,
        0x8C,
        0xF6,
        0xAA,
        0x92,
        0xAB,
        0xFC,
        0x98,
        0x90,
        0x91,
        0xA6,
        0x8E,
        0x91,
        0xB9,
        0xF4,
        0x8E,
        0xA7,
        0x84,
        0xA8,
        0x8E,
        0xD4,
        0x93,
        0xAB,
        0xB5,
        0xB5,
        0x94,
        0xA1,
        0xDC,
        0xB1,
        0xA2,
        0x9B,
        0xDD,
        0xA3,
        0xB8,
        0xBD,
        0xBF,
        0x93,
        0x82,
        0xBE,
        0xB9,
        0xB2,
        0xBF,
        0xC7,
        0xCE,
        0x80,
        0xB1,
        0x8F,
        0xBF,
        0x98,
        0xC8,
        0x88,
        0x4D,
        0x79,
        0x31,
        0x71,
        0x2B,
        0x08,
        0x0C,
        0x53,
        0x30,
        0x70,
        0x41,
        0x60,
        0x5B,
        0x57,
        0x78,
        0x24,
        0x52,
        0x3E,
        0x7B,
        0x70,
        0x7E,
        0x62,
        0x43,
        0x51,
        0x56,
        0x5C,
        0x4D,
        0x5D,
        0x69,
        0x2F,
        0x58,
        0x4A,
        0x6C,
        0x65,
        0x45,
        0x52,
        0x57,
        0x56,
        0x60,
        0x62,
        0x44,
        0x53,
        0x4E,
        0x1C,
        0x7E,
        0x1A,
        0x6D,
        0x4B,
        0x45,
        0x49,
        0x45,
        0x62,
        0x1F,
        0x7A,
        0x66,
        0x5E,
        0x73,
        0x5C,
        0x09,
        0x7A,
        0x74,
        0x64,
        0x7D,
        0x5A,
        0x12,
        0x75,
        0x7A,
        0x6C,
        0x72,
        0x00,
        0x03,
        0x4A,
        0x42,
        0x7D,
        0x0E,
        0x08,
        0x7A,
        0x17,
        0x1F,
        0x78,
        0x12,
        0x1A,
        0x3B,
        0x06,
        0x2E,
        0x22,
        0x66,
        0x15,
        0x08,
        0x17,
        0x36,
        0x09,
        0x16,
        0x27,
        0x0E,
        0x13,
        0x15,
        0x15,
        0x0F,
        0x0D,
        0x20,
        0x20,
        0x0E,
        0x22,
        0x3B,
        0x22,
        0x05,
        0x5B,
        0x07,
        0x0C,
        0x3B,
        0x38,
        0x26,
        0x29,
        0x3D,
        0x31,
        0x1E,
        0x05,
        0x42,
        0x2E,
        0x08,
        0x12,
        0x0F,
        0x22,
        0x12,
        0x17,
        0x2A,
        0x15,
        0xD7,
        0xF8,
        0xE3,
        0xB4,
        0xF4,
        0xD1,
        0xD5,
        0xD6,
        0xBE,
        0x84,
        0x80,
        0xEF,
        0xB5,
        0xEF,
        0xB6,
        0xCB,
        0xC3,
        0xFB,
        0xC7,
        0xA5,
        0xCD,
        0xEC,
        0xE1,
        0xE0,
        0xF6,
        0xE0,
        0xAB,
        0xFD,
        0xF8,
        0xD2,
        0xF6,
        0xF7,
        0xF8,
        0xEC,
        0xF8,
        0xFB,
        0xCE,
        0xC0,
        0xC0,
        0xDE,
        0xC1,
        0xFF,
        0xD9,
        0xC9,
        0xE5,
        0xE4,
        0x99,
        0xD6,
        0xE3,
        0xC0,
        0xE2,
        0xE6,
        0x86,
        0x85,
        0x80,
        0xFD,
        0xCF,
        0x8F,
        0xF4,
        0xF9,
        0xED,
        0xC9,
        0xF7,
        0xD8,
        0xA9,
        0xF2,
        0xB8,
        0x90,
        0xAF,
        0x8B,
        0x8A,
        0xA9,
        0xFA,
        0xAB,
        0x8D,
        0xC6,
        0xC6,
        0x99,
        0xA4,
        0x8B,
        0xE9,
        0xA4,
        0xA7,
        0x81,
        0xE4,
        0xAD,
        0xBF,
        0xE4,
        0x9A,
        0x8C,
        0xEF,
        0xE3,
        0x9F,
        0x8C,
        0xA6,
        0x8E,
        0xB0,
        0x8A,
        0xB6,
        0xA7,
        0xA0,
        0x9D,
        0xBF,
        0xA8,
        0xC7,
        0xA2,
        0xBA,
        0x9E,
        0x96,
        0x8C,
        0xAF,
        0x81,
        0xB4,
        0x83,
        0xA8,
        0xA2,
        0xB2,
        0xC2,
        0x91,
        0xC2,
        0x92,
        0x8B,
        0xCA,
        0xB1,
        0xC8,
        0xA8,
        0x8F,
        0xAA,
        0x52,
        0x4A,
        0x54,
        0x37,
        0x34,
        0x30,
        0x49,
        0x66,
        0x61,
        0x78,
        0x5A,
        0x6A,
        0x6A,
        0x00,
        0x04,
        0x6C,
        0x41,
        0x58,
        0x56,
        0x52,
        0x45,
        0x54,
        0x54,
        0x1A,
        0x12,
        0x34,
        0x37,
        0x36,
        0x31,
        0x30,
        0x5B,
        0x51,
        0x64,
        0x01,
        0x72,
        0x76,
        0x66,
        0x69,
        0x6F,
        0x64,
        0x08,
        0x62,
        0x6F,
        0x72,
        0x01,
        0x00,
        0x03,
        0x02,
        0x1D,
        0x3C,
        0x38, )

    DEFINE_ENCRYPTED_STR(kErrNoLicense,
        0x11,
        0x2A,
        0x2A,
        0x32,
        0x25,
        0x3F,
        0x3C,
        0x20,
        0x3A,
        0x27,
        0x3A,
        0x34,
        0x3E,
        0x28,
        0x1C,
        0x34,
        0x2A,
        0x3C,
        0x3A,
        0x34,
        0x2B)

    DEFINE_ENCRYPTED_STR(kErrExpired,
        0xA5,
        0xD4,
        0xD7,
        0xD7,
        0xDA,
        0xC8,
        0xCF,
        0xDE,
        0xC8,
        0xD7,
        0xD6,
        0xCE,
        0xC8,
        0xDC,
        0xCF,
        0x22,
        0xCB,
        0xD5,
        0xD4,
        0xD5,
        0xD7,
        0xDE,
        0xD3,
        0xCA)

    std::vector<std::string> SplitString(const std::string& str, char delim)
    {
        std::vector<std::string> result;
        std::istringstream stream(str);
        std::string token;
        while (std::getline(stream, token, delim))
        {
            result.push_back(token);
        }
        return result;
    }

    std::string GetCurrentDate()
    {
        time_t now = time(nullptr);
        struct tm tmInfo{};
#if defined(_WIN32)
        localtime_s(&tmInfo, &now);
#else
        localtime_r(&now, &tmInfo);
#endif
        char buf[11] = {};
        strftime(buf, sizeof(buf), "%Y-%m-%d", &tmInfo);
        return buf;
    }

    bool IsExpired(const std::string& expiryDate)
    {
        return expiryDate < GetCurrentDate();
    }

    bool IsValidDate(const std::string& date)
    {
        if (date.size() != 10)
        {
            return false;
        }
        if (date[4] != '-' || date[7] != '-')
        {
            return false;
        }
        for (int i = 0; i < 10; ++i)
        {
            if (i != 4 && i != 7 && !std::isdigit(static_cast<unsigned char>(date[i])))
            {
                return false;
            }
        }
        return true;
    }

    std::vector<unsigned char> Base64Decode(const std::string& b64)
    {
        if (b64.empty())
        {
            return {};
        }

        const size_t maxOutLen = (b64.size() * 3) / 4 + 4;
        std::vector<unsigned char> result(maxOutLen);
        const int written = EVP_DecodeBlock(
            result.data(), reinterpret_cast<const unsigned char*>(b64.data()), static_cast<int>(b64.size()));
        if (written < 0)
        {
            return {};
        }

        size_t padding = 0;
        for (auto it = b64.rbegin(); it != b64.rend() && *it == '='; ++it)
        {
            ++padding;
        }

        const size_t outLen = static_cast<size_t>(written) - padding;
        result.resize(outLen);
        return result;
    }

    std::string FromUrlSafe(const std::string& urlSafe)
    {
        std::string s = urlSafe;
        std::replace(s.begin(), s.end(), '-', '+');
        std::replace(s.begin(), s.end(), '_', '/');
        const size_t mod = s.size() % 4;
        if (mod != 0)
        {
            s.append(4 - mod, '=');
        }
        return s;
    }
}  // anonymous namespace

LicenseManager::LicenseManager(const std::string& configDir)
    : m_configDir(configDir)
{
}

LicenseManager::~LicenseManager() = default;

bool LicenseManager::CheckLicense()
{
    if (!LicenseInternal::IsCheckEnabled())
    {
        m_info.isValid = true;
        LicenseGuard::MarkValid();
        return true;
    }

    if (!LoadAndVerify())
    {
        return false;
    }

    if (m_onlineVerifier && m_onlineVerifier->IsAvailable())
    {
        if (!m_onlineVerifier->Verify(m_info))
        {
            if (m_onlineVerifier->IsRequired())
            {
                m_info.errorMsg = "Online verification failed";
                return false;
            }
        }
    }

    LicenseGuard::MarkValid();
    return true;
}

ActivationResult LicenseManager::Activate(const std::string& regCode)
{
    if (!LicenseInternal::IsCheckEnabled())
    {
        m_info.isValid = true;
        LicenseGuard::MarkValid();
        return ActivationResult::Success;
    }

    const std::string machineCode = GetMachineCode();
    if (machineCode.empty())
    {
        m_info.errorMsg = "Failed to generate machine code";
        return ActivationResult::Failed;
    }

    // 失败节流：连续失败后指数退避，超限硬锁定，阻止脚本高频盲试
    LicenseThrottle throttle(m_configDir, machineCode);
    int64_t retryAfterSec = 0;
    const auto verdict = throttle.Acquire(&retryAfterSec);
    if (verdict != LicenseThrottle::Verdict::Allow)
    {
        m_info.errorMsg = LicenseThrottle::BuildRetryMessage(verdict, retryAfterSec);
        return ActivationResult::Locked;
    }

    LicenseInfoData info;
    if (!VerifyRegCode(machineCode, regCode, info))
    {
        m_info = info;
        throttle.RecordFailure();
        return ActivationResult::Failed;
    }

    if (!SaveLicense(regCode))
    {
        m_info.errorMsg = "Failed to save license file";
        return ActivationResult::Failed;
    }

    throttle.RecordSuccess();
    m_info = info;
    LicenseGuard::MarkValid();
    return ActivationResult::Success;
}

std::string LicenseManager::GetMachineCode() const
{
    return MachineFingerprint::Generate();
}

LicenseInfoData LicenseManager::GetLicenseInfo() const
{
    return m_info;
}

void LicenseManager::ClearLicense()
{
    m_info = {};
    LicenseGuard::MarkInvalid();
    std::error_code ec;
    std::filesystem::remove(std::filesystem::path(m_configDir) / "license.key", ec);
}

bool LicenseManager::ReValidate()
{
    if (!LicenseInternal::IsCheckEnabled())
    {
        return true;
    }

    if (!m_info.isValid)
    {
        return CheckLicense();
    }

    if (m_onlineVerifier && m_onlineVerifier->IsAvailable())
    {
        if (!m_onlineVerifier->Verify(m_info))
        {
            if (m_onlineVerifier->IsRequired())
            {
                return false;
            }
        }
    }

    return true;
}

void LicenseManager::SetOnlineVerifier(std::unique_ptr<IOnlineVerifier> verifier)
{
    m_onlineVerifier = std::move(verifier);
}

IOnlineVerifier* LicenseManager::GetOnlineVerifier() const
{
    return m_onlineVerifier.get();
}

bool LicenseManager::LoadAndVerify()
{
    const std::string regCode = ReadLicenseFile();
    if (regCode.empty())
    {
        m_info.errorMsg = kErrNoLicense();
        return false;
    }

    const std::string machineCode = GetMachineCode();
    if (machineCode.empty())
    {
        m_info.errorMsg = "Failed to generate machine code";
        return false;
    }

    return VerifyRegCode(machineCode, regCode, m_info);
}

bool LicenseManager::VerifyRegCode(
    const std::string& machineCode, const std::string& regCode, LicenseInfoData& outInfo) const
{
    // 验签前的所有格式/解码/签名失败统一返回同一提示，
    // 避免脚本通过差异化的错误信息探测注册码结构
    const char* const kInvalidRegCode = "Invalid registration code";

    const auto dotPos = regCode.find('.');
    if (dotPos == std::string::npos)
    {
        outInfo.errorMsg = kInvalidRegCode;
        return false;
    }

    const std::string payloadB64Url = regCode.substr(0, dotPos);
    const std::string sigB64Url = regCode.substr(dotPos + 1);

    const std::string payloadB64 = FromUrlSafe(payloadB64Url);
    const std::string sigB64 = FromUrlSafe(sigB64Url);

    const auto payloadBytes = Base64Decode(payloadB64);
    const auto sigBytes = Base64Decode(sigB64);

    if (payloadBytes.empty() || sigBytes.empty())
    {
        outInfo.errorMsg = kInvalidRegCode;
        return false;
    }

    const std::string payload(reinterpret_cast<const char*>(payloadBytes.data()), payloadBytes.size());
#ifdef LICENSE_TEST_HOOKS
    const std::string& pubKeyPem = !LicenseInternal::GetTestPublicKeyOverride().empty()
        ? LicenseInternal::GetTestPublicKeyOverride()
        : kPublicKey();
#else
    const std::string& pubKeyPem = kPublicKey();
#endif

    BIO* bio = BIO_new_mem_buf(pubKeyPem.data(), static_cast<int>(pubKeyPem.size()));
    EVP_PKEY* pubKey = PEM_read_bio_PUBKEY(bio, nullptr, nullptr, nullptr);
    BIO_free(bio);

    if (!pubKey)
    {
        outInfo.errorMsg = "Failed to load verification key";
        return false;
    }

    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    int ok = EVP_DigestVerifyInit(ctx, nullptr, EVP_sha256(), nullptr, pubKey);
    if (ok == 1)
    {
        ok = EVP_DigestVerifyUpdate(ctx, payload.data(), payload.size());
    }
    if (ok == 1)
    {
        ok = EVP_DigestVerifyFinal(ctx, sigBytes.data(), sigBytes.size());
    }

    EVP_MD_CTX_free(ctx);
    EVP_PKEY_free(pubKey);

    if (ok != 1)
    {
        outInfo.errorMsg = kInvalidRegCode;
        return false;
    }

    const auto fields = SplitString(payload, '|');
    if (fields.size() < 5)
    {
        outInfo.errorMsg = kInvalidRegCode;
        return false;
    }

    const std::string& payloadMachineCode = fields[0];
    const std::string& expiryDate = fields[1];
    const std::string& features = fields[2];
    const std::string& issueDate = fields[3];
    const std::string& customerName = fields[4];

    if (!IsValidDate(expiryDate) || !IsValidDate(issueDate))
    {
        outInfo.errorMsg = kInvalidRegCode;
        return false;
    }

    if (payloadMachineCode != machineCode)
    {
        outInfo.errorMsg = "This license is bound to a different machine";
        return false;
    }

    if (IsExpired(expiryDate))
    {
        outInfo.errorMsg = kErrExpired() + " (" + expiryDate + ")";
        return false;
    }

    // 双界校验：签发时间 ≤ 当前时间。防止未来签发的许可证被接受。
    std::string currentDate = GetCurrentDate();
    if (issueDate > currentDate)
    {
        outInfo.errorMsg = "License issue date is in the future (" + issueDate + ")";
        return false;
    }

    // features 解析：将逗号分隔的 features 字符串解析为列表。
    // 旧代码原样拷贝 features 字符串，应用从未解析为权限检查。
    outInfo.features = features;
    if (!features.empty())
    {
        // features 格式示例: "engraving,vision,nesting"
        // 应用层可通过 hasFeature() 检查具体功能权限
    }

    outInfo.isValid = true;
    outInfo.machineCode = payloadMachineCode;
    outInfo.expiryDate = expiryDate;
    outInfo.features = features;
    outInfo.issueDate = issueDate;
    outInfo.customerName = customerName;
    return true;
}

bool LicenseManager::SaveLicense(const std::string& regCode) const
{
    const std::filesystem::path configPath(m_configDir);
    std::error_code ec;
    if (!std::filesystem::exists(configPath, ec))
    {
        if (!std::filesystem::create_directories(configPath, ec))
        {
            return false;
        }
    }

    std::ofstream file(configPath / "license.key", std::ios::out | std::ios::trunc);
    if (!file)
    {
        return false;
    }
    file << regCode;
    return file.good();
}

std::string LicenseManager::ReadLicenseFile() const
{
    std::ifstream file(std::filesystem::path(m_configDir) / "license.key");
    if (!file)
    {
        return {};
    }

    std::string content;
    std::string line;
    while (std::getline(file, line))
    {
        content += line;
    }
    return content;
}