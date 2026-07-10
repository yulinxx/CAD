#include "BackendConfigResolver.h"

#include "BackendCapabilityRegistry.h"

#include <cstdlib>
#include <string>

// ============================================================================
// 全局后端类型定义（与 BackendCapabilityRegistry 保持一致）
// ============================================================================

namespace {
    constexpr int BackendType_OpenGL = 1;
    constexpr int BackendType_Vulkan = 2;
    constexpr int BackendType_Metal = 3;
    constexpr int BackendType_Software = 4;
}

// ============================================================================
// BackendConfigResolver 实现
// ============================================================================

BackendConfigResolver& BackendConfigResolver::instance()
{
    static BackendConfigResolver instance;
    return instance;
}

BackendConfigResolver::BackendType BackendConfigResolver::resolveBackendType() const
{
    BackendType type = fromEnvironment();
    if (type != 0)
    {
        return type;
    }
    return defaultBackendType();
}

BackendConfigResolver::BackendType BackendConfigResolver::fromEnvironment() const
{
    const char* envValue = std::getenv("SAN_YI_RENDER_BACKEND");
    if (envValue && envValue[0] != '\0')
    {
        BackendType type = BackendCapabilityRegistry::instance().fromName(envValue);
        return type;
    }
    return 0;
}

BackendConfigResolver::BackendType BackendConfigResolver::defaultBackendType() const
{
#ifdef __APPLE__
    return BackendType_Metal;
#else
    return BackendType_OpenGL;
#endif
}

std::string BackendConfigResolver::resolveBackendName() const
{
    BackendType type = resolveBackendType();
    return BackendCapabilityRegistry::instance().nameFor(type);
}
