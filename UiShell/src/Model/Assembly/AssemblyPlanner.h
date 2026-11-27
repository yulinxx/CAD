#pragma once

#include "UiShell/Contract/FeatureFlag.h"
#include "Model/Assembly/AssemblyInfo.h"
#include "Model/Profile/ProfileModel.h"

namespace RD
{
    class ICommandCatalog;
    class ITextProvider;

    AssemblyInfo planAssembly(const ProfileModel& model,
        const FeatureFlagSet& flags,
        const ICommandCatalog* catalog = nullptr,
        const ITextProvider* text = nullptr);
}  // namespace RD
