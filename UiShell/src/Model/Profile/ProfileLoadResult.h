#pragma once

#include <QList>
#include <QString>
#include <optional>

#include "Model/Profile/ProfileModel.h"
#include "UiShell/Contract/Diagnostic.h"

namespace RD
{
    // 加载结果：只有没有任何 Error 时 model 才有值，Warning 不影响产出。
    // Model 层内部结构体，不跨 DLL 边界。
    struct ProfileLoadResult
    {
        std::optional<ProfileModel> profileModel;
        QList<Diagnostic> diagnostics;

        bool isOk() const
        {
            return profileModel.has_value();
        }
    };
}  // namespace RD
