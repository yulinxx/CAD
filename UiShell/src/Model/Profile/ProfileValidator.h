#pragma once

#include <QList>

#include "Model/Profile/ProfileLoadResult.h"

namespace RD
{
    // 对已解析模型做跨字段一致性校验（id 唯一、子区域引用、必备字段等）。
    class ProfileValidator
    {
    public:
        // 返回校验诊断列表；空列表表示模型完全合法。
        static QList<Diagnostic> validate(const ProfileModel& model);
    };
}  // namespace RD
