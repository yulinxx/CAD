#pragma once

#include "UiShell/i18n/IQtTextProvider.h"

namespace AA
{
    // 具体类不导出：调用方使用 createQtTextProvider() 返回的 IQtTextProvider*。
    class QtTextProvider final : public IQtTextProvider
    {
    public:
        QString text(const QString& key) const override;

        void destroy() override
        {
            delete this;
        }
    };
}  // namespace AA
