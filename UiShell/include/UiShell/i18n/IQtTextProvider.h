#pragma once

#include "UiShell/Export.h"
#include "UiShell/Contract/IApiObject.h"
#include "UiShell/Contract/ITextProvider.h"

namespace AA
{
    // Qt 翻译实现接口
    class IQtTextProvider : public ITextProvider, public IApiObject
    {
    protected:
        // 只能经 destroy()释放，禁止跨模块直接 delete
        ~IQtTextProvider() override = default;
    };

    UI_SHELL_API IQtTextProvider* createQtTextProvider();
}  // namespace AA
