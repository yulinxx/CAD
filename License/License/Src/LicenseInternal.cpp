#include "LicenseInternal.h"

#include <atomic>
#include <cstring>

namespace
{
    std::atomic<bool> g_checkEnabled{ false };
}

namespace LicenseInternal
{
    bool IsCheckEnabled()
    {
        return g_checkEnabled.load(std::memory_order_acquire);
    }

    void SetCheckEnabled(bool enabled)
    {
        g_checkEnabled.store(enabled, std::memory_order_release);
    }

    void CopyStringField(char* dest, size_t destSize, const std::string& value)
    {
        if (!dest || destSize == 0)
        {
            return;
        }

        std::strncpy(dest, value.c_str(), destSize - 1);
        dest[destSize - 1] = '\0';
    }

#ifdef LICENSE_TEST_HOOKS

    std::string& GetTestPublicKeyOverride()
    {
        static std::string s_override;
        return s_override;
    }

    void SetTestPublicKeyOverride(const std::string& pem)
    {
        GetTestPublicKeyOverride() = pem;
    }

    void ClearTestPublicKeyOverride()
    {
        GetTestPublicKeyOverride().clear();
    }

#endif
}  // namespace LicenseInternal