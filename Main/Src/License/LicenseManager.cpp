#include "LicenseManager.h"
#include "LicenseGuard.h"
#include "MachineFingerprint.h"
#include "OnlineVerifier.h"
#include "StrEncrypt.h"

#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/err.h>

#include <algorithm>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

namespace
{

// ---------------------------------------------------------------
// 加密的公钥（PEM 格式）- 运行期 XOR 解密后使用
// 由 Tools/encrypt_string.py 生成
// 防静态搜索：字符串不在 .rdata 中以明文出现
// ---------------------------------------------------------------
DEFINE_ENCRYPTED_STR(kPublicKey,
    0x67, 0x4A, 0x45, 0x44, 0x47, 0x46, 0x2E, 0x28, 0x29, 0x26, 0x3E, 0x51,
    0x22, 0x26, 0x36, 0x39, 0x3F, 0x34, 0x58, 0x32, 0x3F, 0x22, 0x51, 0x50,
    0x53, 0x52, 0xAD, 0x8B, 0xCF, 0xCA, 0xCD, 0xC7, 0xCF, 0xED, 0xC9, 0xC7,
    0xC8, 0xEC, 0xE7, 0xFC, 0xE6, 0xE4, 0xF9, 0xD6, 0xAB, 0xE4, 0xA4, 0xD7,
    0xD7, 0xC6, 0xDD, 0xDF, 0xDB, 0xDA, 0xD3, 0xDE, 0xDF, 0xCE, 0x98, 0xE0,
    0xEF, 0xEA, 0xED, 0xE7, 0xE5, 0xC0, 0xE3, 0xEA, 0xEB, 0xFA, 0xE9, 0xEC,
    0x9A, 0xC3, 0xD9, 0xC3, 0xD6, 0xF0, 0xD2, 0xDB, 0xD8, 0xF4, 0xFB, 0xFA,
    0xD1, 0xF6, 0xE4, 0xCE, 0xF2, 0xFE, 0xB1, 0xAA, 0xC8, 0xBA, 0x91, 0xF6,
    0xAD, 0xAE, 0xA2, 0xE2, 0x84, 0x85, 0x8D, 0xF9, 0xE1, 0xF7, 0x89, 0x87,
    0xAB, 0x98, 0x82, 0x84, 0xE0, 0x84, 0xF3, 0xBB, 0xAD, 0xAB, 0xB2, 0xF6,
    0xE6, 0x99, 0xB5, 0xB9, 0xA7, 0x8C, 0x9D, 0xAE, 0xB3, 0x84, 0xBC, 0x93,
    0xA8, 0x86, 0xA8, 0xB9, 0x81, 0xD7, 0x9E, 0xBA, 0xC1, 0x90, 0xA6, 0xB0,
    0xB9, 0x90, 0xB7, 0x8B, 0xCA, 0xA8, 0x8E, 0xAB, 0xBF, 0xBB, 0x4D, 0x57,
    0x78, 0x09, 0x65, 0x6A, 0x52, 0x51, 0x65, 0x6C, 0x46, 0x62, 0x68, 0x3C,
    0x3E, 0x75, 0x58, 0x7C, 0x6A, 0x6B, 0x73, 0x73, 0x78, 0x50, 0x79, 0x78,
    0x7D, 0x6D, 0x75, 0x29, 0x75, 0x6D, 0x41, 0x66, 0x46, 0x42, 0x14, 0x5F,
    0x6F, 0x63, 0x5A, 0x45, 0x52, 0x4C, 0x40, 0x1F, 0x61, 0x4E, 0x08, 0x44,
    0x19, 0x61, 0x78, 0x6D, 0x07, 0x4F, 0x5F, 0x5E, 0x4A, 0x0A, 0x45, 0x6A,
    0x5A, 0x7A, 0x33, 0x09, 0x76, 0x77, 0x4E, 0x7D, 0x32, 0x1D, 0x22, 0x03,
    0x04, 0x1B, 0x09, 0x38, 0x16, 0x0D, 0x36, 0x32, 0x17, 0x36, 0x2D, 0x3C,
    0x3C, 0x32, 0x6D, 0x3C, 0x2C, 0x17, 0x04, 0x28, 0x06, 0x17, 0x37, 0x2F,
    0x35, 0x17, 0x30, 0x0D, 0x14, 0x06, 0x01, 0x2C, 0x1F, 0x1E, 0x0A, 0x23,
    0x38, 0x25, 0x09, 0x38, 0x17, 0x27, 0x4C, 0x30, 0x38, 0x2F, 0x28, 0x2E,
    0x3B, 0x2F, 0x2A, 0x3C, 0x55, 0x30, 0xEA, 0xE7, 0xDB, 0xCE, 0xB3, 0x8F,
    0xA9, 0xC9, 0xE3, 0xDE, 0xC9, 0xF8, 0xBE, 0xE1, 0xEF, 0xC1, 0xE0, 0xE9,
    0xD1, 0xFB, 0xD0, 0xF7, 0xC4, 0xDA, 0xC8, 0xD0, 0xF0, 0xF3, 0xA4, 0xAD,
    0xDA, 0xD4, 0xCB, 0xCF, 0xF4, 0xF6, 0xD3, 0xC7, 0xE5, 0xEC, 0xF2, 0xDB,
    0xC1, 0xEC, 0xE0, 0x9F, 0xC7, 0xE3, 0x87, 0xE0, 0xDB, 0xE9, 0xD3, 0xEF,
    0xE7, 0x81, 0xEB, 0xE3, 0xF9, 0xF8, 0x97, 0xDA, 0xC7, 0xCD, 0xA2, 0x98,
    0x8B, 0xAD, 0xAC, 0x8F, 0xCC, 0xA1, 0xFF, 0x9B, 0xBB, 0xB2, 0xFD, 0xA3,
    0x96, 0xA4, 0xAA, 0x93, 0xFD, 0x86, 0x87, 0xB4, 0xB5, 0x86, 0x96, 0x95,
    0x80, 0x9E, 0xA4, 0x93, 0xBA, 0xA7, 0xD7, 0x83, 0xB8, 0xAB, 0xAB, 0xB0,
    0x85, 0x9F, 0x8E, 0x83, 0xD8, 0xDA, 0x94, 0x94, 0xDA, 0x9D, 0x84, 0x9D,
    0x99, 0xA2, 0x85, 0xA3, 0xB9, 0xA2, 0x88, 0xB4, 0x9E, 0xB6, 0x97, 0xAD,
    0xBD, 0xC6, 0x74, 0x68, 0x50, 0x37, 0x46, 0x55, 0x49, 0x0D, 0x78, 0x7E,
    0x43, 0x4F, 0x4D, 0x5C, 0x4F, 0x4D, 0x1A, 0x3C, 0x3F, 0x3E, 0x39, 0x38,
    0x53, 0x59, 0x5C, 0x39, 0x4A, 0x4E, 0x5E, 0x51, 0x57, 0x5C, 0x00, 0x6A,
    0x67, 0x7A, 0x09, 0x08, 0x0B, 0x0A, 0x05, 0x23, 0x20
)

// ---------------------------------------------------------------
// 加密的错误信息模板（防静态搜索关键字）
// ---------------------------------------------------------------
DEFINE_ENCRYPTED_STR(kErrNoLicense,
    0x11, 0x2A, 0x2A, 0x32, 0x25, 0x3F, 0x3C, 0x20, 0x3A, 0x27, 0x3A, 0x34,
    0x3E, 0x28, 0x1C, 0x34, 0x2A, 0x3C, 0x3A, 0x34, 0x2B
)
DEFINE_ENCRYPTED_STR(kErrExpired,
    0xA5, 0xD4, 0xD7, 0xD7, 0xDA, 0xC8, 0xCF, 0xDE, 0xC8, 0xD7, 0xD6, 0xCE,
    0xC8, 0xDC, 0xCF, 0x22, 0xCB, 0xD5, 0xD4, 0xD5, 0xD7, 0xDE, 0xD3, 0xCA
)

// ---------------------------------------------------------------
// 通用工具函数
// ---------------------------------------------------------------

std::vector<std::string> SplitString(const std::string& str, char delim)
{
    std::vector<std::string> result;
    std::istringstream stream(str);
    std::string token;
    while (std::getline(stream, token, delim))
        result.push_back(token);
    return result;
}

std::string GetCurrentDate()
{
    time_t now = time(nullptr);
    struct tm tm;
    localtime_s(&tm, &now);
    char buf[11] = {};
    strftime(buf, sizeof(buf), "%Y-%m-%d", &tm);
    return buf;
}

bool IsExpired(const std::string& expiryDate)
{
    return expiryDate < GetCurrentDate();
}

bool IsValidDate(const std::string& date)
{
    if (date.size() != 10) return false;
    if (date[4] != '-' || date[7] != '-') return false;
    for (int i = 0; i < 10; ++i)
        if (i != 4 && i != 7 && !isdigit(static_cast<unsigned char>(date[i])))
            return false;
    return true;
}

// ---------------------------------------------------------------
// Base64 / URL-safe 工具
// ---------------------------------------------------------------

std::string Base64Encode(const unsigned char* data, size_t len)
{
    int encodedLen = static_cast<int>(EVP_EncodeBlock(nullptr, data, static_cast<int>(len)));
    std::string result(encodedLen, '\0');
    EVP_EncodeBlock(reinterpret_cast<unsigned char*>(&result[0]), data, static_cast<int>(len));
    return result;
}

std::vector<unsigned char> Base64Decode(const std::string& b64)
{
    int decodedLen = static_cast<int>(EVP_DecodeBlock(nullptr,
        reinterpret_cast<const unsigned char*>(b64.data()), static_cast<int>(b64.size())));
    if (decodedLen < 0)
        return {};

    std::vector<unsigned char> result(decodedLen);
    int ret = EVP_DecodeBlock(result.data(),
        reinterpret_cast<const unsigned char*>(b64.data()), static_cast<int>(b64.size()));
    if (ret < 0)
        return {};

    size_t padding = 0;
    for (auto it = b64.rbegin(); it != b64.rend() && *it == '='; ++it)
        ++padding;
    result.resize(result.size() - padding);
    return result;
}

std::string ToUrlSafe(const std::string& b64)
{
    std::string s = b64;
    while (!s.empty() && s.back() == '=')
        s.pop_back();
    std::replace(s.begin(), s.end(), '+', '-');
    std::replace(s.begin(), s.end(), '/', '_');
    return s;
}

std::string FromUrlSafe(const std::string& urlSafe)
{
    std::string s = urlSafe;
    std::replace(s.begin(), s.end(), '-', '+');
    std::replace(s.begin(), s.end(), '_', '/');
    size_t mod = s.size() % 4;
    if (mod != 0)
        s.append(4 - mod, '=');
    return s;
}

} // anonymous namespace

