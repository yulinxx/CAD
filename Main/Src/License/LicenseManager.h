#pragma once

#include <memory>
#include <string>

class IOnlineVerifier;

// ============================================================
// 主开关 - 修改 return 值控制整个注册校验系统的启停
// true  = 启用注册校验（发布版本）
// false = 禁用注册校验（开发调试，无许可限制）
// ============================================================
inline bool IsLicenseCheckEnabled() { return true; }

struct LicenseInfo
{
    bool   isValid = false;
    std::string machineCode;
    std::string expiryDate;
    std::string features;
    std::string issueDate;
    std::string customerName;
    std::string errorMsg;
};

class LicenseManager
{
public:
    explicit LicenseManager(const std::string& configDir);
    ~LicenseManager();

    // ---------- 核心接口 ----------

    bool        CheckLicense();
    bool        Activate(const std::string& regCode);
    std::string GetMachineCode() const;
    LicenseInfo GetLicenseInfo() const;
    void        ClearLicense();

    // ---------- 散射校验 ----------

    // 运行时再验证（供 LicenseGuard::Check 内部调用）
    // 返回 false = 校验状态失效，需重新激活
    bool ReValidate();

    // ---------- 在线验证扩展 ----------

    void SetOnlineVerifier(std::unique_ptr<IOnlineVerifier> verifier);
    IOnlineVerifier* GetOnlineVerifier() const;

private:
    bool        LoadAndVerify();
    bool        VerifyRegCode(const std::string& machineCode, const std::string& regCode, LicenseInfo& outInfo) const;
    bool        SaveLicense(const std::string& regCode) const;
    std::string ReadLicenseFile() const;

    std::string m_configDir;
    LicenseInfo m_info;

    // 在线验证器（可空，不设置则纯离线）
    std::unique_ptr<IOnlineVerifier> m_onlineVerifier;
};
