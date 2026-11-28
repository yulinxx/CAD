#include "i18n/TextContext.h"

#include <QLatin1Char>

namespace AA
{
    QString textContextForKey(const QString& key)
    {
        // 只按第一个 '.' 切分：command.file.new 归到 command，
        // mantisoft.strTitle 归到 mantisoft，译者在 Linguist 里按模块浏览。
        const qsizetype separator = key.indexOf(QLatin1Char('.'));
        if (separator <= 0)
            return QStringLiteral("Profile");

        return key.left(separator);
    }
}  // namespace AA