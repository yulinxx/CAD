#pragma once

#include <QString>

#include "RenderCoreApi.h"

class RENDER_CORE_API BackendConfigResolver
{
public:
    using BackendType = int;

    static BackendConfigResolver& instance();

    BackendType resolveBackendType() const;

    BackendType fromEnvironment() const;

    BackendType defaultBackendType() const;

    QString resolveBackendName() const;

private:
    BackendConfigResolver() = default;
    ~BackendConfigResolver() = default;

    BackendConfigResolver(const BackendConfigResolver&) = delete;
    BackendConfigResolver& operator=(const BackendConfigResolver&) = delete;
};
