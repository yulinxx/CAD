#pragma once

#include <QString>

namespace RD
{
    // profile 文本 key → Qt 翻译上下文。
    // 规则：取第一个 '.' 之前的部分（menu.file → menu，mantisoft.title → mantisoft）。
    // shell 内部纯函数（QtTextProvider 使用），不导出。
    QString textContextForKey(const QString& key);
}  // namespace RD
