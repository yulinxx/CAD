#include "License/LicenseDLL.h"
#include "LicenseGuard.h"
#include "LicenseInternal.h"
#include "LicenseManager.h"
#include "Log/SyLogger.h"

#include <cstring>
#include <memory>
#include <string>

#define LICENSE_STRINGIFY_IMPL(x) #x
#define LICENSE_STRINGIFY(x)      LICENSE_STRINGIFY_IMPL(x)

struct LicenseContext
{
    std::unique_ptr<LicenseManager> manager;
};

namespace
{
    thread_local char g_lastError[512] = {};

    void setLastError(const char* message)
    {
        if (!message)
        {
            g_lastError[0] = '\0';
            return;
        }

        std::strncpy(g_lastError, message, sizeof(g_lastError) - 1);
        g_lastError[sizeof(g_lastError) - 1] = '\0';
    }

    LicenseManager* getManager(LicenseContext* ctx)
    {
        return ctx && ctx->manager ? ctx->manager.get() : nullptr;
    }

    int copyStringToBuffer(const std::string& value, char* buffer, size_t bufferSize)
    {
        if (!buffer || bufferSize == 0)
        {
            setLastError("Output buffer is null or too small");
            return LICENSE_ERR_NULL_POINTER;
        }

        if (value.size() + 1 > bufferSize)
        {
            setLastError("Output buffer is too small");
            return LICENSE_ERR_BUFFER_TOO_SMALL;
        }

        std::memcpy(buffer, value.c_str(), value.size() + 1);
        return LICENSE_OK;
    }

    void fillLicenseInfo(LicenseInfo* outInfo, const LicenseInfoData& data)
    {
        if (!outInfo)
        {
            return;
        }

        std::memset(outInfo, 0, sizeof(LicenseInfo));
        outInfo->structSize = sizeof(LicenseInfo);
        outInfo->isValid = data.isValid ? 1 : 0;
        LicenseInternal::CopyStringField(outInfo->machineCode, sizeof(outInfo->machineCode), data.machineCode);
        LicenseInternal::CopyStringField(outInfo->expiryDate, sizeof(outInfo->expiryDate), data.expiryDate);
        LicenseInternal::CopyStringField(outInfo->features, sizeof(outInfo->features), data.features);
        LicenseInternal::CopyStringField(outInfo->issueDate, sizeof(outInfo->issueDate), data.issueDate);
        LicenseInternal::CopyStringField(outInfo->customerName, sizeof(outInfo->customerName), data.customerName);
        LicenseInternal::CopyStringField(outInfo->errorMessage, sizeof(outInfo->errorMessage), data.errorMsg);
    }

    LicenseGuard::Flavor guardFlavorFromC(LicenseGuardFlavor flavor)
    {
        switch (flavor)
        {
        case LICENSE_GUARD_STARTUP:
            return LicenseGuard::Flavor_Startup;
        case LICENSE_GUARD_SAVE:
            return LicenseGuard::Flavor_Save;
        case LICENSE_GUARD_EXPORT:
            return LicenseGuard::Flavor_Export;
        case LICENSE_GUARD_RENDER:
            return LicenseGuard::Flavor_Render;
        case LICENSE_GUARD_GENERIC:
        default:
            return LicenseGuard::Flavor_Generic;
        }
    }
}  // namespace

