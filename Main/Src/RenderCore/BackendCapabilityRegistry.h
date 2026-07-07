#pragma once

#include <QString>
#include <QVector>
#include <unordered_map>

#include "RenderCoreApi.h"
#include "RenderTypes.h"

class RENDER_CORE_API BackendCapabilityRegistry
{
public:
    using BackendType = int;

    struct BackendInfo
    {
        QString name;
        BackendCapability capabilities;
        bool available;
    };

    static BackendCapabilityRegistry& instance();

    void registerBackend(BackendType type, const QString& name, BackendCapability capabilities, bool available = true);

    bool isAvailable(BackendType type) const;

    BackendCapability capabilitiesFor(BackendType type) const;

    QString nameFor(BackendType type) const;

    QVector<BackendType> availableBackends() const;

    BackendType fromName(const QString& name) const;

    QString toName(BackendType type) const;

private:
    BackendCapabilityRegistry();
    ~BackendCapabilityRegistry() = default;

    BackendCapabilityRegistry(const BackendCapabilityRegistry&) = delete;
    BackendCapabilityRegistry& operator=(const BackendCapabilityRegistry&) = delete;

    std::unordered_map<BackendType, BackendInfo> m_backends;
    std::unordered_map<QString, BackendType> m_nameToType;
};
