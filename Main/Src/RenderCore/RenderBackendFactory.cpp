#include "RenderBackendFactory.h"

#include "IRenderBackend.h"
#include "DefaultRenderBackend.h"
#include "BackendCapabilityRegistry.h"
#include "BackendConfigResolver.h"

#include <string>

// ============================================================================
// 类型转换辅助函数
// ============================================================================

namespace
{
    constexpr int BackendType_OpenGL = 1;
    constexpr int BackendType_Vulkan = 2;
    constexpr int BackendType_Metal = 3;
    constexpr int BackendType_Software = 4;
}

int RenderBackendFactory::toRegistryType(BackendType type)
{
    switch (type)
    {
        case BackendType::OpenGL:   return BackendType_OpenGL;
        case BackendType::Vulkan:   return BackendType_Vulkan;
        case BackendType::Metal:    return BackendType_Metal;
        case BackendType::Software: return BackendType_Software;
        default:                    return BackendType_OpenGL;
    }
}

RenderBackendFactory::BackendType RenderBackendFactory::fromRegistryType(int type)
{
    switch (type)
    {
        case BackendType_OpenGL:   return BackendType::OpenGL;
        case BackendType_Vulkan:   return BackendType::Vulkan;
        case BackendType_Metal:    return BackendType::Metal;
        case BackendType_Software: return BackendType::Software;
        default:                   return BackendType::OpenGL;
    }
}

// ============================================================================
// 工厂方法（核心创建逻辑）
// ============================================================================

std::unique_ptr<IRenderBackend> RenderBackendFactory::create(BackendType type)
{
    BackendCapability capabilities = capabilitiesFor(type);
    std::string name = backendTypeName(type);
    return std::make_unique<DefaultRenderBackend>(name, capabilities);
}

// ============================================================================
// 委托给 BackendCapabilityRegistry 的方法
// ============================================================================

std::vector<RenderBackendFactory::BackendType> RenderBackendFactory::availableBackends()
{
    auto& registry = BackendCapabilityRegistry::instance();
    std::vector<BackendType> result;
    for (int type : registry.availableBackends())
    {
        result.push_back(fromRegistryType(type));
    }
    return result;
}

std::string RenderBackendFactory::backendTypeName(BackendType type)
{
    return BackendCapabilityRegistry::instance().nameFor(toRegistryType(type));
}

BackendCapability RenderBackendFactory::capabilitiesFor(BackendType type)
{
    return BackendCapabilityRegistry::instance().capabilitiesFor(toRegistryType(type));
}

RenderBackendFactory::BackendType RenderBackendFactory::fromString(const std::string& name)
{
    int registryType = BackendCapabilityRegistry::instance().fromName(name);
    return fromRegistryType(registryType);
}

std::string RenderBackendFactory::toString(BackendType type)
{
    return backendTypeName(type);
}

// ============================================================================
// 委托给 BackendConfigResolver 的方法
// ============================================================================

RenderBackendFactory::BackendType RenderBackendFactory::defaultBackendType()
{
    int registryType = BackendConfigResolver::instance().defaultBackendType();
    return fromRegistryType(registryType);
}

RenderBackendFactory::BackendType RenderBackendFactory::backendFromEnvironment()
{
    int registryType = BackendConfigResolver::instance().fromEnvironment();
    return fromRegistryType(registryType);
}

std::unique_ptr<IRenderBackend> RenderBackendFactory::createConfigured()
{
    int registryType = BackendConfigResolver::instance().resolveBackendType();
    BackendType type = fromRegistryType(registryType);
    return create(type);
}