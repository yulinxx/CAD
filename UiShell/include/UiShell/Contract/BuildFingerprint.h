#pragma once

#include "UiShell/Export.h"
#include "UiBuildInfo.h"

namespace RD
{
    // EXE 的构建指纹
    inline const char* hostBuildFingerprint() noexcept
    {
        return UI_BUILD_FINGERPRINT;
    }

    // shell DLL 的构建指纹
    UI_SHELL_API const char* shellBuildFingerprint() noexcept;

    // 当前客户 DLL 的构建指纹
    UI_CUSTOMER_API const char* customerBuildFingerprint() noexcept;
}  // namespace RD
