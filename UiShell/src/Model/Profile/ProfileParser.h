#pragma once

#include <QByteArray>

#include "Model/Profile/ProfileLoadResult.h"

namespace AA
{
    // 从 UTF-8 JSON 文本解析 profile。
    // 只做解析与校验，不读文件、不创建控件，便于单测与复用。
    class ProfileParser
    {
    public:
        // 输入 UTF-8 JSON 字节；不创建任何 Qt Widgets 对象。
        // 允许带 BOM 与 //、/* */ 注释；出错时返回 diagnostics 且 model 为空。
        static ProfileLoadResult loadFromJson(const QByteArray& utf8Json);
    };
}  // namespace AA
