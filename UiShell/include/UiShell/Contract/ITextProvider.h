#pragma once

#include <QString>

namespace AA
{
    // 文本 strId → 实际文字的只读接口
    class ITextProvider
    {
    public:
        virtual ~ITextProvider() = default;

    public:
        virtual QString text(const QString& key) const = 0;
    };
}  // namespace AA
