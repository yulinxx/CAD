#pragma once

#include <QCoreApplication>
#include <QString>
#include <QStringList>

namespace AA
{
    // 命令行语言参数有两种等价写法：--lang <id> 与 --lang=<id>。
    // 入口装载翻译与运行期判断“是否显式指定语言”必须共用这一份解析规则，
    // 否则其中一处会静默忽略另一种写法。
    inline QString requestedLanguageArgument(const QStringList& args)
    {
        const QString joinedPrefix = QStringLiteral("--lang=");
        for (int i = 1; i < args.size(); ++i)
        {
            const QString& arg = args.at(i);
            if (arg.startsWith(joinedPrefix))
                return arg.mid(joinedPrefix.size());
            if (arg == QStringLiteral("--lang") && i + 1 < args.size())
                return args.at(i + 1);
        }
        return QString();
    }

    inline QString requestedLanguageArgument()
    {
        return requestedLanguageArgument(QCoreApplication::arguments());
    }

    inline bool hasExplicitLanguageArgument(const QStringList& args)
    {
        return !requestedLanguageArgument(args).isEmpty();
    }

    inline bool hasExplicitLanguageArgument()
    {
        return hasExplicitLanguageArgument(QCoreApplication::arguments());
    }
} // namespace AA
