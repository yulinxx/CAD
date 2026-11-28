#pragma once

#include <QString>

#include "UiShell/Export.h"

namespace AA
{
    // profile 文本 key → Qt 翻译上下文；取第一个 '.' 之前的部分（menu.file → menu）
    UI_SHELL_API QString textContextForKey(const QString& key);
}  // namespace AA
