#pragma once

#include <string>
#include <vector>
#include <unordered_map>

#include "RenderCoreApi.h"
#include "RenderTypes.h"

class RENDER_CORE_API BackendCapabilityRegistry
{
public:
    using BackendType = int;

    struct BackendInfo
    {
        std::string name;
        BackendCapability capabilities;
        bool available;
    };

    static BackendCapabilityRegistry& instance();

    void registerBackend(BackendType type, const std::string& name, BackendCapability capabilities, bool available = true);

    bool isAvailable(BackendType type) const;

    BackendCapability capabilitiesFor(BackendType type) const;

    std::string nameFor(BackendType type) const;

    std::vector<BackendType> availableBackends() const;

    BackendType fromName(const std::string& name) const;

    std::string toName(BackendType type) const;

private:
    BackendCapabilityRegistry();
    ~BackendCapabilityRegistry() = default;

    BackendCapabilityRegistry(const BackendCapabilityRegistry&) = delete;
    BackendCapabilityRegistry& operator=(const BackendCapabilityRegistry&) = delete;

    std::unordered_map<BackendType, BackendInfo> m_backends;
    std::unordered_map<std::string, BackendType> m_nameToType;
};