// ============================================================
// LicenseManager 实现
// ============================================================

LicenseManager::LicenseManager(const std::filesystem::path& configDir)
    : m_configDir(configDir)
{
}

LicenseManager::~LicenseManager() = default;

bool LicenseManager::CheckLicense()
{
    if (!IsLicenseCheckEnabled())
    {
        m_info.isValid = true;
        LicenseGuard::MarkValid();
        return true;
    }

    if (!LoadAndVerify())
        return false;

    // 可选在线验证
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

    // 通知散射校验器
    LicenseGuard::MarkValid();
    return true;
}

bool LicenseManager::Activate(const std::string& regCode)
{
    if (!IsLicenseCheckEnabled())
    {
        m_info.isValid = true;
        LicenseGuard::MarkValid();
        return true;
    }

    std::string machineCode = GetMachineCode();
    if (machineCode.empty())
    {
        m_info.errorMsg = "Failed to generate machine code";
        return false;
    }

    LicenseInfo info;
    if (!VerifyRegCode(machineCode, regCode, info))
    {
        m_info = info;
        return false;
    }

    if (!SaveLicense(regCode))
    {
        m_info.errorMsg = "Failed to save license file";
        return false;
    }

    m_info = info;
    LicenseGuard::MarkValid();
    return true;
}

