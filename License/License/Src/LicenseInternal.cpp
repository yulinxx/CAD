#include "LicenseInternal.h"

#include <atomic>
#include <cstring>

namespace
{
    // [B1-P0 修复] 通过编译期宏控制默认值：
    // - SANYI_ENABLE_LICENSE=ON  → 默认启用许可校验（生产环境）
    // - 未定义或 OFF            → 默认禁用（开发/测试环境）
    // 用户仍可在运行时通过 SetCheckEnabled() 动态切换。
#ifdef SANYI_ENABLE_LICENSE
    std::atomic<bool> g_checkEnabled{ true };
#else
    std::atomic<bool> g_checkEnabled{ false };
#endif
}  // namespace

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