extern "C"
{
    uint32_t License_GetVersion(void)
    {
        return LICENSE_VERSION;
    }

    const char* License_GetVersionString(void)
    {
        static const char kVersionString[] = LICENSE_STRINGIFY(LICENSE_VERSION_MAJOR) "." LICENSE_STRINGIFY(
            LICENSE_VERSION_MINOR) "." LICENSE_STRINGIFY(LICENSE_VERSION_PATCH);
        return kVersionString;
    }

    void License_ConfigInit(LicenseConfig* config)
    {
        if (!config)
        {
            return;
        }

        std::memset(config, 0, sizeof(LicenseConfig));
        config->structSize = sizeof(LicenseConfig);
        config->enableCheck = 1;
    }

    int License_IsCheckEnabled(void)
    {
        return LicenseInternal::IsCheckEnabled() ? 1 : 0;
    }

    // [B2-P0 修复] License_SetCheckEnabled 已从公开导出表移除。
    // 保留实现仅供内部测试使用（通过 #ifdef LICENSE_TEST_HOOKS 守卫）。
#ifdef LICENSE_TEST_HOOKS
    void License_SetCheckEnabled(int enabled)
    {
        LicenseInternal::SetCheckEnabled(enabled != 0);
    }
#endif

LicenseContext* License_Create(const LicenseConfig* config)
{
    SY_INFO("[LicenseDLL] License_Create: creating license context");
    try
    {
        if (!config)
        {
            SY_ERROR("[LicenseDLL] License_Create: config is null");
            setLastError("config is null");
            return nullptr;
        }

        if (config->structSize != sizeof(LicenseConfig))
        {
            SY_ERROR("[LicenseDLL] License_Create: LicenseConfig struct size mismatch");
            setLastError("LicenseConfig struct size mismatch");
            return nullptr;
        }

        if (!config->configDir || config->configDir[0] == '\0')
        {
            SY_ERROR("[LicenseDLL] License_Create: configDir is required");
            setLastError("configDir is required");
            return nullptr;
        }

        if (config->enableCheck >= 0)
        {
            LicenseInternal::SetCheckEnabled(config->enableCheck != 0);
        }

        auto* ctx = new LicenseContext();
        ctx->manager = std::make_unique<LicenseManager>(config->configDir);
        setLastError(nullptr);
        SY_INFO("[LicenseDLL] License_Create: license context created successfully");
        return ctx;
    }
        catch (const std::exception& ex)
        {
            setLastError(ex.what());
            return nullptr;
        }
        catch (...)
        {
            setLastError("Unknown internal error");
            return nullptr;
        }
    }

void License_Destroy(LicenseContext* ctx)
{
    SY_INFO("[LicenseDLL] License_Destroy: destroying license context");
    delete ctx;
}

int License_Check(LicenseContext* ctx)
{
    SY_INFO("[LicenseDLL] License_Check: verifying license");
    try
    {
        LicenseManager* manager = getManager(ctx);
        if (!manager)
        {
            SY_ERROR("[LicenseDLL] License_Check: LicenseContext is null");
            setLastError("LicenseContext is null");
            return LICENSE_ERR_NULL_POINTER;
        }

        if (!manager->CheckLicense())
        {
            SY_WARNF("[LicenseDLL] License_Check: license verification failed: %s", manager->GetLicenseInfo().errorMsg.c_str());
            setLastError(manager->GetLicenseInfo().errorMsg.c_str());
            return LICENSE_ERR_VERIFY_FAILED;
        }

        SY_INFO("[LicenseDLL] License_Check: license verification passed");
        setLastError(nullptr);
        return LICENSE_OK;
    }
        catch (const std::exception& ex)
        {
            setLastError(ex.what());
            return LICENSE_ERR_INTERNAL;
        }
        catch (...)
        {
            setLastError("Unknown internal error");
            return LICENSE_ERR_INTERNAL;
        }
    }

int License_Activate(LicenseContext* ctx, const char* regCode)
{
    SY_INFO("[LicenseDLL] License_Activate: activating license");
    try
    {
        LicenseManager* manager = getManager(ctx);
        if (!manager)
        {
            SY_ERROR("[LicenseDLL] License_Activate: LicenseContext is null");
            setLastError("LicenseContext is null");
            return LICENSE_ERR_NULL_POINTER;
        }

        if (!regCode || regCode[0] == '\0')
        {
            SY_ERROR("[LicenseDLL] License_Activate: regCode is required");
            setLastError("regCode is required");
            return LICENSE_ERR_INVALID_ARG;
        }

        if (!manager->Activate(regCode))
        {
            SY_WARNF("[LicenseDLL] License_Activate: license activation failed: %s", manager->GetLicenseInfo().errorMsg.c_str());
            setLastError(manager->GetLicenseInfo().errorMsg.c_str());
            return LICENSE_ERR_VERIFY_FAILED;
        }

        SY_INFO("[LicenseDLL] License_Activate: license activation succeeded");
        setLastError(nullptr);
        return LICENSE_OK;
    }
        catch (const std::exception& ex)
        {
            setLastError(ex.what());
            return LICENSE_ERR_INTERNAL;
        }
        catch (...)
        {
            setLastError("Unknown internal error");
            return LICENSE_ERR_INTERNAL;
        }
    }

int License_ReValidate(LicenseContext* ctx)
{
    SY_INFO("[LicenseDLL] License_ReValidate: re-validating license");
    try
    {
        LicenseManager* manager = getManager(ctx);
        if (!manager)
        {
            SY_ERROR("[LicenseDLL] License_ReValidate: LicenseContext is null");
            setLastError("LicenseContext is null");
            return LICENSE_ERR_NULL_POINTER;
        }

        if (!manager->ReValidate())
        {
            SY_WARN("[LicenseDLL] License_ReValidate: license re-validation failed");
            setLastError("License re-validation failed");
            return LICENSE_ERR_VERIFY_FAILED;
        }

        SY_INFO("[LicenseDLL] License_ReValidate: license re-validation passed");
        setLastError(nullptr);
        return LICENSE_OK;
    }
        catch (const std::exception& ex)
        {
            setLastError(ex.what());
            return LICENSE_ERR_INTERNAL;
        }
        catch (...)
        {
            setLastError("Unknown internal error");
            return LICENSE_ERR_INTERNAL;
        }
    }

int License_Clear(LicenseContext* ctx)
{
    SY_INFO("[LicenseDLL] License_Clear: clearing license");
    try
    {
        LicenseManager* manager = getManager(ctx);
        if (!manager)
        {
            SY_ERROR("[LicenseDLL] License_Clear: LicenseContext is null");
            setLastError("LicenseContext is null");
            return LICENSE_ERR_NULL_POINTER;
        }

        manager->ClearLicense();
        SY_INFO("[LicenseDLL] License_Clear: license cleared");
        setLastError(nullptr);
        return LICENSE_OK;
    }
        catch (const std::exception& ex)
        {
            setLastError(ex.what());
            return LICENSE_ERR_INTERNAL;
        }
        catch (...)
        {
            setLastError("Unknown internal error");
            return LICENSE_ERR_INTERNAL;
        }
    }

int License_GetMachineCode(LicenseContext* ctx, char* buffer, size_t bufferSize)
{
    SY_DEBUG("[LicenseDLL] License_GetMachineCode: getting machine code");
    try
    {
        LicenseManager* manager = getManager(ctx);
        if (!manager)
        {
            SY_ERROR("[LicenseDLL] License_GetMachineCode: LicenseContext is null");
            setLastError("LicenseContext is null");
            return LICENSE_ERR_NULL_POINTER;
        }

        return copyStringToBuffer(manager->GetMachineCode(), buffer, bufferSize);
    }
        catch (const std::exception& ex)
        {
            setLastError(ex.what());
            return LICENSE_ERR_INTERNAL;
        }
        catch (...)
        {
            setLastError("Unknown internal error");
            return LICENSE_ERR_INTERNAL;
        }
    }

int License_GetInfo(LicenseContext* ctx, LicenseInfo* outInfo)
{
    SY_DEBUG("[LicenseDLL] License_GetInfo: getting license info");
    try
    {
        LicenseManager* manager = getManager(ctx);
        if (!manager)
        {
            SY_ERROR("[LicenseDLL] License_GetInfo: LicenseContext is null");
            setLastError("LicenseContext is null");
            return LICENSE_ERR_NULL_POINTER;
        }

        if (!outInfo)
        {
            SY_ERROR("[LicenseDLL] License_GetInfo: outInfo is null");
            setLastError("outInfo is null");
            return LICENSE_ERR_NULL_POINTER;
        }

        if (outInfo->structSize != 0 && outInfo->structSize != sizeof(LicenseInfo))
        {
            SY_ERROR("[LicenseDLL] License_GetInfo: LicenseInfo struct size mismatch");
            setLastError("LicenseInfo struct size mismatch");
            return LICENSE_ERR_VERSION_MISMATCH;
        }

        fillLicenseInfo(outInfo, manager->GetLicenseInfo());
        setLastError(nullptr);
        return LICENSE_OK;
    }
        catch (const std::exception& ex)
        {
            setLastError(ex.what());
            return LICENSE_ERR_INTERNAL;
        }
        catch (...)
        {
            setLastError("Unknown internal error");
            return LICENSE_ERR_INTERNAL;
        }
    }

    // [B2-P0 修复] License_GuardMarkValid 已从公开导出表移除。
    // 此函数允许任意进程绕过 Guard 防 patch 设计，仅在测试构建中可用。
#ifdef LICENSE_TEST_HOOKS
    int License_GuardMarkValid(void)
    {
        LicenseGuard::MarkValid();
        return LICENSE_OK;
    }
#endif

    int License_GuardMarkInvalid(void)
    {
        SY_INFO("[LicenseDLL] License_GuardMarkInvalid: marking license as invalid");
        LicenseGuard::MarkInvalid();
        return LICENSE_OK;
    }

    int License_GuardRefresh(void)
    {
        SY_INFO("[LicenseDLL] License_GuardRefresh: refreshing license guard");
        LicenseGuard::Refresh();
        return LICENSE_OK;
    }

    int License_GuardCheck(LicenseGuardFlavor flavor)
    {
        SY_DEBUGF("[LicenseDLL] License_GuardCheck: checking license guard (flavor=%d)", static_cast<int>(flavor));
        return LicenseGuard::Check(guardFlavorFromC(flavor)) ? LICENSE_OK : LICENSE_ERR_VERIFY_FAILED;
    }

    int License_GuardIsQuickValid(void)
    {
        SY_DEBUG("[LicenseDLL] License_GuardIsQuickValid: quick license check");
        return LicenseGuard::IsQuickValid() ? 1 : 0;
    }

    int License_GetLastErrorMessage(char* buffer, size_t bufferSize)
    {
        if (!buffer || bufferSize == 0)
        {
            return LICENSE_ERR_NULL_POINTER;
        }

        std::strncpy(buffer, g_lastError, bufferSize - 1);
        buffer[bufferSize - 1] = '\0';
        return LICENSE_OK;
    }
}