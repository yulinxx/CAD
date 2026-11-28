#pragma once

#include <QString>

namespace AA
{
    struct Diagnostic
    {
        enum class Level
        {
            Info,
            Warning,
            Error
        };

        Level level = Level::Error;
        QString strJsonPath;
        QString strMsg;
        int nLine = 0;     // 1 基行号，0 = 未知
        int nColumn = 0;   // 1 基列号，0 = 未知
    };
}  // namespace AA