std::string LicenseManager::GetMachineCode() const
{
    return MachineFingerprint::Generate();
}

LicenseInfo LicenseManager::GetLicenseInfo() const
{
    return m_info;
}

void LicenseManager::ClearLicense()
{
    m_info = {};
    LicenseGuard::MarkInvalid();
    std::error_code ec;
    std::filesystem::remove(m_configDir / "license.key", ec);
}

bool LicenseManager::ReValidate()
{
    if (!IsLicenseCheckEnabled())
        return true;

    if (!m_info.isValid)
    {
        // 尝试重新加载
        return CheckLicense();
    }

    // 在线验证（如配置）
    if (m_onlineVerifier && m_onlineVerifier->IsAvailable())
    {
        if (!m_onlineVerifier->Verify(m_info))
        {
            if (m_onlineVerifier->IsRequired())
                return false;
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

// ============================================================
// 私有方法
// ============================================================

bool LicenseManager::LoadAndVerify()
{
    std::string regCode = ReadLicenseFile();
    if (regCode.empty())
    {
        m_info.errorMsg = kErrNoLicense();
        return false;
    }

    std::string machineCode = GetMachineCode();
    if (machineCode.empty())
    {
        m_info.errorMsg = "Failed to generate machine code";
        return false;
    }

    return VerifyRegCode(machineCode, regCode, m_info);
}

bool LicenseManager::VerifyRegCode(const std::string& machineCode,
                                   const std::string& regCode,
                                   LicenseInfo& outInfo) const
{
    auto dotPos = regCode.find('.');
    if (dotPos == std::string::npos)
    {
        outInfo.errorMsg = "Invalid registration code format (missing separator)";
        return false;
    }

    std::string payloadB64Url = regCode.substr(0, dotPos);
    std::string sigB64Url = regCode.substr(dotPos + 1);

    std::string payloadB64 = FromUrlSafe(payloadB64Url);
    std::string sigB64 = FromUrlSafe(sigB64Url);

    auto payloadBytes = Base64Decode(payloadB64);
    auto sigBytes = Base64Decode(sigB64);

    if (payloadBytes.empty() || sigBytes.empty())
    {
        outInfo.errorMsg = "Failed to decode registration code";
        return false;
    }

    std::string payload(reinterpret_cast<const char*>(payloadBytes.data()), payloadBytes.size());

    // 解密公钥并验签
    const std::string& pubKeyPem = kPublicKey();

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
        ok = EVP_DigestVerifyUpdate(ctx, payload.data(), payload.size());
    if (ok == 1)
        ok = EVP_DigestVerifyFinal(ctx, sigBytes.data(), sigBytes.size());

    EVP_MD_CTX_free(ctx);
    EVP_PKEY_free(pubKey);

    if (ok != 1)
    {
        unsigned long err = ERR_get_error();
        char errBuf[256];
        ERR_error_string_n(err, errBuf, sizeof(errBuf));
        outInfo.errorMsg = "Signature verification failed: ";
        outInfo.errorMsg += errBuf;
        return false;
    }

    auto fields = SplitString(payload, '|');
    if (fields.size() < 5)
    {
        outInfo.errorMsg = "Invalid license payload format";
        return false;
    }

    std::string payloadMachineCode = fields[0];
    std::string expiryDate         = fields[1];
    std::string features           = fields[2];
    std::string issueDate          = fields[3];
    std::string customerName       = fields[4];

    if (!IsValidDate(expiryDate) || !IsValidDate(issueDate))
    {
        outInfo.errorMsg = "Invalid date format in license";
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

    outInfo.isValid      = true;
    outInfo.machineCode  = payloadMachineCode;
    outInfo.expiryDate   = expiryDate;
    outInfo.features     = features;
    outInfo.issueDate    = issueDate;
    outInfo.customerName = customerName;
    return true;
}

bool LicenseManager::SaveLicense(const std::string& regCode) const
{
    std::error_code ec;
    if (!std::filesystem::exists(m_configDir, ec))
    {
        if (!std::filesystem::create_directories(m_configDir, ec))
            return false;
    }

    std::ofstream file(m_configDir / "license.key", std::ios::out | std::ios::trunc);
    if (!file)
        return false;
    file << regCode;
    return file.good();
}

std::string LicenseManager::ReadLicenseFile() const
{
    std::ifstream file(m_configDir / "license.key");
    if (!file)
        return {};

    std::string content, line;
    while (std::getline(file, line))
        content += line;

    return content;
}
