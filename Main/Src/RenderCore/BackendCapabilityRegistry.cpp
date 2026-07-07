#include "BackendCapabilityRegistry.h"

#include <QString>

// ============================================================================
// 全局后端类型定义（与 RenderBackendFactory 保持一致）
// ============================================================================

namespace {
    constexpr int BackendType_OpenGL = 1;
    constexpr int BackendType_Vulkan = 2;
    constexpr int BackendType_Metal = 3;
    constexpr int BackendType_Software = 4;
}

// ============================================================================
// BackendCapabilityRegistry 实现
// ============================================================================

BackendCapabilityRegistry::BackendCapabilityRegistry()
{
    registerBackend(BackendType_OpenGL, QStringLiteral("OpenGL"),
        BackendCapability::HardwareAccelerated
        | BackendCapability::AntiAliasing
        | BackendCapability::HighDPI
        | BackendCapability::OffscreenRendering,
        true);

#ifdef _WIN32
    registerBackend(BackendType_Vulkan, QStringLiteral("Vulkan"),
        BackendCapability::HardwareAccelerated
        | BackendCapability::MultiViewport
        | BackendCapability::InstancedRendering
        | BackendCapability::ComputeShader
        | BackendCapability::AntiAliasing
        | BackendCapability::HighDPI
        | BackendCapability::OffscreenRendering,
        false);
#endif

#ifdef __APPLE__
    registerBackend(BackendType_Metal, QStringLiteral("Metal"),
        BackendCapability::HardwareAccelerated
        | BackendCapability::MultiViewport
        | BackendCapability::InstancedRendering
        | BackendCapability::ComputeShader
        | BackendCapability::AntiAliasing
        | BackendCapability::HighDPI,
        true);
#else
    registerBackend(BackendType_Metal, QStringLiteral("Metal"),
        BackendCapability::HardwareAccelerated
        | BackendCapability::MultiViewport
        | BackendCapability::InstancedRendering
        | BackendCapability::ComputeShader
        | BackendCapability::AntiAliasing
        | BackendCapability::HighDPI,
        false);
#endif

    registerBackend(BackendType_Software, QStringLiteral("Software"),
        BackendCapability::OffscreenRendering,
        true);
}

BackendCapabilityRegistry& BackendCapabilityRegistry::instance()
{
    static BackendCapabilityRegistry instance;
    return instance;
}

void BackendCapabilityRegistry::registerBackend(BackendType type, const QString& name, BackendCapability capabilities, bool available)
{
    BackendInfo info;
    info.name = name;
    info.capabilities = capabilities;
    info.available = available;

    m_backends[type] = info;
    m_nameToType[name.toLower()] = type;
}

bool BackendCapabilityRegistry::isAvailable(BackendType type) const
{
    auto it = m_backends.find(type);
    return it != m_backends.end() && it->second.available;
}

BackendCapability BackendCapabilityRegistry::capabilitiesFor(BackendType type) const
{
    auto it = m_backends.find(type);
    return it != m_backends.end() ? it->second.capabilities : BackendCapability::None;
}

QString BackendCapabilityRegistry::nameFor(BackendType type) const
{
    auto it = m_backends.find(type);
    return it != m_backends.end() ? it->second.name : QStringLiteral("Unknown");
}

QVector<BackendCapabilityRegistry::BackendType> BackendCapabilityRegistry::availableBackends() const
{
    QVector<BackendType> result;
    for (const auto& pair : m_backends)
    {
        if (pair.second.available)
        {
            result.append(pair.first);
        }
    }
    return result;
}

BackendCapabilityRegistry::BackendType BackendCapabilityRegistry::fromName(const QString& name) const
{
    auto it = m_nameToType.find(name.toLower());
    return it != m_nameToType.end() ? it->second : BackendType_OpenGL;
}

QString BackendCapabilityRegistry::toName(BackendType type) const
{
    return nameFor(type);
}
