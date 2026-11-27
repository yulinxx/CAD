#pragma once

#include <QList>
#include <QString>

#include "Model/Profile/ProfileLoadResult.h"

namespace RD
{
    // Diagnostic 构造辅助 — 公共工具，放在命名空间中避免 unity build 下匿名命名空间冲突
    static inline Diagnostic makeDiagnostic(Diagnostic::Level level, const QString& jsonPath, const QString& message)
    {
        Diagnostic diagnostic;
        diagnostic.level = level;
        diagnostic.strJsonPath = jsonPath;
        diagnostic.strMsg = message;
        return diagnostic;
    }

    static inline void addError(QList<Diagnostic>& diagnostics, const QString& path, const QString& message)
    {
        diagnostics.append(makeDiagnostic(Diagnostic::Level::Error, path, message));
    }

    static inline void addWarning(QList<Diagnostic>& diagnostics, const QString& path, const QString& message)
    {
        diagnostics.append(makeDiagnostic(Diagnostic::Level::Warning, path, message));
    }
}  // namespace RD
