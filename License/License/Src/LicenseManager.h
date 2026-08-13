#pragma once

#include <memory>
#include <string>

struct LicenseInfoData;

class IOnlineVerifier
{
public:
    virtual ~IOnlineVerifier() = default;

    virtual bool Verify(const LicenseInfoData& info) = 0;
    virtual bool IsAvailable() const = 0;
    virtual const char* GetName() const = 0;

    virtual bool IsRequired() const
    {
        return false;
    }
};

struct LicenseInfoData
{
    bool isValid = false;
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

    bool CheckLicense();
    bool Activate(const std::string& regCode);
    std::string GetMachineCode() const;
    LicenseInfoData GetLicenseInfo() const;
    void ClearLicense();
    bool ReValidate();

    void SetOnlineVerifier(std::unique_ptr<IOnlineVerifier> verifier);
    IOnlineVerifier* GetOnlineVerifier() const;

private:
    bool LoadAndVerify();
    bool VerifyRegCode(const std::string& machineCode, const std::string& regCode, LicenseInfoData& outInfo) const;
    bool SaveLicense(const std::string& regCode) const;
    std::string ReadLicenseFile() const;

    std::string m_configDir;
    LicenseInfoData m_info;
    std::unique_ptr<IOnlineVerifier> m_onlineVerifier;
};
